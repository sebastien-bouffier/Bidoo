#pragma once
#include <rack.hpp>
#include <vector>
#include "Svf.hpp"
#include "dep/bidoo-dsp/bidoo/dsp/dynamics/Dynamics.h"

// Shared by BAR (stereo, linked) and MINIBAR (one compressor per channel).
// 5 V = 0 dBFS.

inline float barToDb(float v) {
	return std::max(20.0f * std::log10(std::fabs(v) * 0.2f + 1e-9f), -96.3f);
}

// log2 within 0.0013 (0.008 dB): exponent bits plus a cubic on the mantissa
inline float barFastLog2(float x) {
	union { float f; uint32_t i; } v { x };
	float e = (float)((int)((v.i >> 23) & 255) - 127);
	v.i = (v.i & 0x007FFFFF) | 0x3F800000;
	float m = v.f;  // [1, 2)
	return e + ((0.15824871f * m - 1.05187502f) * m + 3.04788335f) * m - 2.15419564f;
}

// barToDb with barFastLog2 (20 log10(x / 5) = 6.0206 log2(x) - 13.979)
inline float barToDbFast(float v) {
	return std::max(6.02059991f * barFastLog2(std::fabs(v) + 1e-9f) - 13.97940009f, -96.3f);
}

// The level the compressor sees: |x| at once on the way up, then a fall with
// a time constant of the release (at most 25 ms) on the way down. Continuous:
// the release works from 1 ms, and a slow release still smooths the ripple
// of low notes between their peaks.
struct BarPeakHold {
	float level = 0.0f;

	float process(float x, float fall) {
		float a = std::fabs(x);
		level = a > level ? a : level * fall;
		return level;
	}
};

// Knobs read once per sample, coefficients worked out only when they move.
struct BarSettings {
	float threshold = 0.0f, ratio = 1.0f, attack = 10.0f, release = 10.0f, knee = 6.0f;
	float makeup = 0.0f, mix = 1.0f, lookAhead = 0.0f, rate = 0.0f;
	float cAtt = 0.0f, cRel = 0.0f, slope = 0.0f, fall = 0.0f;
	int delay = 0;
	// make-up (dB) and mix, gliding (10 ms) to the knobs
	float makeupNow = 0.0f, mixNow = 1.0f, cGlide = 0.0f;
	bool primed = false;
	// below this linear level (knee included) the compressor does nothing
	float quietBelow = 0.0f;
	float lastThreshold = 1.0f, lastKnee = -1.0f;
	// baR: the top of the RATIO knob is a true limit (infinite ratio); MINIBAR keeps 20:1
	bool infiniteTop = false;
	float ratioTop = 20.0f;
	bool limit = false;
	// the threshold as a level (5 V = 0 dB), and the attack in samples (at least 1)
	float thresholdLin = 5.0f;
	int attackSamples = 1;

	void update(float th, float ra, float at, float re, float kn, float mu, float mx, float la, float sampleRate) {
		threshold = th; ratio = ra; knee = kn; makeup = mu; mix = mx;
		limit = infiniteTop && ratio >= ratioTop;
		slope = limit ? -1.0f : 1.0f / ratio - 1.0f;
		if (th != lastThreshold || kn != lastKnee) {
			lastThreshold = th;
			lastKnee = kn;
			thresholdLin = 5.0f * std::pow(10.0f, th / 20.0f);
			// 5 V = 0 dB, 0.5 dB of margin under the knee
			quietBelow = 5.0f * std::pow(10.0f, (th - 0.5f * kn - 0.5f) / 20.0f);
		}
		if (at != attack || re != release || la != lookAhead || sampleRate != rate) {
			attack = at; release = re; lookAhead = la; rate = sampleRate;
			cAtt = std::exp(-1.0f / (attack * rate * 0.001f));
			cRel = std::exp(-1.0f / (release * rate * 0.001f));
			fall = std::exp(-1.0f / (std::min(release, 25.0f) * rate * 0.001f));
			cGlide = 1.0f - std::exp(-1.0f / (0.01f * rate));
			// lookahead in % of the attack time
			delay = (int)(lookAhead * 0.01f * attack * 0.001f * rate);
			attackSamples = std::max(1, (int)(attack * 0.001f * rate));
		}
		if (!primed) {
			makeupNow = makeup;
			mixNow = mix;
			primed = true;
		}
	}

	// once per sample
	void glide() {
		makeupNow += (makeup - makeupNow) * cGlide;
		mixNow += (mix - mixNow) * cGlide;
	}
};

// Gain reduction in dB, smoothed with attack and release.
struct BarGain {
	float reduction = 0.0f;

	float process(float levelDb, const BarSettings &s) {
		float dist = levelDb - s.threshold;
		float target;
		if (dist <= -0.5f * s.knee)
			target = 0.0f;
		else if (dist < 0.5f * s.knee)
			target = s.slope * (dist + s.knee * 0.5f) * (dist + s.knee * 0.5f) / (2.0f * s.knee);
		else
			target = s.slope * dist;
		float c = target < reduction ? s.cAtt : s.cRel;
		reduction = c * reduction + (1.0f - c) * target;
		return reduction;
	}
};

// The lookahead delay. A new length fades in over 10 ms instead of jumping.
struct BarDelay {
	std::vector<float> buf = std::vector<float>(1, 0.0f);
	int w = 0, cur = 0, next = 0;
	float x = 1.0f;
	bool primed = false;

	// room for 200% of a 100 ms attack
	void resize(float sampleRate) {
		buf.assign((size_t)(0.2f * sampleRate) + 2, 0.0f);
		w = 0;
		cur = next = std::min(cur, (int)buf.size() - 1);
		x = 1.0f;
		primed = false;
	}

	// the lengths being read now (two while a new length fades in)
	int longest() const { return std::max(cur, next); }
	int shortest() const { return std::min(cur, next); }

	float read(int d) const {
		int n = buf.size(), i = w - d;
		return buf[i < 0 ? i + n : i];
	}

	float process(float in, int target, float fadeStep) {
		int n = buf.size();
		buf[w] = in;
		target = rack::math::clamp(target, 0, n - 1);
		// the first length (new module, loaded patch) applies at once
		if (!primed) {
			cur = next = target;
			primed = true;
		}
		if (x >= 1.0f && target != cur) {
			next = target;
			x = 0.0f;
		}
		float out = read(cur);
		if (x < 1.0f) {
			out += (read(next) - out) * x;
			x += fadeStep;
			if (x >= 1.0f) {
				cur = next;
				x = 1.0f;
			}
		}
		w = w + 1 < n ? w + 1 : 0;
		return out;
	}
};

// baR's peak catcher, for the limit (infinite ratio) with lookahead: the gain that keeps
// the delayed audio under the threshold, whatever comes in. The level's maximum over the
// lookahead (bidoo-dsp's SlidingMax), its gain to the threshold, a release that lets go
// with the release time, then a moving average no longer than the lookahead. The average
// ramps the gain down over its length (the attack, or the whole lookahead when shorter), and
// every gain it averages already covers the peak, so the ramp ends on the peak's own gain
// as the peak leaves the delay: no overshoot, single samples included.
// Gain held   : longest delay + 1 samples (the peak stays in view until it leaves the delay)
// Ramp length : at most shortest delay + 1 samples (it ends before the peak comes out)
struct BarCatcher {
	bidoo::dsp::SlidingMax peak;
	std::vector<float> ring = std::vector<float>(2, 1.0f);
	double sum = 0.0, invLen = 1.0;
	int w = 0, len = 0;
	float held = 1.0f;
	bool running = false;

	// as BarDelay::resize: room for 200% of a 100 ms attack
	void resize(float sampleRate) {
		int cap = (int)(0.2f * sampleRate) + 2;
		peak.prepare(cap + 1);
		peak.setLength(1);
		ring.assign((size_t)cap + 2, 1.0f);
		reset();
	}

	// starts again from rest (unity gain, nothing seen)
	void reset() {
		peak.reset();
		std::fill(ring.begin(), ring.end(), 1.0f);
		sum = 0.0;
		w = len = 0;
		held = 1.0f;
		running = false;
	}

	// level: the detector's input now (linear, positive); returns the gain (<= 1) for the
	// audio coming out of the delay now
	float process(float level, float ceiling, int holdLength, int rampLength, float cRel) {
		int n = (int)ring.size();
		peak.setLength(holdLength);
		float h = peak.process(level);
		float need = h > ceiling ? ceiling / h : 1.0f;
		// down at once, up with the release
		held = need < held ? need : need + cRel * (held - need);
		rampLength = rack::math::clamp(rampLength, 1, n - 1);
		if (rampLength != len) {
			// a new length (a knob moved): the sum of the last values, at once
			len = rampLength;
			invLen = 1.0 / len;
			sum = 0.0;
			for (int k = 1; k <= len - 1; k++) {
				int i = w - k;
				sum += ring[i < 0 ? i + n : i];
			}
		}
		else {
			int i = w - len;
			sum -= ring[i < 0 ? i + n : i];
		}
		ring[w] = held;
		sum += held;
		w = w + 1 < n ? w + 1 : 0;
		return std::min((float)(sum * invLen), 1.0f);
	}
};

// RMS (10 ms) and VU (340 ms) levels, block peak and falling peak, in dBFS.
// The dB values are refreshed every 64 samples for the display.
struct BarMeter {
	float msRms = 0.0f, msVu = 0.0f, blockPeak = 0.0f;
	float rmsDb = -96.3f, vuDb = -96.3f, inDb = -96.3f, peakDb = -96.3f;
	int n = 0;
	// coefficients, worked out when the rate changes
	float rate = 0.0f, cRms = 0.0f, cVu = 0.0f, peakFall = 0.0f;

	void process(float x, float sampleRate) {
		if (sampleRate != rate) {
			rate = sampleRate;
			cRms = 1.0f / (0.0107f * sampleRate);
			cVu = 1.0f / (0.341f * sampleRate);
			peakFall = 50.0f * 64.0f / sampleRate;
		}
		float x2 = x * x;
		msRms += (x2 - msRms) * cRms;
		msVu += (x2 - msVu) * cVu;
		blockPeak = std::max(blockPeak, std::fabs(x));
		if (++n >= 64) {
			n = 0;
			rmsDb = barToDb(std::sqrt(msRms));
			vuDb = barToDb(std::sqrt(msVu));
			inDb = barToDb(blockPeak);
			blockPeak = 0.0f;
			peakDb = inDb > peakDb ? inDb : std::max(peakDb - peakFall, -96.3f);
		}
	}

	void clear() {
		float r = rate, a = cRms, b = cVu, c = peakFall;
		*this = BarMeter();
		rate = r; cRms = a; cVu = b; peakFall = c;
	}
};

// dB to a linear gain
inline float barDbToGain(float db) {
	return rack::dsp::exp2_taylor5(db * 0.16609640474f);
}
