#pragma once
#include <rack.hpp>
#include <vector>
#include "Svf.hpp"

// Shared by BAR (stereo, linked) and MINIBAR (one compressor per channel).
// 5 V = 0 dBFS.

inline float barToDb(float v) {
	return std::max(20.0f * std::log10(std::fabs(v) * 0.2f + 1e-9f), -96.3f);
}

// The loudest |x| over the last 25 to 50 ms (two 25 ms buckets): a 20 Hz sine
// peaks twice per bucket, so the level holds steady between its zero
// crossings instead of the gain following the waveform.
struct BarPeakHold {
	float cur = 0.0f, prev = 0.0f;
	int count = 0;

	float process(float x, int bucket) {
		cur = std::max(cur, std::fabs(x));
		if (++count >= bucket) {
			prev = cur;
			cur = 0.0f;
			count = 0;
		}
		return std::max(prev, cur);
	}
};

// Knobs read once per sample, coefficients worked out only when they move.
struct BarSettings {
	float threshold = 0.0f, ratio = 1.0f, attack = 10.0f, release = 10.0f, knee = 6.0f;
	float makeup = 0.0f, mix = 1.0f, lookAhead = 0.0f, rate = 0.0f;
	float cAtt = 0.0f, cRel = 0.0f, slope = 0.0f;
	int bucket = 1200, delay = 0;

	void update(float th, float ra, float at, float re, float kn, float mu, float mx, float la, float sampleRate) {
		threshold = th; ratio = ra; knee = kn; makeup = mu; mix = mx;
		slope = 1.0f / ratio - 1.0f;
		if (at != attack || re != release || la != lookAhead || sampleRate != rate) {
			attack = at; release = re; lookAhead = la; rate = sampleRate;
			cAtt = std::exp(-1.0f / (attack * rate * 0.001f));
			cRel = std::exp(-1.0f / (release * rate * 0.001f));
			bucket = std::max(1, (int)(0.025f * rate));
			// lookahead in % of the attack time
			delay = (int)(lookAhead * 0.01f * attack * 0.001f * rate);
		}
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

// RMS (10 ms) and VU (340 ms) levels, block peak and falling peak, in dBFS.
// The dB values are refreshed every 64 samples for the display.
struct BarMeter {
	float msRms = 0.0f, msVu = 0.0f, blockPeak = 0.0f;
	float rmsDb = -96.3f, vuDb = -96.3f, inDb = -96.3f, peakDb = -96.3f;
	int n = 0;

	void process(float x, float sampleRate) {
		float x2 = x * x;
		msRms += (x2 - msRms) * (1.0f / (0.0107f * sampleRate));
		msVu += (x2 - msVu) * (1.0f / (0.341f * sampleRate));
		blockPeak = std::max(blockPeak, std::fabs(x));
		if (++n >= 64) {
			n = 0;
			rmsDb = barToDb(std::sqrt(msRms));
			vuDb = barToDb(std::sqrt(msVu));
			inDb = barToDb(blockPeak);
			blockPeak = 0.0f;
			peakDb = inDb > peakDb ? inDb : std::max(peakDb - 50.0f * 64.0f / sampleRate, -96.3f);
		}
	}

	void clear() {
		*this = BarMeter();
	}
};

// dB to a linear gain
inline float barDbToGain(float db) {
	return rack::dsp::exp2_taylor5(db * 0.16609640474f);
}
