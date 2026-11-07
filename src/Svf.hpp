#pragma once
#include <cmath>

// Linear up to 8 V, then bends to stay under 10 V (9.95 V at most).
inline float bidooSoftLimit(float x) {
	float a = std::fabs(x);
	if (a <= 8.0f)
		return x;
	return std::copysign(8.0f + 1.95f * std::tanh((a - 8.0f) * 0.5f), x);
}

// Prewarped gain of a TPT filter, the cutoff kept under Nyquist.
inline float svfGain(float freq, float sampleRate) {
	freq = std::fmin(std::fmax(freq, 1.0f), 0.45f * sampleRate);
	return std::tan(float(M_PI) * freq / sampleRate);
}

// Zavalishin's state-variable filter (Will Pirkle's courses), one channel.
struct Svf {
	float g = 0.0f, R = 1.0f, h = 1.0f;
	float hp = 0.0f, bp = 0.0f, lp = 0.0f, mem1 = 0.0f, mem2 = 0.0f;

	void set(float gain, float q) {
		g = gain;
		R = 1.0f / (2.0f * q);
		h = 1.0f / (1.0f + 2.0f * R * g + g * g);
	}

	void process(float x) {
		hp = (x - (2.0f * R + g) * mem1 - mem2) * h;
		bp = g * hp + mem1;
		lp = g * bp + mem2;
		mem1 = g * hp + bp;
		mem2 = g * bp + lp;
	}
};

// The gain of a cutoff, worked out again only when the cutoff or rate moves.
struct SvfGainCache {
	float freq = -1.0f, rate = 0.0f, g = 0.0f;

	float get(float f, float sampleRate) {
		if (f != freq || sampleRate != rate) {
			freq = f;
			rate = sampleRate;
			g = svfGain(f, sampleRate);
		}
		return g;
	}
};

// 2x up / down sampler: a polyphase IIR halfband (two chains of first-order
// allpasses in z^-2, Laurent de Soras' hiir design), 6 coefficients: flat to
// 0.21 fs (20 kHz at 48 kHz), -95 dB from 0.29 fs. Six multiplies each way.
struct Halfband2x {
	struct Chain {
		float x[3] = {}, y[3] = {};
		float process(float in, const float *k) {
			for (int i = 0; i < 3; i++) {
				float out = k[i] * (in - y[i]) + x[i];
				x[i] = in;
				y[i] = out;
				in = out;
			}
			return in;
		}
	};
	Chain upA, upB, downA, downB;

	static const float *coefA() {
		static const float k[3] = {0.045362164f, 0.337149688f, 0.708064136f};
		return k;
	}
	static const float *coefB() {
		static const float k[3] = {0.168087481f, 0.522377854f, 0.897445591f};
		return k;
	}

	// one sample in, two out (in time order)
	void up(float in, float &first, float &second) {
		first = upA.process(in, coefA());
		second = upB.process(in, coefB());
	}

	// two samples in (in time order), one out
	float down(float first, float second) {
		return 0.5f * (downA.process(second, coefA()) + downB.process(first, coefB()));
	}

	void reset() {
		*this = Halfband2x();
	}
};
