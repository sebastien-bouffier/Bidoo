#pragma once
// sOUNAiRe engine: a sample, its analysis (normalize, root note, zero crossings,
// overview), and the voices that play it (loops with crossfades, reverse,
// declicking, multimode filter, envelope, optional Signalsmith time-stretch).
// No Rack dependency, so the harness can drive it directly.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <signalsmith-stretch/signalsmith-stretch.h>

namespace sounaire {

constexpr int MAX_VOICES = 16;
constexpr int STRETCH_POOL = 6;   // stretched voices at once (the rest repitch)
constexpr int STRETCH_BLOCK = 32;

enum LoopMode { LOOP_OFF, LOOP_FORWARD, LOOP_PINGPONG, LOOP_REVERSE, NUM_LOOP_MODES };
enum TimeMode { TIME_REPITCH, TIME_STRETCH, TIME_STRETCH_CHEAP, TIME_STRETCH_FORMANT, NUM_TIME_MODES };
enum FilterMode { FILTER_OFF, FILTER_LP, FILTER_HP, FILTER_BP, FILTER_NOTCH, NUM_FILTER_MODES };

inline float noteHz(float note) {
	return 440.f * std::pow(2.f, (note - 69.f) / 12.f);
}

// ---------------------------------------------------------------- sample

struct Sample {
	std::vector<float> ch[2];
	int channels = 1;
	int64_t frames = 0;
	float rate = 48000.f;
	int bits = 24;          // bit depth to keep when stored as FLAC
	float peak = 0.f;
	std::string name;
	static constexpr int OVERVIEW = 2048;
	std::vector<float> ovMin, ovMax;

	float at(int c, int64_t i) const {
		if (i < 0 || i >= frames)
			return 0.f;
		return ch[c < channels ? c : 0][(size_t)i];
	}
	float mono(int64_t i) const {
		return channels == 2 ? 0.5f * (at(0, i) + at(1, i)) : at(0, i);
	}
	// 4-point Hermite at a fractional frame
	float read(int c, double pos) const {
		int64_t i = (int64_t)std::floor(pos);
		float f = (float)(pos - (double)i);
		float y0 = at(c, i - 1), y1 = at(c, i), y2 = at(c, i + 1), y3 = at(c, i + 2);
		float c1 = 0.5f * (y2 - y0);
		float c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
		float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
		return ((c3 * f + c2) * f + c1) * f + y1;
	}
	void analyse() {
		peak = 0.f;
		for (int c = 0; c < channels; c++)
			for (float v : ch[c])
				peak = std::max(peak, std::fabs(v));
		ovMin.assign(OVERVIEW, 0.f);
		ovMax.assign(OVERVIEW, 0.f);
		if (frames <= 0)
			return;
		for (int b = 0; b < OVERVIEW; b++) {
			int64_t a = frames * b / OVERVIEW, e = std::max(a + 1, frames * (b + 1) / OVERVIEW);
			float lo = 1e9f, hi = -1e9f;
			for (int64_t i = a; i < e && i < frames; i++) {
				float v = mono(i);
				lo = std::min(lo, v);
				hi = std::max(hi, v);
			}
			ovMin[b] = lo > hi ? 0.f : lo;
			ovMax[b] = lo > hi ? 0.f : hi;
		}
	}
	// nearest zero crossing (mono sign change) to `i`, within `radius` frames
	int64_t zeroCrossing(int64_t i, int64_t radius = 4096) const {
		if (frames < 2)
			return i;
		i = std::max<int64_t>(0, std::min(frames - 1, i));
		for (int64_t d = 0; d <= radius; d++) {
			for (int64_t j : {i - d, i + d}) {
				if (j <= 0 || j >= frames)
					continue;
				float a = mono(j - 1), b = mono(j);
				if ((a <= 0.f && b > 0.f) || (a >= 0.f && b < 0.f) || b == 0.f)
					return std::fabs(a) < std::fabs(b) ? j - 1 : j;
			}
		}
		return i;
	}
};

// YIN (de Cheveigne & Kawahara 2002) over a region: the median pitch of the
// voiced frames, as a MIDI note (fractional). Returns -1 when nothing is pitched.
inline float detectRootNote(const Sample &s, int64_t from, int64_t to) {
	from = std::max<int64_t>(0, from);
	to = std::min(s.frames, to);
	const float sr = s.rate;
	const int W = std::max(256, (int)(0.025f * sr));     // integration window: 25 ms (down to 40 Hz)
	const int maxLag = std::min(W, (int)(sr / 40.f));
	const int minLag = std::max(2, (int)(sr / 2000.f));
	const int hop = std::max(64, W / 2);
	std::vector<float> d(maxLag + 2), x(W + maxLag + 2);
	std::vector<float> pitches;
	for (int64_t start = from; start + W + maxLag < to; start += hop) {
		double energy = 0;
		for (int i = 0; i < W + maxLag; i++) {
			x[i] = s.mono(start + i);
			if (i < W)
				energy += (double)x[i] * x[i];
		}
		if (energy / W < 1e-6)
			continue;  // silence
		d[0] = 1.f;
		double run = 0;
		int found = -1;
		for (int tau = 1; tau <= maxLag; tau++) {
			double sum = 0;
			for (int i = 0; i < W; i++) {
				float e = x[i] - x[i + tau];
				sum += (double)e * e;
			}
			run += sum;
			d[tau] = run > 0 ? (float)(sum * tau / run) : 1.f;
		}
		for (int tau = minLag; tau < maxLag; tau++) {
			if (d[tau] < 0.12f) {
				while (tau + 1 < maxLag && d[tau + 1] < d[tau])
					tau++;
				found = tau;
				break;
			}
		}
		if (found < 0)
			continue;
		// parabolic interpolation of the dip
		float a = d[found - 1], b = d[found], c = d[found + 1];
		float den = a - 2.f * b + c;
		float t = found + (std::fabs(den) > 1e-9f ? 0.5f * (a - c) / den : 0.f);
		float hz = sr / t;
		pitches.push_back(69.f + 12.f * std::log2(hz / 440.f));
	}
	if (pitches.size() < 3)
		return -1.f;
	std::sort(pitches.begin(), pitches.end());
	return pitches[pitches.size() / 2];
}

// ---------------------------------------------------------------- slices

constexpr int MAX_SLICES = 128;
enum PlayType { PLAY_ONE_SHOT, PLAY_GATE, PLAY_LOOP, PLAY_PINGPONG, PLAY_REVERSE, PLAY_REVERSE_LOOP, NUM_PLAY_TYPES };

// The slices of the sample, in its frames: slice i runs from start[i] to
// start[i + 1] (the last one to the end of the sample); start[0] is 0.
struct Slices {
	int count = 1;
	int64_t start[MAX_SLICES] = {};
	uint8_t type[MAX_SLICES] = {};
	uint8_t choke[MAX_SLICES] = {};   // 0: none, 1-4: a slice cuts the others of its group

	int64_t end(int i, int64_t frames) const {
		return i + 1 < count ? start[i + 1] : frames;
	}
	// the slice holding frame f
	int at(int64_t f) const {
		int i = 0;
		while (i + 1 < count && start[i + 1] <= f)
			i++;
		return i;
	}
};

// Transients: where the energy of the sample's slope (its first difference,
// so the attacks stand out) rises by `riseDb` over the 20 ms before, at least
// 60 ms apart, each placed at the zero crossing just before its attack.
inline std::vector<int64_t> detectTransients(const Sample &s, float riseDb) {
	std::vector<int64_t> out {0};
	const int H = std::max(64, (int)(s.rate * 0.005f));   // 5 ms hops
	const int64_t n = s.frames / H;
	if (n < 8)
		return out;
	std::vector<float> L((size_t)n);
	float top = -240.f;
	for (int64_t i = 0; i < n; i++) {
		double e = 0;
		for (int64_t k = i * H; k < (i + 1) * H; k++) {
			float d = s.mono(k) - s.mono(k - 1);
			e += (double)d * d;
		}
		L[(size_t)i] = 10.f * std::log10((float)(e / H) + 1e-12f);
		top = std::max(top, L[(size_t)i]);
	}
	const int back = 4;
	const int64_t gap = (int64_t)(0.06f * s.rate);
	// a hit in the first 10 ms is the start of slice 1; later ones may follow it closely
	const int64_t lead = (int64_t)(0.01f * s.rate);
	int64_t last = -gap;
	for (int64_t i = back; i < n && (int)out.size() < MAX_SLICES; i++) {
		float before = 0.f;
		for (int j = 1; j <= back; j++)
			before += L[(size_t)(i - j)];
		before /= back;
		// a rise, and loud enough not to be noise in a quiet tail
		if (L[(size_t)i] - before < riseDb || L[(size_t)i] < top - 40.f)
			continue;
		// the attack: the first frame of this hop and the one before whose slope
		// reaches a quarter of the steepest there
		int64_t a = (i - 1) * H, b = (i + 1) * H;
		float steep = 0.f;
		for (int64_t k = a; k < b; k++)
			steep = std::max(steep, std::fabs(s.mono(k) - s.mono(k - 1)));
		int64_t pos = a;
		for (int64_t k = a; k < b; k++)
			if (std::fabs(s.mono(k) - s.mono(k - 1)) >= 0.25f * steep) {
				pos = k;
				break;
			}
		// back to the zero crossing before it, at most 2 ms
		int64_t limit = std::max<int64_t>(1, pos - (int64_t)(0.002f * s.rate));
		for (int64_t k = pos; k > limit; k--) {
			float x0 = s.mono(k - 1), x1 = s.mono(k);
			if ((x0 <= 0.f && x1 > 0.f) || (x0 >= 0.f && x1 < 0.f)) {
				pos = k;
				break;
			}
		}
		if (pos < lead || pos - last < gap)
			continue;
		out.push_back(pos);
		last = pos;
	}
	return out;
}

// ---------------------------------------------------------------- filter

struct Svf {
	float s1 = 0.f, s2 = 0.f;
	void reset() { s1 = s2 = 0.f; }
	// returns (lp, bp, hp) through the references
	void tick(float x, float g, float k, float h, float &lp, float &bp, float &hp) {
		hp = (x - (k + g) * s1 - s2) * h;
		bp = g * hp + s1;
		lp = g * bp + s2;
		s1 = g * hp + bp;
		s2 = g * bp + lp;
	}
};

struct FilterSettings {
	int mode = FILTER_OFF;
	int stages = 2;       // 12 dB/oct each
	float cutoffNote = 120.f;
	float resonance = 0.f;  // 0..1
};

// a cascade of 1-4 state-variable stages; Butterworth damping on all but the
// last, whose damping the resonance lowers
struct VoiceFilter {
	Svf st[2][4];
	float g = 0.f, gk[4] = {}, h[4] = {};
	void reset() {
		for (auto &c : st)
			for (Svf &s : c)
				s.reset();
	}
	void set(const FilterSettings &f, float note, float sampleRate) {
		float hz = std::min(noteHz(note), 0.45f * sampleRate);
		g = std::tan((float)M_PI * hz / sampleRate);
		static const float bw[4][4] = {
			{1.4142f, 0, 0, 0},
			{0.7654f, 1.8478f, 0, 0},
			{0.5176f, 1.4142f, 1.9319f, 0},
			{0.3902f, 1.1111f, 1.6629f, 1.9616f},
		};
		int n = std::max(1, std::min(4, f.stages));
		for (int i = 0; i < n; i++) {
			float k = bw[n - 1][i];
			if (i == n - 1)
				k = std::max(0.05f, k * (1.f - 0.97f * f.resonance));
			gk[i] = k;
			h[i] = 1.f / (1.f + k * g + g * g);
		}
	}
	float process(int c, float x, const FilterSettings &f) {
		if (f.mode == FILTER_OFF)
			return x;
		int n = std::max(1, std::min(4, f.stages));
		for (int i = 0; i < n; i++) {
			float lp, bp, hp;
			st[c][i].tick(x, g, gk[i], h[i], lp, bp, hp);
			switch (f.mode) {
				case FILTER_LP: x = lp; break;
				case FILTER_HP: x = hp; break;
				case FILTER_BP: x = bp * gk[i]; break;   // unity gain at the centre
				default: x = lp + hp; break;             // notch
			}
		}
		return x;
	}
};

// ---------------------------------------------------------------- playback

// what a voice needs from the module, read once per block
struct PlaySettings {
	double start = 0, end = 1, loopStart = 0, loopEnd = 1;  // 0..1 of the sample
	int loopMode = LOOP_OFF;
	float xfadeMs = 10.f;
	bool reverse = false;
	int timeMode = TIME_REPITCH;
	float speed = 1.f;         // stretch modes: playback speed; repitch: rate factor too
	float attackMs = 1.f, releaseMs = 50.f;
	float gain = 1.f;          // normalize and level
	FilterSettings filter;
};

// The play head runs in "play space": the sample reversed when REVERSE is on,
// so loops and crossfades are written once.
struct PlayHead {
	const Sample *s = nullptr;
	double v = 0;      // position in play space
	int dir = 1;
	bool inLoop = false;
	bool wrapped = false;   // the loop went round at least once
	bool done = false;

	struct Region {
		double start, end, ls, le, x;
		int mode;
		bool reverse;
		bool tail = false;   // the crossfade uses the sample after the seam (see region())
	};

	static Region region(const Sample &s, const PlaySettings &p, float engineRate) {
		double F = (double)s.frames;
		double a = std::min(p.start, p.end) * F, b = std::max(p.start, p.end) * F;
		double la = std::min(p.loopStart, p.loopEnd) * F, lb = std::max(p.loopStart, p.loopEnd) * F;
		Region r;
		r.reverse = p.reverse;
		if (p.reverse) {
			r.start = F - b; r.end = F - a; r.ls = F - lb; r.le = F - la;
		}
		else {
			r.start = a; r.end = b; r.ls = la; r.le = lb;
		}
		r.mode = (lb - la >= 16) ? p.loopMode : LOOP_OFF;
		// crossfade: at least 1 ms (no click at the seam), at most half the loop.
		// A forward loop fades its end into the sample before its start; when
		// that is too short (a loop from the start of the sample, slice 1) it
		// fades the sample after its end into its start instead ("tail").
		// A reverse loop the other way round.
		double x = std::max(1.0, (double)p.xfadeMs) * 0.001 * s.rate;
		x = std::min(x, 0.5 * (r.le - r.ls));
		double before = r.ls, after = F - 1 - r.le;
		double usual = r.mode == LOOP_REVERSE ? after : before;
		double other = r.mode == LOOP_REVERSE ? before : after;
		r.tail = false;
		if (r.mode == LOOP_FORWARD || r.mode == LOOP_REVERSE) {
			if (usual < x && other > usual) {
				r.tail = true;
				x = std::min(x, other);
			}
			else
				x = std::min(x, usual);
		}
		r.x = std::max(0.0, x);
		(void)engineRate;
		return r;
	}

	float get(int c, double pos, bool reverse) const {
		return reverse ? s->read(c, (double)s->frames - 1.0 - pos) : s->read(c, pos);
	}

	// one output frame, then the head moves by `step` play-space frames
	void next(const Region &r, double step, float &l, float &rr) {
		if (done) {
			l = rr = 0.f;
			return;
		}
		int c1 = s->channels > 1 ? 1 : 0;
		double L = r.le - r.ls;
		l = get(0, v, r.reverse);
		rr = get(c1, v, r.reverse);
		if (inLoop && r.x > 0 && r.tail) {
			// after each wrap: what followed the seam fades into the loop's start
			if (wrapped && r.mode == LOOP_FORWARD && v < r.ls + r.x) {
				float t = 1.f - (float)((v - r.ls) / r.x);
				l += t * (get(0, v + L, r.reverse) - l);
				rr += t * (get(c1, v + L, r.reverse) - rr);
			}
			else if (wrapped && r.mode == LOOP_REVERSE && v > r.le - r.x) {
				float t = 1.f - (float)((r.le - v) / r.x);
				l += t * (get(0, v - L, r.reverse) - l);
				rr += t * (get(c1, v - L, r.reverse) - rr);
			}
		}
		else if (inLoop && r.x > 0) {
			if (r.mode == LOOP_FORWARD && v > r.le - r.x) {
				float t = (float)((v - (r.le - r.x)) / r.x);
				l += t * (get(0, v - L, r.reverse) - l);
				rr += t * (get(c1, v - L, r.reverse) - rr);
			}
			else if (r.mode == LOOP_REVERSE && v < r.ls + r.x) {
				float t = (float)(((r.ls + r.x) - v) / r.x);
				l += t * (get(0, v + L, r.reverse) - l);
				rr += t * (get(c1, v + L, r.reverse) - rr);
			}
		}
		v += step * dir;
		if (r.mode != LOOP_OFF && !inLoop && v >= r.ls && v < r.le)
			inLoop = true;
		if (inLoop && r.mode != LOOP_OFF && L > 0) {
			switch (r.mode) {
				case LOOP_FORWARD:
					while (v >= r.le) {
						v -= L;
						wrapped = true;
					}
					break;
				case LOOP_PINGPONG:
					if (v >= r.le) { v = 2 * r.le - v; dir = -1; }
					if (v <= r.ls) { v = 2 * r.ls - v; dir = 1; }
					break;
				case LOOP_REVERSE:
					dir = -1;
					while (v <= r.ls) {
						v += L;
						wrapped = true;
					}
					break;
			}
		}
		else if (v >= r.end || v < 0) {
			done = true;
		}
	}
};

// Signalsmith Stretch fed a "tape": the sample as it plays at its own speed
// (loops, direction and end applied), read at `speed` and transposed.
struct Stretcher {
	signalsmith::stretch::SignalsmithStretch<float> st {0x5eed};
	int tapeMask = 0;
	std::vector<float> tape[2];
	int64_t written = 0, consumed = 0;
	float out[2][STRETCH_BLOCK] = {};
	int outPos = STRETCH_BLOCK;
	double acc = 0;
	bool started = false, inUse = false;
	int mode = -1;
	float rate = 0.f;

	void prepare(float sampleRate, int m) {
		if (rate == sampleRate && mode == m)
			return;
		rate = sampleRate;
		mode = m;
		if (m == TIME_STRETCH_CHEAP)
			st.presetCheaper(2, sampleRate);
		else
			st.presetDefault(2, sampleRate);
		int need = 1;
		while (need < (int)(sampleRate * 2.f))
			need <<= 1;
		tape[0].assign(need, 0.f);
		tape[1].assign(need, 0.f);
		tapeMask = need - 1;
	}
	void begin() {
		written = consumed = 0;
		outPos = STRETCH_BLOCK;
		acc = 0;
		started = false;
	}
	struct Channel {
		const float *d;
		int64_t base;
		int mask;
		float operator[](int i) const {
			int64_t k = base + i;
			return k < 0 ? 0.f : d[k & mask];
		}
	};
	struct Input {
		const Stretcher *s;
		int64_t base;
		Channel operator[](int c) const { return {s->tape[c].data(), base, s->tapeMask}; }
	};
};

struct Voice {
	bool active = false;
	bool gate = false;
	bool fadingOut = false;   // retriggered or stolen: a 3 ms fade, then off
	int outChannel = 0;
	const Sample *sample = nullptr;
	PlayHead head;
	VoiceFilter filter;
	float env = 0.f;
	float fadeGain = 1.f;
	float pitchSemis = 0.f;   // from the root note, bend and tune included
	float velocity = 1.f;
	float cutoffNote = 120.f;
	float speed = 1.f;        // the knob plus this channel's CV
	float resonance = 0.f;    // the knob plus this channel's CV
	int stretch = -1;         // index in the stretch pool, -1: repitch
	int tail = 0;             // stretch: output frames left once the tape has ended
	int filterCount = 0;
	int64_t age = 0;
	float ratioSemis = 1e9f;   // the pitch the cached ratio was worked out for
	double ratio = 1.0;
	// SLICES mode: the voice plays its own slice region; a one shot plays to
	// the end of it whatever the gate does
	bool ownRegion = false;
	bool oneShot = false;
	int choke = 0;            // the slice's choke group
	PlayHead::Region region {};
};

// Everything that plays: 16 channels, each with a spare slot for the fade-out of
// a retriggered note, plus the TRIG slice, the clicked key and the preview
// voice. Audio thread only.
struct Engine {
	static constexpr int SLOTS = MAX_VOICES * 2 + 3;
	static constexpr int TRIGGER = SLOTS - 3;   // a slice from the TRIG input
	static constexpr int MOUSE = SLOTS - 2;     // a key clicked on the keyboard strip
	static constexpr int PREVIEW = SLOTS - 1;
	Voice voices[SLOTS];
	Stretcher *pool = nullptr;   // STRETCH_POOL stretchers, built by the worker
	float rate = 48000.f;
	int poolMode = -1;           // the preset the pool runs, -1: none yet
	float poolRate = 0.f;

	const Sample *current = nullptr;
	// samples replaced while voices still played them (hot-swap)
	const Sample *old[8] = {};

	void prepare(float sampleRate) {
		rate = sampleRate;
		for (Voice &v : voices) {
			v.active = false;
			v.stretch = -1;
		}
	}

	static int presetFor(int timeMode) {
		return timeMode == TIME_STRETCH_CHEAP ? TIME_STRETCH_CHEAP : TIME_STRETCH;
	}

	// a pool of stretchers for a preset and a rate, warmed up so their first
	// notes cost no more than the next ones (worker thread)
	static Stretcher *buildPool(float sampleRate, int preset) {
		Stretcher *p = new Stretcher[STRETCH_POOL];
		float out[2][STRETCH_BLOCK];
		float *o[2] = {out[0], out[1]};
		for (int i = 0; i < STRETCH_POOL; i++) {
			Stretcher &s = p[i];
			s.prepare(sampleRate, preset);
			s.begin();
			int len = s.st.outputSeekLength(1.f);
			s.st.outputSeek(Stretcher::Input{&s, 0}, len);
			for (int b = 0; b < 400; b++)
				s.st.process(Stretcher::Input{&s, len + b * STRETCH_BLOCK}, STRETCH_BLOCK, o, STRETCH_BLOCK);
			s.st.reset();
			s.begin();
		}
		return p;
	}

	// takes a pool from the worker; returns the one it replaces, for the worker to free
	Stretcher *setPool(Stretcher *p, int preset, float sampleRate) {
		for (Voice &v : voices) {
			if (v.stretch >= 0) {
				v.active = false;   // a stretched voice cannot move to another pool
				v.stretch = -1;
			}
		}
		Stretcher *old = pool;
		pool = p;
		poolMode = preset;
		poolRate = sampleRate;
		return old;
	}

	bool poolReady(int timeMode) const {
		return pool && poolMode == presetFor(timeMode) && poolRate == rate;
	}

	// hot-swap: new notes take the new sample, playing ones keep theirs
	void setSample(const Sample *s) {
		if (s == current)
			return;
		if (current) {
			for (const Sample *&o : old) {
				if (!o) {
					o = current;
					current = s;
					return;
				}
			}
			// no room left: stop whatever still plays the oldest one
			for (Voice &v : voices)
				if (v.sample == old[0])
					v.active = false;
			for (int i = 0; i < 7; i++)
				old[i] = old[i + 1];
			old[7] = current;
		}
		current = s;
	}

	// a replaced sample no voice uses any more, for the worker to free
	const Sample *takeUnused() {
		for (const Sample *&o : old) {
			if (!o)
				continue;
			bool used = false;
			for (const Voice &v : voices)
				used |= v.active && v.sample == o;
			if (!used) {
				const Sample *r = o;
				o = nullptr;
				return r;
			}
		}
		return nullptr;
	}

	int freeStretcher() {
		if (!pool)
			return -1;
		for (int i = 0; i < STRETCH_POOL; i++)
			if (!pool[i].inUse)
				return i;
		return -1;
	}

	void release(Voice &v) {
		if (v.stretch >= 0 && pool)
			pool[v.stretch].inUse = false;
		v.stretch = -1;
		v.active = false;
	}

	// the voice on `slot` moves to a spare slot and fades out there
	void fadeOut(int slot) {
		Voice &v = voices[slot];
		if (!v.active)
			return;
		int spare = -1;
		for (int i = MAX_VOICES; i < 2 * MAX_VOICES; i++) {
			if (!voices[i].active) {
				spare = i;
				break;
			}
		}
		if (spare < 0) {
			// all spares busy: cut the quietest fade
			spare = MAX_VOICES;
			for (int i = MAX_VOICES; i < 2 * MAX_VOICES; i++)
				if (voices[i].fadeGain < voices[spare].fadeGain)
					spare = i;
			release(voices[spare]);
		}
		voices[spare] = v;
		voices[spare].fadingOut = true;
		v.active = false;
		v.stretch = -1;   // the stretcher went with the fading copy
	}

	// `own`: a slice's start, end, loop and direction instead of the module's
	void noteOn(int slot, int outChannel, float semis, float velocity, const PlaySettings &p, double startOffset, float cutoffNote,
	            const PlaySettings *own = nullptr, bool oneShot = false) {
		if (!current || current->frames < 4)
			return;
		fadeOut(slot);
		Voice &v = voices[slot];
		PlayHead::Region r = PlayHead::region(*current, own ? *own : p, rate);
		v = Voice();
		v.active = true;
		v.gate = true;
		v.outChannel = outChannel;
		v.sample = current;
		v.head.s = current;
		v.head.v = std::max(r.start, std::min(r.end - 1, r.start + startOffset * (r.end - r.start)));
		v.pitchSemis = semis;
		v.velocity = velocity;
		v.cutoffNote = cutoffNote;
		v.speed = p.speed;
		v.resonance = p.filter.resonance;
		v.ownRegion = own != nullptr;
		v.oneShot = oneShot;
		v.region = r;
		v.filter.reset();
		// stretched when a warmed-up pool is there and has a free stretcher, else repitched
		if (p.timeMode != TIME_REPITCH && poolReady(p.timeMode)) {
			v.stretch = freeStretcher();
			if (v.stretch >= 0) {
				pool[v.stretch].inUse = true;
				pool[v.stretch].begin();
			}
		}
	}

	void noteOff(int slot) {
		if (!voices[slot].oneShot)
			voices[slot].gate = false;
	}

	// Alt+click: plays the sample at its own pitch from `pos` (0..1) while held
	void previewOn(double pos, const PlaySettings &p) {
		if (!current)
			return;
		Voice &v = voices[PREVIEW];
		v = Voice();
		v.active = true;
		v.gate = true;
		v.sample = current;
		v.head.s = current;
		v.head.v = std::max(0.0, std::min(1.0, pos)) * current->frames;
		(void)p;
	}
	void previewOff() {
		voices[PREVIEW].gate = false;
	}

	void envelope(Voice &v, const PlaySettings &p) {
		if (v.gate) {
			// never faster than 1 ms: a note starting mid-wave would click
			float a = 1.f / (std::max(1.f, p.attackMs) * 0.001f * rate);
			v.env = std::min(1.f, v.env + a);
		}
		else {
			float rel = std::max(2.f, p.releaseMs) * 0.001f * rate;
			v.env = std::max(0.f, v.env - 1.f / rel);
		}
		if (v.fadingOut) {
			v.fadeGain -= 1.f / (0.003f * rate);
			if (v.fadeGain <= 0.f)
				release(v);
		}
	}

	// a 2 ms fade before the end of a sample that does not loop
	float endFade(const Voice &v, const PlayHead::Region &r, double step) const {
		if ((r.mode != LOOP_OFF && v.head.inLoop) || step <= 0)
			return 1.f;
		double left = (r.end - v.head.v) / step;
		return (float)std::max(0.0, std::min(1.0, left / (0.002 * rate)));
	}

	// one frame of every voice, added into outL/outR by output channel
	void process(const PlaySettings &p, float *outL, float *outR) {
		// one stretcher start per frame: a chord's start-ups spread over a few samples
		bool seekFree = true;
		PlayHead::Region mainRegion {};
		if (current)
			mainRegion = PlayHead::region(*current, p, rate);
		for (int i = 0; i < SLOTS; i++) {
			Voice &v = voices[i];
			if (!v.active || !v.sample)
				continue;
			const Sample &s = *v.sample;
			bool preview = i == PREVIEW;
			PlaySettings ps;
			const PlaySettings &pr = p;
			if (!preview) {
				ps = pr;
				ps.speed = v.speed;
				ps.filter.resonance = v.resonance;
			}
			else {
				ps = pr;
				ps.loopMode = LOOP_OFF;
				ps.reverse = false;
				ps.start = 0;
				ps.end = 1;
				ps.filter.mode = FILTER_OFF;
			}
			PlayHead::Region r = v.ownRegion ? v.region : ((!preview && v.sample == current) ? mainRegion : PlayHead::region(s, ps, rate));
			const double srcRatio = s.rate / rate;
			float l = 0.f, rr = 0.f;
			double step = srcRatio;
			if (v.stretch >= 0 && !preview) {
				Stretcher &st = pool[v.stretch];
				if (!st.started && !seekFree) {
					v.age++;
					continue;   // starts on a later frame
				}
				if (st.outPos >= STRETCH_BLOCK) {
					if (!st.started) {
						seekFree = false;
						int seekLen = st.st.outputSeekLength(std::min(std::max(ps.speed, 0.25f), 4.f));
						while (st.written < seekLen) {
							float a, b;
							v.head.next(r, srcRatio, a, b);
							st.tape[0][st.written & st.tapeMask] = a;
							st.tape[1][st.written & st.tapeMask] = b;
							st.written++;
						}
						st.st.setFormantFactor(1.f, ps.timeMode == TIME_STRETCH_FORMANT);
						st.st.setTransposeSemitones(v.pitchSemis);
						st.st.outputSeek(Stretcher::Input{&st, 0}, seekLen);
						st.consumed = seekLen;
						st.started = true;
					}
					st.st.setFormantFactor(1.f, ps.timeMode == TIME_STRETCH_FORMANT);
					st.st.setTransposeSemitones(v.pitchSemis);
					int count = (int)std::floor(st.acc + ps.speed * STRETCH_BLOCK);
					st.acc += ps.speed * STRETCH_BLOCK - count;
					while (st.written < st.consumed + count) {
						float a, b;
						v.head.next(r, srcRatio, a, b);
						st.tape[0][st.written & st.tapeMask] = a;
						st.tape[1][st.written & st.tapeMask] = b;
						st.written++;
					}
					float *o[2] = {st.out[0], st.out[1]};
					st.st.process(Stretcher::Input{&st, st.consumed}, count, o, STRETCH_BLOCK);
					st.consumed += count;
					st.outPos = 0;
					if (v.head.done && v.tail == 0)
						v.tail = st.st.outputLatency() + (int)(st.st.inputLatency() / std::max(ps.speed, 0.0625f)) + 2 * STRETCH_BLOCK;
				}
				l = st.out[0][st.outPos];
				rr = st.out[1][st.outPos];
				st.outPos++;
				if (v.tail > 0 && --v.tail == 0)
					v.gate = false, v.env = 0.f;
			}
			else {
				if (v.pitchSemis != v.ratioSemis) {
					v.ratioSemis = v.pitchSemis;
					v.ratio = std::exp2((double)v.pitchSemis / 12.0);
				}
				step = srcRatio * v.ratio * (preview ? 1.0 : (double)ps.speed);
				float fade = endFade(v, r, step);
				v.head.next(r, step, l, rr);
				l *= fade;
				rr *= fade;
				if (v.head.done && !v.fadingOut)
					v.gate = false, v.env = 0.f;
			}
			envelope(v, ps);
			if (!v.active)
				continue;
			// filter coefficients every 16 frames
			if (ps.filter.mode != FILTER_OFF) {
				if (v.filterCount-- <= 0) {
					v.filter.set(ps.filter, v.cutoffNote, rate);
					v.filterCount = 15;
				}
				l = v.filter.process(0, l, ps.filter);
				rr = v.filter.process(1, rr, ps.filter);
			}
			float g = v.env * v.fadeGain * v.velocity * p.gain;
			outL[v.outChannel] += l * g;
			outR[v.outChannel] += rr * g;
			v.age++;
			if (!v.gate && v.env <= 0.f)
				release(v);
		}
	}
};

}  // namespace sounaire
