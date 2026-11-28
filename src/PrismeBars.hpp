#pragma once
// PRiSME's drawn pages, 64 bars each, 0..1: four spectra (A to D, morphed in a chain), the attack and decay times of
// each harmonic, and a formant filter drawn over frequency. Plain C++ (no Rack), so a standalone test can check the
// drawing path. The tools follow EMILE's (src/EmileCanvas.hpp): a soft brush whose width is set by the wheel, a
// straight line, the eraser on the right button, shapes, undo through Rack's history.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace prisme {

static constexpr int kBars = 64;
enum BarPage { PAGE_A, PAGE_B, PAGE_C, PAGE_D, PAGE_ATTACK, PAGE_DECAY, PAGE_FORMANT, NUM_BAR_PAGES };
static constexpr int kBarPages = NUM_BAR_PAGES;
static constexpr int kSpectra = 4;

inline bool isSpectrum(int page) { return page < kSpectra; }
inline bool isTimePage(int page) { return page == PAGE_ATTACK || page == PAGE_DECAY; }

// The formant page: band i is centred at kFormantLow x 2^(i x kFormantOctaves / 63), from 50 Hz to 12.8 kHz; a bar is
// a gain in dB, (bar - kFormantUnity) x kFormantDbSpan: 0 dB at 0.75, +12 dB at the top, -36 dB at the bottom
static constexpr float kFormantLow = 50.f;
static constexpr float kFormantOctaves = 8.f;
static constexpr float kFormantUnity = 0.75f;
static constexpr float kFormantDbSpan = 48.f;

struct Bars {
	float v[kBarPages][kBars];

	// what a page holds when nothing is drawn: A a sawtooth, B a square, C a triangle, D an organ's stops; the times
	// at x1; the formant one bump at 1 kHz
	void setDefaults();
	bool operator==(const Bars &o) const { return std::memcmp(v, o.v, sizeof(v)) == 0; }
	bool operator!=(const Bars &o) const { return !(*this == o); }
};

// A level in dB as a spectrum bar (the engine's 60 dB range): 0 dB is 1, -60 dB is 0
inline float dbToBar(float db) {
	return std::min(std::max(1.f + db / 60.f, 0.f), 1.f);
}

// 1/k^p in dB as a bar: a sawtooth is p = 1, a triangle's odd harmonics p = 2
inline float powerBar(int k, float p) {
	return dbToBar(-20.f * p * std::log10((float)k));
}

// A formant gain in dB as a formant bar
inline float formantDbToBar(float db) {
	return std::min(std::max(kFormantUnity + db / kFormantDbSpan, 0.f), 1.f);
}

// The centre of formant band i, in Hz
inline float formantBandHz(float i) {
	return kFormantLow * std::exp2(i * kFormantOctaves / (kBars - 1));
}

enum Shape { SHAPE_SAW, SHAPE_SQUARE, SHAPE_TRIANGLE, SHAPE_ORGAN, SHAPE_RANDOM, NUM_SHAPES };

// Shape names: on a spectrum or a time page the waveforms, on the formant page five vowels
inline const char *shapeName(int shape, int page) {
	static const char *wave[NUM_SHAPES] = {"Sawtooth", "Square", "Triangle", "Organ", "Random"};
	static const char *vowel[NUM_SHAPES] = {"Vowel A", "Vowel E", "Vowel I", "Vowel O", "Vowel OU"};
	return page == PAGE_FORMANT ? vowel[shape] : wave[shape];
}

// Formant peaks of five vowels (Hz, dB): textbook averages of an adult voice
inline void vowelPeaks(int vowel, float hz[3], float db[3]) {
	static const float f[NUM_SHAPES][3] = {{800, 1200, 2500}, {400, 2100, 2800}, {300, 2300, 3000}, {450, 800, 2600}, {325, 700, 2500}};
	static const float g[NUM_SHAPES][3] = {{12, 9, 3}, {12, 6, 4}, {12, 5, 5}, {12, 9, 0}, {12, 6, -2}};
	for (int i = 0; i < 3; i++) {
		hz[i] = f[vowel][i];
		db[i] = g[vowel][i];
	}
}

// A shape written over a page. On a time page the same pictures as times: a sawtooth's falling bars give high harmonics
// that are quicker, the random one a shimmer. On the formant page a vowel. seed: for the random one
inline void writeShape(float *page, int shape, int pageId, uint32_t seed) {
	if (pageId == PAGE_FORMANT) {
		float hz[3], db[3];
		vowelPeaks(std::min(std::max(shape, 0), NUM_SHAPES - 1), hz, db);
		for (int i = 0; i < kBars; i++) {
			const float o = std::log2(formantBandHz((float)i));
			float gain = -24.f;   // between the peaks
			for (int p = 0; p < 3; p++) {
				const float d = (o - std::log2(hz[p])) / 0.22f;
				gain = std::max(gain, -24.f + (db[p] + 24.f) * std::exp(-0.5f * d * d));
			}
			page[i] = formantDbToBar(gain);
		}
		return;
	}
	const bool timePage = isTimePage(pageId);
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
			b = 0.25f + 0.5f * b;   // as times: from 1/4 to 4 times the knob, not to nothing
		page[i] = b;
	}
}

// The formant page's default: one bump at 1 kHz, +9.5 dB, down to -36 dB two octaves away (the bump PRiSME had first)
inline void writeDefaultFormant(float *page) {
	for (int i = 0; i < kBars; i++) {
		const float d = std::log2(formantBandHz((float)i) / 1000.f) / 0.6f;
		const float bump = std::exp(-0.5f * d * d);
		page[i] = formantDbToBar(-36.f + 45.5f * bump);
	}
}

inline void Bars::setDefaults() {
	writeShape(v[PAGE_A], SHAPE_SAW, PAGE_A, 0);
	writeShape(v[PAGE_B], SHAPE_SQUARE, PAGE_B, 0);
	writeShape(v[PAGE_C], SHAPE_TRIANGLE, PAGE_C, 0);
	writeShape(v[PAGE_D], SHAPE_ORGAN, PAGE_D, 0);
	for (int i = 0; i < kBars; i++)
		v[PAGE_ATTACK][i] = v[PAGE_DECAY][i] = 0.5f;
	writeDefaultFormant(v[PAGE_FORMANT]);
}

// A page's neutral value: what the eraser leaves (silence on a spectrum, the knob's own time on a time page, 0 dB on
// the formant page)
inline float eraseValue(int page) {
	return isTimePage(page) ? 0.5f : (page == PAGE_FORMANT ? kFormantUnity : 0.f);
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
