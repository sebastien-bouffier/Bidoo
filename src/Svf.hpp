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
