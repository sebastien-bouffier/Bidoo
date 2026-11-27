// The samplers' screens drawn one way (sOUNAiRe, cANARd, OUAIve, eDsaroS, cOPIA, pARtiCuLeS): the waveform in hot pink
// where it plays (inside START..END and the loop), dimmer pink elsewhere; each column the peaks it covers, with its RMS
// as a brighter core inside, so the loud parts stand out from the peaks; one column per pixel of the screen, so it
// stays sharp when Rack is zoomed in; the loop shaded in pink; START, END, loop points and cuts as yellow bars; play
// heads as electric blue bars added onto what is under them, so they light the pink up.
// Include after BidooTheme.hpp (BidooComponents.hpp does).
#pragma once

namespace wavestyle {

const NVGcolor kActive = nvgRGB(0xFF, 0x2D, 0x95);           // the waveform where it plays
const NVGcolor kIdle = nvgRGBA(0xA4, 0x03, 0x6F, 0x8C);       // PINK_BIDOO, dimmed: where it does not play
const NVGcolor kLoop = nvgRGBA(0xFF, 0x2D, 0x95, 0x38);       // behind the loop
const NVGcolor kBar = theme::kScreenYellow;                   // START, END, loop points, cuts
const NVGcolor kHead = nvgRGB(0x29, 0x8C, 0xFF);              // play heads
const NVGcolor kActivePeak = nvgRGBA(0xFF, 0x2D, 0x95, 0x8C); // the peaks around the core, where it plays
const NVGcolor kIdlePeak = nvgRGBA(0xA4, 0x03, 0x6F, 0x59);   // and where it does not
const NVGcolor kActiveCore = nvgRGB(0xFF, 0x6B, 0xB5);        // the RMS core, where it plays
const NVGcolor kIdleCore = nvgRGBA(0xC2, 0x1C, 0x86, 0xA6);   // and where it does not

// One column: the lowest and highest values it covers (-1..1) and their RMS (0..1). Empty while hi < lo.
struct Col {
	float lo = 1.f, hi = -1.f, rms = 0.f;
	bool line = false;   // it covers less than a cycle of a low note: the wave itself shows, drawn bright, no core
	bool empty() const { return hi < lo; }
};

// The colours of a waveform: peaks and core, where it plays and where it does not (sOUNAiRe's blue theme has its own)
struct Palette {
	NVGcolor activePeak = kActivePeak, idlePeak = kIdlePeak, activeCore = kActiveCore, idleCore = kIdleCore;
};

// A screen pixel in the widget's units: 1 at 100 % zoom, 0.5 at 200 %, so a column that wide is one pixel of the
// screen (at most four columns per unit)
inline float pixelStep(NVGcontext *vg) {
	float t[6];
	nvgCurrentTransform(vg, t);
	const float s = std::sqrt(std::fabs(t[0] * t[3] - t[1] * t[2]));
	return 1.f / clamp(s, 1.f, 4.f);
}

// A waveform across 0..width, one column per screen pixel: `get(x0, x1)` gives the Col for widget x0..x1. The columns
// inside activeX0..activeX1 are the part that plays. `gain` scales the values (a display normalized), clamped to the
// screen. Peaks first, the RMS core over them, the part that plays last.
template <class Get>
void wave(NVGcontext *vg, float width, float mid, float amp, float activeX0, float activeX1, Get get, float gain = 1.f,
          const Palette &pal = Palette()) {
	const float step = pixelStep(vg);
	const int n = std::max(0, (int)std::ceil(width / step));
	std::vector<Col> cols((size_t)n);
	for (int i = 0; i < n; i++)
		cols[(size_t)i] = get(i * step, std::min(width, (i + 1) * step));
	// joined: where the wave moves faster than a column, two neighbours would not touch (dots instead of a line);
	// each grows to meet the other halfway
	for (int i = 1; i < n; i++) {
		Col &a = cols[(size_t)i - 1], &b = cols[(size_t)i];
		if (a.empty() || b.empty())
			continue;
		if (b.lo > a.hi) {
			const float m = 0.5f * (a.hi + b.lo);
			a.hi = m;
			b.lo = m;
		}
		else if (b.hi < a.lo) {
			const float m = 0.5f * (a.lo + b.hi);
			a.lo = m;
			b.hi = m;
		}
	}
	for (int pass = 0; pass < 2; pass++) {
		const bool active = pass == 1;
		for (int layer = 0; layer < 2; layer++) {
			nvgBeginPath(vg);
			for (int i = 0; i < n; i++) {
				const Col &c = cols[(size_t)i];
				const float x = i * step;
				if (c.empty() || (x >= activeX0 && x <= activeX1) != active)
					continue;
				const float lo = clamp(c.lo * gain, -1.f, 1.f), hi = clamp(c.hi * gain, -1.f, 1.f);
				if (c.line) {
					// zoomed in to the wave itself: one bright line, no core
					if (layer == 1)
						nvgRect(vg, x, mid - hi * amp, step, std::max(step, (hi - lo) * amp));
				}
				else if (layer == 0)
					nvgRect(vg, x, mid - hi * amp, step, std::max(step, (hi - lo) * amp));
				else {
					// the core: +-RMS, inside the peaks (a column with an offset keeps its core inside)
					const float r = clamp(c.rms * gain, 0.f, 1.f);
					const float top = std::min(hi, r), bottom = std::max(lo, -r);
					if (top > bottom)
						nvgRect(vg, x, mid - top * amp, step, (top - bottom) * amp);
				}
			}
			nvgFillColor(vg, layer == 0 ? (active ? pal.activePeak : pal.idlePeak) : (active ? pal.activeCore : pal.idleCore));
			nvgFill(vg);
		}
	}
}

// The Col of frames j0..j1 (inclusive) of a signal, read by `at(j)`
template <class At>
inline Col colOf(int64_t j0, int64_t j1, At at) {
	Col c;
	double sq = 0.0;
	for (int64_t j = j0; j <= j1; j++) {
		const float v = at(j);
		c.lo = std::min(c.lo, v);
		c.hi = std::max(c.hi, v);
		sq += (double)v * v;
	}
	if (j1 >= j0)
		c.rms = (float)std::sqrt(sq / (double)(j1 - j0 + 1));
	return c;
}

// The Col of overview bins k0..k1-1 (lowest, highest and RMS of each bin)
inline Col colOfBins(const float *mn, const float *mx, const float *rms, int k0, int k1) {
	Col c;
	double sq = 0.0;
	for (int k = k0; k < k1; k++) {
		c.lo = std::min(c.lo, mn[k]);
		c.hi = std::max(c.hi, mx[k]);
		sq += (double)rms[k] * rms[k];
	}
	if (k1 > k0)
		c.rms = (float)std::sqrt(sq / (double)(k1 - k0));
	return c;
}

// A column of fewer frames (about 20 ms, a cycle of 47 Hz) shows the wave itself: it is drawn as a line, without the
// RMS core, which over so few frames would follow the wave and light up in grains (a short kick, or a zoom)
constexpr double kLineFrames = 1024.0;

// One channel of a sample at frames f0..f1 (fractional): from its overview (`bins` bins over `total` frames) when a
// column covers a bin or more, from the frames themselves (`at(j)`) when zoomed in
template <class At>
inline Col colOfSample(double f0, double f1, int64_t total, const float *mn, const float *mx, const float *rms, int bins,
                       bool haveFrames, At at) {
	if (f1 <= 0.0 || f0 >= (double)total || total <= 0)
		return Col();
	const double binFrames = (double)total / bins;
	auto range = [&](double g0, double g1) {
		if (g1 - g0 >= binFrames || !haveFrames) {
			const int k0 = clamp((int)(g0 / binFrames), 0, bins - 1), k1 = clamp((int)(g1 / binFrames), k0 + 1, bins);
			return colOfBins(mn, mx, rms, k0, k1);
		}
		const int64_t j0 = std::clamp<int64_t>((int64_t)g0, 0, total - 1), j1 = std::min<int64_t>(total - 1, std::max<int64_t>(j0, (int64_t)std::ceil(g1)));
		return colOf(j0, j1, at);
	};
	Col c = range(f0, f1);
	c.line = f1 - f0 < kLineFrames;
	return c;
}

// Fills one overview bin from frames i0..i1-1 read by `at(i)`
template <class At>
inline void binOf(int64_t i0, int64_t i1, At at, float &mn, float &mx, float &rms) {
	float lo = 0.f, hi = 0.f;
	double sq = 0.0;
	for (int64_t i = i0; i < i1; i++) {
		const float v = at(i);
		lo = std::min(lo, v);
		hi = std::max(hi, v);
		sq += (double)v * v;
	}
	mn = lo;
	mx = hi;
	rms = i1 > i0 ? (float)std::sqrt(sq / (double)(i1 - i0)) : 0.f;
}

// A live peak bin with its RMS, written sample by sample on the audio thread (cOPIA, pARtiCuLeS): `add` for each
// sample of the bin being written, `start` when the writing moves on to another bin
struct LiveBin {
	double sq = 0.0;
	int n = 0;
	void start() {
		sq = 0.0;
		n = 0;
	}
	float add(float v) {
		sq += (double)v * v;
		n++;
		return (float)std::sqrt(sq / n);
	}
};

// One column of the waveform: a rectangle from its highest to its lowest value (-1..1), `amp` px either side of
// `mid`. Call between nvgBeginPath and fillWave.
inline void column(NVGcontext *vg, float x, float lo, float hi, float mid, float amp, float w = 1.f) {
	lo = clamp(lo, -1.f, 1.f);
	hi = clamp(hi, -1.f, 1.f);
	nvgRect(vg, x, mid - hi * amp, w, std::max(1.f, (hi - lo) * amp));
}

inline void fill(NVGcontext *vg, bool active) {
	nvgFillColor(vg, active ? kActive : kIdle);
	nvgFill(vg);
}

// The loop (or the part that plays, where a module shows it as a band), behind the waveform
inline void band(NVGcontext *vg, float x0, float x1, float top, float bottom, NVGcolor c = kLoop) {
	if (x1 < x0)
		std::swap(x0, x1);
	nvgBeginPath(vg);
	nvgRect(vg, x0, top, x1 - x0, bottom - top);
	nvgFillColor(vg, c);
	nvgFill(vg);
}

// A yellow bar; flag 1 or -1 adds a small flag at the top, pointing right (a start) or left (an end)
inline void bar(NVGcontext *vg, float x, float top, float bottom, int flag = 0, NVGcolor c = kBar) {
	nvgBeginPath(vg);
	nvgRect(vg, x - 0.5f, top, 1.f, bottom - top);
	nvgFillColor(vg, c);
	nvgFill(vg);
	if (flag) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, x, top);
		nvgLineTo(vg, x + 5.f * flag, top);
		nvgLineTo(vg, x, top + 6.f);
		nvgClosePath(vg);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}
}

// A play head: a 2 px blue bar with a faint glow, added onto what is under it (NVG_LIGHTER), so the pink under it
// turns white-violet. `strength` (0..1) dims it, for a voice that fades. Draw it after the waveform and the bars.
// `c`: another colour (cOPIA's write head is orange), its glow a quarter of it.
inline void head(NVGcontext *vg, float x, float top, float bottom, float strength = 1.f, NVGcolor c = kHead) {
	strength = clamp(strength, 0.f, 1.f);
	auto scaled = [&](NVGcolor c) { return nvgRGBf(c.r * strength, c.g * strength, c.b * strength); };
	nvgSave(vg);
	nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
	nvgBeginPath(vg);
	nvgRect(vg, x - 3.f, top, 2.f, bottom - top);
	nvgRect(vg, x + 1.f, top, 2.f, bottom - top);
	nvgFillColor(vg, scaled(nvgRGBf(c.r * 0.25f, c.g * 0.25f, c.b * 0.25f)));
	nvgFill(vg);
	nvgBeginPath(vg);
	nvgRect(vg, x - 1.f, top, 2.f, bottom - top);
	nvgFillColor(vg, scaled(c));
	nvgFill(vg);
	nvgRestore(vg);
}

}  // namespace wavestyle
