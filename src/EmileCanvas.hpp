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
