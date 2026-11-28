// PRiSME's sound, without the ports: a bank of 64 sines per voice, up to 16 voices. The module (src/PRISME.cpp) hands
// it the port voltages and the knobs; a test can drive it with nothing but Rack's simd headers.
//
// Each voice plays harmonics 1..64 of its note. Their amplitudes come from two drawn spectra (A and B, crossfaded by
// MORPH, or glided from A to MORPH's position by SWEEP), shaped by BRIGHT (a tilt), ODD/EVEN, a formant bump at a fixed
// frequency, and an envelope per harmonic: a shared attack, then each harmonic decays to the sustain level and releases
// at its own rate (DECAY and RELEASE, times the drawn T page, shorter for higher harmonics by DAMP).
//
// Amplitudes, frequencies and envelopes are worked out every kControl samples (or at once on a gate edge) and ramped
// linearly to the next values, so nothing steps. A harmonic fades out between 0.40 and 0.45 of the sample rate and is
// not computed above it; groups of four harmonics that are silent are skipped, as are voices that have died out.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <simd/Vector.hpp>
#include <simd/functions.hpp>

namespace prisme {

using rack::simd::float_4;

static constexpr int kHarmonics = 64;
static constexpr int kGroups = kHarmonics / 4;
static constexpr int kMaxVoices = 16;
static constexpr int kControl = 32;          // samples between two control steps
static constexpr int kPages = 3;             // A, B, T
enum Page { PAGE_A, PAGE_B, PAGE_T };

// 2^x in four lanes (Rack's simd has exp, not exp2)
inline float_4 exp2v(float_4 x) {
	return rack::simd::exp(x * 0.69314718f);
}

// sin(2 pi p) for p in [0, 1): folded to a quarter cycle, then the odd Taylor series to z^11 (error under 1e-7)
inline float_4 sin2pi(float_4 p) {
	const float_4 a = p - 0.5f;                  // in [-0.5, 0.5): sin(2 pi p) = -sin(2 pi a)
	const float_4 m = rack::simd::abs(a);
	const float_4 q = rack::simd::fmin(m, 0.5f - m);   // sin(2 pi |a|) = sin(2 pi q), q in [0, 0.25]
	const float_4 z = q * (float)(2.0 * M_PI);
	const float_4 z2 = z * z;
	const float_4 s = z * (1.f + z2 * (-1.f / 6.f + z2 * (1.f / 120.f + z2 * (-1.f / 5040.f + z2 * (1.f / 362880.f
		+ z2 * (-1.f / 39916800.f))))));
	return rack::simd::ifelse(a < 0.f, s, -s);
}

// A drawn bar (0..1) as a linear amplitude: 60 dB of range, 1 is 0 dB, and the last 5 % fade to silence at 0, so a bar
// at 0 is off. Morphing happens on the bars, so a crossfade moves through levels in dB, as the ear hears them.
static constexpr float kBarDb = 60.f;
inline float_4 barToAmp(float_4 bar) {
	const float_4 b = rack::simd::clamp(bar, 0.f, 1.f);
	return exp2v((b - 1.f) * (float)(kBarDb / 6.0205999)) * rack::simd::fmin(b * 20.f, 1.f);
}

// The drawn T page (0..1) as a multiplier of DECAY and RELEASE: 1/8 at 0, 1 at 0.5, 8 at 1
inline float_4 barToTime(float_4 bar) {
	return exp2v((rack::simd::clamp(bar, 0.f, 1.f) - 0.5f) * 6.f);
}

struct Engine {
	static constexpr float kC4 = 261.6256f;
	static constexpr float kVolts = 10.f / 3.f;   // a single harmonic alone peaks at 3.3 V; a sawtooth (same RMS) near 5 V
	static constexpr float kFadeFrom = 0.40f, kFadeTo = 0.45f;   // band-limiting, as fractions of the sample rate
	static constexpr float kStretch = 0.12f;  // STRETCH at +-1: harmonic k at k^(1 +- 0.12)
	static constexpr float kFormantSigma = 0.6f;   // the formant bump's width, in octaves (one standard deviation)
	static constexpr float kDead = 1e-4f;     // a released voice below -80 dB stops

	// what the module gives: port voltages (16 floats each, zero beyond their channel counts) and the knobs, as values
	struct Io {
		const float *voct = nullptr; int voctChannels = 0;
		const float *gate = nullptr; int gateChannels = 0;   // 0: unplugged, every voice held (a drone)
		const float *vel = nullptr; int velChannels = 0;     // 0: unplugged, full level
		const float *morphCv = nullptr; int morphChannels = 0;
		const float *brightCv = nullptr; int brightChannels = 0;
		const float *stretchCv = nullptr; int stretchChannels = 0;
		const float *formantCv = nullptr; int formantChannels = 0;
		float morph = 0.f;           // 0..1
		float sweep = 0.f;           // seconds, 0: off
		float bright = 0.f;          // -1..1: -6..+6 dB per octave
		float oddEven = 0.f;         // -1 odd only .. +1 even only
		float stretch = 0.f;         // -1..1
		float formantHz = 1000.f;
		float depth = 0.f;           // 0..1
		float attack = 0.005f;       // seconds
		float decay = 1.f;           // seconds to -60 dB
		float sustain = 1.f;         // 0..1
		float release = 0.3f;        // seconds to -60 dB
		float damp = 0.f;            // 0..1: harmonic k decays k^damp times faster
		float tune = 0.f;            // octaves
		float spread = 0.f;          // 0..1
		bool spreadPhases = false;   // note-on phases: false all at 0 (the classic waveforms), true Schroeder's (lower peaks)
		const float *bars = nullptr; // kPages x kHarmonics, read at each control step
		// out
		int channels = 1;
		float poly[kMaxVoices] = {};
		float left = 0.f, right = 0.f;
	};

	enum Stage { IDLE, ATTACK, DECAY, RELEASE };

	struct Voice {
		float_4 phase[kGroups], inc[kGroups], dinc[kGroups], amp[kGroups], damp[kGroups];
		float_4 env[kGroups], start[kGroups];
		float_4 shown[kGroups];      // the last targets, for the screen
		int stage = IDLE;
		int groups = 0;              // groups computed per sample: the highest that sounds, plus one
		float attackPos = 0.f, sweepPos = 1.f;
		bool gate = false;
	};

	Voice voices[kMaxVoices];
	float sampleRate = 44100.f;
	int countdown = 0;
	float_4 log2k[kGroups];          // log2 of the harmonic numbers
	float_4 schroeder[kGroups];      // note-on phases with a low peak: k (k - 1) / (2 N) cycles
	float_4 oddMask, evenMask;
	// the screen's copy: the newest voice's spectrum, and how many voices play
	int shownVoice = 0;
	int shownChannels = 0;

	Engine() {
		for (int g = 0; g < kGroups; g++)
			for (int l = 0; l < 4; l++) {
				const int k = 4 * g + l + 1;
				log2k[g][l] = std::log2((float)k);
				const double c = (double)k * (k - 1) / (2.0 * kHarmonics);
				schroeder[g][l] = (float)(c - std::floor(c));
			}
		oddMask = float_4(1.f, 0.f, 1.f, 0.f);   // lanes 0 and 2 are harmonics 1, 3, 5...
		evenMask = float_4(0.f, 1.f, 0.f, 1.f);
		reset();
	}

	void setSampleRate(float sr) {
		sampleRate = std::max(sr, 1000.f);
		countdown = 0;
	}

	void reset() {
		for (Voice &v : voices) {
			for (int g = 0; g < kGroups; g++) {
				v.phase[g] = v.inc[g] = v.dinc[g] = v.amp[g] = v.damp[g] = 0.f;
				v.env[g] = v.start[g] = v.shown[g] = 0.f;
			}
			v.stage = IDLE;
			v.groups = 0;
			v.attackPos = 0.f;
			v.sweepPos = 1.f;
			v.gate = false;
		}
		countdown = 0;
	}

	static float chan(const float *v, int n, int c) {
		if (!v || n <= 0)
			return 0.f;
		const float x = v[n == 1 ? 0 : std::min(c, n - 1)];
		return std::isfinite(x) ? x : 0.f;   // a NaN in a CV would stay in the phases for good
	}

	void noteOn(Voice &v, bool spreadPhases) {
		// a voice that was silent starts its harmonics on a fixed phase, so every note begins alike; one still sounding
		// keeps its phases (a jump would click)
		bool silent = v.stage == IDLE;
		if (!silent) {
			float_4 m = 0.f;
			for (int g = 0; g < kGroups; g++)
				m = rack::simd::fmax(m, v.amp[g]);
			silent = std::max(std::max(m[0], m[1]), std::max(m[2], m[3])) < 1e-4f;
		}
		if (silent)
			for (int g = 0; g < kGroups; g++) {
				v.phase[g] = spreadPhases ? schroeder[g] : float_4(0.f);
				v.amp[g] = 0.f;
				v.env[g] = 0.f;
			}
		for (int g = 0; g < kGroups; g++)
			v.start[g] = v.env[g];
		v.stage = ATTACK;
		v.attackPos = 0.f;
		v.sweepPos = 0.f;
	}

	// One control step: targets for the next kControl samples, ramps towards them
	void control(Io &io, int channels) {
		const float dt = kControl / sampleRate;
		const float *barsA = io.bars, *barsB = io.bars + kHarmonics, *barsT = io.bars + 2 * kHarmonics;
		// the decay and release coefficients per harmonic, the same for every voice
		float_4 dc[kGroups], rc[kGroups];
		const float decayTau = std::max(io.decay, 1e-3f) / 6.9078f, releaseTau = std::max(io.release, 1e-3f) / 6.9078f;
		for (int g = 0; g < kGroups; g++) {
			const float_4 tm = barToTime(float_4::load(barsT + 4 * g)) * exp2v(-io.damp * log2k[g]);
			dc[g] = rack::simd::exp(-dt / (decayTau * tm));
			rc[g] = rack::simd::exp(-dt / (releaseTau * tm));
		}
		const float sustain = rack::simd::clamp(io.sustain, 0.f, 1.f);
		const float attackStep = dt / std::max(io.attack, 1e-3f);
		const float sweepStep = io.sweep > 0.f ? dt / io.sweep : 2.f;
		const float gOdd = std::min(1.f, 1.f - io.oddEven), gEven = std::min(1.f, 1.f + io.oddEven);
		const float_4 oddEven = oddMask * gOdd + evenMask * gEven;
		const float iSr = 1.f / sampleRate;
		const float iN = 1.f / kControl;

		for (int c = 0; c < channels; c++) {
			Voice &v = voices[c];
			// the envelope
			if (v.stage == IDLE)
				continue;
			if (v.stage == ATTACK) {
				v.attackPos = std::min(1.f, v.attackPos + attackStep);
				for (int g = 0; g < kGroups; g++)
					v.env[g] = v.start[g] + (1.f - v.start[g]) * v.attackPos;
				if (v.attackPos >= 1.f)
					v.stage = DECAY;
			}
			else if (v.stage == DECAY) {
				for (int g = 0; g < kGroups; g++)
					v.env[g] = sustain + (v.env[g] - sustain) * dc[g];
			}
			else {
				float_4 m = 0.f;
				for (int g = 0; g < kGroups; g++) {
					v.env[g] *= rc[g];
					m = rack::simd::fmax(m, v.env[g]);
				}
				if (std::max(std::max(m[0], m[1]), std::max(m[2], m[3])) < kDead) {
					v.stage = IDLE;
					for (int g = 0; g < kGroups; g++)
						v.amp[g] = v.damp[g] = v.env[g] = v.shown[g] = 0.f;
					v.groups = 0;
					continue;
				}
			}
			v.sweepPos = std::min(1.f, v.sweepPos + sweepStep);

			// the spectrum
			const float voct = chan(io.voct, io.voctChannels, c) + io.tune;
			const float f0 = kC4 * std::exp2(rack::simd::clamp(voct, -10.f, 10.f));
			const float morph = rack::simd::clamp(io.morph + 0.1f * chan(io.morphCv, io.morphChannels, c), 0.f, 1.f) * v.sweepPos;
			const float bright = rack::simd::clamp(io.bright + 0.2f * chan(io.brightCv, io.brightChannels, c), -1.f, 1.f);
			const float stretch = rack::simd::clamp(io.stretch + 0.2f * chan(io.stretchCv, io.stretchChannels, c), -1.f, 1.f);
			const float log2Fc = std::log2(std::max(io.formantHz, 1.f)) + rack::simd::clamp(chan(io.formantCv, io.formantChannels, c), -10.f, 10.f);
			const float depth = rack::simd::clamp(io.depth, 0.f, 1.f);
			const float vel = io.velChannels > 0 ? rack::simd::clamp(chan(io.vel, io.velChannels, c) * 0.1f, 0.f, 1.f) : 1.f;
			const float log2f0 = std::log2(f0);
			const float sExp = 1.f + kStretch * stretch;

			float_4 a[kGroups], incT[kGroups];
			float_4 sum = 0.f, sum2 = 0.f;
			for (int g = 0; g < kGroups; g++) {
				const float_4 log2r = log2k[g] * sExp;           // the harmonic's ratio to the fundamental, in octaves
				const float_4 nf = exp2v(log2r) * (f0 * iSr);   // its frequency, in cycles per sample
				const float_4 barA = float_4::load(barsA + 4 * g), barB = float_4::load(barsB + 4 * g);
				const float_4 bar = barA + (barB - barA) * morph;
				const float_4 b = rack::simd::clamp(bar, 0.f, 1.f);
				// the bar in dB and the tilt in one exp2
				float_4 x = exp2v((b - 1.f) * (float)(kBarDb / 6.0205999) + bright * log2k[g])
					* rack::simd::fmin(b * 20.f, 1.f) * oddEven;
				if (depth > 0.f) {
					const float_4 d = (log2r + log2f0 - log2Fc) * (1.f / kFormantSigma);
					const float_4 bump = rack::simd::exp(-0.5f * d * d);
					x *= (1.f - depth) + 3.f * depth * bump;
				}
				x *= rack::simd::clamp((kFadeTo - nf) * (1.f / (kFadeTo - kFadeFrom)), 0.f, 1.f);
				a[g] = x;
				incT[g] = rack::simd::fmin(nf, 0.5f);
				sum += x;
				sum2 += x * x;
			}
			// the level: every spectrum at the same RMS, so drawing more harmonics keeps the loudness (one harmonic alone
			// peaks at 3.3 V, a sawtooth near 5 V); a spectrum whose harmonics would all add up in phase (a pulse train)
			// is held lower, its peak never over 5 x 3.3 V
			const float s1 = sum[0] + sum[1] + sum[2] + sum[3], s2 = sum2[0] + sum2[1] + sum2[2] + sum2[3];
			const float norm = kVolts * vel / std::max(std::max(std::sqrt(s2), s1 * 0.2f), 1e-3f);
			int groups = 0;
			for (int g = 0; g < kGroups; g++) {
				const float_4 target = a[g] * v.env[g] * norm;
				v.shown[g] = target;
				v.damp[g] = (target - v.amp[g]) * iN;
				v.dinc[g] = (incT[g] - v.inc[g]) * iN;
				const float_4 any = rack::simd::fmax(target, v.amp[g]);
				if (std::max(std::max(any[0], any[1]), std::max(any[2], any[3])) > 0.f)
					groups = g + 1;
			}
			v.groups = groups;
		}
	}

	void process(Io &io) {
		const int channels = std::max(1, std::min(kMaxVoices, std::max(io.voctChannels, io.gateChannels)));
		bool edge = false;
		for (int c = 0; c < channels; c++) {
			Voice &v = voices[c];
			const bool g = io.gateChannels > 0 ? chan(io.gate, io.gateChannels, c) >= (v.gate ? 0.1f : 1.f) : true;
			if (g && !v.gate) {
				v.gate = true;
				noteOn(v, io.spreadPhases);
				shownVoice = c;
				edge = true;
			}
			else if (!g && v.gate) {
				v.gate = false;
				if (v.stage == ATTACK || v.stage == DECAY)
					v.stage = RELEASE;
				edge = true;
			}
		}
		// voices beyond the channel count release
		for (int c = channels; c < kMaxVoices; c++)
			if (voices[c].gate) {
				voices[c].gate = false;
				if (voices[c].stage == ATTACK || voices[c].stage == DECAY)
					voices[c].stage = RELEASE;
			}
		if (countdown <= 0 || edge) {
			control(io, kMaxVoices);
			countdown = kControl;
		}
		countdown--;

		const float spread = rack::simd::clamp(io.spread, 0.f, 1.f);
		float left = 0.f, right = 0.f;
		for (int c = 0; c < kMaxVoices; c++) {
			Voice &v = voices[c];
			float out = 0.f;
			if (v.groups > 0) {
				float_4 acc = 0.f;
				float fund = 0.f;
				for (int g = 0; g < v.groups; g++) {
					float_4 p = v.phase[g] + v.inc[g];
					p = rack::simd::ifelse(p >= 1.f, p - 1.f, p);
					v.phase[g] = p;
					const float_4 s = sin2pi(p) * v.amp[g];
					if (g == 0)
						fund = s[0];
					acc += s;
					v.amp[g] += v.damp[g];
					v.inc[g] += v.dinc[g];
				}
				const float odd = acc[0] + acc[2], even = acc[1] + acc[3];
				out = odd + even;
				// SPREAD: odd harmonics (but the fundamental) to the left, even ones to the right
				const float oddRest = odd - fund;
				left += fund + oddRest + even * (1.f - spread);
				right += fund + oddRest * (1.f - spread) + even;
			}
			if (c < channels)
				io.poly[c] = out;
		}
		io.channels = channels;
		shownChannels = channels;
		// the sums over 1 / sqrt(channels): a chord of 4 at the level of 2 voices, 16 at 4. The channel count, not the
		// voices sounding, so the level does not pump as notes come and go
		const float g = 1.f / std::sqrt((float)channels);
		io.left = left * g;
		io.right = right * g;
	}
};

} // namespace prisme
