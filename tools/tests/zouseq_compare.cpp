// zOu MAi: sample-exact comparison of the engine against an older one (a baseline commit), in Step mode,
// and checks of the Fast clock mode.
// Both modules run side by side on the same random patches and the same clock, reset, RUN, pattern,
// fill and track-reset events, each with its own copy of Rack's random generator; every output
// (gate, V/O, CV1, CV2 of the 8 tracks and the 4 polyphonic ones) must be equal on every sample.
//
// Build (MSYS2 MinGW64, after a plugin build for quantizer.cpp.o), from the plugin's folder:
//   mkdir -p <dir>/base && git show <commit>:src/ZouSeq.hpp | sed '/^#include/d;/^#pragma once/d' > <dir>/base/ZouSeq_base.hpp
//   g++ -std=c++17 -O2 -Isrc/dep -I<SDK>/include -I<SDK>/dep/include -I<dir>/base -D_USE_MATH_DEFINES
//       -march=nehalem -funsafe-math-optimizations tools/tests/zouseq_compare.cpp build/src/dep/quantizer.cpp.o
//       -L<SDK> -lRack -static-libstdc++ -o <dir>/zouseq_compare.exe
// and run it next to a copy of the DLLs of an installed Rack: zouseq_compare.exe [seeds] [seconds]
// (the plugin's own flags, so both engines are compiled as Rack runs them).
#include "../../src/ZouSeq.hpp"

// the baseline's header, its #include and #pragma lines taken out (the same files, included above)
namespace base {
#include "ZouSeq_base.hpp"
}

Plugin *pluginInstance = nullptr;
Model *modelZOUMAIExpander = nullptr;
json_t *BidooModule::dataToJson() {return json_object();}
void BidooModule::dataFromJson(json_t *) {}

// The traits of the baseline (Bidoo 2.7.7, src/ZOUMAI.cpp)
namespace base {
struct ZouMaiTraits {
	static constexpr bool INTERPOLATE = true;
	static constexpr int TICKS = 1008000;
	static constexpr int UNITS_PER_CLOCK = TICKS;
	static constexpr int KNOB_PER_STEP = 1;
	static constexpr int MAX_SPEED = 4;
	static constexpr int MIN_LENGTH = 0, MAX_LENGTH = 64 * TICKS, DEFAULT_LENGTH = TICKS / 10 * 9, SHORTEST_NOTE = TICKS / 100;
	static constexpr int MAX_DISTANCE = 64 * TICKS, DEFAULT_DISTANCE = TICKS / 2;
	static constexpr int MAX_TRIM = TICKS;
	static constexpr int OLD_TICKS = TICKS;
	static constexpr float PREVIEW_LENGTH = 0.99f, PREVIEW_DISTANCE = 1.0f;
	static constexpr const char *QUANTIZED_LABEL = "Quant";
	static constexpr const char *PANEL = "res/ZOUMAI.svg";
	static Model *expanderModel() {return nullptr;}
};
}

// The traits now (src/ZouTraits.hpp), with no expander
#include "../../src/ZouTraits.hpp"
struct NewZouMai : ZouMaiTraits {static Model *expanderModel() {return nullptr;}};

using BaseZ = base::ZouSeq<base::ZouMaiTraits>;
using NewZ = ZouSeq<NewZouMai>;

// ---- random patches, as a patch file has them

static std::mt19937_64 gen;
static long long gateEdges = 0;   // rising gates seen, so that a run is known to have played
static double uni() {return std::uniform_real_distribution<double>(0.0, 1.0)(gen);}
static int irand(int lo, int hi) {return std::uniform_int_distribution<int>(lo, hi)(gen);}

// a time in the patch's format: steps as reals
static json_t *timeJ(double steps) {
	return json_real(steps);
}

static double musical(bool fine) {
	static const double f[] = {1/32., 1/24., 1/16., 1/12., 3/32., 1/8., 1/6., 3/16., 1/4., 1/3., 3/8., 1/2., 2/3., 3/4., 7/8., 1., 4/3., 3/2., 2.};
	if (fine && uni() < 0.3)
		return 0.01 + uni() * 2.0;   // off the musical grid
	return f[irand(0, 18)];
}

static json_t *makePatch() {
	json_t *root = json_object();
	json_object_set_new(root, "currentPattern", json_integer(0));
	json_object_set_new(root, "ticksPerStep", json_integer(1008000));
	static const double speeds[] = {0.125, 0.25, 0.5, 0.75, 1, 1.5, 2, 3, 4, 5, 16.0 / 3.0, 6, 7, 8};
	const int nSpeeds = 9;   // the baseline zOu MAi stops at x4
	const int patterns = irand(1, 3);
	for (int p = 0; p < patterns; p++) {
		json_t *patternJ = json_object();
		for (int t = 0; t < 8; t++) {
			json_t *trackJ = json_object();
			if (uni() < 0.1)
				json_object_set_new(trackJ, "isActive", json_false());
			if (uni() < 0.5)
				json_object_set_new(trackJ, "speed", json_real(speeds[irand(0, nSpeeds - 1)]));
			if (uni() < 0.4)
				json_object_set_new(trackJ, "readMode", json_integer(irand(0, 4)));
			const int length = uni() < 0.5 ? 16 : irand(1, 64);
			json_object_set_new(trackJ, "length", json_integer(length));
			if (uni() < 0.4) {
				json_object_set_new(trackJ, "rootNote", json_integer(irand(0, 11)));
				json_object_set_new(trackJ, "scale", json_integer(irand(0, 10)));
			}
			if (uni() < 0.2)
				json_object_set_new(trackJ, "quantizeCV1", json_integer(1));
			json_object_set_new(trackJ, "slideMode", json_boolean(uni() < 0.5));
			if (uni() < 0.3)
				json_object_set_new(trackJ, "cvHold", json_true());
			if (uni() < 0.3)
				json_object_set_new(trackJ, "swing", json_real(50.0 + uni() * 25.0));
			const int trigs = irand(0, std::min(length, 24));
			for (int n = 0; n < trigs; n++) {
				const int k = irand(0, length - 1);
				json_t *trigJ = json_object();
				json_object_set_new(trigJ, "isActive", json_boolean(uni() < 0.85));
				if (uni() < 0.2)
					json_object_set_new(trigJ, "slide", json_real(uni()));
				if (uni() < 0.2)
					json_object_set_new(trigJ, "trigSlideType", json_true());
				if (uni() < 0.3)
					json_object_set_new(trigJ, "trim", timeJ(uni() * 2.0 - 1.0));
				json_object_set_new(trigJ, "length", timeJ(musical(true)));
				if (uni() < 0.4) {
					json_object_set_new(trigJ, "pulseCount", json_integer(irand(2, uni() < 0.8 ? 8 : 64)));
					double dist = uni() < 0.3 ? 0.0 : std::min(musical(true), 1.0);
					json_object_set_new(trigJ, "pulseDistance", timeJ(dist));
					if (uni() < 0.3)
						json_object_set_new(trigJ, "distRamp", json_real(25.0 + uni() * 375.0));
					if (uni() < 0.3)
						json_object_set_new(trigJ, "pitchRamp", json_integer(irand(-12, 12)));
					if (uni() < 0.3)
						json_object_set_new(trigJ, "CV1End", json_real(uni() * 10.0));
				}
				if (uni() < 0.5) {
					json_object_set_new(trigJ, "proba", json_integer(irand(0, 9)));
					json_object_set_new(trigJ, "count", json_integer(irand(1, 100)));
					json_object_set_new(trigJ, "countReset", json_integer(irand(1, 8)));
				}
				json_object_set_new(trigJ, "octave", json_integer(irand(-2, 2)));
				json_object_set_new(trigJ, "semitones", json_integer(irand(0, 11)));
				json_object_set_new(trigJ, "CV1", json_real(uni() * 10.0 - 5.0));
				json_object_set_new(trigJ, "CV2", json_real(uni() * 10.0));
				json_object_set_new(trackJ, ("trig" + std::to_string(k)).c_str(), trigJ);
			}
			json_object_set_new(patternJ, ("track" + std::to_string(t)).c_str(), trackJ);
		}
		json_object_set_new(root, ("pattern" + std::to_string(p)).c_str(), patternJ);
	}
	return root;
}

// ---- the events: one list, played into both modules

struct Event {
	long long at;
	int kind;      // 0 RUN, 1 reset, 2 pattern CV, 3 fill gate, 4 track reset
	float value;
	int track;
};

template <typename M>
static void setInputs(M &m, const std::vector<Event> &events, size_t &next, long long i, bool clockHigh, float &patternV,
		float &fillV, int &runPulse, int &resetPulse, int (&trackPulse)[8]) {
	while (next < events.size() && events[next].at == i) {
		const Event &e = events[next++];
		switch (e.kind) {
			case 0: runPulse = 20; break;
			case 1: resetPulse = 20; break;
			case 2: patternV = e.value; break;
			case 3: fillV = e.value; break;
			case 4: trackPulse[e.track] = 20; break;
		}
	}
	m.inputs[M::EXTCLOCK_INPUT].setVoltage(clockHigh ? 10.f : 0.f);
	m.inputs[M::RUN_INPUT].setVoltage(runPulse > 0 ? 10.f : 0.f);
	m.inputs[M::RESET_INPUT].setVoltage(resetPulse > 0 ? 10.f : 0.f);
	m.inputs[M::PATTERN_INPUT].setVoltage(patternV);
	m.inputs[M::FILL_INPUT].setVoltage(fillV);
	for (int t = 0; t < 8; t++)
		m.inputs[M::TRACKRESET_INPUTS + t].setVoltage(trackPulse[t] > 0 ? 10.f : 0.f);
}

template <typename A, typename B>
static bool compare(uint64_t seed, double seconds) {
	gen.seed(seed);
	const float sampleRates[] = {44100.f, 48000.f, 96000.f};
	const float sr = sampleRates[irand(0, 2)];
	json_t *patch = makePatch();
	A *a = new A();
	B *b = new B();
	a->dataFromJson(patch);
	b->dataFromJson(patch);
	json_decref(patch);

	// track reset inputs: some patched
	bool trackResetPatched[8];
	for (int t = 0; t < 8; t++) {
		trackResetPatched[t] = uni() < 0.15;
		if (trackResetPatched[t]) {
			a->inputs[A::TRACKRESET_INPUTS + t].setChannels(1);
			b->inputs[B::TRACKRESET_INPUTS + t].setChannels(1);
		}
	}
	const bool patternPatched = uni() < 0.6;
	if (patternPatched) {
		a->inputs[A::PATTERN_INPUT].setChannels(1);
		b->inputs[B::PATTERN_INPUT].setChannels(1);
	}

	// the clock: a period per clock that may drift, jump or jitter
	const double step = sr * (0.08 + uni() * 0.3);               // samples a step
	const double basePeriod = step;
	const int mode = irand(0, 3);                                // steady, drift, random, jitter
	const long long total = (long long)(seconds * sr);
	std::vector<long long> clocks;
	double t = irand(0, 1000);
	int c = 0;
	while (t < total) {
		clocks.push_back((long long)t);
		double period = basePeriod;
		if (mode == 1)
			period *= 1.0 + 0.4 * std::sin(c * 0.01);
		else if (mode == 2)
			period *= 0.6 + uni() * 0.8;
		else if (mode == 3)
			period += irand(-3, 3);
		t += std::max(period, 2.0);
		c++;
	}
	const int width = std::max(1, std::min((int)(basePeriod / 2), irand(1, 200)));

	std::vector<Event> events;
	events.push_back({irand(0, 3000), 0, 0.f, 0});   // RUN, maybe between two clocks
	const int nEvents = irand(0, 40);
	for (int n = 0; n < nEvents; n++) {
		Event e {irand(100, (int)total - 1), irand(0, 4), 0.f, 0};
		if (e.kind == 0 && uni() < 0.7)
			e.kind = 1;
		if (e.kind == 2)
			e.value = (float)(irand(0, 3) * 0.625 + 0.1);
		if (e.kind == 3)
			e.value = uni() < 0.5 ? 10.f : 0.f;
		if (e.kind == 4)
			e.track = irand(0, 7);
		// sometimes on a clock, or just after one
		if (uni() < 0.3 && !clocks.empty()) {
			const long long k = clocks[irand(0, (int)clocks.size() - 1)];
			e.at = k + irand(0, 60);
		}
		events.push_back(e);
	}
	std::sort(events.begin(), events.end(), [](const Event &x, const Event &y) {return x.at < y.at;});

	random::Xoroshiro128Plus rngA, rngB;
	rngA.seed(seed * 7 + 1, seed + 3);
	rngB = rngA;
	Module::ProcessArgs args;
	args.sampleRate = sr;
	args.sampleTime = 1.f / sr;
	size_t nextA = 0, nextB = 0, clockIdx = 0;
	float patA = 0.f, patB = 0.f, fillA = 0.f, fillB = 0.f;
	int runA = 0, runB = 0, resetA = 0, resetB = 0;
	int trackA[8] = {}, trackB[8] = {};
	bool ok = true;
	float lastGate[8] = {};
	for (long long i = 0; i < total && ok; i++) {
		while (clockIdx + 1 < clocks.size() && clocks[clockIdx + 1] <= i)
			clockIdx++;
		const bool high = !clocks.empty() && i >= clocks[clockIdx] && i < clocks[clockIdx] + width;
		args.frame = i;
		setInputs(*a, events, nextA, i, high, patA, fillA, runA, resetA, trackA);
		setInputs(*b, events, nextB, i, high, patB, fillB, runB, resetB, trackB);
		random::local() = rngA;
		a->process(args);
		rngA = random::local();
		random::local() = rngB;
		b->process(args);
		rngB = random::local();
		if (runA > 0) {runA--; runB--;}
		if (resetA > 0) {resetA--; resetB--;}
		for (int k = 0; k < 8; k++)
			if (trackA[k] > 0) {trackA[k]--; trackB[k]--;}
		for (int k = 0; k < 8; k++) {
			const float g = b->outputs[B::GATE_OUTPUTS + k].getVoltage();
			if (g > 0.f && lastGate[k] <= 0.f)
				gateEdges++;
			lastGate[k] = g;
		}
		for (int o = 0; o < A::NUM_OUTPUTS; o++) {
			const int ch = std::max(1, a->outputs[o].getChannels());
			for (int ci = 0; ci < ch; ci++) {
				const float x = a->outputs[o].getVoltage(ci), y = b->outputs[o].getVoltage(ci);
				if (std::memcmp(&x, &y, sizeof(float)) != 0 || a->outputs[o].getChannels() != b->outputs[o].getChannels()) {
					printf("seed %llu (%s, %g Hz, clock mode %d): output %d channel %d differs at sample %lld: %g / %g\n",
						(unsigned long long)seed, "zOu MAi", sr, mode, o, ci, i, x, y);
					ok = false;
					break;
				}
			}
			if (!ok)
				break;
		}
	}
	delete a;
	delete b;
	return ok;
}

// ---- zOu MAi's Fast clock mode: the head moves on clocks only, 32 a step (6 at x16/3), and a change of
// mode keeps every track where it is
static int fastFailures = 0;
#define FCHECK(c) do { if (!(c)) { printf("FAST CLOCK FAILED line %d: %s\n", __LINE__, #c); fastFailures++; } } while (0)

static void fastClockChecks() {
	Module::ProcessArgs args;
	args.sampleRate = 48000.f;
	args.sampleTime = 1.f / 48000.f;
	auto run = [&](NewZ &m, int samples, int period, long long &frame, std::vector<long long> *heads, std::vector<bool> *clocked) {
		for (int i = 0; i < samples; i++, frame++) {
			const bool high = frame % period < 10;
			m.inputs[NewZ::EXTCLOCK_INPUT].setVoltage(high ? 10.f : 0.f);
			m.inputs[NewZ::RUN_INPUT].setVoltage(frame >= 20 && frame < 30 ? 10.f : 0.f);
			args.frame = frame;
			m.process(args);
			if (heads) {
				heads->push_back(m.trackHead[m.currentPattern][0]);
				clocked->push_back(frame % period == 0);
			}
		}
	};
	{
		NewZ *m = new NewZ();
		m->fastClock = true;
		FCHECK(m->clocksPerStep() == 32 && !m->interpolating());
		long long frame = 0;   // RUN at 20, before the second clock
		std::vector<long long> heads;
		std::vector<bool> clocked;
		run(*m, 100 * 40, 100, frame, &heads, &clocked);
		bool onlyOnClocks = true;
		for (size_t i = 1; i < heads.size(); i++)
			if (heads[i] != heads[i - 1] && !clocked[i])
				onlyOnClocks = false;
		FCHECK(onlyOnClocks);
		// 39 clocks after RUN at x1: 39/32 step
		FCHECK(heads.back() == 39LL * (NewZ::TICKS / 32));
		// x16/3: 6 clocks a step
		m->nTracksAttibutes[m->currentPattern][1].setTrackSpeed(10);
		m->inputs[NewZ::RESET_INPUT].setVoltage(10.f);
		run(*m, 1, 100, frame, nullptr, nullptr);
		m->inputs[NewZ::RESET_INPUT].setVoltage(0.f);
		const long long start = frame;
		while ((frame - start) < 100 * 6 + 50) run(*m, 1, 100, frame, nullptr, nullptr);
		FCHECK(m->trackHead[m->currentPattern][1] % NewZ::TICKS == 0 && m->trackHead[m->currentPattern][1] / NewZ::TICKS >= 1);
		// saved and read back
		json_t *root = m->dataToJson();
		NewZ *o = new NewZ();
		o->dataFromJson(root);
		FCHECK(o->fastClock && o->fastClockApplied);
		json_decref(root);
		delete o;
		delete m;
	}
	{
		// one clock a step, stopped half way between two clocks, then the Fast mode: the head stays
		NewZ *m = new NewZ();
		long long frame = 0;
		run(*m, 4800 * 5 + 2400, 4800, frame, nullptr, nullptr);
		const long long before = m->trackHead[m->currentPattern][0];
		FCHECK(before % NewZ::TICKS != 0);
		m->fastClock = true;
		m->inputs[NewZ::EXTCLOCK_INPUT].setVoltage(0.f);
		m->process(args);
		FCHECK(m->trackHead[m->currentPattern][0] == before);
		// and back: the head still stays
		m->fastClock = false;
		m->process(args);
		FCHECK(m->trackHead[m->currentPattern][0] == before);
		delete m;
	}
}

int main(int argc, char **argv) {
	const int seeds = argc > 1 ? atoi(argv[1]) : 200;
	const double seconds = argc > 2 ? atof(argv[2]) : 20.0;
	int failures = 0;
	long long checked = 0;
	for (int s = 1; s <= seeds; s++) {
		if (!compare<BaseZ, NewZ>((uint64_t)s, seconds))
			failures++;
		checked++;
	}
	printf("%lld runs of %g s, %lld gates played, %d with a difference\n", checked, seconds, gateEdges, failures);
	fastClockChecks();
	printf("Fast clock mode checks: %d failures\n", fastFailures);
	printf("%s\n", failures || fastFailures ? "FAILED" : "ok: every output equal on every sample, Fast clock mode checked");
	return failures || fastFailures ? 1 : 0;
}
