#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include <vector>
#include "dep/quantizer.hpp"

using namespace std;

// 8 stages played as up to 16 steps. Each stage has a pitch, a number of
// pulses (clocks it lasts), a gate type, a slide and a skip. 16 patterns: the
// panel edits the selected one, the pattern CV can play any other.

struct Step {
	int index = 0;     // stage 0..7
	int number = 0;    // step 0..15
	bool skip = false;
	bool slide = false;
	int pulses = 1;
	float pitch = 0.f;
	int type = 2;
};

struct Pattern {
	enum GateType { REST, SINGLE, MULTIPLE, HOLD, EXT1, EXT2 };

	// Settings, saved with the patch.
	int playMode = 0;    // 0 forward, 1 backward, 2 ping-pong, 3 random, 4 brownian
	int countMode = 0;   // 0 steps, 1 pulses
	int numberOfStepsParam = 8;
	int rootNote = 0;    // -1 none
	int scale = 0;
	float gateTime = 0.5f;
	float slideTime = 0.2f;
	float sensitivity = 1.f;
	float stagePitch[8] = {};
	int stagePulses[8] = {1, 1, 1, 1, 1, 1, 1, 1};
	int stageType[8] = {2, 2, 2, 2, 2, 2, 2, 2};
	bool stageSlide[8] = {};
	bool stageSkip[8] = {};

	// Play position.
	int currentStep = 0;
	int currentPulse = 0;
	bool forward = true;

	// The steps, built from the settings.
	Step steps[16];

	Pattern() {
		build(numberOfStepsParam);
	}

	void copySettings(const Pattern &o) {
		int s = currentStep, p = currentPulse;
		bool f = forward;
		*this = o;
		currentStep = s;
		currentPulse = p;
		forward = f;
	}

	// In steps mode the first n steps play; in pulses mode the steps play
	// until n pulses are counted, skipped stages counting none.
	void build(int n) {
		int pCount = 0;
		for (int i = 0; i < 16; i++) {
			Step &s = steps[i];
			int idx = i % 8;
			s.index = idx;
			s.number = i;
			s.slide = stageSlide[idx];
			s.pitch = stagePitch[idx];
			s.type = stageType[idx];
			s.pulses = stagePulses[idx];
			if (countMode == 0)
				s.skip = stageSkip[idx] || i >= n;
			else if (stageSkip[idx] || pCount >= n)
				s.skip = true;
			else {
				s.skip = false;
				s.pulses = std::min(s.pulses, n - pCount);
				pCount += s.pulses;
			}
		}
	}

	int firstStep() {
		for (int i = 0; i < 16; i++)
			if (!steps[i].skip)
				return i;
		return 0;
	}

	int lastStep() {
		for (int i = 15; i >= 0; i--)
			if (!steps[i].skip)
				return i;
		return 15;
	}

	int nextForward(int pos) {
		for (int i = 1; i < 16; i++)
			if (!steps[(pos + i) % 16].skip)
				return (pos + i) % 16;
		return pos;
	}

	int nextBackward(int pos) {
		for (int i = 1; i < 16; i++)
			if (!steps[(pos + 16 - i) % 16].skip)
				return (pos + 16 - i) % 16;
		return pos;
	}

	void advance(bool reset) {
		if (reset) {
			currentStep = (playMode == 1) ? lastStep() : firstStep();
			currentPulse = 0;
			forward = true;
			return;
		}
		if (!steps[currentStep].skip && currentPulse < steps[currentStep].pulses - 1) {
			currentPulse++;
			return;
		}
		currentPulse = 0;
		switch (playMode) {
			case 0: currentStep = nextForward(currentStep); break;
			case 1: currentStep = nextBackward(currentStep); break;
			case 2: {
				if (currentStep == lastStep())
					forward = false;
				if (currentStep == firstStep())
					forward = true;
				currentStep = forward ? nextForward(currentStep) : nextBackward(currentStep);
				break;
			}
			case 3: {
				int count = 0;
				for (int i = 0; i < 16; i++)
					count += !steps[i].skip;
				if (count == 0)
					break;
				int k = random::u32() % count;
				for (int i = 0; i < 16; i++) {
					if (!steps[i].skip && k-- == 0) {
						currentStep = i;
						break;
					}
				}
				break;
			}
			case 4: currentStep = (random::uniform() < 0.5f) ? nextBackward(currentStep) : nextForward(currentStep); break;
			default: break;
		}
	}
};

struct DTROY : BidooModule {
	enum ParamIds {
		CLOCK_PARAM,
		RUN_PARAM,
		RESET_PARAM,
		STEPS_PARAM,
		SLIDE_TIME_PARAM,
		GATE_TIME_PARAM,
		ROOT_NOTE_PARAM,
		SCALE_PARAM,
		PLAY_MODE_PARAM,
		COUNT_MODE_PARAM,
		PATTERN_PARAM,
		SENSITIVITY_PARAM,
		TRIG_COUNT_PARAM = SENSITIVITY_PARAM + 8,
		TRIG_TYPE_PARAM = TRIG_COUNT_PARAM + 8,
		TRIG_PITCH_PARAM = TRIG_TYPE_PARAM + 8,
		TRIG_SLIDE_PARAM = TRIG_PITCH_PARAM + 8,
		TRIG_SKIP_PARAM = TRIG_SLIDE_PARAM + 8,
		LEFT_PARAM = TRIG_SKIP_PARAM + 8,
		RIGHT_PARAM,
		UP_PARAM,
		DOWN_PARAM,
		COPY_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		EXT_CLOCK_INPUT,
		RESET_INPUT,
		STEPS_INPUT,
		SLIDE_TIME_INPUT,
		GATE_TIME_INPUT,
		ROOT_NOTE_INPUT,
		SCALE_INPUT,
		EXTGATE1_INPUT,
		EXTGATE2_INPUT,
		PATTERN_INPUT,
		TRANSPOSE_INPUT,
		SENSITIVITY_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		GATE_OUTPUT,
		PITCH_OUTPUT,
		STEP_OUTPUT,
		NUM_OUTPUTS = STEP_OUTPUT + 8
	};
	enum LightIds {
		RUNNING_LIGHT,
		RESET_LIGHT,
		GATE_LIGHT,
		STEPS_LIGHTS = GATE_LIGHT + 8,
		SLIDES_LIGHTS = STEPS_LIGHTS + 8,
		SKIPS_LIGHTS = SLIDES_LIGHTS + 8,
		COPY_LIGHT = SKIPS_LIGHTS + 8,
		NUM_LIGHTS
	};

	bool running = true;
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger runningTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger slideTriggers[8];
	dsp::SchmittTrigger skipTriggers[8];
	dsp::SchmittTrigger playModeTrigger;
	dsp::SchmittTrigger countModeTrigger;
	dsp::SchmittTrigger copyTrigger;
	dsp::SchmittTrigger upTrigger;
	dsp::SchmittTrigger downTrigger;
	dsp::SchmittTrigger leftTrigger;
	dsp::SchmittTrigger rightTrigger;
	dsp::PulseGenerator stepPulse[8];

	double phase = 0.0;          // position in the current pulse, 0..1
	long sinceClock = -1;        // samples since the last external clock
	long clockInterval = 0;      // samples between the last two external clocks
	bool resetArmed = true;      // the next clock plays the first step
	bool started = false;        // a step has been played since the start
	int pulse = 0;
	long sincePulse = 0;         // samples since the last clock
	float pitch = 0.f;
	float target = 0.f;          // the quantized pitch of the step
	float slideFrom = 0.f;
	bool lastGate = false;
	int selectedPattern = 0;
	int playedPattern = 0;
	int copyPattern = -1;
	bool stepOutputsMode = false;

	Pattern patterns[16];
	quantizer::Quantizer quant;

	DTROY() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CLOCK_PARAM, -3.f, 3.f, 0.f, "Clock", " BPM", 2.f, 120.f);
		configButton(RUN_PARAM, "Run");
		configButton(RESET_PARAM, "Reset (the next clock plays the first step)");
		configParam(STEPS_PARAM, 1.f, 16.f, 8.f, "Steps (or pulses in pulse count mode)");
		paramQuantities[STEPS_PARAM]->snapEnabled = true;
		std::vector<std::string> roots, scales;
		for (int i = 0; i < quantizer::numNotes; i++)
			roots.push_back(quantizer::rootNotes[i].label);
		for (int i = 0; i < quantizer::numScales; i++)
			scales.push_back(quantizer::scales[i].label);
		configSwitch(ROOT_NOTE_PARAM, -1.f, quantizer::numNotes - 2, 0.f, "Root note", roots);
		configSwitch(SCALE_PARAM, 0.f, quantizer::numScales - 1, 0.f, "Scale", scales);
		configParam(GATE_TIME_PARAM, 0.1f, 1.f, 0.5f, "Gate length", "%", 0.f, 100.f);
		configParam(SLIDE_TIME_PARAM, 0.1f, 1.f, 0.2f, "Slide curve", "%", 0.f, 100.f);
		configButton(PLAY_MODE_PARAM, "Play mode (forward, backward, ping-pong, random, brownian)");
		configButton(COUNT_MODE_PARAM, "Count mode (steps or pulses)");
		configParam(PATTERN_PARAM, 1.f, 16.f, 1.f, "Edited pattern");
		paramQuantities[PATTERN_PARAM]->snapEnabled = true;
		paramQuantities[PATTERN_PARAM]->randomizeEnabled = false;
		configParam(SENSITIVITY_PARAM, 0.1f, 1.f, 1.f, "Pitch sensitivity", "%", 0.f, 100.f);

		configButton(COPY_PARAM, "Copy, then paste on another pattern");
		configButton(LEFT_PARAM, "Rotate the stages left");
		configButton(RIGHT_PARAM, "Rotate the stages right");
		configButton(UP_PARAM, "Transpose up 1 semitone");
		configButton(DOWN_PARAM, "Transpose down 1 semitone");

		configInput(CLOCK_INPUT, "Clock tempo (1V/oct, 0 V = 120 BPM with the knob at 120)");
		configInput(EXT_CLOCK_INPUT, "External clock");
		configInput(RESET_INPUT, "Reset (the next clock plays the first step)");
		configInput(STEPS_INPUT, "Steps (1 V per step)");
		configInput(SLIDE_TIME_INPUT, "Slide curve (10 V = 100%)");
		configInput(GATE_TIME_INPUT, "Gate length (10 V = 100%)");
		configInput(ROOT_NOTE_INPUT, "Root note (0-10 V = 0-11 semitones)");
		configInput(SCALE_INPUT, "Scale (1 V per scale)");
		configInput(EXTGATE1_INPUT, "External gate 1");
		configInput(EXTGATE2_INPUT, "External gate 2");
		configInput(PATTERN_INPUT, "Played pattern (0-10 V = 1-16)");
		configInput(TRANSPOSE_INPUT, "Transpose (1V/oct, before the quantizer)");
		configInput(SENSITIVITY_INPUT, "Sensitivity (10 V = 100%)");

		configOutput(GATE_OUTPUT, "Gate");
		configOutput(PITCH_OUTPUT, "Pitch (1V/oct)");

		for (int i = 0; i < 8; i++) {
			configParam(TRIG_PITCH_PARAM + i, -4.f, 6.f, 0.f, string::f("Stage %d pitch", i + 1), " V");
			configParam(TRIG_COUNT_PARAM + i, 1.f, 8.f, 1.f, string::f("Stage %d pulses", i + 1));
			paramQuantities[TRIG_COUNT_PARAM + i]->snapEnabled = true;
			configSwitch(TRIG_TYPE_PARAM + i, 0.f, 5.f, 2.f, string::f("Stage %d gate", i + 1), {"Rest", "Single", "Multiple", "Hold", "External gate 1", "External gate 2"});
			configButton(TRIG_SLIDE_PARAM + i, string::f("Stage %d slide", i + 1));
			configButton(TRIG_SKIP_PARAM + i, string::f("Stage %d skip", i + 1));
			configOutput(STEP_OUTPUT + i, string::f("Stage %d", i + 1));
		}
	}

	// The panel shows the selected pattern.
	void loadPanel() {
		const Pattern &p = patterns[selectedPattern];
		params[STEPS_PARAM].setValue(p.numberOfStepsParam);
		params[ROOT_NOTE_PARAM].setValue(p.rootNote);
		params[SCALE_PARAM].setValue(p.scale);
		params[GATE_TIME_PARAM].setValue(p.gateTime);
		params[SLIDE_TIME_PARAM].setValue(p.slideTime);
		params[SENSITIVITY_PARAM].setValue(p.sensitivity);
		for (int i = 0; i < 8; i++) {
			params[TRIG_PITCH_PARAM + i].setValue(p.stagePitch[i]);
			params[TRIG_COUNT_PARAM + i].setValue(p.stagePulses[i]);
			params[TRIG_TYPE_PARAM + i].setValue(p.stageType[i]);
		}
	}

	void savePanel() {
		Pattern &p = patterns[selectedPattern];
		p.numberOfStepsParam = clamp((int)std::round(params[STEPS_PARAM].getValue()), 1, 16);
		p.rootNote = clamp((int)std::round(params[ROOT_NOTE_PARAM].getValue()), -1, 11);
		p.scale = clamp((int)std::round(params[SCALE_PARAM].getValue()), 0, quantizer::numScales - 1);
		p.gateTime = params[GATE_TIME_PARAM].getValue();
		p.slideTime = params[SLIDE_TIME_PARAM].getValue();
		p.sensitivity = params[SENSITIVITY_PARAM].getValue();
		for (int i = 0; i < 8; i++) {
			p.stagePitch[i] = params[TRIG_PITCH_PARAM + i].getValue();
			p.stagePulses[i] = clamp((int)std::round(params[TRIG_COUNT_PARAM + i].getValue()), 1, 8);
			p.stageType[i] = clamp((int)std::round(params[TRIG_TYPE_PARAM + i].getValue()), 0, 5);
		}
	}

	void process(const ProcessArgs &args) override;

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "running", json_boolean(running));
		json_object_set_new(rootJ, "stepOutputsMode", json_boolean(stepOutputsMode));
		json_object_set_new(rootJ, "selectedPattern", json_integer(selectedPattern));
		json_object_set_new(rootJ, "playedPattern", json_integer(playedPattern));
		json_t *patternsJ = json_array();
		for (int i = 0; i < 16; i++) {
			const Pattern &p = patterns[i];
			json_t *pJ = json_object();
			json_object_set_new(pJ, "playMode", json_integer(p.playMode));
			json_object_set_new(pJ, "countMode", json_integer(p.countMode));
			json_object_set_new(pJ, "numSteps", json_integer(p.numberOfStepsParam));
			json_object_set_new(pJ, "rootNote", json_integer(p.rootNote));
			json_object_set_new(pJ, "scale", json_integer(p.scale));
			json_object_set_new(pJ, "gateTime", json_real(p.gateTime));
			json_object_set_new(pJ, "slideTime", json_real(p.slideTime));
			json_object_set_new(pJ, "sensitivity", json_real(p.sensitivity));
			json_t *stagesJ = json_array();
			for (int j = 0; j < 8; j++) {
				json_t *sJ = json_object();
				json_object_set_new(sJ, "pitch", json_real(p.stagePitch[j]));
				json_object_set_new(sJ, "pulses", json_integer(p.stagePulses[j]));
				json_object_set_new(sJ, "type", json_integer(p.stageType[j]));
				json_object_set_new(sJ, "slide", json_boolean(p.stageSlide[j]));
				json_object_set_new(sJ, "skip", json_boolean(p.stageSkip[j]));
				json_array_append_new(stagesJ, sJ);
			}
			json_object_set_new(pJ, "stages", stagesJ);
			json_array_append_new(patternsJ, pJ);
		}
		json_object_set_new(rootJ, "patterns", patternsJ);
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		if (json_t *j = json_object_get(rootJ, "running")) running = json_is_true(j);
		if (json_t *j = json_object_get(rootJ, "stepOutputsMode")) stepOutputsMode = json_is_true(j);
		if (json_t *j = json_object_get(rootJ, "selectedPattern")) selectedPattern = clamp((int)json_integer_value(j), 0, 15);
		if (json_t *j = json_object_get(rootJ, "playedPattern")) playedPattern = clamp((int)json_integer_value(j), 0, 15);
		json_t *patternsJ = json_object_get(rootJ, "patterns");
		for (int i = 0; i < 16; i++) {
			Pattern &p = patterns[i];
			p = Pattern();
			json_t *pJ = patternsJ ? json_array_get(patternsJ, i) : NULL;
			if (!pJ)
				continue;
			if (json_t *j = json_object_get(pJ, "playMode")) p.playMode = clamp((int)json_integer_value(j), 0, 4);
			if (json_t *j = json_object_get(pJ, "countMode")) p.countMode = clamp((int)json_integer_value(j), 0, 1);
			if (json_t *j = json_object_get(pJ, "numSteps")) p.numberOfStepsParam = clamp((int)json_integer_value(j), 1, 16);
			if (json_t *j = json_object_get(pJ, "rootNote")) p.rootNote = clamp((int)json_integer_value(j), -1, 11);
			if (json_t *j = json_object_get(pJ, "scale")) p.scale = clamp((int)json_integer_value(j), 0, quantizer::numScales - 1);
			if (json_t *j = json_object_get(pJ, "gateTime")) p.gateTime = json_number_value(j);
			if (json_t *j = json_object_get(pJ, "slideTime")) p.slideTime = json_number_value(j);
			if (json_t *j = json_object_get(pJ, "sensitivity")) p.sensitivity = json_number_value(j);
			json_t *stagesJ = json_object_get(pJ, "stages");
			for (int s = 0; s < 8 && stagesJ; s++) {
				json_t *sJ = json_array_get(stagesJ, s);
				if (!sJ)
					continue;
				if (json_t *j = json_object_get(sJ, "pitch")) p.stagePitch[s] = clamp((float)json_number_value(j), -4.f, 6.f);
				if (json_t *j = json_object_get(sJ, "pulses")) p.stagePulses[s] = clamp((int)json_integer_value(j), 1, 8);
				if (json_t *j = json_object_get(sJ, "type")) p.stageType[s] = clamp((int)json_integer_value(j), 0, 5);
				if (json_t *j = json_object_get(sJ, "slide")) p.stageSlide[s] = json_is_true(j);
				if (json_t *j = json_object_get(sJ, "skip")) p.stageSkip[s] = json_is_true(j);
			}
			p.build(p.numberOfStepsParam);
		}
		resetArmed = true;
		started = false;
	}

	void onReset() override {
		for (int i = 0; i < 16; i++)
			patterns[i] = Pattern();
		selectedPattern = playedPattern = 0;
		copyPattern = -1;
		resetArmed = true;
		started = false;
		running = true;
	}

	void onRandomize() override {
		randomizeSlidesSkips();
	}

	void randomizeSlidesSkips() {
		Pattern &p = patterns[selectedPattern];
		for (int i = 0; i < 8; i++) {
			p.stageSlide[i] = random::uniform() > 0.8f;
			p.stageSkip[i] = random::uniform() > 0.85f;
		}
	}

	void randomizePitch() {
		for (int i = 0; i < 8; i++)
			params[TRIG_PITCH_PARAM + i].setValue(random::uniform() * 10.f - 4.f);
	}

	void randomizeGates() {
		for (int i = 0; i < 8; i++) {
			params[TRIG_COUNT_PARAM + i].setValue((int)(random::uniform() * 7.f + 1.f));
			params[TRIG_TYPE_PARAM + i].setValue((int)(random::uniform() * 5.f));
		}
	}
};

void DTROY::process(const ProcessArgs &args) {
	if (runningTrigger.process(params[RUN_PARAM].getValue()))
		running = !running;

	// Editing: the panel shows the selected pattern.
	int target_ = clamp((int)params[PATTERN_PARAM].getValue() - 1, 0, 15);
	if (target_ != selectedPattern) {
		selectedPattern = target_;
		loadPanel();
	}
	Pattern &ed = patterns[selectedPattern];

	if (copyTrigger.process(params[COPY_PARAM].getValue())) {
		if (copyPattern < 0)
			copyPattern = selectedPattern;
		else {
			if (copyPattern != selectedPattern) {
				ed.copySettings(patterns[copyPattern]);
				loadPanel();
			}
			copyPattern = -1;
		}
	}
	if (upTrigger.process(params[UP_PARAM].getValue()))
		for (int i = 0; i < 8; i++)
			params[TRIG_PITCH_PARAM + i].setValue(std::min(params[TRIG_PITCH_PARAM + i].getValue() + 1.f / 12.f, 6.f));
	if (downTrigger.process(params[DOWN_PARAM].getValue()))
		for (int i = 0; i < 8; i++)
			params[TRIG_PITCH_PARAM + i].setValue(std::max(params[TRIG_PITCH_PARAM + i].getValue() - 1.f / 12.f, -4.f));
	bool left = leftTrigger.process(params[LEFT_PARAM].getValue());
	bool right = rightTrigger.process(params[RIGHT_PARAM].getValue());
	if (left || right) {
		float p[8], c[8], t[8];
		bool sl[8], sk[8];
		for (int i = 0; i < 8; i++) {
			int from = left ? (i + 1) % 8 : (i + 7) % 8;
			p[i] = params[TRIG_PITCH_PARAM + from].getValue();
			c[i] = params[TRIG_COUNT_PARAM + from].getValue();
			t[i] = params[TRIG_TYPE_PARAM + from].getValue();
			sl[i] = ed.stageSlide[from];
			sk[i] = ed.stageSkip[from];
		}
		for (int i = 0; i < 8; i++) {
			params[TRIG_PITCH_PARAM + i].setValue(p[i]);
			params[TRIG_COUNT_PARAM + i].setValue(c[i]);
			params[TRIG_TYPE_PARAM + i].setValue(t[i]);
			ed.stageSlide[i] = sl[i];
			ed.stageSkip[i] = sk[i];
		}
	}
	for (int i = 0; i < 8; i++) {
		if (slideTriggers[i].process(params[TRIG_SLIDE_PARAM + i].getValue()))
			ed.stageSlide[i] = !ed.stageSlide[i];
		if (skipTriggers[i].process(params[TRIG_SKIP_PARAM + i].getValue()))
			ed.stageSkip[i] = !ed.stageSkip[i];
	}
	if (playModeTrigger.process(params[PLAY_MODE_PARAM].getValue()))
		ed.playMode = (ed.playMode + 1) % 5;
	if (countModeTrigger.process(params[COUNT_MODE_PARAM].getValue()))
		ed.countMode = (ed.countMode + 1) % 2;
	savePanel();

	// Playing: the pattern CV picks any pattern, the position carries over.
	int newPattern = inputs[PATTERN_INPUT].isConnected() ? clamp((int)rescale(inputs[PATTERN_INPUT].getVoltage(), 0.f, 10.f, 1.f, 16.1f) - 1, 0, 15) : selectedPattern;
	if (newPattern != playedPattern) {
		patterns[newPattern].currentStep = patterns[playedPattern].currentStep;
		patterns[newPattern].currentPulse = patterns[playedPattern].currentPulse;
		playedPattern = newPattern;
	}
	Pattern &pl = patterns[playedPattern];
	pl.build(clamp((int)(pl.numberOfStepsParam + inputs[STEPS_INPUT].getVoltage()), 1, 16));

	// Clock: internal at 1V/oct (0 V = 120 BPM), or the external clock with
	// the phase measured from its last interval.
	bool tick = false;
	double rate = 2.0 * std::exp2((double)params[CLOCK_PARAM].getValue() + inputs[CLOCK_INPUT].getVoltage());
	bool ext = inputs[EXT_CLOCK_INPUT].isConnected();
	bool extEdge = clockTrigger.process(ext ? inputs[EXT_CLOCK_INPUT].getVoltage() : 0.f, 0.1f, 1.f);
	if (ext) {
		if (sinceClock >= 0)
			sinceClock++;
		if (extEdge) {
			if (sinceClock > 0)
				clockInterval = sinceClock;
			sinceClock = 0;
		}
	}
	else {
		sinceClock = -1;
		clockInterval = 0;
	}
	if (resetTrigger.process(params[RESET_PARAM].getValue() + inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
		resetArmed = true;
		lights[RESET_LIGHT].setBrightness(1.f);
		// The internal clock restarts on the reset: its first clock is now. A
		// reset up to 1 ms after an external clock belongs to that clock.
		if (!ext) {
			phase = 0.0;
			tick = running;
		}
		else if (running && started && sincePulse < (long)std::round(1e-3f * args.sampleRate))
			tick = true;
	}
	if (running) {
		if (ext) {
			if (extEdge) {
				tick = true;
				phase = 0.0;
			}
			else
				phase = clockInterval > 0 ? (double)sinceClock / clockInterval : phase + rate * args.sampleTime;
		}
		else if (!tick) {
			phase += rate * args.sampleTime;
			if (phase >= 1.0) {
				phase -= std::floor(phase);
				tick = true;
			}
		}
	}

	if (tick) {
		slideFrom = pitch;
		pl.advance(resetArmed);
		resetArmed = false;
		started = true;
		pulse = pl.currentPulse;
		sincePulse = 0;
		const Step &s = pl.steps[pl.currentStep];
		if (!s.skip && (stepOutputsMode || pulse == 0))
			stepPulse[s.index].trigger(1e-3f);
		lights[STEPS_LIGHTS + s.index].setBrightness(1.f);
	}

	// Gate.
	const Step &s = pl.steps[pl.currentStep];
	bool active = running && started && !s.skip;
	float gateCoeff = clamp(pl.gateTime - 0.02f + inputs[GATE_TIME_INPUT].getVoltage() * 0.1f, 0.f, 0.99f);
	bool timed = phase < gateCoeff || sincePulse < (long)std::round(1e-3f * args.sampleRate);
	bool gate = false;
	float gateValue = 10.f;
	if (active) {
		switch (s.type) {
			case Pattern::SINGLE: gate = pulse == 0 && timed; break;
			case Pattern::MULTIPLE: gate = timed; break;
			case Pattern::HOLD: gate = pulse < s.pulses - 1 || timed; break;
			case Pattern::EXT1: gate = true; gateValue = inputs[EXTGATE1_INPUT].getVoltage(); break;
			case Pattern::EXT2: gate = true; gateValue = inputs[EXTGATE2_INPUT].getVoltage(); break;
			default: break;
		}
		// A new gate always gets a low sample before it, so it is a new edge.
		if (tick && lastGate && (pulse == 0 || s.type == Pattern::MULTIPLE) && s.type >= Pattern::SINGLE && s.type <= Pattern::HOLD)
			gate = false;
	}
	lastGate = gate;
	sincePulse++;

	// Pitch: quantized while the stage plays, held through rests.
	if (active && s.type != Pattern::REST) {
		float sens = clamp(pl.sensitivity + (inputs[SENSITIVITY_INPUT].isConnected() ? inputs[SENSITIVITY_INPUT].getVoltage() * 0.1f : 0.f), 0.1f, 1.f);
		float v = clamp(s.pitch, -4.f, 6.f) * sens + inputs[TRANSPOSE_INPUT].getVoltage();
		int root = pl.rootNote;
		if (root >= 0)
			root = clamp((int)(root + clamp(inputs[ROOT_NOTE_INPUT].getVoltage(), 0.f, 10.f) * 1.1f), 0, 11);
		int scale = clamp((int)(pl.scale + inputs[SCALE_INPUT].getVoltage()), 0, quantizer::numScales - 1);
		target = std::get<0>(quant.closestVoltageInScale(v, root, scale));
	}
	if (s.slide && pulse == 0 && started) {
		float curve = clamp(pl.slideTime + inputs[SLIDE_TIME_INPUT].getVoltage() * 0.1f, 0.f, 0.99f);
		pitch = slideFrom + (target - slideFrom) * std::pow((float)std::min(phase, 1.0), curve);
	}
	else
		pitch = target;

	outputs[GATE_OUTPUT].setVoltage(gate ? gateValue : 0.f);
	outputs[PITCH_OUTPUT].setVoltage(pitch);

	// Lights & stage outputs.
	const float lambda = 13.333333f * args.sampleTime;
	for (int i = 0; i < 8; i++) {
		float b = lights[STEPS_LIGHTS + i].getBrightness();
		b -= b * lambda;
		lights[STEPS_LIGHTS + i].setBrightness(b);
		lights[SLIDES_LIGHTS + i].setBrightness(ed.stageSlide[i] ? 1.f - b : b);
		lights[SKIPS_LIGHTS + i].setBrightness(ed.stageSkip[i] ? 1.f - b : b);
		outputs[STEP_OUTPUT + i].setVoltage(stepPulse[i].process(args.sampleTime) ? 10.f : 0.f);
	}
	float r = lights[RESET_LIGHT].getBrightness();
	lights[RESET_LIGHT].setBrightness(r - r * lambda);
	lights[RUNNING_LIGHT].setBrightness(running ? 1.f : 0.f);
	lights[COPY_LIGHT].setBrightness(copyPattern >= 0 ? 1.f : 0.f);
}

struct DTROYDisplay : TransparentWidget {
	DTROY *module;
	int frame = 0;

	std::string note, scale, steps, playMode, selectedPattern, playedPattern;

	DTROYDisplay() {

	}

	void drawMessage(NVGcontext *vg, Vec pos, std::string note, std::string playMode, std::string selectedPattern, std::string playedPattern, std::string steps, std::string scale) {
		nvgFontSize(vg, 18.0f);
		nvgFillColor(vg, YELLOW_BIDOO);
		nvgText(vg, pos.x + 3.0f, pos.y + 8.0f, playMode.c_str(), NULL);
		nvgFontSize(vg, 14.0f);
		nvgText(vg, pos.x + 114.0f, pos.y + 7.0f, selectedPattern.c_str(), NULL);

		nvgText(vg, pos.x + 30.0f, pos.y + 7.0f, steps.c_str(), NULL);
		nvgText(vg, pos.x + 3.0f, pos.y + 21.0f, note.c_str(), NULL);
		nvgText(vg, pos.x + 25.0f, pos.y + 21.0f, scale.c_str(), NULL);

		if (++frame <= 30) {
			nvgText(vg, pos.x + 90.0f, pos.y + 7.0f, playedPattern.c_str(), NULL);
		}
		else if (++frame>60) {
			frame = 0;
		}
	}

	std::string displayRootNote(int value) {
		return quantizer::rootNotes[value+1].label.c_str();
	}

	std::string displayScale(int value) {
		return quantizer::scales[value].label.c_str();
	}

	std::string displayPlayMode(int value) {
		switch(value){
			case 0: return "►";
			case 1: return "◄";
			case 2: return "►◄";
			case 3: return "►*";
			case 4: return "►?";
			default: return "";
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module) {
	      note = displayRootNote(module->patterns[module->selectedPattern].rootNote);
	  		steps = (module->patterns[module->selectedPattern].countMode == 0 ? "steps:" : "pulses:" ) + to_string(module->patterns[module->selectedPattern].numberOfStepsParam);
	  		playMode = displayPlayMode(module->patterns[module->selectedPattern].playMode);
	  		scale = displayScale(module->patterns[module->selectedPattern].scale);
	  		selectedPattern = "P" + to_string(module->selectedPattern + 1);
	  		playedPattern = "P" + to_string(module->playedPattern + 1);
	  		drawMessage(args.vg, Vec(0, 20), note, playMode, selectedPattern, playedPattern, steps, scale);
	    }
		}
		Widget::drawLayer(args, layer);
	}

};

struct DTROYWidget : BidooWidget {
	ParamWidget *stepsParam, *scaleParam, *rootNoteParam, *sensitivityParam,
	 *gateTimeParam, *slideTimeParam, *playModeParam, *countModeParam, *patternParam,
		*pitchParams[8], *pulseParams[8], *typeParams[8], *slideParams[8], *skipParams[8],
		 *pitchRndParams[8], *pulseProbParams[8], *accentParams[8], *rndAccentParams[8];

	void appendContextMenu(ui::Menu *menu) override;

	DTROYWidget(DTROY *module);
};

DTROYWidget::DTROYWidget(DTROY *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/DTROY.svg"));

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	{
		DTROYDisplay *display = new DTROYDisplay();
		display->module = module;
		display->box.pos = Vec(20.0f, 217.0f);
		display->box.size = Vec(250.0f, 60.0f);
		addChild(display);
	}

	addParam(createParam<BidooRoundBlackKnob>(Vec(17.0f, 36.0f), module, DTROY::CLOCK_PARAM));
	addParam(createParam<LEDButton>(Vec(61.0f, 40.0f), module, DTROY::RUN_PARAM));
	addChild(createLight<SmallLight<GreenLight>>(Vec(67.0f, 46.0f), module, DTROY::RUNNING_LIGHT));
	addParam(createParam<LEDButton>(Vec(99.0f, 40.0f), module, DTROY::RESET_PARAM));
	addChild(createLight<SmallLight<GreenLight>>(Vec(105.0f, 46.0f), module, DTROY::RESET_LIGHT));
	stepsParam = createParam<BidooBlueSnapKnob>(Vec(132.0f, 36.0f), module, DTROY::STEPS_PARAM);
	addParam(stepsParam);

	static const float portX0[4] = {20.0f, 58.0f, 96.0f, 135.0f};
 	addInput(createInput<PJ301MPort>(Vec(portX0[0], 69.0f), module, DTROY::CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 69.0f), module, DTROY::EXT_CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 69.0f), module, DTROY::RESET_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[3], 69.0f), module, DTROY::STEPS_INPUT));

	rootNoteParam = createParam<BidooBlueSnapKnob>(Vec(portX0[0]-2.0f, 116.0f), module, DTROY::ROOT_NOTE_PARAM);
	addParam(rootNoteParam);
	scaleParam = createParam<BidooBlueSnapKnob>(Vec(portX0[1]-2.0f, 116.0f), module, DTROY::SCALE_PARAM);
	addParam(scaleParam);
	gateTimeParam = createParam<BidooBlueKnob>(Vec(portX0[2]-2.0f, 116.0f), module, DTROY::GATE_TIME_PARAM);
	addParam(gateTimeParam);
	slideTimeParam = createParam<BidooBlueKnob>(Vec(portX0[3]-2.0f, 116.0f), module, DTROY::SLIDE_TIME_PARAM);
	addParam(slideTimeParam);

	addInput(createInput<PJ301MPort>(Vec(portX0[0], 149.0f), module, DTROY::ROOT_NOTE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 149.0f), module, DTROY::SCALE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 149.0f), module, DTROY::GATE_TIME_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[3], 149.0f), module, DTROY::SLIDE_TIME_INPUT));

	playModeParam = createParam<BlueCKD6>(Vec(portX0[0]-1.0f, 196.0f), module, DTROY::PLAY_MODE_PARAM);
	addParam(playModeParam);
	countModeParam = createParam<BlueCKD6>(Vec(portX0[1]-1.0f, 196.0f), module, DTROY::COUNT_MODE_PARAM);
	addParam(countModeParam);
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 198.0f), module, DTROY::PATTERN_INPUT));
	patternParam = createParam<BidooRoundBlackSnapKnob>(Vec(portX0[3]-1,196.0f), module, DTROY::PATTERN_PARAM);
	addParam(patternParam);

	static const float portX1[8] = {200.0f, 238.0f, 276.0f, 315.0f, 353.0f, 392.0f, 430.0f, 469.0f};

	sensitivityParam = createParam<BidooBlueTrimpot>(Vec(portX1[6]+21.0f, 18.0f), module, DTROY::SENSITIVITY_PARAM);
	addParam(sensitivityParam);
	addInput(createInput<TinyPJ301MPort>(Vec(portX1[6]-10.f, 18.0f), module, DTROY::SENSITIVITY_INPUT));

	addInput(createInput<PJ301MPort>(Vec(portX0[0], 286.0f), module, DTROY::TRANSPOSE_INPUT));
	addParam(createParam<BlueCKD6>(Vec(portX0[1]-1.0f, 285.0f), module, DTROY::COPY_PARAM));
	addChild(createLight<SmallLight<GreenLight>>(Vec(portX0[1]+23.0f, 283.0f), module, DTROY::COPY_LIGHT));

	addParam(createParam<LeftBtn>(Vec(104.0f, 290.0f), module, DTROY::LEFT_PARAM));
	addParam(createParam<RightBtn>(Vec(134.0f, 290.0f), module, DTROY::RIGHT_PARAM));
	addParam(createParam<UpBtn>(Vec(119.0f, 282.0f), module, DTROY::UP_PARAM));
	addParam(createParam<DownBtn>(Vec(119.0f, 297.0f), module, DTROY::DOWN_PARAM));

	for (int i = 0; i < 8; i++) {
		pitchParams[i] = createParam<BidooBlueKnob>(Vec(portX1[i]-3.0f, 36.0f), module, DTROY::TRIG_PITCH_PARAM + i);
		addParam(pitchParams[i]);
		pulseParams[i] = createParam<BidooSlidePotLong>(Vec(portX1[i]+2.0f, 87.0f), module, DTROY::TRIG_COUNT_PARAM + i);
		addParam(pulseParams[i]);
		typeParams[i] = createParam<BidooSlidePotShort>(Vec(portX1[i]+2.0f, 204.0f), module, DTROY::TRIG_TYPE_PARAM + i);
		addParam(typeParams[i]);
		slideParams[i] = createParam<LEDButton>(Vec(portX1[i]+2.0f, 297.0f), module, DTROY::TRIG_SLIDE_PARAM + i);
		addParam(slideParams[i]);
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[i]+8.0f, 303.0f), module, DTROY::SLIDES_LIGHTS + i));
		skipParams[i] = createParam<LEDButton>(Vec(portX1[i]+2.0f, 321.0f), module, DTROY::TRIG_SKIP_PARAM + i);
		addParam(skipParams[i]);
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[i]+8.0f, 327.0f), module, DTROY::SKIPS_LIGHTS + i));
		addOutput(createOutput<TinyPJ301MPort>(Vec(portX1[i]+4.0f, 344.0f), module, DTROY::STEP_OUTPUT + i));
	}

	addInput(createInput<PJ301MPort>(Vec(portX0[0], 330.0f), module, DTROY::EXTGATE1_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 330.0f), module, DTROY::EXTGATE2_INPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[2]-1, 330.0f), module, DTROY::GATE_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[3]-1, 330.0f), module, DTROY::PITCH_OUTPUT));
}

void DTROYWidget::appendContextMenu(ui::Menu *menu) {
	BidooWidget::appendContextMenu(menu);
	DTROY *m = dynamic_cast<DTROY*>(this->module);
	if (!m)
		return;
	menu->addChild(new MenuSeparator());
	menu->addChild(createMenuItem("Randomize pitches", "", [=]() { m->randomizePitch(); }));
	menu->addChild(createMenuItem("Randomize gates", "", [=]() { m->randomizeGates(); }));
	menu->addChild(createMenuItem("Randomize slides & skips", "", [=]() { m->randomizeSlidesSkips(); }));
	menu->addChild(createBoolPtrMenuItem("Stage outputs on every pulse", "", &m->stepOutputsMode));
}

Model *modelDTROY = createModel<DTROY, DTROYWidget>("dTrOY");
