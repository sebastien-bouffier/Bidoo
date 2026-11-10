// rEgIStRE's sequencer: four tracks of patterns of steps, each step holding indices into its track's two voltage tables
// (CV A and CV B), a duration and a gate in clock pulses, smoothing and ratchet. Loops and reset steps come from parts,
// which a CV and a gate switch at the end of a loop. Groups of steps take transforms that the X, Y and Z gates switch
// and CVs that the X, Y and Z inputs add while they play. Recording writes steps from CV and gate inputs.
// No Rack here: tools/tests/registre_engine_test.cpp builds it alone.
//
// Threads: the UI edits its own Song (the document) and hands copies to the audio thread through a triple buffer, so
// process() never waits, locks or allocates. The play cursor follows a step by its uid, so edits that move steps under
// it keep it on the same step. What the audio records goes back to the UI through a lock-free queue of events.
//
// Timing is counted in whole clock pulses and whole samples: a step lasts exactly its duration in pulses, and a clock
// multiplied by n puts its pulses at base + k * period / n samples, in integers, from the last external pulse.
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <vector>

namespace registre {

static constexpr int kTracks = 4;
static constexpr int kMaxPatterns = 100;
static constexpr int kMaxPatternSteps = 100;
static constexpr int kMaxTrackSteps = 1000;
static constexpr int kTableSize = 100;
static constexpr int kParts = 100;           // part 0 is the stop part: every track silent
static constexpr int kGroups = 16;
static constexpr int kChannels = 3;          // the modulation bus: X, Y, Z
static constexpr int kMaxValue = 99;         // the largest index, duration and gate
static constexpr float kMinVolts = -5.f;
static constexpr float kMaxVolts = 10.f;
static constexpr float kGateVolts = 10.f;

enum Field { CV_A, CV_B, DUR, GATE, NUM_FIELDS };
enum StepFlag : uint8_t { SMOOTH_A = 1, SMOOTH_B = 2, RATCHET = 4 };
enum Transition { FIRST, LAST, NOW, NUM_TRANSITIONS };   // when a cued part starts: first loop end, last, at once
enum CommitAt { COMMIT_NOW, COMMIT_STEP, COMMIT_PATTERN, COMMIT_LOOP };   // also when a quantized reset happens
enum RecMode { REC_REALTIME, REC_STEP, REC_ALTER, NUM_REC_MODES };
enum RecFocus { REC_TRACK, REC_PATTERN, REC_AT_STEP, NUM_REC_FOCUS };     // where real-time recording inserts

struct Step {
	uint32_t uid = 0;
	uint8_t v[NUM_FIELDS] = {48, 0, 4, 2};
	uint8_t flags = 0;
	uint16_t groups = 0;                     // bit g: the step is in group g
	bool operator==(const Step &o) const {
		return std::memcmp(v, o.v, sizeof(v)) == 0 && flags == o.flags && groups == o.groups;
	}
};

// MATH: per field, P' = QUANTIZE(G * (RD > 0 ? RANDOM(RD) : P) + JITTER(JT) + ADD, QT) when destructive (applied once
// to the steps), and QUANTIZE(G * (P + RANDOM(RD)) + JITTER(JT) + ADD, QT) when not (a group's high and low transforms,
// drawn again each time a step plays). g >= 1 multiplies, g <= -2 divides by -g, g = 0 sets (with ADD). A result out of
// 0..99 leaves the value as it was.
struct Transform {
	int8_t rd[NUM_FIELDS], g[NUM_FIELDS], jt[NUM_FIELDS], add[NUM_FIELDS], qt[NUM_FIELDS];
	Transform() { clear(); }
	void clear() {
		for (int f = 0; f < NUM_FIELDS; f++) {
			rd[f] = 0; g[f] = 1; jt[f] = 0; add[f] = 0; qt[f] = 1;
		}
	}
	bool identity() const { return *this == Transform(); }
	bool operator==(const Transform &o) const { return std::memcmp(this, &o, sizeof(Transform)) == 0; }
	Transform inverted() const {
		Transform t = *this;
		for (int f = 0; f < NUM_FIELDS; f++) {
			t.add[f] = (int8_t)-add[f];
			if (g[f] >= 2 || g[f] <= -2)
				t.g[f] = (int8_t)-g[f];
		}
		return t;
	}
};

inline int roundDiv(int x, int d) { return (x >= 0) ? (x + d / 2) / d : -((-x + d / 2) / d); }

// One field through a transform; rnd(lo, hi) gives a uniform integer
template <typename R>
inline int transformField(const Transform &m, int f, int v, bool destructive, R rnd) {
	int x = v;
	if (m.rd[f] > 0)
		x = destructive ? rnd(0, m.rd[f]) : v + rnd(0, m.rd[f]);
	const int g = m.g[f];
	if (g >= 1 || g == -1)
		x *= (g == -1 ? 1 : g);
	else if (g == 0)
		x = 0;
	else
		x = roundDiv(x, -g);
	if (m.jt[f] > 0)
		x += rnd(-m.jt[f], m.jt[f]);
	x += m.add[f];
	const int q = std::max(1, (int)m.qt[f]);
	if (q > 1)
		x = roundDiv(x, q) * q;
	return (x < 0 || x > kMaxValue) ? v : x;
}

// A group: a selection of steps (Step::groups), a destructive transform, and for each channel of the bus a high and a
// low transform (its gate high or low) and slopes (how much its CV adds: volts per volt to CV A and B, units per volt
// to DUR and GATE)
struct Group {
	Transform math;
	Transform high[kChannels], low[kChannels];
	float slope[kChannels][NUM_FIELDS] = {};
	bool isDefault() const {
		for (int c = 0; c < kChannels; c++) {
			if (!high[c].identity() || !low[c].identity())
				return false;
			for (int f = 0; f < NUM_FIELDS; f++)
				if (slope[c][f] != 0.f)
					return false;
		}
		return math.identity();
	}
};

// Recording, per song
struct RecSettings {
	uint8_t mode = REC_REALTIME, focus = REC_TRACK;
	uint8_t waitFirst = 1;                   // real-time: the take starts with the first note, not at punch in
	uint8_t trig[kTracks] = {1, 1, 1, 1};    // bit 0: a change of CV A starts a step; bit 1: of CV B
	uint8_t durQ[kTracks] = {1, 1, 1, 1}, gateQ[kTracks] = {1, 1, 1, 1};
};

// ---- Reference tables
enum RefTable { REF_12TET, REF_24TET, REF_SHRUTI, REF_MAJOR, REF_MINOR, REF_BLUES, REF_PENTA, REF_LIN10, REF_LIN5, REF_EXP10, NUM_REFS };
inline const char *refName(int r) {
	static const char *names[NUM_REFS] = {"12-TET", "24-TET", "22 SHRUTI", "MAJOR", "MINOR", "BLUES", "PENTA", "LIN 0-10", "LIN +-5", "EXP 0-10"};
	return (r >= 0 && r < NUM_REFS) ? names[r] : "";
}

inline float clampVolts(float v) { return std::min(kMaxVolts, std::max(kMinVolts, v)); }

// A scale repeated over octaves, its degree 0 at 0 V at index zeroIndex
inline float scaleVolts(int i, const double *cents, int n, int zeroIndex) {
	const int k = i - zeroIndex;
	const int oct = (k >= 0) ? k / n : -((-k + n - 1) / n);
	const int deg = k - oct * n;
	return (float)(oct + cents[deg] / 1200.0);
}

inline void referenceTable(int r, float *out) {
	static const double major[7] = {0, 200, 400, 500, 700, 900, 1100};
	static const double minor[7] = {0, 200, 300, 500, 700, 800, 1000};
	static const double blues[9] = {0, 200, 300, 400, 500, 700, 900, 1000, 1100};
	static const double pentaMaj[5] = {0, 200, 400, 700, 900};
	static const double pentaMin[5] = {0, 300, 500, 700, 1000};
	static const double ratios[22] = {1., 256. / 243, 16. / 15, 10. / 9, 9. / 8, 32. / 27, 6. / 5, 5. / 4, 81. / 64, 4. / 3, 27. / 20,
		45. / 32, 729. / 512, 3. / 2, 128. / 81, 8. / 5, 5. / 3, 27. / 16, 16. / 9, 9. / 5, 15. / 8, 243. / 128};
	double shruti[22];
	for (int k = 0; k < 22; k++)
		shruti[k] = 1200.0 * std::log2(ratios[k]);
	for (int i = 0; i < kTableSize; i++) {
		float v = 0.f;
		switch (r) {
			case REF_12TET: v = (i - 48) / 12.f; break;
			case REF_24TET: v = (i - 48) / 24.f; break;
			case REF_SHRUTI: v = scaleVolts(i, shruti, 22, 44); break;
			case REF_MAJOR: v = scaleVolts(i, major, 7, 28); break;
			case REF_MINOR: v = scaleVolts(i, minor, 7, 28); break;
			case REF_BLUES: v = scaleVolts(i, blues, 9, 36); break;
			case REF_PENTA: v = (i < 50) ? scaleVolts(i, pentaMaj, 5, 20) : scaleVolts(i - 50, pentaMin, 5, 20); break;
			case REF_LIN10: v = i * 0.1f; break;
			case REF_LIN5: v = (i - 50) * 0.1f; break;
			case REF_EXP10: v = (float)(10.0 * (std::pow(2.0, 8.0 * i / (kTableSize - 1)) - 1.0) / 255.0); break;
			default: break;
		}
		out[i] = clampVolts(v);
	}
}

// The index whose voltage is nearest v
inline int nearestIndex(const float *table, float v) {
	int best = 0;
	float bestD = std::fabs(table[0] - v);
	for (int i = 1; i < kTableSize; i++) {
		const float d = std::fabs(table[i] - v);
		if (d < bestD) {
			best = i;
			bestD = d;
		}
	}
	return best;
}

// ---- The song
enum TrackFlag : uint8_t { TRACK_TRIGGERS = 8, NOTES_A = 16, NOTES_B = 32 };   // with SMOOTH_A and SMOOTH_B for the whole track

struct Track {
	int16_t numSteps = 0, numPatterns = 0;
	uint8_t patternLength[kMaxPatterns] = {};
	uint8_t patternFlags[kMaxPatterns] = {};   // SMOOTH_A, SMOOTH_B for a whole pattern
	uint8_t flags = NOTES_A;                   // SMOOTH_A, SMOOTH_B, TRACK_TRIGGERS, NOTES_A, NOTES_B
	uint8_t div = 1, mult = 1;
	uint32_t nextUid = 1;
	float table[2][kTableSize];
	Transform math;
	Step steps[kMaxTrackSteps];
	// From layout(): where each pattern starts, and each step's pattern
	int16_t patternStart[kMaxPatterns + 1] = {};
	uint8_t stepPattern[kMaxTrackSteps] = {};

	bool triggers() const { return flags & TRACK_TRIGGERS; }

	void layout() {
		int s = 0;
		for (int p = 0; p < numPatterns; p++) {
			patternStart[p] = (int16_t)s;
			for (int j = 0; j < patternLength[p]; j++)
				stepPattern[s + j] = (uint8_t)p;
			s += patternLength[p];
		}
		patternStart[numPatterns] = (int16_t)s;
		numSteps = (int16_t)s;
	}

	int indexOf(uint32_t uid) const {
		if (!uid)
			return -1;
		for (int i = 0; i < numSteps; i++)
			if (steps[i].uid == uid)
				return i;
		return -1;
	}

	bool smooth(int i, int ab) const {
		const uint8_t bit = ab ? SMOOTH_B : SMOOTH_A;
		return (steps[i].flags & bit) || (patternFlags[stepPattern[i]] & bit) || (flags & bit);
	}
};

// A part sets, for each track, the step a transition (or RESET) goes to and the looped section. Steps by uid, 0 = none
struct Part {
	uint32_t reset[kTracks] = {}, loopStart[kTracks] = {}, loopEnd[kTracks] = {};
	bool empty() const {
		for (int t = 0; t < kTracks; t++)
			if (reset[t] || loopStart[t] || loopEnd[t])
				return false;
		return true;
	}
};

struct Song {
	Track tracks[kTracks];
	Part parts[kParts];
	Group groups[kGroups];
	RecSettings rec;
	uint8_t transition = LAST;
	uint8_t resetAt = COMMIT_NOW, resetTrack = 0;   // RESET waits for the end of a step, pattern or loop of a track
	// From layout(): the parts' steps as indices, -1 = none
	int16_t partReset[kParts][kTracks], partLoopStart[kParts][kTracks], partLoopEnd[kParts][kTracks];

	Song() { init(); }

	// Track 1 plays 16 notes a beat apart (4 pulses), the others hold 16 rests to fill in
	void init() {
		for (int t = 0; t < kTracks; t++) {
			Track &tr = tracks[t];
			tr = Track();
			referenceTable(REF_12TET, tr.table[0]);
			referenceTable(REF_LIN10, tr.table[1]);
			tr.numPatterns = 1;
			tr.patternLength[0] = 16;
			for (int i = 0; i < 16; i++) {
				Step &s = tr.steps[i];
				s = Step();
				s.uid = tr.nextUid++;
				s.v[CV_A] = (uint8_t)nearestIndex(tr.table[0], 0.f);
				s.v[CV_B] = (uint8_t)nearestIndex(tr.table[1], 0.f);
				s.v[DUR] = 4;
				s.v[GATE] = t == 0 ? 2 : 0;
			}
		}
		for (int p = 0; p < kParts; p++)
			parts[p] = Part();
		for (int g = 0; g < kGroups; g++)
			groups[g] = Group();
		rec = RecSettings();
		transition = LAST;
		resetAt = COMMIT_NOW;
		resetTrack = 0;
		layout();
	}

	void layout() {
		for (int t = 0; t < kTracks; t++)
			tracks[t].layout();
		for (int p = 0; p < kParts; p++)
			for (int t = 0; t < kTracks; t++) {
				const Part &pt = parts[p];
				partReset[p][t] = (int16_t)tracks[t].indexOf(pt.reset[t]);
				partLoopStart[p][t] = (int16_t)tracks[t].indexOf(pt.loopStart[t]);
				partLoopEnd[p][t] = (int16_t)tracks[t].indexOf(pt.loopEnd[t]);
			}
	}
};

// ---- Saving: a compact binary form (base64 in the patch). Version 2:
// 'R','G','S',version, transition, resetAt, resetTrack, the recording settings; per track: patterns, their lengths and
// flags, track flags, div, mult, MATH (20 bytes), steps (CV A, CV B, DUR, GATE, flags, groups; uids too when withUids),
// tables as float32; then the parts that are set, as indices into their tracks; then the groups that are not default.
static constexpr uint8_t kFormat = 2;

inline void put16(std::vector<uint8_t> &o, int v) { o.push_back((uint8_t)(v & 0xff)); o.push_back((uint8_t)((v >> 8) & 0xff)); }
inline void put32(std::vector<uint8_t> &o, uint32_t v) { for (int k = 0; k < 4; k++) o.push_back((uint8_t)(v >> (8 * k))); }
inline void putFloat(std::vector<uint8_t> &o, float f) { uint32_t b; std::memcpy(&b, &f, 4); put32(o, b); }
inline void putBytes(std::vector<uint8_t> &o, const void *p, size_t n) { const uint8_t *b = (const uint8_t *)p; o.insert(o.end(), b, b + n); }

inline void serialize(const Song &s, std::vector<uint8_t> &o, bool withUids = false) {
	o.clear();
	o.insert(o.end(), {'R', 'G', 'S', (uint8_t)(withUids ? 0x80 | kFormat : kFormat), s.transition, s.resetAt, s.resetTrack});
	putBytes(o, &s.rec, sizeof(RecSettings));
	for (int t = 0; t < kTracks; t++) {
		const Track &tr = s.tracks[t];
		o.push_back((uint8_t)tr.numPatterns);
		putBytes(o, tr.patternLength, tr.numPatterns);
		putBytes(o, tr.patternFlags, tr.numPatterns);
		o.push_back(tr.flags);
		o.push_back(tr.div);
		o.push_back(tr.mult);
		putBytes(o, &tr.math, sizeof(Transform));
		if (withUids)
			put32(o, tr.nextUid);
		for (int i = 0; i < tr.numSteps; i++) {
			const Step &st = tr.steps[i];
			putBytes(o, st.v, NUM_FIELDS);
			o.push_back(st.flags);
			put16(o, st.groups);
			if (withUids)
				put32(o, st.uid);
		}
		for (int ab = 0; ab < 2; ab++)
			for (int i = 0; i < kTableSize; i++)
				putFloat(o, tr.table[ab][i]);
	}
	std::vector<int> used;
	for (int p = 0; p < kParts; p++)
		if (!s.parts[p].empty())
			used.push_back(p);
	o.push_back((uint8_t)used.size());
	for (int p : used) {
		o.push_back((uint8_t)p);
		for (int t = 0; t < kTracks; t++) {
			put16(o, s.partReset[p][t]);
			put16(o, s.partLoopStart[p][t]);
			put16(o, s.partLoopEnd[p][t]);
		}
	}
	std::vector<int> groups;
	for (int g = 0; g < kGroups; g++)
		if (!s.groups[g].isDefault())
			groups.push_back(g);
	o.push_back((uint8_t)groups.size());
	for (int g : groups) {
		const Group &gr = s.groups[g];
		o.push_back((uint8_t)g);
		putBytes(o, &gr.math, sizeof(Transform));
		putBytes(o, gr.high, sizeof(gr.high));
		putBytes(o, gr.low, sizeof(gr.low));
		for (int c = 0; c < kChannels; c++)
			for (int f = 0; f < NUM_FIELDS; f++)
				putFloat(o, gr.slope[c][f]);
	}
}

// A transform read from a blob, its settings kept within their ranges
inline void sanitize(Transform &m) {
	for (int f = 0; f < NUM_FIELDS; f++) {
		m.rd[f] = (int8_t)std::max(0, std::min(99, (int)m.rd[f]));
		m.g[f] = (int8_t)std::max(-99, std::min(99, (int)m.g[f]));
		m.jt[f] = (int8_t)std::max(0, std::min(99, (int)m.jt[f]));
		m.add[f] = (int8_t)std::max(-99, std::min(99, (int)m.add[f]));
		m.qt[f] = (int8_t)std::max(1, std::min(99, (int)m.qt[f]));
	}
}

// Reads what serialize() wrote into out. On a malformed blob it returns false and leaves out as it was.
inline bool deserialize(const uint8_t *d, size_t n, Song &out) {
	size_t i = 0;
	auto need = [&](size_t k) { return i + k <= n; };
	auto get16 = [&]() { int v = (int16_t)(d[i] | (d[i + 1] << 8)); i += 2; return v; };
	auto get32 = [&]() { uint32_t v = 0; for (int k = 0; k < 4; k++) v |= (uint32_t)d[i + k] << (8 * k); i += 4; return v; };
	auto getFloat = [&]() { uint32_t b = get32(); float f; std::memcpy(&f, &b, 4); return std::isfinite(f) ? f : 0.f; };
	if (!need(7 + sizeof(RecSettings)) || d[0] != 'R' || d[1] != 'G' || d[2] != 'S' || (d[3] & 0x7f) != kFormat)
		return false;
	const bool withUids = d[3] & 0x80;
	std::unique_ptr<Song> s(new Song);
	s->transition = d[4] < NUM_TRANSITIONS ? d[4] : (uint8_t)LAST;
	s->resetAt = d[5] <= COMMIT_LOOP ? d[5] : (uint8_t)COMMIT_NOW;
	s->resetTrack = d[6] < kTracks ? d[6] : 0;
	i = 7;
	std::memcpy(&s->rec, d + i, sizeof(RecSettings));
	i += sizeof(RecSettings);
	RecSettings &r = s->rec;
	r.mode = std::min<uint8_t>(r.mode, NUM_REC_MODES - 1);
	r.focus = std::min<uint8_t>(r.focus, NUM_REC_FOCUS - 1);
	for (int t = 0; t < kTracks; t++) {
		r.trig[t] &= 3;
		r.durQ[t] = (uint8_t)std::max(1, std::min(99, (int)r.durQ[t]));
		r.gateQ[t] = (uint8_t)std::max(1, std::min(99, (int)r.gateQ[t]));
	}
	for (int t = 0; t < kTracks; t++) {
		Track &tr = s->tracks[t];
		if (!need(1))
			return false;
		const int np = d[i++];
		if (np > kMaxPatterns || !need(2 * np + 3 + sizeof(Transform)))
			return false;
		tr.numPatterns = (int16_t)np;
		int total = 0;
		for (int p = 0; p < np; p++) {
			tr.patternLength[p] = d[i + p];
			total += d[i + p];
			if (d[i + p] > kMaxPatternSteps)
				return false;
		}
		i += np;
		std::memcpy(tr.patternFlags, d + i, np);
		i += np;
		if (total > kMaxTrackSteps)
			return false;
		tr.flags = d[i++];
		tr.div = (uint8_t)std::max(1, std::min(99, (int)d[i++]));
		tr.mult = (uint8_t)std::max(1, std::min(99, (int)d[i++]));
		std::memcpy(&tr.math, d + i, sizeof(Transform));
		sanitize(tr.math);
		i += sizeof(Transform);
		if (withUids) {
			if (!need(4))
				return false;
			tr.nextUid = get32();
		}
		const size_t stepBytes = withUids ? 11 : 7;
		if (!need(stepBytes * total + 8 * kTableSize))
			return false;
		for (int k = 0; k < total; k++) {
			Step &st = tr.steps[k];
			for (int f = 0; f < NUM_FIELDS; f++)
				st.v[f] = (uint8_t)std::min((int)d[i + f], kMaxValue);
			st.flags = d[i + NUM_FIELDS] & (SMOOTH_A | SMOOTH_B | RATCHET);
			i += 5;
			st.groups = (uint16_t)get16();
			st.uid = withUids ? get32() : tr.nextUid++;
		}
		for (int ab = 0; ab < 2; ab++)
			for (int k = 0; k < kTableSize; k++)
				tr.table[ab][k] = clampVolts(getFloat());
		tr.layout();
	}
	if (!need(1))
		return false;
	const int used = d[i++];
	if (!need((size_t)used * (1 + 6 * kTracks)))
		return false;
	for (int u = 0; u < used; u++) {
		const int p = d[i++];
		if (p >= kParts)
			return false;
		for (int t = 0; t < kTracks; t++) {
			const Track &tr = s->tracks[t];
			auto uidAt = [&](int idx) { return (idx >= 0 && idx < tr.numSteps) ? tr.steps[idx].uid : 0u; };
			s->parts[p].reset[t] = uidAt(get16());
			s->parts[p].loopStart[t] = uidAt(get16());
			s->parts[p].loopEnd[t] = uidAt(get16());
		}
	}
	if (!need(1))
		return false;
	const int groups = d[i++];
	const size_t groupBytes = 1 + sizeof(Transform) * (1 + 2 * kChannels) + 4 * kChannels * NUM_FIELDS;
	if (groups > kGroups || !need(groups * groupBytes))
		return false;
	for (int u = 0; u < groups; u++) {
		const int g = d[i++];
		if (g >= kGroups)
			return false;
		Group &gr = s->groups[g];
		std::memcpy(&gr.math, d + i, sizeof(Transform));
		i += sizeof(Transform);
		std::memcpy(gr.high, d + i, sizeof(gr.high));
		i += sizeof(gr.high);
		std::memcpy(gr.low, d + i, sizeof(gr.low));
		i += sizeof(gr.low);
		sanitize(gr.math);
		for (int c = 0; c < kChannels; c++) {
			sanitize(gr.high[c]);
			sanitize(gr.low[c]);
			for (int f = 0; f < NUM_FIELDS; f++)
				gr.slope[c][f] = std::max(-99.f, std::min(99.f, getFloat()));
		}
	}
	s->layout();
	out = *s;
	return true;
}

// ---- Editing, on the UI's Song. Every function keeps the layout; they return false when the song is full
struct Editor {
	Song &s;
	std::mt19937 &rng;

	Track &tr(int t) { return s.tracks[t]; }

	// Inserts n steps (copies of src, given new uids) into pattern p before its step `pos`
	bool insertSteps(int t, int p, int pos, const Step *src, int n) {
		Track &k = tr(t);
		if (p < 0 || p >= k.numPatterns || n <= 0 || k.numSteps + n > kMaxTrackSteps || k.patternLength[p] + n > kMaxPatternSteps)
			return false;
		pos = std::max(0, std::min(pos, (int)k.patternLength[p]));
		const int at = k.patternStart[p] + pos;
		std::memmove(&k.steps[at + n], &k.steps[at], sizeof(Step) * (k.numSteps - at));
		for (int j = 0; j < n; j++) {
			k.steps[at + j] = src[j];
			k.steps[at + j].uid = k.nextUid++;
		}
		k.patternLength[p] = (uint8_t)(k.patternLength[p] + n);
		s.layout();
		return true;
	}

	// Removes steps first..first+n-1 of the track (patterns keep their place, maybe empty)
	void deleteSteps(int t, int first, int n) {
		Track &k = tr(t);
		first = std::max(0, first);
		n = std::min(n, k.numSteps - first);
		if (n <= 0)
			return;
		for (int j = first; j < first + n; j++)
			k.patternLength[k.stepPattern[j]]--;
		std::memmove(&k.steps[first], &k.steps[first + n], sizeof(Step) * (k.numSteps - first - n));
		s.layout();
	}

	// A new pattern at index p holding n copies of src (n may be 0: an empty pattern)
	bool insertPattern(int t, int p, const Step *src, int n) {
		Track &k = tr(t);
		if (k.numPatterns >= kMaxPatterns || n > kMaxPatternSteps || k.numSteps + n > kMaxTrackSteps)
			return false;
		p = std::max(0, std::min(p, (int)k.numPatterns));
		std::memmove(&k.patternLength[p + 1], &k.patternLength[p], k.numPatterns - p);
		std::memmove(&k.patternFlags[p + 1], &k.patternFlags[p], k.numPatterns - p);
		k.patternLength[p] = 0;
		k.patternFlags[p] = 0;
		k.numPatterns++;
		k.layout();
		if (n > 0)
			return insertSteps(t, p, 0, src, n);
		s.layout();
		return true;
	}

	void deletePattern(int t, int p) {
		Track &k = tr(t);
		if (p < 0 || p >= k.numPatterns)
			return;
		deleteSteps(t, k.patternStart[p], k.patternLength[p]);
		std::memmove(&k.patternLength[p], &k.patternLength[p + 1], k.numPatterns - p - 1);
		std::memmove(&k.patternFlags[p], &k.patternFlags[p + 1], k.numPatterns - p - 1);
		k.numPatterns--;
		s.layout();
	}

	// Pattern p ends before its step pos; the rest becomes a new pattern after it
	bool splitPattern(int t, int p, int pos) {
		Track &k = tr(t);
		if (p < 0 || p >= k.numPatterns || pos <= 0 || pos >= k.patternLength[p] || k.numPatterns >= kMaxPatterns)
			return false;
		const int rest = k.patternLength[p] - pos;
		std::memmove(&k.patternLength[p + 2], &k.patternLength[p + 1], k.numPatterns - p - 1);
		std::memmove(&k.patternFlags[p + 2], &k.patternFlags[p + 1], k.numPatterns - p - 1);
		k.patternLength[p] = (uint8_t)pos;
		k.patternLength[p + 1] = (uint8_t)rest;
		k.patternFlags[p + 1] = k.patternFlags[p];
		k.numPatterns++;
		s.layout();
		return true;
	}

	// Pattern p and the next one become one
	bool joinPatterns(int t, int p) {
		Track &k = tr(t);
		if (p < 0 || p + 1 >= k.numPatterns || k.patternLength[p] + k.patternLength[p + 1] > kMaxPatternSteps)
			return false;
		k.patternLength[p] = (uint8_t)(k.patternLength[p] + k.patternLength[p + 1]);
		std::memmove(&k.patternLength[p + 1], &k.patternLength[p + 2], k.numPatterns - p - 2);
		std::memmove(&k.patternFlags[p + 1], &k.patternFlags[p + 2], k.numPatterns - p - 2);
		k.numPatterns--;
		s.layout();
		return true;
	}

	// The step becomes two with the same values; the first gets the longer half of the duration
	bool splitStep(int t, int i) {
		Track &k = tr(t);
		if (i < 0 || i >= k.numSteps)
			return false;
		Step a = k.steps[i];
		const int d = a.v[DUR];
		Step b = a;
		a.v[DUR] = (uint8_t)((d + 1) / 2);
		b.v[DUR] = (uint8_t)(d / 2);
		a.v[GATE] = (uint8_t)std::min<int>(a.v[GATE], a.v[DUR]);
		b.v[GATE] = (uint8_t)std::min<int>(b.v[GATE], b.v[DUR]);
		const int p = k.stepPattern[i];
		if (!insertSteps(t, p, i - k.patternStart[p] + 1, &b, 1))
			return false;
		const uint32_t uid = k.steps[i].uid;
		k.steps[i] = a;
		k.steps[i].uid = uid;
		return true;
	}

	// Moves the boundary between step i and the next: what one gains the other loses
	bool swing(int t, int i, int delta) {
		Track &k = tr(t);
		if (i < 0 || i + 1 >= k.numSteps)
			return false;
		Step &a = k.steps[i], &b = k.steps[i + 1];
		delta = std::max(delta, -(int)a.v[DUR]);
		delta = std::min(delta, (int)b.v[DUR]);
		delta = std::min(delta, kMaxValue - a.v[DUR]);
		delta = std::max(delta, -(kMaxValue - b.v[DUR]));
		if (!delta)
			return false;
		a.v[DUR] = (uint8_t)(a.v[DUR] + delta);
		b.v[DUR] = (uint8_t)(b.v[DUR] - delta);
		return true;
	}

	int transformValue(const Transform &m, int f, int v) {
		return transformField(m, f, v, true, [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); });
	}

	void applyMath(int t, int first, int n, const Transform &m, int group = -1) {
		Track &k = tr(t);
		for (int i = std::max(0, first); i < std::min(first + n, (int)k.numSteps); i++) {
			if (group >= 0 && !(k.steps[i].groups & (1 << group)))
				continue;
			for (int f = 0; f < NUM_FIELDS; f++)
				k.steps[i].v[f] = (uint8_t)transformValue(m, f, k.steps[i].v[f]);
		}
	}

	// A row of the grid: a field, or a flag (SMOOTH_A, SMOOTH_B, RATCHET) given as NUM_FIELDS + its bit's position
	static int rowValue(const Step &st, int row) {
		return row < NUM_FIELDS ? st.v[row] : (st.flags >> (row - NUM_FIELDS)) & 1;
	}
	static void setRowValue(Step &st, int row, int v) {
		if (row < NUM_FIELDS)
			st.v[row] = (uint8_t)std::max(0, std::min(kMaxValue, v));
		else if (v)
			st.flags |= (uint8_t)(1 << (row - NUM_FIELDS));
		else
			st.flags &= (uint8_t)~(1 << (row - NUM_FIELDS));
	}

	void reverse(int t, int first, int n, int row) {
		Track &k = tr(t);
		n = std::min(n, k.numSteps - first);
		for (int a = first, b = first + n - 1; a < b; a++, b--) {
			const int va = rowValue(k.steps[a], row), vb = rowValue(k.steps[b], row);
			setRowValue(k.steps[a], row, vb);
			setRowValue(k.steps[b], row, va);
		}
	}

	// Shifts one row's values by one step: later (dir 1) or earlier (dir -1), wrapping within the range
	void rotate(int t, int first, int n, int row, int dir) {
		Track &k = tr(t);
		n = std::min(n, k.numSteps - first);
		if (n < 2)
			return;
		if (dir > 0) {
			const int last = rowValue(k.steps[first + n - 1], row);
			for (int i = first + n - 1; i > first; i--)
				setRowValue(k.steps[i], row, rowValue(k.steps[i - 1], row));
			setRowValue(k.steps[first], row, last);
		}
		else {
			const int head = rowValue(k.steps[first], row);
			for (int i = first; i < first + n - 1; i++)
				setRowValue(k.steps[i], row, rowValue(k.steps[i + 1], row));
			setRowValue(k.steps[first + n - 1], row, head);
		}
	}

	// ---- Group membership, on the steps first..first+n-1 of track t
	void setMembers(int t, int first, int n, int g, bool in) {
		Track &k = tr(t);
		for (int i = std::max(0, first); i < std::min(first + n, (int)k.numSteps); i++) {
			if (in)
				k.steps[i].groups |= (uint16_t)(1 << g);
			else
				k.steps[i].groups &= (uint16_t)~(1 << g);
		}
	}
	void invertMembers(int t, int first, int n, int g) {
		Track &k = tr(t);
		for (int i = std::max(0, first); i < std::min(first + n, (int)k.numSteps); i++)
			k.steps[i].groups ^= (uint16_t)(1 << g);
	}
	// later (dir 1) or earlier by one step, over the whole track
	void rotateMembers(int t, int g, int dir) {
		Track &k = tr(t);
		if (k.numSteps < 2)
			return;
		const uint16_t bit = (uint16_t)(1 << g);
		std::vector<bool> in(k.numSteps);
		for (int i = 0; i < k.numSteps; i++)
			in[i] = k.steps[i].groups & bit;
		for (int i = 0; i < k.numSteps; i++) {
			const bool b = in[(i - dir + k.numSteps) % k.numSteps];
			k.steps[i].groups = (uint16_t)(b ? (k.steps[i].groups | bit) : (k.steps[i].groups & ~bit));
		}
	}
	// A Euclidean mask E(hits, length) repeated over the range: steps under a 1 join the group, the others leave it
	void euclidMembers(int t, int first, int n, int g, int hits, int length) {
		Track &k = tr(t);
		length = std::max(1, std::min(99, length));
		hits = std::max(0, std::min(hits, length));
		for (int j = 0; j < n && first + j < k.numSteps; j++) {
			const int pos = j % length;
			// the evenly spaced form of Bjorklund's pattern, starting on a hit
			const bool on = hits > 0 && ((pos * hits) % length) < hits;
			Step &st = k.steps[first + j];
			st.groups = (uint16_t)(on ? (st.groups | (1 << g)) : (st.groups & ~(1 << g)));
		}
	}
	// adds group src's steps to group dst (the union), on track t
	void unionMembers(int t, int src, int dst) {
		Track &k = tr(t);
		for (int i = 0; i < k.numSteps; i++)
			if (k.steps[i].groups & (1 << src))
				k.steps[i].groups |= (uint16_t)(1 << dst);
	}
};

// The Euclidean mask, also for the screen
inline bool euclidHit(int pos, int hits, int length) {
	return hits > 0 && length > 0 && ((pos * hits) % length) < hits;
}

// ---- The hand-over from the UI to the audio thread: a triple buffer. The UI fills its back buffer and swaps it in;
// the audio thread takes the newest when it is ready to (at once, or at the end of a step, pattern or loop)
struct Handoff {
	std::unique_ptr<Song> buf[3];
	std::atomic<int> state{1};        // the middle buffer, | 4 when it is newer than the audio's
	std::atomic<int> commitAt{COMMIT_NOW}, commitTrack{0};
	int back = 0, front = 2;          // the UI's and the audio's

	Handoff() {
		for (auto &b : buf)
			b.reset(new Song);
	}
	// UI
	Song &writeBuffer() { return *buf[back]; }
	void publish(int at = COMMIT_NOW, int track = 0) {
		commitAt.store(at);
		commitTrack.store(track);
		back = state.exchange(back | 4) & 3;
	}
	// Audio
	bool pending() const { return state.load() & 4; }
	Song &song() { return *buf[front]; }
	void take() { front = state.exchange(front) & 3; }
};

// ---- What the audio records, for the UI to write into the document: a single-producer, single-consumer queue
enum RecEventType : uint8_t { EV_TAKE, EV_NOTE, EV_INSERT, EV_DELETE, EV_ALTER };
struct RecEvent {
	uint8_t type = EV_NOTE, track = 0, a = 0, b = 0;
	int32_t dur = 0, gate = 0;     // in pulses; a note may be longer than 99 (the UI ties steps)
	uint32_t uid = 0;              // EV_ALTER: the step
	uint8_t mask = 0;              // EV_ALTER: which fields (bit f)
};

struct RecQueue {
	static constexpr int kSize = 1024;
	RecEvent ev[kSize];
	std::atomic<int> head{0}, tail{0};
	bool push(const RecEvent &e) {
		const int h = head.load(std::memory_order_relaxed), n = (h + 1) % kSize;
		if (n == tail.load(std::memory_order_acquire))
			return false;
		ev[h] = e;
		head.store(n, std::memory_order_release);
		return true;
	}
	bool pop(RecEvent &e) {
		const int t = tail.load(std::memory_order_relaxed);
		if (t == head.load(std::memory_order_acquire))
			return false;
		e = ev[t];
		tail.store((t + 1) % kSize, std::memory_order_release);
		return true;
	}
};

// ---- Playback
struct Engine {
	struct Io {
		// rising edges this sample
		bool clock = false, reset = false, run = false, partGo = false;
		bool partGoHigh = false;      // GO held high: the cued part follows PART
		int partCv = -1;              // the part PART's voltage selects, -1 unplugged
		float transpose[kTracks] = {};
		// the modulation bus
		float modCv[kChannels] = {};
		bool modHigh[kChannels] = {};
		// recording inputs: CV A, CV B (A-1, A-2), the gate (or GATE value) and DUR value (AD-1, AD-2), INSERT and
		// DELETE triggers (D-1, D-2, rising edges), PUNCH (held: punched in)
		float recA = 0.f, recB = 0.f, recGateV = 0.f, recDurV = 0.f;
		bool recAOn = false, recBOn = false, recGateOn = false, recDurOn = false;
		bool recInsert = false, recDelete = false;
		bool punch = false;
		// outputs
		float a[kTracks] = {}, b[kTracks] = {}, gate[kTracks] = {};
	};

	enum Arm { ARM_NONE, ARM_RESET, ARM_UID };

	// A note being recorded in real time, in pulses of its track
	struct Take {
		bool on = false;              // a take is running (from punch in or the first note)
		bool note = false;            // a note is being recorded
		double start = 0.0, fall = -1.0;
		int a = 0, b = 0;
		int64_t sampleAt = -1;        // when its CVs are read: half a pulse after it started
		bool thru = false;            // the inputs go to the outputs (monitoring)
		bool gateWas = false;
	};

	struct Play {
		int step = 0, next = 0;       // indices in the song the audio plays
		uint32_t uid = 0;
		int pulse = 0;                // pulses since the step began
		uint8_t e[NUM_FIELDS] = {};   // the step's values as it plays: groups' transforms, slopes, recording applied
		int arm = ARM_RESET;          // what the next pulse starts
		uint32_t armUid = 0;
		bool idle = true;             // nothing to play: no step with a duration
		bool looped = false;          // a loop ended since the part began
		// the track's clock
		int divCount = 0;
		int sub = 0;                  // the multiplied pulses already given since the last external one
		int64_t lastTick = -1;
		int64_t ticks = 0;            // pulses since the start, for recording
		int trigLeft = 0;             // samples left of a trigger
		Take take;
	};

	Handoff handoff;
	RecQueue recQueue;
	Play play[kTracks];
	double sampleRate = 44100.0;
	int64_t now = 0;                  // samples since start
	int64_t lastEdge = -1, period = 0;
	int playingPart = 1, pendingPart = -1;
	bool pendingReset = false;
	bool punchWas = false;
	uint32_t rng = 0x9e3779b9u;
	const Io *io = nullptr;           // this sample's inputs, while process() runs
	std::atomic<bool> running{true};
	std::atomic<bool> armed[kTracks];
	std::atomic<int> uiGoPart{-1};    // the UI cues a part
	std::atomic<int> jumpTrack{-1};   // the UI moves a play cursor (FOLLOW)
	std::atomic<uint32_t> jumpUid{0};
	std::atomic<bool> resetRequest{false};
	std::atomic<int> setPart{-1};     // a loaded patch's playing part, without a transition
	// for the UI
	std::atomic<uint32_t> shownUid[kTracks];
	std::atomic<int> shownPulse[kTracks], shownPart{1}, shownPending{-1};
	std::atomic<bool> shownCommitPending{false}, shownResetPending{false}, shownRecording{false};
	std::atomic<float> shownRec[4];   // recording inputs: CV A, CV B, gate value, duration value
	std::atomic<bool> shownMod[kChannels];
	std::atomic<float> shownModCv[kChannels];

	Engine() {
		for (int t = 0; t < kTracks; t++) {
			shownUid[t] = 0;
			shownPulse[t] = 0;
			armed[t] = false;
		}
		for (auto &r : shownRec)
			r = 0.f;
		for (int c = 0; c < kChannels; c++) {
			shownMod[c] = false;
			shownModCv[c] = 0.f;
		}
	}

	Song &song() { return handoff.song(); }

	int rnd(int lo, int hi) {
		rng ^= rng << 13;
		rng ^= rng >> 17;
		rng ^= rng << 5;
		return lo + (int)(rng % (uint32_t)(hi - lo + 1));
	}

	int loopStart(int t) { return song().partLoopStart[playingPart][t]; }
	int loopEnd(int t) { return song().partLoopEnd[playingPart][t]; }

	// The step after i, and whether that ends a loop
	int nextIndex(int t, int i, bool &wrapped) {
		const Track &k = song().tracks[t];
		const int ls = std::max(0, (int)loopStart(t)), le = loopEnd(t);
		wrapped = (le >= 0 && i == le) || i + 1 >= k.numSteps;
		return wrapped ? ls : i + 1;
	}

	// The next step with a duration after i (i itself when none), for smoothing towards it
	int findPlayable(int t, int i) {
		const Track &k = song().tracks[t];
		int j = i;
		for (int tries = 0; tries <= k.numSteps; tries++) {
			bool w;
			j = nextIndex(t, j, w);
			if (k.steps[j].v[DUR] > 0)
				return j;
		}
		return i;
	}

	bool recordingIn(int t, int mode) {
		return io && armed[t].load(std::memory_order_relaxed) && io->punch && running.load(std::memory_order_relaxed) && song().rec.mode == mode;
	}

	// ALTER: the step about to play takes the recording inputs' values, in the song the audio plays and, through an
	// event, in the document
	void alter(int t, int i) {
		Track &k = song().tracks[t];
		Step &st = k.steps[i];
		RecEvent e;
		e.type = EV_ALTER;
		e.track = (uint8_t)t;
		e.uid = st.uid;
		if (io->recAOn) { st.v[CV_A] = (uint8_t)nearestIndex(k.table[0], io->recA); e.mask |= 1; }
		if (io->recBOn) { st.v[CV_B] = (uint8_t)nearestIndex(k.table[1], io->recB); e.mask |= 2; }
		if (io->recDurOn) { st.v[DUR] = (uint8_t)std::max(0, std::min(kMaxValue, (int)std::floor(io->recDurV * 20.f))); e.mask |= 4; }
		if (io->recGateOn) { st.v[GATE] = (uint8_t)std::max(0, std::min(kMaxValue, (int)std::floor(io->recGateV * 20.f))); e.mask |= 8; }
		if (!e.mask)
			return;
		e.a = st.v[CV_A];
		e.b = st.v[CV_B];
		e.dur = st.v[DUR];
		e.gate = st.v[GATE];
		recQueue.push(e);
	}

	// Starts step i: its values as it will play (recording, then the groups); false when that makes it last 0 pulses
	bool begin(int t, int i) {
		Play &p = play[t];
		Track &k = song().tracks[t];
		if (recordingIn(t, REC_ALTER))
			alter(t, i);
		const Step &st = k.steps[i];
		for (int f = 0; f < NUM_FIELDS; f++)
			p.e[f] = st.v[f];
		if (st.groups && io) {
			for (int g = 0; g < kGroups; g++) {
				if (!(st.groups & (1 << g)))
					continue;
				const Group &gr = song().groups[g];
				for (int c = 0; c < kChannels; c++) {
					const Transform &m = io->modHigh[c] ? gr.high[c] : gr.low[c];
					if (!m.identity())
						for (int f = 0; f < NUM_FIELDS; f++)
							p.e[f] = (uint8_t)transformField(m, f, p.e[f], false, [&](int lo, int hi) { return rnd(lo, hi); });
					for (int f = DUR; f <= GATE; f++)
						if (gr.slope[c][f] != 0.f)
							p.e[f] = (uint8_t)std::max(0, std::min(kMaxValue, p.e[f] + (int)std::lround(gr.slope[c][f] * io->modCv[c])));
				}
			}
		}
		if (p.e[DUR] == 0)
			return false;
		p.idle = false;
		p.step = i;
		p.uid = st.uid;
		p.pulse = 0;
		p.next = findPlayable(t, i);
		if (k.triggers())
			fireTrigger(t, 0);
		return true;
	}

	// From i (or the first step after it that plays). Sets idle when none does
	bool startAt(int t, int i, bool &wrapped) {
		Play &p = play[t];
		const Track &k = song().tracks[t];
		wrapped = false;
		if (k.numSteps <= 0) {
			p.idle = true;
			return false;
		}
		i = std::max(0, std::min(i, k.numSteps - 1));
		for (int tries = 0; tries <= k.numSteps; tries++) {
			if ((k.steps[i].v[DUR] > 0 || recordingIn(t, REC_ALTER)) && begin(t, i))
				return true;
			bool w;
			i = nextIndex(t, i, w);
			wrapped = wrapped || w;
		}
		p.idle = true;
		return false;
	}

	void fireTrigger(int t, int pulse) {
		Play &p = play[t];
		const Step &st = song().tracks[t].steps[p.step];
		const int g = p.e[GATE], d = p.e[DUR];
		if (g <= 0 || d <= 0)
			return;
		if (st.flags & RATCHET) {
			// GATE triggers of 0.5 ms spread over the step, on whole pulses
			const int n = std::min(g, d);
			const int k = (pulse * n + d - 1) / d;
			if (k < n && (k * d) / n == pulse)
				p.trigLeft = std::max(1, (int)std::lround(0.0005 * sampleRate));
		}
		else if (pulse == 0)
			p.trigLeft = std::max(1, (int)std::lround(g * 0.0001 * sampleRate));
	}

	// One pulse of track t's clock. Returns what it ended: 1 a step, 2 a pattern, 3 a loop
	int tick(int t) {
		Play &p = play[t];
		const Track &k = song().tracks[t];
		p.lastTick = now;
		p.ticks++;
		if (p.arm != ARM_NONE) {
			int target = 0;
			if (p.arm == ARM_UID)
				target = std::max(0, k.indexOf(p.armUid));
			else
				target = std::max(0, (int)song().partReset[playingPart][t]);
			p.arm = ARM_NONE;
			bool w;
			startAt(t, target, w);
			return 0;
		}
		if (p.idle) {
			bool w;
			startAt(t, std::max(0, (int)loopStart(t)), w);
			return 0;
		}
		p.pulse++;
		if (p.pulse < p.e[DUR]) {
			if (k.triggers())
				fireTrigger(t, p.pulse);
			return 0;
		}
		const int from = p.step;
		bool wrapped = false;
		int i = from;
		bool started = false;
		for (int tries = 0; tries <= k.numSteps && !started; tries++) {
			bool w;
			i = nextIndex(t, i, w);
			wrapped = wrapped || w;
			started = (k.steps[i].v[DUR] > 0 || recordingIn(t, REC_ALTER)) && begin(t, i);
		}
		if (!started) {
			p.idle = true;
			return 0;
		}
		if (wrapped) {
			p.looped = true;
			return 3;
		}
		return k.stepPattern[from] != k.stepPattern[p.step] ? 2 : 1;
	}

	// After the audio takes a new song: every cursor back on its step
	void remap() {
		for (int t = 0; t < kTracks; t++) {
			Play &p = play[t];
			const Track &k = song().tracks[t];
			if (k.numSteps <= 0) {
				p.idle = true;
				continue;
			}
			if (p.idle)
				continue;
			int i = k.indexOf(p.uid);
			if (i < 0)
				i = std::min(p.step, k.numSteps - 1);
			p.step = i;
			p.uid = k.steps[i].uid;
			p.next = findPlayable(t, i);
		}
	}

	// RESET: the next pulse starts each track's reset step
	void doReset() {
		pendingReset = false;
		for (int t = 0; t < kTracks; t++) {
			Play &p = play[t];
			p.arm = ARM_RESET;
			p.looped = false;
			p.divCount = 0;
			p.sub = 1 << 20;          // no multiplied pulses until the next external one
			p.trigLeft = 0;
			endTake(t);
		}
	}

	// A quantized RESET, at the end of a step, pattern or loop: the tracks that pulsed now start their reset step now
	void resetNow(const bool *ticked) {
		pendingReset = false;
		for (int t = 0; t < kTracks; t++) {
			Play &p = play[t];
			p.looped = false;
			endTake(t);
			if (ticked[t]) {
				bool w;
				startAt(t, std::max(0, (int)song().partReset[playingPart][t]), w);
			}
			else
				p.arm = ARM_RESET;
		}
	}

	void switchPart(int part, bool withReset, const bool *ticked) {
		playingPart = part;
		pendingPart = -1;
		for (int t = 0; t < kTracks; t++) {
			Play &p = play[t];
			p.looped = false;
			if (!withReset || song().partReset[part][t] < 0)
				continue;
			if (ticked[t]) {
				bool w;
				startAt(t, song().partReset[part][t], w);
			}
			else
				p.arm = ARM_RESET;
		}
		// a cursor outside the new loop plays on to its end, as the hardware does
	}

	float tableValue(const Track &k, int ab, int idx, int offset) {
		return k.table[ab][std::max(0, std::min(kMaxValue, idx + offset))];
	}

	// ---- Real-time recording
	double pulsePeriod(int t) {
		const Track &k = song().tracks[t];
		return period > 0 ? (double)period * k.div / k.mult : 0.0;
	}
	// where track t is now, in pulses with a fraction
	double position(int t) {
		const Play &p = play[t];
		const double pp = pulsePeriod(t);
		const double frac = (pp > 0 && p.lastTick >= 0) ? std::min(0.999, (now - p.lastTick) / pp) : 0.0;
		return (double)p.ticks + frac;
	}
	static double quantize(double x, int q) { return std::round(x / q) * q; }

	void finishNote(int t, double end) {
		Take &tk = play[t].take;
		if (!tk.note)
			return;
		tk.note = false;
		const RecSettings &r = song().rec;
		const double qs = quantize(tk.start, r.durQ[t]), qe = quantize(end, r.durQ[t]);
		RecEvent e;
		e.type = EV_NOTE;
		e.track = (uint8_t)t;
		e.a = (uint8_t)tk.a;
		e.b = (uint8_t)tk.b;
		e.dur = (int32_t)std::lround(qe - qs);
		const double held = (tk.fall >= 0 ? tk.fall : end) - tk.start;
		e.gate = tk.fall >= 0 ? (int32_t)std::lround(quantize(held, r.gateQ[t])) : e.dur;
		if (e.dur > 0)
			recQueue.push(e);
	}

	void startNote(int t, double at, bool rest) {
		Take &tk = play[t].take;
		const Track &k = song().tracks[t];
		tk.note = true;
		tk.start = at;
		tk.fall = rest ? at : -1.0;
		tk.a = nearestIndex(k.table[0], io->recAOn ? io->recA : 0.f);
		tk.b = nearestIndex(k.table[1], io->recBOn ? io->recB : 0.f);
		const double pp = pulsePeriod(t);
		tk.sampleAt = now + (pp > 0 ? (int64_t)(pp * 0.5) : 0);
	}

	void endTake(int t) {
		Take &tk = play[t].take;
		if (tk.on && io)
			finishNote(t, position(t));
		tk.on = false;
		tk.note = false;
		tk.thru = false;
	}

	void recordRealtime(int t) {
		Take &tk = play[t].take;
		const bool gate = io->recGateOn && io->recGateV > 1.f;
		const bool rise = gate && !tk.gateWas, fall = !gate && tk.gateWas;
		tk.gateWas = gate;
		if (!recordingIn(t, REC_REALTIME)) {
			if (tk.on)
				endTake(t);
			// rehearsal: an armed, running track plays its inputs once a note comes in
			if (armed[t].load(std::memory_order_relaxed) && running.load(std::memory_order_relaxed) && song().rec.mode == REC_REALTIME && rise)
				tk.thru = true;
			if (!armed[t].load(std::memory_order_relaxed) || !running.load(std::memory_order_relaxed))
				tk.thru = false;
			return;
		}
		tk.thru = true;
		const double pos = position(t);
		if (!tk.on) {
			if (song().rec.waitFirst && !rise)
				return;
			tk.on = true;
			RecEvent e;
			e.type = EV_TAKE;
			e.track = (uint8_t)t;
			recQueue.push(e);
			if (!rise) {
				startNote(t, pos, true);   // a take that starts with a rest
				return;
			}
		}
		if (rise) {
			finishNote(t, pos);
			startNote(t, pos, false);
			return;
		}
		if (fall && tk.note && tk.fall < 0)
			tk.fall = pos;
		// read the CVs half a pulse in; a change of CV A (or B) while the gate is held starts a legato step
		if (tk.note && now >= tk.sampleAt && (now - tk.sampleAt) % 32 == 0) {
			const Track &k = song().tracks[t];
			const int a = nearestIndex(k.table[0], io->recAOn ? io->recA : 0.f);
			const int b = nearestIndex(k.table[1], io->recBOn ? io->recB : 0.f);
			if (now == tk.sampleAt) {
				tk.a = a;
				tk.b = b;
			}
			else if (gate && tk.fall < 0 && (((song().rec.trig[t] & 1) && a != tk.a) || ((song().rec.trig[t] & 2) && b != tk.b))) {
				finishNote(t, pos);
				startNote(t, pos, false);
				tk.a = a;
				tk.b = b;
			}
		}
	}

	void process(Io &in) {
		io = &in;
		Song *s = &song();
		if (handoff.pending() && handoff.commitAt.load() == COMMIT_NOW) {
			handoff.take();
			s = &song();
			remap();
		}
		if (resetRequest.exchange(false))
			in.reset = true;
		const int sp = setPart.exchange(-1);
		if (sp >= 0 && sp < kParts) {
			playingPart = sp;
			pendingPart = -1;
		}
		if (in.run)
			running = !running;
		const int jt = jumpTrack.exchange(-1);
		if (jt >= 0 && jt < kTracks) {
			play[jt].arm = ARM_UID;
			play[jt].armUid = jumpUid.load();
		}
		// parts cued by GO (from PART, or the UI's part when PART is unplugged) or by the UI
		const int uiPart = uiGoPart.exchange(-1);
		int cue = -1;
		if (in.partGo)
			cue = in.partCv >= 0 ? in.partCv : -2;
		else if (in.partGoHigh && in.partCv >= 0 && pendingPart >= 0)
			cue = in.partCv;
		if (uiPart >= 0)
			cue = uiPart;
		bool ticked[kTracks] = {};
		if (cue >= 0 && cue < kParts) {
			if (s->transition == NOW)
				switchPart(cue, false, ticked);
			else
				pendingPart = cue;
		}
		if (in.reset) {
			if (s->resetAt == COMMIT_NOW)
				doReset();
			else
				pendingReset = true;
		}
		// punching out ends the takes
		if (punchWas && !in.punch)
			for (int t = 0; t < kTracks; t++)
				endTake(t);
		punchWas = in.punch;

		// the clocks: an external pulse, then each track's multiplied pulses, then its divider
		int tickCount[kTracks] = {};
		if (in.clock) {
			if (lastEdge >= 0)
				period = now - lastEdge;
			lastEdge = now;
			for (int t = 0; t < kTracks; t++)
				tickCount[t]++;
		}
		for (int t = 0; t < kTracks; t++) {
			Play &p = play[t];
			const int m = s->tracks[t].mult;
			if (in.clock)
				p.sub = 1;
			// several sub-pulses can fall on one sample when the period is shorter than the multiplier
			while (m > 1 && period > 0 && p.sub < m && lastEdge + (period * p.sub) / m == now) {
				tickCount[t]++;
				p.sub++;
			}
		}
		const bool run = running.load();
		if (!run)
			for (int t = 0; t < kTracks; t++)
				if (play[t].take.on)
					endTake(t);
		int ended[kTracks] = {};
		bool wrappedAny = false;
		for (int t = 0; t < kTracks; t++) {
			Play &p = play[t];
			for (int c = 0; c < tickCount[t]; c++) {
				const int div = s->tracks[t].div;
				const bool pass = (p.divCount % div) == 0;
				p.divCount = (p.divCount + 1) % div;
				if (!pass || !run)
					continue;
				ticked[t] = true;
				ended[t] = std::max(ended[t], tick(t));
				if (ended[t] == 3)
					wrappedAny = true;
			}
		}
		// a quantized reset at the end of a step, pattern or loop of its track
		if (pendingReset && ended[s->resetTrack] >= s->resetAt)
			resetNow(ticked);
		// a cued part starts at the end of a loop: the first one to end (FIRST), or once every track has ended one
		if (pendingPart >= 0 && wrappedAny && s->transition != NOW) {
			bool go = s->transition == FIRST;
			if (!go) {
				go = true;
				for (int t = 0; t < kTracks; t++)
					if (!play[t].idle && !play[t].looped)
						go = false;
			}
			if (go)
				switchPart(pendingPart, true, ticked);
		}
		// a held commit (HOLD mode): at the end of a step, pattern or loop of its track
		if (handoff.pending()) {
			const int at = handoff.commitAt.load(), ct = handoff.commitTrack.load();
			if (at == COMMIT_NOW || (ct >= 0 && ct < kTracks && ended[ct] >= at)) {
				handoff.take();
				s = &song();
				remap();
			}
		}
		// recording
		bool recording = false;
		for (int t = 0; t < kTracks; t++) {
			if (!armed[t].load(std::memory_order_relaxed) && play[t].take.on)
				endTake(t);
			recordRealtime(t);
			recording = recording || play[t].take.on;
		}
		if (in.punch && s->rec.mode == REC_STEP && run)
			for (int t = 0; t < kTracks; t++)
				if (armed[t].load(std::memory_order_relaxed) && (in.recInsert || in.recDelete)) {
					RecEvent e;
					e.type = in.recInsert ? EV_INSERT : EV_DELETE;
					e.track = (uint8_t)t;
					recQueue.push(e);
					recording = true;
				}

		// outputs
		for (int t = 0; t < kTracks; t++) {
			Play &p = play[t];
			const Track &k = s->tracks[t];
			if (p.take.thru) {
				in.a[t] = in.recAOn ? in.recA : 0.f;
				in.b[t] = in.recBOn ? in.recB : 0.f;
				in.gate[t] = (in.recGateOn && in.recGateV > 1.f) ? kGateVolts : 0.f;
				shownUid[t].store(p.idle ? 0 : p.uid, std::memory_order_relaxed);
				continue;
			}
			const int semis = (int)std::lround(in.transpose[t] * 12.f);
			if (k.numSteps <= 0) {
				in.a[t] = in.b[t] = in.gate[t] = 0.f;
				continue;
			}
			const int i = std::min(p.step, k.numSteps - 1);
			const Step &st = k.steps[i];
			const Step &nx = k.steps[std::min(p.next, k.numSteps - 1)];
			const int ea = p.idle ? st.v[CV_A] : p.e[CV_A], eb = p.idle ? st.v[CV_B] : p.e[CV_B];
			float va = tableValue(k, 0, ea, semis), vb = tableValue(k, 1, eb, 0);
			const int d = p.e[DUR], g = p.e[GATE];
			// smoothing: a line to the next step's value over the end of the step (after the gate, or over its second
			// half for triggers), timed in samples from the track's pulse period
			const double rampStart = k.triggers() ? d * 0.5 : (double)g;
			if (!p.idle && rampStart < d) {
				const double pp = pulsePeriod(t);
				const double frac = (pp > 0 && p.lastTick >= 0) ? std::min(1.0, (now - p.lastTick) / pp) : 0.0;
				const double pos = p.pulse + frac;
				if (pos > rampStart) {
					const float f = (float)std::min(1.0, (pos - rampStart) / (d - rampStart));
					if (k.smooth(i, 0))
						va += (tableValue(k, 0, nx.v[CV_A], semis) - va) * f;
					if (k.smooth(i, 1))
						vb += (tableValue(k, 1, nx.v[CV_B], 0) - vb) * f;
				}
			}
			// the groups' slopes: the bus's CVs added to CV A and B, live
			if (st.groups && !p.idle)
				for (int gi = 0; gi < kGroups; gi++)
					if (st.groups & (1 << gi))
						for (int c = 0; c < kChannels; c++) {
							va += s->groups[gi].slope[c][CV_A] * in.modCv[c];
							vb += s->groups[gi].slope[c][CV_B] * in.modCv[c];
						}
			in.a[t] = va;
			in.b[t] = vb;
			bool high = false;
			if (!p.idle && run && playingPart != 0 && p.arm == ARM_NONE) {
				if (k.triggers())
					high = p.trigLeft > 0;
				else if (g > 0)
					high = (st.flags & RATCHET) ? ((p.pulse / g) % 2 == 0) : p.pulse < g;
			}
			if (p.trigLeft > 0)
				p.trigLeft--;
			in.gate[t] = high ? kGateVolts : 0.f;
			shownUid[t].store(p.idle ? 0 : p.uid, std::memory_order_relaxed);
			shownPulse[t].store(p.pulse, std::memory_order_relaxed);
		}
		shownPart.store(playingPart, std::memory_order_relaxed);
		shownPending.store(pendingPart, std::memory_order_relaxed);
		shownCommitPending.store(handoff.pending(), std::memory_order_relaxed);
		shownResetPending.store(pendingReset, std::memory_order_relaxed);
		shownRecording.store(recording || (in.punch && run), std::memory_order_relaxed);
		if ((now & 63) == 0) {
			shownRec[0].store(in.recA, std::memory_order_relaxed);
			shownRec[1].store(in.recB, std::memory_order_relaxed);
			shownRec[2].store(in.recGateV, std::memory_order_relaxed);
			shownRec[3].store(in.recDurV, std::memory_order_relaxed);
			for (int c = 0; c < kChannels; c++) {
				shownMod[c].store(in.modHigh[c], std::memory_order_relaxed);
				shownModCv[c].store(in.modCv[c], std::memory_order_relaxed);
			}
		}
		now++;
		io = nullptr;
	}
};

// ---- Recorded notes into the document (UI): a note longer than 99 pulses becomes tied steps
inline int noteSteps(int dur, int gate, Step proto, Step *out, int max) {
	int n = 0;
	while (dur > 0 && n < max) {
		const int d = std::min(dur, kMaxValue);
		Step s = proto;
		s.v[DUR] = (uint8_t)d;
		// the gate goes on over the next step: tie it (gate = duration)
		s.v[GATE] = (uint8_t)(gate > d ? d : std::max(0, gate));
		out[n++] = s;
		dur -= d;
		gate -= d;
	}
	return n;
}

// ---- MIDI files: SMF 0 and 1. Channels 1 to 4 go to tracks 1 to 4, monophonic (a new note ends the one playing).
// A step lasts until the next note starts; its gate is the note's length; silences become rests. Pitch is the index
// nearest the note in table A (C4 = 0 V = note 60), velocity the index nearest velocity / 127 x 10 V in table B. A
// new pattern starts on each bar.
struct MidiNote {
	int64_t start, end;
	int key, vel;
};

inline bool readMidi(const uint8_t *d, size_t n, std::vector<MidiNote> notes[kTracks], int &division) {
	size_t i = 0;
	auto be32 = [&](size_t at) { return (uint32_t)d[at] << 24 | (uint32_t)d[at + 1] << 16 | (uint32_t)d[at + 2] << 8 | d[at + 3]; };
	if (n < 14 || std::memcmp(d, "MThd", 4) != 0)
		return false;
	const uint32_t hlen = be32(4);
	const int ntracks = d[10] << 8 | d[11];
	division = d[12] << 8 | d[13];
	if (division <= 0 || (division & 0x8000))
		return false;                 // SMPTE time is not read
	i = 8 + hlen;
	for (int tr = 0; tr < ntracks && i + 8 <= n; tr++) {
		if (std::memcmp(d + i, "MTrk", 4) != 0)
			return false;
		const size_t len = be32(i + 4), end = std::min(n, i + 8 + len);
		size_t j = i + 8;
		int64_t tick = 0;
		uint8_t status = 0;
		int64_t onAt[16][128];
		int onVel[16][128];
		for (auto &c : onAt)
			for (auto &k : c)
				k = -1;
		auto varlen = [&]() { uint32_t v = 0; while (j < end) { uint8_t b = d[j++]; v = (v << 7) | (b & 0x7f); if (!(b & 0x80)) break; } return v; };
		while (j < end) {
			tick += varlen();
			if (j >= end)
				break;
			uint8_t b = d[j];
			if (b & 0x80) {
				status = b;
				j++;
			}
			if (status == 0xff) {
				if (j >= end) break;
				j++;                       // meta type
				const uint32_t l = varlen();
				j += l;
				status = 0;
				continue;
			}
			if (status == 0xf0 || status == 0xf7) {
				const uint32_t l = varlen();
				j += l;
				status = 0;
				continue;
			}
			const int type = status & 0xf0, ch = status & 0x0f;
			const int nbytes = (type == 0xc0 || type == 0xd0) ? 1 : 2;
			if (j + nbytes > end)
				break;
			const int k = d[j], v = nbytes > 1 ? d[j + 1] : 0;
			j += nbytes;
			if (ch >= kTracks || (type != 0x90 && type != 0x80))
				continue;
			if (type == 0x90 && v > 0) {
				if (onAt[ch][k] >= 0)
					notes[ch].push_back({onAt[ch][k], tick, k, onVel[ch][k]});
				onAt[ch][k] = tick;
				onVel[ch][k] = v;
			}
			else if (onAt[ch][k] >= 0) {
				notes[ch].push_back({onAt[ch][k], tick, k, onVel[ch][k]});
				onAt[ch][k] = -1;
			}
		}
		i = i + 8 + len;
	}
	bool any = false;
	for (int t = 0; t < kTracks; t++) {
		std::sort(notes[t].begin(), notes[t].end(), [](const MidiNote &a, const MidiNote &b) { return a.start < b.start; });
		any = any || !notes[t].empty();
	}
	return any;
}

// Writes the notes of a MIDI file into the tracks it uses; pulsesPerBeat clock pulses a quarter note
inline bool importMidi(const uint8_t *d, size_t n, Song &s, int pulsesPerBeat, int beatsPerBar) {
	std::vector<MidiNote> notes[kTracks];
	int division = 0;
	if (!readMidi(d, n, notes, division))
		return false;
	const int64_t bar = (int64_t)pulsesPerBeat * std::max(1, beatsPerBar);
	auto pulses = [&](int64_t tick) { return (int64_t)std::llround((double)tick * pulsesPerBeat / division); };
	for (int t = 0; t < kTracks; t++) {
		if (notes[t].empty())
			continue;
		Track &k = s.tracks[t];
		std::vector<Step> steps;
		std::vector<int64_t> starts;
		Step proto;
		auto add = [&](int64_t at, int dur, int gate, int a, int b) {
			proto.v[CV_A] = (uint8_t)a;
			proto.v[CV_B] = (uint8_t)b;
			proto.flags = 0;
			proto.groups = 0;
			Step tmp[64];
			const int m = noteSteps(dur, gate, proto, tmp, 64);
			for (int j = 0; j < m; j++) {
				steps.push_back(tmp[j]);
				starts.push_back(at);
				at += tmp[j].v[DUR];
			}
		};
		int64_t pos = 0;
		auto &ns = notes[t];
		for (size_t q = 0; q < ns.size(); q++) {
			const int64_t st = pulses(ns[q].start);
			if (st < pos)
				continue;                  // a chord: only its first note
			const int a = nearestIndex(k.table[0], (ns[q].key - 60) / 12.f);
			const int b = nearestIndex(k.table[1], ns[q].vel / 127.f * 10.f);
			if (st > pos)
				add(pos, (int)(st - pos), 0, a, b);
			int64_t next = pulses(ns[q].end);
			for (size_t r = q + 1; r < ns.size(); r++)
				if (pulses(ns[r].start) > st) {
					next = std::max(pulses(ns[r].start), st + 1);
					break;
				}
			const int64_t endP = std::max(pulses(ns[q].end), st + 1);
			add(st, (int)(std::max(next, st + 1) - st), (int)(std::min(endP, next) - st), a, b);
			pos = std::max(next, st + 1);
		}
		// patterns on the bars, at most 100 steps each, at most 1000 steps
		k.numPatterns = 0;
		int64_t barOf = -1;
		int count = 0;
		for (size_t j = 0; j < steps.size() && count < kMaxTrackSteps; j++) {
			const int64_t b = starts[j] / bar;
			if (k.numPatterns == 0 || b != barOf || k.patternLength[k.numPatterns - 1] >= kMaxPatternSteps) {
				if (k.numPatterns >= kMaxPatterns)
					break;
				k.patternLength[k.numPatterns] = 0;
				k.patternFlags[k.numPatterns] = 0;
				k.numPatterns++;
				barOf = b;
			}
			k.steps[count] = steps[j];
			k.steps[count].uid = k.nextUid++;
			k.patternLength[k.numPatterns - 1]++;
			count++;
		}
		k.layout();
	}
	s.layout();
	return true;
}

// ---- Notes, for the screen: 0 V is C4
inline void noteName(float volts, char *out, size_t n, bool cents = true) {
	static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	const double semis = volts * 12.0;
	const long r = std::lround(semis);
	const int c = (int)std::lround((semis - r) * 100.0);
	const int octave = (int)std::floor(r / 12.0) + 4;
	const int k = (int)(((r % 12) + 12) % 12);
	if (cents && c != 0)
		std::snprintf(out, n, "%s%d%+d", names[k], octave, c);
	else
		std::snprintf(out, n, "%s%d", names[k], octave);
}

// Reads "1.25", "-0.5", "G#3", "Bb2", "c4+15" (cents); false when it is neither
inline bool parseVoltsOrNote(const char *s, float &out) {
	while (*s == ' ')
		s++;
	if (!*s)
		return false;
	const char c = (char)std::toupper((unsigned char)*s);
	if (c >= 'A' && c <= 'G') {
		static const int semis[7] = {9, 11, 0, 2, 4, 5, 7};
		int n = semis[c - 'A'];
		s++;
		if (*s == '#') { n++; s++; }
		else if (*s == 'b') { n--; s++; }
		char *end;
		const long oct = std::strtol(s, &end, 10);
		if (end == s)
			return false;
		s = end;
		double cents = 0.0;
		if (*s == '+' || *s == '-') {
			cents = std::strtod(s, &end);
			s = end;
		}
		if (*s)
			return false;
		out = clampVolts((float)((n + 12 * (oct - 4)) / 12.0 + cents / 1200.0));
		return true;
	}
	char *end;
	const double v = std::strtod(s, &end);
	if (end == s)
		return false;
	while (*end == ' ' || *end == 'v' || *end == 'V')
		end++;
	if (*end)
		return false;
	out = clampVolts((float)v);
	return true;
}

} // namespace registre
