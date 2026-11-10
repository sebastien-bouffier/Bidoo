// rEgIStRE: an indexed four-track sequencer. Each step holds two indices into its track's voltage tables, a duration and
// a gate in clock pulses; patterns divide the tracks; parts hold loops and reset steps and switch at the end of a loop.
// The sequencer is src/RegistreEngine.hpp; this file is the module, its screen (mouse and keyboard) and its undo.
#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "RegistreEngine.hpp"
#include <mutex>

using namespace registre;

struct REGISTRE : BidooModule {
	enum ParamIds {
		RUN_PARAM,
		GO_PARAM,
		HOLD_PARAM,
		COMMIT_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		RESET_INPUT,
		RUN_INPUT,
		TRANSPOSE_INPUT,
		PART_INPUT,
		GO_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ENUMS(A_OUTPUTS, kTracks),
		ENUMS(B_OUTPUTS, kTracks),
		ENUMS(GATE_OUTPUTS, kTracks),
		POLY_A_OUTPUT,
		POLY_B_OUTPUT,
		POLY_GATE_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	registre::Engine engine;
	dsp::SchmittTrigger clockTrigger, resetTrigger, runTrigger, goTrigger;

	// The document: what the screen shows and edits, on the UI thread. The audio plays copies of it (Engine::handoff).
	// The lock is only between the UI and the patch's saving and loading: process() never takes it
	std::recursive_mutex docMutex;
	std::unique_ptr<Song> doc{new Song};
	std::unique_ptr<Song> committed{new Song};        // what was last handed to the audio (HOLD discards back to it)
	std::vector<uint8_t> snapshots[16];
	std::mt19937 rng{std::random_device{}()};
	std::atomic<int> uiPart{1};                        // the part whose loops and reset steps the screen edits
	int commitAt = COMMIT_PATTERN;                     // a commit in HOLD waits for the end of a step, pattern or loop
	int commitTrack = 0;
	bool hold = false;

	REGISTRE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
		configButton(GO_PARAM, "Go: cue the part shown on the screen (it starts at the end of a loop, as Transition says)");
		configSwitch(HOLD_PARAM, 0.f, 1.f, 0.f, "Hold: edits wait for COMMIT", {"Off: edits play at once", "On: edits wait for COMMIT"});
		configButton(COMMIT_PARAM, "Commit the held edits (at the end of a step, pattern or loop: see the menu; twice: now)");
		configInput(CLOCK_INPUT, "Clock: each pulse is a pulse of every track (before its divider and multiplier)");
		configInput(RESET_INPUT, "Reset: the next clock plays each track's reset step (the part's, or the first)");
		configInput(RUN_INPUT, "Run: a trigger starts or stops");
		configInput(TRANSPOSE_INPUT, "Transpose CV A: 1/12 V moves the index by one (polyphonic: channel n for track n)");
		configInput(PART_INPUT, "Part: 0.1 V per part, 0 V stops (0 to 9.9 V for parts 0 to 99)");
		configInput(GO_INPUT, "Go: a gate cues the part PART selects (while it stays high, the cue follows PART)");
		for (int t = 0; t < kTracks; t++) {
			configOutput(A_OUTPUTS + t, rack::string::f("Track %d CV A", t + 1));
			configOutput(B_OUTPUTS + t, rack::string::f("Track %d CV B", t + 1));
			configOutput(GATE_OUTPUTS + t, rack::string::f("Track %d gate", t + 1));
		}
		configOutput(POLY_A_OUTPUT, "CV A of the four tracks (channel n = track n)");
		configOutput(POLY_B_OUTPUT, "CV B of the four tracks");
		configOutput(POLY_GATE_OUTPUT, "Gates of the four tracks");
		engine.sampleRate = APP->engine->getSampleRate();
		*committed = *doc;
		publish(COMMIT_NOW);
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		engine.sampleRate = e.sampleRate;
	}

	// UI: hands the document to the audio
	void publish(int at) {
		doc->layout();
		engine.handoff.writeBuffer() = *doc;
		*committed = *doc;
		engine.handoff.publish(at, commitTrack);
	}

	void onReset(const ResetEvent &e) override {
		BidooModule::onReset(e);
		std::lock_guard<std::recursive_mutex> lock(docMutex);
		doc->init();
		for (auto &s : snapshots)
			s.clear();
		uiPart = 1;
		hold = false;
		params[HOLD_PARAM].setValue(0.f);
		publish(COMMIT_NOW);
		engine.resetRequest = true;
	}

	void process(const ProcessArgs &args) override {
		registre::Engine::Io io;
		io.clock = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		io.reset = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		if (runTrigger.process(inputs[RUN_INPUT].getVoltage(), 0.1f, 1.f))
			params[RUN_PARAM].setValue(params[RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f);
		engine.running = params[RUN_PARAM].getValue() > 0.5f;
		io.partGo = goTrigger.process(inputs[GO_INPUT].getVoltage(), 0.1f, 1.f);
		io.partGoHigh = goTrigger.isHigh();
		if (inputs[PART_INPUT].isConnected())
			io.partCv = clamp((int)std::floor(inputs[PART_INPUT].getVoltage() * 10.f + 1e-3f), 0, kParts - 1);
		else if (io.partGo)
			io.partCv = uiPart.load();
		const int tc = inputs[TRANSPOSE_INPUT].getChannels();
		for (int t = 0; t < kTracks; t++)
			io.transpose[t] = tc == 0 ? 0.f : inputs[TRANSPOSE_INPUT].getVoltage(tc == 1 ? 0 : std::min(t, tc - 1));
		engine.process(io);
		for (int t = 0; t < kTracks; t++) {
			outputs[A_OUTPUTS + t].setVoltage(io.a[t]);
			outputs[B_OUTPUTS + t].setVoltage(io.b[t]);
			outputs[GATE_OUTPUTS + t].setVoltage(io.gate[t]);
			outputs[POLY_A_OUTPUT].setVoltage(io.a[t], t);
			outputs[POLY_B_OUTPUT].setVoltage(io.b[t], t);
			outputs[POLY_GATE_OUTPUT].setVoltage(io.gate[t], t);
		}
		outputs[POLY_A_OUTPUT].setChannels(kTracks);
		outputs[POLY_B_OUTPUT].setChannels(kTracks);
		outputs[POLY_GATE_OUTPUT].setChannels(kTracks);
	}

	// ---- Undo: the song before and after each edit, in the compact form with the steps' uids (a few kB)
	struct EditAction : history::ModuleAction {
		std::vector<uint8_t> before, after;
		int mergeKey = 0;
		void apply(const std::vector<uint8_t> &blob) {
			REGISTRE *m = dynamic_cast<REGISTRE *>(APP->engine->getModule(moduleId));
			if (m)
				m->restore(blob);
		}
		void undo() override { apply(before); }
		void redo() override { apply(after); }
	};

	void restore(const std::vector<uint8_t> &blob) {
		std::lock_guard<std::recursive_mutex> lock(docMutex);
		if (deserialize(blob.data(), blob.size(), *doc) && !hold)
			publish(COMMIT_NOW);
	}

	// Runs an edit of the document and adds it to Rack's undo history if it changed anything. Edits with the same
	// mergeKey in a row (a drag, the wheel on one cell) are one action. UI thread.
	template <typename F>
	void edit(const char *name, int mergeKey, F change) {
		std::lock_guard<std::recursive_mutex> lock(docMutex);
		std::vector<uint8_t> before, after;
		serialize(*doc, before, true);
		Editor ed{*doc, rng};
		change(ed);
		doc->layout();
		serialize(*doc, after, true);
		if (before == after)
			return;
		if (!hold)
			publish(COMMIT_NOW);
		history::State *h = APP->history;
		if (!h)
			return;
		if (mergeKey && h->actionIndex > 0 && h->actionIndex <= (int)h->actions.size()) {
			EditAction *last = dynamic_cast<EditAction *>(h->actions[h->actionIndex - 1]);
			if (last && last->moduleId == id && last->mergeKey == mergeKey && h->actionIndex == (int)h->actions.size()) {
				last->after = after;
				return;
			}
		}
		EditAction *a = new EditAction;
		a->name = rack::string::f("rEgIStRE %s", name);
		a->moduleId = id;
		a->before = std::move(before);
		a->after = std::move(after);
		a->mergeKey = mergeKey;
		h->push(a);
	}

	// ---- HOLD and COMMIT, from the panel's buttons (UI thread)
	void setHold(bool on) {
		std::lock_guard<std::recursive_mutex> lock(docMutex);
		if (on == hold)
			return;
		hold = on;
		if (!on) {
			// leaving HOLD without committing drops the held edits, as an undoable edit
			const Song kept = *committed;
			edit("drop held edits", 0, [&](Editor &ed) { ed.s = kept; });
			publish(COMMIT_NOW);
		}
	}

	void commit() {
		std::lock_guard<std::recursive_mutex> lock(docMutex);
		// pressed again while one waits: now
		publish(engine.shownCommitPending.load() ? COMMIT_NOW : commitAt);
	}

	// ---- Snapshots: the whole song, in the patch
	void saveSnapshot(int i) {
		std::lock_guard<std::recursive_mutex> lock(docMutex);
		serialize(*doc, snapshots[i]);
	}

	bool loadSnapshot(int i) {
		std::lock_guard<std::recursive_mutex> lock(docMutex);
		if (snapshots[i].empty())
			return false;
		const std::vector<uint8_t> blob = snapshots[i];
		edit("load snapshot", 0, [&](Editor &ed) { deserialize(blob.data(), blob.size(), ed.s); });
		if (!hold)
			engine.resetRequest = true;       // as the hardware: every cursor back to its start
		return true;
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		std::lock_guard<std::recursive_mutex> lock(docMutex);
		std::vector<uint8_t> blob;
		serialize(*doc, blob);
		json_object_set_new(rootJ, "song", json_string(rack::string::toBase64(blob.data(), blob.size()).c_str()));
		json_t *snapJ = json_array();
		for (auto &s : snapshots)
			json_array_append_new(snapJ, s.empty() ? json_null() : json_string(rack::string::toBase64(s.data(), s.size()).c_str()));
		json_object_set_new(rootJ, "snapshots", snapJ);
		json_object_set_new(rootJ, "part", json_integer(uiPart.load()));
		json_object_set_new(rootJ, "playingPart", json_integer(engine.shownPart.load()));
		json_object_set_new(rootJ, "commitAt", json_integer(commitAt));
		json_object_set_new(rootJ, "commitTrack", json_integer(commitTrack));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		std::lock_guard<std::recursive_mutex> lock(docMutex);
		if (json_t *j = json_object_get(rootJ, "song")) {
			const std::vector<uint8_t> blob = rack::string::fromBase64(json_string_value(j));
			deserialize(blob.data(), blob.size(), *doc);
		}
		if (json_t *j = json_object_get(rootJ, "snapshots"))
			for (size_t i = 0; i < 16 && i < json_array_size(j); i++) {
				json_t *s = json_array_get(j, i);
				snapshots[i] = json_is_string(s) ? rack::string::fromBase64(json_string_value(s)) : std::vector<uint8_t>();
			}
		if (json_t *j = json_object_get(rootJ, "part"))
			uiPart = clamp((int)json_integer_value(j), 0, kParts - 1);
		if (json_t *j = json_object_get(rootJ, "playingPart"))
			engine.setPart = clamp((int)json_integer_value(j), 0, kParts - 1);
		if (json_t *j = json_object_get(rootJ, "commitAt"))
			commitAt = clamp((int)json_integer_value(j), 0, (int)COMMIT_LOOP);
		if (json_t *j = json_object_get(rootJ, "commitTrack"))
			commitTrack = clamp((int)json_integer_value(j), 0, kTracks - 1);
		hold = false;
		params[HOLD_PARAM].setValue(0.f);
		publish(COMMIT_NOW);
		engine.resetRequest = true;
	}
};

// ---- User tables: eight tables kept for every patch, in Rack's user folder
namespace registre_ui {

static const int kUserTables = 8;

struct UserTables {
	float t[kUserTables][kTableSize];
	bool loaded = false;
	std::string path() { return asset::user("Bidoo/rEgIStRE-tables.json"); }
	void load() {
		if (loaded)
			return;
		loaded = true;
		for (int u = 0; u < kUserTables; u++)
			referenceTable(REF_12TET, t[u]);
		json_error_t err;
		json_t *rootJ = json_load_file(path().c_str(), 0, &err);
		if (!rootJ)
			return;
		for (int u = 0; u < kUserTables; u++) {
			json_t *a = json_array_get(json_object_get(rootJ, "tables"), u);
			for (int i = 0; a && i < kTableSize && i < (int)json_array_size(a); i++)
				t[u][i] = clampVolts((float)json_number_value(json_array_get(a, i)));
		}
		json_decref(rootJ);
	}
	void save() {
		system::createDirectories(asset::user("Bidoo"));
		json_t *rootJ = json_object();
		json_t *tables = json_array();
		for (int u = 0; u < kUserTables; u++) {
			json_t *a = json_array();
			for (int i = 0; i < kTableSize; i++)
				json_array_append_new(a, json_real(t[u][i]));
			json_array_append_new(tables, a);
		}
		json_object_set_new(rootJ, "tables", tables);
		json_dump_file(rootJ, path().c_str(), JSON_INDENT(1));
		json_decref(rootJ);
	}
};

static UserTables &userTables() {
	static UserTables u;
	u.load();
	return u;
}

static std::vector<Step> &clipboard() {
	static std::vector<Step> c;
	return c;
}

static std::shared_ptr<window::Font> monoFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}

static const NVGcolor kDim = nvgRGB(0x57, 0x53, 0x4A);       // as cOPIA's screen: what is off or secondary
static const NVGcolor kShade = nvgRGB(0x26, 0x25, 0x21);     // a selected cell, a band
static const NVGcolor kLine = nvgRGB(0x33, 0x32, 0x2D);

// The screen's layout, px from its top left. It is 580 x 233 (the panel's screen, tools/panels/registre.py)
static constexpr float HEAD = 14.f;
static constexpr float LANE_Y = 16.f, LANE_H = 11.f, LANE_LABEL = 52.f;
static constexpr float GRID_Y = 66.f, GRID_W = 466.f, ROW_LABEL = 30.f;
static constexpr int kCols = 16;                                // steps shown at once
static constexpr int kRows = 7;                                 // CV A, CV B, DUR, GATE, SMOOTH A, SMOOTH B, RATCHET
static const float kRowH[kRows] = {22.f, 14.f, 18.f, 18.f, 9.f, 9.f, 9.f};
static const char *kRowName[kRows] = {"CV A", "CV B", "DUR", "GATE", "SM A", "SM B", "RTCH"};
static constexpr float MATH_Y = 184.f;
static constexpr float TABLE_X = 472.f;
// TABLES view
static constexpr float CURVE_X = 34.f, CURVE_Y = 26.f, CURVE_W = 304.f, CURVE_H = 150.f;
static constexpr float REF_X = 352.f, LIST_X = 430.f, SNAP_Y = 206.f;

static float rowTop(int r) {
	float y = GRID_Y;
	for (int i = 0; i < r; i++)
		y += kRowH[i];
	return y;
}
static float gridBottom() { return rowTop(kRows); }
static float colW() { return (GRID_W - ROW_LABEL) / kCols; }

// MATH's cells: per field, RND, x/÷, JIT, +, QNT
static const char *kOpName[5] = {"RND", "x", "JIT", "+", "QNT"};

static int8_t &opRef(Transform &m, int f, int op) {
	switch (op) {
		case 0: return m.rd[f];
		case 1: return m.g[f];
		case 2: return m.jt[f];
		case 3: return m.add[f];
		default: return m.qt[f];
	}
}
static void opRange(int op, int &lo, int &hi) {
	lo = (op == 1 || op == 3) ? -99 : (op == 4 ? 1 : 0);
	hi = 99;
}
static std::string opText(int op, int v) {
	if (op == 1)
		return v >= 1 ? rack::string::f("x%d", v) : v == 0 ? "x0" : rack::string::f("/%d", -v);
	if (op == 3)
		return rack::string::f("%+d", v);
	return rack::string::f("%d", v);
}
// x/÷ steps over -1: ..., /3, /2, x0, x1, x2, ...
static int opStep(int op, int v, int d) {
	int lo, hi;
	opRange(op, lo, hi);
	if (op == 1) {
		int k = v <= -2 ? v + 1 : v;        // /2 -> -1, x0 -> 0, x1 -> 1
		k = clamp(k + d, -98, 99);
		return k <= -1 ? k - 1 : k;
	}
	return clamp(v + d, lo, hi);
}

struct RegistreScreen : OpaqueWidget {
	REGISTRE *module = nullptr;

	// what the screen points at; not saved, except through the module (uiPart)
	int view = 0;              // 0 STEPS, 1 TABLES
	int track = 0;
	int cursor = 0, anchor = 0; // step indices in the track: the selection is between them
	int row = 0;
	int scroll = 0;            // first step shown of the cursor's pattern
	bool follow = false;
	int mathTarget = 0;        // 0 the selection, 1 its pattern, 2 the track
	int mathField = -1, mathOp = 0;  // the MATH cell being typed into
	int tableAB = 0, tableIndex = 48, tableScroll = 41;
	int refSel = 0;
	int focus = 0;             // where typing goes: 0 the grid, 1 a MATH cell, 2 a table entry
	std::string typing;
	int mergeSerial = 1000;    // keys for merged undo actions: one per drag or per cell under the wheel
	int wheelKey = 0;
	Vec wheelPos;
	// a drag
	int dragKind = 0;          // 0 none, 1 grid value, 2 grid range, 3 table point, 4 math cell, 5 list entry
	Vec dragStart, dragAcc;
	int dragKey = 0;
	int dragDecided = 0;
	float dragValue = 0.f;

	std::unique_ptr<Song> preview;

	RegistreScreen() {
		preview.reset(new Song);
		Track &k = preview->tracks[0];
		static const int notes[16] = {48, 51, 55, 58, 60, 58, 55, 53, 51, 48, 46, 43, 48, 48, 55, 60};
		for (int i = 0; i < 16; i++) {
			k.steps[i].v[CV_A] = (uint8_t)notes[i];
			k.steps[i].v[DUR] = (uint8_t)(i % 5 == 4 ? 8 : 4);
			k.steps[i].v[GATE] = (uint8_t)(1 + i % 3);
		}
		k.steps[2].flags = SMOOTH_A;
		k.steps[4].flags = RATCHET;
		preview->layout();
	}

	Song &song() { return module ? *module->doc : *preview; }
	Track &tr() { return song().tracks[track]; }
	int selFirst() { return std::min(cursor, anchor); }
	int selCount() { return std::abs(cursor - anchor) + 1; }

	void clampCursor() {
		const int n = tr().numSteps;
		cursor = clamp(cursor, 0, std::max(0, n - 1));
		anchor = clamp(anchor, 0, std::max(0, n - 1));
	}
	int curPattern() {
		Track &k = tr();
		if (k.numSteps == 0)
			return 0;
		return k.stepPattern[clamp(cursor, 0, k.numSteps - 1)];
	}
	// the pattern shown: the cursor's, or an empty one picked with PgUp/PgDn
	int shownPattern = 0;
	void syncPattern() {
		Track &k = tr();
		if (k.numSteps > 0)
			shownPattern = curPattern();
		shownPattern = clamp(shownPattern, 0, std::max(0, k.numPatterns - 1));
		if (k.numPatterns == 0)
			return;
		const int ps = k.patternStart[shownPattern], pl = k.patternLength[shownPattern];
		const int pos = cursor - ps;
		if (pos < scroll)
			scroll = pos;
		if (pos >= scroll + kCols)
			scroll = pos - kCols + 1;
		scroll = clamp(scroll, 0, std::max(0, pl - kCols));
	}

	void moveCursor(int to, bool extend) {
		cursor = to;
		clampCursor();
		if (!extend)
			anchor = cursor;
		syncPattern();
	}

	template <typename F>
	void edit(const char *name, int mergeKey, F f) {
		if (!module)
			return;
		module->edit(name, mergeKey, f);
		clampCursor();
		syncPattern();
	}

	// ---- values
	void nudge(int d, int key = 0) {
		const int f0 = selFirst(), n = selCount(), r = row, t = track;
		edit("change values", key, [&](Editor &ed) {
			for (int i = f0; i < f0 + n && i < ed.s.tracks[t].numSteps; i++) {
				Step &st = ed.s.tracks[t].steps[i];
				Editor::setRowValue(st, r, r < NUM_FIELDS ? Editor::rowValue(st, r) + d : (d > 0));
			}
		});
	}
	void setValue(int v) {
		const int f0 = selFirst(), n = selCount(), r = row, t = track;
		edit("set values", 0, [&](Editor &ed) {
			for (int i = f0; i < f0 + n && i < ed.s.tracks[t].numSteps; i++)
				Editor::setRowValue(ed.s.tracks[t].steps[i], r, v);
		});
	}
	void toggleFlag(int r) {
		const int f0 = selFirst(), n = selCount(), t = track;
		const int v = tr().numSteps ? !Editor::rowValue(tr().steps[cursor], r) : 1;
		edit("toggle", 0, [&](Editor &ed) {
			for (int i = f0; i < f0 + n && i < ed.s.tracks[t].numSteps; i++)
				Editor::setRowValue(ed.s.tracks[t].steps[i], r, v);
		});
	}

	// ---- steps and patterns
	Step stepForInsert() {
		Track &k = tr();
		if (k.numSteps > 0)
			return k.steps[clamp(cursor, 0, k.numSteps - 1)];
		Step s;
		s.v[CV_A] = (uint8_t)nearestIndex(k.table[0], 0.f);
		s.v[CV_B] = (uint8_t)nearestIndex(k.table[1], 0.f);
		return s;
	}
	void insertSteps(const std::vector<Step> &steps, bool before, const char *name) {
		if (steps.empty())
			return;
		Track &k = tr();
		const int t = track;
		int p, pos;
		if (k.numSteps == 0 || k.patternLength[shownPattern] == 0) {
			p = shownPattern;
			pos = 0;
			if (k.numPatterns == 0) {
				edit(name, 0, [&](Editor &ed) { ed.insertPattern(t, 0, steps.data(), (int)steps.size()); });
				moveCursor(0, false);
				return;
			}
		}
		else {
			p = curPattern();
			pos = (before ? selFirst() : std::max(cursor, anchor) + 1) - k.patternStart[p];
		}
		const int at = k.patternStart[p] + pos;
		edit(name, 0, [&](Editor &ed) { ed.insertSteps(t, p, pos, steps.data(), (int)steps.size()); });
		cursor = at + (int)steps.size() - 1;
		anchor = at;
		clampCursor();
		syncPattern();
	}
	void deleteSelection() {
		const int f0 = selFirst(), n = selCount(), t = track;
		if (tr().numSteps == 0)
			return;
		edit("delete steps", 0, [&](Editor &ed) { ed.deleteSteps(t, f0, n); });
		moveCursor(f0, false);
	}
	void copySelection() {
		Track &k = tr();
		clipboard().clear();
		for (int i = selFirst(); i < selFirst() + selCount() && i < k.numSteps; i++)
			clipboard().push_back(k.steps[i]);
	}
	void pasteOver() {
		const std::vector<Step> c = clipboard();
		const int f0 = cursor, t = track;
		edit("paste values", 0, [&](Editor &ed) {
			Track &k = ed.s.tracks[t];
			for (size_t j = 0; j < c.size() && f0 + (int)j < k.numSteps; j++) {
				const uint32_t uid = k.steps[f0 + j].uid;
				k.steps[f0 + j] = c[j];
				k.steps[f0 + j].uid = uid;
			}
		});
	}
	void newPattern() {
		const int t = track, p = tr().numPatterns ? curPattern() + 1 : 0;
		Step s = stepForInsert();
		edit("new pattern", 0, [&](Editor &ed) { ed.insertPattern(t, p, &s, 1); });
		moveCursor(tr().patternStart[std::min(p, (int)tr().numPatterns - 1)], false);
	}
	void gotoPattern(int d) {
		Track &k = tr();
		if (k.numPatterns == 0)
			return;
		const int p = clamp(shownPattern + d, 0, k.numPatterns - 1);
		shownPattern = p;
		scroll = 0;
		if (k.patternLength[p] > 0)
			moveCursor(k.patternStart[p], false);
	}

	// ---- loops and reset steps of the part shown
	void setPartStep(int which) {      // 0 reset, 1 loop start, 2 loop end
		if (!module || tr().numSteps == 0)
			return;
		const int part = module->uiPart.load(), t = track;
		const uint32_t uid = tr().steps[which == 2 ? std::max(cursor, anchor) : selFirst()].uid;
		edit(which == 0 ? "reset step" : "loop point", 0, [&](Editor &ed) {
			uint32_t &u = which == 0 ? ed.s.parts[part].reset[t] : which == 1 ? ed.s.parts[part].loopStart[t] : ed.s.parts[part].loopEnd[t];
			u = (u == uid) ? 0 : uid;
		});
	}
	void loopSelection(bool clear) {
		if (!module || tr().numSteps == 0)
			return;
		const int part = module->uiPart.load(), t = track;
		int a = selFirst(), b = std::max(cursor, anchor);
		if (a == b && !clear) {       // one step: its pattern
			const int p = curPattern();
			a = tr().patternStart[p];
			b = a + tr().patternLength[p] - 1;
		}
		const uint32_t ua = tr().steps[a].uid, ub = tr().steps[b].uid;
		edit(clear ? "clear loop" : "loop", 0, [&](Editor &ed) {
			Part &pt = ed.s.parts[part];
			const bool same = pt.loopStart[t] == ua && pt.loopEnd[t] == ub;
			pt.loopStart[t] = (clear || same) ? 0 : ua;
			pt.loopEnd[t] = (clear || same) ? 0 : ub;
		});
	}

	// ---- MATH and the row operations, on the target
	void target(int &first, int &n) {
		Track &k = tr();
		if (mathTarget == 2) {
			first = 0;
			n = k.numSteps;
		}
		else if (mathTarget == 1 && k.numSteps) {
			first = k.patternStart[curPattern()];
			n = k.patternLength[curPattern()];
		}
		else {
			first = selFirst();
			n = selCount();
		}
	}
	void applyMath(bool inverted) {
		int f0, n;
		target(f0, n);
		const int t = track;
		edit(inverted ? "math (inverted)" : "math", 0, [&](Editor &ed) {
			const Transform m = inverted ? ed.s.tracks[t].math.inverted() : ed.s.tracks[t].math;
			ed.applyMath(t, f0, n, m);
		});
	}
	void reverseRow() {
		int f0, n;
		target(f0, n);
		const int t = track, r = row;
		edit("reverse", 0, [&](Editor &ed) { ed.reverse(t, f0, n, r); });
	}
	void rotateRow(int dir) {
		int f0, n;
		target(f0, n);
		const int t = track, r = row;
		edit("rotate", 0, [&](Editor &ed) { ed.rotate(t, f0, n, r, dir); });
	}

	void commitTyping() {
		if (typing.empty())
			return;
		const std::string s = typing;
		typing.clear();
		if (focus == 2) {
			float v;
			if (parseVoltsOrNote(s.c_str(), v)) {
				const int t = track, ab = tableAB, i = tableIndex;
				edit("table value", 0, [&](Editor &ed) { ed.s.tracks[t].table[ab][i] = v; });
			}
			return;
		}
		char *end;
		const long v = std::strtol(s.c_str(), &end, 10);
		if (end == s.c_str())
			return;
		if (focus == 1 && mathField >= 0) {
			const int t = track, f = mathField, op = mathOp;
			int lo, hi;
			opRange(op, lo, hi);
			edit("math setting", 0, [&](Editor &ed) { opRef(ed.s.tracks[t].math, f, op) = (int8_t)clamp((int)v, lo, hi); });
			return;
		}
		setValue((int)v);
	}

	// ---- keyboard: control keys here, characters in onSelectText (so that any keyboard layout types the right ones)
	void onSelectKey(const SelectKeyEvent &e) override {
		if (e.action != GLFW_PRESS && e.action != GLFW_REPEAT)
			return;
		const int mods = e.mods & RACK_MOD_MASK;
		const bool shift = mods & GLFW_MOD_SHIFT, ctrl = mods & RACK_MOD_CTRL, alt = mods & GLFW_MOD_ALT;
		if (ctrl && (e.keyName == "z" || e.keyName == "y"))
			return;                       // Rack's undo and redo
		Track &k = tr();
		bool used = true;
		switch (e.key) {
			case GLFW_KEY_LEFT:
			case GLFW_KEY_RIGHT: {
				const int d = e.key == GLFW_KEY_LEFT ? -1 : 1;
				if (focus == 2 && view == 1) {
					tableIndex = clamp(tableIndex + d, 0, kTableSize - 1);
					break;
				}
				if (alt) {            // swing: the boundary after the cursor's step
					const int t = track, i = cursor;
					edit("swing", 1, [&](Editor &ed) { ed.swing(t, i, d); });
					break;
				}
				focus = 0;
				moveCursor(cursor + d, shift);
				break;
			}
			case GLFW_KEY_UP:
			case GLFW_KEY_DOWN: {
				const int d = e.key == GLFW_KEY_UP ? -1 : 1;
				if (focus == 2) {
					if (ctrl) {
						const int t = track, ab = tableAB, i = tableIndex;
						edit("table value", 2, [&](Editor &ed) { float &v = ed.s.tracks[t].table[ab][i]; v = clampVolts(v - d * (shift ? 1.f / 12.f : 0.01f)); });
					}
					else
						tableIndex = clamp(tableIndex + d, 0, kTableSize - 1);
					break;
				}
				if (ctrl) {
					nudge(-d * (shift ? 12 : 1), 3);
					break;
				}
				focus = 0;
				row = clamp(row + d, 0, kRows - 1);
				break;
			}
			case GLFW_KEY_HOME:
				if (k.numPatterns)
					moveCursor(k.patternStart[curPattern()], shift);
				break;
			case GLFW_KEY_END:
				if (k.numPatterns)
					moveCursor(k.patternStart[curPattern()] + std::max(0, k.patternLength[curPattern()] - 1), shift);
				break;
			case GLFW_KEY_PAGE_UP: gotoPattern(-1); break;
			case GLFW_KEY_PAGE_DOWN: gotoPattern(1); break;
			case GLFW_KEY_TAB:
				track = (track + (shift ? kTracks - 1 : 1)) % kTracks;
				moveCursor(cursor, false);
				break;
			case GLFW_KEY_ENTER:
			case GLFW_KEY_KP_ENTER:
				if (!typing.empty())
					commitTyping();
				else if (row >= NUM_FIELDS && focus == 0)
					toggleFlag(row);
				else if (module && module->hold)
					module->commit();
				break;
			case GLFW_KEY_ESCAPE:
				if (!typing.empty())
					typing.clear();
				else {
					anchor = cursor;
					focus = 0;
				}
				break;
			case GLFW_KEY_BACKSPACE:
				if (!typing.empty())
					typing.pop_back();
				else if (focus == 0)
					deleteSelection();
				break;
			case GLFW_KEY_DELETE:
				if (ctrl) {
					const int t = track, p = curPattern();
					edit("delete pattern", 0, [&](Editor &ed) { ed.deletePattern(t, p); });
					moveCursor(cursor, false);
				}
				else
					deleteSelection();
				break;
			case GLFW_KEY_INSERT:
				insertSteps({stepForInsert()}, shift, "insert step");
				break;
			case GLFW_KEY_SPACE:
				if (module)
					module->params[REGISTRE::RUN_PARAM].setValue(module->params[REGISTRE::RUN_PARAM].getValue() > 0.5f ? 0.f : 1.f);
				break;
			default:
				used = false;
		}
		if (!used && ctrl) {
			used = true;
			if (e.keyName == "c")
				copySelection();
			else if (e.keyName == "x") {
				copySelection();
				deleteSelection();
			}
			else if (e.keyName == "v") {
				if (shift)
					pasteOver();
				else
					insertSteps(clipboard(), false, "paste");
			}
			else if (e.keyName == "d") {
				copySelection();
				insertSteps(clipboard(), false, "duplicate");
			}
			else if (e.keyName == "a") {
				if (k.numPatterns && selFirst() == k.patternStart[curPattern()] && selCount() == k.patternLength[curPattern()]) {
					anchor = 0;
					cursor = k.numSteps - 1;
				}
				else if (k.numPatterns) {
					anchor = k.patternStart[curPattern()];
					cursor = anchor + std::max(0, k.patternLength[curPattern()] - 1);
				}
				clampCursor();
			}
			else
				used = false;
		}
		if (used)
			e.consume(this);
	}

	void onSelectText(const SelectTextEvent &e) override {
		const int c = e.codepoint;
		if (c > 127)
			return;
		const char ch = (char)c;
		// typing a number (or a voltage or a note in a table)
		const bool digit = ch >= '0' && ch <= '9';
		if (focus == 2 && (digit || ch == '.' || ch == '#' || (ch == '-' && typing.empty()) || (!typing.empty() && (ch == '-' || ch == '+' || ch == 'b'))
			|| (std::strchr("ABCDEFGabcdefg", ch) && typing.empty()))) {
			typing += ch;
			e.consume(this);
			return;
		}
		if (digit || (ch == '-' && focus == 1 && typing.empty())) {
			if (typing.size() < 4)
				typing += ch;
			e.consume(this);
			return;
		}
		switch (ch) {
			case '+': case '=': nudge(1, 4); break;
			case '-': nudge(-1, 4); break;
			case '*': nudge(12, 4); break;
			case '/': nudge(-12, 4); break;
			case 'i': insertSteps({stepForInsert()}, false, "insert step"); break;
			case 'I': insertSteps({stepForInsert()}, true, "insert step"); break;
			case 'k': { const int t = track, i = cursor; edit("split step", 0, [&](Editor &ed) { ed.splitStep(t, i); }); break; }
			case 'n': newPattern(); break;
			case 'N': {
				const int t = track, p = curPattern(), pos = cursor - tr().patternStart[curPattern()];
				edit("split pattern", 0, [&](Editor &ed) { ed.splitPattern(t, p, pos); });
				syncPattern();
				break;
			}
			case 'j': { const int t = track, p = curPattern(); edit("join patterns", 0, [&](Editor &ed) { ed.joinPatterns(t, p); }); break; }
			case 's': toggleFlag(row == 5 || row == 1 ? NUM_FIELDS + 1 : NUM_FIELDS); break;
			case 't': toggleFlag(NUM_FIELDS + 2); break;
			case '[': setPartStep(1); break;
			case ']': setPartStep(2); break;
			case 'l': loopSelection(false); break;
			case 'L': loopSelection(true); break;
			case 'p': setPartStep(0); break;
			case 'm': applyMath(false); break;
			case 'M': applyMath(true); break;
			case 'r': reverseRow(); break;
			case ',': case '<': rotateRow(-1); break;
			case '.': case '>': rotateRow(1); break;
			case 'f': follow = !follow; break;
			case 'h':
				if (module)
					module->params[REGISTRE::HOLD_PARAM].setValue(module->hold ? 0.f : 1.f);
				break;
			case 'g':
				if (module)
					module->engine.uiGoPart = module->uiPart.load();
				break;
			default:
				return;
		}
		e.consume(this);
	}

	// ---- mouse
	enum Hit { H_NONE, H_TRACKTAB, H_VIEW, H_FOLLOW, H_PART, H_PATTERN, H_LANE, H_LANEDIV, H_LANEMULT, H_GRID, H_MATHCELL,
		H_MATHWORD, H_MATHTARGET, H_TABLEPANE, H_CURVE, H_REF, H_LIST, H_TABLEWORD, H_SNAP, H_TABLEAB };
	struct HitInfo {
		int kind = H_NONE;
		int a = 0, b = 0;
	};

	HitInfo hit(Vec p) {
		HitInfo h;
		const float x = p.x, y = p.y;
		if (y < HEAD) {
			if (x < 52.f) { h.kind = H_TRACKTAB; h.a = clamp((int)((x - 2.f) / 12.f), 0, kTracks - 1); }
			else if (x < 140.f) h.kind = H_PATTERN;
			else if (x >= 228.f && x < 330.f) h.kind = H_PART;
			else if (x >= 372.f && x < 412.f) h.kind = H_FOLLOW;
			else if (x >= 496.f) { h.kind = H_VIEW; h.a = x >= 534.f; }
			return h;
		}
		if (view == 1) {
			if (y >= SNAP_Y + 6.f) {
				h.kind = H_SNAP;
				h.a = clamp((int)((x - 60.f) / 32.f), 0, 15);
				return x >= 60.f ? h : HitInfo();
			}
			if (y >= SNAP_Y - 18.f && y < SNAP_Y) {
				h.kind = H_TABLEWORD;
				h.a = clamp((int)(x / 96.f), 0, 5);
				return h;
			}
			if (y < CURVE_Y && x >= 110.f && x < 150.f) {
				h.kind = H_TABLEAB;
				h.a = x >= 130.f;
				return h;
			}
			if (x >= CURVE_X && x < CURVE_X + CURVE_W && y >= CURVE_Y && y < CURVE_Y + CURVE_H) {
				h.kind = H_CURVE;
				h.a = clamp((int)std::round((x - CURVE_X - 4.f) / (CURVE_W - 8.f) * (kTableSize - 1)), 0, kTableSize - 1);
				return h;
			}
			if (x >= REF_X && x < LIST_X - 6.f && y >= 24.f) {
				h.kind = H_REF;
				h.a = (int)((y - 24.f) / 9.f);
				return h.a < NUM_REFS + kUserTables ? h : HitInfo();
			}
			if (x >= LIST_X && y >= 24.f) {
				h.kind = H_LIST;
				h.a = clamp(tableScroll + (int)((y - 24.f) / 10.f), 0, kTableSize - 1);
				return h;
			}
			return h;
		}
		if (y >= LANE_Y && y < LANE_Y + 4 * LANE_H) {
			const int t = clamp((int)((y - LANE_Y) / LANE_H), 0, kTracks - 1);
			h.a = t;
			if (x < 14.f) h.kind = H_LANE, h.b = -1;
			else if (x < 32.f) h.kind = H_LANEDIV;
			else if (x < LANE_LABEL) h.kind = H_LANEMULT;
			else { h.kind = H_LANE; h.b = laneStepAt(t, x); }
			return h;
		}
		if (x >= TABLE_X && y >= GRID_Y - 4.f) {
			h.kind = H_TABLEPANE;
			h.a = clamp(tableScroll + (int)((y - GRID_Y - 8.f) / 10.5f), 0, kTableSize - 1);
			return h;
		}
		if (y >= GRID_Y && y < gridBottom() && x >= ROW_LABEL && x < GRID_W) {
			h.kind = H_GRID;
			int r = 0;
			while (r < kRows - 1 && y >= rowTop(r + 1))
				r++;
			h.a = r;
			h.b = (int)((x - ROW_LABEL) / colW());   // column
			return h;
		}
		if (y >= MATH_Y) {
			if (y < MATH_Y + 12.f && x >= 120.f && x < 260.f) {
				h.kind = H_MATHTARGET;
				h.a = clamp((int)((x - 120.f) / 40.f), 0, 2);
				return h;
			}
			if (y >= MATH_Y + 22.f && x < 4 * 74.f) {
				h.kind = H_MATHCELL;
				h.a = clamp((int)(x / 74.f), 0, 3);
				h.b = clamp((int)((x - h.a * 74.f - 4.f) / 14.f), 0, 4);
				return h;
			}
			if (x >= 300.f && x < GRID_W && y >= MATH_Y + 12.f) {
				h.kind = H_MATHWORD;
				h.a = clamp((int)((x - 300.f) / 56.f), 0, 2) + 3 * (y >= MATH_Y + 30.f);
				return h;
			}
		}
		return h;
	}

	// The grid's column -> step index (or -1)
	int gridStep(int col) {
		Track &k = tr();
		if (k.numPatterns == 0)
			return -1;
		const int pos = scroll + col;
		if (pos < 0 || pos >= k.patternLength[shownPattern])
			return -1;
		return k.patternStart[shownPattern] + pos;
	}

	// Lanes: steps as long as their duration over the track's whole length
	int64_t laneTotal(const Track &k) {
		int64_t s = 0;
		for (int i = 0; i < k.numSteps; i++)
			s += k.steps[i].v[DUR];
		return std::max<int64_t>(1, s);
	}
	int laneStepAt(int t, float x) {
		const Track &k = song().tracks[t];
		const int64_t total = laneTotal(k);
		const float w = box.size.x - LANE_LABEL - 2.f;
		const double pos = (x - LANE_LABEL) / w * total;
		int64_t acc = 0;
		for (int i = 0; i < k.numSteps; i++) {
			acc += k.steps[i].v[DUR];
			if (pos < acc)
				return i;
		}
		return k.numSteps - 1;
	}

	void openGridMenu() {
		if (!module)
			return;
		REGISTRE *m = module;
		const int t = track, p = curPattern();
		Menu *menu = createMenu();
		menu->addChild(createMenuLabel(rack::string::f("Track %d, pattern %d", t + 1, p + 1)));
		menu->addChild(createMenuItem("Insert a step after", "i / Insert", [=]() { insertSteps({stepForInsert()}, false, "insert step"); }));
		menu->addChild(createMenuItem("Insert a step before", "Shift+i", [=]() { insertSteps({stepForInsert()}, true, "insert step"); }));
		menu->addChild(createMenuItem("Split the step", "k", [=]() { const int i = cursor; edit("split step", 0, [&](Editor &ed) { ed.splitStep(t, i); }); }));
		menu->addChild(createMenuItem("Delete the selection", "Delete", [=]() { deleteSelection(); }));
		menu->addChild(createMenuItem("Copy", RACK_MOD_CTRL_NAME "+C", [=]() { copySelection(); }));
		menu->addChild(createMenuItem("Paste after", RACK_MOD_CTRL_NAME "+V", [=]() { insertSteps(clipboard(), false, "paste"); }));
		menu->addChild(createMenuItem("Paste over the values", RACK_MOD_CTRL_NAME "+Shift+V", [=]() { pasteOver(); }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("New pattern after this one", "n", [=]() { newPattern(); }));
		menu->addChild(createMenuItem("Split the pattern before the cursor", "Shift+n", [=]() {
			const int pos = cursor - tr().patternStart[p];
			edit("split pattern", 0, [&](Editor &ed) { ed.splitPattern(t, p, pos); });
		}));
		menu->addChild(createMenuItem("Join with the next pattern", "j", [=]() { edit("join patterns", 0, [&](Editor &ed) { ed.joinPatterns(t, p); }); }));
		menu->addChild(createMenuItem("Delete the pattern", RACK_MOD_CTRL_NAME "+Delete", [=]() { edit("delete pattern", 0, [&](Editor &ed) { ed.deletePattern(t, p); }); }));
		for (int ab = 0; ab < 2; ab++) {
			const uint8_t bit = ab ? SMOOTH_B : SMOOTH_A;
			menu->addChild(createCheckMenuItem(ab ? "Smooth CV B over the whole pattern" : "Smooth CV A over the whole pattern", "",
				[=]() { return (m->doc->tracks[t].patternFlags[p] & bit) != 0; },
				[=]() { edit("smooth pattern", 0, [&](Editor &ed) { ed.s.tracks[t].patternFlags[p] ^= bit; }); }));
		}
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Loop the selection (one step: its pattern)", "l", [=]() { loopSelection(false); }));
		menu->addChild(createMenuItem("Clear the loop", "Shift+l", [=]() { loopSelection(true); }));
		menu->addChild(createMenuItem("Reset step here (on and off)", "p", [=]() { setPartStep(0); }));
	}

	void openTrackMenu(int t) {
		if (!module)
			return;
		REGISTRE *m = module;
		Menu *menu = createMenu();
		menu->addChild(createMenuLabel(rack::string::f("Track %d", t + 1)));
		std::vector<std::string> n99;
		for (int i = 1; i <= 99; i++)
			n99.push_back(std::to_string(i));
		menu->addChild(createIndexSubmenuItem("Clock divider", n99, [=]() { return (size_t)(m->doc->tracks[t].div - 1); },
			[=](size_t i) { edit("divider", 0, [&](Editor &ed) { ed.s.tracks[t].div = (uint8_t)(i + 1); }); }));
		menu->addChild(createIndexSubmenuItem("Clock multiplier", n99, [=]() { return (size_t)(m->doc->tracks[t].mult - 1); },
			[=](size_t i) { edit("multiplier", 0, [&](Editor &ed) { ed.s.tracks[t].mult = (uint8_t)(i + 1); }); }));
		auto flagItem = [&](const char *name, uint8_t bit) {
			menu->addChild(createCheckMenuItem(name, "", [=]() { return (m->doc->tracks[t].flags & bit) != 0; },
				[=]() { edit(name, 0, [&](Editor &ed) { ed.s.tracks[t].flags ^= bit; }); }));
		};
		flagItem("Triggers instead of gates (GATE x 0.1 ms; ratchet: GATE triggers)", TRACK_TRIGGERS);
		flagItem("Smooth CV A over the whole track", SMOOTH_A);
		flagItem("Smooth CV B over the whole track", SMOOTH_B);
		flagItem("Show CV A as notes", NOTES_A);
		flagItem("Show CV B as notes", NOTES_B);
		menu->addChild(createMenuItem("Clear the track (one empty pattern)", "", [=]() {
			edit("clear track", 0, [&](Editor &ed) {
				Track &k = ed.s.tracks[t];
				k.numPatterns = 1;
				k.patternLength[0] = 0;
				k.patternFlags[0] = 0;
				ed.s.layout();
			});
		}));
	}

	// Not OpaqueWidget's: a right-click this screen does not use goes on to the module's menu
	void onButton(const ButtonEvent &e) override {
		if (e.action != GLFW_PRESS)
			return;
		const HitInfo h = hit(e.pos);
		const bool shift = (e.mods & RACK_MOD_MASK) & GLFW_MOD_SHIFT;
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			if (h.kind == H_GRID) {
				const int s = gridStep(h.b);
				if (s >= 0 && (s < selFirst() || s >= selFirst() + selCount()))
					moveCursor(s, false);
				openGridMenu();
				e.consume(this);
			}
			else if (h.kind == H_LANE || h.kind == H_LANEDIV || h.kind == H_LANEMULT || h.kind == H_TRACKTAB) {
				openTrackMenu(h.a);
				e.consume(this);
			}
			else if (h.kind == H_SNAP && module) {
				const int i = h.a;
				REGISTRE *m = module;
				Menu *menu = createMenu();
				menu->addChild(createMenuLabel(rack::string::f("Snapshot %d", i + 1)));
				menu->addChild(createMenuItem("Load", "click", [=]() { m->loadSnapshot(i); }));
				menu->addChild(createMenuItem("Save the song here", "Shift+click", [=]() { m->saveSnapshot(i); }));
				menu->addChild(createMenuItem("Clear", "", [=]() { std::lock_guard<std::recursive_mutex> lock(m->docMutex); m->snapshots[i].clear(); }));
				e.consume(this);
			}
			return;
		}
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		e.consume(this);
		dragStartPosX = e.pos.x;
		dragKind = 0;
		dragDecided = 0;
		dragAcc = Vec();
		dragKey = ++mergeSerial;
		if (h.kind != H_MATHCELL && h.kind != H_LIST && h.kind != H_CURVE && h.kind != H_TABLEPANE)
			commitTyping();
		switch (h.kind) {
			case H_TRACKTAB: track = h.a; moveCursor(cursor, false); break;
			case H_VIEW: view = h.a; break;
			case H_FOLLOW: follow = !follow; break;
			case H_LANE:
				track = h.a;
				if (h.b >= 0) {
					if (follow && module) {
						module->engine.jumpUid = tr().steps[h.b].uid;
						module->engine.jumpTrack = h.a;
					}
					moveCursor(h.b, shift);
				}
				else
					moveCursor(cursor, false);
				break;
			case H_GRID: {
				focus = 0;
				const int s = gridStep(h.b);
				row = h.a;
				if (s < 0)
					break;
				const bool inside = s >= selFirst() && s < selFirst() + selCount() && selCount() > 1;
				if (shift)
					moveCursor(s, true);
				else if (!inside)
					moveCursor(s, false);
				if (row >= NUM_FIELDS && !shift) {
					toggleFlag(row);
					break;
				}
				dragKind = 1;
				break;
			}
			case H_MATHTARGET: mathTarget = h.a; break;
			case H_MATHCELL: focus = 1; mathField = h.a; mathOp = h.b; typing.clear(); dragKind = 4; break;
			case H_MATHWORD:
				switch (h.a) {
					case 0: applyMath(false); break;
					case 1: applyMath(true); break;
					case 2: { const int t = track; edit("clear math", 0, [&](Editor &ed) { ed.s.tracks[t].math.clear(); }); break; }
					case 3: reverseRow(); break;
					case 4: rotateRow(-1); break;
					case 5: rotateRow(1); break;
				}
				break;
			case H_TABLEPANE: {
				// click: the entry becomes the selection's index (on the row of its table)
				tableAB = row == 1 ? 1 : 0;
				tableIndex = h.a;
				focus = 2;
				dragKind = 5;
				const int f0 = selFirst(), n = selCount(), t = track, ab = tableAB, idx = h.a;
				if (tr().numSteps)
					edit("index from table", 0, [&](Editor &ed) {
						for (int i = f0; i < f0 + n; i++)
							ed.s.tracks[t].steps[i].v[ab ? CV_B : CV_A] = (uint8_t)idx;
					});
				break;
			}
			case H_PART: case H_PATTERN: break;
			case H_CURVE: focus = 2; tableIndex = h.a; dragKind = 3; dragValue = tr().table[tableAB][h.a]; break;
			case H_LIST: focus = 2; tableIndex = h.a; dragKind = 5; dragValue = tr().table[tableAB][h.a]; break;
			case H_TABLEAB: tableAB = h.a; break;
			case H_REF: refSel = h.a; break;
			case H_TABLEWORD: tableWord(h.a); break;
			case H_SNAP:
				if (module) {
					if (shift)
						module->saveSnapshot(h.a);
					else
						module->loadSnapshot(h.a);
				}
				break;
			default: break;
		}
		if (dragKind == 3 || dragKind == 5)
			dragValue = tr().table[tableAB][tableIndex];
	}

	void loadTable(int r, float *out) {
		if (r < NUM_REFS)
			referenceTable(r, out);
		else
			std::memcpy(out, userTables().t[r - NUM_REFS], sizeof(float) * kTableSize);
	}

	void tableWord(int w) {
		const int t = track;
		if (w == 0 || w == 1) {        // LOAD > A, LOAD > B
			float tab[kTableSize];
			loadTable(refSel, tab);
			edit("load table", 0, [&](Editor &ed) { std::memcpy(ed.s.tracks[t].table[w], tab, sizeof(tab)); });
		}
		else if (w == 2 || w == 3) {   // SAVE A / B to the selected user table
			if (refSel < NUM_REFS)
				return;
			std::memcpy(userTables().t[refSel - NUM_REFS], tr().table[w - 2], sizeof(float) * kTableSize);
			userTables().save();
		}
		else if (w == 4) {             // ROUND to semitones
			const int ab = tableAB;
			edit("round table", 0, [&](Editor &ed) {
				for (float &v : ed.s.tracks[t].table[ab])
					v = std::round(v * 12.f) / 12.f;
			});
		}
		else if (w == 5) {             // the selected step's index
			const int ab = tableAB;
			if (tr().numSteps)
				tableIndex = tr().steps[cursor].v[ab ? CV_B : CV_A];
		}
	}

	void onDragMove(const DragMoveEvent &e) override {
		if (e.button != GLFW_MOUSE_BUTTON_LEFT || !dragKind)
			return;
		const float zoom = getAbsoluteZoom();
		const Vec d = e.mouseDelta.div(zoom);
		dragAcc = dragAcc.plus(d);
		const bool fine = (APP->window->getMods() & RACK_MOD_MASK) & GLFW_MOD_SHIFT;
		const bool ctrl = (APP->window->getMods() & RACK_MOD_MASK) & RACK_MOD_CTRL;
		if (dragKind == 1) {
			if (!dragDecided) {
				if (std::fabs(dragAcc.x) > 4.f && std::fabs(dragAcc.x) > std::fabs(dragAcc.y))
					dragDecided = 2;
				else if (std::fabs(dragAcc.y) > 3.f)
					dragDecided = 1;
				else
					return;
				if (dragDecided == 2)
					dragKind = 2;
			}
		}
		if (dragKind == 2) {
			// a range: the cursor follows the mouse over the grid's columns
			const int col = (int)std::floor((dragStartX() + dragAcc.x - ROW_LABEL) / colW());
			const int s = gridStep(clamp(col, 0, kCols - 1));
			if (s >= 0)
				cursor = s;
			return;
		}
		const float per = fine ? 18.f : 6.f;
		if (dragKind == 1 || dragKind == 4) {
			int steps = 0;
			while (dragAcc.y <= -per) { steps++; dragAcc.y += per; }
			while (dragAcc.y >= per) { steps--; dragAcc.y -= per; }
			if (!steps)
				return;
			if (dragKind == 1)
				nudge(steps * (ctrl ? 12 : 1), dragKey);
			else
				mathNudge(steps, dragKey);
			return;
		}
		if (dragKind == 3 || dragKind == 5) {
			dragValue = clampVolts(dragValue - d.y * (fine ? 0.001f : 0.02f));
			float v = dragValue;
			if (ctrl)
				v = std::round(v * 12.f) / 12.f;
			const int t = track, ab = tableAB, i = tableIndex;
			edit("table value", dragKey, [&](Editor &ed) { ed.s.tracks[t].table[ab][i] = v; });
		}
	}
	float dragStartPosX = 0.f;
	float dragStartX() { return dragStartPosX; }

	void mathNudge(int d, int key) {
		if (mathField < 0)
			return;
		const int t = track, f = mathField, op = mathOp;
		edit("math setting", key, [&](Editor &ed) {
			int8_t &v = opRef(ed.s.tracks[t].math, f, op);
			v = (int8_t)opStep(op, v, d);
		});
	}

	void onHoverScroll(const HoverScrollEvent &e) override {
		const HitInfo h = hit(e.pos);
		const int d = e.scrollDelta.y > 0 ? 1 : e.scrollDelta.y < 0 ? -1 : 0;
		if (!d)
			return;
		const int mods = APP->window->getMods() & RACK_MOD_MASK;
		const bool ctrl = mods & RACK_MOD_CTRL, shift = mods & GLFW_MOD_SHIFT;
		// one undo action for the wheel on one place
		if (e.pos.minus(wheelPos).norm() > 3.f)
			wheelKey = ++mergeSerial;
		wheelPos = e.pos;
		switch (h.kind) {
			case H_GRID: {
				const int s = gridStep(h.b);
				if (s < 0)
					return;
				row = h.a;
				if (!(s >= selFirst() && s < selFirst() + selCount()))
					moveCursor(s, false);
				if (h.a == DUR && (mods & GLFW_MOD_ALT)) {
					const int t = track, i = s;
					edit("swing", wheelKey, [&](Editor &ed) { ed.swing(t, i, d); });
				}
				else if (h.a < NUM_FIELDS)
					nudge(d * (ctrl ? 12 : 1), wheelKey);
				break;
			}
			case H_MATHCELL: mathField = h.a; mathOp = h.b; focus = 1; mathNudge(d, wheelKey); break;
			case H_TABLEPANE:
			case H_LIST:
			case H_CURVE: {
				const int t = track, i = h.a;
				const int ab = h.kind == H_TABLEPANE ? (row == 1 ? 1 : 0) : tableAB;
				const float step = ctrl ? 1.f / 12.f : shift ? 0.001f : 0.01f;
				edit("table value", wheelKey, [&](Editor &ed) { float &v = ed.s.tracks[t].table[ab][i]; v = clampVolts(v + d * step); });
				break;
			}
			case H_LANEDIV:
			case H_LANEMULT: {
				const int t = h.a;
				const bool div = h.kind == H_LANEDIV;
				edit(div ? "divider" : "multiplier", wheelKey, [&](Editor &ed) {
					uint8_t &v = div ? ed.s.tracks[t].div : ed.s.tracks[t].mult;
					v = (uint8_t)clamp(v + d, 1, 99);
				});
				break;
			}
			case H_PART:
				if (module)
					module->uiPart = clamp(module->uiPart.load() + d, 0, kParts - 1);
				break;
			case H_PATTERN: gotoPattern(-d); break;
			default:
				if (view == 0 && e.pos.x >= TABLE_X)
					tableScroll = clamp(tableScroll - d * 3, 0, kTableSize - 14);
				else if (view == 1 && e.pos.x >= LIST_X)
					tableScroll = clamp(tableScroll - d * 3, 0, kTableSize - 17);
				else
					return;
		}
		e.consume(this);
	}

	void step() override {
		if (module && follow) {
			const uint32_t uid = module->engine.shownUid[track].load();
			const int i = tr().indexOf(uid);
			if (i >= 0) {
				cursor = anchor = i;
				syncPattern();
			}
		}
		if (focus == 2) {
			if (tableIndex < tableScroll)
				tableScroll = tableIndex;
			const int rows = view == 0 ? 14 : 17;
			if (tableIndex >= tableScroll + rows)
				tableScroll = tableIndex - rows + 1;
		}
		OpaqueWidget::step();
	}

	// ---- drawing
	void text(NVGcontext *vg, float x, float y, const std::string &s, int align, NVGcolor c, float size = 9.f) {
		nvgFontSize(vg, size);
		nvgTextAlign(vg, align);
		nvgFillColor(vg, c);
		nvgText(vg, x, y, s.c_str(), nullptr);
	}
	void rect(NVGcontext *vg, float x, float y, float w, float h, NVGcolor c) {
		nvgBeginPath(vg);
		nvgRect(vg, x, y, w, h);
		nvgFillColor(vg, c);
		nvgFill(vg);
	}
	void frame(NVGcontext *vg, float x, float y, float w, float h, NVGcolor c, float sw = 1.f) {
		nvgBeginPath(vg);
		nvgRect(vg, x + 0.5f, y + 0.5f, w - 1.f, h - 1.f);
		nvgStrokeColor(vg, c);
		nvgStrokeWidth(vg, sw);
		nvgStroke(vg);
	}

	void draw(const DrawArgs &args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, theme::kScreenBlack);
		nvgFill(args.vg);
		if (!module)
			drawContent(args.vg);
	}

	void drawLayer(const DrawArgs &args, int layer) override {
		if (layer == 1 && module) {
			std::lock_guard<std::recursive_mutex> lock(module->docMutex);
			drawContent(args.vg);
		}
		OpaqueWidget::drawLayer(args, layer);
	}

	void drawContent(NVGcontext *vg) {
		std::shared_ptr<window::Font> f = monoFont();
		if (!f)
			return;
		nvgFontFaceId(vg, f->handle);
		clampCursor();
		syncPattern();
		drawHeader(vg);
		if (view == 0) {
			drawLanes(vg);
			drawGrid(vg);
			drawMath(vg);
			drawTablePane(vg);
		}
		else
			drawTables(vg);
	}

	bool selected() { return APP->event->getSelectedWidget() == this; }

	void drawHeader(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow, O = theme::kLitOrange;
		const int mid = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE;
		for (int t = 0; t < kTracks; t++) {
			const bool gate = module && module->outputs[REGISTRE::GATE_OUTPUTS + t].getVoltage() > 1.f;
			text(vg, 4.f + 12.f * t, 7.f, std::to_string(t + 1), mid, t == track ? Y : gate ? O : kDim, 10.f);
		}
		Track &k = tr();
		text(vg, 56.f, 7.f, rack::string::f("PAT %02d/%02d", shownPattern + 1, std::max<int>(1, k.numPatterns)), mid, Y);
		const int inPat = k.numSteps ? cursor - k.patternStart[curPattern()] + 1 : 0;
		text(vg, 128.f, 7.f, rack::string::f("STEP %02d/%02d", inPat, k.numPatterns ? k.patternLength[shownPattern] : 0), mid, Y);
		const int part = module ? module->uiPart.load() : 1;
		const int playing = module ? module->engine.shownPart.load() : 1;
		const int pending = module ? module->engine.shownPending.load() : -1;
		std::string ps = part == 0 ? "PART STOP" : rack::string::f("PART %02d", part);
		text(vg, 230.f, 7.f, ps, mid, Y);
		std::string pl = playing == 0 ? "> STOP" : rack::string::f("> %02d", playing);
		if (pending >= 0)
			pl += rack::string::f(" > %02d", pending);
		text(vg, 288.f, 7.f, pl, mid, pending >= 0 ? O : kDim);
		text(vg, 374.f, 7.f, "FOLLOW", mid, follow ? O : kDim);
		if (module && module->hold) {
			const bool waiting = module->engine.shownCommitPending.load();
			const bool blink = !waiting || std::fmod(system::getTime(), 0.5) < 0.25;
			if (blink)
				text(vg, 420.f, 7.f, waiting ? "COMMIT.." : "HOLD", mid, O);
		}
		text(vg, 498.f, 7.f, "STEPS", mid, view == 0 ? Y : kDim);
		text(vg, 536.f, 7.f, "TABLES", mid, view == 1 ? Y : kDim);
		if (selected())
			rect(vg, 0, HEAD - 1.f, box.size.x, 0.6f, kDim);
	}

	void drawLanes(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow;
		const float w = box.size.x - LANE_LABEL - 2.f;
		const int part = module ? module->uiPart.load() : 1;
		for (int t = 0; t < kTracks; t++) {
			const Track &k = song().tracks[t];
			const float y = LANE_Y + t * LANE_H;
			const NVGcolor lc = t == track ? Y : kDim;
			text(vg, 4.f, y + LANE_H / 2, std::to_string(t + 1), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, lc, 9.f);
			text(vg, 14.f, y + LANE_H / 2, rack::string::f("/%d", k.div), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, k.div > 1 ? Y : kDim, 8.f);
			text(vg, 32.f, y + LANE_H / 2, rack::string::f("x%d", k.mult), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, k.mult > 1 ? Y : kDim, 8.f);
			rect(vg, LANE_LABEL, y + 0.5f, w, LANE_H - 1.f, t == track ? kShade : nvgRGB(0x12, 0x12, 0x10));
			const int64_t total = [&]() { int64_t s = 0; for (int i = 0; i < k.numSteps; i++) s += k.steps[i].v[DUR]; return std::max<int64_t>(1, s); }();
			const float sc = w / total;
			int64_t pos = 0;
			const int playUid = module ? (int)module->engine.shownUid[t].load() : -1;
			float playX = -1.f;
			bool first = true;
			float lo = 1e9f, hi = -1e9f;
			for (int i = 0; i < k.numSteps; i++) {
				const float v = k.table[0][k.steps[i].v[CV_A]];
				lo = std::min(lo, v);
				hi = std::max(hi, v);
			}
			const float range = std::max(0.5f, hi - lo);
			for (int i = 0; i < k.numSteps; i++) {
				const Step &st = k.steps[i];
				const float x0 = LANE_LABEL + pos * sc, x1 = LANE_LABEL + (pos + st.v[DUR]) * sc;
				if (st.v[GATE] > 0 && st.v[DUR] > 0)
					rect(vg, x0, y + LANE_H - 3.f, std::max(0.6f, std::min(x1, LANE_LABEL + (pos + std::min(st.v[GATE], st.v[DUR])) * sc) - x0 - 0.3f), 1.6f, theme::kKeyWhite);
				if ((int)st.uid == playUid)
					playX = LANE_LABEL + (pos + std::min<int>(st.v[DUR], module->engine.shownPulse[t].load())) * sc;
				if (k.numPatterns && i == k.patternStart[k.stepPattern[i]] && i > 0)
					rect(vg, x0, y + 0.5f, 0.6f, 2.5f, kDim);
				pos += st.v[DUR];
			}
			// CV A as a line, in a path of its own (the gates above are rectangles)
			nvgBeginPath(vg);
			pos = 0;
			for (int i = 0; i < k.numSteps; i++) {
				const Step &st = k.steps[i];
				const float x0 = LANE_LABEL + pos * sc, x1 = LANE_LABEL + (pos + st.v[DUR]) * sc;
				const float yy = y + LANE_H - 4.f - (k.table[0][st.v[CV_A]] - lo) / range * (LANE_H - 6.f);
				if (st.v[DUR] > 0) {
					if (first)
						nvgMoveTo(vg, x0, yy);
					else
						nvgLineTo(vg, x0, yy);
					nvgLineTo(vg, x1, yy);
					first = false;
				}
				pos += st.v[DUR];
			}
			nvgStrokeColor(vg, theme::kWavePink);
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
			// the shown part's loop and reset step
			auto xOf = [&](int idx, bool end) {
				int64_t p = 0;
				for (int i = 0; i < idx + (end ? 1 : 0) && i < k.numSteps; i++)
					p += k.steps[i].v[DUR];
				return LANE_LABEL + p * sc;
			};
			const int ls = song().partLoopStart[part][t], le = song().partLoopEnd[part][t], rs = song().partReset[part][t];
			if (ls >= 0 || le >= 0) {
				const float a = xOf(std::max(0, ls), false), b = le >= 0 ? xOf(le, true) : LANE_LABEL + w;
				rect(vg, a, y + 0.5f, b - a, 1.2f, t == track ? Y : kDim);
				rect(vg, a, y + 0.5f, 1.f, 4.f, t == track ? Y : kDim);
				rect(vg, b - 1.f, y + 0.5f, 1.f, 4.f, t == track ? Y : kDim);
			}
			if (rs >= 0) {
				const float a = xOf(rs, false);
				nvgBeginPath(vg);
				nvgMoveTo(vg, a, y + LANE_H - 1.f);
				nvgLineTo(vg, a + 4.f, y + LANE_H - 1.f);
				nvgLineTo(vg, a, y + LANE_H - 5.f);
				nvgClosePath(vg);
				nvgFillColor(vg, theme::kLitOrange);
				nvgFill(vg);
			}
			// the shown pattern on the selected track, and the play cursor
			if (t == track && k.numPatterns) {
				const int p = shownPattern;
				const float a = xOf(k.patternStart[p], false), b = xOf(k.patternStart[p] + k.patternLength[p] - 1, true);
				nvgBeginPath(vg);
				nvgRect(vg, a + 0.5f, y + 0.5f, std::max(1.f, b - a - 1.f), LANE_H - 1.f);
				nvgStrokeColor(vg, Y);
				nvgStrokeWidth(vg, 0.8f);
				nvgStroke(vg);
			}
			if (playX >= 0.f)
				rect(vg, playX - 0.5f, y + 0.5f, 1.f, LANE_H - 1.f, theme::kButtonWhite);
		}
	}

	void drawGrid(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow, O = theme::kLitOrange;
		Track &k = tr();
		const float cw = colW();
		const int c0 = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE;
		for (int r = 0; r < kRows; r++)
			text(vg, 2.f, rowTop(r) + kRowH[r] / 2, kRowName[r], NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, r == row ? Y : kDim, 8.f);
		if (k.numPatterns == 0 || k.patternLength[shownPattern] == 0) {
			text(vg, GRID_W / 2, GRID_Y + 40.f, "EMPTY PATTERN: i OR INSERT ADDS A STEP", c0, kDim, 9.f);
			return;
		}
		const uint32_t playUid = module ? module->engine.shownUid[track].load() : 0;
		const int part = module ? module->uiPart.load() : 1;
		const bool notesA = k.flags & NOTES_A, notesB = k.flags & NOTES_B;
		for (int c = 0; c < kCols; c++) {
			const int s = gridStep(c);
			const float x = ROW_LABEL + c * cw;
			if (s < 0)
				break;
			const Step &st = k.steps[s];
			const bool insel = s >= selFirst() && s < selFirst() + selCount();
			if (insel)
				rect(vg, x + 0.5f, GRID_Y, cw - 1.f, gridBottom() - GRID_Y, kShade);
			for (int r = 0; r < kRows; r++) {
				const float y = rowTop(r), h = kRowH[r];
				const bool cur = s == cursor && r == row;
				if (cur)
					rect(vg, x + 0.5f, y + 0.5f, cw - 1.f, h - 1.f, Y);
				const NVGcolor tc = cur ? theme::kScreenBlack : Y;
				if (r == CV_A || r == CV_B) {
					std::string v = (cur && focus == 0 && !typing.empty()) ? typing + "_" : std::to_string(st.v[r]);
					text(vg, x + cw / 2, y + (r == CV_A ? 7.f : h / 2), v, c0, tc, 10.f);
					const bool notes = r == CV_A ? notesA : notesB;
					if (r == CV_A) {
						char buf[16];
						const float volts = k.table[0][st.v[CV_A]];
						if (notes)
							noteName(volts, buf, sizeof(buf), false);
						else
							std::snprintf(buf, sizeof(buf), "%.2f", volts);
						text(vg, x + cw / 2, y + 17.f, buf, c0, cur ? theme::kScreenBlack : theme::kWavePink, 7.5f);
					}
				}
				else if (r == DUR || r == GATE) {
					const int v = st.v[r];
					const float bh = (h - 11.f) * std::min(1.f, v / 16.f);
					if (!cur)
						rect(vg, x + 3.f, y + h - 2.f - bh, cw - 6.f, bh, r == DUR ? theme::kKeyBlack : theme::kKeyWhite);
					std::string vs = (cur && focus == 0 && !typing.empty()) ? typing + "_" : std::to_string(v);
					text(vg, x + cw / 2, y + 5.5f, vs, c0, tc, 9.f);
				}
				else {
					const bool on = Editor::rowValue(st, r);
					// a flag set for the whole pattern or track shows dim
					const uint8_t bit = (uint8_t)(1 << (r - NUM_FIELDS));
					const bool wide = r < 6 && ((k.patternFlags[k.stepPattern[s]] & bit) || (k.flags & bit));
					if (on)
						text(vg, x + cw / 2, y + h / 2, r == 6 ? "R" : r == 4 ? "A" : "B", c0, cur ? theme::kScreenBlack : O, 8.f);
					else
						rect(vg, x + cw / 2 - 1.f, y + h / 2 - 1.f, 2.f, 2.f, cur ? theme::kScreenBlack : wide ? O : kLine);
				}
			}
			// column number, loop and reset marks, play cursor
			const int inPat = s - k.patternStart[shownPattern] + 1;
			text(vg, x + cw / 2, gridBottom() + 5.f, std::to_string(inPat), c0, kDim, 7.f);
			const Part &pt = song().parts[part];
			if (pt.loopStart[track] == st.uid)
				text(vg, x + 1.f, GRID_Y - 3.f, "[", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, Y, 9.f);
			if (pt.loopEnd[track] == st.uid)
				text(vg, x + cw - 1.f, GRID_Y - 3.f, "]", NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, Y, 9.f);
			if (pt.reset[track] == st.uid)
				text(vg, x + cw / 2, GRID_Y - 3.f, "R", c0, O, 8.f);
			if (playUid && st.uid == playUid)
				rect(vg, x + 2.f, gridBottom() - 1.f, cw - 4.f, 2.f, theme::kButtonWhite);
			rect(vg, x, GRID_Y, 0.5f, gridBottom() - GRID_Y, inPat % 4 == 1 ? kDim : kLine);
		}
		// the selection's frame
		const int ps = k.patternStart[shownPattern];
		const int a = std::max(selFirst() - ps - scroll, 0), b = std::min(selFirst() + selCount() - 1 - ps - scroll, kCols - 1);
		if (selCount() > 1 && b >= a)
			frame(vg, ROW_LABEL + a * cw, GRID_Y, (b - a + 1) * cw, gridBottom() - GRID_Y, Y);
		if (k.patternLength[shownPattern] > kCols) {
			// where the window is in a long pattern
			const float sw = GRID_W - ROW_LABEL;
			const float pl = k.patternLength[shownPattern];
			rect(vg, ROW_LABEL + sw * scroll / pl, gridBottom() + 9.f, sw * kCols / pl, 1.5f, kDim);
		}
	}

	void drawMath(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow;
		rect(vg, 0, MATH_Y - 2.f, GRID_W, 0.6f, kLine);
		text(vg, 2.f, MATH_Y + 6.f, "MATH", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, Y, 9.f);
		int f0, n;
		target(f0, n);
		text(vg, 40.f, MATH_Y + 6.f, rack::string::f("ON %d", n), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, kDim, 8.f);
		static const char *targets[3] = {"SEL", "PAT", "TRK"};
		for (int i = 0; i < 3; i++)
			text(vg, 120.f + 40.f * i, MATH_Y + 6.f, targets[i], NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, mathTarget == i ? Y : kDim, 9.f);
		const Transform &m = tr().math;
		static const char *fieldName[4] = {"CV A", "CV B", "DUR", "GATE"};
		for (int f = 0; f < NUM_FIELDS; f++) {
			const float x = f * 74.f;
			text(vg, x + 4.f, MATH_Y + 17.f, fieldName[f], NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, kDim, 8.f);
			for (int op = 0; op < 5; op++) {
				const float cx = x + 4.f + op * 14.f + 7.f;
				Transform id;
				const int v = opRef(const_cast<Transform &>(m), f, op);
				const bool changed = v != opRef(id, f, op);
				const bool cur = focus == 1 && mathField == f && mathOp == op;
				if (cur)
					rect(vg, cx - 7.f, MATH_Y + 22.f, 14.f, 22.f, kShade);
				text(vg, cx, MATH_Y + 27.f, kOpName[op], NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, kDim, 6.5f);
				std::string vs = (cur && !typing.empty()) ? typing + "_" : opText(op, v);
				text(vg, cx, MATH_Y + 38.f, vs, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, changed || cur ? Y : kDim, 8.f);
			}
		}
		static const char *words[6] = {"APPLY", "INVERT", "CLEAR", "REVERSE", "< ROT", "ROT >"};
		for (int i = 0; i < 6; i++)
			text(vg, 300.f + 56.f * (i % 3), MATH_Y + 21.f + 18.f * (i / 3), words[i], NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, Y, 9.f);
	}

	void drawTablePane(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow;
		Track &k = tr();
		const int ab = row == 1 ? 1 : 0;
		const float x0 = TABLE_X, w = box.size.x - TABLE_X - 2.f;
		rect(vg, x0 - 4.f, GRID_Y - 4.f, 0.6f, box.size.y - GRID_Y + 2.f, kLine);
		text(vg, x0, GRID_Y + 2.f, ab ? "TABLE B" : "TABLE A", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, Y, 9.f);
		const int used = k.numSteps ? k.steps[cursor].v[ab ? CV_B : CV_A] : -1;
		if (focus != 2 && used >= 0 && (used < tableScroll || used >= tableScroll + 14))
			tableScroll = clamp(used - 6, 0, kTableSize - 14);
		const bool notes = k.flags & (ab ? NOTES_B : NOTES_A);
		for (int j = 0; j < 14; j++) {
			const int i = tableScroll + j;
			if (i >= kTableSize)
				break;
			const float y = GRID_Y + 8.f + j * 10.5f;
			const bool cur = i == used;
			const bool edit = focus == 2 && i == tableIndex && tableAB == ab;
			if (cur)
				rect(vg, x0 - 2.f, y, w + 2.f, 10.f, Y);
			else if (edit)
				rect(vg, x0 - 2.f, y, w + 2.f, 10.f, kShade);
			const NVGcolor c = cur ? theme::kScreenBlack : Y;
			text(vg, x0, y + 5.f, rack::string::f("%02d", i), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, cur ? c : kDim, 8.5f);
			std::string vs = (edit && !typing.empty()) ? typing + "_" : rack::string::f("%+.3f", k.table[ab][i]);
			text(vg, x0 + 16.f, y + 5.f, vs, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, c, 8.5f);
			if (notes) {
				char buf[16];
				noteName(k.table[ab][i], buf, sizeof(buf), false);
				text(vg, x0 + w - 1.f, y + 5.f, buf, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, cur ? c : theme::kWavePink, 8.f);
			}
		}
	}

	void drawTables(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow;
		Track &k = tr();
		const float *tab = k.table[tableAB];
		text(vg, 4.f, 21.f, rack::string::f("TRACK %d TABLE", track + 1), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, Y, 9.f);
		text(vg, 112.f, 21.f, "A", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, tableAB == 0 ? Y : kDim, 10.f);
		text(vg, 132.f, 21.f, "B", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, tableAB == 1 ? Y : kDim, 10.f);
		// the curve: -5 to 10 V
		auto yOf = [&](float v) { return CURVE_Y + CURVE_H - (v - kMinVolts) / (kMaxVolts - kMinVolts) * CURVE_H; };
		auto xOf = [&](int i) { return CURVE_X + 4.f + i * (CURVE_W - 8.f) / (kTableSize - 1); };
		for (int v = -4; v <= 10; v += 2) {
			rect(vg, CURVE_X, yOf((float)v), CURVE_W, 0.5f, v == 0 ? kDim : kLine);
			text(vg, CURVE_X - 3.f, yOf((float)v), rack::string::f("%d", v), NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, kDim, 7.f);
		}
		frame(vg, CURVE_X, CURVE_Y, CURVE_W, CURVE_H, kLine);
		// which indices the track's steps use
		bool used[kTableSize] = {};
		for (int i = 0; i < k.numSteps; i++)
			used[k.steps[i].v[tableAB ? CV_B : CV_A]] = true;
		for (int i = 0; i < kTableSize; i++)
			if (used[i])
				rect(vg, xOf(i) - 0.5f, CURVE_Y + CURVE_H - 3.f, 1.f, 3.f, Y);
		nvgBeginPath(vg);
		for (int i = 0; i < kTableSize; i++) {
			if (i == 0)
				nvgMoveTo(vg, xOf(i), yOf(tab[i]));
			else
				nvgLineTo(vg, xOf(i), yOf(tab[i]));
		}
		nvgStrokeColor(vg, theme::kWavePink);
		nvgStrokeWidth(vg, 1.2f);
		nvgStroke(vg);
		for (int i = 0; i < kTableSize; i++) {
			nvgBeginPath(vg);
			nvgCircle(vg, xOf(i), yOf(tab[i]), i == tableIndex ? 2.2f : 1.f);
			nvgFillColor(vg, i == tableIndex ? Y : theme::kWavePink);
			nvgFill(vg);
		}
		rect(vg, xOf(tableIndex) - 0.25f, CURVE_Y, 0.5f, CURVE_H, kDim);
		char note[16];
		noteName(tab[tableIndex], note, sizeof(note));
		text(vg, CURVE_X + 4.f, CURVE_Y + CURVE_H + 8.f, rack::string::f("%02d  %+.3f V  %s", tableIndex, tab[tableIndex], note),
			NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, Y, 9.f);
		text(vg, CURVE_X + CURVE_W, CURVE_Y + CURVE_H + 8.f, "DRAG  SHIFT FINE  " RACK_MOD_CTRL_NAME " SEMITONES", NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, kDim, 7.f);
		// reference and user tables
		text(vg, REF_X, 18.f, "REFERENCE", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, kDim, 8.f);
		for (int r = 0; r < NUM_REFS + kUserTables; r++) {
			const float y = 24.f + r * 9.f;
			if (r == refSel)
				rect(vg, REF_X - 2.f, y, LIST_X - REF_X - 6.f, 9.f, Y);
			const std::string name = r < NUM_REFS ? refName(r) : rack::string::f("USER %d", r - NUM_REFS + 1);
			text(vg, REF_X, y + 4.5f, name, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, r == refSel ? theme::kScreenBlack : r < NUM_REFS ? Y : kDim, 8.f);
		}
		// the list
		const bool notes = k.flags & (tableAB ? NOTES_B : NOTES_A);
		for (int j = 0; j < 17; j++) {
			const int i = tableScroll + j;
			if (i >= kTableSize)
				break;
			const float y = 24.f + j * 10.f;
			const bool cur = i == tableIndex;
			if (cur)
				rect(vg, LIST_X - 2.f, y, box.size.x - LIST_X, 10.f, focus == 2 ? Y : kShade);
			const NVGcolor c = cur && focus == 2 ? theme::kScreenBlack : Y;
			text(vg, LIST_X, y + 5.f, rack::string::f("%02d", i), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, cur ? c : used[i] ? Y : kDim, 8.5f);
			std::string vs = (cur && focus == 2 && !typing.empty()) ? typing + "_" : rack::string::f("%+.3f V", tab[i]);
			text(vg, LIST_X + 18.f, y + 5.f, vs, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, c, 8.5f);
			if (notes) {
				noteName(tab[i], note, sizeof(note));
				text(vg, box.size.x - 4.f, y + 5.f, note, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, cur && focus == 2 ? c : theme::kWavePink, 8.f);
			}
		}
		// the table words
		const std::string target = refSel < NUM_REFS ? "" : rack::string::f("%d", refSel - NUM_REFS + 1);
		const std::string words[6] = {"LOAD > A", "LOAD > B", refSel < NUM_REFS ? "SAVE A > USER" : "SAVE A > USER " + target,
			refSel < NUM_REFS ? "SAVE B > USER" : "SAVE B > USER " + target, "ROUND", "GO TO STEP"};
		for (int i = 0; i < 6; i++)
			text(vg, i * 96.f + 4.f, SNAP_Y - 9.f, words[i], NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, (i == 2 || i == 3) && refSel < NUM_REFS ? kDim : Y, 8.5f);
		// snapshots
		rect(vg, 0, SNAP_Y + 1.f, box.size.x, 0.6f, kLine);
		text(vg, 4.f, SNAP_Y + 17.f, "SNAPSHOTS", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, kDim, 8.f);
		for (int i = 0; i < 16; i++) {
			const float x = 60.f + i * 32.f;
			const bool full = module && !module->snapshots[i].empty();
			frame(vg, x, SNAP_Y + 8.f, 29.f, 18.f, full ? Y : kLine);
			text(vg, x + 14.5f, SNAP_Y + 17.f, std::to_string(i + 1), NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, full ? Y : kDim, 9.f);
		}
	}
};

// HOLD's and COMMIT's buttons, GO's: lit while a commit or a part waits
struct RegistreButton : BidooRoundButton {
	std::function<bool()> lit;
	void draw(const DrawArgs &args) override {
		const float c = box.size.x / 2;
		engine::ParamQuantity *pq = getParamQuantity();
		bool on = pq && pq->getValue() > 0.5f;
		if (lit)
			on = on || lit();
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, c - 0.5f);
		nvgFillColor(args.vg, theme::kScreenBlack);
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, c * inner);
		nvgFillColor(args.vg, on ? theme::kLitOrange : theme::kButtonWhite);
		nvgFill(args.vg);
	}
};

} // namespace registre_ui

struct REGISTREWidget : BidooWidget {
	registre_ui::RegistreScreen *screen = nullptr;
	float lastCommit = 0.f, lastGo = 0.f;

	REGISTREWidget(REGISTRE *module) {
		using namespace registre_ui;
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/REGISTRE.svg"));
		theme::addScrews(this);

		const float w = box.size.x;
		const int n = 12;              // jack columns, as tools/panels/registre.py N
		const int nb = 16;             // the buttons' columns on row 1, as registre.py NB
		auto at = [&](float i, float k) { return theme::jackAt(i, n, k, w); };

		screen = new RegistreScreen;
		screen->module = module;
		screen->box = theme::screen(1, w);
		addChild(screen);

		const int ins[6] = {REGISTRE::CLOCK_INPUT, REGISTRE::RESET_INPUT, REGISTRE::RUN_INPUT, REGISTRE::TRANSPOSE_INPUT,
			REGISTRE::PART_INPUT, REGISTRE::GO_INPUT};
		for (int i = 0; i < 6; i++)
			addInput(theme::input<PJ301MPort>(at(i, 1), module, ins[i]));
		const int buttons[4] = {REGISTRE::RUN_PARAM, REGISTRE::GO_PARAM, REGISTRE::HOLD_PARAM, REGISTRE::COMMIT_PARAM};
		for (int i = 0; i < 4; i++) {
			RegistreButton *b = theme::param<RegistreButton>(theme::at(8 + i, nb, 1, w), module, buttons[i]);
			if (module && buttons[i] == REGISTRE::GO_PARAM)
				b->lit = [=]() { return module->engine.shownPending.load() >= 0; };
			if (module && buttons[i] == REGISTRE::COMMIT_PARAM)
				b->lit = [=]() { return module->engine.shownCommitPending.load(); };
			addParam(b);
		}
		addOutput(theme::output<PJ301MPort>(at(9, 1), module, REGISTRE::POLY_A_OUTPUT));
		addOutput(theme::output<PJ301MPort>(at(10, 1), module, REGISTRE::POLY_B_OUTPUT));
		addOutput(theme::output<PJ301MPort>(at(11, 1), module, REGISTRE::POLY_GATE_OUTPUT));
		for (int t = 0; t < kTracks; t++) {
			addOutput(theme::output<PJ301MPort>(at(3 * t, 0), module, REGISTRE::A_OUTPUTS + t));
			addOutput(theme::output<PJ301MPort>(at(3 * t + 1, 0), module, REGISTRE::B_OUTPUTS + t));
			addOutput(theme::output<PJ301MPort>(at(3 * t + 2, 0), module, REGISTRE::GATE_OUTPUTS + t));
		}
	}

	// The buttons that act on the document are read here, on the UI thread
	void step() override {
		REGISTRE *m = dynamic_cast<REGISTRE *>(module);
		if (m) {
			const bool hold = m->params[REGISTRE::HOLD_PARAM].getValue() > 0.5f;
			if (hold != m->hold)
				m->setHold(hold);
			const float c = m->params[REGISTRE::COMMIT_PARAM].getValue();
			if (c > 0.5f && lastCommit <= 0.5f && m->hold)
				m->commit();
			lastCommit = c;
			const float g = m->params[REGISTRE::GO_PARAM].getValue();
			if (g > 0.5f && lastGo <= 0.5f)
				m->engine.uiGoPart = m->uiPart.load();
			lastGo = g;
		}
		BidooWidget::step();
	}

	void appendContextMenu(Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		REGISTRE *m = dynamic_cast<REGISTRE *>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexSubmenuItem("Part transition", {"First: when the first track ends its loop", "Last: once every track has ended one", "Now: at once, without reset"},
			[=]() { return (size_t)m->doc->transition; },
			[=](size_t i) { m->edit("transition", 0, [&](Editor &ed) { ed.s.transition = (uint8_t)i; }); }));
		menu->addChild(createIndexSubmenuItem("Commit (HOLD) at the end of", {"Nothing: at once", "A step", "A pattern", "A loop"},
			[=]() { return (size_t)m->commitAt; }, [=](size_t i) { m->commitAt = (int)i; }));
		menu->addChild(createIndexSubmenuItem("Commit timed by track", {"1", "2", "3", "4"},
			[=]() { return (size_t)m->commitTrack; }, [=](size_t i) { m->commitTrack = (int)i; }));
		menu->addChild(createMenuItem("Clear every part's loops and reset steps", "", [=]() {
			m->edit("clear parts", 0, [&](Editor &ed) { for (auto &p : ed.s.parts) p = Part(); });
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createSubmenuItem("Keyboard (click the screen first)", "", [=](Menu *sub) {
			static const char *lines[] = {
				"Arrows: move, Shift: select, Home/End, PgUp/PgDn: pattern, Tab: track",
				"0-9 then Enter: type a value (Esc cancels); + - one, * / twelve",
				RACK_MOD_CTRL_NAME "+Up/Down: change; Alt+Left/Right: swing (move the step's end)",
				"i / Insert: insert after, Shift: before; k: split; Delete: delete",
				RACK_MOD_CTRL_NAME "+C X V D: copy, cut, paste after, duplicate; +Shift+V: paste over",
				RACK_MOD_CTRL_NAME "+A: the pattern, again: the track",
				"n: new pattern, Shift+n: split pattern, j: join, " RACK_MOD_CTRL_NAME "+Delete: delete pattern",
				"s: smooth, t: ratchet, Enter: toggle a flag row",
				"[ ]: loop start / end, l: loop selection (or pattern), Shift+l: clear, p: reset step",
				"m: MATH, Shift+m: inverted, r: reverse the row, < >: rotate it",
				"Space: run, f: follow, h: hold, Enter: commit, g: go (cue the part)",
				RACK_MOD_CTRL_NAME "+Z: undo",
			};
			for (const char *l : lines)
				sub->addChild(createMenuLabel(l));
		}));
	}
};

Model *modelREGISTRE = createModel<REGISTRE, REGISTREWidget>("REGISTRE");
