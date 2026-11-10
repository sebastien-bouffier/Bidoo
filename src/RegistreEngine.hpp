// rEgIStRE's sequencer: four tracks of patterns of steps, each step holding indices into its track's two voltage tables
// (CV A and CV B), a duration and a gate in clock pulses, smoothing and ratchet. Loops and reset steps come from parts,
// which a CV and a gate switch at the end of a loop. No Rack here: tools/tests/registre_engine_test.cpp builds it alone.
//
// Threads: the UI edits its own Song (the document) and hands copies to the audio thread through a triple buffer, so
// process() never waits, locks or allocates. The play cursor follows a step by its uid, so edits that move steps under
// it keep it on the same step.
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
static constexpr int kMaxValue = 99;         // the largest index, duration and gate
static constexpr float kMinVolts = -5.f;
static constexpr float kMaxVolts = 10.f;
static constexpr float kGateVolts = 10.f;

enum Field { CV_A, CV_B, DUR, GATE, NUM_FIELDS };
enum StepFlag : uint8_t { SMOOTH_A = 1, SMOOTH_B = 2, RATCHET = 4 };
enum Transition { FIRST, LAST, NOW, NUM_TRANSITIONS };   // when a cued part starts: first loop end, last, at once
enum CommitAt { COMMIT_NOW, COMMIT_STEP, COMMIT_PATTERN, COMMIT_LOOP };

struct Step {
	uint32_t uid = 0;
	uint8_t v[NUM_FIELDS] = {48, 0, 4, 2};
	uint8_t flags = 0;
	bool operator==(const Step &o) const {
		return std::memcmp(v, o.v, sizeof(v)) == 0 && flags == o.flags;
	}
};

// MATH: per field, P' = QUANTIZE(G * (RD > 0 ? RANDOM(RD) : P) + JITTER(JT) + ADD, QT). g >= 1 multiplies, g <= -2
// divides by -g, g = 0 sets (with ADD). A result out of 0..99 leaves the value as it was.
struct Transform {
	int8_t rd[NUM_FIELDS], g[NUM_FIELDS], jt[NUM_FIELDS], add[NUM_FIELDS], qt[NUM_FIELDS];
	Transform() { clear(); }
	void clear() {
		for (int f = 0; f < NUM_FIELDS; f++) {
			rd[f] = 0; g[f] = 1; jt[f] = 0; add[f] = 0; qt[f] = 1;
		}
	}
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

// ---- Reference tables
enum RefTable { REF_12TET, REF_24TET, REF_SHRUTI, REF_MAJOR, REF_MINOR, REF_BLUES, REF_PENTA, REF_LIN10, REF_LIN5, REF_EXP10, NUM_REFS };
inline const char *refName(int r) {
	static const char *names[NUM_REFS] = {"12-TET", "24-TET", "22 SHRUTI", "MAJOR", "MINOR", "BLUES", "PENTA", "LIN 0-10", "LIN +-5", "EXP 0-10"};
	return (r >= 0 && r < NUM_REFS) ? names[r] : "";
}

inline float clampVolts(float v) { return std::min(kMaxVolts, std::max(kMinVolts, v)); }

// A scale repeated over octaves, degree 0 of octave `zeroOct` at 0 V
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
	static double shruti[22];
	static const double ratios[22] = {1., 256. / 243, 16. / 15, 10. / 9, 9. / 8, 32. / 27, 6. / 5, 5. / 4, 81. / 64, 4. / 3, 27. / 20,
		45. / 32, 729. / 512, 3. / 2, 128. / 81, 8. / 5, 5. / 3, 27. / 16, 16. / 9, 9. / 5, 15. / 8, 243. / 128};
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
	for (int i = 1; i < kTableSize; i++)
		if (std::fabs(table[i] - v) < std::fabs(table[best] - v))
			best = i;
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
	uint8_t transition = LAST;
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
				s.uid = tr.nextUid++;
				s.v[CV_A] = (uint8_t)nearestIndex(tr.table[0], 0.f);
				s.v[CV_B] = (uint8_t)nearestIndex(tr.table[1], 0.f);
				s.v[DUR] = 4;
				s.v[GATE] = t == 0 ? 2 : 0;
				s.flags = 0;
			}
		}
		for (int p = 0; p < kParts; p++)
			parts[p] = Part();
		transition = LAST;
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

// ---- Saving: a compact binary form (base64 in the patch). Version 1:
// 'R','G','S',version, transition; per track: patterns, their lengths and flags, track flags, div, mult, MATH (20 bytes),
// steps (CV A, CV B, DUR, GATE, flags; uids too when withUids), tables as float32; then the parts that are set, as
// indices into their tracks.
inline void put16(std::vector<uint8_t> &o, int v) { o.push_back((uint8_t)(v & 0xff)); o.push_back((uint8_t)((v >> 8) & 0xff)); }
inline void put32(std::vector<uint8_t> &o, uint32_t v) { for (int k = 0; k < 4; k++) o.push_back((uint8_t)(v >> (8 * k))); }

inline void serialize(const Song &s, std::vector<uint8_t> &o, bool withUids = false) {
	o.clear();
	o.insert(o.end(), {'R', 'G', 'S', (uint8_t)(withUids ? 0x81 : 1), s.transition});
	for (int t = 0; t < kTracks; t++) {
		const Track &tr = s.tracks[t];
		o.push_back((uint8_t)tr.numPatterns);
		o.insert(o.end(), tr.patternLength, tr.patternLength + tr.numPatterns);
		o.insert(o.end(), tr.patternFlags, tr.patternFlags + tr.numPatterns);
		o.push_back(tr.flags);
		o.push_back(tr.div);
		o.push_back(tr.mult);
		const uint8_t *m = reinterpret_cast<const uint8_t *>(&tr.math);
		o.insert(o.end(), m, m + sizeof(Transform));
		if (withUids)
			put32(o, tr.nextUid);
		for (int i = 0; i < tr.numSteps; i++) {
			const Step &st = tr.steps[i];
			o.insert(o.end(), st.v, st.v + NUM_FIELDS);
			o.push_back(st.flags);
			if (withUids)
				put32(o, st.uid);
		}
		for (int ab = 0; ab < 2; ab++)
			for (int i = 0; i < kTableSize; i++) {
				uint32_t bits;
				std::memcpy(&bits, &tr.table[ab][i], 4);
				put32(o, bits);
			}
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
}

// Reads what serialize() wrote into s. On a malformed blob it returns false and leaves s as it was.
inline bool deserialize(const uint8_t *d, size_t n, Song &out) {
	size_t i = 0;
	auto need = [&](size_t k) { return i + k <= n; };
	auto get16 = [&]() { int v = (int16_t)(d[i] | (d[i + 1] << 8)); i += 2; return v; };
	auto get32 = [&]() { uint32_t v = 0; for (int k = 0; k < 4; k++) v |= (uint32_t)d[i + k] << (8 * k); i += 4; return v; };
	if (!need(5) || d[0] != 'R' || d[1] != 'G' || d[2] != 'S' || (d[3] & 0x7f) != 1)
		return false;
	const bool withUids = d[3] & 0x80;
	std::unique_ptr<Song> s(new Song);
	s->transition = d[4] < NUM_TRANSITIONS ? d[4] : (uint8_t)LAST;
	i = 5;
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
		i += sizeof(Transform);
		if (withUids) {
			if (!need(4))
				return false;
			tr.nextUid = get32();
		}
		const size_t stepBytes = withUids ? 9 : 5;
		if (!need(stepBytes * total + 8 * kTableSize))
			return false;
		for (int k = 0; k < total; k++) {
			Step &st = tr.steps[k];
			for (int f = 0; f < NUM_FIELDS; f++)
				st.v[f] = (uint8_t)std::min((int)d[i + f], kMaxValue);
			st.flags = d[i + NUM_FIELDS] & (SMOOTH_A | SMOOTH_B | RATCHET);
			i += 5;
			st.uid = withUids ? get32() : tr.nextUid++;
		}
		for (int ab = 0; ab < 2; ab++)
			for (int k = 0; k < kTableSize; k++) {
				uint32_t bits = get32();
				float v;
				std::memcpy(&v, &bits, 4);
				tr.table[ab][k] = std::isfinite(v) ? clampVolts(v) : 0.f;
			}
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

	static int roundDiv(int x, int d) { return (x >= 0) ? (x + d / 2) / d : -((-x + d / 2) / d); }

	int transformValue(const Transform &m, int f, int v) {
		int x = (m.rd[f] > 0) ? std::uniform_int_distribution<int>(0, m.rd[f])(rng) : v;
		const int g = m.g[f];
		if (g >= 1 || g == -1)
			x *= (g == -1 ? 1 : g);
		else if (g == 0)
			x = 0;
		else
			x = roundDiv(x, -g);
		if (m.jt[f] > 0)
			x += std::uniform_int_distribution<int>(-m.jt[f], m.jt[f])(rng);
		x += m.add[f];
		const int q = std::max(1, (int)m.qt[f]);
		if (q > 1)
			x = roundDiv(x, q) * q;
		return (x < 0 || x > kMaxValue) ? v : x;
	}

	void applyMath(int t, int first, int n, const Transform &m) {
		Track &k = tr(t);
		for (int i = std::max(0, first); i < std::min(first + n, (int)k.numSteps); i++)
			for (int f = 0; f < NUM_FIELDS; f++)
				k.steps[i].v[f] = (uint8_t)transformValue(m, f, k.steps[i].v[f]);
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
};

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

// ---- Playback
struct Engine {
	struct Io {
		// rising edges this sample
		bool clock = false, reset = false, run = false, partGo = false;
		bool partGoHigh = false;      // GO held high: the cued part follows PART
		int partCv = -1;              // the part PART's voltage selects, -1 unplugged
		float transpose[kTracks] = {};
		float a[kTracks] = {}, b[kTracks] = {}, gate[kTracks] = {};
	};

	enum Arm { ARM_NONE, ARM_RESET, ARM_UID };

	struct Play {
		int step = 0, next = 0;       // indices in the song the audio plays
		uint32_t uid = 0;
		int pulse = 0;                // pulses since the step began
		int arm = ARM_RESET;          // what the next pulse starts
		uint32_t armUid = 0;
		bool idle = true;             // nothing to play: no step with a duration
		bool looped = false;          // a loop ended since the part began
		// the track's clock
		int divCount = 0;
		int sub = 0;                  // the multiplied pulses already given since the last external one
		int64_t lastTick = -1;
		int trigLeft = 0;             // samples left of a trigger
	};

	Handoff handoff;
	Play play[kTracks];
	double sampleRate = 44100.0;
	int64_t now = 0;                  // samples since start
	int64_t lastEdge = -1, period = 0;
	int playingPart = 1, pendingPart = -1;
	std::atomic<bool> running{true};
	std::atomic<int> uiGoPart{-1};    // the UI cues a part
	std::atomic<int> jumpTrack{-1};   // the UI moves a play cursor (FOLLOW)
	std::atomic<uint32_t> jumpUid{0};
	std::atomic<bool> resetRequest{false};
	std::atomic<int> setPart{-1};     // a loaded patch's playing part, without a transition
	// for the UI
	std::atomic<uint32_t> shownUid[kTracks];
	std::atomic<int> shownPulse[kTracks], shownPart{1}, shownPending{-1};
	std::atomic<bool> shownCommitPending{false};

	Engine() {
		for (int t = 0; t < kTracks; t++) {
			shownUid[t] = 0;
			shownPulse[t] = 0;
		}
	}

	Song &song() { return handoff.song(); }

	int loopStart(int t) { return song().partLoopStart[playingPart][t]; }
	int loopEnd(int t) { return song().partLoopEnd[playingPart][t]; }

	// The step after i, and whether that ends a loop
	int nextIndex(int t, int i, bool &wrapped) {
		const Track &k = song().tracks[t];
		const int ls = std::max(0, (int)loopStart(t)), le = loopEnd(t);
		wrapped = (le >= 0 && i == le) || i + 1 >= k.numSteps;
		return wrapped ? ls : i + 1;
	}

	// From i (or the first step after it with a duration). Sets idle when no step has one
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
			if (k.steps[i].v[DUR] > 0) {
				p.idle = false;
				p.step = i;
				p.uid = k.steps[i].uid;
				p.pulse = 0;
				bool w;
				p.next = findPlayable(t, i, w);
				startStep(t);
				return true;
			}
			bool w;
			i = nextIndex(t, i, w);
			wrapped = wrapped || w;
		}
		p.idle = true;
		return false;
	}

	// The next step with a duration after i (i itself when none)
	int findPlayable(int t, int i, bool &wrapped) {
		const Track &k = song().tracks[t];
		wrapped = false;
		int j = i;
		for (int tries = 0; tries <= k.numSteps; tries++) {
			bool w;
			j = nextIndex(t, j, w);
			wrapped = wrapped || w;
			if (k.steps[j].v[DUR] > 0)
				return j;
		}
		return i;
	}

	void startStep(int t) {
		Play &p = play[t];
		const Track &k = song().tracks[t];
		const Step &st = k.steps[p.step];
		if (k.triggers())
			fireTrigger(t, st, 0);
	}

	void fireTrigger(int t, const Step &st, int pulse) {
		Play &p = play[t];
		const int g = st.v[GATE], d = st.v[DUR];
		if (g <= 0)
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
		if (p.pulse < k.steps[p.step].v[DUR]) {
			if (k.triggers())
				fireTrigger(t, k.steps[p.step], p.pulse);
			return 0;
		}
		const int from = p.step;
		bool wrapped;
		const int to = findPlayable(t, from, wrapped);
		if (to == from && !wrapped) {
			bool w;
			startAt(t, from, w);
		}
		else {
			p.step = to;
			p.uid = k.steps[to].uid;
			p.pulse = 0;
			bool w;
			p.next = findPlayable(t, to, w);
			startStep(t);
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
			bool w;
			p.next = findPlayable(t, i, w);
		}
	}

	void doReset() {
		for (int t = 0; t < kTracks; t++) {
			Play &p = play[t];
			p.arm = ARM_RESET;
			p.looped = false;
			p.divCount = 0;
			p.sub = 1 << 20;          // no multiplied pulses until the next external one
			p.trigLeft = 0;
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

	void process(Io &io) {
		Song *s = &song();
		if (handoff.pending() && handoff.commitAt.load() == COMMIT_NOW) {
			handoff.take();
			s = &song();
			remap();
		}
		if (resetRequest.exchange(false))
			io.reset = true;
		const int sp = setPart.exchange(-1);
		if (sp >= 0 && sp < kParts) {
			playingPart = sp;
			pendingPart = -1;
		}
		if (io.run)
			running = !running;
		const int jt = jumpTrack.exchange(-1);
		if (jt >= 0 && jt < kTracks) {
			play[jt].arm = ARM_UID;
			play[jt].armUid = jumpUid.load();
		}
		// parts cued by GO (from PART, or the UI's part when PART is unplugged) or by the UI
		const int uiPart = uiGoPart.exchange(-1);
		int cue = -1;
		if (io.partGo)
			cue = io.partCv >= 0 ? io.partCv : -2;
		else if (io.partGoHigh && io.partCv >= 0 && pendingPart >= 0)
			cue = io.partCv;
		if (uiPart >= 0)
			cue = uiPart;
		bool ticked[kTracks] = {};
		if (cue >= 0 && cue < kParts) {
			if (s->transition == NOW)
				switchPart(cue, false, ticked);
			else
				pendingPart = cue;
		}
		if (io.reset)
			doReset();

		// the clocks: an external pulse, then each track's multiplied pulses, then its divider
		int tickCount[kTracks] = {};
		if (io.clock) {
			if (lastEdge >= 0)
				period = now - lastEdge;
			lastEdge = now;
			for (int t = 0; t < kTracks; t++) {
				play[t].sub = 0;
				tickCount[t]++;
			}
		}
		for (int t = 0; t < kTracks; t++) {
			Play &p = play[t];
			const int m = s->tracks[t].mult;
			if (io.clock)
				p.sub = 1;
			// several sub-pulses can fall on one sample when the period is shorter than the multiplier
			while (m > 1 && period > 0 && p.sub < m && lastEdge + (period * p.sub) / m == now) {
				tickCount[t]++;
				p.sub++;
			}
		}
		const bool run = running.load();
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

		// outputs
		for (int t = 0; t < kTracks; t++) {
			Play &p = play[t];
			const Track &k = s->tracks[t];
			const int semis = (int)std::lround(io.transpose[t] * 12.f);
			if (k.numSteps <= 0) {
				io.a[t] = io.b[t] = io.gate[t] = 0.f;
				continue;
			}
			const int i = std::min(p.step, k.numSteps - 1);
			const Step &st = k.steps[i];
			const Step &nx = k.steps[std::min(p.next, k.numSteps - 1)];
			float va = tableValue(k, 0, st.v[CV_A], semis), vb = tableValue(k, 1, st.v[CV_B], 0);
			const int d = st.v[DUR], g = st.v[GATE];
			// smoothing: a line to the next step's value over the end of the step (after the gate, or over its second
			// half for triggers), timed in samples from the track's pulse period
			const double rampStart = k.triggers() ? d * 0.5 : (double)g;
			if (!p.idle && rampStart < d) {
				const double pulsePeriod = period > 0 ? (double)period * k.div / k.mult : 0.0;
				const double frac = (pulsePeriod > 0 && p.lastTick >= 0) ? std::min(1.0, (now - p.lastTick) / pulsePeriod) : 0.0;
				const double pos = p.pulse + frac;
				if (pos > rampStart) {
					const float f = (float)std::min(1.0, (pos - rampStart) / (d - rampStart));
					if (k.smooth(i, 0))
						va += (tableValue(k, 0, nx.v[CV_A], semis) - va) * f;
					if (k.smooth(i, 1))
						vb += (tableValue(k, 1, nx.v[CV_B], 0) - vb) * f;
				}
			}
			io.a[t] = va;
			io.b[t] = vb;
			bool high = false;
			if (!p.idle && run && playingPart != 0 && p.arm == ARM_NONE) {
				if (k.triggers())
					high = p.trigLeft > 0;
				else if (g > 0)
					high = (st.flags & RATCHET) ? ((p.pulse / g) % 2 == 0) : p.pulse < g;
			}
			if (p.trigLeft > 0)
				p.trigLeft--;
			io.gate[t] = high ? kGateVolts : 0.f;
			shownUid[t].store(p.idle ? 0 : p.uid, std::memory_order_relaxed);
			shownPulse[t].store(p.pulse, std::memory_order_relaxed);
		}
		shownPart.store(playingPart, std::memory_order_relaxed);
		shownPending.store(pendingPart, std::memory_order_relaxed);
		shownCommitPending.store(handoff.pending(), std::memory_order_relaxed);
		now++;
	}
};

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
