// rEgIStRE's engine without Rack: exact timing, voltage tables, saving, undo, parts, groups, quantized reset,
// recording and MIDI import.
//   g++ -std=c++17 -O2 -I src tools/tests/registre_engine_test.cpp -o registre_test && ./registre_test
#include "RegistreEngine.hpp"
#include <cstdio>
#include <vector>

using namespace registre;

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// An engine playing `song`, with the UI's hand-over
static void load(Engine &e, const Song &s) {
	e.handoff.writeBuffer() = s;
	e.handoff.writeBuffer().layout();
	e.handoff.publish();
}

static Song oneTrack(int dur, int gate, int n = 4) {
	Song s;
	for (int t = 0; t < kTracks; t++) {
		Track &k = s.tracks[t];
		k.numPatterns = 1;
		k.patternLength[0] = (uint8_t)n;
		for (int i = 0; i < n; i++) {
			k.steps[i].v[DUR] = (uint8_t)dur;
			k.steps[i].v[GATE] = (uint8_t)(t == 0 ? gate : 0);
			k.steps[i].v[CV_A] = (uint8_t)i;
		}
	}
	s.layout();
	return s;
}

// Runs `samples`, a clock every `period`, and records track t's gate rises and falls
struct Run {
	std::vector<int64_t> rises, falls;
	std::vector<float> a;
};
static Run run(Engine &e, int64_t samples, int64_t period, int t = 0, int64_t clockOffset = 0) {
	Run r;
	bool prev = false;
	for (int64_t n = 0; n < samples; n++) {
		Engine::Io io;
		io.clock = n >= clockOffset && (n - clockOffset) % period == 0;
		e.process(io);
		const bool g = io.gate[t] > 1.f;
		if (g && !prev)
			r.rises.push_back(n);
		if (!g && prev)
			r.falls.push_back(n);
		prev = g;
		r.a.push_back(io.a[t]);
	}
	return r;
}

static void testTiming() {
	// 4 pulses a step, gate 2, a clock every 1000 samples: rises every 4000 exactly, 2000 samples high, over 40 minutes
	{
		Engine e;
		load(e, oneTrack(4, 2));
		const int64_t P = 1000, N = 40LL * 60 * 48000 / 10;   // a tenth: 4 min at 48 kHz, every sample checked
		Run r = run(e, N, P);
		CHECK(!r.rises.empty() && r.rises[0] == 0, "first step on the first clock (%lld)", r.rises.empty() ? -1LL : (long long)r.rises[0]);
		bool ok = true;
		for (size_t i = 0; i < r.rises.size(); i++) {
			ok = ok && r.rises[i] == (int64_t)i * 4 * P;
			if (i < r.falls.size())
				ok = ok && r.falls[i] - r.rises[i] == 2 * P;
		}
		CHECK(ok, "steps of 4 pulses and gates of 2 land on exact samples");
		CHECK((int64_t)r.rises.size() == (N + 4 * P - 1) / (4 * P), "every step played (%zu)", r.rises.size());
	}
	// multiplied by 3 with a period of 1000: pulses at edge + 0, 333, 666; one step a pulse rises on exactly those
	{
		Engine e;
		Song s = oneTrack(1, 1);
		s.tracks[0].mult = 3;
		s.tracks[0].flags |= TRACK_TRIGGERS;
		load(e, s);
		Run r = run(e, 100000, 1000);
		bool ok = true;
		// the second external pulse gives the period; from then on three pulses a period
		std::vector<int64_t> want;
		for (int64_t edge = 0; edge < 100000; edge += 1000) {
			want.push_back(edge);
			if (edge >= 1000)
				for (int k = 1; k < 3; k++)
					want.push_back(edge + (1000 * k) / 3);
		}
		ok = r.rises.size() == want.size();
		for (size_t i = 0; ok && i < want.size(); i++)
			ok = r.rises[i] == want[i];
		CHECK(ok, "x3 pulses on exact integer positions (%zu rises, %zu wanted)", r.rises.size(), want.size());
	}
	// divided by 3: the step plays on external pulses 1, 4, 7...
	{
		Engine e;
		Song s = oneTrack(1, 1);
		s.tracks[0].div = 3;
		s.tracks[0].flags |= TRACK_TRIGGERS;
		load(e, s);
		Run r = run(e, 30000, 1000);
		bool ok = r.rises.size() == 10;
		for (size_t i = 0; ok && i < r.rises.size(); i++)
			ok = r.rises[i] == (int64_t)i * 3000;
		CHECK(ok, "/3 plays every third pulse");
	}
	// ratchet: duration 8, gate 2: high 2, low 2, high 2, low 2
	{
		Engine e;
		Song s = oneTrack(8, 2, 1);
		s.tracks[0].steps[0].flags = RATCHET;
		load(e, s);
		Run r = run(e, 8000, 1000);
		CHECK(r.rises.size() == 2 && r.rises[1] == 4000 && r.falls[0] == 2000 && r.falls[1] == 6000, "ratchet 8 / 2");
	}
	// duration 0 skipped; gate >= duration ties the steps
	{
		Engine e;
		Song s = oneTrack(2, 2, 3);
		s.tracks[0].steps[1].v[DUR] = 0;
		load(e, s);
		Run r = run(e, 12000, 1000);
		CHECK(r.rises.size() == 1 && r.falls.empty(), "legato steps keep the gate high (%zu rises)", r.rises.size());
		CHECK(r.a[0] == s.tracks[0].table[0][0] && r.a[2000] == s.tracks[0].table[0][2], "the step of duration 0 is skipped");
	}
	// reset: the next clock plays the first step again
	{
		Engine e;
		load(e, oneTrack(1, 1));
		for (int n = 0; n < 2500; n++) {
			Engine::Io io;
			io.clock = n % 1000 == 0;
			io.reset = n == 2500 - 1;
			e.process(io);
		}
		Engine::Io io;
		io.clock = true;
		e.process(io);
		CHECK(e.play[0].step == 0, "reset: back to step 1 on the next clock (step %d)", e.play[0].step);
	}
}

static void testTables() {
	Engine e;
	Song s = oneTrack(1, 1, 3);
	for (int i = 0; i < kTableSize; i++) {
		s.tracks[0].table[0][i] = -5.f + 0.15f * i;
		s.tracks[0].table[1][i] = 10.f - 0.1f * i;
	}
	s.tracks[0].steps[0].v[CV_A] = 7;
	s.tracks[0].steps[0].v[CV_B] = 93;
	s.tracks[0].steps[1].v[CV_A] = 99;
	s.tracks[0].steps[2].v[CV_A] = 0;
	load(e, s);
	Engine::Io io;
	io.clock = true;
	e.process(io);
	CHECK(io.a[0] == s.tracks[0].table[0][7] && io.b[0] == s.tracks[0].table[1][93], "CV A and B read their tables (%f %f)", io.a[0], io.b[0]);
	io = Engine::Io();
	io.transpose[0] = 1.f;      // +12 indices
	e.process(io);
	CHECK(io.a[0] == s.tracks[0].table[0][19], "TRANSP moves CV A by 12 indices a volt (%f)", io.a[0]);
	io = Engine::Io();
	io.clock = true;
	io.transpose[0] = 1.f;
	e.process(io);
	CHECK(io.a[0] == s.tracks[0].table[0][99], "an index pushed past 99 stays on 99");
	// reference tables
	float t12[kTableSize];
	referenceTable(REF_12TET, t12);
	CHECK(t12[48] == 0.f && std::fabs(t12[60] - 1.f) < 1e-6f, "12-TET: 0 V at 48, 1 V at 60");
	for (int r = 0; r < NUM_REFS; r++) {
		float t[kTableSize];
		referenceTable(r, t);
		bool ok = true;
		for (int i = 0; i < kTableSize; i++)
			ok = ok && t[i] >= kMinVolts && t[i] <= kMaxVolts && (i == 0 || t[i] >= t[i - 1] || r == REF_PENTA);
		CHECK(ok, "reference %s within -5..10 V and rising", refName(r));
	}
	float v;
	CHECK(parseVoltsOrNote("G#3", v) && std::fabs(v - (-4.f / 12)) < 1e-6f, "G#3 is -4 semitones");
	CHECK(parseVoltsOrNote("1.25", v) && v == 1.25f, "1.25 V");
	CHECK(parseVoltsOrNote("c5+50", v) && std::fabs(v - (1.f + 0.5f / 12)) < 1e-6f, "c5+50 cents");
	CHECK(!parseVoltsOrNote("H2", v), "H2 is not a note");
	// smoothing: a line from step 1 to step 2 after the gate
	{
		Engine e2;
		Song s2 = oneTrack(4, 2, 2);
		s2.tracks[0].steps[0].flags = SMOOTH_A;
		s2.tracks[0].steps[0].v[CV_A] = 48;
		s2.tracks[0].steps[1].v[CV_A] = 60;
		referenceTable(REF_12TET, s2.tracks[0].table[0]);
		load(e2, s2);
		Run r = run(e2, 4000, 1000);
		CHECK(r.a[1500] == 0.f && std::fabs(r.a[3000] - 0.5f) < 1e-3f && std::fabs(r.a[3999] - 1.f) < 2e-3f, "smooth: flat during the gate, then a line (%f %f %f)", r.a[1500], r.a[3000], r.a[3999]);
	}
}

static Song randomSong(std::mt19937 &rng) {
	Song s;
	Editor ed{s, rng};
	for (int t = 0; t < kTracks; t++) {
		Track &k = s.tracks[t];
		for (int p = 0; p < 5; p++) {
			Step st;
			ed.insertPattern(t, k.numPatterns, &st, 1 + rng() % 20);
		}
		for (int i = 0; i < k.numSteps; i++) {
			for (int f = 0; f < NUM_FIELDS; f++)
				k.steps[i].v[f] = (uint8_t)(rng() % 100);
			k.steps[i].flags = (uint8_t)(rng() % 8);
		}
		for (int i = 0; i < kTableSize; i++)
			k.table[rng() % 2][i] = clampVolts((float)(rng() % 15000) / 1000.f - 5.f);
		k.div = (uint8_t)(1 + rng() % 9);
		k.mult = (uint8_t)(1 + rng() % 9);
		k.flags = (uint8_t)(rng() % 64);
		k.patternFlags[2] = SMOOTH_B;
		k.math.add[1] = 7;
		k.math.g[2] = -3;
		for (int i = 0; i < k.numSteps; i++)
			k.steps[i].groups = (uint16_t)(rng() & 0xffff);
	}
	s.groups[3].high[1].add[CV_A] = 12;
	s.groups[3].slope[2][DUR] = 1.5f;
	s.groups[15].math.qt[GATE] = 4;
	s.rec.mode = REC_ALTER;
	s.rec.durQ[2] = 3;
	s.resetAt = COMMIT_PATTERN;
	s.resetTrack = 2;
	for (int p : {1, 5, 99})
		for (int t = 0; t < kTracks; t++) {
			s.parts[p].reset[t] = s.tracks[t].steps[rng() % s.tracks[t].numSteps].uid;
			s.parts[p].loopEnd[t] = s.tracks[t].steps[rng() % s.tracks[t].numSteps].uid;
		}
	s.transition = FIRST;
	s.layout();
	return s;
}

static void testSaving() {
	std::mt19937 rng(7);
	for (int round = 0; round < 20; round++) {
		Song s = randomSong(rng);
		std::vector<uint8_t> a, b;
		serialize(s, a);
		Song *back = new Song;
		const bool ok = deserialize(a.data(), a.size(), *back);
		serialize(*back, b);
		CHECK(ok && a == b, "save, load, save gives the same bytes (round %d)", round);
		// the parts point at the same steps
		bool same = true;
		for (int p = 0; p < kParts; p++)
			for (int t = 0; t < kTracks; t++)
				same = same && back->partReset[p][t] == s.partReset[p][t] && back->partLoopEnd[p][t] == s.partLoopEnd[p][t];
		CHECK(same, "parts keep their steps");
		delete back;
	}
	// a damaged blob is refused and leaves the song alone
	Song s;
	std::vector<uint8_t> a;
	serialize(randomSong(rng), a);
	Song *keep = new Song;
	std::vector<uint8_t> before, after;
	serialize(*keep, before);
	int refused = 0;
	for (size_t cut = 0; cut < a.size(); cut += 37)
		refused += !deserialize(a.data(), cut, *keep);
	serialize(*keep, after);
	CHECK(before == after && refused == (int)((a.size() + 36) / 37), "a truncated blob is refused (%d)", refused);
	delete keep;
}

// What the module's undo does: the song before and after an edit, with uids, and back
static void testUndo() {
	std::mt19937 rng(3);
	Song *doc = new Song(randomSong(rng));
	Editor ed{*doc, rng};
	std::vector<uint8_t> original, before, after, now;
	serialize(*doc, original, true);
	// a chain of edits, each one recorded
	std::vector<std::pair<std::vector<uint8_t>, std::vector<uint8_t>>> history;
	for (int i = 0; i < 30; i++) {
		serialize(*doc, before, true);
		const int t = rng() % kTracks;
		switch (i % 6) {
			case 0: ed.splitStep(t, rng() % doc->tracks[t].numSteps); break;
			case 1: ed.deleteSteps(t, rng() % doc->tracks[t].numSteps, 2); break;
			case 2: { Transform m; m.add[CV_A] = 5; m.jt[GATE] = 3; ed.applyMath(t, 0, doc->tracks[t].numSteps, m); break; }
			case 3: ed.rotate(t, 0, doc->tracks[t].numSteps, DUR, 1); break;
			case 4: ed.splitPattern(t, 0, 1); break;
			case 5: ed.swing(t, 0, 3); break;
		}
		serialize(*doc, after, true);
		history.push_back({before, after});
	}
	// undo all
	for (int i = (int)history.size() - 1; i >= 0; i--)
		CHECK(deserialize(history[i].first.data(), history[i].first.size(), *doc), "undo %d loads", i);
	serialize(*doc, now, true);
	CHECK(now == original, "undoing every edit gives the song back");
	// redo all
	for (auto &h : history)
		deserialize(h.second.data(), h.second.size(), *doc);
	serialize(*doc, now, true);
	CHECK(now == history.back().second, "redoing every edit gives the edited song");
	// the uids came back too: a play cursor on a step finds it again
	CHECK(doc->tracks[0].indexOf(doc->tracks[0].steps[3].uid) == 3, "uids kept by undo");
	delete doc;
}

static void testEditing() {
	std::mt19937 rng(1);
	Song *s = new Song;
	Editor ed{*s, rng};
	Track &k = s->tracks[0];
	const uint32_t u5 = k.steps[5].uid;
	Step st;
	st.v[CV_A] = 77;
	CHECK(ed.insertSteps(0, 0, 2, &st, 1) && k.numSteps == 17 && k.steps[2].v[CV_A] == 77, "insert");
	CHECK(k.indexOf(u5) == 6, "a step keeps its uid when steps move");
	k.steps[0].v[DUR] = 5;
	ed.splitStep(0, 0);
	CHECK(k.steps[0].v[DUR] == 3 && k.steps[1].v[DUR] == 2, "split 5 into 3 + 2");
	k.steps[3].v[DUR] = 16; k.steps[4].v[DUR] = 16;
	ed.swing(0, 3, 1);
	CHECK(k.steps[3].v[DUR] == 17 && k.steps[4].v[DUR] == 15, "swing keeps the total");
	Transform m;
	m.g[DUR] = 2;
	k.steps[0].v[DUR] = 64;
	ed.applyMath(0, 0, 1, m);
	CHECK(k.steps[0].v[DUR] == 64, "out of range leaves the value");
	m.clear(); m.g[CV_A] = 0; m.add[CV_A] = 12;
	ed.applyMath(0, 0, 4, m);
	CHECK(k.steps[0].v[CV_A] == 12 && k.steps[3].v[CV_A] == 12, "G = 0 sets");
	m.clear(); m.add[GATE] = 3; m.g[GATE] = 2;
	Transform inv = m.inverted();
	CHECK(inv.add[GATE] == -3 && inv.g[GATE] == -2, "inverted transform");
	m.clear(); m.qt[CV_A] = 8; k.steps[0].v[CV_A] = 13;
	ed.applyMath(0, 0, 1, m);
	CHECK(k.steps[0].v[CV_A] == 16, "quantize to 8 (%d)", k.steps[0].v[CV_A]);
	for (int i = 0; i < 4; i++) k.steps[i].v[GATE] = (uint8_t)i;
	ed.reverse(0, 0, 4, GATE);
	CHECK(k.steps[0].v[GATE] == 3 && k.steps[3].v[GATE] == 0, "reverse");
	ed.rotate(0, 0, 4, GATE, 1);
	CHECK(k.steps[0].v[GATE] == 0 && k.steps[1].v[GATE] == 3, "rotate later");
	const int np = k.numPatterns;
	CHECK(ed.splitPattern(0, 0, 4) && k.numPatterns == np + 1 && k.patternLength[0] == 4, "split pattern");
	CHECK(ed.joinPatterns(0, 0) && k.numPatterns == np, "join patterns");
	delete s;
}

// Parts: LAST waits for every track to end a loop, then each track with a reset step starts there
static void testParts() {
	Engine e;
	Song s = oneTrack(1, 1, 4);
	s.tracks[1].steps[0].v[GATE] = 1;
	s.tracks[1].patternLength[0] = 2;      // track 2 loops every 2 pulses, track 1 every 4
	s.layout();
	s.parts[2].reset[0] = s.tracks[0].steps[2].uid;
	s.parts[2].reset[1] = s.tracks[1].steps[0].uid;
	s.transition = LAST;
	s.layout();
	load(e, s);
	auto pulse = [&](bool go = false, int cv = -1) {
		Engine::Io io;
		io.clock = true;
		io.partGo = go;
		io.partCv = cv;
		e.process(io);
		for (int n = 1; n < 100; n++) {
			Engine::Io idle;
			e.process(idle);
		}
	};
	pulse();                     // step 0 of both
	pulse(true, 2);              // cue part 2 by CV: 0.2 V
	CHECK(e.pendingPart == 2 && e.playingPart == 1, "cued");
	pulse();                     // t1 step 2, t2 wraps to 0 (t2 looped)
	CHECK(e.playingPart == 1, "LAST waits for track 1");
	pulse();                     // t1 step 3
	pulse();                     // t1 wraps: both looped -> part 2, t1 starts at step 2
	CHECK(e.playingPart == 2 && e.play[0].step == 2, "part 2 at the last loop end, reset step (step %d)", e.play[0].step);
	// stop part
	e.uiGoPart = 0;
	s.transition = NOW;
	load(e, s);
	pulse();
	Engine::Io io;
	e.process(io);
	CHECK(e.playingPart == 0 && io.gate[0] == 0.f, "part 0 stops every gate");
}


// Groups: the high and low transforms follow the X/Y/Z gates as the steps play; slopes add the bus's CVs
static void testGroups() {
	Engine e;
	Song s = oneTrack(4, 2, 2);
	referenceTable(REF_12TET, s.tracks[0].table[0]);
	s.tracks[0].steps[0].v[CV_A] = 48;
	s.tracks[0].steps[1].v[CV_A] = 48;
	s.tracks[0].steps[1].groups = 1 << 5;
	s.groups[5].high[0].add[CV_A] = 12;       // X high: an octave up
	s.groups[5].low[0].g[DUR] = 2;            // X low: twice as long
	s.groups[5].slope[1][CV_A] = 0.5f;        // Y CV: half a volt per volt on CV A
	load(e, s);
	auto run1 = [&](bool xHigh, float yCv, int samples, std::vector<float> &a, std::vector<float> &g) {
		for (int n = 0; n < samples; n++) {
			Engine::Io io;
			io.clock = n % 1000 == 0;
			io.modHigh[0] = xHigh;
			io.modCv[1] = yCv;
			e.process(io);
			a.push_back(io.a[0]);
			g.push_back(io.gate[0]);
		}
	};
	std::vector<float> a, g;
	run1(true, 0.f, 8000, a, g);
	CHECK(a[1000] == 0.f && std::fabs(a[5000] - 1.f) < 1e-6f, "X high: the group's step an octave up (%f)", a[5000]);
	e.resetRequest = true;
	a.clear(); g.clear();
	run1(false, 2.f, 16000, a, g);
	// the group's step lasts 8 pulses now: steps at 0 and 4000, then the first again at 12000
	CHECK(g[12000] > 1.f && g[9000] < 1.f, "X low: the group's step twice as long");
	CHECK(std::fabs(a[5000] - 1.f) < 1e-6f && a[1000] == 0.f, "Y slope: +0.5 V/V on the group's steps only (%f)", a[5000]);
	// membership tools
	std::mt19937 rng(1);
	Song *t = new Song;
	Editor ed{*t, rng};
	ed.euclidMembers(0, 0, 16, 2, 3, 8);
	int count = 0;
	for (int i = 0; i < 16; i++)
		count += (t->tracks[0].steps[i].groups >> 2) & 1;
	CHECK(count == 6 && (t->tracks[0].steps[0].groups & 4) && (t->tracks[0].steps[3].groups & 4) && (t->tracks[0].steps[6].groups & 4), "E(3,8) repeated");
	ed.rotateMembers(0, 2, 1);
	CHECK((t->tracks[0].steps[1].groups & 4) && !(t->tracks[0].steps[0].groups & 4), "rotate membership");
	ed.invertMembers(0, 0, 16, 2);
	CHECK((t->tracks[0].steps[0].groups & 4) && !(t->tracks[0].steps[1].groups & 4), "invert membership");
	t->tracks[0].steps[0].v[GATE] = 1;
	t->tracks[0].steps[1].v[GATE] = 1;
	Transform m;
	m.add[GATE] = 5;
	ed.applyMath(0, 0, 16, m, 2);
	CHECK(t->tracks[0].steps[0].v[GATE] == 6 && t->tracks[0].steps[1].v[GATE] == 1, "group MATH only on members");
	delete t;
}

// RESET waiting for the end of a pattern: the reset lands exactly on the pattern boundary
static void testQuantizedReset() {
	Engine e;
	Song s = oneTrack(1, 1, 8);
	s.tracks[0].patternLength[0] = 4;
	s.tracks[0].patternLength[1] = 4;
	s.tracks[0].numPatterns = 2;
	s.resetAt = COMMIT_PATTERN;
	s.layout();
	load(e, s);
	int stepAt[12];
	for (int c = 0; c < 12; c++) {
		Engine::Io io;
		io.clock = true;
		io.reset = c == 5;      // asked during pattern 2
		e.process(io);
		for (int n = 1; n < 10; n++) {
			Engine::Io idle;
			e.process(idle);
		}
		stepAt[c] = e.play[0].step;
	}
	CHECK(stepAt[5] == 5 && stepAt[6] == 6 && stepAt[7] == 7 && stepAt[8] == 0, "reset waits for the end of the pattern (%d %d %d %d)", stepAt[5], stepAt[6], stepAt[7], stepAt[8]);
}

static std::vector<RecEvent> drain(Engine &e) {
	std::vector<RecEvent> v;
	RecEvent ev;
	while (e.recQueue.pop(ev))
		v.push_back(ev);
	return v;
}

// Real-time recording: notes from a gate, timed in pulses, CV quantized to the table
static void testRecording() {
	Engine e;
	Song s = oneTrack(4, 2, 4);
	referenceTable(REF_12TET, s.tracks[0].table[0]);
	load(e, s);
	e.armed[0] = true;
	// gate: on at pulse 2 for 1 pulse at C5 (1 V), on at pulse 5 for 3 pulses at G4, punch out at pulse 10
	for (int n = 0; n < 12000; n++) {
		Engine::Io io;
		io.clock = n % 1000 == 0;
		io.punch = n < 10000;
		const bool g1 = n >= 2000 && n < 3000, g2 = n >= 5000 && n < 8000;
		io.recGateOn = io.recAOn = true;
		io.recGateV = (g1 || g2) ? 10.f : 0.f;
		io.recA = g1 ? 1.f : 7.f / 12.f;
		e.process(io);
	}
	std::vector<RecEvent> ev = drain(e);
	CHECK(ev.size() == 3 && ev[0].type == EV_TAKE, "a take, then two notes (%zu events)", ev.size());
	if (ev.size() == 3) {
		CHECK(ev[1].dur == 3 && ev[1].gate == 1 && ev[1].a == 60, "first note: 3 pulses to the next, gate 1, C5 (%d %d %d)", ev[1].dur, ev[1].gate, ev[1].a);
		CHECK(ev[2].dur == 5 && ev[2].gate == 3 && ev[2].a == 55, "second note: to the punch out, gate 3, G4 (%d %d %d)", ev[2].dur, ev[2].gate, ev[2].a);
	}
	// while recording, the track plays its inputs
	Engine::Io io;
	io.punch = true;
	io.recGateOn = io.recAOn = true;
	io.recGateV = 10.f;
	io.recA = 0.25f;
	e.process(io);
	CHECK(io.a[0] == 0.25f && io.gate[0] == kGateVolts, "pass-thru while armed and punched in");
	// a long note becomes tied steps
	Step out[8], proto;
	const int m = noteSteps(250, 230, proto, out, 8);
	CHECK(m == 3 && out[0].v[DUR] == 99 && out[0].v[GATE] == 99 && out[2].v[DUR] == 52 && out[2].v[GATE] == 32, "250 pulses: 99 + 99 + 52, tied");

	// ALTER: the step about to play takes the inputs' values, in the audio's song and in an event
	Engine e2;
	Song s2 = oneTrack(2, 1, 2);
	referenceTable(REF_12TET, s2.tracks[0].table[0]);
	s2.rec.mode = REC_ALTER;
	load(e2, s2);
	e2.armed[0] = true;
	for (int n = 0; n < 3000; n++) {
		Engine::Io io2;
		io2.clock = n % 1000 == 0;
		io2.punch = true;
		io2.recAOn = io2.recGateOn = true;
		io2.recA = 2.f;
		io2.recGateV = 0.5f;          // GATE 10
		e2.process(io2);
	}
	ev = drain(e2);
	CHECK(!ev.empty() && ev[0].type == EV_ALTER && ev[0].a == 72 && ev[0].gate == 10 && ev[0].mask == (1 | 8), "alter: CV A and GATE rewritten");
	CHECK(e2.song().tracks[0].steps[0].v[CV_A] == 72, "alter: the playing song changed");

	// STEP: INSERT and DELETE triggers become events for the UI
	Engine e3;
	Song s3 = oneTrack(2, 1, 2);
	s3.rec.mode = REC_STEP;
	load(e3, s3);
	e3.armed[2] = true;
	Engine::Io io3;
	io3.punch = true;
	io3.recInsert = true;
	e3.process(io3);
	ev = drain(e3);
	CHECK(ev.size() == 1 && ev[0].type == EV_INSERT && ev[0].track == 2, "step mode: insert event");
}

static void put32be(std::vector<uint8_t> &v, uint32_t x) { for (int k = 3; k >= 0; k--) v.push_back((uint8_t)(x >> (8 * k))); }

static void testMidi() {
	// SMF 0, 96 ticks a beat (4 pulses a beat): channel 1: C4 for a beat, a beat of silence, E4 half a beat, then G4
	// a beat; channel 2: A3 from beat 1 for 2.5 beats
	std::vector<uint8_t> trk = {
		0x00, 0x90, 60, 100,  0x60, 0x80, 60, 0,
		0x00, 0x91, 57, 50,
		0x60, 0x90, 64, 127,  0x30, 0x80, 64, 0,
		0x00, 0x90, 67, 64,   0x00, 0x81, 57, 0,
		0x60, 0x80, 67, 0,    0x00, 0xff, 0x2f, 0x00};
	std::vector<uint8_t> f = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0, 96, 'M', 'T', 'r', 'k'};
	put32be(f, (uint32_t)trk.size());
	f.insert(f.end(), trk.begin(), trk.end());
	Song *s = new Song;
	CHECK(importMidi(f.data(), f.size(), *s, 4, 4), "MIDI file read");
	const Track &k = s->tracks[0];
	// C4 lasts until E4 starts (8 pulses) with gate 4; E4 2 pulses, gate 2; G4 4 pulses, gate 4
	CHECK(k.numSteps == 3, "three steps on track 1 (%d)", k.numSteps);
	if (k.numSteps == 3) {
		CHECK(k.steps[0].v[CV_A] == 48 && k.steps[0].v[DUR] == 8 && k.steps[0].v[GATE] == 4, "C4: lasts to E4, gate 4 (%d %d)", k.steps[0].v[DUR], k.steps[0].v[GATE]);
		CHECK(k.steps[1].v[CV_A] == 52 && k.steps[1].v[DUR] == 2 && k.steps[1].v[GATE] == 2, "E4");
		CHECK(k.steps[2].v[CV_A] == 55 && k.steps[2].v[DUR] == 4 && k.steps[2].v[GATE] == 4, "G4");
	}
	const Track &k2 = s->tracks[1];
	CHECK(k2.numSteps == 2 && k2.steps[0].v[GATE] == 0 && k2.steps[0].v[DUR] == 4 && k2.steps[1].v[CV_A] == 45 && k2.steps[1].v[DUR] == 6,
		"channel 2: a rest of 4 pulses, then A3 for 6 (%d steps)", k2.numSteps);
	delete s;
}

int main() {
	testTiming();
	testTables();
	testSaving();
	testUndo();
	testEditing();
	testParts();
	testGroups();
	testQuantizedReset();
	testRecording();
	testMidi();
	std::printf(failures ? "%d FAILED\n" : "all passed\n", failures);
	return failures ? 1 : 0;
}
