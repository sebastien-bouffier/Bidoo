// zOù MAï / enCORE: pulse timing (Fit, detached ratchets), the musical knob table and the
// 32 -> 96 tick conversion of enCORE patches. Builds against the Rack SDK (MSYS2 MinGW64, after
// a plugin build for quantizer.cpp.o) and runs next to a copy of the DLLs of an installed Rack:
//   g++ -std=c++17 -O1 -Isrc/dep -I<SDK>/include -I<SDK>/dep/include -D_USE_MATH_DEFINES
//       tools/tests/zouseq_pulses_test.cpp build/src/dep/quantizer.cpp.o -L<SDK> -lRack -static-libstdc++
//       -o <dir>/zouseq_pulses_test.exe
#include "../../src/ZouSeq.hpp"

Plugin *pluginInstance = nullptr;
Model *modelZOUMAIExpander = nullptr;
Model *modelENCOREExpander = nullptr;
// the theme part of BidooModule, not under test
json_t *BidooModule::dataToJson() {return json_object();}
void BidooModule::dataFromJson(json_t *) {}

struct TestEncore {
	static constexpr bool INTERPOLATE = false;
	static constexpr int TICKS = 96;
	static constexpr int UNITS_PER_CLOCK = 3;
	static constexpr int KNOB_PER_STEP = 32;
	static constexpr int MAX_SPEED = 8;
	static constexpr int MIN_LENGTH = 3, MAX_LENGTH = 6144, DEFAULT_LENGTH = 45, SHORTEST_NOTE = 3;
	static constexpr int MAX_DISTANCE = 96, DEFAULT_DISTANCE = 3;
	static constexpr int MAX_TRIM = 95;
	static constexpr int OLD_TICKS = 32;
	static constexpr float PREVIEW_LENGTH = 0.5f, PREVIEW_DISTANCE = 0.03f;
	static constexpr const char *QUANTIZED_LABEL = "Qnt.";
	static constexpr const char *PANEL = "res/ENCORE.svg";
	static Model *expanderModel() {return nullptr;}
};
using E = ZouSeq<TestEncore>;

static int failures = 0;
#define CHECK(c) do { if (!(c)) { printf("FAILED line %d: %s\n", __LINE__, #c); failures++; } } while (0)

// the gate of track 0, trig 0 (at step 0) with the head at t ticks
static float gateAt(E &m, int t) {
	m.trackHead[m.currentPattern][0] = t;
	return m.trackGetGate(0, 0);
}

int main() {
	E *m = new E();
	const int p = m->currentPattern;
	m->nTrigsAttibutes[p][0][0].setTrigActive(true);

	// one pulse: the gate lasts the length
	m->trigLength[p][0][0] = 48;
	m->nTrigsAttibutes[p][0][0].setTrigPulseCount(1);
	CHECK(gateAt(*m, 0) > 0.f && gateAt(*m, 47) > 0.f && gateAt(*m, 48) == 0.f);

	// 4 pulses 1/4 step apart, length 0.9 step: each gate is half the gap (12), so the hits stay apart
	m->trigLength[p][0][0] = 86;
	m->nTrigsAttibutes[p][0][0].setTrigPulseCount(4);
	m->trigPulseDistance[p][0][0] = 24;
	for (int k = 0; k < 4; k++) {
		CHECK(gateAt(*m, k * 24) > 0.f);
		CHECK(gateAt(*m, k * 24 + 11) > 0.f);
		CHECK(gateAt(*m, k * 24 + 12) == 0.f);
	}
	CHECK(gateAt(*m, 4 * 24) == 0.f);
	CHECK(m->trigGetFullLength(0, 0) == 3 * 24 + 12);

	// a short length stays as it is
	m->trigLength[p][0][0] = 6;
	CHECK(gateAt(*m, 24 + 5) > 0.f && gateAt(*m, 24 + 6) == 0.f);

	// Fit: 3 pulses over one step = triplets, exact on 96 ticks
	m->trigLength[p][0][0] = 96;
	m->nTrigsAttibutes[p][0][0].setTrigPulseCount(3);
	m->trigPulseDistance[p][0][0] = 0;
	CHECK(m->trigGap(0, 0) == 32);
	for (int k = 0; k < 3; k++) {
		CHECK(gateAt(*m, k * 32) > 0.f);
		CHECK(gateAt(*m, k * 32 + 15) > 0.f);
		CHECK(gateAt(*m, k * 32 + 16) == 0.f);
	}
	CHECK(m->trigPulseAt(0, 0) == 2);  // head left at 80

	// Fit with a distance ramp goes through the pulse table, same first gap
	m->trigDistRamp[p][0][0] = 5000;
	CHECK(m->trigHasPulseTable(0, 0));
	CHECK(gateAt(*m, 0) > 0.f && gateAt(*m, 15) > 0.f && gateAt(*m, 16) == 0.f);
	CHECK(gateAt(*m, 32) > 0.f);   // second pulse at the first gap
	CHECK(gateAt(*m, 48) > 0.f);   // third at half the gap later
	m->trigDistRamp[p][0][0] = 10000;

	// the musical table: exact, rising, Fit first for the distance
	const std::vector<int> &len = E::timeTable(false), &dist = E::timeTable(true);
	CHECK(dist[0] == 0 && len[0] == 3);
	for (size_t i = 1; i < len.size(); i++)
		CHECK(len[i] > len[i - 1]);
	CHECK(std::find(len.begin(), len.end(), 32) != len.end());   // 1/3 step
	CHECK(std::find(dist.begin(), dist.end(), 16) != dist.end()); // 1/6 step
	CHECK(dist.back() == 96);
	CHECK(E::timeName(32, false) == "1/3" && E::timeName(18, false) == "3/16" && E::timeName(0, true) == "Fit" && E::timeName(192, false) == "2");
	for (int v : len)
		CHECK(m->timeFromKnob(m->timeToKnob(v, false), false) == v);
	for (int v : dist)
		CHECK(m->timeFromKnob(m->timeToKnob(v, true), true) == v);

	// an old enCORE patch (32 ticks a step) loads scaled by 3
	{
		json_t *root = m->dataToJson();
		json_object_del(root, "ticksPerStep");
		json_t *trigJ = json_object();
		json_object_set_new(trigJ, "isActive", json_true());
		json_object_set_new(trigJ, "length", json_integer(15));
		json_object_set_new(trigJ, "pulseDistance", json_integer(8));
		json_object_set_new(trigJ, "trim", json_integer(-5));
		json_t *trackJ = json_object();
		json_object_set_new(trackJ, "trig0", trigJ);
		json_t *patternJ = json_object();
		json_object_set_new(patternJ, "track0", trackJ);
		json_object_set_new(root, "pattern0", patternJ);
		E *o = new E();
		o->dataFromJson(root);
		printf("old patch: length %d distance %d trim %d\n", o->trigLength[0][0][0], o->trigPulseDistance[0][0][0], o->trigTrim[0][0][0]);
		CHECK(o->trigLength[0][0][0] == 45 && o->trigPulseDistance[0][0][0] == 24 && o->trigTrim[0][0][0] == -15);
		json_decref(root);
		delete o;
	}

	delete m;
	printf("%s (%d failures)\n", failures ? "FAILED" : "ok", failures);
	return failures ? 1 : 0;
}
