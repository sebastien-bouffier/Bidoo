// pARtiCuLeS's engine without Rack: envelopes, exact timing on a clock and free running, grains that never cross the
// write head, a new buffer taken without a click, the shimmer that stays bounded, and the cost.
//   g++ -std=c++17 -O2 -I src -I src/dep/bidoo-dsp tools/tests/particules_engine_test.cpp -o particules_test && ./particules_test
#include "ParticulesEngine.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace particules;

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static const double SR = 48000.0;

// Runs the engine for `seconds` on a sine (103 Hz, 1 = 5 V), clock pulses at 120 BPM as positions, and returns the
// output; counts the grains started.
struct Run {
	Engine e;
	Controls c;
	std::vector<float> outL, outR;
	std::vector<int64_t> starts;
	int64_t frame = 0;
	double bpm = 120.0;
	float amp = 1.f;
	std::vector<int64_t> trigAt;
	Run() {
		e.prepare(SR);
		e.seed(7);
	}
	void go(double seconds) {
		const int blocks = (int)(seconds * SR / kBlock);
		float inL[kBlock], inR[kBlock], oL[kBlock], oR[kBlock], env[kBlock];
		double ppq[kBlock];
		bool trig[kBlock] = {}, started[kBlock];
		float voct[1] = {0.f};
		for (int b = 0; b < blocks; b++) {
			for (int i = 0; i < kBlock; i++) {
				const int64_t f = frame + i;
				inL[i] = inR[i] = amp * (float)std::sin(2.0 * M_PI * 103.0 * (double)f / SR);
				ppq[i] = (double)f * bpm / 60.0 / SR;
				trig[i] = std::find(trigAt.begin(), trigAt.end(), f) != trigAt.end();
			}
			e.process(inL, inR, ppq, true, bpm, trig, voct, 1, c, oL, oR, started, env);
			for (int i = 0; i < kBlock; i++) {
				outL.push_back(oL[i]);
				outR.push_back(oR[i]);
				if (started[i])
					starts.push_back(frame + i);
			}
			frame += kBlock;
		}
	}
	// the largest second difference from sample `from` on (5 V scale)
	float worstJump(size_t from = 0) const {
		float m = 0.f;
		for (size_t n = std::max<size_t>(from, 2); n < outL.size(); n++) {
			m = std::max(m, 5.f * std::fabs(outL[n] - 2.f * outL[n - 1] + outL[n - 2]));
			m = std::max(m, 5.f * std::fabs(outR[n] - 2.f * outR[n - 1] + outR[n - 2]));
		}
		return m;
	}
	float peak(size_t from, size_t to) const {
		float m = 0.f;
		for (size_t n = from; n < std::min(to, outL.size()); n++)
			m = std::max(m, std::max(std::fabs(outL[n]), std::fabs(outR[n])));
		return m;
	}
};

static void envelopes() {
	Engine e;
	for (float a : {0.01f, 0.5f, 0.99f})
		for (float s : {0.f, 0.5f, 1.f}) {
			CHECK(e.envelope(0.f, a, s) == 0.f && e.envelope(1.f, a, s) == 0.f, "ends at 0 (attack %.2f soft %.1f)", a, s);
			float peak = 0.f;
			for (int i = 1; i < 1000; i++)
				peak = std::max(peak, e.envelope(i / 1000.f, a, s));
			CHECK(peak > 0.9f && peak <= 1.0001f, "peak %.3f (attack %.2f soft %.1f)", peak, a, s);
		}
	// percussive: full level within the first few percent, then falling
	CHECK(e.envelope(0.02f, 0.01f, 0.f) > 0.9f, "percussive attack");
	CHECK(e.envelope(0.5f, 0.01f, 0.f) < 0.1f, "percussive decay is sharp");
}

static void clockTiming() {
	Run r;
	r.c.clockMode = true;
	r.c.division = 13;          // 1/16
	r.c.chance = 1.f;
	r.c.count = 1;
	r.c.size = 0.02f;
	r.go(0.5);                  // half a second of sound in the buffer first
	r.starts.clear();
	const int64_t from = r.frame;
	r.go(4.0);                  // two bars at 120 BPM: 32 sixteenths
	CHECK(r.starts.size() == 32, "32 sixteenths in two bars, got %zu", r.starts.size());
	// each on the first sample at or after its sixteenth (6000 samples apart)
	bool exact = true;
	for (int64_t s : r.starts) {
		const int64_t rel = s % 6000;
		exact = exact && (rel == 0);
	}
	CHECK(exact, "every grain on its sixteenth's sample (first at %lld)", r.starts.empty() ? -1LL : (long long)(r.starts[0] - from));
	// triplets: 1/8T = 1/3 beat, 12 in two beats
	Run t;
	t.c.clockMode = true;
	t.c.division = 11;
	t.c.size = 0.02f;
	t.go(0.5);
	t.starts.clear();
	t.go(1.0);
	CHECK(t.starts.size() == 6, "six 1/8T in a second at 120 BPM, got %zu", t.starts.size());
	// COUNT starts several at once, CHANCE 0 none
	Run k;
	k.c.clockMode = true;
	k.c.division = 7;           // 1/4
	k.c.count = 3;
	k.c.size = 0.05f;
	k.go(0.5);
	k.starts.clear();
	k.go(1.0);
	CHECK(k.starts.size() == 2, "two quarters a second, got %zu", k.starts.size());
	Run z;
	z.c.clockMode = true;
	z.c.chance = 0.f;
	z.go(1.0);
	CHECK(z.starts.empty(), "CHANCE 0 starts nothing, got %zu", z.starts.size());
}

// RATE at 0: nothing on its own, COUNT grains on each TRIG, on that very sample
static void trigOnly() {
	Run r;
	r.c.rate = 0.f;
	r.c.count = 2;
	r.c.size = 0.05f;
	r.go(1.0);
	CHECK(r.starts.empty(), "RATE 0 starts nothing by itself, got %zu", r.starts.size());
	r.trigAt = {r.frame + 1000, r.frame + 30017};
	r.go(1.0);
	CHECK(r.starts.size() == 2 && r.starts[0] == r.trigAt[0] && r.starts[1] == r.trigAt[1], "a grain on each TRIG's sample (%zu)", r.starts.size());
	CHECK(r.e.active.load() == 0, "they ended");
}

static void freeTiming() {
	Run r;
	r.c.rate = 10.f;
	r.c.chaos = 0.f;
	r.c.size = 0.02f;
	r.go(0.5);
	r.starts.clear();
	r.go(2.0);
	CHECK(r.starts.size() == 20, "10 a second for two seconds, got %zu", r.starts.size());
	bool regular = true;
	for (size_t i = 1; i < r.starts.size(); i++)
		regular = regular && (r.starts[i] - r.starts[i - 1] == 4800);
	CHECK(regular, "CHAOS 0: every 4800 samples");
	Run c;
	c.c.rate = 10.f;
	c.c.chaos = 1.f;
	c.c.size = 0.02f;
	c.go(0.5);
	c.starts.clear();
	c.go(20.0);
	CHECK(c.starts.size() > 160 && c.starts.size() < 240, "CHAOS 1: about 10 a second on average, got %zu in 20 s", c.starts.size());
}

// No click with smooth grains anywhere: forward, reverse, ping-pong, pitched up and down, at the newest sound while it
// records (a grain crossing the write head would read the old sound against the new)
static void noClicks() {
	for (int dir = 0; dir < 4; dir++)
		for (float tune : {-12.f, 0.f, 12.f, 24.f}) {
			Run r;
			r.c.direction = dir;
			r.c.tune = tune;
			r.c.position = 1.f;     // the newest sound
			r.c.spray = 0.3f;
			r.c.size = 0.25f;
			r.c.rate = 30.f;
			r.c.chaos = 0.5f;
			r.c.shape = 0.5f;
			r.c.soft = 1.f;
			r.c.mix = 0.f;
			r.go(1.0);
			const size_t from = r.outL.size();
			r.go(3.0);
			const float j = r.worstJump(from);
			// a 103 Hz sine an octave or two up: its own second difference, times the overlap, is about 0.01 to 0.3 V
			const float limit = 0.12f * std::pow(2.f, std::max(0.f, tune) / 12.f) * std::pow(2.f, std::max(0.f, tune) / 12.f) + 0.08f;
			CHECK(j < limit, "direction %d tune %+.0f: worst jump %.3f V (limit %.3f)", dir, tune, j, limit);
		}
}

// A new buffer (a file loaded) while grains play: they fade out from the old one, no click; the old one comes back
static void bufferHandoff() {
	Run r;
	r.c.size = 0.3f;
	r.c.rate = 20.f;
	r.c.soft = 1.f;
	r.c.mix = 0.f;
	r.c.record = false;
	r.go(1.5);
	const size_t from = r.outL.size();
	Buffer *nb = new Buffer;
	std::vector<float> l((size_t)SR), rr((size_t)SR);
	for (size_t i = 0; i < l.size(); i++)
		l[i] = rr[i] = 0.8f * (float)std::sin(2.0 * M_PI * 307.0 * (double)i / SR);
	nb->fill(l.data(), rr.data(), (int64_t)l.size(), SR);
	r.e.pending.store(nb);
	r.go(1.0);
	CHECK(r.e.buffer() == nb, "the new buffer is the one read");
	Buffer *old = r.e.retired.exchange(nullptr);
	CHECK(old != nullptr, "the old buffer came back to be freed");
	delete old;
	const float j = r.worstJump(from);
	CHECK(j < 0.3f, "no click when the buffer changes: worst jump %.3f V", j);
}

static float burstPeak(float shimmer) {
	Run r;
	r.c.shimmer = shimmer;
	r.c.decay = 30.f;
	r.c.mix = 1.f;
	r.c.rate = 40.f;
	r.c.size = 0.1f;
	r.go(8.0);
	return r.peak(0, r.outL.size());
}

// Shimmer at the top, the longest decay: a burst, then silence for 20 s: the tail must not grow
static void shimmerBounded() {
	for (int iv = 0; iv < kNumIntervals; iv++) {
		Run r;
		r.c.shimmer = 1.f;
		r.c.interval = iv;
		r.c.decay = 30.f;
		r.c.mix = 1.f;
		r.c.rate = 40.f;
		r.c.size = 0.1f;
		r.go(2.0);
		r.c.rate = 0.05f;
		r.c.record = false;
		r.amp = 0.f;
		r.go(14.0);
		const size_t s = (size_t)SR;
		const float early = r.peak(3 * s, 6 * s), late = r.peak(12 * s, 16 * s);
		CHECK(std::isfinite(late) && late <= early * 1.5f + 1e-3f, "interval %s: the tail does not grow: %.3f then %.3f", kIntervalNames[iv], early, late);
	}
	Run r;
	r.c.shimmer = 1.f;
	r.c.decay = 30.f;
	r.c.mix = 1.f;
	r.c.rate = 40.f;
	r.c.size = 0.1f;
	r.go(2.0);
	r.c.rate = 0.05f;           // nearly no grains
	r.c.record = false;
	r.amp = 0.f;
	r.go(20.0);
	bool finite = true;
	for (float v : r.outL)
		finite = finite && std::isfinite(v);
	CHECK(finite, "finite output");
	const size_t s = (size_t)SR;
	const float early = r.peak(3 * s, 6 * s), late = r.peak(18 * s, 22 * s);
	CHECK(late <= early * 1.5f + 1e-3f, "the shimmer tail does not grow: %.3f then %.3f", early, late);
	const float with = burstPeak(1.f), without = burstPeak(0.f);
	CHECK(with < 1.6f * without, "the shimmer adds at most 60 %% to the reverb's peak: %.3f against %.3f", with, without);
}

static void cost() {
	Run r;
	r.c.rate = 200.f;
	r.c.size = 1.f;             // 64 grains at once
	r.c.shimmer = 0.5f;
	r.c.mix = 0.5f;
	r.go(1.0);
	const auto t0 = std::chrono::steady_clock::now();
	r.go(5.0);
	const double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	std::printf("64 grains + reverb + shimmer: %.2f %% of a core\n", 100.0 * s / 5.0);
	for (int part = 0; part < 2; part++) {
		Run q;
		q.c.rate = part == 0 ? 200.f : 0.05f;
		q.c.size = part == 0 ? 1.f : 0.01f;
		q.c.shimmer = part == 0 ? 0.f : 0.5f;
		q.go(1.0);
		const auto u0 = std::chrono::steady_clock::now();
		q.go(5.0);
		const double us = std::chrono::duration<double>(std::chrono::steady_clock::now() - u0).count();
		std::printf("  %s: %.2f %%\n", part == 0 ? "64 grains + reverb, no shimmer" : "no grains, reverb + shimmer", 100.0 * us / 5.0);
	}
	CHECK(r.e.active.load() >= 60, "about 64 grains at once, got %d", r.e.active.load());
}

int main() {
	envelopes();
	clockTiming();
	trigOnly();
	freeTiming();
	noClicks();
	bufferHandoff();
	shimmerBounded();
	cost();
	std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}
