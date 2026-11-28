// EMILE's drawing canvas without Rack: a stroke lands in the right harmonic band, undo, eraser, intensity, colours,
// the canvas as a 16-bit image. From the plugin folder:
//   g++ -std=c++17 -O1 -I src tools/tests/emile_canvas_test.cpp -o emile_canvas_test && ./emile_canvas_test
// Standalone test of EMILE's drawing path (src/EmileCanvas.hpp): rasterising, blends, undo, canvas -> EmileImage.
#include "EmileCanvas.hpp"
#include <cstdio>

static int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static unsigned px16(const EmileImage &im, unsigned x, unsigned y, int ch) {
	const unsigned char *p = &im.rgba[((size_t)y * im.width + x) * 8 + 2 * ch];
	return 256u * p[0] + p[1];
}

int main() {
	const int W = 256, H = 256;
	EmileCanvas c;
	c.reset(W, H);
	CHECK(c.valid());
	CHECK(c.rgba[3] == 0 && c.rgba[0] == 0);   // empty: transparent, silent

	// a vertical white stroke at x = W/4, size 8, full intensity, drawn in two pieces
	EmileUndo undo;
	undo.push(c);
	const std::vector<unsigned char> before = c.rgba;
	EmileBrush br;
	br.size = 8.f;
	emileStrokeSegment(c, W / 4.f, 0.f, W / 4.f, 100.f, br);
	emileStrokeSegment(c, W / 4.f, 100.f, W / 4.f, (float)H, br);
	EmileCanvas once;
	once.reset(W, H);
	emileStrokeSegment(once, W / 4.f, 0.f, W / 4.f, (float)H, br);
	CHECK(once.rgba == c.rgba);   // pieces or one segment: the same pixels

	// max-blend: the same stroke again changes nothing
	std::vector<unsigned char> s1 = c.rgba;
	emileStrokeSegment(c, W / 4.f, 0.f, W / 4.f, (float)H, br);
	CHECK(s1 == c.rgba);

	// to the played image: v * 257, the tables as a file load makes them
	EmileImage im;
	emileCanvasToImage(c.rgba.data(), W, H, im);
	CHECK(im.width == (unsigned)W && im.height == (unsigned)H && im.rgba.size() == (size_t)W * H * 8);
	CHECK(im.shape.size() == (size_t)W && im.logBase.size() == (size_t)W && im.shapeCurve == -1.f);
	CHECK(std::fabs(im.logBase[64] - std::log(1.f - 64.f / W)) < 1e-6f);
	CHECK(px16(im, 64, 128, 0) == 65535u && px16(im, 64, 128, 3) == 65535u);   // full white, alpha full
	CHECK(px16(im, 0, 128, 0) == 0u);
	// the row's harmonic bands as buildCycle reads them (16 bands, R G B on, A off)
	const int bands = 16;
	int lit = 0, litBand = -1;
	float level[bands + 1] = {};
	for (int k = 1; k <= bands; k++) {
		unsigned x0 = (unsigned)((k - 1) * (double)W / bands), x1 = (unsigned)(k * (double)W / bands);
		double sum = 0.0;
		for (unsigned x = x0; x < x1; x++)
			sum += px16(im, x, 128, 0) + px16(im, x, 128, 1) + px16(im, x, 128, 2);
		level[k] = (float)(sum / (65535.0 * 3 * (x1 - x0)));
		if (level[k] > 0.f) {
			lit++;
			if (litBand < 0 || level[k] > level[litBand])
				litBand = k;
		}
	}
	std::printf("x = W/4 stroke: bands lit %d, brightest k=%d (%.3f), k=4 %.3f, k=5 %.3f\n", lit, litBand, level[litBand], level[4], level[5]);
	CHECK(lit == 2 && level[4] > 0.f && level[5] > 0.f);   // column 64 is the edge of bands 4 and 5
	// the column brightness peaks on the stroke
	unsigned best = 0, bestX = 0;
	for (unsigned x = 0; x < (unsigned)W; x++)
		if (px16(im, x, 128, 0) > best) { best = px16(im, x, 128, 0); bestX = x; }
	CHECK(bestX >= 62 && bestX <= 65);

	// undo: the canvas as before the stroke
	CHECK(undo.pop(c));
	CHECK(c.rgba == before);
	CHECK(!undo.pop(c));
	// bounded at 16
	for (int i = 0; i < 40; i++)
		undo.push(c);
	CHECK(undo.snaps.size() == EmileUndo::kMax);

	// eraser: black and transparent where it passes, max-blend never brought back
	c = once;
	EmileBrush er = br;
	er.erase = true;
	er.size = 16.f;
	emileStrokeSegment(c, W / 4.f, 0.f, W / 4.f, (float)H, er);
	bool zero = true;
	for (int x = 60; x <= 68; x++)
		for (int ch = 0; ch < 3; ch++)
			zero = zero && c.rgba[((size_t)128 * W + x) * 4 + ch] == 0;
	CHECK(zero);
	CHECK(c.rgba[((size_t)128 * W + 64) * 4 + 3] == 0);   // erased: alpha 0 too

	// intensity 50 %: half brightness at the centre, and going over at 50 % stays there
	c.reset(W, H);
	EmileBrush half = br;
	half.intensity = 0.5f;
	emileStrokeSegment(c, 100.f, 100.f, 100.f, 100.f, half);
	emileStrokeSegment(c, 100.f, 100.f, 100.f, 100.f, half);
	const int centre = c.rgba[((size_t)100 * W + 100) * 4];
	std::printf("intensity 50 %%: centre %d / 255\n", centre);
	CHECK(centre >= 126 && centre <= 129);
	// a full stroke over it lifts it to full; a 50 % stroke over full leaves full
	emileStrokeSegment(c, 100.f, 100.f, 100.f, 100.f, br);
	emileStrokeSegment(c, 100.f, 100.f, 100.f, 100.f, half);
	CHECK(c.rgba[((size_t)100 * W + 100) * 4] == 255);

	// colour: red writes R only
	c.reset(W, H);
	EmileBrush red = br;
	red.channel[1] = red.channel[2] = false;
	emileStrokeSegment(c, 10.f, 10.f, 10.f, 10.f, red);
	const unsigned char *p = &c.rgba[((size_t)10 * W + 10) * 4];
	CHECK(p[0] == 255 && p[1] == 0 && p[2] == 0 && p[3] == 255);

	// size 1: a single pixel lit at full, its neighbours partly at most
	c.reset(64, 64);
	EmileBrush one = br;
	one.size = 1.f;
	emileStrokeSegment(c, 20.5f, 20.5f, 20.5f, 20.5f, one);
	CHECK(c.rgba[((size_t)20 * 64 + 20) * 4] == 255);
	CHECK(c.rgba[((size_t)20 * 64 + 22) * 4] == 0);

	// off the canvas: nothing, no crash
	c.reset(64, 64);
	CHECK(!emileStrokeSegment(c, -100.f, -100.f, -90.f, -90.f, br));

	std::printf(fails ? "%d FAILED\n" : "all passed\n", fails);
	return fails ? 1 : 0;
}
