// PRiSME's sound, without the ports: a bank of 64 sines per voice, up to 16 voices. The module (src/PRISME.cpp) hands
// it the port voltages and the knobs; a test can drive it with nothing but Rack's simd headers.
//
// Each voice plays harmonics 1..64 of its note. Their levels come from up to four drawn spectra (A to D, morphed along
// the chain by MORPH, its CV and velocity, from where each note travels to the last spectrum in SWEEP's time), shaped by BRIGHT (a tilt), ODD/EVEN,
// a formant filter drawn over frequency (moved by FORMANT, its CV and the note's envelope) and an envelope per
// harmonic: each rises in ATTACK times its bar on the attack page, falls to the sustain level in DECAY times its bar on
// the decay page (shorter for high harmonics by DAMP), and releases likewise. In loop mode each harmonic rises again
// when it has fallen to the sustain level, at its own pace, for as long as the gate is held.
//
// Levels, frequencies and envelopes are worked out every kControl samples (or at once on a gate edge) and ramped
// linearly to the next values, so nothing steps. A harmonic fades out between 0.40 and 0.45 of the sample rate and is
// not computed above it; groups of four harmonics that are silent are skipped, as are voices that have died out.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <simd/Vector.hpp>
#include <simd/functions.hpp>
#include "PrismeBars.hpp"

namespace prisme {

using rack::simd::float_4;

static constexpr int kHarmonics = kBars;
static constexpr int kGroups = kHarmonics / 4;
static constexpr int kMaxVoices = 16;
static constexpr int kControl = 32;          // samples between two control steps

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

// A spectrum bar (0..1) as a linear level: 60 dB of range, 1 is 0 dB, and the last 5 % fade to silence at 0, so a bar
// at 0 is off. Morphing happens on the bars, so a crossfade moves through levels in dB, as the ear hears them.
static constexpr float kBarDb = 60.f;

// A time bar (0..1) as a multiplier of the knob's time: 1/8 at 0, 1 at 0.5, 8 at 1
inline float_4 barToTime(float_4 bar) {
	return exp2v((rack::simd::clamp(bar, 0.f, 1.f) - 0.5f) * 6.f);
}

struct Engine {
	static constexpr float kC4 = 261.6256f;
	static constexpr float kVolts = 10.f / 3.f;   // a single harmonic alone peaks at 3.3 V; a sawtooth (same RMS) near 5 V
	static constexpr float kFadeFrom = 0.40f, kFadeTo = 0.45f;   // band-limiting, as fractions of the sample rate
	static constexpr float kStretch = 0.12f;  // STRETCH at +-1: harmonic k at k^(1 +- 0.12)
	static constexpr float kDead = 1e-4f;     // a released voice below -80 dB stops
	static constexpr float kLoopNear = 0.02f; // loop mode: a harmonic rises again within 2 % of its fall to SUSTAIN

	// what the module gives: port voltages (16 floats each, zero beyond their channel counts) and the knobs, as values
	struct Io {
		const float *voct = nullptr; int voctChannels = 0;
		const float *gate = nullptr; int gateChannels = 0;   // 0: unplugged, every voice held (a drone)
		const float *vel = nullptr; int velChannels = 0;     // 0: unplugged, full level, no morph
		const float *morphCv = nullptr; int morphChannels = 0;
		const float *brightCv = nullptr; int brightChannels = 0;
		const float *stretchCv = nullptr; int stretchChannels = 0;
		const float *formantCv = nullptr; int formantChannels = 0;
		float morph = 0.f;           // 0..1 along the chain of spectra
		int spectra = 4;             // 2..4: A-B, A-B-C or A-B-C-D
		float sweep = 0.f;           // seconds from MORPH's position to the last spectrum, 0: off
		float bright = 0.f;          // -1..1: -6..+6 dB per octave
		float oddEven = 0.f;         // -1 odd only .. +1 even only
		float stretch = 0.f;         // -1..1
		float formantShift = 0.f;    // octaves the drawn formant curve moves
		float formantEnv = 0.f;      // octaves it moves at the note's full envelope (the fundamental's)
		float depth = 0.f;           // 0..1
		float attack = 0.005f;       // seconds, times each harmonic's attack bar
		float decay = 1.f;           // seconds to -60 dB, times each harmonic's decay bar
		float sustain = 1.f;         // 0..1
		float release = 0.3f;        // seconds to -60 dB, times each harmonic's decay bar
		float damp = 0.f;            // 0..1: harmonic k decays k^damp times faster
		bool loop = false;           // each harmonic rises again once it has fallen to SUSTAIN
		float velLevel = 1.f;        // 0..1: how much velocity sets the level
		float velMorph = 0.f;        // 0..1: how far along the chain full velocity moves MORPH
		float tune = 0.f;            // octaves
		float spread = 0.f;          // 0..1
		bool spreadPhases = false;   // note-on phases: false all at 0 (the classic waveforms), true Schroeder's (lower peaks)
		const float *bars = nullptr; // kBarPages x kHarmonics, read at each control step
		// out
		int channels = 1;
		float poly[kMaxVoices] = {};
		float left = 0.f, right = 0.f;
	};

	enum Stage { IDLE, ON, RELEASE };

	struct Voice {
		float_4 phase[kGroups], inc[kGroups], dinc[kGroups], amp[kGroups], damp[kGroups];
		float_4 env[kGroups];
		float_4 rising[kGroups];     // 1 while a harmonic rises (its attack), 0 while it falls
		float_4 shown[kGroups];      // the last targets, for the screen
		int stage = IDLE;
		int groups = 0;              // groups computed per sample: the highest that sounds, plus one
		float sweepPos = 1.f;
		float shift = 0.f;           // the formant's shift last worked out, for the screen
		bool gate = false;
	};

	Voice voices[kMaxVoices];
	float sampleRate = 44100.f;
	int countdown = 0;
	float_4 log2k[kGroups];          // log2 of the harmonic numbers
	float_4 schroeder[kGroups];      // note-on phases with a low peak: k (k - 1) / (2 N) cycles
	float_4 oddMask, evenMask;
	// the screen's copy: the newest voice, and how many voices play
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
				v.env[g] = v.rising[g] = v.shown[g] = 0.f;
			}
			v.stage = IDLE;
			v.groups = 0;
			v.sweepPos = 1.f;
			v.shift = 0.f;
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

	static float hmax(float_4 m) {
		return std::max(std::max(m[0], m[1]), std::max(m[2], m[3]));
	}

	void noteOn(Voice &v, bool spreadPhases) {
		// a voice that was silent starts its harmonics on a fixed phase, so every note begins alike; one still sounding
		// keeps its phases and its levels, and every harmonic rises again from where it is (a jump would click)
		bool silent = v.stage == IDLE;
		if (!silent) {
			float_4 m = 0.f;
			for (int g = 0; g < kGroups; g++)
				m = rack::simd::fmax(m, v.amp[g]);
			silent = hmax(m) < 1e-4f;
		}
		for (int g = 0; g < kGroups; g++) {
			if (silent) {
				v.phase[g] = spreadPhases ? schroeder[g] : float_4(0.f);
				v.amp[g] = 0.f;
				v.env[g] = 0.f;
			}
			v.rising[g] = 1.f;
		}
		v.stage = ON;
		v.sweepPos = 0.f;
	}

	// the formant curve's gain at log2 frequencies lf (Hz), the curve moved by `shift` octaves: the bars interpolated
	// along the bands, held at the edges, dB to a gain
	static float_4 formantGain(const float *bars, float_4 lf, float shift) {
		static const float lowOct = std::log2(kFormantLow);
		const float_4 pos = (lf - lowOct - shift) * ((kBars - 1) / kFormantOctaves);
		float_4 out;
		for (int l = 0; l < 4; l++) {
			const float p = std::min(std::max(pos[l], 0.f), (float)(kBars - 1));
			const int i = std::min((int)p, kBars - 2);
			const float f = p - i;
			out[l] = bars[i] + (bars[i + 1] - bars[i]) * f;
		}
		return exp2v((out - kFormantUnity) * (kFormantDbSpan / 6.0206f));
	}

	// One control step: targets for the next kControl samples, ramps towards them
	void control(Io &io, int channels) {
		const float dt = kControl / sampleRate;
		const float *barsAttack = io.bars + PAGE_ATTACK * kHarmonics, *barsDecay = io.bars + PAGE_DECAY * kHarmonics;
		const float *barsFormant = io.bars + PAGE_FORMANT * kHarmonics;
		// the envelope's steps per harmonic, the same for every voice
		float_4 up[kGroups], dc[kGroups], rc[kGroups];
		const float decayTau = std::max(io.decay, 1e-3f) / 6.9078f, releaseTau = std::max(io.release, 1e-3f) / 6.9078f;
		const float attack = std::max(io.attack, 1e-3f);
		for (int g = 0; g < kGroups; g++) {
			const float_4 tm = barToTime(float_4::load(barsDecay + 4 * g)) * exp2v(-io.damp * log2k[g]);
			dc[g] = rack::simd::exp(-dt / (decayTau * tm));
			rc[g] = rack::simd::exp(-dt / (releaseTau * tm));
			up[g] = dt / (attack * barToTime(float_4::load(barsAttack + 4 * g)));
		}
		const float sustain = rack::simd::clamp(io.sustain, 0.f, 1.f);
		const float loopAt = kLoopNear * (1.f - sustain) + 1e-4f;
		const float sweepStep = io.sweep > 0.f ? dt / io.sweep : 2.f;
		const float gOdd = std::min(1.f, 1.f - io.oddEven), gEven = std::min(1.f, 1.f + io.oddEven);
		const float_4 oddEven = oddMask * gOdd + evenMask * gEven;
		const int spectra = std::min(std::max(io.spectra, 2), kSpectra);
		const float iSr = 1.f / sampleRate;
		const float iN = 1.f / kControl;

		for (int c = 0; c < channels; c++) {
			Voice &v = voices[c];
			if (v.stage == IDLE)
				continue;
			// the envelope of each harmonic
			if (v.stage == ON) {
				for (int g = 0; g < kGroups; g++) {
					const float_4 rising = v.rising[g] > 0.5f;
					const float_4 rise = rack::simd::fmin(v.env[g] + up[g], 1.f);
					const float_4 fall = sustain + (v.env[g] - sustain) * dc[g];
					v.env[g] = rack::simd::ifelse(rising, rise, fall);
					// at the top, a harmonic starts falling; in loop mode, near SUSTAIN it rises again
					float_4 again = 0.f;
					if (io.loop)
						again = rack::simd::ifelse(rack::simd::abs(fall - sustain) <= loopAt, float_4(1.f), float_4(0.f));
					v.rising[g] = rack::simd::ifelse(rising, rack::simd::ifelse(rise >= 1.f, float_4(0.f), float_4(1.f)), again);
				}
			}
			else {
				float_4 m = 0.f;
				for (int g = 0; g < kGroups; g++) {
					v.env[g] *= rc[g];
					m = rack::simd::fmax(m, v.env[g]);
				}
				if (hmax(m) < kDead) {
					v.stage = IDLE;
					for (int g = 0; g < kGroups; g++)
						v.amp[g] = v.damp[g] = v.env[g] = v.shown[g] = v.rising[g] = 0.f;
					v.groups = 0;
					continue;
				}
			}
			v.sweepPos = std::min(1.f, v.sweepPos + sweepStep);

			// the spectrum
			const float voct = chan(io.voct, io.voctChannels, c) + io.tune;
			const float f0 = kC4 * std::exp2(rack::simd::clamp(voct, -10.f, 10.f));
			const float vel = io.velChannels > 0 ? rack::simd::clamp(chan(io.vel, io.velChannels, c) * 0.1f, 0.f, 1.f) : 1.f;
			const float velForMorph = io.velChannels > 0 ? vel : 0.f;
			// where the note starts along the chain (MORPH, its CV, velocity), and with SWEEP where it has travelled
			// to, from there towards the last spectrum
			const float start = rack::simd::clamp(io.morph + 0.1f * chan(io.morphCv, io.morphChannels, c)
				+ io.velMorph * velForMorph, 0.f, 1.f);
			const float morph = io.sweep > 0.f ? start + (1.f - start) * v.sweepPos : start;
			// where along the chain: between spectra seg and seg + 1
			const float along = morph * (spectra - 1);
			const int seg = std::min((int)along, spectra - 2);
			const float frac = along - seg;
			const float *barsFrom = io.bars + seg * kHarmonics, *barsTo = io.bars + (seg + 1) * kHarmonics;
			const float bright = rack::simd::clamp(io.bright + 0.2f * chan(io.brightCv, io.brightChannels, c), -1.f, 1.f);
			const float stretch = rack::simd::clamp(io.stretch + 0.2f * chan(io.stretchCv, io.stretchChannels, c), -1.f, 1.f);
			const float depth = rack::simd::clamp(io.depth, 0.f, 1.f);
			const float shift = io.formantShift + rack::simd::clamp(chan(io.formantCv, io.formantChannels, c), -10.f, 10.f)
				+ io.formantEnv * v.env[0][0];
			v.shift = shift;
			const float level = 1.f - io.velLevel + io.velLevel * vel;
			const float log2f0 = std::log2(f0);
			const float sExp = 1.f + kStretch * stretch;

			float_4 a[kGroups], incT[kGroups];
			float_4 sum = 0.f, sum2 = 0.f;
			for (int g = 0; g < kGroups; g++) {
				const float_4 log2r = log2k[g] * sExp;           // the harmonic's ratio to the fundamental, in octaves
				const float_4 nf = exp2v(log2r) * (f0 * iSr);    // its frequency, in cycles per sample
				const float_4 b0 = float_4::load(barsFrom + 4 * g), b1 = float_4::load(barsTo + 4 * g);
				const float_4 b = rack::simd::clamp(b0 + (b1 - b0) * frac, 0.f, 1.f);
				// the bar in dB and the tilt in one exp2
				float_4 x = exp2v((b - 1.f) * (float)(kBarDb / 6.0205999) + bright * log2k[g])
					* rack::simd::fmin(b * 20.f, 1.f) * oddEven;
				if (depth > 0.f)
					x *= (1.f - depth) + depth * formantGain(barsFormant, log2r + log2f0, shift);
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
			const float norm = kVolts * level / std::max(std::max(std::sqrt(s2), s1 * 0.2f), 1e-3f);
			int groups = 0;
			for (int g = 0; g < kGroups; g++) {
				const float_4 target = a[g] * v.env[g] * norm;
				v.shown[g] = target;
				v.damp[g] = (target - v.amp[g]) * iN;
				v.dinc[g] = (incT[g] - v.inc[g]) * iN;
				if (hmax(rack::simd::fmax(target, v.amp[g])) > 0.f)
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
				if (v.stage == ON)
					v.stage = RELEASE;
				edge = true;
			}
		}
		// voices beyond the channel count release
		for (int c = channels; c < kMaxVoices; c++)
			if (voices[c].gate) {
				voices[c].gate = false;
				if (voices[c].stage == ON)
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
