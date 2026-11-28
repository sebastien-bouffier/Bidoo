#pragma once
// EMILE's image and its drawing canvas: plain C++ (no Rack), so a standalone test can check the drawing path.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

struct EmileImage {
	std::vector<unsigned char> rgba;  // 16 bits per channel, big endian, as lodepng gives it
	unsigned width = 0;
	unsigned height = 0;
	// Per pixel column: where it lands on the frequency curve (0 to 1), for
	// the curve it was computed at. Sized by the worker, filled by the audio
	// thread when the curve moves.
	std::vector<float> shape;
	float shapeCurve = -1.f;
	// Per pixel column: log(1 - x / width), worked out once by the worker, so a
	// moving curve costs one exp per column instead of a pow.
	std::vector<float> logBase;
};

// What every load does once the pixels are in (a file or a drawing): the per-column tables. Worker thread.
inline void emileFinishImage(EmileImage &im) {
	im.shape.assign(im.width, 0.f);
	im.logBase.resize(im.width);
	const float iWidth = 1.0f / im.width;
	for (unsigned x = 0; x < im.width; x++)
		im.logBase[x] = std::log(1.0f - x * iWidth);
}

// The drawing: 8-bit RGBA. Alpha follows the paint (0 where nothing is drawn, the brush's level where it is), so an empty
// canvas is silent with the A button on, as it is with R, G and B. Owned by the UI thread.
struct EmileCanvas {
	int width = 0, height = 0;
	std::vector<unsigned char> rgba;

	void reset(int w, int h) {
		width = w;
		height = h;
		rgba.assign((size_t)w * h * 4, 0);
	}
	bool valid() const { return width > 0 && height > 0 && rgba.size() == (size_t)width * height * 4; }
};

// The canvas as the played image: 8-bit values to 16 bits (v * 257: 255 stays full scale), then the tables.
inline void emileCanvasToImage(const unsigned char *rgba8, int w, int h, EmileImage &im) {
	im.width = (unsigned)w;
	im.height = (unsigned)h;
	const size_t n = (size_t)w * h * 4;
	im.rgba.resize(n * 2);
	// big endian, and v * 257 has v in both bytes
	for (size_t i = 0; i < n; i++)
		im.rgba[2 * i] = im.rgba[2 * i + 1] = rgba8[i];
	im.shapeCurve = -1.f;
	emileFinishImage(im);
}

struct EmileBrush {
	float size = 8.f;            // diameter, canvas px
	bool channel[3] = {true, true, true};   // the colour: which of R, G, B it writes
	float intensity = 1.f;       // 0 to 1
	bool erase = false;          // black instead of the colour
};

// A stroke segment (a capsule from a to b, a dot when a == b) with a soft edge. The brush max-blends, so going over the
// same place again never adds past its level; the eraser min-blends towards black. Both are the same whether the
// segment is drawn once or in many overlapping pieces. Returns false when nothing of it is on the canvas.
inline bool emileStrokeSegment(EmileCanvas &c, float ax, float ay, float bx, float by, const EmileBrush &br) {
	if (!c.valid())
		return false;
	const float r = std::max(0.5f, 0.5f * br.size);
	const float soft = std::max(0.5f, 0.4f * r);   // the soft rim, inside the radius
	const int x0 = std::max(0, (int)std::floor(std::min(ax, bx) - r - 1.f));
	const int x1 = std::min(c.width - 1, (int)std::ceil(std::max(ax, bx) + r + 1.f));
	const int y0 = std::max(0, (int)std::floor(std::min(ay, by) - r - 1.f));
	const int y1 = std::min(c.height - 1, (int)std::ceil(std::max(ay, by) + r + 1.f));
	if (x0 > x1 || y0 > y1)
		return false;
	const float dx = bx - ax, dy = by - ay;
	const float len2 = dx * dx + dy * dy;
	const float target = 255.f * std::min(std::max(br.intensity, 0.f), 1.f);
	for (int y = y0; y <= y1; y++)
		for (int x = x0; x <= x1; x++) {
			// distance from the pixel's centre to the segment
			const float px = x + 0.5f - ax, py = y + 0.5f - ay;
			const float t = len2 > 0.f ? std::min(std::max((px * dx + py * dy) / len2, 0.f), 1.f) : 0.f;
			const float ex = px - t * dx, ey = py - t * dy;
			const float d = std::sqrt(ex * ex + ey * ey);
			float cov = (r + 0.5f - d) / (soft + 0.5f);
			if (cov <= 0.f)
				continue;
			cov = std::min(cov, 1.f);
			cov = cov * cov * (3.f - 2.f * cov);   // smooth rim
			unsigned char *p = &c.rgba[((size_t)y * c.width + x) * 4];
			if (br.erase) {
				const unsigned char cap = (unsigned char)std::lround(255.f * (1.f - cov));
				for (int ch = 0; ch < 4; ch++)   // alpha too: erased is silent
					p[ch] = std::min(p[ch], cap);
			}
			else {
				const unsigned char v = (unsigned char)std::lround(target * cov);
				for (int ch = 0; ch < 3; ch++)
					if (br.channel[ch])
						p[ch] = std::max(p[ch], v);
				p[3] = std::max(p[3], v);   // alpha at the paint's level, whatever the colour
			}
		}
	return true;
}

// Whole-canvas snapshots taken before each stroke (or clear, or new canvas): 16 at most, 1 MB each at 512 x 512.
// A linear gradient over the whole canvas, from nothing at a to the brush's colour and intensity at b (held before a
// and past b), max-blended like the brush so it adds to what is there. In harmonics mode a horizontal one is a timbre
// that rises or falls with the harmonics, a vertical one a timbre that changes as POS moves.
inline void emileGradient(EmileCanvas &c, float ax, float ay, float bx, float by, const EmileBrush &br) {
	if (!c.valid())
		return;
	const float dx = bx - ax, dy = by - ay;
	const float len2 = dx * dx + dy * dy;
	if (len2 < 1.f)
		return;
	const float target = 255.f * std::min(std::max(br.intensity, 0.f), 1.f);
	for (int y = 0; y < c.height; y++)
		for (int x = 0; x < c.width; x++) {
			const float t = std::min(std::max(((x + 0.5f - ax) * dx + (y + 0.5f - ay) * dy) / len2, 0.f), 1.f);
			const unsigned char v = (unsigned char)std::lround(target * t);
			unsigned char *p = &c.rgba[((size_t)y * c.width + x) * 4];
			for (int ch = 0; ch < 3; ch++)
				if (br.channel[ch])
					p[ch] = std::max(p[ch], v);
			p[3] = std::max(p[3], v);
		}
}

// One change of the canvas for Rack's undo history: the rectangle that changed, before and after (the whole canvas when
// its size changed). Kept small: a stroke stores only its own rectangle.
struct EmileEdit {
	int w0 = 0, h0 = 0, w1 = 0, h1 = 0;   // the canvas's size before and after
	int x = 0, y = 0, w = 0, h = 0;       // the rectangle (the whole canvas when the size changed)
	std::vector<unsigned char> before, after;
	bool empty() const { return w <= 0 || h <= 0; }
};

// the edit from the canvas as it was (rgba0, w0 x h0) to the canvas now
inline EmileEdit emileMakeEdit(const std::vector<unsigned char> &rgba0, int w0, int h0, const EmileCanvas &c) {
	EmileEdit e;
	e.w0 = w0; e.h0 = h0; e.w1 = c.width; e.h1 = c.height;
	if (w0 != c.width || h0 != c.height) {
		e.x = e.y = 0;
		e.w = std::max(w0, c.width);   // marks it as not empty; the whole canvases are kept
		e.h = std::max(h0, c.height);
		e.before = rgba0;
		e.after = c.rgba;
		return e;
	}
	int x0 = c.width, y0 = c.height, x1 = -1, y1 = -1;
	for (int y = 0; y < c.height; y++)
		for (int x = 0; x < c.width; x++) {
			const size_t i = ((size_t)y * c.width + x) * 4;
			if (std::memcmp(&rgba0[i], &c.rgba[i], 4) != 0) {
				x0 = std::min(x0, x); x1 = std::max(x1, x);
				y0 = std::min(y0, y); y1 = std::max(y1, y);
			}
		}
	if (x1 < 0)
		return e;   // nothing changed
	e.x = x0; e.y = y0; e.w = x1 - x0 + 1; e.h = y1 - y0 + 1;
	e.before.resize((size_t)e.w * e.h * 4);
	e.after.resize((size_t)e.w * e.h * 4);
	for (int y = 0; y < e.h; y++) {
		const size_t src = ((size_t)(e.y + y) * c.width + e.x) * 4;
		std::memcpy(&e.before[(size_t)y * e.w * 4], &rgba0[src], (size_t)e.w * 4);
		std::memcpy(&e.after[(size_t)y * e.w * 4], &c.rgba[src], (size_t)e.w * 4);
	}
	return e;
}

// the edit undone (back to before) or redone (to after); false when the canvas is not the one it was made on
inline bool emileApplyEdit(EmileCanvas &c, const EmileEdit &e, bool undo) {
	const int fromW = undo ? e.w1 : e.w0, fromH = undo ? e.h1 : e.h0;
	if (!c.valid() || c.width != fromW || c.height != fromH)
		return false;
	if (e.w0 != e.w1 || e.h0 != e.h1) {
		c.width = undo ? e.w0 : e.w1;
		c.height = undo ? e.h0 : e.h1;
		c.rgba = undo ? e.before : e.after;
		return true;
	}
	const std::vector<unsigned char> &src = undo ? e.before : e.after;
	for (int y = 0; y < e.h; y++)
		std::memcpy(&c.rgba[((size_t)(e.y + y) * c.width + e.x) * 4], &src[(size_t)y * e.w * 4], (size_t)e.w * 4);
	return true;
}

struct EmileUndo {
	static constexpr size_t kMax = 16;
	struct Snap {
		int width, height;
		std::vector<unsigned char> rgba;
	};
	std::deque<Snap> snaps;

	void push(const EmileCanvas &c) {
		if (!c.valid())
			return;
		if (snaps.size() >= kMax)
			snaps.pop_front();
		snaps.push_back({c.width, c.height, c.rgba});
	}
	// the canvas back as it was before the last stroke; false if there is none
	bool pop(EmileCanvas &c) {
		if (snaps.empty())
			return false;
		Snap &s = snaps.back();
		c.width = s.width;
		c.height = s.height;
		c.rgba.swap(s.rgba);
		snaps.pop_back();
		return true;
	}
	// the snapshot of the stroke in progress (for the line tool, which redraws from it)
	const Snap *last() const { return snaps.empty() ? nullptr : &snaps.back(); }
	void clear() { snaps.clear(); }
	bool empty() const { return snaps.empty(); }
};
