// PRiSME's engine without Rack: pitch and level, the spectrum it plays, band-limiting, envelopes per harmonic, release
// to silence, no clicks at note-on, NaN in the CVs, rate changes, and the CPU of 16 voices.
//   g++ -std=c++17 -O3 -march=nehalem -funsafe-math-optimizations -I src -I G:/Rack-SDK/include -I G:/Rack-SDK/dep/include
//       tools/tests/prisme_engine_test.cpp -o prisme_test && ./prisme_test
#include "PrismeEngine.hpp"
#include "PrismeBars.hpp"
#include <chrono>
#include <cstdio>
#include <vector>

using namespace prisme;

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Rig {
	Engine e;
	Engine::Io io;
	Bars bars;
	float voct[16] = {}, gate[16] = {};
	Rig(float sr = 48000.f) {
		e.setSampleRate(sr);
		bars.setDefaults();
		io.bars = &bars.v[0][0];
		io.voct = voct;
		io.voctChannels = 1;
		io.attack = 0.001f;
		io.sustain = 1.f;
		io.decay = 1.f;
		io.release = 0.3f;
	}
	void only(int k) {   // A: harmonic k alone at full level
		for (int i = 0; i < kBars; i++)
			bars.v[0][i] = 0.f;
		bars.v[0][k - 1] = 1.f;
	}
	std::vector<float> run(int n) {
		std::vector<float> out(n);
		for (int i = 0; i < n; i++) {
			e.process(io);
			out[i] = io.left;
		}
		return out;
	}
};

// the level of frequency f in x, as a sine's peak (a Hann-windowed DFT)
static double level(const std::vector<float> &x, double f, double sr) {
	double re = 0, im = 0, wsum = 0;
	const size_t n = x.size();
	for (size_t i = 0; i < n; i++) {
		const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1));
		re += w * x[i] * std::cos(2 * M_PI * f * i / sr);
		im += w * x[i] * std::sin(2 * M_PI * f * i / sr);
		wsum += w;
	}
	return 2 * std::sqrt(re * re + im * im) / wsum;
}

static double db(double a) { return 20 * std::log10(std::max(a, 1e-12)); }

static float peak(const std::vector<float> &x, size_t from = 0) {
	float m = 0.f;
	for (size_t i = from; i < x.size(); i++)
		m = std::max(m, std::fabs(x[i]));
	return m;
}

int main() {
	const float sr = 48000.f;
	{
		// sin2pi against std::sin
		double worst = 0;
		for (int i = 0; i < 100000; i++) {
			const float p = i / 100000.f;
			worst = std::max(worst, (double)std::fabs(sin2pi(float_4(p))[0] - std::sin(2 * M_PI * p)));
		}
		CHECK(worst < 2e-6, "sin2pi error %g", worst);
	}
	{
		// one harmonic, gate unplugged (held): a sine at C4, kVolts peak
		Rig r;
		r.only(1);
		std::vector<float> x = r.run(48000);
		std::vector<float> tail(x.begin() + 24000, x.end());
		const double a = level(tail, Engine::kC4, sr);
		CHECK(std::fabs(a - Engine::kVolts) < 0.05, "a single harmonic peaks at %g V, not %g", a, Engine::kVolts);
		CHECK(std::fabs(peak(tail) - Engine::kVolts) < 0.05f, "peak %g", peak(tail));
		// the same at +1 V: C5
		r.voct[0] = 1.f;
		x = r.run(48000);
		tail.assign(x.begin() + 24000, x.end());
		CHECK(std::fabs(level(tail, 2 * Engine::kC4, sr) - Engine::kVolts) < 0.05, "C5 level %g", level(tail, 2 * Engine::kC4, sr));
	}
	{
		// the default A (a sawtooth's 1/k): harmonic 2 at -6 dB, 8 at -18 dB, under 10 V
		Rig r;
		r.voct[0] = -1.f;
		std::vector<float> x = r.run(48000);
		std::vector<float> tail(x.begin() + 24000, x.end());
		const double f0 = Engine::kC4 / 2, l1 = level(tail, f0, sr);
		CHECK(std::fabs(db(level(tail, 2 * f0, sr) / l1) + 6.02) < 0.3, "saw h2 %g dB", db(level(tail, 2 * f0, sr) / l1));
		CHECK(std::fabs(db(level(tail, 8 * f0, sr) / l1) + 18.06) < 0.5, "saw h8 %g dB", db(level(tail, 8 * f0, sr) / l1));
		CHECK(peak(tail) < 10.f, "saw peak %g", peak(tail));
		std::printf("saw at C3: peak %.2f V, fundamental %.2f V\n", peak(tail), l1);
		// B (odd only, a square) at full MORPH: no even harmonics
		r.io.morph = 1.f;
		x = r.run(48000);
		tail.assign(x.begin() + 24000, x.end());
		CHECK(db(level(tail, 2 * f0, sr) / level(tail, f0, sr)) < -80, "square h2 %g dB", db(level(tail, 2 * f0, sr) / level(tail, f0, sr)));
		// ODD/EVEN at -1 on the saw: no even harmonics either
		r.io.morph = 0.f;
		r.io.oddEven = -1.f;
		x = r.run(48000);
		tail.assign(x.begin() + 24000, x.end());
		CHECK(db(level(tail, 4 * f0, sr) / level(tail, f0, sr)) < -80, "odd only h4 %g dB", db(level(tail, 4 * f0, sr) / level(tail, f0, sr)));
	}
	{
		// band-limiting: C9 (8372 Hz) at 48 kHz: harmonic 2 (16.7 kHz) plays, 3 (25.1 kHz) would fold to 22.9 kHz
		Rig r;
		for (int i = 0; i < kBars; i++)
			r.bars.v[0][i] = 1.f;
		r.voct[0] = 5.f;
		std::vector<float> x = r.run(48000);
		std::vector<float> tail(x.begin() + 24000, x.end());
		const double f0 = Engine::kC4 * 32, l1 = level(tail, f0, sr);
		CHECK(db(level(tail, sr - 3 * f0, sr) / l1) < -100, "alias of h3 at %g dB", db(level(tail, sr - 3 * f0, sr) / l1));
		CHECK(level(tail, 2 * f0, sr) > 0.1 * l1, "h2 missing");
		// STRETCH up: the harmonics move off their multiples (harmonic 2 at 2^1.12)
		Rig s;
		s.only(2);
		s.io.stretch = 1.f;
		x = s.run(48000);
		tail.assign(x.begin() + 24000, x.end());
		CHECK(std::fabs(level(tail, Engine::kC4 * std::pow(2.0, 1.12), sr) - Engine::kVolts) < 0.1, "stretched h2 %g",
			level(tail, Engine::kC4 * std::pow(2.0, 1.12), sr));
	}
	{
		// a note: gate on, attack, no click; gate off, release to silence in about RELEASE
		Rig r;
		r.only(1);
		r.io.gate = r.gate;
		r.io.gateChannels = 1;
		r.run(1000);
		CHECK(r.e.voices[0].stage == Engine::IDLE && peak(r.run(100)) == 0.f, "silent before the gate");
		r.gate[0] = 10.f;
		std::vector<float> x = r.run(24000);
		float jump = 0.f;
		for (size_t i = 1; i < x.size(); i++)
			jump = std::max(jump, std::fabs(x[i] - x[i - 1]));
		const float slope = 2 * M_PI * Engine::kC4 / sr * Engine::kVolts;   // the sine's steepest step
		CHECK(jump <= slope * 1.05f, "note-on step %g V (a sine's %g)", jump, slope);
		r.gate[0] = 0.f;
		x = r.run((int)(0.3f * sr));
		CHECK(db(peak(x, x.size() - 480) / Engine::kVolts) < -50 && db(peak(x, x.size() - 480) / Engine::kVolts) > -70,
			"after RELEASE the level is %g dB, not about -60", db(peak(x, x.size() - 480) / Engine::kVolts));
		r.run(48000);
		CHECK(r.e.voices[0].stage == Engine::IDLE && r.e.voices[0].groups == 0, "the voice did not stop");
	}
	{
		// envelopes per harmonic: DAMP 1, SUSTAIN 0, DECAY 2 s: harmonic 8 decays 8 times faster than the fundamental.
		// T page: harmonic 2 at 8 times DECAY
		Rig r;
		r.io.gate = r.gate;
		r.io.gateChannels = 1;
		r.io.sustain = 0.f;
		r.io.decay = 2.f;
		r.io.damp = 1.f;
		r.bars.v[2][1] = 1.f;
		r.gate[0] = 10.f;
		r.run((int)(0.5f * sr));
		const Engine::Voice &v = r.e.voices[0];
		const float e1 = v.env[0][0], e2 = v.env[0][1], e8 = v.env[1][3];
		// after 0.5 s: fundamental -60 * 0.5 / 2 = -15 dB; harmonic 2: 2 s * 8 / 2 = 8 s, -3.75 dB; 8: 0.25 s, gone
		CHECK(std::fabs(db(e1) + 15) < 0.5, "fundamental at %g dB", db(e1));
		CHECK(std::fabs(db(e2) + 3.75) < 0.5, "harmonic 2 at %g dB", db(e2));
		CHECK(db(e8) < -100, "harmonic 8 at %g dB", db(e8));
	}
	{
		// SWEEP: a note starts on A and reaches MORPH's position in SWEEP's time
		Rig r;
		r.io.gate = r.gate;
		r.io.gateChannels = 1;
		r.io.morph = 1.f;
		r.io.sweep = 0.5f;
		r.gate[0] = 10.f;
		r.voct[0] = -1.f;
		std::vector<float> x = r.run(1440);   // the first 30 ms: MORPH under 0.06
		const double f0 = Engine::kC4 / 2;
		CHECK(db(level(x, 2 * f0, sr) / level(x, f0, sr)) > -8, "the start is not A's sawtooth (h2 %g dB)", db(level(x, 2 * f0, sr) / level(x, f0, sr)));
		r.run(27360);
		x = r.run(9600);
		CHECK(db(level(x, 2 * f0, sr) / level(x, f0, sr)) < -80, "the end is not B's square");
	}
	{
		// a formant at 2 kHz, full depth, on the saw at C3: harmonics near 2 kHz up, far ones down
		Rig r;
		r.voct[0] = -1.f;
		r.io.formantHz = 2093.f;   // harmonic 16 of C3
		r.io.depth = 1.f;
		std::vector<float> x = r.run(48000);
		std::vector<float> tail(x.begin() + 24000, x.end());
		const double f0 = Engine::kC4 / 2;
		CHECK(level(tail, 16 * f0, sr) > 4 * level(tail, f0, sr), "the formant does not lift harmonic 16 over the fundamental");
	}
	{
		// NaN in V/O and a CV, then a rate change: the output stays finite
		Rig r;
		float nan[1] = {NAN};
		r.voct[0] = NAN;
		r.io.morphCv = nan;
		r.io.morphChannels = 1;
		std::vector<float> x = r.run(4800);
		bool finite = true;
		for (float v : x)
			finite &= std::isfinite(v);
		r.voct[0] = 0.f;
		r.e.setSampleRate(96000.f);
		x = r.run(9600);
		for (float v : x)
			finite &= std::isfinite(v);
		r.e.setSampleRate(22050.f);
		x = r.run(9600);
		for (float v : x)
			finite &= std::isfinite(v) && std::fabs(v) < 20.f;
		CHECK(finite, "a NaN or a rate change gave a non-finite output");
	}
	{
		// CPU: 16 voices, every bar full, notes from C1 up (most harmonics under Nyquist), 10 s at 48 kHz
		Rig r;
		for (int i = 0; i < kBars; i++)
			r.bars.v[0][i] = 1.f;
		r.io.voctChannels = 16;
		for (int c = 0; c < 16; c++)
			r.voct[c] = -3.f + c / 12.f;
		r.io.spread = 0.5f;
		r.run(4800);
		const int n = 480000;
		float sink = 0.f;
		const auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < n; i++) {
			r.e.process(r.io);
			sink += r.io.left;
		}
		const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		std::printf("16 voices x 64 harmonics: %.1f %% of one core at 48 kHz (%.0f ns per sample) [%g]\n", 100.0 * s / 10.0,
			1e9 * s / n, sink * 0.f);
		Rig one;
		for (int i = 0; i < kBars; i++)
			one.bars.v[0][i] = 1.f;
		one.voct[0] = -3.f;
		one.run(4800);
		const auto t1 = std::chrono::steady_clock::now();
		for (int i = 0; i < n; i++) {
			one.e.process(one.io);
			sink += one.io.left;
		}
		const double s1 = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
		std::printf("1 voice x 64 harmonics: %.2f %% of one core at 48 kHz [%g]\n", 100.0 * s1 / 10.0, sink * 0.f);
	}
	{
		// the drawing: a brush stroke sets the bars it crosses, a wide one moves the neighbours less, a line is exact
		float p[kBars] = {};
		strokeBars(p, 10.f, 0.5f, 20.f, 0.5f, 1.f);
		CHECK(p[9] == 0.f && p[10] == 0.5f && p[20] == 0.5f && p[21] == 0.f, "a 1-bar stroke");
		float q[kBars] = {};
		strokeBars(q, 30.f, 1.f, 30.f, 1.f, 8.f);
		CHECK(q[30] == 1.f && q[28] > 0.f && q[28] < 1.f && q[26] < q[28] && q[40] == 0.f, "a wide dab");
		float l[kBars] = {};
		lineBars(l, 0.f, 0.f, 63.f, 1.f);
		CHECK(l[0] == 0.f && l[63] == 1.f && std::fabs(l[21] - 21.f / 63.f) < 1e-6f, "a line");
	}
	std::printf(failures ? "%d failures\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}
