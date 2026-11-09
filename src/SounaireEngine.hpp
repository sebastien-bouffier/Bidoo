#pragma once
// sOUNAiRe engine: a sample, its analysis (normalize, root note, zero crossings,
// overview), and the voices that play it (loops with crossfades, reverse,
// declicking, multimode filter, envelope, optional Signalsmith time-stretch).
// No Rack dependency, so the harness can drive it directly.
#include "dep/pffft/pffft.h"
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

// ---------------------------------------------------------------- band-limited reading

// A Kaiser-windowed sinc over Z zero crossings, tabulated RES points per crossing.
inline double besselI0(double x) {
	double s = 1, term = 1;
	for (int k = 1; k < 40; k++) {
		term *= (x / (2 * k)) * (x / (2 * k));
		s += term;
	}
	return s;
}

template <int Z, int RES>
struct SincTable {
	static constexpr int ZEROS = Z;
	static constexpr int RES_ = RES;
	static constexpr int LAST = Z * RES;
	float t[Z * RES + 4];
	explicit SincTable(double beta) {
		double norm = besselI0(beta);
		for (int i = 0; i <= Z * RES + 3; i++) {
			double u = (double)i / RES, r = u / Z;
			double w = r < 1 ? besselI0(beta * std::sqrt(1 - r * r)) / norm : 0;
			t[i] = (float)(w * (u == 0 ? 1.0 : std::sin(M_PI * u) / (M_PI * u)));
		}
	}
	// u: distance in zero crossings, >= 0
	float operator()(float u) const {
		float x = u * RES;
		int i = (int)x;
		if (i >= Z * RES)
			return 0.f;
		float f = x - (float)i;
		return t[i] + f * (t[i + 1] - t[i]);
	}
};

// playback: short (4 zero crossings), cheap enough for 16 voices
inline const SincTable<4, 512> &playKernel() {
	static const SincTable<4, 512> k(6.0);
	return k;
}
// load-time resampling and the octave copies: long, clean (worker thread)
inline const SincTable<24, 256> &longKernel() {
	static const SincTable<24, 256> k(9.0);
	return k;
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
	// a copy made for playback (resampled to the engine's rate) points at the
	// sample it was made from; slices and markers are in that one's frames
	const Sample *origin = this;
	int64_t originFrames = 0;
	// octave copies for reading pitched up without aliasing: level k (1..)
	// at 1/2^k of the rate, low-passed before each halving
	static constexpr int MIPS = 4;
	std::vector<float> mip[MIPS][2];
	int64_t mipFrames[MIPS] = {};
	int mipLevels = 0;
	bool mipsBuilt = false;   // set before the audio thread ever sees the sample

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
	// both channels at `pos` (frames of level 0) from octave copy `level`
	// (0: the sample), low-passed at `cut` of that copy's Nyquist
	void readBandLimited(int level, float cut, double pos, float &l, float &r) const {
		const float *a = level ? mip[level - 1][0].data() : ch[0].data();
		const float *b = channels > 1 ? (level ? mip[level - 1][1].data() : ch[1].data()) : a;
		int64_t n = level ? mipFrames[level - 1] : frames;
		double p = pos * (1.0 / (double)(1 << level));
		const auto &K = playKernel();
		double half = K.ZEROS / cut;
		int64_t i0 = (int64_t)std::ceil(p - half), i1 = (int64_t)std::floor(p + half);
		// the table position of tap k: (p - i0 - k) cut, from the far left side down through 0
		const float stepX = cut * K.RES_;
		const float x0 = (float)(p - (double)i0) * stepX;
		const int taps = (int)(i1 - i0) + 1;
		float sl = 0.f, sr = 0.f, sw = 0.f;
		const float *t = K.t;
		if (i0 >= 0 && i1 < n) {
			const float *pa = a + i0, *pb = b + i0;
			float sl2 = 0.f, sr2 = 0.f, sw2 = 0.f;
			int k = 0;
			// two taps a turn, two sums: the adds do not wait on each other
			for (; k + 1 < taps; k += 2) {
				float ax = std::fabs(x0 - (float)k * stepX), bx = std::fabs(x0 - (float)(k + 1) * stepX);
				int ja = (int)ax, jb = (int)bx;
				float wa = t[ja] + (ax - (float)ja) * (t[ja + 1] - t[ja]);
				float wb = t[jb] + (bx - (float)jb) * (t[jb + 1] - t[jb]);
				sw += wa;
				sw2 += wb;
				sl += wa * pa[k];
				sl2 += wb * pa[k + 1];
				sr += wa * pb[k];
				sr2 += wb * pb[k + 1];
			}
			for (; k < taps; k++) {
				float ax = std::fabs(x0 - (float)k * stepX);
				int ja = (int)ax;
				float wa = t[ja] + (ax - (float)ja) * (t[ja + 1] - t[ja]);
				sw += wa;
				sl += wa * pa[k];
				sr += wa * pb[k];
			}
			sw += sw2;
			sl += sl2;
			sr += sr2;
		}
		else {
			float x = x0;
			for (int64_t i = i0; i <= i1; i++, x -= stepX) {
				float ax = std::fabs(x);
				int j = std::min((int)ax, K.LAST);
				float w = t[j] + (ax - (float)j) * (t[j + 1] - t[j]);
				sw += w;
				if (i >= 0 && i < n) {
					sl += w * a[i];
					sr += w * b[i];
				}
			}
		}
		float k = sw > 0.f ? 1.f / sw : 0.f;
		l = sl * k;
		r = sr * k;
	}
	// the octave copies (worker thread, before the audio thread sees the sample)
	void buildMips() {
		mipLevels = 0;
		// half-band low-pass: cut at 0.45 of the old Nyquist (the new one is 0.5)
		const double cut = 0.45, beta = 8.0;
		const int M = 26;
		std::vector<float> h(2 * M + 1);
		double sum = 0;
		for (int k = -M; k <= M; k++) {
			double u = k * cut, r = (double)k / (M + 1);
			double w = besselI0(beta * std::sqrt(1 - r * r)) / besselI0(beta);
			h[(size_t)(k + M)] = (float)((k == 0 ? 1.0 : std::sin(M_PI * u) / (M_PI * u)) * w);
			sum += h[(size_t)(k + M)];
		}
		for (float &x : h)
			x = (float)(x / sum);
		for (int L = 0; L < MIPS; L++) {
			const int64_t srcN = L ? mipFrames[L - 1] : frames;
			const int64_t n = (srcN + 1) / 2;
			if (n < 64)
				break;
			for (int c = 0; c < channels; c++) {
				const float *src = L ? mip[L - 1][c].data() : ch[c].data();
				std::vector<float> &dst = mip[L][c];
				dst.assign((size_t)n, 0.f);
				for (int64_t j = 0; j < n; j++) {
					int64_t centre = 2 * j;
					float acc = 0.f;
					int k0 = (int)std::max<int64_t>(-M, centre - (srcN - 1)), k1 = (int)std::min<int64_t>(M, centre);
					for (int k = k0; k <= k1; k++)
						acc += h[(size_t)(k + M)] * src[centre - k];
					dst[(size_t)j] = acc;
				}
			}
			mipFrames[L] = n;
			mipLevels = L + 1;
		}
		mipsBuilt = true;
	}
	void analyse() {
		originFrames = frames;
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

// The sample as the engine plays it, at `rate` (worker thread): the same
// object when the rates match, else a copy through a long windowed sinc (low-
// passed under the lower Nyquist); with its octave copies either way.
inline std::shared_ptr<Sample> playableAt(const std::shared_ptr<Sample> &s, float rate) {
	if (s->rate == rate || s->frames < 2 || rate <= 0.f) {
		if (!s->mipsBuilt)
			s->buildMips();
		return s;
	}
	auto p = std::make_shared<Sample>();
	const double R = (double)rate / s->rate;   // output frames per input frame
	const auto &K = longKernel();
	const float cut = (float)(std::min(1.0, R) * 0.95);   // of the input's Nyquist
	const double half = K.ZEROS / cut;
	const int64_t n = std::max<int64_t>(1, (int64_t)std::ceil(s->frames * R));
	p->channels = s->channels;
	p->frames = n;
	p->rate = rate;
	p->bits = s->bits;
	p->name = s->name;
	p->peak = s->peak;   // the normalization the display shows
	p->origin = s.get();
	p->originFrames = s->frames;
	for (int c = 0; c < s->channels; c++)
		p->ch[c].assign((size_t)n, 0.f);
	std::vector<float> w;
	for (int64_t j = 0; j < n; j++) {
		double pos = j / R;
		int64_t i0 = (int64_t)std::ceil(pos - half), i1 = (int64_t)std::floor(pos + half);
		w.resize((size_t)(i1 - i0 + 1));
		float sw = 0.f;
		for (int64_t i = i0; i <= i1; i++) {
			float x = K((float)std::fabs(pos - (double)i) * cut);
			w[(size_t)(i - i0)] = x;
			sw += x;
		}
		int64_t a = std::max<int64_t>(0, i0), b = std::min(s->frames - 1, i1);
		for (int c = 0; c < s->channels; c++) {
			const float *src = s->ch[c].data();
			float acc = 0.f;
			for (int64_t i = a; i <= b; i++)
				acc += w[(size_t)(i - i0)] * src[i];
			p->ch[c][(size_t)j] = sw > 0.f ? acc / sw : 0.f;
		}
	}
	p->buildMips();
	return p;
}

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
	// each slice's own sound: level (dB), tuning (semitones), pan (-1..1), and
	// attack and release in ms (below 0: the ATTACK and RELEASE knobs)
	float level[MAX_SLICES] = {};
	float tune[MAX_SLICES] = {};
	float pan[MAX_SLICES] = {};
	float attack[MAX_SLICES];
	float release[MAX_SLICES];
	Slices() {
		for (int i = 0; i < MAX_SLICES; i++)
			attack[i] = release[i] = -1.f;
	}
	// slice `to` takes the settings of slice `from`
	void copySettings(int from, int to) {
		type[to] = type[from];
		choke[to] = choke[from];
		level[to] = level[from];
		tune[to] = tune[from];
		pan[to] = pan[from];
		attack[to] = attack[from];
		release[to] = release[from];
	}
	void resetSettings(int i, int t) {
		type[i] = (uint8_t)t;
		choke[i] = 0;
		level[i] = tune[i] = pan[i] = 0.f;
		attack[i] = release[i] = -1.f;
	}

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

// Onsets. For every 5 ms hop, how sharply the sound starts there:
// - DRUMS: the energy of the sample's slope (its first difference, so attacks
//   stand out) over the 20 ms before, in dB;
// - TONAL: the spectral flux, the share of the spectrum that is new compared
//   with the hop before (a new note at the same level counts), on the same
//   dB-like scale.
// Each local peak is a candidate, placed at the zero crossing just before its
// attack, with its strength; picking them at a threshold gives the cuts, so
// "about N slices" is a search over the threshold, not new analyses.
struct Onset {
	int64_t pos;
	float strength;
};

inline std::vector<Onset> findOnsets(const Sample &s, bool tonal) {
	std::vector<Onset> out;
	const int H = std::max(64, (int)(s.rate * 0.005f));
	const int64_t n = s.frames / H;
	if (n < 8)
		return out;
	std::vector<float> d((size_t)n, 0.f);
	std::vector<float> L((size_t)n);
	float top = -240.f;
	for (int64_t i = 0; i < n; i++) {
		double e = 0;
		for (int64_t k = i * H; k < (i + 1) * H; k++) {
			float x = s.mono(k) - s.mono(k - 1);
			e += (double)x * x;
		}
		L[(size_t)i] = 10.f * std::log10((float)(e / H) + 1e-12f);
		top = std::max(top, L[(size_t)i]);
	}
	if (!tonal) {
		for (int64_t i = 4; i < n; i++) {
			float before = 0.25f * (L[(size_t)i - 1] + L[(size_t)i - 2] + L[(size_t)i - 3] + L[(size_t)i - 4]);
			d[(size_t)i] = L[(size_t)i] - before;
		}
	}
	else {
		const int N = 1024;
		PFFFT_Setup *setup = pffft_new_setup(N, PFFFT_REAL);
		float *in = (float *)pffft_aligned_malloc(N * sizeof(float));
		float *spec = (float *)pffft_aligned_malloc(N * sizeof(float));
		// against the spectrum one window earlier: a new note fades in over
		// several hops, and only the whole window shows all of it as new
		const int lag = std::max(1, N / H);
		std::vector<std::vector<float>> past(lag, std::vector<float>(N / 2, 0.f));
		std::vector<float> win(N);
		for (int k = 0; k < N; k++)
			win[k] = 0.5f - 0.5f * std::cos(2.f * (float)M_PI * k / N);
		for (int64_t i = 0; i < n; i++) {
			int64_t c = i * H + H / 2 - N / 2;
			for (int k = 0; k < N; k++)
				in[k] = s.mono(c + k) * win[k];
			pffft_transform_ordered(setup, in, spec, nullptr, PFFFT_FORWARD);
			std::vector<float> &old = past[(size_t)(i % lag)];
			double rise = 0, total = 0;
			for (int b = 1; b < N / 2; b++) {
				float m = std::sqrt(spec[2 * b] * spec[2 * b] + spec[2 * b + 1] * spec[2 * b + 1]);
				rise += std::max(0.f, m - old[b]);
				total += m;
				old[b] = m;
			}
			// twice the dB of (1 + new share): a new note at the same level ~ 12
			d[(size_t)i] = i >= lag ? 40.f * std::log10(1.f + (float)(rise / (total + 1e-9))) : 0.f;
		}
		pffft_aligned_free(in);
		pffft_aligned_free(spec);
		pffft_destroy_setup(setup);
	}
	for (int64_t i = 1; i + 1 < n; i++) {
		float v = d[(size_t)i];
		// a peak, a rise, and loud enough not to be noise in a quiet tail
		if (v < 1.f || v < d[(size_t)i - 1] || v <= d[(size_t)i + 1] || L[(size_t)i] < top - 40.f)
			continue;
		// the attack: the first frame of the hops around the peak whose slope
		// reaches a quarter of the steepest there
		int64_t a = std::max<int64_t>(1, (i - 2) * H), b = (i + 1) * H;
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
		out.push_back({pos, v});
	}
	return out;
}

// the cuts at a threshold (dB of rise): 0 first, then onsets at least 60 ms
// apart; one in the first 10 ms is the start of slice 1
inline std::vector<int64_t> pickOnsets(const std::vector<Onset> &onsets, float threshold, float rate) {
	std::vector<int64_t> cuts {0};
	const int64_t gap = (int64_t)(0.06f * rate), lead = (int64_t)(0.01f * rate);
	int64_t last = -gap;
	for (const Onset &o : onsets) {
		if ((int)cuts.size() >= MAX_SLICES)
			break;
		if (o.strength < threshold || o.pos < lead || o.pos - last < gap)
			continue;
		cuts.push_back(o.pos);
		last = o.pos;
	}
	return cuts;
}

// the threshold that gives the number of slices nearest to `slices`
inline std::vector<int64_t> pickAbout(const std::vector<Onset> &onsets, int slices, float rate) {
	std::vector<int64_t> best = pickOnsets(onsets, 1.f, rate);
	float lo = 1.f, hi = 60.f;
	for (int it = 0; it < 24; it++) {
		float t = 0.5f * (lo + hi);
		std::vector<int64_t> c = pickOnsets(onsets, t, rate);
		if (std::abs((int)c.size() - slices) < std::abs((int)best.size() - slices))
			best = c;
		if ((int)c.size() > slices)
			lo = t;
		else
			hi = t;
	}
	return best;
}

// the dB of rise a sensitivity (0..1) asks for
inline float riseForSensitivity(float sens) {
	return 15.f - 11.f * std::max(0.f, std::min(1.f, sens));
}

inline std::vector<int64_t> detectTransients(const Sample &s, float riseDb, bool tonal = false) {
	return pickOnsets(findOnsets(s, tonal), riseDb, s.rate);
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
// last, whose damping the resonance lowers. New coefficients are reached in a
// straight line over the frames until the next ones (no zipper under audio-
// rate cutoff CV); a stage that joins (SLOPE up, or the filter switched on)
// starts from rest.
struct VoiceFilter {
	Svf st[2][4];
	float g = 0.f, k[4] = {}, h[4] = {};   // now (h follows g and k)
	float dg = 0.f, dk[4] = {};    // per frame, towards the target
	float tg = 0.f, tk[4] = {};    // the target
	int ramp = 0;
	int stages = 0;                // stages running, 0: off (they restart from rest)
	void reset() {
		for (auto &c : st)
			for (Svf &s : c)
				s.reset();
		stages = 0;
		ramp = 0;
	}
	static int count(const FilterSettings &f) {
		return std::max(1, std::min(4, f.stages));
	}
	void set(const FilterSettings &f, float note, float sampleRate, int frames) {
		if (!std::isfinite(note))
			return;   // keeps the last good coefficients
		float hz = std::min(noteHz(std::max(-60.f, note)), 0.45f * sampleRate);
		float tgNew = std::tan((float)M_PI * hz / sampleRate);
		static const float bw[4][4] = {
			{1.4142f, 0, 0, 0},
			{0.7654f, 1.8478f, 0, 0},
			{0.5176f, 1.4142f, 1.9319f, 0},
			{0.3902f, 1.1111f, 1.6629f, 1.9616f},
		};
		int n = count(f);
		tg = tgNew;
		for (int i = 0; i < n; i++) {
			tk[i] = bw[n - 1][i];
			if (i == n - 1)
				tk[i] = std::max(0.05f, tk[i] * (1.f - 0.97f * f.resonance));
		}
		bool same = stages == n && tg == g;
		for (int i = 0; i < n && same; i++)
			same = tk[i] == k[i];
		if (same) {
			ramp = 0;   // nothing moves: no glide, no division per frame
		}
		else if (stages == 0) {
			for (auto &c : st)
				for (Svf &s : c)
					s.reset();
			g = tg;
			for (int i = 0; i < n; i++) {
				k[i] = tk[i];
				h[i] = 1.f / (1.f + k[i] * g + g * g);
			}
			ramp = 0;
		}
		else {
			float inv = 1.f / (float)std::max(1, frames);
			dg = (tg - g) * inv;
			for (int i = 0; i < n; i++) {
				if (i < stages) {
					dk[i] = (tk[i] - k[i]) * inv;
				}
				else {
					st[0][i].reset();
					st[1][i].reset();
					k[i] = tk[i];
					dk[i] = 0.f;
					h[i] = 1.f / (1.f + k[i] * g + g * g);
				}
			}
			ramp = std::max(1, frames);
		}
		stages = n;
	}
	void process(float &l, float &r, int mode) {
		if (ramp > 0) {
			if (--ramp == 0) {
				g = tg;   // land exactly on the target
				for (int i = 0; i < stages; i++)
					k[i] = tk[i];
			}
			else {
				g += dg;
				for (int i = 0; i < stages; i++)
					k[i] += dk[i];
			}
			for (int i = 0; i < stages; i++)
				h[i] = 1.f / (1.f + k[i] * g + g * g);
		}
		for (int i = 0; i < stages; i++) {
			float lp, bp, hp;
			st[0][i].tick(l, g, k[i], h[i], lp, bp, hp);
			switch (mode) {
				case FILTER_LP: l = lp; break;
				case FILTER_HP: l = hp; break;
				case FILTER_BP: l = bp * k[i]; break;   // unity gain at the centre
				default: l = lp + hp; break;            // notch
			}
			st[1][i].tick(r, g, k[i], h[i], lp, bp, hp);
			switch (mode) {
				case FILTER_LP: r = lp; break;
				case FILTER_HP: r = hp; break;
				case FILTER_BP: r = bp * k[i]; break;
				default: r = lp + hp; break;
			}
		}
	}
};

// ---------------------------------------------------------------- playback

// what a voice needs from the module, read once per block
struct PlaySettings {
	double start = 0, end = 1, loopStart = 0, loopEnd = 1;  // 0..1 of the sample
	int loopMode = LOOP_OFF;
	float xfadeMs = 10.f;
	bool equalPower = true;    // loop crossfade: equal power (uncorrelated material), else linear
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
	// how it reads: Hermite at or under the sample's own rate (level -1),
	// else a windowed sinc low-passed under the output's Nyquist, from the
	// octave copy that keeps the step under 2
	int level = -1;
	float cut = 1.f;
	double lastStep = -1.0;

	struct Region {
		double start, end, ls, le, x;
		int mode;
		bool reverse;
		bool tail = false;   // the crossfade uses the sample after the seam (see region())
		bool equalPower = true;
	};

	// the shortest loop while its points move: IN and OUT cross without it ever
	// falling to nothing (2 ms, a buzz around 500 Hz at the sample's own pitch)
	static double minLiveLoop(const Sample &s) {
		return std::min((double)s.frames, std::max(16.0, 0.002 * (double)s.rate));
	}

	// minLoop: a loop under this length grows around its centre instead of
	// switching off (moving points, see nextLive); 0 when the points stay put
	static Region region(const Sample &s, const PlaySettings &p, float engineRate, double minLoop = 0.0) {
		double F = (double)s.frames;
		double a = std::min(p.start, p.end) * F, b = std::max(p.start, p.end) * F;
		double la = std::min(p.loopStart, p.loopEnd) * F, lb = std::max(p.loopStart, p.loopEnd) * F;
		if (lb - la < minLoop) {
			la = std::max(0.0, std::min(F - minLoop, 0.5 * (la + lb) - 0.5 * minLoop));
			lb = la + minLoop;
		}
		Region r;
		r.reverse = p.reverse;
		r.equalPower = p.equalPower;
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

	// how to read for a step (frames of the sample per output frame)
	void setStep(double step) {
		step = std::fabs(step);
		if (step == lastStep)
			return;
		lastStep = step;
		if (!(step > 1.0) || !s) {
			level = -1;
			return;
		}
		double local = step;   // under 2 unless out of octave copies
		level = 0;
		while (local >= 2.0 && level < s->mipLevels) {
			local *= 0.5;
			level++;
		}
		cut = 0.85f / (float)std::min(local, 4.0);
	}

	void read(double pos, bool reverse, float &l, float &r) const {
		double p = reverse ? (double)s->frames - 1.0 - pos : pos;
		if (level < 0) {
			l = s->read(0, p);
			r = s->channels > 1 ? s->read(1, p) : l;
		}
		else {
			s->readBandLimited(level, cut, p, l, r);
		}
	}

	// the crossfade: `t` (0..1) of the way from (l, r) to (bl, br). Constant
	// power: the gains of a linear fade, scaled so the level goes from one
	// side's to the other's whatever their correlation, measured since the
	// fade began (a plain linear fade dips 3 dB on noise; an equal-power one
	// bumps 3 dB on a tone)
	double xaa = 0, xbb = 0, xab = 0;
	bool fading = false;
	void blend(const Region &rg, float t, float bl, float br, float &l, float &r) {
		t = std::max(0.f, std::min(1.f, t));
		fading = true;
		float ga = 1.f - t, gb = t;
		if (rg.equalPower) {
			float ma = l + r, mb = bl + br;
			xaa += (double)ma * ma;
			xbb += (double)mb * mb;
			xab += (double)ma * mb;
			double want = ga * xaa + gb * xbb;
			double got = (double)ga * ga * xaa + (double)gb * gb * xbb + 2.0 * ga * gb * std::max(0.0, xab);
			if (got > 1e-9 * (xaa + xbb) && got > 1e-20) {
				float k = (float)std::min(1.5, std::sqrt(want / got));
				ga *= k;
				gb *= k;
			}
		}
		l = ga * l + gb * bl;
		r = ga * r + gb * br;
	}

	// one output frame, then the head moves by `step` play-space frames
	void next(const Region &r, double step, float &l, float &rr) {
		if (done) {
			l = rr = 0.f;
			return;
		}
		setStep(step);
		double L = r.le - r.ls;
		read(v, r.reverse, l, rr);
		float bl, br;
		bool wasFading = fading;
		fading = false;
		if (inLoop && r.x > 0 && r.tail) {
			// after each wrap: what followed the seam fades into the loop's start
			if (wrapped && r.mode == LOOP_FORWARD && v < r.ls + r.x) {
				read(v + L, r.reverse, bl, br);
				blend(r, 1.f - (float)((v - r.ls) / r.x), bl, br, l, rr);
			}
			else if (wrapped && r.mode == LOOP_REVERSE && v > r.le - r.x) {
				read(v - L, r.reverse, bl, br);
				blend(r, 1.f - (float)((r.le - v) / r.x), bl, br, l, rr);
			}
		}
		else if (inLoop && r.x > 0) {
			if (r.mode == LOOP_FORWARD && v > r.le - r.x) {
				read(v - L, r.reverse, bl, br);
				blend(r, (float)((v - (r.le - r.x)) / r.x), bl, br, l, rr);
			}
			else if (r.mode == LOOP_REVERSE && v < r.ls + r.x) {
				read(v + L, r.reverse, bl, br);
				blend(r, (float)(((r.ls + r.x) - v) / r.x), bl, br, l, rr);
			}
		}
		if (wasFading && !fading)
			xaa = xbb = xab = 0;   // the next fade measures afresh
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

	// ---- when the points move (the END, LOOP IN and LOOP OUT inputs). next()
	// works out its crossfade from where the seam is now, so a seam that moves
	// would move the reading position (pitch) and the fade (click). Here a
	// crossfade is decided once: the old position goes on as `ghost` while the
	// head jumps to the new one, both at the speed of the note, and the fade runs
	// to its end whatever the points do meanwhile. A point that moved only decides
	// when the next jump happens and where to. With points that stay put it plays
	// what next() plays.
	double ghost = 0;       // the old position, fading out
	int ghostDir = 1;
	double fadePos = 0, fadeLen = 0;   // play-space frames, fadeLen 0: no fade
	float endGain = 1.f;    // the fade before END, also when END jumps behind the head
	bool underLoop = false; // it was under LOOP IN at some point before reaching the loop

	void beginFade(double to, double pos, double len, int newDir) {
		ghost = v;
		ghostDir = dir;
		v = to;
		dir = newDir;
		fadePos = pos;
		fadeLen = len;
		xaa = xbb = xab = 0;
	}

	// what the loop asks of a head inside it. A head that the points left behind
	// on the side it is walking towards just walks on (a forward loop's LOOP IN
	// passed it); on the other side it jumps, modulo the length like next().
	void followLoop(const Region &r, double L, double step, float rate) {
		const double moved = step * 1.0001;   // further than one step: a point moved, not the head
		const double seamFade = 0.001 * rate * step, jumpFade = 0.003 * rate * step;
		switch (r.mode) {
			case LOOP_FORWARD:
				dir = 1;
				if (fadeLen > 0)
					break;
				if (v >= r.le) {
					bool own = v - r.le <= moved;
					double to = r.ls + std::fmod(v - r.ls, L);
					beginFade(to, own ? to - r.ls : 0.0, std::max(r.x, own ? seamFade : jumpFade), 1);
				}
				else if (!r.tail && r.x > 0 && v > r.le - r.x) {
					double in = v - (r.le - r.x);
					if (in <= moved)
						beginFade(v - L, in, std::max(r.x, seamFade), 1);
					else
						beginFade(v - L, 0.0, std::max(r.le - v, jumpFade), 1);
				}
				break;
			case LOOP_REVERSE:
				dir = -1;
				if (fadeLen > 0)
					break;
				if (v <= r.ls) {
					bool own = r.ls - v <= moved;
					double to = r.le - std::fmod(r.le - v, L);
					beginFade(to, own ? r.le - to : 0.0, std::max(r.x, own ? seamFade : jumpFade), -1);
				}
				else if (!r.tail && r.x > 0 && v < r.ls + r.x) {
					double in = (r.ls + r.x) - v;
					if (in <= moved)
						beginFade(v + L, in, std::max(r.x, seamFade), -1);
					else
						beginFade(v + L, 0.0, std::max(v - r.ls, jumpFade), -1);
				}
				break;
			case LOOP_PINGPONG:
				// turning needs no fade: a little outside it just turns; far outside it jumps to the point
				if (v >= r.le) {
					double d = v - r.le;
					if (d <= moved && dir > 0) {
						v = 2 * r.le - v;
						dir = -1;
					}
					else if (d <= jumpFade || fadeLen > 0)
						dir = -1;
					else
						beginFade(r.le, 0.0, std::max(r.x, jumpFade), -1);
				}
				else if (v <= r.ls) {
					double d = r.ls - v;
					if (d <= moved && dir < 0) {
						v = 2 * r.ls - v;
						dir = 1;
					}
					else if (d <= jumpFade || fadeLen > 0)
						dir = 1;
					else
						beginFade(r.ls, 0.0, std::max(r.x, jumpFade), 1);
				}
				break;
		}
	}

	// one output frame with moving points: `rate` is the output's
	void nextLive(const Region &r, double step, float rate, float &l, float &rr) {
		if (done) {
			l = rr = 0.f;
			return;
		}
		setStep(step);
		const double L = r.le - r.ls;
		const bool looping = inLoop && r.mode != LOOP_OFF && L > 0;
		if (looping)
			followLoop(r, L, step, rate);
		if (fadeLen > 0) {
			float bl, br;
			read(ghost, r.reverse, l, rr);
			read(v, r.reverse, bl, br);
			blend(r, (float)(fadePos / fadeLen), bl, br, l, rr);
		}
		else {
			read(v, r.reverse, l, rr);
		}
		// 2 ms before END; when END jumps closer than that the gain still takes 2 ms
		const float slew = 1.f / (0.002f * rate);
		float want = 1.f;
		if (!looping && step > 0) {
			double left = (dir > 0 ? r.end - v : v) / step;
			want = (float)std::max(0.0, std::min(1.0, left * slew));
		}
		endGain = want < endGain ? std::max(want, endGain - slew) : std::min(want, endGain + slew);
		l *= endGain;
		rr *= endGain;
		const double before = v, last = (double)s->frames - 1.0;
		v += step * dir;
		if (fadeLen > 0) {
			// neither side of a fade leaves the sample: it would drop to nothing at once
			ghost += step * ghostDir;
			if (ghost > last || ghost < 0) {
				ghost = std::max(0.0, std::min(last, ghost > last ? 2 * last - ghost : -ghost));
				ghostDir = -ghostDir;
			}
			if (v > last || v < 0) {
				v = std::max(0.0, std::min(last, v > last ? 2 * last - v : -v));
				dir = -dir;
			}
			fadePos += step;
			if (fadePos >= fadeLen)
				fadeLen = 0;
		}
		// in once it reaches the loop, also when a fast sweep carries the loop over it
		if (r.mode != LOOP_OFF && !inLoop) {
			if (v >= r.ls && (v < r.le || before < r.le || underLoop))
				inLoop = true;
			underLoop |= v < r.ls;
		}
		if (!(inLoop && r.mode != LOOP_OFF && L > 0) && (v >= r.end || v < 0) && endGain <= 0.f)
			done = true;
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
		// split computation: each block's FFT work spread over the interval, so
		// no block costs a whole analysis (one interval more of latency, which
		// outputSeek and the tail account for)
		if (m == TIME_STRETCH_CHEAP)
			st.presetCheaper(2, sampleRate, true);
		else
			st.presetDefault(2, sampleRate, true);
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
	bool fadingOut = false;   // retriggered, stolen, its sample or pool replaced: a 3 ms fade, then off
	int outChannel = 0;
	const Sample *sample = nullptr;
	const Sample *origin = nullptr;   // the sample the display shows for it (never read through)
	double frames = 0;                // frames of `sample`, for the display
	PlayHead head;
	VoiceFilter filter;
	float env = 0.f;
	float fadeGain = 1.f;
	float pitchSemis = 0.f;   // from the root note, bend and tune included
	float velocity = 1.f;
	float cutoffNote = 120.f;
	float speed = 1.f;        // the knob plus this channel's CV
	float resonance = 0.f;    // the knob plus this channel's CV
	Stretcher *st = nullptr;  // its stretcher, nullptr: repitch
	int tail = 0;             // stretch: output frames left once the tape has ended
	int filterCount = 0;
	int64_t age = 0;
	float ratioSemis = 1e9f;   // the pitch the cached ratio was worked out for
	double ratio = 1.0;
	float goodSemis = 0.f;     // the last finite pitch (a NaN CV keeps it)
	// SLICES mode: the voice plays its own slice region; a one shot plays to
	// the end of it whatever the gate does
	bool ownRegion = false;
	bool oneShot = false;
	int choke = 0;            // the slice's choke group
	int slice = -1;           // SLICES mode: the slice it plays (for the pads)
	float pitchOffset = 0.f;  // the slice's tuning, on top of pitchSemis
	float gainL = 1.f, gainR = 1.f;   // the slice's level and pan
	float attackMs = -1.f, releaseMs = -1.f;   // the slice's envelope, below 0: the knobs
	PlayHead::Region region {};
	// its END, LOOP IN and LOOP OUT follow voltages while it plays: `liveTo` from
	// the module, `liveNow` smoothed; `region` is worked out from them every
	// LIVE_BLOCK frames and the head uses nextLive(). Off: the head uses next().
	enum { LIVE_END, LIVE_LOOP_IN, LIVE_LOOP_OUT, LIVE_POINTS };
	bool live = false;
	int liveCount = 0;
	float liveTo[LIVE_POINTS] = {};
	float liveNow[LIVE_POINTS] = {};
	// SLICES mode: the slice (0..1 of the sample), how it plays
	double sliceFrom = 0, sliceTo = 1;
	int ownLoopMode = LOOP_OFF;
	bool ownReverse = false;

	void setLivePoints(float end, float loopIn, float loopOut) {
		liveTo[LIVE_END] = end;
		liveTo[LIVE_LOOP_IN] = loopIn;
		liveTo[LIVE_LOOP_OUT] = loopOut;
		if (!live) {
			// a new note or a new cable starts where the voltage is, no glide from the knob
			for (int k = 0; k < LIVE_POINTS; k++)
				liveNow[k] = liveTo[k];
			live = true;
			liveCount = 0;
		}
	}
};

// Everything that plays: 16 channels, each with a spare slot for the fade-out of
// a retriggered note, plus the TRIG slice, the clicked key and the preview
// voice. Audio thread only.
struct Engine {
	static constexpr int SLOTS = MAX_VOICES * 2 + 3;
	static constexpr int TRIGGER = SLOTS - 3;   // a slice from the TRIG input
	static constexpr int MOUSE = SLOTS - 2;     // a key clicked on the keyboard strip
	static constexpr int PREVIEW = SLOTS - 1;
	static constexpr int LIVE_BLOCK = 16;       // frames between two looks at the moving points
	static constexpr float LIVE_SMOOTH = 0.005f;   // their smoothing: a 5 ms one-pole
	Voice voices[SLOTS];
	Stretcher *pool = nullptr;   // STRETCH_POOL stretchers, built by the worker
	Stretcher *oldPool = nullptr;   // the one replaced, until its voices have faded
	float rate = 48000.f;
	int poolMode = -1;           // the preset the pool runs, -1: none yet
	float poolRate = 0.f;
	float liveCoef = 1.f;

	const Sample *current = nullptr;
	// samples replaced while voices still played them (hot-swap)
	const Sample *old[8] = {};

	// a new rate: what plays fades out (its stretchers keep their old rate meanwhile)
	void prepare(float sampleRate) {
		rate = sampleRate;
		liveCoef = 1.f - std::exp(-(float)LIVE_BLOCK / (LIVE_SMOOTH * sampleRate));
		for (Voice &v : voices)
			if (v.active)
				v.fadingOut = true;
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

	// the previous pool is still fading out: a new one waits
	bool canTakePool() const {
		return !oldPool;
	}

	// takes a pool from the worker; the stretched voices fade out on the old
	// one, which takeOldPool() hands back once they are done
	void setPool(Stretcher *p, int preset, float sampleRate) {
		for (Voice &v : voices)
			if (v.active && v.st)
				v.fadingOut = true;   // a stretched voice cannot move to another pool
		oldPool = pool;
		pool = p;
		poolMode = preset;
		poolRate = sampleRate;
	}

	// the replaced pool once no voice uses it, for the worker to free
	Stretcher *takeOldPool() {
		if (!oldPool)
			return nullptr;
		for (int i = 0; i < STRETCH_POOL; i++)
			if (oldPool[i].inUse)
				return nullptr;
		Stretcher *o = oldPool;
		oldPool = nullptr;
		return o;
	}

	bool poolReady(int timeMode) const {
		return pool && poolMode == presetFor(timeMode) && poolRate == rate;
	}

	// room to keep the playing sample while a new one comes in
	bool canTake() const {
		if (!current)
			return true;
		for (const Sample *o : old)
			if (!o)
				return true;
		return false;
	}
	// no room: the voices of the oldest replaced sample fade out, then
	// takeUnused() hands it back and the new one can come in
	void makeRoom() {
		for (Voice &v : voices)
			if (v.active && v.sample == old[0])
				v.fadingOut = true;
	}

	// hot-swap: new notes take the new sample, playing ones keep theirs (call
	// when canTake())
	void setSample(const Sample *s) {
		if (s == current)
			return;
		if (current) {
			for (const Sample *&o : old) {
				if (!o) {
					o = current;
					break;
				}
			}
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
				// keep the oldest first: makeRoom() fades old[0]
				for (const Sample **q = &o; q + 1 < old + 8; q++)
					*q = *(q + 1);
				old[7] = nullptr;
				return r;
			}
		}
		return nullptr;
	}

	Stretcher *freeStretcher() {
		if (!pool)
			return nullptr;
		for (int i = 0; i < STRETCH_POOL; i++)
			if (!pool[i].inUse)
				return &pool[i];
		return nullptr;
	}

	void release(Voice &v) {
		if (v.st)
			v.st->inUse = false;
		v.st = nullptr;
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
		v.st = nullptr;   // the stretcher went with the fading copy
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
		v.origin = current->origin;
		v.frames = (double)current->frames;
		v.head.s = current;
		v.head.v = std::max(r.start, std::min(r.end - 1, r.start + startOffset * (r.end - r.start)));
		v.pitchSemis = std::isfinite(semis) ? semis : 0.f;
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
			v.st = freeStretcher();
			if (v.st) {
				v.st->inUse = true;
				v.st->begin();
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
		release(v);
		v = Voice();
		v.active = true;
		v.gate = true;
		v.sample = current;
		v.origin = current->origin;
		v.frames = (double)current->frames;
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
			float a = 1.f / (std::max(1.f, v.attackMs >= 0.f ? v.attackMs : p.attackMs) * 0.001f * rate);
			v.env = std::min(1.f, v.env + a);
		}
		else {
			float rel = std::max(2.f, v.releaseMs >= 0.f ? v.releaseMs : p.releaseMs) * 0.001f * rate;
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

	// a voice's moving points: every LIVE_BLOCK frames they glide on and give it a new region
	void updateLive(Voice &v, const Sample &s, const PlaySettings &p) {
		if (--v.liveCount > 0)
			return;
		v.liveCount = LIVE_BLOCK;
		for (int k = 0; k < Voice::LIVE_POINTS; k++)
			v.liveNow[k] += liveCoef * (v.liveTo[k] - v.liveNow[k]);
		PlaySettings q = p;
		if (v.ownRegion) {
			// a slice: END trims inside it, and its loop is its trimmed length
			double t0 = p.start, t1 = v.liveNow[Voice::LIVE_END], n = v.sliceTo - v.sliceFrom;
			q.start = q.loopStart = v.sliceFrom + std::min(t0, t1) * n;
			q.end = q.loopEnd = v.sliceFrom + std::max(t0, t1) * n;
			q.loopMode = v.ownLoopMode;
			q.reverse = v.ownReverse;
		}
		else {
			q.end = v.liveNow[Voice::LIVE_END];
			q.loopStart = v.liveNow[Voice::LIVE_LOOP_IN];
			q.loopEnd = v.liveNow[Voice::LIVE_LOOP_OUT];
		}
		v.region = PlayHead::region(s, q, rate, PlayHead::minLiveLoop(s));
	}

	// one frame of every voice, added into outL/outR by output channel
	void process(const PlaySettings &p, float *outL, float *outR) {
		// one stretcher start per frame: a chord's start-ups spread over a few samples
		bool seekFree = true;
		PlayHead::Region mainRegion {};
		if (current)
			mainRegion = PlayHead::region(*current, p, rate);
		const float speedKnob = std::isfinite(p.speed) ? p.speed : 1.f;
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
				ps.speed = std::isfinite(v.speed) ? v.speed : speedKnob;
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
			// a NaN pitch CV keeps the last good pitch
			float semis = v.pitchSemis + v.pitchOffset;
			if (std::isfinite(semis))
				v.goodSemis = semis;
			else
				semis = v.goodSemis;
			if (v.live)
				updateLive(v, s, ps);
			PlayHead::Region r = (v.ownRegion || v.live) ? v.region : ((!preview && v.sample == current) ? mainRegion : PlayHead::region(s, ps, rate));
			const double srcRatio = s.rate / rate;
			float l = 0.f, rr = 0.f;
			double step = srcRatio;
			if (v.st && !preview) {
				Stretcher &st = *v.st;
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
							if (v.live)
								v.head.nextLive(r, srcRatio, rate, a, b);
							else
								v.head.next(r, srcRatio, a, b);
							st.tape[0][st.written & st.tapeMask] = a;
							st.tape[1][st.written & st.tapeMask] = b;
							st.written++;
						}
						st.st.setFormantFactor(1.f, ps.timeMode == TIME_STRETCH_FORMANT);
						st.st.setTransposeSemitones(semis);
						st.st.outputSeek(Stretcher::Input{&st, 0}, seekLen);
						st.consumed = seekLen;
						st.started = true;
					}
					st.st.setFormantFactor(1.f, ps.timeMode == TIME_STRETCH_FORMANT);
					st.st.setTransposeSemitones(semis);
					int count = (int)std::floor(st.acc + ps.speed * STRETCH_BLOCK);
					st.acc += ps.speed * STRETCH_BLOCK - count;
					while (st.written < st.consumed + count) {
						float a, b;
						if (v.live)
							v.head.nextLive(r, srcRatio, rate, a, b);
						else
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
				if (semis != v.ratioSemis) {
					v.ratioSemis = semis;
					v.ratio = std::exp2((double)semis / 12.0);
				}
				step = srcRatio * v.ratio * (preview ? 1.0 : (double)ps.speed);
				if (!std::isfinite(step) || !std::isfinite(v.head.v)) {
					// nothing sane to play: the voice stops, its filter from rest
					v.filter.reset();
					release(v);
					continue;
				}
				if (v.live) {
					v.head.nextLive(r, step, rate, l, rr);
				}
				else {
					float fade = endFade(v, r, step);
					v.head.next(r, step, l, rr);
					l *= fade;
					rr *= fade;
				}
				if (v.head.done && !v.fadingOut)
					v.gate = false, v.env = 0.f;
			}
			envelope(v, ps);
			if (!v.active)
				continue;
			// filter coefficients every 16 frames, glided to in between; at once
			// when the slope changes or the filter comes on
			if (ps.filter.mode != FILTER_OFF) {
				if (v.filterCount-- <= 0 || v.filter.stages != VoiceFilter::count(ps.filter)) {
					v.filter.set(ps.filter, v.cutoffNote, rate, 16);
					v.filterCount = 15;
				}
				v.filter.process(l, rr, ps.filter.mode);
			}
			else {
				v.filter.stages = 0;
			}
			if (!std::isfinite(l) || !std::isfinite(rr)) {
				v.filter.reset();
				l = rr = 0.f;
			}
			float g = v.env * v.fadeGain * v.velocity * p.gain;
			outL[v.outChannel] += l * g * v.gainL;
			outR[v.outChannel] += rr * g * v.gainR;
			v.age++;
			if (!v.gate && v.env <= 0.f)
				release(v);
		}
	}
};

}  // namespace sounaire
