#pragma once
// dFUZE's engine: the tail is Grotte's 16-line core (bidoo-dsp FdnReverb: exact RT60 per frequency,
// dense from the first milliseconds, a slowly turning matrix so held notes don't ring metallic), the
// early part a diffused cluster of reflections spread over the room's size, and the input goes first
// through gverb's input bandwidth filter, so the knobs keep their meaning. It replaced gverb, whose
// tail took 200-500 ms to grow dense and rang on a few modes at long times.
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "dep/bidoo-dsp/bidoo/dsp/reverb/FdnReverb.h"

struct DfuzeEngine {
	struct Params {
		float sizeM = 30.f;     // 1..300 m
		float revTime = 0.5f;   // s
		float damp = 0.5f;      // 0..0.9
		float bandwidth = 0.5f; // 0..1, gverb's input damper
		float early = 0.5f;     // 0..1
		float tail = 0.5f;      // 0..1
		bool freeze = false;
	};

	static constexpr int kEr = 40;

	void prepare(float sampleRate) {
		sr = sampleRate;
		fdn.prepare(sr);
		int size = 16;
		while (size < (int)(0.3f * sr) + 8)
			size *= 2;
		for (auto &b : erBuf)
			b.assign((size_t)size, 0.f);
		mask = size - 1;
		const float ms = sr * 0.001f;
		const float apMs[2][2] = {{1.3f, 2.9f}, {1.7f, 3.4f}};
		for (int ch = 0; ch < 2; ch++)
			for (int k = 0; k < 2; k++)
				ap[ch][k].prepare((int)(apMs[ch][k] * ms));
		// the reflections' places (0..1 of the span, denser late) and gains (falling, random signs),
		// a different draw per side so the early part is wide
		uint32_t seed = 0x2f6e2b1u;
		auto rnd = [&seed]() { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return (seed >> 8) * (1.f / 16777216.f); };
		for (int ch = 0; ch < 2; ch++) {
			float sum = 0.f;
			for (int k = 0; k < kEr; k++) {
				const float x = ((float)k + 0.5f + 0.7f * (rnd() - 0.5f)) / (float)kEr;
				erAt[ch][k] = std::pow(x, 1.4f);
				const float g = std::exp(-2.5f * erAt[ch][k]) * (0.6f + 0.4f * rnd()) * (rnd() < 0.5f ? -1.f : 1.f);
				erGain[ch][k] = g;
				sum += g * g;
			}
			for (float &g : erGain[ch])
				g /= std::sqrt(sum);
		}
		spanGlide = 1.f - std::exp(-1.f / (0.05f * sr));
		reset();
	}

	void reset() {
		fdn.reset();
		for (auto &b : erBuf)
			std::fill(b.begin(), b.end(), 0.f);
		for (auto &c : ap)
			for (auto &a : c)
				a.reset();
		write = 0;
		lp[0] = lp[1] = 0.f;
		span = -1.f;
	}

	// per sample (no latency); the parameters may change on every call
	void process(float inL, float inR, float &outL, float &outR, const Params &p) {
		// gverb's input bandwidth: a one-pole whose pole is 1 - bandwidth at 48 kHz, the same corner at any rate
		if (p.bandwidth != lastBw) {
			lastBw = p.bandwidth;
			damper = std::pow(std::clamp(1.f - p.bandwidth, 0.f, 0.98f), 48000.f / sr);
		}
		const float x[2] = {inL, inR};
		float f[2];
		for (int ch = 0; ch < 2; ch++) {
			lp[ch] = x[ch] * (1.f - damper) + lp[ch] * damper;
			f[ch] = lp[ch];
		}

		// tail
		fp.rt60 = std::max(0.1f, p.revTime);
		fp.size = std::log(std::clamp(p.sizeM, 1.f, 300.f)) / std::log(300.f);
		fp.brightness = std::clamp(1.f - p.damp, 0.f, 1.f);
		fp.width = 1.0;
		fp.modulation = 0.5;
		fp.freeze = p.freeze;
		float tL, tR;
		fdn.process(&f[0], &f[1], &tL, &tR, 1, fp);

		// early reflections over about 1.6 ms per metre (4-220 ms), gliding with the size
		const float spanTarget = std::clamp(1.6f * p.sizeM, 4.f, 220.f) * 0.001f * sr;
		span = span < 0.f ? spanTarget : span + spanGlide * (spanTarget - span);
		erBuf[0][(size_t)write] = f[0];
		erBuf[1][(size_t)write] = f[1];
		float e[2];
		for (int ch = 0; ch < 2; ch++) {
			float acc = 0.f;
			for (int k = 0; k < kEr; k++) {
				const float d = 1.f + erAt[ch][k] * span;
				const int i = (int)d;
				const float t = d - (float)i;
				const float a = erBuf[ch][(size_t)((write - i) & mask)], b = erBuf[ch][(size_t)((write - i - 1) & mask)];
				acc += erGain[ch][k] * (a + t * (b - a));
			}
			e[ch] = ap[ch][1].process(ap[ch][0].process(acc, 0.6f), 0.55f);
		}
		write = (write + 1) & mask;

		outL = kEarly * p.early * e[0] + kTail * p.tail * tL;
		outR = kEarly * p.early * e[1] + kTail * p.tail * tR;
	}

	// levels: the early and late energies of gverb at 30 m (its early part was mostly the diffused input)
	static constexpr float kEarly = 2.5f, kTail = 1.12f;

private:
	struct Allpass {
		std::vector<float> buf;
		int pos = 0;
		void prepare(int len) { buf.assign((size_t)std::max(1, len), 0.f); pos = 0; }
		void reset() { std::fill(buf.begin(), buf.end(), 0.f); pos = 0; }
		float process(float x, float g) {
			const float d = buf[(size_t)pos];
			const float v = x - g * d;
			buf[(size_t)pos] = v;
			if (++pos >= (int)buf.size())
				pos = 0;
			return d + g * v;
		}
	};

	float sr = 48000.f;
	bidoo::dsp::reverb::FdnReverb fdn;
	bidoo::dsp::reverb::FdnReverbParams fp;
	std::array<std::vector<float>, 2> erBuf;
	int mask = 0, write = 0;
	std::array<std::array<float, kEr>, 2> erAt {}, erGain {};
	std::array<std::array<Allpass, 2>, 2> ap;
	float lp[2] = {}, damper = 0.5f, lastBw = -1.f, span = -1.f, spanGlide = 0.f;
};
