// rEgIStRE: an indexed four-track sequencer. Each step holds two indices into its track's voltage tables, a duration and
// a gate in clock pulses; patterns divide the tracks; parts hold loops and reset steps and switch at the end of a loop;
// groups of steps take transforms and CVs from the X, Y and Z bus; recording writes steps from CV and gate inputs.
// The sequencer is src/RegistreEngine.hpp; this file is the module, its screen (mouse and keyboard) and its undo.
#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "RegistreEngine.hpp"
#include <osdialog.h>
#include <mutex>

using namespace registre;

struct REGISTRE : BidooModule {
	enum ParamIds {
		RUN_PARAM,
		GO_PARAM,
		HOLD_PARAM,
		COMMIT_PARAM,
		PUNCH_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		RESET_INPUT,
		RUN_INPUT,
		TRANSPOSE_INPUT,
		PART_INPUT,
		GO_INPUT,
		ENUMS(MOD_CV_INPUTS, kChannels),
		ENUMS(MOD_GATE_INPUTS, kChannels),
		REC_A_INPUT,
		REC_B_INPUT,
		REC_GATE_INPUT,
		REC_DUR_INPUT,
		REC_INSERT_INPUT,
		REC_DELETE_INPUT,
		PUNCH_INPUT,
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
	dsp::SchmittTrigger clockTrigger, resetTrigger, runTrigger, goTrigger, insertTrigger, deleteTrigger;
	dsp::SchmittTrigger modGate[kChannels], punchGate;

	// The document: what the screen shows and edits, on the UI thread. The audio plays copies of it (Engine::handoff).
	// The lock is only between the UI and the patch's saving and loading: process() never takes it
	std::recursive_mutex docMutex;
	std::unique_ptr<Song> doc{new Song};
	std::unique_ptr<Song> committed{new Song};        // what was last handed to the audio (HOLD discards back to it)
	std::vector<uint8_t> snapshots[16];
	std::string snapshotNames[16];
	std::mt19937 rng{std::random_device{}()};
	std::atomic<int> uiPart{1};                        // the part whose loops and reset steps the screen edits
	int commitAt = COMMIT_PATTERN;                     // a commit in HOLD waits for the end of a step, pattern or loop
	int commitTrack = 0;
	bool hold = false;
	int midiPulsesPerBeat = 4, midiBeatsPerBar = 4;

	// Recording, on the UI thread: where real-time takes go, the step that step recording follows, the edit cursor
	uint32_t editUid[kTracks] = {};                    // the screen's cursor on each track
	bool cursorMoved[kTracks] = {};                    // recording moved it: the screen follows
	int takePattern[kTracks] = {}, takePos[kTracks] = {};
	uint32_t stepRecUid[kTracks] = {};
	int recKey = 0;
	double alterTime = -10.0;

	REGISTRE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(RUN_PARAM, 0.f, 1.f, 1.f, "Run", {"Stopped", "Running"});
		configButton(GO_PARAM, "Go: cue the part shown on the screen (it starts at the end of a loop, as Transition says)");
		configSwitch(HOLD_PARAM, 0.f, 1.f, 0.f, "Hold: edits wait for COMMIT", {"Off: edits play at once", "On: edits wait for COMMIT"});
		configButton(COMMIT_PARAM, "Commit the held edits (at the end of a step, pattern or loop: see the menu; twice: now)");
		configSwitch(PUNCH_PARAM, 0.f, 1.f, 0.f, "Record: punch in the armed tracks", {"Punched out", "Punched in"});
		configInput(CLOCK_INPUT, "Clock: each pulse is a pulse of every track (before its divider and multiplier)");
		configInput(RESET_INPUT, "Reset: each track goes to its reset step (the part's, or the first), at once or at the end of a step, pattern or loop (menu)");
		configInput(RUN_INPUT, "Run: a trigger starts or stops");
		configInput(TRANSPOSE_INPUT, "Transpose CV A: 1/12 V moves the index by one (polyphonic: channel n for track n)");
		configInput(PART_INPUT, "Part: 0.1 V per part, 0 V stops (0 to 9.9 V for parts 0 to 99)");
		configInput(GO_INPUT, "Go: a gate cues the part PART selects (while it stays high, the cue follows PART)");
		static const char *xyz[3] = {"X", "Y", "Z"};
		for (int c = 0; c < kChannels; c++) {
			configInput(MOD_CV_INPUTS + c, rack::string::f("%s CV: added to the steps of the groups that use it (their slopes)", xyz[c]));
			configInput(MOD_GATE_INPUTS + c, rack::string::f("%s gate: high (above 1.5 V) switches the groups to their %s HIGH transforms", xyz[c], xyz[c]));
		}
		configInput(REC_A_INPUT, "Record CV A: quantized to the track's table A");
		configInput(REC_B_INPUT, "Record CV B: quantized to the track's table B");
		configInput(REC_GATE_INPUT, "Record gate (real-time: the notes; step and alter: GATE, 20 per volt, 99 from 5 V)");
		configInput(REC_DUR_INPUT, "Record duration (step and alter: DUR, 20 per volt, 99 from 5 V)");
		configInput(REC_INSERT_INPUT, "Step recording: insert a step after the cursor (it follows the inputs while held)");
		configInput(REC_DELETE_INPUT, "Step recording: delete the step at the cursor");
		configInput(PUNCH_INPUT, "Punch: punched in while high (with the REC button)");
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
		for (int i = 0; i < 16; i++) {
			snapshots[i].clear();
			snapshotNames[i].clear();
		}
		uiPart = 1;
		hold = false;
		params[HOLD_PARAM].setValue(0.f);
		for (int t = 0; t < kTracks; t++)
			engine.armed[t] = false;
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
		for (int c = 0; c < kChannels; c++) {
			io.modCv[c] = inputs[MOD_CV_INPUTS + c].getVoltage();
			modGate[c].process(inputs[MOD_GATE_INPUTS + c].getVoltage(), 0.5f, 1.5f);
			io.modHigh[c] = modGate[c].isHigh();
		}
		io.recAOn = inputs[REC_A_INPUT].isConnected();
		io.recBOn = inputs[REC_B_INPUT].isConnected();
		io.recGateOn = inputs[REC_GATE_INPUT].isConnected();
		io.recDurOn = inputs[REC_DUR_INPUT].isConnected();
		io.recA = inputs[REC_A_INPUT].getVoltage();
		io.recB = inputs[REC_B_INPUT].getVoltage();
		io.recGateV = inputs[REC_GATE_INPUT].getVoltage();
		io.recDurV = inputs[REC_DUR_INPUT].getVoltage();
		io.recInsert = insertTrigger.process(inputs[REC_INSERT_INPUT].getVoltage(), 0.5f, 1.5f);
		io.recDelete = deleteTrigger.process(inputs[REC_DELETE_INPUT].getVoltage(), 0.5f, 1.5f);
		punchGate.process(inputs[PUNCH_INPUT].getVoltage(), 0.5f, 1.5f);
		io.punch = params[PUNCH_PARAM].getValue() > 0.5f || punchGate.isHigh();
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
	// mergeKey in a row (a drag, the wheel on one cell, a recorded take) are one action. UI thread.
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
			std::unique_ptr<Song> kept(new Song(*committed));
			edit("drop held edits", 0, [&](Editor &ed) { ed.s = *kept; });
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

	bool importMidiFile(const std::string &path) {
		std::vector<uint8_t> data = system::readFile(path);
		if (data.empty())
			return false;
		bool ok = false;
		edit("import MIDI file", 0, [&](Editor &ed) { ok = importMidi(data.data(), data.size(), ed.s, midiPulsesPerBeat, midiBeatsPerBar); });
		if (ok && !hold)
			engine.resetRequest = true;
		return ok;
	}

	// ---- What the audio recorded, into the document (UI thread, once a frame)
	void insertRecorded(int t, const Step *steps, int n, int key) {
		edit("record", key, [&](Editor &ed) {
			Track &k = ed.s.tracks[t];
			for (int j = 0; j < n; j++) {
				if (takePattern[t] >= k.numPatterns || k.patternLength[takePattern[t]] >= kMaxPatternSteps) {
					// the pattern is full: a new one after it
					const int p = std::min<int>(takePattern[t] + 1, k.numPatterns);
					if (!ed.insertPattern(t, p, nullptr, 0))
						return;
					takePattern[t] = p;
					takePos[t] = 0;
				}
				if (!ed.insertSteps(t, takePattern[t], takePos[t], &steps[j], 1))
					return;
				takePos[t]++;
			}
		});
	}

	void drainRecording() {
		RecEvent e;
		while (engine.recQueue.pop(e)) {
			const int t = e.track;
			Track &k = doc->tracks[t];
			const int cur = k.indexOf(editUid[t]);
			switch (e.type) {
				case EV_TAKE: {
					// where the take goes: a new pattern at the end of the track, or after the cursor's pattern, or after
					// the cursor's step
					recKey = (int)(random::u32() & 0x7fffffff) | 1;
					const int focus = doc->rec.focus;
					if (focus == REC_AT_STEP && cur >= 0) {
						takePattern[t] = k.stepPattern[cur];
						takePos[t] = cur - k.patternStart[takePattern[t]] + 1;
					}
					else {
						const int p = (focus == REC_PATTERN && cur >= 0) ? k.stepPattern[cur] + 1 : k.numPatterns;
						edit("record", recKey, [&](Editor &ed) { ed.insertPattern(t, p, nullptr, 0); });
						takePattern[t] = p;
						takePos[t] = 0;
					}
					break;
				}
				case EV_NOTE: {
					Step proto;
					proto.v[CV_A] = e.a;
					proto.v[CV_B] = e.b;
					Step steps[16];
					const int n = noteSteps(e.dur, e.gate, proto, steps, 16);
					insertRecorded(t, steps, n, recKey);
					break;
				}
				case EV_INSERT: {
					// step recording: a step after the cursor, with the inputs' values; it follows them while INSERT is held
					Step s = cur >= 0 ? k.steps[cur] : Step();
					s.groups = 0;
					stepValues(t, s);
					int p = 0, pos = 0;
					if (cur >= 0) {
						p = k.stepPattern[cur];
						pos = cur - k.patternStart[p] + 1;
					}
					else if (k.numPatterns == 0)
						edit("step record", 0, [&](Editor &ed) { ed.insertPattern(t, 0, nullptr, 0); });
					uint32_t uid = 0;
					edit("step record", 0, [&](Editor &ed) {
						if (ed.insertSteps(t, p, pos, &s, 1))
							uid = ed.s.tracks[t].steps[ed.s.tracks[t].patternStart[p] + pos].uid;
					});
					if (uid) {
						stepRecUid[t] = uid;
						editUid[t] = uid;
						cursorMoved[t] = true;
					}
					break;
				}
				case EV_DELETE:
					if (cur >= 0) {
						const uint32_t prev = cur > 0 ? k.steps[cur - 1].uid : (k.numSteps > 1 ? k.steps[1].uid : 0);
						edit("step record", 0, [&](Editor &ed) { ed.deleteSteps(t, cur, 1); });
						editUid[t] = prev;
						cursorMoved[t] = true;
					}
					break;
				case EV_ALTER: {
					// one undo action per run of altering
					const double now = system::getTime();
					if (now - alterTime > 1.0)
						recKey = (int)(random::u32() & 0x7fffffff) | 1;
					alterTime = now;
					const uint32_t uid = e.uid;
					edit("alter", recKey, [&](Editor &ed) {
						Track &kk = ed.s.tracks[t];
						const int i = kk.indexOf(uid);
						if (i < 0)
							return;
						const int vals[NUM_FIELDS] = {e.a, e.b, e.dur, e.gate};
						for (int f = 0; f < NUM_FIELDS; f++)
							if (e.mask & (1 << f))
								kk.steps[i].v[f] = (uint8_t)vals[f];
					});
					break;
				}
			}
		}
		// the step being step-recorded follows the inputs while INSERT is held
		for (int t = 0; t < kTracks; t++) {
			if (!stepRecUid[t])
				continue;
			if (inputs[REC_INSERT_INPUT].getVoltage() < 1.f) {
				stepRecUid[t] = 0;
				continue;
			}
			const uint32_t uid = stepRecUid[t];
			const int i = doc->tracks[t].indexOf(uid);
			if (i < 0)
				continue;
			Step s = doc->tracks[t].steps[i];
			stepValues(t, s);
			if (!(s == doc->tracks[t].steps[i]))
				edit("step record", (int)uid, [&](Editor &ed) { ed.s.tracks[t].steps[i] = s; });
		}
	}

	// The step recording inputs' values on a step, from the connected inputs
	void stepValues(int t, Step &s) {
		const Track &k = doc->tracks[t];
		if (inputs[REC_A_INPUT].isConnected())
			s.v[CV_A] = (uint8_t)nearestIndex(k.table[0], inputs[REC_A_INPUT].getVoltage());
		if (inputs[REC_B_INPUT].isConnected())
			s.v[CV_B] = (uint8_t)nearestIndex(k.table[1], inputs[REC_B_INPUT].getVoltage());
		if (inputs[REC_GATE_INPUT].isConnected())
			s.v[GATE] = (uint8_t)clamp((int)std::floor(inputs[REC_GATE_INPUT].getVoltage() * 20.f), 0, kMaxValue);
		if (inputs[REC_DUR_INPUT].isConnected())
			s.v[DUR] = (uint8_t)clamp((int)std::floor(inputs[REC_DUR_INPUT].getVoltage() * 20.f), 0, kMaxValue);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		std::lock_guard<std::recursive_mutex> lock(docMutex);
		std::vector<uint8_t> blob;
		serialize(*doc, blob);
		json_object_set_new(rootJ, "song", json_string(rack::string::toBase64(blob.data(), blob.size()).c_str()));
		json_t *snapJ = json_array();
		json_t *namesJ = json_array();
		for (int i = 0; i < 16; i++) {
			json_array_append_new(snapJ, snapshots[i].empty() ? json_null() : json_string(rack::string::toBase64(snapshots[i].data(), snapshots[i].size()).c_str()));
			json_array_append_new(namesJ, json_string(snapshotNames[i].c_str()));
		}
		json_object_set_new(rootJ, "snapshots", snapJ);
		json_object_set_new(rootJ, "snapshotNames", namesJ);
		json_object_set_new(rootJ, "part", json_integer(uiPart.load()));
		json_object_set_new(rootJ, "playingPart", json_integer(engine.shownPart.load()));
		json_object_set_new(rootJ, "commitAt", json_integer(commitAt));
		json_object_set_new(rootJ, "commitTrack", json_integer(commitTrack));
		json_t *armJ = json_array();
		for (int t = 0; t < kTracks; t++)
			json_array_append_new(armJ, json_boolean(engine.armed[t].load()));
		json_object_set_new(rootJ, "armed", armJ);
		json_object_set_new(rootJ, "midiPulsesPerBeat", json_integer(midiPulsesPerBeat));
		json_object_set_new(rootJ, "midiBeatsPerBar", json_integer(midiBeatsPerBar));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		std::lock_guard<std::recursive_mutex> lock(docMutex);
		if (json_t *j = json_object_get(rootJ, "song")) {
			const std::vector<uint8_t> blob = rack::string::fromBase64(json_string_value(j));
			deserialize(blob.data(), blob.size(), *doc);
		}
		json_t *snapJ = json_object_get(rootJ, "snapshots"), *namesJ = json_object_get(rootJ, "snapshotNames");
		for (size_t i = 0; i < 16; i++) {
			json_t *s = snapJ ? json_array_get(snapJ, i) : nullptr;
			snapshots[i] = json_is_string(s) ? rack::string::fromBase64(json_string_value(s)) : std::vector<uint8_t>();
			json_t *n = namesJ ? json_array_get(namesJ, i) : nullptr;
			snapshotNames[i] = json_is_string(n) ? json_string_value(n) : "";
		}
		if (json_t *j = json_object_get(rootJ, "part"))
			uiPart = clamp((int)json_integer_value(j), 0, kParts - 1);
		if (json_t *j = json_object_get(rootJ, "playingPart"))
			engine.setPart = clamp((int)json_integer_value(j), 0, kParts - 1);
		if (json_t *j = json_object_get(rootJ, "commitAt"))
			commitAt = clamp((int)json_integer_value(j), 0, (int)COMMIT_LOOP);
		if (json_t *j = json_object_get(rootJ, "commitTrack"))
			commitTrack = clamp((int)json_integer_value(j), 0, kTracks - 1);
		if (json_t *j = json_object_get(rootJ, "armed"))
			for (int t = 0; t < kTracks; t++)
				engine.armed[t] = json_is_true(json_array_get(j, t));
		if (json_t *j = json_object_get(rootJ, "midiPulsesPerBeat"))
			midiPulsesPerBeat = clamp((int)json_integer_value(j), 1, 96);
		if (json_t *j = json_object_get(rootJ, "midiBeatsPerBar"))
			midiBeatsPerBar = clamp((int)json_integer_value(j), 1, 16);
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

// The screen's layout, px from its top left. It is 760 x 233 (the panel's screen, tools/panels/registre.py)
static constexpr float HEAD = 14.f;
static constexpr float LANE_Y = 16.f, LANE_H = 11.f, LANE_LABEL = 60.f;
static constexpr float GRID_Y = 66.f, ROW_LABEL = 30.f;
static constexpr int kCols = 24;                                // steps shown at once
static constexpr int kRows = 8;                                 // CV A, CV B, DUR, GATE, SMOOTH A, SMOOTH B, RATCHET, GROUP
static constexpr int ROW_GROUP = 7;
static const float kRowH[kRows] = {22.f, 13.f, 16.f, 16.f, 9.f, 9.f, 9.f, 9.f};
static const char *kRowName[kRows] = {"CV A", "CV B", "DUR", "GATE", "SM A", "SM B", "RTCH", "GRP"};
static constexpr float MATH_Y = 184.f;
static constexpr float TABLE_PANE_W = 108.f;
// a block of transform cells: four fields of five settings
static constexpr float CELL_W = 14.f, FIELD_W = 74.f, CELLS_H = 34.f;
// TABLES view
static constexpr float CURVE_X = 34.f, CURVE_Y = 26.f, CURVE_W = 420.f, CURVE_H = 150.f;
static constexpr float REF_X = 470.f, LIST_X = 560.f, SNAP_Y = 206.f;
// GROUPS view
static constexpr float GLIST_W = 100.f, G_X = 110.f;
static constexpr float G_SEL_Y = 20.f, G_MATH_Y = 62.f, G_BUS_Y = 102.f, G_HIGH_Y = 128.f, G_SLOPE_Y = 168.f, GLIST_Y = 26.f;

static float rowTop(int r) {
	float y = GRID_Y;
	for (int i = 0; i < r; i++)
		y += kRowH[i];
	return y;
}
static float gridBottom() { return rowTop(kRows); }

// Transform cells: per field, RND, x/÷, JIT, +, QNT
static const char *kOpName[5] = {"RND", "x", "JIT", "+", "QNT"};
static const char *kFieldName[4] = {"CV A", "CV B", "DUR", "GATE"};

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
// Slopes step finely near 0 and coarsely far from it
static float slopeStep(float v, int d) {
	const float a = std::fabs(v + (d > 0 ? 1e-4f : -1e-4f));
	const float inc = a < 1.f ? 0.01f : a < 10.f ? 0.1f : 1.f;
	return clamp(std::round((v + d * inc) / inc) * inc, -99.f, 99.f);
}

// Which value a cell edits
enum CellKind { C_NONE, C_TRACK_MATH, C_GROUP_MATH, C_HIGH, C_LOW, C_SLOPE, C_EUCLID_N, C_EUCLID_M };
struct Cell {
	int kind = C_NONE, field = 0, op = 0;
	bool operator==(const Cell &o) const { return kind == o.kind && field == o.field && op == o.op; }
};

struct RegistreScreen : OpaqueWidget {
	REGISTRE *module = nullptr;

	// what the screen points at; not saved, except through the module (uiPart)
	int view = 0;              // 0 STEPS, 1 TABLES, 2 GROUPS, 3 REC
	int track = 0;
	int cursor = 0, anchor = 0; // step indices in the track: the selection is between them
	int row = 0;
	int scroll = 0;            // first step shown of the cursor's pattern
	bool follow = false;
	int mathTarget = 0;        // 0 the selection, 1 its pattern, 2 the track, 3 the shown part's loop
	int group = 0, channel = 0;
	int euclidN = 4, euclidM = 16, euclidTarget = 0;   // E(N, M) over the pattern (0) or the track (1)
	int groupClip = -1;        // a copied group selection
	int tableAB = 0, tableIndex = 48, tableScroll = 41;
	int refSel = 0;
	int focus = 0;             // where typing goes: 0 the grid, 1 a cell, 2 a table entry
	Cell cell;                 // the cell being typed into
	std::string typing;
	int mergeSerial = 1000;    // keys for merged undo actions: one per drag or per cell under the wheel
	int wheelKey = 0;
	Vec wheelPos, hoverPos;
	// a drag
	int dragKind = 0;          // 0 none, 1 grid value, 2 grid range, 3 table point, 4 cell, 5 list entry
	Vec dragAcc;
	float dragStartX = 0.f;
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
		k.steps[8].groups = k.steps[12].groups = 1;
		preview->layout();
	}

	float W() const { return box.size.x; }
	float tableX() const { return W() - TABLE_PANE_W; }
	float gridW() const { return W() - TABLE_PANE_W - 6.f; }
	float colW() const { return (gridW() - ROW_LABEL) / kCols; }

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
		if (cursor != syncedCursor || shownPattern != syncedPattern || track != syncedTrack) {
			if (pos < scroll)
				scroll = pos;
			if (pos >= scroll + kCols)
				scroll = pos - kCols + 1;
			syncedCursor = cursor;
			syncedPattern = shownPattern;
			syncedTrack = track;
		}
		scroll = clamp(scroll, 0, std::max(0, pl - kCols));
	}
	int syncedCursor = -1, syncedPattern = -1, syncedTrack = -1;

	// The grid's scroll bar, under the step numbers: the window centred where the mouse is
	void scrollTo(float x) {
		Track &k = tr();
		if (k.numPatterns == 0)
			return;
		const int pl = k.patternLength[shownPattern];
		const float frac = (x - ROW_LABEL) / (gridW() - ROW_LABEL);
		scroll = clamp((int)std::lround(frac * pl - kCols * 0.5f), 0, std::max(0, pl - kCols));
	}
	void scrollBy(int d) {
		Track &k = tr();
		if (k.numPatterns == 0)
			return;
		scroll = clamp(scroll + d, 0, std::max(0, k.patternLength[shownPattern] - kCols));
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
		const int f0 = selFirst(), n = selCount(), r = row, t = track, g = group;
		edit("change values", key, [&](Editor &ed) {
			for (int i = f0; i < f0 + n && i < ed.s.tracks[t].numSteps; i++) {
				Step &st = ed.s.tracks[t].steps[i];
				if (r == ROW_GROUP)
					ed.setMembers(t, i, 1, g, d > 0);
				else
					Editor::setRowValue(st, r, r < NUM_FIELDS ? Editor::rowValue(st, r) + d : (d > 0));
			}
		});
	}
	void setValue(int v) {
		const int f0 = selFirst(), n = selCount(), r = row, t = track, g = group;
		edit("set values", 0, [&](Editor &ed) {
			if (r == ROW_GROUP) {
				ed.setMembers(t, f0, n, g, v != 0);
				return;
			}
			for (int i = f0; i < f0 + n && i < ed.s.tracks[t].numSteps; i++)
				Editor::setRowValue(ed.s.tracks[t].steps[i], r, v);
		});
	}
	void toggleFlag(int r) {
		const int f0 = selFirst(), n = selCount(), t = track, g = group;
		if (!tr().numSteps)
			return;
		if (r == ROW_GROUP) {
			const bool in = !(tr().steps[cursor].groups & (1 << g));
			edit(in ? "add to group" : "remove from group", 0, [&](Editor &ed) { ed.setMembers(t, f0, n, g, in); });
			return;
		}
		const int v = !Editor::rowValue(tr().steps[cursor], r);
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
		else if (mathTarget == 3 && k.numSteps) {
			// the shown part's loop: its start (or the first step) to its end (or the last)
			const int part = module ? module->uiPart.load() : 1;
			const int ls = song().partLoopStart[part][track], le = song().partLoopEnd[part][track];
			first = std::max(0, ls);
			n = (le >= 0 ? le : k.numSteps - 1) - first + 1;
			if (n < 1)
				n = 0;
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
	void applyGroupMath(bool inverted) {
		const int t = track, g = group;
		edit(inverted ? "group math (inverted)" : "group math", 0, [&](Editor &ed) {
			const Transform m = inverted ? ed.s.groups[g].math.inverted() : ed.s.groups[g].math;
			ed.applyMath(t, 0, ed.s.tracks[t].numSteps, m, g);
		});
	}
	void reverseRow() {
		int f0, n;
		target(f0, n);
		const int t = track, r = row;
		if (r == ROW_GROUP)
			return;
		edit("reverse", 0, [&](Editor &ed) { ed.reverse(t, f0, n, r); });
	}
	void rotateRow(int dir) {
		int f0, n;
		target(f0, n);
		const int t = track, r = row, g = group;
		if (r == ROW_GROUP) {
			edit("rotate group", 0, [&](Editor &ed) { ed.rotateMembers(t, g, dir); });
			return;
		}
		edit("rotate", 0, [&](Editor &ed) { ed.rotate(t, f0, n, r, dir); });
	}

	// ---- cells: transforms, slopes, the Euclidean mask
	int cellValue(const Cell &c) {
		Song &s = song();
		switch (c.kind) {
			case C_TRACK_MATH: return opRef(s.tracks[track].math, c.field, c.op);
			case C_GROUP_MATH: return opRef(s.groups[group].math, c.field, c.op);
			case C_HIGH: return opRef(s.groups[group].high[channel], c.field, c.op);
			case C_LOW: return opRef(s.groups[group].low[channel], c.field, c.op);
			case C_EUCLID_N: return euclidN;
			case C_EUCLID_M: return euclidM;
			default: return 0;
		}
	}
	void cellSet(const Cell &c, int d, bool absolute, int key) {
		if (c.kind == C_EUCLID_N) {
			euclidN = clamp(absolute ? d : euclidN + d, 0, euclidM);
			return;
		}
		if (c.kind == C_EUCLID_M) {
			euclidM = clamp(absolute ? d : euclidM + d, 1, 99);
			euclidN = std::min(euclidN, euclidM);
			return;
		}
		const int t = track, g = group, ch = channel;
		if (c.kind == C_SLOPE) {
			edit("slope", key, [&](Editor &ed) {
				float &v = ed.s.groups[g].slope[ch][c.field];
				v = absolute ? clamp(d / 100.f, -99.f, 99.f) : slopeStep(v, d);
			});
			return;
		}
		edit(c.kind == C_TRACK_MATH ? "math setting" : "group setting", key, [&](Editor &ed) {
			Transform &m = c.kind == C_TRACK_MATH ? ed.s.tracks[t].math : c.kind == C_GROUP_MATH ? ed.s.groups[g].math
				: c.kind == C_HIGH ? ed.s.groups[g].high[ch] : ed.s.groups[g].low[ch];
			int8_t &v = opRef(m, c.field, c.op);
			int lo, hi;
			opRange(c.op, lo, hi);
			v = (int8_t)(absolute ? clamp(d, lo, hi) : opStep(c.op, v, d));
		});
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
		if (focus == 1 && cell.kind == C_SLOPE) {
			char *end;
			const double v = std::strtod(s.c_str(), &end);
			if (end != s.c_str())
				cellSet(cell, (int)std::lround(v * 100.0), true, 0);
			return;
		}
		char *end;
		const long v = std::strtol(s.c_str(), &end, 10);
		if (end == s.c_str())
			return;
		if (focus == 1 && cell.kind != C_NONE) {
			cellSet(cell, (int)v, true, 0);
			return;
		}
		setValue((int)v);
	}

	// ---- groups: the selection tools, on the shown track
	void groupOp(int op) {
		const int t = track, g = group;
		switch (op) {
			case 0: { const int f0 = selFirst(), n = selCount(); edit("add to group", 0, [&](Editor &ed) { ed.setMembers(t, f0, n, g, true); }); break; }
			case 1: { const int f0 = selFirst(), n = selCount(); edit("remove from group", 0, [&](Editor &ed) { ed.setMembers(t, f0, n, g, false); }); break; }
			case 2: edit("invert group", 0, [&](Editor &ed) { ed.invertMembers(t, 0, ed.s.tracks[t].numSteps, g); }); break;
			case 3: edit("rotate group", 0, [&](Editor &ed) { ed.rotateMembers(t, g, -1); }); break;
			case 4: edit("rotate group", 0, [&](Editor &ed) { ed.rotateMembers(t, g, 1); }); break;
			case 5: edit("clear group", 0, [&](Editor &ed) { ed.setMembers(t, 0, ed.s.tracks[t].numSteps, g, false); }); break;
			case 6: groupClip = g; break;
			case 7:
				if (groupClip >= 0 && groupClip != g) {
					const int src = groupClip;
					edit("paste group", 0, [&](Editor &ed) { ed.unionMembers(t, src, g); });
				}
				break;
			case 8: {
				Track &k = tr();
				int f0 = 0, n = k.numSteps;
				if (euclidTarget == 0 && k.numPatterns) {
					f0 = k.patternStart[curPattern()];
					n = k.patternLength[curPattern()];
				}
				const int hits = euclidN, len = euclidM;
				edit("euclidean group", 0, [&](Editor &ed) { ed.euclidMembers(t, f0, n, g, hits, len); });
				break;
			}
			case 9: edit("clear group settings", 0, [&](Editor &ed) { ed.s.groups[g] = Group(); }); break;
		}
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
				if (focus == 1 && cell.kind != C_NONE) {
					cellSet(cell, -d * (shift ? 10 : 1), false, 5);
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
		// typing a number (or a voltage or a note in a table, or a slope)
		const bool digit = ch >= '0' && ch <= '9';
		if (focus == 2 && (digit || ch == '.' || ch == '#' || (ch == '-' && typing.empty()) || (!typing.empty() && (ch == '-' || ch == '+' || ch == 'b'))
			|| (std::strchr("ABCDEFGabcdefg", ch) && typing.empty()))) {
			typing += ch;
			e.consume(this);
			return;
		}
		if (digit || (ch == '-' && focus == 1 && typing.empty()) || (ch == '.' && focus == 1 && cell.kind == C_SLOPE)) {
			if (typing.size() < 6)
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
			case 'e': toggleFlag(ROW_GROUP); break;
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
			case 'a':
				if (module)
					module->engine.armed[track] = !module->engine.armed[track].load();
				break;
			default:
				return;
		}
		e.consume(this);
	}

	// ---- mouse
	enum Hit { H_NONE, H_TRACKTAB, H_VIEW, H_FOLLOW, H_PART, H_PATTERN, H_LANE, H_LANEARM, H_LANEDIV, H_LANEMULT, H_GRID, H_CELL,
		H_MATHWORD, H_MATHTARGET, H_TABLEPANE, H_CURVE, H_REF, H_LIST, H_TABLEWORD, H_SNAP, H_TABLEAB,
		H_GLIST, H_GWORD, H_GMATHWORD, H_GCHANNEL, H_EUCLIDTARGET, H_RECMODE, H_RECFOCUS, H_RECWAIT, H_RECTRACK, H_GRIDBAR };
	struct HitInfo {
		int kind = H_NONE;
		int a = 0, b = 0;
		Cell cell;
	};

	// a block of transform cells at (x, y): which field and setting is under p
	static bool cellsHit(Vec p, float x, float y, int &field, int &op) {
		if (p.x < x || p.x >= x + 4 * FIELD_W || p.y < y + 10.f || p.y >= y + CELLS_H)
			return false;
		field = clamp((int)((p.x - x) / FIELD_W), 0, 3);
		op = clamp((int)((p.x - x - field * FIELD_W - 4.f) / CELL_W), 0, 4);
		return true;
	}

	HitInfo hit(Vec p) {
		HitInfo h;
		const float x = p.x, y = p.y;
		if (y < HEAD) {
			if (x < 52.f) { h.kind = H_TRACKTAB; h.a = clamp((int)((x - 2.f) / 12.f), 0, kTracks - 1); }
			else if (x < 200.f) h.kind = H_PATTERN;
			else if (x >= 228.f && x < 330.f) h.kind = H_PART;
			else if (x >= 372.f && x < 412.f) h.kind = H_FOLLOW;
			else if (x >= W() - 212.f) { h.kind = H_VIEW; h.a = clamp((int)((x - (W() - 212.f)) / 52.f), 0, 3); }
			return h;
		}
		if (view == 1)
			return hitTables(p);
		if (view == 2)
			return hitGroups(p);
		if (view == 3)
			return hitRec(p);
		if (y >= LANE_Y && y < LANE_Y + 4 * LANE_H) {
			const int t = clamp((int)((y - LANE_Y) / LANE_H), 0, kTracks - 1);
			h.a = t;
			if (x < 12.f) h.kind = H_LANE, h.b = -1;
			else if (x < 22.f) h.kind = H_LANEARM;
			else if (x < 40.f) h.kind = H_LANEDIV;
			else if (x < LANE_LABEL) h.kind = H_LANEMULT;
			else { h.kind = H_LANE; h.b = laneStepAt(t, x); }
			return h;
		}
		if (x >= tableX() && y >= GRID_Y - 4.f) {
			h.kind = H_TABLEPANE;
			h.a = clamp(tableScroll + (int)((y - GRID_Y - 8.f) / 10.5f), 0, kTableSize - 1);
			return h;
		}
		if (y >= gridBottom() && y < gridBottom() + 14.f && x >= ROW_LABEL && x < gridW()) {
			h.kind = H_GRIDBAR;     // the step numbers and the scroll bar under them
			return h;
		}
		if (y >= GRID_Y && y < gridBottom() && x >= ROW_LABEL && x < gridW()) {
			h.kind = H_GRID;
			int r = 0;
			while (r < kRows - 1 && y >= rowTop(r + 1))
				r++;
			h.a = r;
			h.b = (int)((x - ROW_LABEL) / colW());   // column
			return h;
		}
		if (y >= MATH_Y) {
			if (y < MATH_Y + 12.f && x >= 120.f && x < 280.f) {
				h.kind = H_MATHTARGET;
				h.a = clamp((int)((x - 120.f) / 40.f), 0, 3);
				return h;
			}
			int f, op;
			if (cellsHit(p, 0.f, MATH_Y + 8.f, f, op)) {
				h.kind = H_CELL;
				h.cell = {C_TRACK_MATH, f, op};
				return h;
			}
			if (x >= 310.f && x < gridW() && y >= MATH_Y + 12.f) {
				h.kind = H_MATHWORD;
				h.a = clamp((int)((x - 310.f) / 64.f), 0, 2) + 3 * (y >= MATH_Y + 30.f);
				return h;
			}
		}
		return h;
	}

	HitInfo hitTables(Vec p) {
		HitInfo h;
		const float x = p.x, y = p.y;
		if (y >= SNAP_Y + 6.f) {
			h.kind = H_SNAP;
			h.a = clamp((int)((x - 60.f) / 34.f), 0, 15);
			return x >= 60.f && x < 60.f + 16 * 34.f ? h : HitInfo();
		}
		if (y >= SNAP_Y - 18.f && y < SNAP_Y) {
			h.kind = H_TABLEWORD;
			h.a = clamp((int)(x / 110.f), 0, 5);
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

	HitInfo hitGroups(Vec p) {
		HitInfo h;
		const float x = p.x, y = p.y;
		if (x < GLIST_W && y >= GLIST_Y) {
			h.kind = H_GLIST;
			h.a = clamp((int)((y - GLIST_Y) / 12.f), 0, kGroups - 1);
			return h;
		}
		if (y >= G_SEL_Y && y < G_SEL_Y + 14.f && x >= G_X) {
			h.kind = H_GWORD;
			h.a = clamp((int)((x - G_X) / 64.f), 0, 7);
			return h;
		}
		if (y >= G_SEL_Y + 18.f && y < G_SEL_Y + 32.f && x >= G_X) {
			// E( N , M ) over PAT / TRK, APPLY, RESET SETTINGS
			const float rx = x - G_X;
			if (rx < 40.f) h.kind = H_CELL, h.cell = {C_EUCLID_N, 0, 0};
			else if (rx < 80.f) h.kind = H_CELL, h.cell = {C_EUCLID_M, 0, 0};
			else if (rx < 160.f) h.kind = H_EUCLIDTARGET, h.a = rx >= 120.f;
			else if (rx < 224.f) h.kind = H_GWORD, h.a = 8;
			else if (rx >= 300.f && rx < 420.f) h.kind = H_GWORD, h.a = 9;
			return h;
		}
		int f, op;
		if (cellsHit(p, G_X, G_MATH_Y, f, op)) {
			h.kind = H_CELL;
			h.cell = {C_GROUP_MATH, f, op};
			return h;
		}
		if (y >= G_MATH_Y + 10.f && y < G_MATH_Y + CELLS_H && x >= G_X + 310.f && x < G_X + 450.f) {
			h.kind = H_GMATHWORD;
			h.a = x >= G_X + 380.f;
			return h;
		}
		if (y >= G_BUS_Y && y < G_BUS_Y + 12.f && x >= G_X && x < G_X + 3 * 60.f) {
			h.kind = H_GCHANNEL;
			h.a = clamp((int)((x - G_X) / 60.f), 0, 2);
			return h;
		}
		if (cellsHit(p, G_X, G_HIGH_Y, f, op)) {
			h.kind = H_CELL;
			h.cell = {C_HIGH, f, op};
			return h;
		}
		if (cellsHit(p, G_X + 4 * FIELD_W + 20.f, G_HIGH_Y, f, op)) {
			h.kind = H_CELL;
			h.cell = {C_LOW, f, op};
			return h;
		}
		if (y >= G_SLOPE_Y + 10.f && y < G_SLOPE_Y + 30.f && x >= G_X && x < G_X + 4 * FIELD_W) {
			h.kind = H_CELL;
			h.cell = {C_SLOPE, clamp((int)((x - G_X) / FIELD_W), 0, 3), 0};
			return h;
		}
		return h;
	}

	HitInfo hitRec(Vec p) {
		HitInfo h;
		const float x = p.x, y = p.y;
		if (y >= 18.f && y < 30.f && x >= 60.f && x < 60.f + 3 * 80.f) {
			h.kind = H_RECMODE;
			h.a = clamp((int)((x - 60.f) / 80.f), 0, 2);
		}
		else if (y >= 34.f && y < 46.f && x >= 60.f && x < 60.f + 3 * 80.f) {
			h.kind = H_RECFOCUS;
			h.a = clamp((int)((x - 60.f) / 80.f), 0, 2);
		}
		else if (y >= 34.f && y < 46.f && x >= 320.f && x < 460.f)
			h.kind = H_RECWAIT;
		else if (y >= 66.f && y < 66.f + 4 * 16.f) {
			h.kind = H_RECTRACK;
			h.a = clamp((int)((y - 66.f) / 16.f), 0, kTracks - 1);
			// columns: ARM, TRIG A, TRIG B, DUR Q, GATE Q
			const float cx[5] = {70.f, 130.f, 200.f, 270.f, 340.f};
			h.b = -1;
			for (int i = 0; i < 5; i++)
				if (x >= cx[i] && x < cx[i] + 60.f)
					h.b = i;
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
	static int64_t laneTotal(const Track &k) {
		int64_t s = 0;
		for (int i = 0; i < k.numSteps; i++)
			s += k.steps[i].v[DUR];
		return std::max<int64_t>(1, s);
	}
	int laneStepAt(int t, float x) {
		const Track &k = song().tracks[t];
		const int64_t total = laneTotal(k);
		const float w = W() - LANE_LABEL - 2.f;
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
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem(rack::string::f("Add the selection to group %d", group + 1), "", [=]() { groupOp(0); }));
		menu->addChild(createMenuItem(rack::string::f("Remove the selection from group %d", group + 1), "", [=]() { groupOp(1); }));
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
		menu->addChild(createCheckMenuItem("Armed for recording", "a", [=]() { return m->engine.armed[t].load(); },
			[=]() { m->engine.armed[t] = !m->engine.armed[t].load(); }));
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

	void openSnapshotMenu(int i) {
		REGISTRE *m = module;
		Menu *menu = createMenu();
		menu->addChild(createMenuLabel(rack::string::f("Snapshot %d", i + 1)));
		menu->addChild(createMenuItem("Load", "click", [=]() { m->loadSnapshot(i); }));
		menu->addChild(createMenuItem("Save the song here", "Shift+click", [=]() { m->saveSnapshot(i); }));
		menu->addChild(createMenuItem("Clear", "", [=]() {
			std::lock_guard<std::recursive_mutex> lock(m->docMutex);
			m->snapshots[i].clear();
			m->snapshotNames[i].clear();
		}));
		menu->addChild(createMenuLabel("Name:"));
		struct NameField : ui::TextField {
			REGISTRE *m;
			int i;
			void onChange(const ChangeEvent &e) override {
				m->snapshotNames[i] = text.substr(0, 24);
				ui::TextField::onChange(e);
			}
		};
		NameField *f = new NameField;
		f->m = m;
		f->i = i;
		f->box.size.x = 200.f;
		f->text = m->snapshotNames[i];
		f->placeholder = "a name for this snapshot";
		menu->addChild(f);
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
			else if (h.kind == H_LANE || h.kind == H_LANEARM || h.kind == H_LANEDIV || h.kind == H_LANEMULT || h.kind == H_TRACKTAB) {
				openTrackMenu(h.a);
				e.consume(this);
			}
			else if (h.kind == H_SNAP && module) {
				openSnapshotMenu(h.a);
				e.consume(this);
			}
			return;
		}
		if (e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		e.consume(this);
		dragStartX = e.pos.x;
		dragKind = 0;
		dragDecided = 0;
		dragAcc = Vec();
		dragKey = ++mergeSerial;
		if (h.kind != H_CELL && h.kind != H_LIST && h.kind != H_CURVE && h.kind != H_TABLEPANE)
			commitTyping();
		switch (h.kind) {
			case H_TRACKTAB: track = h.a; moveCursor(cursor, false); break;
			case H_VIEW: view = h.a; focus = 0; break;
			case H_FOLLOW: follow = !follow; break;
			case H_LANEARM:
				if (module)
					module->engine.armed[h.a] = !module->engine.armed[h.a].load();
				break;
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
			case H_GRIDBAR: scrollTo(e.pos.x); dragKind = 6; break;
			case H_MATHTARGET: mathTarget = h.a; break;
			case H_CELL: focus = 1; cell = h.cell; typing.clear(); dragKind = 4; break;
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
			case H_CURVE: focus = 2; tableIndex = h.a; dragKind = 3; break;
			case H_LIST: focus = 2; tableIndex = h.a; dragKind = 5; break;
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
			case H_GLIST: group = h.a; break;
			case H_GWORD: groupOp(h.a); break;
			case H_GMATHWORD: applyGroupMath(h.a == 1); break;
			case H_GCHANNEL: channel = h.a; break;
			case H_EUCLIDTARGET: euclidTarget = h.a; break;
			case H_RECMODE: { const int v = h.a; edit("record mode", 0, [&](Editor &ed) { ed.s.rec.mode = (uint8_t)v; }); break; }
			case H_RECFOCUS: { const int v = h.a; edit("record focus", 0, [&](Editor &ed) { ed.s.rec.focus = (uint8_t)v; }); break; }
			case H_RECWAIT: edit("wait for the first note", 0, [&](Editor &ed) { ed.s.rec.waitFirst = !ed.s.rec.waitFirst; }); break;
			case H_RECTRACK: {
				const int t = h.a;
				if (h.b == 0 && module)
					module->engine.armed[t] = !module->engine.armed[t].load();
				else if (h.b == 1 || h.b == 2) {
					const uint8_t bit = (uint8_t)(h.b == 1 ? 1 : 2);
					edit("record trigger", 0, [&](Editor &ed) { ed.s.rec.trig[t] ^= bit; });
				}
				break;
			}
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
		const Vec d = e.mouseDelta.div(getAbsoluteZoom());
		dragAcc = dragAcc.plus(d);
		const bool fine = (APP->window->getMods() & RACK_MOD_MASK) & GLFW_MOD_SHIFT;
		const bool ctrl = (APP->window->getMods() & RACK_MOD_MASK) & RACK_MOD_CTRL;
		if (dragKind == 1 && !dragDecided) {
			if (std::fabs(dragAcc.x) > 4.f && std::fabs(dragAcc.x) > std::fabs(dragAcc.y))
				dragDecided = 2;
			else if (std::fabs(dragAcc.y) > 3.f)
				dragDecided = 1;
			else
				return;
			if (dragDecided == 2)
				dragKind = 2;
		}
		if (dragKind == 6) {
			scrollTo(dragStartX + dragAcc.x);
			return;
		}
		if (dragKind == 2) {
			// a range: the cursor follows the mouse over the grid's columns; past an edge the grid scrolls
			const int col = (int)std::floor((dragStartX + dragAcc.x - ROW_LABEL) / colW());
			if (col < 0)
				scrollBy(-1);
			else if (col >= kCols)
				scrollBy(1);
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
				cellSet(cell, steps * (ctrl ? 10 : 1), false, dragKey);
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

	void onHover(const HoverEvent &e) override {
		hoverPos = e.pos;
		OpaqueWidget::onHover(e);
	}

	void onHoverScroll(const HoverScrollEvent &e) override {
		const HitInfo h = hit(e.pos);
		// a horizontal wheel (or a trackpad) over the grid, or the wheel over its scroll bar: the grid scrolls
		if ((h.kind == H_GRID || h.kind == H_GRIDBAR) && std::fabs(e.scrollDelta.x) > std::fabs(e.scrollDelta.y)) {
			scrollBy(e.scrollDelta.x > 0 ? -2 : 2);
			e.consume(this);
			return;
		}
		if (h.kind == H_GRIDBAR) {
			scrollBy(e.scrollDelta.y > 0 ? -2 : 2);
			e.consume(this);
			return;
		}
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
			case H_CELL: cell = h.cell; focus = 1; cellSet(cell, d * (ctrl ? 10 : 1), false, wheelKey); break;
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
			case H_GLIST: group = clamp(group - d, 0, kGroups - 1); break;
			case H_RECTRACK:
				if (h.b == 3 || h.b == 4) {
					const int t = h.a, which = h.b;
					edit("record grid", wheelKey, [&](Editor &ed) {
						uint8_t &v = which == 3 ? ed.s.rec.durQ[t] : ed.s.rec.gateQ[t];
						v = (uint8_t)clamp(v + d, 1, 99);
					});
				}
				break;
			default:
				if (view == 0 && e.pos.x >= tableX())
					tableScroll = clamp(tableScroll - d * 3, 0, kTableSize - 14);
				else if (view == 1 && e.pos.x >= LIST_X)
					tableScroll = clamp(tableScroll - d * 3, 0, kTableSize - 16);
				else
					return;
		}
		e.consume(this);
	}

	void step() override {
		if (module) {
			// recording moved the cursor
			if (module->cursorMoved[track]) {
				module->cursorMoved[track] = false;
				const int i = tr().indexOf(module->editUid[track]);
				if (i >= 0)
					moveCursor(i, false);
			}
			if (follow) {
				const uint32_t uid = module->engine.shownUid[track].load();
				const int i = tr().indexOf(uid);
				if (i >= 0) {
					cursor = anchor = i;
					syncPattern();
				}
			}
			if (tr().numSteps)
				module->editUid[track] = tr().steps[clamp(cursor, 0, tr().numSteps - 1)].uid;
		}
		if (focus == 2) {
			if (tableIndex < tableScroll)
				tableScroll = tableIndex;
			const int rows = view == 0 ? 14 : 16;
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
	const int LM = NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, CM = NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, RM = NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE;

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
		else if (view == 1)
			drawTables(vg);
		else if (view == 2)
			drawGroups(vg);
		else
			drawRec(vg);
	}

	bool selected() { return APP->event->getSelectedWidget() == this; }

	void drawHeader(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow, O = theme::kLitOrange;
		for (int t = 0; t < kTracks; t++) {
			const bool gate = module && module->outputs[REGISTRE::GATE_OUTPUTS + t].getVoltage() > 1.f;
			text(vg, 4.f + 12.f * t, 7.f, std::to_string(t + 1), LM, t == track ? Y : gate ? O : kDim, 10.f);
		}
		Track &k = tr();
		text(vg, 56.f, 7.f, rack::string::f("PAT %02d/%02d", shownPattern + 1, std::max<int>(1, k.numPatterns)), LM, Y);
		const int inPat = k.numSteps ? cursor - k.patternStart[curPattern()] + 1 : 0;
		text(vg, 128.f, 7.f, rack::string::f("STEP %02d/%02d", inPat, k.numPatterns ? k.patternLength[shownPattern] : 0), LM, Y);
		const int part = module ? module->uiPart.load() : 1;
		const int playing = module ? module->engine.shownPart.load() : 1;
		const int pending = module ? module->engine.shownPending.load() : -1;
		text(vg, 230.f, 7.f, part == 0 ? "PART STOP" : rack::string::f("PART %02d", part), LM, Y);
		std::string pl = playing == 0 ? "> STOP" : rack::string::f("> %02d", playing);
		if (pending >= 0)
			pl += rack::string::f(" > %02d", pending);
		text(vg, 288.f, 7.f, pl, LM, pending >= 0 ? O : kDim);
		text(vg, 374.f, 7.f, "FOLLOW", LM, follow ? O : kDim);
		const bool blink = std::fmod(system::getTime(), 0.5) < 0.25;
		if (module && module->hold) {
			const bool waiting = module->engine.shownCommitPending.load();
			if (!waiting || blink)
				text(vg, 420.f, 7.f, waiting ? "COMMIT.." : "HOLD", LM, O);
		}
		if (module && module->engine.shownResetPending.load() && blink)
			text(vg, 470.f, 7.f, "RESET..", LM, O);
		if (module && module->engine.shownRecording.load())
			text(vg, 520.f, 7.f, "REC", LM, O);
		static const char *views[4] = {"STEPS", "TABLES", "GROUPS", "REC"};
		for (int v = 0; v < 4; v++)
			text(vg, W() - 210.f + 52.f * v, 7.f, views[v], LM, view == v ? Y : kDim);
		if (selected())
			rect(vg, 0, HEAD - 1.f, W(), 0.6f, kDim);
	}

	void drawLanes(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow;
		const float w = W() - LANE_LABEL - 2.f;
		const int part = module ? module->uiPart.load() : 1;
		for (int t = 0; t < kTracks; t++) {
			const Track &k = song().tracks[t];
			const float y = LANE_Y + t * LANE_H;
			const NVGcolor lc = t == track ? Y : kDim;
			const bool armed = module && module->engine.armed[t].load();
			text(vg, 3.f, y + LANE_H / 2, std::to_string(t + 1), LM, lc, 9.f);
			nvgBeginPath(vg);
			nvgCircle(vg, 16.f, y + LANE_H / 2, 2.2f);
			nvgFillColor(vg, armed ? theme::kLitOrange : kLine);
			nvgFill(vg);
			text(vg, 22.f, y + LANE_H / 2, rack::string::f("/%d", k.div), LM, k.div > 1 ? Y : kDim, 8.f);
			text(vg, 40.f, y + LANE_H / 2, rack::string::f("x%d", k.mult), LM, k.mult > 1 ? Y : kDim, 8.f);
			rect(vg, LANE_LABEL, y + 0.5f, w, LANE_H - 1.f, t == track ? kShade : nvgRGB(0x12, 0x12, 0x10));
			const int64_t total = laneTotal(k);
			const float sc = w / total;
			int64_t pos = 0;
			const int playUid = module ? (int)module->engine.shownUid[t].load() : -1;
			float playX = -1.f;
			float lo = 1e9f, hi = -1e9f;
			for (int i = 0; i < k.numSteps; i++) {
				const float v = k.table[0][k.steps[i].v[CV_A]];
				lo = std::min(lo, v);
				hi = std::max(hi, v);
			}
			const float range = std::max(0.5f, hi - lo);
			// gates, pattern ticks, the play cursor
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
			bool first = true;
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
		for (int r = 0; r < kRows; r++) {
			const std::string name = r == ROW_GROUP ? rack::string::f("G%02d", group + 1) : kRowName[r];
			text(vg, 2.f, rowTop(r) + kRowH[r] / 2, name, LM, r == row ? Y : kDim, 8.f);
		}
		if (k.numPatterns == 0 || k.patternLength[shownPattern] == 0) {
			text(vg, gridW() / 2, GRID_Y + 40.f, "EMPTY PATTERN: i OR INSERT ADDS A STEP", CM, kDim, 9.f);
			return;
		}
		const uint32_t playUid = module ? module->engine.shownUid[track].load() : 0;
		const int part = module ? module->uiPart.load() : 1;
		const bool notesA = k.flags & NOTES_A;
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
					text(vg, x + cw / 2, y + (r == CV_A ? 7.f : h / 2), v, CM, tc, 10.f);
					if (r == CV_A) {
						char buf[16];
						const float volts = k.table[0][st.v[CV_A]];
						if (notesA)
							noteName(volts, buf, sizeof(buf), false);
						else
							std::snprintf(buf, sizeof(buf), "%.2f", volts);
						text(vg, x + cw / 2, y + 17.f, buf, CM, cur ? theme::kScreenBlack : theme::kWavePink, 7.5f);
					}
				}
				else if (r == DUR || r == GATE) {
					const int v = st.v[r];
					const float bh = (h - 11.f) * std::min(1.f, v / 16.f);
					if (!cur)
						rect(vg, x + 3.f, y + h - 2.f - bh, cw - 6.f, bh, r == DUR ? theme::kKeyBlack : theme::kKeyWhite);
					std::string vs = (cur && focus == 0 && !typing.empty()) ? typing + "_" : std::to_string(v);
					text(vg, x + cw / 2, y + 5.5f, vs, CM, tc, 9.f);
				}
				else if (r == ROW_GROUP) {
					// the shown group: filled when in it; the step's other groups as a dim mark
					const bool in = st.groups & (1 << group);
					if (in)
						rect(vg, x + 4.f, y + 2.f, cw - 8.f, h - 4.f, cur ? theme::kScreenBlack : theme::kWavePink);
					else
						rect(vg, x + cw / 2 - 1.f, y + h / 2 - 1.f, 2.f, 2.f, cur ? theme::kScreenBlack : st.groups ? kDim : kLine);
				}
				else {
					const bool on = Editor::rowValue(st, r);
					// a flag set for the whole pattern or track shows dim
					const uint8_t bit = (uint8_t)(1 << (r - NUM_FIELDS));
					const bool wide = r < 6 && ((k.patternFlags[k.stepPattern[s]] & bit) || (k.flags & bit));
					if (on)
						text(vg, x + cw / 2, y + h / 2, r == 6 ? "R" : r == 4 ? "A" : "B", CM, cur ? theme::kScreenBlack : O, 8.f);
					else
						rect(vg, x + cw / 2 - 1.f, y + h / 2 - 1.f, 2.f, 2.f, cur ? theme::kScreenBlack : wide ? O : kLine);
				}
			}
			// column number, loop and reset marks, play cursor
			const int inPat = s - k.patternStart[shownPattern] + 1;
			text(vg, x + cw / 2, gridBottom() + 5.f, std::to_string(inPat), CM, kDim, 7.f);
			const Part &pt = song().parts[part];
			if (pt.loopStart[track] == st.uid)
				text(vg, x + 1.f, GRID_Y - 3.f, "[", LM, Y, 9.f);
			if (pt.loopEnd[track] == st.uid)
				text(vg, x + cw - 1.f, GRID_Y - 3.f, "]", RM, Y, 9.f);
			if (pt.reset[track] == st.uid)
				text(vg, x + cw / 2, GRID_Y - 3.f, "R", CM, O, 8.f);
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
			// where the window is in a long pattern: click or drag the bar, the wheel over it, or a horizontal wheel
			const float sw = gridW() - ROW_LABEL;
			const float pl = k.patternLength[shownPattern];
			rect(vg, ROW_LABEL, gridBottom() + 9.f, sw, 3.f, kLine);
			rect(vg, ROW_LABEL + sw * scroll / pl, gridBottom() + 9.f, sw * kCols / pl, 3.f, theme::kKeyWhite);
			if (scroll > 0)
				text(vg, ROW_LABEL - 3.f, GRID_Y + 40.f, "<", RM, theme::kScreenYellow, 10.f);
			if (scroll + kCols < pl)
				text(vg, gridW() + 1.f, GRID_Y + 40.f, ">", LM, theme::kScreenYellow, 10.f);
		}
	}

	// Four fields of five settings at (x, y), with the cell kind they edit
	void drawCells(NVGcontext *vg, float x, float y, const Transform &m, int kind, bool labels = true) {
		const NVGcolor Y = theme::kScreenYellow;
		const Transform id;
		for (int f = 0; f < NUM_FIELDS; f++) {
			const float fx = x + f * FIELD_W;
			if (labels)
				text(vg, fx + 4.f, y + 4.f, kFieldName[f], LM, kDim, 8.f);
			for (int op = 0; op < 5; op++) {
				const float cx = fx + 4.f + op * CELL_W + 7.f;
				const int v = opRef(const_cast<Transform &>(m), f, op);
				const bool changed = v != opRef(const_cast<Transform &>(id), f, op);
				const bool cur = focus == 1 && cell == Cell{kind, f, op};
				if (cur)
					rect(vg, cx - 7.f, y + 10.f, CELL_W, CELLS_H - 10.f, kShade);
				text(vg, cx, y + 15.f, kOpName[op], CM, kDim, 6.5f);
				std::string vs = (cur && !typing.empty()) ? typing + "_" : opText(op, v);
				text(vg, cx, y + 26.f, vs, CM, changed || cur ? Y : kDim, 8.f);
			}
		}
	}

	void drawMath(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow;
		rect(vg, 0, MATH_Y - 2.f, gridW(), 0.6f, kLine);
		text(vg, 2.f, MATH_Y + 4.f, "MATH", LM, Y, 9.f);
		int f0, n;
		target(f0, n);
		text(vg, 40.f, MATH_Y + 4.f, rack::string::f("ON %d", n), LM, kDim, 8.f);
		static const char *targets[4] = {"SEL", "PAT", "TRK", "PRT"};
		for (int i = 0; i < 4; i++)
			text(vg, 120.f + 40.f * i, MATH_Y + 4.f, targets[i], LM, mathTarget == i ? Y : kDim, 9.f);
		drawCells(vg, 0.f, MATH_Y + 8.f, tr().math, C_TRACK_MATH);
		static const char *words[6] = {"APPLY", "INVERT", "CLEAR", "REVERSE", "< ROT", "ROT >"};
		for (int i = 0; i < 6; i++)
			text(vg, 310.f + 64.f * (i % 3), MATH_Y + 21.f + 18.f * (i / 3), words[i], LM, Y, 9.f);
	}

	void drawTablePane(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow;
		Track &k = tr();
		const int ab = row == 1 ? 1 : 0;
		const float x0 = tableX(), w = W() - x0 - 2.f;
		rect(vg, x0 - 4.f, GRID_Y - 4.f, 0.6f, box.size.y - GRID_Y + 2.f, kLine);
		text(vg, x0, GRID_Y + 2.f, ab ? "TABLE B" : "TABLE A", LM, Y, 9.f);
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
			text(vg, x0, y + 5.f, rack::string::f("%02d", i), LM, cur ? c : kDim, 8.5f);
			std::string vs = (edit && !typing.empty()) ? typing + "_" : rack::string::f("%+.3f", k.table[ab][i]);
			text(vg, x0 + 16.f, y + 5.f, vs, LM, c, 8.5f);
			if (notes) {
				char buf[16];
				noteName(k.table[ab][i], buf, sizeof(buf), false);
				text(vg, x0 + w - 1.f, y + 5.f, buf, RM, cur ? c : theme::kWavePink, 8.f);
			}
		}
	}

	void drawTables(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow;
		Track &k = tr();
		const float *tab = k.table[tableAB];
		text(vg, 4.f, 21.f, rack::string::f("TRACK %d TABLE", track + 1), LM, Y, 9.f);
		text(vg, 112.f, 21.f, "A", LM, tableAB == 0 ? Y : kDim, 10.f);
		text(vg, 132.f, 21.f, "B", LM, tableAB == 1 ? Y : kDim, 10.f);
		// the curve: -5 to 10 V
		auto yOf = [&](float v) { return CURVE_Y + CURVE_H - (v - kMinVolts) / (kMaxVolts - kMinVolts) * CURVE_H; };
		auto xOf = [&](int i) { return CURVE_X + 4.f + i * (CURVE_W - 8.f) / (kTableSize - 1); };
		for (int v = -4; v <= 10; v += 2) {
			rect(vg, CURVE_X, yOf((float)v), CURVE_W, 0.5f, v == 0 ? kDim : kLine);
			if (v < 10)   // 10 V is the frame's top, under the title
				text(vg, CURVE_X - 3.f, yOf((float)v), rack::string::f("%d", v), RM, kDim, 7.f);
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
		text(vg, CURVE_X + 4.f, CURVE_Y + CURVE_H + 8.f, rack::string::f("%02d  %+.3f V  %s", tableIndex, tab[tableIndex], note), LM, Y, 9.f);
		text(vg, CURVE_X + CURVE_W, CURVE_Y + CURVE_H + 8.f, "DRAG  SHIFT FINE  " RACK_MOD_CTRL_NAME " SEMITONES", RM, kDim, 7.f);
		// reference and user tables
		text(vg, REF_X, 18.f, "REFERENCE", LM, kDim, 8.f);
		for (int r = 0; r < NUM_REFS + kUserTables; r++) {
			const float y = 24.f + r * 9.f;
			if (r == refSel)
				rect(vg, REF_X - 2.f, y, LIST_X - REF_X - 6.f, 9.f, Y);
			const std::string name = r < NUM_REFS ? refName(r) : rack::string::f("USER %d", r - NUM_REFS + 1);
			text(vg, REF_X, y + 4.5f, name, LM, r == refSel ? theme::kScreenBlack : r < NUM_REFS ? Y : kDim, 8.f);
		}
		// the list
		const bool notes = k.flags & (tableAB ? NOTES_B : NOTES_A);
		for (int j = 0; j < 16; j++) {
			const int i = tableScroll + j;
			if (i >= kTableSize)
				break;
			const float y = 24.f + j * 10.f;
			const bool cur = i == tableIndex;
			if (cur)
				rect(vg, LIST_X - 2.f, y, W() - LIST_X, 10.f, focus == 2 ? Y : kShade);
			const NVGcolor c = cur && focus == 2 ? theme::kScreenBlack : Y;
			text(vg, LIST_X, y + 5.f, rack::string::f("%02d", i), LM, cur ? c : used[i] ? Y : kDim, 8.5f);
			std::string vs = (cur && focus == 2 && !typing.empty()) ? typing + "_" : rack::string::f("%+.3f V", tab[i]);
			text(vg, LIST_X + 18.f, y + 5.f, vs, LM, c, 8.5f);
			if (notes) {
				noteName(tab[i], note, sizeof(note));
				text(vg, W() - 4.f, y + 5.f, note, RM, cur && focus == 2 ? c : theme::kWavePink, 8.f);
			}
		}
		// the table words
		const std::string target = refSel < NUM_REFS ? "" : rack::string::f(" %d", refSel - NUM_REFS + 1);
		const std::string words[6] = {"LOAD > A", "LOAD > B", "SAVE A > USER" + target, "SAVE B > USER" + target, "ROUND", "GO TO STEP"};
		for (int i = 0; i < 6; i++)
			text(vg, i * 110.f + 4.f, SNAP_Y - 9.f, words[i], LM, (i == 2 || i == 3) && refSel < NUM_REFS ? kDim : Y, 8.5f);
		// snapshots, with the name of the one under the mouse
		rect(vg, 0, SNAP_Y + 1.f, W(), 0.6f, kLine);
		text(vg, 4.f, SNAP_Y + 17.f, "SNAPSHOTS", LM, kDim, 8.f);
		for (int i = 0; i < 16; i++) {
			const float x = 60.f + i * 34.f;
			const bool full = module && !module->snapshots[i].empty();
			frame(vg, x, SNAP_Y + 8.f, 31.f, 18.f, full ? Y : kLine);
			text(vg, x + 15.5f, SNAP_Y + 17.f, std::to_string(i + 1), CM, full ? Y : kDim, 9.f);
		}
		if (module && hoverPos.y >= SNAP_Y + 6.f && hoverPos.x >= 60.f && hoverPos.x < 60.f + 16 * 34.f) {
			const int i = clamp((int)((hoverPos.x - 60.f) / 34.f), 0, 15);
			const std::string name = module->snapshotNames[i].empty() ? (module->snapshots[i].empty() ? "EMPTY" : "NO NAME") : module->snapshotNames[i];
			text(vg, 60.f + 16 * 34.f + 8.f, SNAP_Y + 17.f, name, LM, Y, 8.5f);
		}
	}

	void drawGroups(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow, O = theme::kLitOrange;
		Song &s = song();
		Track &k = tr();
		// the list: members on the shown track, and a mark when the group has settings
		text(vg, 4.f, 20.f, rack::string::f("TRACK %d", track + 1), LM, kDim, 8.f);
		for (int g = 0; g < kGroups; g++) {
			const float y = GLIST_Y + g * 12.f;
			int n = 0;
			for (int i = 0; i < k.numSteps; i++)
				n += (k.steps[i].groups >> g) & 1;
			if (g == group)
				rect(vg, 2.f, y, GLIST_W - 6.f, 12.f, Y);
			const NVGcolor c = g == group ? theme::kScreenBlack : Y;
			text(vg, 4.f, y + 6.f, rack::string::f("G%02d", g + 1), LM, c, 9.f);
			text(vg, 40.f, y + 6.f, rack::string::f("%d", n), LM, g == group ? c : n ? Y : kDim, 8.5f);
			if (!s.groups[g].isDefault())
				text(vg, GLIST_W - 10.f, y + 6.f, "*", RM, g == group ? c : O, 9.f);
		}
		rect(vg, GLIST_W, 18.f, 0.6f, box.size.y - 20.f, kLine);
		// the selection tools
		static const char *words[8] = {"ADD SEL", "REMOVE SEL", "INVERT", "< ROT", "ROT >", "CLEAR", "COPY", "PASTE +"};
		for (int i = 0; i < 8; i++)
			text(vg, G_X + 64.f * i, G_SEL_Y + 7.f, words[i], LM, i == 7 && groupClip < 0 ? kDim : Y, 8.5f);
		// E(N, M)
		const bool curN = focus == 1 && cell.kind == C_EUCLID_N, curM = focus == 1 && cell.kind == C_EUCLID_M;
		if (curN) rect(vg, G_X, G_SEL_Y + 18.f, 38.f, 13.f, kShade);
		if (curM) rect(vg, G_X + 40.f, G_SEL_Y + 18.f, 38.f, 13.f, kShade);
		text(vg, G_X + 2.f, G_SEL_Y + 25.f, rack::string::f("E %s", curN && !typing.empty() ? (typing + "_").c_str() : std::to_string(euclidN).c_str()), LM, Y, 9.f);
		text(vg, G_X + 42.f, G_SEL_Y + 25.f, rack::string::f("/ %s", curM && !typing.empty() ? (typing + "_").c_str() : std::to_string(euclidM).c_str()), LM, Y, 9.f);
		text(vg, G_X + 84.f, G_SEL_Y + 25.f, "PAT", LM, euclidTarget == 0 ? Y : kDim, 9.f);
		text(vg, G_X + 122.f, G_SEL_Y + 25.f, "TRK", LM, euclidTarget == 1 ? Y : kDim, 9.f);
		text(vg, G_X + 162.f, G_SEL_Y + 25.f, "EUCLID", LM, Y, 9.f);
		// the mask, drawn
		for (int j = 0; j < std::min(euclidM, 32); j++)
			rect(vg, G_X + 224.f + j * 2.2f, G_SEL_Y + 21.f, 1.6f, 8.f, euclidHit(j, euclidN, euclidM) ? theme::kWavePink : kLine);
		text(vg, G_X + 300.f, G_SEL_Y + 25.f, "CLEAR SETTINGS", LM, Y, 8.5f);
		// the group's MATH
		text(vg, G_X + 310.f - 6.f, G_MATH_Y + 4.f, rack::string::f("G%02d MATH, ON ITS STEPS OF TRACK %d", group + 1, track + 1), LM, kDim, 8.f);
		drawCells(vg, G_X, G_MATH_Y, s.groups[group].math, C_GROUP_MATH);
		text(vg, G_X + 310.f, G_MATH_Y + 21.f, "APPLY", LM, Y, 9.f);
		text(vg, G_X + 380.f, G_MATH_Y + 21.f, "INVERT", LM, Y, 9.f);
		// the bus: channel tabs with their live gate and CV
		static const char *xyz[3] = {"X", "Y", "Z"};
		for (int c = 0; c < kChannels; c++) {
			const bool high = module && module->engine.shownMod[c].load();
			const float cv = module ? module->engine.shownModCv[c].load() : 0.f;
			text(vg, G_X + 60.f * c, G_BUS_Y + 6.f, rack::string::f("%s %s %+.1f", xyz[c], high ? "H" : "L", cv), LM, c == channel ? Y : kDim, 9.f);
		}
		rect(vg, G_X, G_BUS_Y + 12.f, W() - G_X - 4.f, 0.6f, kLine);
		const Group &gr = s.groups[group];
		drawCells(vg, G_X, G_HIGH_Y, gr.high[channel], C_HIGH);
		drawCells(vg, G_X + 4 * FIELD_W + 20.f, G_HIGH_Y, gr.low[channel], C_LOW);
		text(vg, G_X + 4.f, G_HIGH_Y - 6.f, rack::string::f("%s HIGH: WHILE THE %s GATE IS HIGH", xyz[channel], xyz[channel]), LM, O, 8.f);
		text(vg, G_X + 4 * FIELD_W + 24.f, G_HIGH_Y - 6.f, rack::string::f("%s LOW: WHILE IT IS LOW", xyz[channel]), LM, O, 8.f);
		// slopes
		text(vg, G_X, G_SLOPE_Y + 3.f, rack::string::f("%s SLOPE: ADDED PER VOLT OF %s CV (CV A, CV B IN VOLTS, DUR, GATE IN UNITS)", xyz[channel], xyz[channel]), LM, kDim, 7.5f);
		for (int f = 0; f < NUM_FIELDS; f++) {
			const float fx = G_X + f * FIELD_W;
			const bool cur = focus == 1 && cell == Cell{C_SLOPE, f, 0};
			if (cur)
				rect(vg, fx, G_SLOPE_Y + 10.f, FIELD_W - 4.f, 20.f, kShade);
			text(vg, fx + 4.f, G_SLOPE_Y + 15.f, kFieldName[f], LM, kDim, 7.5f);
			const float v = gr.slope[channel][f];
			std::string vs = (cur && !typing.empty()) ? typing + "_" : rack::string::f("%+.2f", v);
			text(vg, fx + 4.f, G_SLOPE_Y + 25.f, vs, LM, v != 0.f || cur ? Y : kDim, 9.f);
		}
		text(vg, G_X, box.size.y - 6.f, "GATES SWITCH HIGH/LOW AS EACH STEP STARTS; THEY CHANGE HOW IT PLAYS, NOT THE STEP. e: STEP IN/OUT OF THE GROUP", LM, kDim, 7.f);
	}

	void drawRec(NVGcontext *vg) {
		const NVGcolor Y = theme::kScreenYellow, O = theme::kLitOrange;
		const RecSettings &r = song().rec;
		text(vg, 4.f, 24.f, "MODE", LM, kDim, 8.f);
		static const char *modes[3] = {"REAL-TIME", "STEP", "ALTER"};
		for (int i = 0; i < 3; i++)
			text(vg, 60.f + 80.f * i, 24.f, modes[i], LM, r.mode == i ? Y : kDim, 9.f);
		text(vg, 4.f, 40.f, "INSERT AT", LM, kDim, 8.f);
		static const char *focuses[3] = {"TRACK END", "NEXT PATTERN", "CURSOR"};
		for (int i = 0; i < 3; i++)
			text(vg, 60.f + 80.f * i, 40.f, focuses[i], LM, r.focus == i ? Y : kDim, 9.f);
		text(vg, 320.f, 40.f, "WAIT FOR THE FIRST NOTE", LM, r.waitFirst ? Y : kDim, 9.f);
		static const char *heads[5] = {"ARM", "NEW ON A", "NEW ON B", "DUR GRID", "GATE GRID"};
		const float cx[5] = {70.f, 130.f, 200.f, 270.f, 340.f};
		for (int i = 0; i < 5; i++)
			text(vg, cx[i], 58.f, heads[i], LM, kDim, 7.5f);
		for (int t = 0; t < kTracks; t++) {
			const float y = 66.f + t * 16.f + 8.f;
			const bool armed = module && module->engine.armed[t].load();
			text(vg, 4.f, y, rack::string::f("TRACK %d", t + 1), LM, t == track ? Y : kDim, 9.f);
			nvgBeginPath(vg);
			nvgCircle(vg, cx[0] + 6.f, y, 3.5f);
			nvgFillColor(vg, armed ? O : kLine);
			nvgFill(vg);
			text(vg, cx[1], y, (r.trig[t] & 1) ? "YES" : "NO", LM, (r.trig[t] & 1) ? Y : kDim, 9.f);
			text(vg, cx[2], y, (r.trig[t] & 2) ? "YES" : "NO", LM, (r.trig[t] & 2) ? Y : kDim, 9.f);
			text(vg, cx[3], y, std::to_string(r.durQ[t]), LM, Y, 9.f);
			text(vg, cx[4], y, std::to_string(r.gateQ[t]), LM, Y, 9.f);
		}
		// the inputs, live
		const float iy = 140.f;
		rect(vg, 0, iy - 8.f, W(), 0.6f, kLine);
		const bool punched = module && module->engine.shownRecording.load();
		text(vg, 4.f, iy, punched ? "RECORDING" : "PUNCHED OUT", LM, punched ? O : kDim, 9.f);
		if (module) {
			static const char *names[4] = {"REC A", "REC B", "REC G", "REC D"};
			const int ins[4] = {REGISTRE::REC_A_INPUT, REGISTRE::REC_B_INPUT, REGISTRE::REC_GATE_INPUT, REGISTRE::REC_DUR_INPUT};
			for (int i = 0; i < 4; i++) {
				const bool on = module->inputs[ins[i]].isConnected();
				const float v = module->engine.shownRec[i].load();
				std::string s = on ? rack::string::f("%s %+.2f V", names[i], v) : rack::string::f("%s -", names[i]);
				if (on && i < 2) {
					const Track &k = tr();
					const int idx = nearestIndex(k.table[i], v);
					s += rack::string::f("  #%02d", idx);
				}
				if (on && i >= 2)
					s += rack::string::f("  = %d", clamp((int)std::floor(v * 20.f), 0, 99));
				text(vg, 110.f + 150.f * i, iy, s, LM, on ? Y : kDim, 8.5f);
			}
		}
		static const char *help[3] = {
			"REAL-TIME: ARMED TRACKS RECORD NOTES FROM REC G (PITCH REC A, VELOCITY REC B) WHILE PUNCHED IN, TIMED ON THEIR CLOCK; THEY PLAY THE INPUTS MEANWHILE",
			"STEP: INS INSERTS A STEP AFTER THE CURSOR FROM REC A, B, G (GATE) AND D (DURATION), FOLLOWING THEM WHILE HELD; DEL DELETES THE STEP AT THE CURSOR",
			"ALTER: EACH STEP OF THE ARMED TRACKS TAKES THE CONNECTED INPUTS' VALUES JUST BEFORE IT PLAYS, WHILE PUNCHED IN"};
		text(vg, 4.f, iy + 22.f, help[r.mode], LM, kDim, 7.f);
		text(vg, 4.f, iy + 34.f, "PUNCH: THE REC BUTTON OR A HIGH PUNCH GATE. a ARMS THE SHOWN TRACK. A TAKE IS ONE UNDO.", LM, kDim, 7.f);
		text(vg, 4.f, iy + 46.f, "NEW ON A / B: A CHANGE OF THAT CV WHILE THE GATE IS HELD STARTS A NEW STEP. GRIDS: DURATIONS AND GATES ROUNDED TO MULTIPLES. WHEEL TO CHANGE.", LM, kDim, 7.f);
	}
};

// HOLD's and COMMIT's buttons, GO's, REC's: lit while a commit or a part waits, or while recording
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
		const int n = 20;              // jack and button columns, as tools/panels/registre.py N
		auto at = [&](float i, float k) { return theme::jackAt(i, n, k, w); };

		screen = new RegistreScreen;
		screen->module = module;
		screen->box = theme::screen(1, w);
		addChild(screen);

		const int ins[6] = {REGISTRE::CLOCK_INPUT, REGISTRE::RESET_INPUT, REGISTRE::RUN_INPUT, REGISTRE::TRANSPOSE_INPUT,
			REGISTRE::PART_INPUT, REGISTRE::GO_INPUT};
		for (int i = 0; i < 6; i++)
			addInput(theme::input<PJ301MPort>(at(i, 1), module, ins[i]));
		// buttons: RUN, GO, HOLD, COMMIT on row 1, REC on row 0 (registre.py BUTTONS)
		const int buttons[5] = {REGISTRE::RUN_PARAM, REGISTRE::GO_PARAM, REGISTRE::HOLD_PARAM, REGISTRE::COMMIT_PARAM, REGISTRE::PUNCH_PARAM};
		const float bcol[5] = {6, 7, 8, 9, 19}, brow[5] = {1, 1, 1, 1, 0};
		for (int i = 0; i < 5; i++) {
			RegistreButton *b = theme::param<RegistreButton>(at(bcol[i], brow[i]), module, buttons[i]);   // the jacks' columns: REC under POLY G
			if (module && buttons[i] == REGISTRE::GO_PARAM)
				b->lit = [=]() { return module->engine.shownPending.load() >= 0; };
			if (module && buttons[i] == REGISTRE::COMMIT_PARAM)
				b->lit = [=]() { return module->engine.shownCommitPending.load(); };
			if (module && buttons[i] == REGISTRE::PUNCH_PARAM)
				b->lit = [=]() { return module->engine.shownRecording.load(); };
			addParam(b);
		}
		for (int c = 0; c < kChannels; c++) {
			addInput(theme::input<PJ301MPort>(at(10 + c, 1), module, REGISTRE::MOD_CV_INPUTS + c));
			addInput(theme::input<PJ301MPort>(at(13 + c, 1), module, REGISTRE::MOD_GATE_INPUTS + c));
		}
		addOutput(theme::output<PJ301MPort>(at(17, 1), module, REGISTRE::POLY_A_OUTPUT));
		addOutput(theme::output<PJ301MPort>(at(18, 1), module, REGISTRE::POLY_B_OUTPUT));
		addOutput(theme::output<PJ301MPort>(at(19, 1), module, REGISTRE::POLY_GATE_OUTPUT));
		for (int t = 0; t < kTracks; t++) {
			addOutput(theme::output<PJ301MPort>(at(3 * t, 0), module, REGISTRE::A_OUTPUTS + t));
			addOutput(theme::output<PJ301MPort>(at(3 * t + 1, 0), module, REGISTRE::B_OUTPUTS + t));
			addOutput(theme::output<PJ301MPort>(at(3 * t + 2, 0), module, REGISTRE::GATE_OUTPUTS + t));
		}
		const int recs[7] = {REGISTRE::REC_A_INPUT, REGISTRE::REC_B_INPUT, REGISTRE::REC_GATE_INPUT, REGISTRE::REC_DUR_INPUT,
			REGISTRE::REC_INSERT_INPUT, REGISTRE::REC_DELETE_INPUT, REGISTRE::PUNCH_INPUT};
		for (int i = 0; i < 7; i++)
			addInput(theme::input<PJ301MPort>(at(12 + i, 0), module, recs[i]));
	}

	// The buttons that act on the document, and the recorded events, are read here, on the UI thread
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
			std::lock_guard<std::recursive_mutex> lock(m->docMutex);
			m->drainRecording();
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
		menu->addChild(createIndexSubmenuItem("Reset at the end of", {"Nothing: at once", "A step", "A pattern", "A loop"},
			[=]() { return (size_t)m->doc->resetAt; },
			[=](size_t i) { m->edit("reset timing", 0, [&](Editor &ed) { ed.s.resetAt = (uint8_t)i; }); }));
		menu->addChild(createIndexSubmenuItem("Reset timed by track", {"1", "2", "3", "4"},
			[=]() { return (size_t)m->doc->resetTrack; },
			[=](size_t i) { m->edit("reset timing", 0, [&](Editor &ed) { ed.s.resetTrack = (uint8_t)i; }); }));
		menu->addChild(createIndexSubmenuItem("Commit (HOLD) at the end of", {"Nothing: at once", "A step", "A pattern", "A loop"},
			[=]() { return (size_t)m->commitAt; }, [=](size_t i) { m->commitAt = (int)i; }));
		menu->addChild(createIndexSubmenuItem("Commit timed by track", {"1", "2", "3", "4"},
			[=]() { return (size_t)m->commitTrack; }, [=](size_t i) { m->commitTrack = (int)i; }));
		menu->addChild(createMenuItem("Clear every part's loops and reset steps", "", [=]() {
			m->edit("clear parts", 0, [&](Editor &ed) { for (auto &p : ed.s.parts) p = Part(); });
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Import a MIDI file (channels 1-4 to tracks 1-4)...", "", [=]() {
			osdialog_filters *filters = osdialog_filters_parse("MIDI file:mid,midi,smf");
			char *path = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, filters);
			osdialog_filters_free(filters);
			if (!path)
				return;
			const std::string p = path;
			std::free(path);
			if (!m->importMidiFile(p))
				osdialog_message(OSDIALOG_WARNING, OSDIALOG_OK, "rEgIStRE: no notes on channels 1 to 4 in this file, or it is not a MIDI file.");
		}));
		static const int ppbs[6] = {1, 2, 4, 8, 12, 24};
		menu->addChild(createIndexSubmenuItem("MIDI import: clock pulses per beat", {"1", "2", "4 (16th notes)", "8", "12", "24"},
			[=]() { for (int i = 0; i < 6; i++) if (ppbs[i] == m->midiPulsesPerBeat) return (size_t)i; return (size_t)2; },
			[=](size_t i) { m->midiPulsesPerBeat = ppbs[i]; }));
		menu->addChild(createIndexSubmenuItem("MIDI import: beats per bar (a pattern a bar)", {"1", "2", "3", "4", "5", "6", "7", "8"},
			[=]() { return (size_t)clamp(m->midiBeatsPerBar - 1, 0, 7); }, [=](size_t i) { m->midiBeatsPerBar = (int)i + 1; }));
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
				"s: smooth, t: ratchet, e: in the shown group, Enter: toggle a flag row",
				"[ ]: loop start / end, l: loop selection (or pattern), Shift+l: clear, p: reset step",
				"m: MATH, Shift+m: inverted, r: reverse the row, < >: rotate it",
				"Space: run, f: follow, h: hold, Enter: commit, g: go (cue the part), a: arm the track",
				RACK_MOD_CTRL_NAME "+Z: undo",
			};
			for (const char *l : lines)
				sub->addChild(createMenuLabel(l));
		}));
	}
};

Model *modelREGISTRE = createModel<REGISTRE, REGISTREWidget>("REGISTRE");
