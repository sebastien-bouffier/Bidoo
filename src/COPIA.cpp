// cOPIA: live resampling. CAPTURE records 1 to 8 bars of the incoming mix from the next bar of the
// clock, then plays them back on the clock through Mangle's glitch engine: eight effect slots, their
// lanes drawn on the display, eight scenes with a morph. IN/OUT brings the remix in over the live mix
// or in its place. The loop is kept in the patch storage as FLAC.
#include "plugin.hpp"
#include "CopiaEngine.hpp"
#include "SounaireFlac.hpp"
#include "dep/dr_flac/dr_flac.h"
#include <atomic>
#include <cstring>

struct COPIA : BidooModule {
	enum ParamIds {
		CAPTURE_PARAM,
		LENGTH_PARAM,
		GRID_PARAM,
		RAND_PARAM,
		MUTATE_PARAM,
		MORPH_PARAM,
		MIX_PARAM,
		INOUT_PARAM,
		ENUMS(FX_PARAM, copia::kSlots),
		ENUMS(PUNCH_PARAM, copia::kSlots),
		NUM_PARAMS
	};
	enum InputIds {
		INL_INPUT,
		INR_INPUT,
		CLOCK_INPUT,
		RESET_INPUT,
		CAPTURE_INPUT,
		INOUT_INPUT,
		SCENE_INPUT,
		MORPH_INPUT,
		MIX_INPUT,
		ENUMS(PUNCH_INPUT, copia::kSlots),
		NUM_INPUTS
	};
	enum OutputIds {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		WETL_OUTPUT,
		WETR_OUTPUT,
		BAR_OUTPUT,
		REC_OUTPUT,
		LANES_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	copia::Engine engine;

	// ---- options (context menu), saved
	int clockPpqn = 4;
	int beatsPerBar = 4;
	int sceneQuant = 3;          // 0 at once, 1 step, 2 beat, 3 bar
	int inQuant = 1;
	int law = 0;
	bool autoIn = false;
	bool resampleOut = false;
	float internalBpm = 120.f;   // without a clock
	float mutateAmount = 0.3f;

	// ---- the UI's side (atomics: the display writes them)
	std::atomic<int> sceneTarget {0};   // the pads
	std::atomic<int> morphB {1};
	std::atomic<bool> mute[copia::kSlots];
	std::atomic<int> randLane {-1};     // a lane's DICE from the display
	std::atomic<bool> uiRand {false}, uiMutate {false};

	// ---- audio thread
	dsp::SchmittTrigger clockIn, resetIn, captureIn, captureBtn, randBtn, mutateBtn;
	dsp::PulseGenerator barPulse;
	bidoo::dsp::PulseClock pulseClock;
	double freePpq = 0.0;
	double lastBarPpq = -1.0;
	float inL[copia::kBlock] = {}, inR[copia::kBlock] = {};
	float outL[copia::kBlock] = {}, outR[copia::kBlock] = {}, wetL[copia::kBlock] = {}, wetR[copia::kBlock] = {};
	double ppqs[copia::kBlock] = {};
	int pos = 0;
	bool blockRunning = false;
	double blockBpm = 120.0;
	int shownFx[copia::kSlots];
	int shownGrid = -1;
	int shownScene = -1;
	int lastCvScene = -1;
	bidoo::dsp::Rng rnd {0x5eed};
	std::atomic<float> bpmNow {0.f};
	std::atomic<bool> runningNow {false};
	std::atomic<bool> dirty {false};

	COPIA() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(CAPTURE_PARAM, "Capture (on the next bar; again to give up; Shift+click clears the loop)");
		configSwitch(LENGTH_PARAM, 0.f, 3.f, 2.f, "Capture length", {"1 bar", "2 bars", "4 bars", "8 bars"});
		configSwitch(GRID_PARAM, 0.f, copia::kNumGrids - 1, 2.f, "Grid (this scene)", {"1/8", "1/8T", "1/16", "1/16T", "1/32"});
		configButton(RAND_PARAM, "Rand: a new pattern and settings for this scene");
		configButton(MUTATE_PARAM, "Mutate: change this scene a little");
		configParam(MORPH_PARAM, 0.f, 1.f, 0.f, "Morph to scene B", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix", "%", 0.f, 100.f);
		configSwitch(INOUT_PARAM, 0.f, 1.f, 0.f, "IN/OUT: the remix", {"Out", "In"});
		std::vector<std::string> fxNames;
		for (int i = 0; i < copia::kNumEffects; i++)
			fxNames.push_back(copia::g::kEffectNames[(int)copia::kEffects[i].fx]);
		for (int s = 0; s < copia::kSlots; s++) {
			configSwitch(FX_PARAM + s, 0.f, copia::kNumEffects - 1, (float)copia::effectIndex(copia::defaultEffect(s)), rack::string::f("Slot %d effect", s + 1), fxNames);
			configParam(PUNCH_PARAM + s, 0.f, 1.f, 0.f, rack::string::f("Slot %d punch in", s + 1), "%", 0.f, 100.f);
			configInput(PUNCH_INPUT + s, rack::string::f("Slot %d punch in (0-10 V)", s + 1));
			mute[s] = false;
			shownFx[s] = -1;
		}
		configInput(INL_INPUT, "Left (polyphonic channels are summed)");
		configInput(INR_INPUT, "Right (unpatched: the left)");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset (bar 1)");
		configInput(CAPTURE_INPUT, "Capture trigger");
		configInput(INOUT_INPUT, "IN/OUT gate (high: in)");
		configInput(SCENE_INPUT, "Scene (0-10 V: scenes 1 to 8)");
		configInput(MORPH_INPUT, "Morph (0-10 V, added to the knob)");
		configInput(MIX_INPUT, "Mix (0-10 V, added to the knob)");
		configOutput(OUTL_OUTPUT, "Left");
		configOutput(OUTR_OUTPUT, "Right");
		configOutput(WETL_OUTPUT, "Remix left");
		configOutput(WETR_OUTPUT, "Remix right");
		configOutput(BAR_OUTPUT, "Bar trigger");
		configOutput(REC_OUTPUT, "Capturing gate");
		configOutput(LANES_OUTPUT, "Lane gates (8 channels)");
		configBypass(INL_INPUT, OUTL_OUTPUT);
		configBypass(INR_INPUT, OUTR_OUTPUT);
		updateClockName();
		engine.prepare(APP->engine->getSampleRate());
	}

	void updateClockName() {
		inputInfos[CLOCK_INPUT]->name = rack::string::f("Clock (%d PPQN)", clockPpqn);
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		engine.prepare(e.sampleRate);
		pulseClock.clear();
	}

	void onReset(const ResetEvent &e) override {
		Module::onReset(e);
		engine.clearRequest = true;
		for (auto &sc : engine.scenes) {
			sc.pattern.clear();
			sc.used = false;
			sc.grid = 2;
			sc.bars = 1;
			for (int s = 0; s < copia::kSlots; s++)
				sc.slot[s].setEffect(copia::defaultEffect(s));
		}
		engine.scenes[0].used = true;
		sceneTarget = 0;
		morphB = 1;
	}

	// ---------------------------------------------------------------- process

	void process(const ProcessArgs &args) override {
		// the clock: pulses, or free running at internalBpm without a cable
		const bool reset = resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f);
		double ppq, bpm;
		bool running;
		if (inputs[CLOCK_INPUT].isConnected()) {
			pulseClock.setPpqn(clockPpqn);
			if (reset)
				pulseClock.reset();
			pulseClock.tick(clockIn.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f), args.sampleRate);
			running = pulseClock.running();
			ppq = pulseClock.position();
			bpm = running ? pulseClock.bpm() : blockBpm;
		}
		else {
			if (reset)
				freePpq = 0.0;
			ppq = freePpq;
			freePpq += internalBpm / 60.0 / args.sampleRate;
			running = true;
			bpm = internalBpm;
		}

		// BAR out
		const int bpb = std::max(1, beatsPerBar);
		if (running && (lastBarPpq < 0.0 || std::floor(ppq / bpb + 1e-9) != std::floor(lastBarPpq / bpb + 1e-9)))
			barPulse.trigger(1e-3f);
		lastBarPpq = running ? ppq : -1.0;

		// CAPTURE: the button (Shift+click clears, in the widget) or a trigger
		bool cap = captureBtn.process(params[CAPTURE_PARAM].getValue());
		cap = captureIn.process(inputs[CAPTURE_INPUT].getVoltage(), 0.1f, 1.f) || cap;
		if (cap)
			engine.captureTrigger();

		inL[pos] = inputs[INL_INPUT].getVoltageSum() * 0.2f;
		inR[pos] = (inputs[INR_INPUT].isConnected() ? inputs[INR_INPUT].getVoltageSum() : inputs[INL_INPUT].getVoltageSum()) * 0.2f;
		ppqs[pos] = ppq;
		if (pos == 0) {
			blockRunning = running;
			blockBpm = bpm;
		}
		blockRunning = blockRunning && running;

		// what the block before gave
		outputs[OUTL_OUTPUT].setVoltage(outL[pos] * 5.f);
		outputs[OUTR_OUTPUT].setVoltage(outR[pos] * 5.f);
		outputs[WETL_OUTPUT].setVoltage(wetL[pos] * 5.f);
		outputs[WETR_OUTPUT].setVoltage(wetR[pos] * 5.f);
		outputs[BAR_OUTPUT].setVoltage(barPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[REC_OUTPUT].setVoltage(engine.capState.load(std::memory_order_relaxed) == 2 ? 10.f : 0.f);

		if (++pos < copia::kBlock)
			return;
		pos = 0;
		runBlock();
	}

	void runBlock() {
		copia::Engine &e = engine;
		// a scene that changed (a pad, the CV, a switch due): the knobs show it
		const int cur = e.current.load();
		if (cur != shownScene) {
			shownScene = cur;
			for (int s = 0; s < copia::kSlots; s++) {
				shownFx[s] = copia::effectIndex(e.scenes[cur].slot[s].effect());
				params[FX_PARAM + s].setValue((float)shownFx[s]);
			}
			shownGrid = e.scenes[cur].grid.load();
			params[GRID_PARAM].setValue((float)shownGrid);
		}
		// knobs turned: into the scene that plays
		copia::Scene &sc = e.scenes[cur];
		for (int s = 0; s < copia::kSlots; s++) {
			const int v = clamp((int)std::round(params[FX_PARAM + s].getValue()), 0, copia::kNumEffects - 1);
			if (v != shownFx[s]) {
				shownFx[s] = v;
				sc.slot[s].setEffect(copia::kEffects[v].fx);
			}
		}
		const int gv = clamp((int)std::round(params[GRID_PARAM].getValue()), 0, copia::kNumGrids - 1);
		if (gv != shownGrid) {
			shownGrid = gv;
			sc.grid = gv;
		}
		// Rand, Mutate, Dice
		if (randBtn.process(params[RAND_PARAM].getValue()) || uiRand.exchange(false))
			copia::randomizeScene(sc, beatsPerBar, rnd);
		if (mutateBtn.process(params[MUTATE_PARAM].getValue()) || uiMutate.exchange(false))
			copia::mutateScene(sc, beatsPerBar, mutateAmount, rnd);
		const int lane = randLane.exchange(-1);
		if (lane >= 0 && lane < copia::kLanes) {
			const float w = lane < copia::kSlots ? 0.5f * sc.slot[lane].weight.load() : 0.5f;
			copia::randomizeLane(sc.pattern, lane, sc.steps(beatsPerBar), sc.div(), w, rnd);
		}

		// SCENE CV: a new scene when it moves into another eighth
		if (inputs[SCENE_INPUT].isConnected()) {
			const int v = clamp((int)(inputs[SCENE_INPUT].getVoltage() / 1.25f), 0, copia::kScenes - 1);
			if (v != lastCvScene) {
				lastCvScene = v;
				sceneTarget = v;
			}
		}
		else {
			lastCvScene = -1;
		}

		copia::Controls ctl;
		ctl.beatsPerBar = beatsPerBar;
		static const int lengths[4] = {1, 2, 4, 8};
		ctl.lengthBars = lengths[clamp((int)std::round(params[LENGTH_PARAM].getValue()), 0, 3)];
		ctl.mix = clamp(params[MIX_PARAM].getValue() + inputs[MIX_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		ctl.law = law;
		ctl.wantIn = params[INOUT_PARAM].getValue() > 0.5f || inputs[INOUT_INPUT].getVoltage() >= 1.f;
		ctl.inQuant = inQuant;
		ctl.sceneQuant = sceneQuant;
		ctl.sceneTarget = sceneTarget.load();
		ctl.morphB = morphB.load();
		ctl.morph = clamp(params[MORPH_PARAM].getValue() + inputs[MORPH_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		for (int s = 0; s < copia::kSlots; s++) {
			ctl.punch[s] = clamp(params[PUNCH_PARAM + s].getValue() + inputs[PUNCH_INPUT + s].getVoltage() * 0.1f, 0.f, 1.f);
			ctl.mute[s] = mute[s].load();
		}
		ctl.autoIn = autoIn;

		e.process(inL, inR, ppqs, blockRunning, blockBpm, resampleOut, ctl, outL, outR, wetL, wetR);

		if (e.autoInFired.exchange(false))
			params[INOUT_PARAM].setValue(1.f);
		if (e.loopChanged.exchange(false))
			dirty = true;
		outputs[LANES_OUTPUT].setChannels(copia::kSlots);
		for (int s = 0; s < copia::kSlots; s++)
			outputs[LANES_OUTPUT].setVoltage(e.laneGate[s].load() ? 10.f : 0.f, s);
		bpmNow = (float)blockBpm;
		runningNow = blockRunning;
	}

	// ---------------------------------------------------------------- patch

	std::string storagePath() {
		return rack::system::join(createPatchStorageDirectory(), "loop.flac");
	}

	void onSave(const SaveEvent &e) override {
		const int f = engine.front.load();
		std::string path = storagePath();
		if (f < 0) {
			if (rack::system::isFile(path))
				rack::system::remove(path);
			return;
		}
		if (!dirty && rack::system::isFile(path))
			return;
		engine.blockCapture = true;
		const copia::Loop &lp = engine.loops[f];
		const int64_t total = lp.pre + lp.frames;
		std::vector<float> ch[2];
		ch[0].assign(lp.l.begin(), lp.l.begin() + total);
		ch[1].assign(lp.r.begin(), lp.r.begin() + total);
		engine.blockCapture = false;
		if (sounaire::flacWriteFile(path, ch, 2, total, (int)lp.rate, 24))
			dirty = false;
	}

	void onAdd(const AddEvent &e) override {
		std::string path = rack::system::join(getPatchStorageDirectory(), "loop.flac");
		if (!rack::system::isFile(path) || savedBeats <= 0.0)
			return;
		unsigned int channels = 0, rate = 0;
		drflac_uint64 frames = 0;
		float *data = drflac_open_file_and_read_pcm_frames_f32(path.c_str(), &channels, &rate, &frames, nullptr);
		if (!data)
			return;
		std::vector<float> l((size_t)frames), r((size_t)frames);
		for (drflac_uint64 i = 0; i < frames; i++) {
			l[(size_t)i] = data[i * channels];
			r[(size_t)i] = data[i * channels + (channels > 1 ? 1 : 0)];
		}
		drflac_free(data, nullptr);
		engine.blockCapture = true;
		engine.installLoop(l.data(), r.data(), (int64_t)frames, savedPre, savedBeats, savedStart, savedBpm, (float)rate);
		engine.blockCapture = false;
	}

	// what the patch said about its loop (read before onAdd)
	double savedBeats = 0.0, savedStart = 0.0;
	float savedBpm = 0.f;
	int savedPre = 0;

	static std::string laneText(const copia::g::Pattern &p, int lane) {
		std::string t(copia::g::Pattern::kMaxSteps, '.');
		for (int s = 0; s < copia::g::Pattern::kMaxSteps; s++)
			t[(size_t)s] = p.isStart(lane, s) ? '#' : p.isOn(lane, s) ? '-' : '.';
		while (!t.empty() && t.back() == '.')
			t.pop_back();
		return t;
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "clockPpqn", json_integer(clockPpqn));
		json_object_set_new(rootJ, "beatsPerBar", json_integer(beatsPerBar));
		json_object_set_new(rootJ, "sceneQuant", json_integer(sceneQuant));
		json_object_set_new(rootJ, "inQuant", json_integer(inQuant));
		json_object_set_new(rootJ, "law", json_integer(law));
		json_object_set_new(rootJ, "autoIn", json_boolean(autoIn));
		json_object_set_new(rootJ, "resampleOut", json_boolean(resampleOut));
		json_object_set_new(rootJ, "internalBpm", json_real(internalBpm));
		json_object_set_new(rootJ, "mutateAmount", json_real(mutateAmount));
		json_object_set_new(rootJ, "scene", json_integer(engine.current.load()));
		json_object_set_new(rootJ, "morphB", json_integer(morphB.load()));
		json_t *mutes = json_array();
		for (int s = 0; s < copia::kSlots; s++)
			json_array_append_new(mutes, json_boolean(mute[s].load()));
		json_object_set_new(rootJ, "mute", mutes);
		const int f = engine.front.load();
		if (f >= 0) {
			const copia::Loop &lp = engine.loops[f];
			json_t *loopJ = json_object();
			json_object_set_new(loopJ, "beats", json_real(lp.beats));
			// where the loop starts, within its own length: the clock starts again from 0 on reload
			json_object_set_new(loopJ, "start", json_real(std::fmod(lp.startPpq, lp.beats)));
			json_object_set_new(loopJ, "bpm", json_real(lp.bpm));
			json_object_set_new(loopJ, "pre", json_integer(lp.pre));
			json_object_set_new(rootJ, "loop", loopJ);
		}
		json_t *scenesJ = json_array();
		for (const copia::Scene &sc : engine.scenes) {
			json_t *sj = json_object();
			json_object_set_new(sj, "used", json_boolean(sc.used.load()));
			json_object_set_new(sj, "grid", json_integer(sc.grid.load()));
			json_object_set_new(sj, "bars", json_integer(sc.bars.load()));
			json_object_set_new(sj, "seed", json_integer(sc.pattern.getSeed()));
			json_t *slots = json_array();
			for (const copia::SlotData &sd : sc.slot) {
				json_t *o = json_object();
				json_object_set_new(o, "fx", json_integer(sd.fx.load()));
				json_t *ps = json_array();
				for (auto &v : sd.p)
					json_array_append_new(ps, json_real(v.load()));
				json_object_set_new(o, "p", ps);
				json_object_set_new(o, "freq", json_real(sd.freq.load()));
				json_object_set_new(o, "filter", json_integer(sd.filter.load()));
				json_object_set_new(o, "live", json_boolean(sd.live.load()));
				json_object_set_new(o, "weight", json_real(sd.weight.load()));
				json_array_append_new(slots, o);
			}
			json_object_set_new(sj, "slots", slots);
			json_t *lanes = json_array();
			json_t *blocks = json_array();
			for (int l = 0; l < copia::kLanes; l++) {
				json_array_append_new(lanes, json_string(laneText(sc.pattern, l).c_str()));
				for (int s = 0; s < copia::g::Pattern::kMaxSteps; s++)
					if (sc.pattern.isStart(l, s)) {
						const copia::g::BlockInfo b = sc.pattern.infoAt(l, s);
						if (b.level == 100 && b.shape == 0 && b.probability == 100 && b.ratchets == 1)
							continue;
						json_t *bj = json_array();
						for (int v : {l, s, (int)b.level, (int)b.shape, (int)b.probability, (int)b.ratchets})
							json_array_append_new(bj, json_integer(v));
						json_array_append_new(blocks, bj);
					}
			}
			json_object_set_new(sj, "lanes", lanes);
			json_object_set_new(sj, "blocks", blocks);
			json_array_append_new(scenesJ, sj);
		}
		json_object_set_new(rootJ, "scenes", scenesJ);
		return rootJ;
	}

	static int intOf(json_t *o, const char *key, int def) {
		json_t *j = json_object_get(o, key);
		return j ? (int)json_integer_value(j) : def;
	}
	static double realOf(json_t *o, const char *key, double def) {
		json_t *j = json_object_get(o, key);
		return j ? json_number_value(j) : def;
	}
	static bool boolOf(json_t *o, const char *key, bool def) {
		json_t *j = json_object_get(o, key);
		return j ? json_is_true(j) : def;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		clockPpqn = clamp(intOf(rootJ, "clockPpqn", 4), 1, 96);
		beatsPerBar = clamp(intOf(rootJ, "beatsPerBar", 4), 1, 16);
		sceneQuant = clamp(intOf(rootJ, "sceneQuant", 3), 0, 3);
		inQuant = clamp(intOf(rootJ, "inQuant", 1), 0, 3);
		law = clamp(intOf(rootJ, "law", 0), 0, 4);
		autoIn = boolOf(rootJ, "autoIn", false);
		resampleOut = boolOf(rootJ, "resampleOut", false);
		internalBpm = (float)std::clamp(realOf(rootJ, "internalBpm", 120.0), 30.0, 300.0);
		mutateAmount = (float)std::clamp(realOf(rootJ, "mutateAmount", 0.3), 0.0, 1.0);
		updateClockName();
		const int scene = clamp(intOf(rootJ, "scene", 0), 0, copia::kScenes - 1);
		sceneTarget = scene;
		engine.current = scene;
		shownScene = -1;
		morphB = clamp(intOf(rootJ, "morphB", 1), 0, copia::kScenes - 1);
		if (json_t *mutes = json_object_get(rootJ, "mute"))
			for (int s = 0; s < copia::kSlots; s++)
				mute[s] = json_is_true(json_array_get(mutes, s));
		savedBeats = 0.0;
		if (json_t *loopJ = json_object_get(rootJ, "loop")) {
			savedBeats = realOf(loopJ, "beats", 0.0);
			savedStart = realOf(loopJ, "start", 0.0);
			savedBpm = (float)realOf(loopJ, "bpm", 120.0);
			savedPre = intOf(loopJ, "pre", 0);
		}
		json_t *scenesJ = json_object_get(rootJ, "scenes");
		for (int i = 0; i < copia::kScenes && scenesJ; i++) {
			json_t *sj = json_array_get(scenesJ, i);
			if (!sj)
				break;
			copia::Scene &sc = engine.scenes[i];
			sc.used = boolOf(sj, "used", false);
			sc.grid = clamp(intOf(sj, "grid", 2), 0, copia::kNumGrids - 1);
			sc.bars = clamp(intOf(sj, "bars", 1), 1, 2);
			sc.pattern.clear();
			sc.pattern.setSeed(intOf(sj, "seed", 1));
			json_t *slots = json_object_get(sj, "slots");
			for (int s = 0; s < copia::kSlots && slots; s++) {
				json_t *o = json_array_get(slots, s);
				if (!o)
					break;
				copia::SlotData &sd = sc.slot[s];
				sd.fx = clamp(intOf(o, "fx", 0), 0, (int)copia::g::Fx::count - 1);
				if (json_t *ps = json_object_get(o, "p"))
					for (int k = 0; k < copia::g::kNumEffectParams; k++)
						sd.p[k] = clamp((float)json_number_value(json_array_get(ps, k)), 0.f, 1.f);
				sd.freq = (float)std::clamp(realOf(o, "freq", 0.5), 0.0, 1.0);
				sd.filter = clamp(intOf(o, "filter", 0), 0, 4);
				sd.live = boolOf(o, "live", false);
				sd.weight = (float)std::clamp(realOf(o, "weight", 1.0), 0.0, 1.0);
			}
			if (json_t *lanes = json_object_get(sj, "lanes"))
				for (int l = 0; l < copia::kLanes; l++) {
					const char *t = json_string_value(json_array_get(lanes, l));
					if (!t)
						continue;
					const int len = std::min((int)std::strlen(t), copia::g::Pattern::kMaxSteps);
					for (int s = 0; s < len;) {
						if (t[s] != '#') {
							s++;
							continue;
						}
						int end = s;
						while (end + 1 < len && t[end + 1] == '-')
							end++;
						sc.pattern.setBlock(l, s, end);
						s = end + 1;
					}
				}
			if (json_t *blocks = json_object_get(sj, "blocks")) {
				size_t k;
				json_t *bj;
				json_array_foreach(blocks, k, bj) {
					const int l = (int)json_integer_value(json_array_get(bj, 0)), s = (int)json_integer_value(json_array_get(bj, 1));
					if (l < 0 || l >= copia::kLanes || s < 0 || s >= copia::g::Pattern::kMaxSteps || !sc.pattern.isStart(l, s))
						continue;
					copia::g::BlockInfo b = sc.pattern.infoAt(l, s);
					b.level = (uint8_t)clamp((int)json_integer_value(json_array_get(bj, 2)), 0, 100);
					b.shape = (uint8_t)clamp((int)json_integer_value(json_array_get(bj, 3)), 0, (int)copia::g::Shape::count - 1);
					b.probability = (uint8_t)clamp((int)json_integer_value(json_array_get(bj, 4)), 0, 100);
					b.ratchets = (uint8_t)clamp((int)json_integer_value(json_array_get(bj, 5)), 1, 8);
					sc.pattern.setInfo(l, s, b);
				}
			}
		}
	}
};

#include "CopiaWidget.hpp"

Model *modelCOPIA = createModel<COPIA, COPIAWidget>("COPIA");
