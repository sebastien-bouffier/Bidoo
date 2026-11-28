#pragma once
// PRiSME's drawn pages: two spectra (A, B) and the decay times (T), 64 bars each, 0..1. Plain C++ (no Rack), so a
// standalone test can check the drawing path. The tools follow EMILE's (src/EmileCanvas.hpp): a soft brush whose
// width is set by the wheel, a straight line, the eraser on the right button, shapes, undo through Rack's history.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace prisme {

static constexpr int kBars = 64;
static constexpr int kBarPages = 3;

struct Bars {
	float v[kBarPages][kBars];

	// what a page holds when nothing is drawn: A a sawtooth, B a square, T every harmonic at DECAY's time
	void setDefaults();
	bool operator==(const Bars &o) const { return std::memcmp(v, o.v, sizeof(v)) == 0; }
	bool operator!=(const Bars &o) const { return !(*this == o); }
};

// A level in dB as a bar (the engine's 60 dB range): 0 dB is 1, -60 dB is 0
inline float dbToBar(float db) {
	return std::min(std::max(1.f + db / 60.f, 0.f), 1.f);
}

// 1/k^p in dB as a bar: a sawtooth is p = 1, a triangle's odd harmonics p = 2
inline float powerBar(int k, float p) {
	return dbToBar(-20.f * p * std::log10((float)k));
}

enum Shape { SHAPE_SAW, SHAPE_SQUARE, SHAPE_TRIANGLE, SHAPE_ORGAN, SHAPE_RANDOM, NUM_SHAPES };

// A shape written over a page. On T, the same pictures as times: a sawtooth's falling bars give high harmonics that
// die sooner, the random one a shimmer. seed: for the random one
inline void writeShape(float *page, int shape, bool timePage, uint32_t seed) {
	for (int i = 0; i < kBars; i++) {
		const int k = i + 1;
		float b = 0.f;
		switch (shape) {
			case SHAPE_SAW: b = powerBar(k, 1.f); break;
			case SHAPE_SQUARE: b = (k & 1) ? powerBar(k, 1.f) : 0.f; break;
			case SHAPE_TRIANGLE: b = (k & 1) ? powerBar(k, 2.f) : 0.f; break;
			case SHAPE_ORGAN: {
				// a pipe organ's stops: unison, octave, twelfth, fifteenth, seventeenth, nineteenth, twenty-second
				static const int stops[] = {1, 2, 3, 4, 5, 6, 8};
				static const float db[] = {0.f, -3.f, -9.f, -6.f, -15.f, -12.f, -14.f};
				for (int s = 0; s < 7; s++)
					if (k == stops[s])
						b = dbToBar(db[s]);
				break;
			}
			default: {
				seed = seed * 1664525u + 1013904223u;
				const float r = (seed >> 8) * (1.f / 16777216.f);
				b = timePage ? 0.2f + 0.6f * r : r * powerBar(k, 0.5f);
				break;
			}
		}
		if (timePage && shape != SHAPE_RANDOM)
			b = 0.25f + 0.5f * b;   // as times: from 1/4 to 4 times DECAY, not to silence
		page[i] = b;
	}
}

inline void Bars::setDefaults() {
	writeShape(v[0], SHAPE_SAW, false, 0);
	writeShape(v[1], SHAPE_SQUARE, false, 0);
	for (int i = 0; i < kBars; i++)
		v[2][i] = 0.5f;
}

// A page's neutral value: what the eraser leaves (silence on A and B, DECAY's own time on T)
inline float eraseValue(int page) {
	return page == 2 ? 0.5f : 0.f;
}

// The brush between two points of the screen, in bars (x: bar index, fractional; y: value 0..1): every bar it crosses
// takes the value of the line at that bar, and its neighbours within `width` bars move towards it, less the further they
// are, so a wide brush paints a smooth hill. Returns false when nothing changed.
inline bool strokeBars(float *page, float x0, float y0, float x1, float y1, float width) {
	if (x1 < x0) {
		std::swap(x0, x1);
		std::swap(y0, y1);
	}
	const float r = std::max(0.f, 0.5f * (width - 1.f));   // the brush's reach either side, in bars
	const int i0 = std::max(0, (int)std::floor(x0 - r + 0.5f)), i1 = std::min(kBars - 1, (int)std::floor(x1 + r + 0.5f));
	bool changed = false;
	for (int i = i0; i <= i1; i++) {
		const float x = std::min(std::max((float)i, x0), x1);   // the nearest point of the line
		const float y = x1 > x0 ? y0 + (y1 - y0) * (x - x0) / (x1 - x0) : y1;
		const float d = std::fabs((float)i - x);
		float w = r > 0.f ? 1.f - d / (r + 1.f) : (d <= 0.5f ? 1.f : 0.f);
		if (w <= 0.f)
			continue;
		w = std::min(1.f, w * w * (3.f - 2.f * w) * 1.0001f);
		const float nv = std::min(std::max(page[i] + (y - page[i]) * w, 0.f), 1.f);
		if (nv != page[i]) {
			page[i] = nv;
			changed = true;
		}
	}
	return changed;
}

// A straight line from (x0, y0) to (x1, y1) over the bars between, exactly (no soft edge)
inline void lineBars(float *page, float x0, float y0, float x1, float y1) {
	if (x1 < x0) {
		std::swap(x0, x1);
		std::swap(y0, y1);
	}
	const int i0 = std::max(0, (int)std::lround(x0)), i1 = std::min(kBars - 1, (int)std::lround(x1));
	for (int i = i0; i <= i1; i++) {
		const float y = i1 > i0 ? y0 + (y1 - y0) * (float)(i - i0) / (float)(i1 - i0) : y1;
		page[i] = std::min(std::max(y, 0.f), 1.f);
	}
}

// Values read from a patch: finite and in range, missing ones at the page's neutral value
inline float cleanBar(float x, int page) {
	return std::isfinite(x) ? std::min(std::max(x, 0.f), 1.f) : eraseValue(page);
}

} // namespace prisme
