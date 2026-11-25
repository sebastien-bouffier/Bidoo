// Patch data round trips without Rack's engine: SliceSampler's SliceSettings and MetroSeq's MetroPattern (DTROY,
// BORDL) through toJson, a JSON string and fromJson; out-of-range values clamped, missing keys back to defaults.
// Links libRack only for jansson. From the plugin folder, in an MSYS2 MINGW64 shell:
//   g++ -std=c++17 -O1 -D_USE_MATH_DEFINES -I G:/Rack-SDK-2.6.6/include -I G:/Rack-SDK-2.6.6/dep/include -I src/dep -iquote src tools/tests/json_roundtrip_test.cpp -L G:/Rack-SDK-2.6.6 -lRack -o json_roundtrip_test.exe
//   PATH=/g/RackV2:$PATH ./json_roundtrip_test.exe     (libRack.dll from a Rack 2 folder)
#include "plugin.hpp"
#include "BidooComponents.hpp"   // its using namespace std gives MetroSeq.hpp the long long clamp, as in BORDL.cpp
#include "MetroSeq.hpp"
#include "SliceSampler.hpp"
#include <cstdio>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// toJson, to text and back, as a patch file does
static json_t *throughText(json_t *j) {
	char *s = json_dumps(j, 0);
	json_decref(j);
	json_error_t err;
	json_t *back = json_loads(s, 0, &err);
	free(s);
	return back;
}

static void sliceSettings() {
	SliceSettings a;
	a.start = 0.25f; a.len = 0.5f; a.loop = true; a.speed = -1.5f; a.gate = 0; a.filterType = 2; a.q = 0.7f; a.freq = 0.3f; a.kill = 5;
	json_t *j = throughText(a.toJson());
	SliceSettings b;
	b.fromJson(j);
	json_decref(j);
	CHECK(b.start == a.start && b.len == a.len && b.loop == a.loop && b.speed == a.speed && b.gate == a.gate, "slice: basic fields");
	CHECK(b.filterType == a.filterType && b.q == a.q && b.freq == a.freq && b.kill == a.kill, "slice: filter and kill");

	// out of range: clamped
	json_t *bad = json_loads("{\"start\":2,\"len\":-1,\"speed\":9,\"gate\":7,\"filterType\":9,\"q\":0,\"freq\":3,\"kill\":40}", 0, NULL);
	SliceSettings c;
	c.fromJson(bad);
	json_decref(bad);
	CHECK(c.start == 1.f && c.len == 0.f && c.speed == 4.f && c.gate == 1 && c.filterType == 3, "slice: clamps");
	CHECK(c.q == 0.1f && c.freq == 1.f && c.kill == 15, "slice: clamps (q, freq, kill)");

	// missing keys and null: defaults, whatever was there before
	json_t *some = json_loads("{\"len\":0.5}", 0, NULL);
	SliceSettings d = a;
	d.fromJson(some);
	json_decref(some);
	SliceSettings def;
	CHECK(d.len == 0.5f && d.start == def.start && d.loop == def.loop && d.kill == def.kill && d.q == def.q, "slice: missing keys");
	SliceSettings e = a;
	e.fromJson(NULL);
	CHECK(e.start == def.start && e.kill == def.kill && e.filterType == def.filterType, "slice: null");
}

static bool samePattern(const MetroPattern &a, const MetroPattern &b, bool extended) {
	bool ok = a.playMode == b.playMode && a.countMode == b.countMode && a.numberOfStepsParam == b.numberOfStepsParam
		&& a.rootNote == b.rootNote && a.scale == b.scale && a.gateTime == b.gateTime && a.slideTime == b.slideTime
		&& a.sensitivity == b.sensitivity;
	for (int s = 0; s < 8; s++) {
		ok = ok && a.stagePitch[s] == b.stagePitch[s] && a.stagePulses[s] == b.stagePulses[s] && a.stageType[s] == b.stageType[s]
			&& a.stageSlide[s] == b.stageSlide[s] && a.stageSkip[s] == b.stageSkip[s];
		if (extended)
			ok = ok && a.stageGateProb[s] == b.stageGateProb[s] && a.stagePitchRnd[s] == b.stagePitchRnd[s]
				&& a.stageAccent[s] == b.stageAccent[s] && a.stageAccentRnd[s] == b.stageAccentRnd[s];
	}
	return ok;
}

static void metroPattern() {
	MetroPattern a;
	a.playMode = 2; a.countMode = 1; a.numberOfStepsParam = 13; a.rootNote = 9; a.scale = 6;
	a.gateTime = 0.8f; a.slideTime = 0.05f; a.sensitivity = 0.6f;
	for (int s = 0; s < 8; s++) {
		a.stagePitch[s] = -2.f + 0.75f * s;
		a.stagePulses[s] = 1 + s;
		a.stageType[s] = s % 6;
		a.stageSlide[s] = s & 1;
		a.stageSkip[s] = s == 3;
		a.stageGateProb[s] = 0.125f * s;
		a.stagePitchRnd[s] = 0.1f * s;
		a.stageAccent[s] = 1.25f * s;
		a.stageAccentRnd[s] = 0.05f * s;
	}
	a.build(a.numberOfStepsParam);

	// BORDL (extended)
	json_t *j = throughText(a.toJson(true));
	MetroPattern b;
	b.fromJson(j);
	json_decref(j);
	CHECK(samePattern(a, b, true), "metro: extended round trip");

	// DTROY: the extended fields are not saved and come back as defaults
	j = throughText(a.toJson(false));
	MetroPattern c;
	c.fromJson(j);
	json_decref(j);
	MetroPattern def;
	CHECK(samePattern(a, c, false), "metro: plain round trip");
	CHECK(c.stageGateProb[3] == def.stageGateProb[3] && c.stageAccent[5] == def.stageAccent[5], "metro: plain leaves extended at defaults");

	// clamps
	json_t *bad = json_loads("{\"playMode\":9,\"numSteps\":40,\"rootNote\":-3,\"scale\":999,"
		"\"stages\":[{\"pitch\":10,\"pulses\":0,\"type\":8,\"gateProb\":2,\"accent\":50}]}", 0, NULL);
	MetroPattern d;
	d.fromJson(bad);
	json_decref(bad);
	CHECK(d.playMode == 4 && d.numberOfStepsParam == 16 && d.rootNote == 0 && d.scale == quantizer::numScales - 1, "metro: clamps");
	CHECK(d.stagePitch[0] == 6.f && d.stagePulses[0] == 1 && d.stageType[0] == 5 && d.stageGateProb[0] == 1.f && d.stageAccent[0] == 10.f,
		"metro: stage clamps");
	CHECK(d.stagePulses[1] == def.stagePulses[1], "metro: a missing stage keeps defaults");

	// null: a fresh pattern
	MetroPattern e = a;
	e.fromJson(NULL);
	CHECK(samePattern(e, def, true), "metro: null");
}

int main() {
	sliceSettings();
	metroPattern();
	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ok", failures);
	return failures ? 1 : 0;
}
