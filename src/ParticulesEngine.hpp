// pARtiCuLeS's engine, without Rack: a stereo buffer (the live input, recorded without end, or a file), up to 64 grains
// taken from it (free running or on a clock), each with its own envelope, pitch, direction and pan, then a Fdn16 reverb
// with a shimmer (a pitch shifter in its loop). The module feeds it blocks of kBlock samples.
#pragma once
#include <bidoo/dsp/reverb/FdnReverb.h>
#include <bidoo/dsp/delay/GranularPitchShifter.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace particules {

constexpr int kBlock = 16;
constexpr int kGrains = 64;
constexpr int kPeaks = 460;                 // the buffer's waveform for the display
constexpr double kLiveSeconds = 20.0;

// Rhythmic divisions of the clock, in beats
struct Division {
	double beats;
	const char *name;
};
inline const Division kDivisions[] = {
	{16.0, "4 bars"}, {8.0, "2 bars"}, {4.0, "1 bar"}, {3.0, "1/2."}, {2.0, "1/2"}, {4.0 / 3.0, "1/2T"}, {1.5, "1/4."},
	{1.0, "1/4"}, {2.0 / 3.0, "1/4T"}, {0.75, "1/8."}, {0.5, "1/8"}, {1.0 / 3.0, "1/8T"}, {0.375, "1/16."}, {0.25, "1/16"},
	{1.0 / 6.0, "1/16T"}, {0.1875, "1/32."}, {0.125, "1/32"}, {1.0 / 12.0, "1/32T"}, {0.0625, "1/64"},
};
constexpr int kNumDivisions = (int)(sizeof(kDivisions) / sizeof(kDivisions[0]));

enum Direction { FORWARD, REVERSE, PINGPONG, RANDOM };
inline const char *const kDirectionNames[] = {"Forward", "Reverse", "Ping-pong", "Random"};
inline const float kIntervals[] = {12.f, 7.f, -12.f};
inline const char *const kIntervalNames[] = {"+12", "+7", "-12"};
inline const char *const kDetuneQuantNames[] = {"Free", "Octaves", "Fifths and octaves"};

// The sound the grains read: a ring when it records, the file as it was loaded otherwise. Volts / 5.
struct Buffer {
	std::vector<float> l, r;
	int64_t cap = 0;        // frames it holds
	int64_t length = 0;     // frames that hold sound (up to cap)
	int64_t write = 0;      // where the next recorded frame goes
	double rate = 48000.0;  // the rate it was recorded or loaded at
	float peaks[kPeaks] = {};
	int peakBin = -1;

	void allocate(int64_t frames, double sampleRate) {
		cap = std::max<int64_t>(64, frames);
		l.assign((size_t)cap, 0.f);
		r.assign((size_t)cap, 0.f);
		length = 0;
		write = 0;
		rate = sampleRate;
		std::fill(peaks, peaks + kPeaks, 0.f);
	}
	// a file: all of it, from the start
	void fill(const float *fl, const float *fr, int64_t frames, double sampleRate) {
		allocate(frames, sampleRate);
		std::copy(fl, fl + frames, l.begin());
		std::copy(fr, fr + frames, r.begin());
		length = frames;
		write = frames % cap;
		for (int64_t i = 0; i < frames; i++) {
			const int b = bin(i);
			peaks[b] = std::max(peaks[b], std::max(std::fabs(fl[i]), std::fabs(fr[i])));
		}
	}
	int64_t oldest() const {
		return length < cap ? 0 : write;
	}
	int bin(int64_t index) const {
		return std::clamp((int)(index * kPeaks / std::max<int64_t>(1, cap)), 0, kPeaks - 1);
	}
	// one recorded frame, mixed over what was there by `g` (the write fades in and out)
	void record(float inL, float inR, float g) {
		float &a = l[(size_t)write], &b = r[(size_t)write];
		a += (inL - a) * g;
		b += (inR - b) * g;
		const int pb = bin(write);
		if (pb != peakBin) {
			peakBin = pb;
			peaks[pb] = 0.f;
		}
		peaks[pb] = std::max(peaks[pb], std::max(std::fabs(a), std::fabs(b)));
		if (++write >= cap)
			write = 0;
		if (length < cap)
			length++;
	}
	// Hermite read at a ring position (wraps)
	void read(double pos, float &outL, float &outR) const {
		const int64_t i = (int64_t)std::floor(pos);
		const float t = (float)(pos - (double)i);
		if (i >= 1 && i + 2 < cap) {
			const size_t b = (size_t)i;
			outL = hermite(l[b - 1], l[b], l[b + 1], l[b + 2], t);
			outR = hermite(r[b - 1], r[b], r[b + 1], r[b + 2], t);
			return;
		}
		auto at = [&](int64_t j) { j %= cap; if (j < 0) j += cap; return (size_t)j; };
		const size_t a = at(i - 1), b = at(i), c = at(i + 1), d = at(i + 2);
		outL = hermite(l[a], l[b], l[c], l[d], t);
		outR = hermite(r[a], r[b], r[c], r[d], t);
	}
	static float hermite(float xm1, float x0, float x1, float x2, float t) {
		const float c1 = 0.5f * (x1 - xm1);
		const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
		const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
		return ((c3 * t + c2) * t + c1) * t + x0;
	}
};

// What the module sets before each block
struct Controls {
	float position = 0.5f;   // 0..1, oldest to newest
	float spray = 0.1f;      // 0..1 of the buffer, around the position
	float scan = 0.f;        // -1..1: the position moves, 1 = at the speed the sound was recorded
	float size = 0.1f;       // seconds, 0.001..2
	bool clockMode = false;
	float rate = 10.f;       // grains a second (free); 0: only TRIG starts them
	float chaos = 0.3f;      // 0 regular .. 1 random (free)
	int division = 13;       // kDivisions index (clock)
	float chance = 1.f;      // probability a division starts grains (clock)
	int count = 1;           // grains started together
	float shape = 0.5f;      // 0 percussive, 0.5 bell, 1 reversed
	float soft = 0.7f;       // 0 sharp .. 1 smooth
	float tune = 0.f;        // semitones
	float detune = 0.f;      // 0..1
	int detuneQuant = 0;     // kDetuneQuantNames
	int direction = FORWARD;
	float width = 0.5f;      // 0..1 random pan
	bool record = true;
	bool freeze = false;     // the reverb's tail held
	float decay = 3.f;       // reverb seconds
	float room = 0.6f;
	float tone = 0.6f;
	float shimmer = 0.f;     // 0..1
	int interval = 0;        // kIntervals
	float mix = 0.3f;        // 0 grains .. 1 reverb
	float level = 1.f;
};

struct Grain {
	bool on = false;
	const Buffer *buf = nullptr;
	double pos = 0.0, inc = 1.0;
	int64_t n = 0, len = 1, turn = -1;  // turn: where a ping-pong grain goes back
	float attack = 0.5f, soft = 0.7f;
	float gl = 0.7f, gr = 0.7f;
	int fadeLeft = -1;                  // a grain cut short (its buffer replaced): samples left
	float fadeGain = 1.f;
};

class Engine {
public:
	// the buffer the grains read (audio thread), a new one waiting (from the UI) and the old one to free (by the UI)
	std::atomic<Buffer *> pending {nullptr};
	std::atomic<Buffer *> retired {nullptr};
	// for the display: each grain's place (0..1 of the buffer, -1 off) and envelope, the window and the write head
	float grainPos[kGrains] = {};
	float grainEnv[kGrains] = {};
	std::atomic<float> windowLo {0.f}, windowHi {1.f}, scanPos {0.f};
	std::atomic<bool> recording {false};
	std::atomic<int> active {0};
	std::atomic<const Buffer *> view {nullptr};   // the buffer the grains read, for the UI (freed only through `retired`)

	Engine() {
		std::fill(grainPos, grainPos + kGrains, -1.f);
		buildLut();
	}
	~Engine() {
		delete cur;
		delete draining;
		delete pending.exchange(nullptr);
		delete retired.exchange(nullptr);
	}

	// Before the first block (and on a new rate); allocates
	void prepare(double sampleRate) {
		sr = sampleRate;
		if (!cur) {
			cur = new Buffer;
			cur->allocate((int64_t)(kLiveSeconds * std::max(48000.0, sr)), sr);
		}
		view = cur;
		reverb.prepare(sr);
		for (auto &s : shift)
			s.prepare(sr);
		recGain = 0.f;
		recStep = 1.f / (float)(0.005 * sr);
		fadeLen = std::max(16, (int)(0.003 * sr));
		wetRelease = std::exp(-1.f / (float)(0.3 * sr));
		gainCoef = 1.f - std::exp(-1.f / (float)(0.02 * sr));
		mixCoef = 1.f - std::exp(-1.f / (float)(0.005 * sr));
		lastPpq = -1.0;
		nextFree = 0.0;
	}

	const Buffer *buffer() const {
		return cur;
	}

	// One block. `ppq`: the clock's beats at each sample (clock mode), `running`: it runs, `bpm` its tempo;
	// `trig`: a grain asked for on that sample; `voct`/`nv`: the V/O channels the grains take in turn.
	// Writes the output (volts / 5), a flag per sample a grain started on, and the newest grain's envelope.
	void process(const float *inL, const float *inR, const double *ppq, bool running, double bpm, const bool *trig,
	             const float *voct, int nv, const Controls &c, float *outL, float *outR, bool *started, float *env) {
		takePending();
		const int n = kBlock;
		Buffer &b = *cur;
		recording = c.record;
		// what the grains will read: the window, where the scan has got to
		const double seconds = (double)b.length / b.rate;
		if (seconds > 0.0) {
			scan += (double)c.scan * n / sr / seconds;
			scan -= std::floor(scan);
		}
		scanPos = (float)scan;
		const float sizeS = std::clamp(c.size, 0.001f, 2.f);
		// the grains' level: the louder the overlap, the lower (an expected overlap of o grains: 1 / sqrt(o))
		const double perSecond = c.clockMode ? (double)c.count * c.chance * std::max(bpm, 1.0) / 60.0 / kDivisions[clampDiv(c.division)].beats
		                                     : (double)c.count * std::max(c.rate, 1.f);
		const float gainTarget = 1.f / std::sqrt((float)std::max(1.0, perSecond * sizeS));

		float dryL[kBlock], dryR[kBlock];
		for (int i = 0; i < n; i++) {
			// ---- record (fading in and out over 5 ms, so a stop or a start leaves no step in the buffer)
			recGain = std::clamp(recGain + (c.record ? recStep : -recStep), 0.f, 1.f);
			if (recGain > 0.f && b.cap > 0)
				b.record(inL[i], inR[i], recGain);

			// ---- grains asked for: the clock, the free timer, TRIG
			int want = 0;
			if (c.clockMode) {
				const double pos = ppq[i];
				const double div = kDivisions[clampDiv(c.division)].beats;
				if (running && lastPpq >= 0.0 && std::floor(pos / div + 1e-9) != std::floor(lastPpq / div + 1e-9))
					if (rnd() < c.chance)
						want += c.count;
				if (running && lastPpq < 0.0 && std::fabs(pos - std::round(pos / div) * div) < 1e-9 && rnd() < c.chance)
					want += c.count; // the first pulse is a division
				lastPpq = running ? pos : -1.0;
			}
			else {
				lastPpq = -1.0;
				nextFree -= 1.0;
				if (c.rate <= 0.f) {
					nextFree = 0.0; // RATE at 0: only TRIG starts grains (the first comes at once when RATE moves up)
				}
				else if (nextFree <= 0.0) {
					want += c.count;
					const double period = sr / std::clamp((double)c.rate, 0.05, 1000.0);
					const double r = -std::log(std::max(1e-6, (double)rnd()));   // mean 1
					nextFree += period * ((1.0 - c.chaos) + c.chaos * r);
					if (nextFree < 1.0)
						nextFree = 1.0;
				}
			}
			if (trig && trig[i])
				want += c.count;
			started[i] = false;
			for (int k = 0; k < want; k++)
				if (spawn(c, voct, nv, sizeS))
					started[i] = true;

			// ---- the grains
			float l = 0.f, r = 0.f;
			float newestEnv = 0.f;
			for (auto &g : grains) {
				if (!g.on)
					continue;
				const float u = (float)g.n / (float)g.len;
				float e = envelope(u, g.attack, g.soft);
				if (g.fadeLeft >= 0) {
					g.fadeGain = (float)g.fadeLeft / (float)fadeLen;
					e *= g.fadeGain;
					if (--g.fadeLeft < 0)
						g.n = g.len;
				}
				float a, bb;
				g.buf->read(g.pos, a, bb);
				l += e * a * g.gl;
				r += e * bb * g.gr;
				newestEnv = e;
				g.pos += g.inc;
				if (g.pos >= (double)g.buf->cap)
					g.pos -= (double)g.buf->cap;
				else if (g.pos < 0.0)
					g.pos += (double)g.buf->cap;
				if (++g.n == g.turn)
					g.inc = -g.inc;
				if (g.n >= g.len)
					g.on = false;
			}
			grainGain += gainCoef * (gainTarget - grainGain);
			dryL[i] = l * grainGain;
			dryR[i] = r * grainGain;
			env[i] = newestEnv;
		}
		retireDraining();

		// ---- reverb, the shimmer in its loop (the last block's wet, pitched, back into it)
		float revL[kBlock], revR[kBlock], wetL[kBlock], wetR[kBlock];
		const float semis = kIntervals[std::clamp(c.interval, 0, 2)];
		shift[0].setSemitones(semis);
		shift[1].setSemitones(semis);
		// the loop's gain falls as the decay grows (a long tail already holds more of the past), and what goes
		// back in ducks when the wet gets loud, so the shimmer can't run away
		const float amount = 0.6f * std::clamp(c.shimmer, 0.f, 1.f) * std::sqrt(3.f / std::max(3.f, c.decay));
		for (int i = 0; i < n; i++) {
			float sl = 0.f, sr2 = 0.f;
			if (amount > 0.f || shimmerOn) {
				sl = shift[0].process(lastWetL[i]);
				sr2 = shift[1].process(lastWetR[i]);
			}
			shimmerNow += (amount - shimmerNow) * 0.002f;
			shimmerOn = shimmerNow > 1e-4f;
			wetEnv = std::max(std::max(std::fabs(lastWetL[i]), std::fabs(lastWetR[i])), wetEnv * wetRelease);
			const float duck = wetEnv > 0.5f ? 0.5f / wetEnv : 1.f;
			revL[i] = dryL[i] + std::tanh(sl * shimmerNow * duck);
			revR[i] = dryR[i] + std::tanh(sr2 * shimmerNow * duck);
		}
		bidoo::dsp::reverb::FdnReverbParams p;
		p.rt60 = std::clamp((double)c.decay, 0.1, 30.0);
		p.size = c.room;
		p.brightness = c.tone;
		p.width = 1.0;
		p.modulation = 0.5;
		p.freeze = c.freeze;
		reverb.process(revL, revR, wetL, wetR, n, p);
		for (int i = 0; i < n; i++) {
			lastWetL[i] = wetL[i];
			lastWetR[i] = wetR[i];
			mixNow += mixCoef * (c.mix - mixNow);
			levelNow += mixCoef * (c.level - levelNow);
			const float dg = std::cos(mixNow * 1.5707963f), wg = std::sin(mixNow * 1.5707963f);
			outL[i] = (dryL[i] * dg + wetL[i] * wg) * levelNow;
			outR[i] = (dryR[i] * dg + wetR[i] * wg) * levelNow;
		}

		// ---- for the display
		int count = 0;
		const int64_t old = b.oldest();
		const double len = (double)std::max<int64_t>(1, b.length);
		for (int k = 0; k < kGrains; k++) {
			const Grain &g = grains[k];
			if (g.on && g.buf == cur) {
				double rel = g.pos - (double)old;
				rel -= std::floor(rel / (double)b.cap) * (double)b.cap;
				grainPos[k] = (float)std::clamp(rel / len, 0.0, 1.0);
				grainEnv[k] = envelope((float)g.n / (float)g.len, g.attack, g.soft) * (g.fadeLeft >= 0 ? g.fadeGain : 1.f);
				count++;
			}
			else {
				grainPos[k] = -1.f;
			}
		}
		active = count;
		const float base = wrap01(c.position + (float)scan);
		windowLo = base - 0.5f * c.spray;
		windowHi = base + 0.5f * c.spray;
	}

	// The envelope at u (0..1 of the grain): rises over `attack` of it, falls over the rest; `soft` blends a straight
	// rise and a sharp fall with raised cosines. Zero at both ends.
	float envelope(float u, float attack, float soft) const {
		if (u <= 0.f || u >= 1.f)
			return 0.f;
		const float x = std::min(1.f, u / attack);
		const float y = std::min(1.f, (1.f - u) / (1.f - attack));
		const float y2 = y * y;
		const float rise = x + (cosine(x) - x) * soft;
		const float fall = y2 * y2 + (cosine(y) - y2 * y2) * soft;
		return rise * fall;
	}

	void seed(uint32_t s) {
		rng = s ? s : 1;
	}

private:
	static int clampDiv(int d) {
		return std::clamp(d, 0, kNumDivisions - 1);
	}
	static float wrap01(float x) {
		return x - std::floor(x);
	}
	float rnd() {
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return (float)(rng >> 8) * (1.f / 16777216.f);
	}

	// raised cosine 0..1, from a table
	void buildLut() {
		for (int i = 0; i <= kLut; i++)
			lut[i] = 0.5f - 0.5f * std::cos(3.14159265f * (float)i / (float)kLut);
	}
	float cosine(float x) const {
		const float f = x * (float)kLut;
		const int i = std::min(kLut - 1, (int)f);
		return lut[i] + (lut[i + 1] - lut[i]) * (f - (float)i);
	}

	// A new grain: where it reads, how fast and which way, its envelope and pan. False when all 64 play.
	bool spawn(const Controls &c, const float *voct, int nv, float sizeS) {
		Buffer &b = *cur;
		if (b.length < 64)
			return false;
		Grain *g = nullptr;
		for (auto &x : grains)
			if (!x.on) {
				g = &x;
				break;
			}
		if (!g)
			return false;
		// pitch: V/O in turn, TUNE, DETUNE (free or on octaves / fifths)
		float semis = c.tune + (nv > 0 ? 12.f * voct[(turnVoct++) % nv] : 0.f);
		if (c.detune > 0.f) {
			if (c.detuneQuant == 0) {
				semis += (rnd() * 2.f - 1.f) * 12.f * c.detune * c.detune;
			}
			else if (rnd() < c.detune) {
				static const float oct[] = {-12.f, 12.f};
				static const float fifth[] = {-12.f, 7.f, 12.f, 19.f};
				semis += c.detuneQuant == 1 ? oct[(int)(rnd() * 2.f) % 2] : fifth[(int)(rnd() * 4.f) % 4];
			}
		}
		const double ratio = std::pow(2.0, std::clamp(semis, -48.f, 48.f) / 12.0) * b.rate / sr;
		int dir = c.direction == RANDOM ? (int)(rnd() * 3.f) % 3 : std::clamp(c.direction, 0, 2);
		const int64_t len = std::max<int64_t>(16, (int64_t)(sizeS * sr));
		// how far it reads ahead of and behind where it starts, in frames
		const double span = (double)len * ratio;
		const double ahead = dir == FORWARD ? span : dir == PINGPONG ? span * 0.5 : 0.0;
		const double behind = dir == REVERSE ? span : 0.0;
		// where it may start (frames after the oldest): never across the write head, which moves on while it plays
		const double length = (double)b.length, guard = 64.0;
		const bool rec = recGain > 0.f || c.record;
		const bool full = b.length >= b.cap;
		double lo = behind, hi = length - ahead;
		if (rec) {
			hi = length - guard - std::max(0.0, ahead - (dir == PINGPONG ? 0.5 : 1.0) * (double)len);
			if (full)
				lo = behind + (double)len + guard;
		}
		double p = c.position + scan + (rnd() - 0.5f) * c.spray;
		p -= std::floor(p);
		double start = p * length;
		if (lo <= hi)
			start = std::clamp(start, lo, hi);
		else
			start = std::clamp(0.5 * (lo + hi), 0.0, length);
		g->on = true;
		g->buf = cur;
		g->pos = (double)b.oldest() + start;
		if (g->pos >= (double)b.cap)
			g->pos -= (double)b.cap;
		g->inc = dir == REVERSE ? -ratio : ratio;
		g->n = 0;
		g->len = len;
		g->turn = dir == PINGPONG ? len / 2 : -1;
		const float minAttack = std::min(0.5f, (float)(0.0003 * sr) / (float)len);
		g->attack = minAttack + c.shape * (1.f - 2.f * minAttack);
		g->soft = c.soft;
		// equal power, 1 on both sides in the middle
		const float pan = (rnd() * 2.f - 1.f) * std::clamp(c.width, 0.f, 1.f);
		g->gl = 1.41421356f * std::cos((pan + 1.f) * 0.78539816f);
		g->gr = 1.41421356f * std::sin((pan + 1.f) * 0.78539816f);
		g->fadeLeft = -1;
		g->fadeGain = 1.f;
		return true;
	}

	// A buffer from the UI (a file, a patch): the grains on the old one fade out over 3 ms, then it goes back to be freed
	void takePending() {
		if (draining || !pending.load(std::memory_order_relaxed))
			return;
		Buffer *nb = pending.exchange(nullptr, std::memory_order_acq_rel);
		if (!nb)
			return;
		draining = cur;
		cur = nb;
		view = cur;
		for (auto &g : grains)
			if (g.on && g.buf == draining)
				g.fadeLeft = fadeLen;
		scan = 0.0;
	}
	void retireDraining() {
		if (!draining)
			return;
		for (auto &g : grains)
			if (g.on && g.buf == draining)
				return;
		if (retired.load(std::memory_order_relaxed))
			return; // the UI has not freed the last one yet
		retired.store(draining, std::memory_order_release);
		draining = nullptr;
	}

	static constexpr int kLut = 512;
	float lut[kLut + 1] = {};
	double sr = 48000.0;
	Buffer *cur = nullptr, *draining = nullptr;
	std::array<Grain, kGrains> grains {};
	uint32_t rng = 0x9E3779B9u;
	int turnVoct = 0;
	double scan = 0.0, lastPpq = -1.0, nextFree = 0.0;
	float recGain = 0.f, recStep = 0.001f;
	float grainGain = 1.f, gainCoef = 0.001f;
	int fadeLen = 144;
	bidoo::dsp::reverb::FdnReverb reverb;
	bidoo::dsp::GranularPitchShifter shift[2];
	float lastWetL[kBlock] = {}, lastWetR[kBlock] = {};
	float shimmerNow = 0.f, wetEnv = 0.f, wetRelease = 0.9999f;
	bool shimmerOn = false;
	float mixNow = 0.3f, levelNow = 1.f, mixCoef = 0.01f;
};

}  // namespace particules
