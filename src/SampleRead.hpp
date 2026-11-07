#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

// Sample reading shared by the sample players (POUPRE, MAGMA, OAI, OUAIVE,
// CANARD, SPORE, EDSAROS): 4-point Hermite interpolation, band-limited mip
// copies for reading faster than the original speed, and a declicker (a
// fade-in of the play head plus a short fading copy of the old head).
//
// Data is interleaved, C channels per frame (mono: C = 1; dsp::Frame<2>
// vectors: C = 2, two packed floats per frame).

namespace sampleread {

// 4-point Hermite (Catmull-Rom), t in [0, 1) between p1 and p2.
inline float hermite(float p0, float p1, float p2, float p3, float t) {
	float c1 = 0.5f * (p2 - p0);
	float c2 = p0 - 2.5f * p1 + 2.f * p2 - 0.5f * p3;
	float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
	return ((c3 * t + c2) * t + c1) * t + p1;
}

// Hermite read at a fractional frame, indices clamped to the buffer.
template <int C>
inline void hermiteAt(const float *d, int n, double pos, float *out) {
	if (n <= 0) {
		for (int c = 0; c < C; c++)
			out[c] = 0.f;
		return;
	}
	double fl = std::floor(pos);
	int i = (int)fl;
	float t = (float)(pos - fl);
	if (i >= 1 && i + 2 < n) {
		const float *p = d + (i - 1) * C;
		for (int c = 0; c < C; c++)
			out[c] = hermite(p[c], p[C + c], p[2 * C + c], p[3 * C + c], t);
		return;
	}
	int last = n - 1;
	int i0 = std::min(std::max(i - 1, 0), last), i1 = std::min(std::max(i, 0), last);
	int i2 = std::min(std::max(i + 1, 0), last), i3 = std::min(std::max(i + 2, 0), last);
	for (int c = 0; c < C; c++)
		out[c] = hermite(d[i0 * C + c], d[i1 * C + c], d[i2 * C + c], d[i3 * C + c], t);
}

// The low-pass used to build each mip level: Kaiser-windowed sinc, cut at
// 0.23 of the rate of the level it filters (stopband from about 0.25, about
// -85 dB), so halving the rate folds almost nothing back.
constexpr int MIP_HALF = 63;
constexpr int MIP_TAPS = 2 * MIP_HALF + 1;

inline double besselI0(double x) {
	double sum = 1.0, term = 1.0;
	for (int k = 1; k < 50; k++) {
		term *= (x / (2.0 * k)) * (x / (2.0 * k));
		sum += term;
		if (term < 1e-12 * sum)
			break;
	}
	return sum;
}

inline std::array<float, MIP_TAPS> designLowpass(double fc, int half, double beta) {
	std::array<float, MIP_TAPS> k{};
	int taps = 2 * half + 1;
	double sum = 0.0;
	std::vector<double> h(taps);
	for (int n = 0; n < taps; n++) {
		double m = n - half;
		double sinc = (m == 0.0) ? 2.0 * fc : std::sin(2.0 * M_PI * fc * m) / (M_PI * m);
		double r = m / half;
		double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / besselI0(beta);
		h[n] = sinc * w;
		sum += h[n];
	}
	for (int n = 0; n < taps; n++)
		k[n] = (float)(h[n] / sum);
	return k;
}

inline const float *mipKernel() {
	static const std::array<float, MIP_TAPS> k = designLowpass(0.23, MIP_HALF, 8.6);
	return k.data();
}

// Off the audio thread: low-pass every `step`-th frame of `in` (n frames),
// so out has ceil(n / step) frames.
template <int C>
inline void lowpass(const float *in, int n, int step, std::vector<float> &out) {
	const float *h = mipKernel();
	int m = (n + step - 1) / step;
	out.assign((size_t)m * C, 0.f);
	for (int j = 0; j < m; j++) {
		for (int c = 0; c < C; c++) {
			double acc = h[MIP_HALF] * in[(size_t)j * step * C + c];
			if (j - MIP_HALF >= 0 && j + MIP_HALF < m) {
				for (int t = 1; t <= MIP_HALF; t++)
					acc += h[MIP_HALF + t] * (in[(size_t)(j - t) * step * C + c] + in[(size_t)(j + t) * step * C + c]);
			}
			else {
				for (int t = 1; t <= MIP_HALF; t++) {
					float a = (j - t >= 0) ? in[(size_t)(j - t) * step * C + c] : 0.f;
					float b = (j + t < m) ? in[(size_t)(j + t) * step * C + c] : 0.f;
					acc += h[MIP_HALF + t] * (a + b);
				}
			}
			out[(size_t)j * C + c] = (float)acc;
		}
	}
}

// Band-limited copies of a sample, for reading it up to 2^LEVELS times
// faster without aliasing. Level k holds what is left of the sample at 1/2^k
// of its rate, stored at twice that rate (1/2^(k-1)) so the Hermite reading
// stays clean up to its top. About 1.9 times the sample's memory on top of it.
template <int C>
struct Mip {
	static constexpr int LEVELS = 4;  // 2x, 4x, 8x, 16x
	// Longer samples (about 5.8 min at 48 kHz) get no copies, to keep the
	// memory sane: they play with Hermite only.
	static constexpr int MAX_FRAMES = 1 << 24;
	std::vector<float> lv[LEVELS];    // lv[k - 1]: level k
	int n[LEVELS] = {};

	void clear() {
		for (int k = 0; k < LEVELS; k++) {
			std::vector<float>().swap(lv[k]);
			n[k] = 0;
		}
	}

	// Off the audio thread.
	void build(const float *src, int frames, int levels = LEVELS) {
		clear();
		if (frames < 2 || frames > MAX_FRAMES)
			return;
		levels = std::min(levels, LEVELS);
		for (int k = 0; k < levels; k++) {
			if (k == 0)
				lowpass<C>(src, frames, 1, lv[0]);
			else
				lowpass<C>(lv[k - 1].data(), n[k - 1], 2, lv[k]);
			n[k] = (int)(lv[k].size() / C);
			if (n[k] < 2)
				break;
		}
	}

	size_t bytes() const {
		size_t b = 0;
		for (int k = 0; k < LEVELS; k++)
			b += lv[k].capacity() * sizeof(float);
		return b;
	}
};

inline float mipBlend(float f);

// The same band-limited levels for a live signal kept in a ring buffer
// (SPORE): written one sample at a time, with a shorter low-pass (63 taps,
// about -70 dB) so they lag the input by only LATENCY frames. Level 1 is at
// the full rate, level 2 at half rate; good for reading up to 4x.
struct LiveMip {
	static constexpr int HALF = 31;
	static constexpr int LEVELS = 2;
	static constexpr int LATENCY = 3 * HALF + 8;  // frames behind the input that the levels can be read at
	std::vector<float> lv[LEVELS];
	long long mask[LEVELS] = {};

	static const float *kernel() {
		static const std::array<float, MIP_TAPS> k = designLowpass(0.22, HALF, 7.0);
		return k.data();
	}

	// n: a power of two, the size of the input ring.
	void resize(long long n) {
		lv[0].assign((size_t)n, 0.f);
		mask[0] = n - 1;
		lv[1].assign((size_t)(n / 2), 0.f);
		mask[1] = n / 2 - 1;
	}

	// The input ring `x` (mask xm) was just written at frame `now`.
	void push(const float *x, long long xm, long long now) {
		const float *h = kernel();
		long long t = now - HALF;
		float acc = h[HALF] * x[t & xm];
		for (int k = 1; k <= HALF; k++)
			acc += h[HALF + k] * (x[(t - k) & xm] + x[(t + k) & xm]);
		lv[0][t & mask[0]] = acc;
		if ((t & 1) == 0) {
			// Level 2 at frame 2u, from level 1 at the even frames around it.
			long long u = t / 2 - HALF;
			const float *y = lv[0].data();
			float a2 = h[HALF] * y[(2 * u) & mask[0]];
			for (int k = 1; k <= HALF; k++)
				a2 += h[HALF + k] * (y[(2 * (u - k)) & mask[0]] + y[(2 * (u + k)) & mask[0]]);
			lv[1][u & mask[1]] = a2;
		}
	}

	static float hermiteRing(const float *d, long long m, double pos) {
		long long xi = (long long)std::floor(pos);
		float t = (float)(pos - (double)xi);
		return hermite(d[(xi - 1) & m], d[xi & m], d[(xi + 1) & m], d[(xi + 2) & m], t);
	}

	// Frame `pos` of the input (ring x, mask xm) read at |speed|, at least
	// LATENCY frames old.
	float read(const float *x, long long xm, double pos, float absSpeed) const {
		if (absSpeed <= 1.f)
			return hermiteRing(x, xm, pos);
		float o = std::min(std::log2(absSpeed), (float)LEVELS);
		int L = (int)o;
		float f = o - (float)L;
		auto level = [&](int k) {
			if (k == 0)
				return hermiteRing(x, xm, pos);
			return hermiteRing(lv[k - 1].data(), mask[k - 1], pos * (1.0 / (double)(1 << (k - 1))));
		};
		float a = level(L);
		if (f > 0.f && L < LEVELS)
			a += (level(L + 1) - a) * mipBlend(f);
		return a;
	}
};

// How much of the next level is mixed in, from the octave fraction f above
// the level (0: this level only, 1: the next). The next level is alias free;
// this one only aliases its top end, so it fades out quickly.
inline float mipBlend(float f) {
	float g = 1.f - f;
	return 1.f - g * g * g;
}

// Read at frame `pos` (of the full-rate sample) playing at |speed|: Hermite on
// the full-rate data up to 1x, then on the band-limited levels, two adjacent
// levels mixed so the tone does not step at each octave.
template <int C>
inline void readAA(const float *d0, int n0, const Mip<C> *mip, double pos, float absSpeed, float *out) {
	if (!mip || absSpeed <= 1.0f || mip->n[0] == 0) {
		hermiteAt<C>(d0, n0, pos, out);
		return;
	}
	float o = std::log2(absSpeed);
	int L = (int)o;
	float f = o - (float)L;
	if (L >= Mip<C>::LEVELS || mip->n[std::min(L, Mip<C>::LEVELS - 1)] == 0) {
		L = Mip<C>::LEVELS;
		while (L > 0 && mip->n[L - 1] == 0)
			L--;
		f = 0.f;
	}
	auto level = [&](int k, float *o2) {
		if (k == 0)
			hermiteAt<C>(d0, n0, pos, o2);
		else
			hermiteAt<C>(mip->lv[k - 1].data(), mip->n[k - 1], pos * (1.0 / (double)(1 << (k - 1))), o2);
	};
	level(L, out);
	if (f > 0.f && L < Mip<C>::LEVELS && mip->n[L] > 0) {
		float b[C];
		level(L + 1, b);
		float w = mipBlend(f);
		for (int c = 0; c < C; c++)
			out[c] += (b[c] - out[c]) * w;
	}
}

// A view of a sample to read: full-rate data plus its mip copies (or none).
template <int C>
struct View {
	const float *d = nullptr;
	int n = 0;
	const Mip<C> *mip = nullptr;

	void read(double pos, float absSpeed, float *out) const {
		readAA<C>(d, n, mip, pos, absSpeed, out);
	}
};

// Declick length: 2 ms, at least 16 samples.
inline int declickLength(float sampleRate) {
	return std::max(16, (int)(0.002f * sampleRate));
}

// The declicker of one play head. On a jump or a restart the old head goes
// on as a tail that fades out while the new one fades in; on a stop only the
// tail plays.
template <int C>
struct Declick {
	float in = 1.f, inStep = 0.f;  // fade-in of the main head
	double tPos = 0.0;
	float tSpeed = 0.f, tGain = 0.f;
	int tLeft = 0, tLen = 1;

	void reset() {
		in = 1.f;
		inStep = 0.f;
		tLeft = 0;
	}

	// The main head starts again (from silence, or after a jump).
	void fadeIn(int len) {
		in = 0.f;
		inStep = 1.f / (float)std::max(len, 1);
	}

	// The old head at `pos`, moving `speed` frames per sample at `gain`, fades
	// out over `len` samples. A tail already playing is replaced.
	void tail(double pos, float speed, float gain, int len) {
		if (gain == 0.f)
			return;
		tPos = pos;
		tSpeed = speed;
		tGain = gain;
		tLen = std::max(len, 1);
		tLeft = tLen;
	}

	// The gain of the main head for this sample.
	float nextIn() {
		float g = in;
		if (in < 1.f) {
			in += inStep;
			if (in > 1.f)
				in = 1.f;
		}
		return g;
	}

	bool tailActive() const {
		return tLeft > 0;
	}

	// Adds this sample of the tail to acc.
	void addTail(const View<C> &v, float *acc) {
		if (tLeft <= 0)
			return;
		float o[C];
		v.read(tPos, std::fabs(tSpeed), o);
		float g = tGain * (float)tLeft / (float)tLen;
		for (int c = 0; c < C; c++)
			acc[c] += o[c] * g;
		tPos += tSpeed;
		tLeft--;
	}
};

}  // namespace sampleread
