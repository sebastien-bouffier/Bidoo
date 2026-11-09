// dOSE's sound, without the ports: the envelope curve and the polyphonic VCA. The module (src/DOSE.cpp) hands it the
// port voltages; tests can drive it with nothing but Rack's simd headers.
#pragma once
#include <algorithm>
#include <cmath>
#include <simd/Vector.hpp>
#include <simd/functions.hpp>

namespace dose {

using rack::simd::float_4;

// The curve: y = (e^(k x) - 1) / (e^k - 1), on x = ENV / 10 V in 0..1, with k = -curve * kMax.
//   curve 0     k = 0: y = x, the envelope as it is
//   curve -100% k = +kMax: exponential, low levels pushed down (-60 dB at x = 0.17): a shorter, snappier envelope
//   curve +100% k = -kMax: its mirror, y = 1 - f(1 - x), low levels lifted: a longer envelope
// Written as y = x h(k x) / h(k) with h(z) = (e^z - 1) / z, so that k = 0 needs no division by zero: h is a series
// near 0 and exactly 1 at 0, so y == x there, and y(0) = 0, y(1) = 1 exactly for every k.
// A negative k is computed as the mirror of the positive one, 1 - f(1 - x, -k), which is the same function: that way
// exp only sees positive arguments, and where the long curve flattens near the top (slope e^-8) the float rounding of
// 1 - small stays monotonic, where the direct form wobbled by a few 1e-7 at the series/exp seam.
static constexpr float kMax = 8.f;
static constexpr float kSeriesBelow = 0.3f;   // z under this: the series (error under 1e-8), above: exp

// h(z) = (e^z - 1) / z by its series, for 0 <= z < kSeriesBelow; 1 at z = 0
inline float_4 expm1OverZSeries(float_4 z) {
	return 1.f + z * (1.f / 2.f + z * (1.f / 6.f + z * (1.f / 24.f + z * (1.f / 120.f
		+ z * (1.f / 720.f + z * (1.f / 5040.f + z * (1.f / 40320.f)))))));
}

// x in 0..1, k = -curve * kMax
inline float_4 shape(float_4 x, float_4 k) {
	const float_4 mirror = k < 0.f;
	const float_4 a = rack::simd::abs(k);
	const float_4 u = rack::simd::ifelse(mirror, 1.f - x, x);
	const float_4 au = a * u;
	// a small: u h(a u) / h(a), all series. Otherwise (e^(a u) - 1) / (e^a - 1), the top by its series while a u is small
	const float_4 smallA = a < kSeriesBelow, smallAu = au < kSeriesBelow;
	const float_4 sAu = expm1OverZSeries(au);
	const float_4 top = rack::simd::ifelse(smallA, u * sAu, rack::simd::ifelse(smallAu, au * sAu, rack::simd::exp(au) - 1.f));
	const float_4 bottom = rack::simd::ifelse(smallA, expm1OverZSeries(a), rack::simd::exp(a) - 1.f);
	const float_4 v = top / bottom;
	return rack::simd::ifelse(mirror, 1.f - v, v);
}

struct Engine {
	static constexpr int kMaxChannels = 16;
	static constexpr float kVolts = 10.f;        // ENV: 0..10 V is a gain of 0..1; ENV OUT: the shaped gain x 10 V
	static constexpr float kCvRange = 0.2f;      // CURVE CV: +-5 V spans the knob's whole range (-1..1)
	static constexpr float kSmoothSeconds = 0.005f;

	// what the module gives: port voltages (16 floats each, zero beyond their channel counts) and the knobs
	struct Io {
		const float *in = nullptr;
		int inChannels = 0;
		const float *env = nullptr;
		int envChannels = 0;                     // 0: unplugged, gain 1
		const float *cv = nullptr;
		int cvChannels = 0;
		float curveKnob = 0.f;                   // -1..1
		float cvAmount = 0.f;                    // -1..1
		float levelKnob = 1.f;                   // 0..2
		// results
		float out[kMaxChannels] = {};
		int outChannels = 1;
		float envOut[kMaxChannels] = {};
		int envOutChannels = 1;
		float mix = 0.f;
	};

	float coef = 1.f;
	float curve = 0.f, level = 1.f;              // the knobs, smoothed
	bool first = true;
	// for the screen: the last curve amount of channel 1, and each channel's level before and after the curve
	float shownCurve = 0.f;
	float shownX[kMaxChannels] = {}, shownY[kMaxChannels] = {};
	int shownChannels = 0;

	void setSampleRate(float fs) {
		coef = 1.f - std::exp(-1.f / (kSmoothSeconds * fs));
	}

	void reset() {
		first = true;
	}

	void process(Io &io) {
		if (first) {
			curve = io.curveKnob;
			level = io.levelKnob;
			first = false;
		}
		curve += (io.curveKnob - curve) * coef;
		level += (io.levelKnob - level) * coef;

		io.outChannels = std::max(1, io.inChannels);
		io.envOutChannels = std::max(1, std::max(io.envChannels, io.inChannels));
		const int n = io.envOutChannels;
		const float_4 zero = 0.f;
		const float cvGain = io.cvAmount * kCvRange;
		float_4 mix = 0.f;
		for (int c = 0; c < n; c += 4) {
			float_4 x;
			if (io.envChannels == 0)
				x = 1.f;
			else {
				const float_4 v = io.envChannels == 1 ? float_4(io.env[0]) : float_4::load(io.env + c);
				x = rack::simd::clamp(v * (1.f / kVolts), zero, 1.f);   // a NaN becomes 0
			}
			float_4 s = curve;
			if (io.cvChannels > 0) {
				const float_4 cv = io.cvChannels == 1 ? float_4(io.cv[0]) : float_4::load(io.cv + c);
				s = rack::simd::clamp(s + cv * cvGain, -1.f, 1.f);      // a NaN becomes -1
			}
			const float_4 k = -kMax * s;
			float_4 y = shape(x, k);
			float_4 out = float_4::load(io.in + c) * (y * level);
			out.store(io.out + c);
			(y * kVolts).store(io.envOut + c);
			x.store(shownX + c);
			y.store(shownY + c);
			if (c == 0)
				shownCurve = s[0];
			mix += out;
		}
		// channels beyond IN's are zero in `in`, so they add nothing to the sum
		io.mix = mix[0] + mix[1] + mix[2] + mix[3];
		shownChannels = n;
	}
};

} // namespace dose
