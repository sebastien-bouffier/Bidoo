#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

// One step of a sequencer. A start trigger plays the step: its gates (with
// ratchets) and its CV go to the bridge outputs, and the end-of-step output
// starts the next μ. Idle, a μ passes the bridges through, so a chain of μ
// ends on one gate and one CV. Steps are counted in samples, with the rounding
// carried to the next step, so a chain keeps the tempo exactly. The BPM is a
// 1V/oct CV (0 V = 120 BPM) passed from μ to μ; the first one can follow a clock.
struct MU : BidooModule {
	enum ParamIds {
		BPM_PARAM,
		BPMFINE_PARAM,
		STEPLENGTH_PARAM,
		STEPLENGTHFINE_PARAM,
		NOTELENGTH_PARAM,
		STEPPROBA_PARAM,
		ALTEOSTEPPROBA_PARAM,
		NUMTRIGS_PARAM,
		DISTTRIGS_PARAM,
		CV_PARAM,
		START_PARAM,
		CVSTACK_PARAM,
		TRIGSTACK_PARAM,
		MUTE_PARAM,
		OFFSET_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		ACTIVATE_INPUT,
		INHIBIT_INPUT,
		GATEBRIDGE_INPUT,
		CVBRIDGE_INPUT,
		BPM_INPUT,
		NOTELENGTH_INPUT,
		CV_INPUT,
		NUMTRIGS_INPUT,
		DISTTRIGS_INPUT,
		OFFSET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		EOSTEP_OUTPUT,
		ALTEOSTEP_OUTPUT,
		GATEBRIDGE_OUTPUT,
		CVBRIDGE_OUTPUT,
		BPM_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NORMEOS_LIGHT,
		ALTEOS_LIGHT,
		CVSTACK_LIGHT,
		TRIGSTACK_LIGHT,
		START_LIGHT,
		MUTE_LIGHT,
		GATE_LIGHT,
		NUM_LIGHTS = GATE_LIGHT + 3
	};

	static constexpr float BPM_MIN = 1.f;
	static constexpr float BPM_MAX = 1000.f;
	static constexpr int CLOCK_WINDOW = 96;

	dsp::PulseGenerator eosPulse;
	dsp::PulseGenerator startLightPulse;
	dsp::SchmittTrigger activateTrigger;
	dsp::SchmittTrigger inhibitTrigger;
	dsp::SchmittTrigger cvModeTrigger;
	dsp::SchmittTrigger trigModeTrigger;
	dsp::SchmittTrigger muteTrigger;
	dsp::SchmittTrigger startTrigger;
	dsp::SchmittTrigger clockTrigger;

	bool isActive = false;
	long count = 0;          // samples since the step started
	long stepTicks = 0;      // length of the playing step, in samples
	double carry = 0.0;      // rounding carried to the next step
	bool play = false;
	bool alt = false;
	bool cvStack = false;
	bool trigStack = false;
	bool mute = false;

	// Clock follow: the BPM input takes a clock and its tempo is measured.
	bool clockFollow = false;
	int clockPpqn = 1;
	bool startArmed = false;     // the start button waits for the next beat
	long sinceClock = -1;
	long ticks = 0;              // clock ticks since the clock started, a beat every clockPpqn
	long intervals[CLOCK_WINDOW] = {};
	int intervalCount = 0, intervalPos = 0;
	double intervalSum = 0.0;
	double clockBpm = 0.0;

	double tempo = 120.0;
	float bpm = 120.f;
	float displayLength = 100.f;
	float displayNoteLength = 0.f;
	float displayStepProba = 100.f;
	float displayAltProba = 0.f;
	float displayNumTrigs = 1.f;
	float displayDistTrigs = 100.f;
	float displayOffset = 0.f;
	float displayCV = 0.f;

	MU() : BidooModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(BPM_PARAM, 1.f, 800.f, 120.f, "BPM");
		paramQuantities[BPM_PARAM]->snapEnabled = true;
		configParam(BPMFINE_PARAM, 0.f, 0.99f, 0.f, "BPM fine");
		configParam(STEPLENGTH_PARAM, 0.f, 1600.f, 100.f, "Step length", "% of a beat");
		paramQuantities[STEPLENGTH_PARAM]->snapEnabled = true;
		configParam(STEPLENGTHFINE_PARAM, -0.5f, 0.5f, 0.f, "Step length fine", "% of a beat");
		configParam(NOTELENGTH_PARAM, 0.f, 1.f, 0.f, "Gate length (at least 1 ms)", "% of the step", 0.f, 100.f);
		configParam(STEPPROBA_PARAM, 0.f, 1.f, 1.f, "Step probability", "%", 0.f, 100.f);
		configParam(ALTEOSTEPPROBA_PARAM, 0.f, 1.f, 0.f, "Alt end-of-step probability", "%", 0.f, 100.f);
		configParam(NUMTRIGS_PARAM, 1.f, 64.f, 1.f, "Ratchets");
		paramQuantities[NUMTRIGS_PARAM]->snapEnabled = true;
		configParam(DISTTRIGS_PARAM, 0.f, 1.f, 1.f, "Ratchet spacing", "% of the step", 0.f, 100.f);
		configParam(CV_PARAM, -5.f, 5.f, 0.f, "CV", " V");
		configButton(START_PARAM, "Start");
		configButton(CVSTACK_PARAM, "CV stack (adds the incoming CV)");
		configButton(TRIGSTACK_PARAM, "Gate stack (lets the incoming gates through)");
		configButton(MUTE_PARAM, "Mute");
		configParam(OFFSET_PARAM, 0.f, 1.f, 0.f, "Gate offset", "% of the step", 0.f, 100.f);
		configInput(ACTIVATE_INPUT, "Step start");
		configInput(INHIBIT_INPUT, "Inhibit (stops the step)");
		configInput(GATEBRIDGE_INPUT, "Gate bridge");
		configInput(CVBRIDGE_INPUT, "CV bridge");
		configInput(BPM_INPUT, "BPM");
		configInput(NOTELENGTH_INPUT, "Gate length CV (10 V = 100%)");
		configInput(CV_INPUT, "CV");
		configInput(NUMTRIGS_INPUT, "Ratchets CV (10 V = 64)");
		configInput(DISTTRIGS_INPUT, "Ratchet spacing CV (10 V = 100%)");
		configInput(OFFSET_INPUT, "Gate offset CV (10 V = 100%)");
		configOutput(EOSTEP_OUTPUT, "End of step");
		configOutput(ALTEOSTEP_OUTPUT, "Alt end of step");
		configOutput(GATEBRIDGE_OUTPUT, "Gate bridge");
		configOutput(CVBRIDGE_OUTPUT, "CV bridge");
		configOutput(BPM_OUTPUT, "BPM (1V/oct, 0 V = 120 BPM)");
		configBypass(GATEBRIDGE_INPUT, GATEBRIDGE_OUTPUT);
		configBypass(CVBRIDGE_INPUT, CVBRIDGE_OUTPUT);
		updateBpmInputName();
	}

	void updateBpmInputName() {
		inputInfos[BPM_INPUT]->name = clockFollow ? rack::string::f("Clock to follow (%d PPQN)", clockPpqn) : "BPM (1V/oct, 0 V = 120 BPM)";
	}

	void resetClock() {
		sinceClock = -1;
		ticks = 0;
		intervalCount = intervalPos = 0;
		intervalSum = 0.0;
		clockBpm = 0.0;
	}

	void onReset() override {
		isActive = false;
		cvStack = trigStack = mute = false;
		startArmed = false;
		carry = 0.0;
		resetClock();
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "cvStack", json_boolean(cvStack));
		json_object_set_new(rootJ, "trigStack", json_boolean(trigStack));
		json_object_set_new(rootJ, "mute", json_boolean(mute));
		json_object_set_new(rootJ, "clockFollow", json_boolean(clockFollow));
		json_object_set_new(rootJ, "clockPpqn", json_integer(clockPpqn));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		if (json_t *j = json_object_get(rootJ, "cvStack")) cvStack = json_is_true(j);
		if (json_t *j = json_object_get(rootJ, "trigStack")) trigStack = json_is_true(j);
		if (json_t *j = json_object_get(rootJ, "mute")) mute = json_is_true(j);
		if (json_t *j = json_object_get(rootJ, "clockFollow")) clockFollow = json_is_true(j);
		if (json_t *j = json_object_get(rootJ, "clockPpqn")) clockPpqn = clamp((int)json_integer_value(j), 1, 96);
		resetClock();
		updateBpmInputName();
	}

	void setClockFollow(bool on, int ppqn) {
		clockFollow = on;
		clockPpqn = ppqn;
		startArmed = false;
		resetClock();
		updateBpmInputName();
	}

	// Measures the clock tempo as the average interval over the last beats,
	// starting the average again when the tempo jumps by more than 1%. A clock
	// that stops for 4 intervals starts again on a beat. Returns true on a beat.
	bool processClock(float v) {
		if (sinceClock >= 0)
			sinceClock++;
		if (intervalCount > 0 && sinceClock > 4.0 * intervalSum / intervalCount)
			resetClock();
		if (!clockTrigger.process(v, 0.1f, 1.f))
			return false;
		bool beat = sinceClock < 0 || ++ticks % clockPpqn == 0;
		if (sinceClock > 0) {
			if (intervalCount > 0 && std::fabs(sinceClock - intervalSum / intervalCount) > 0.01 * intervalSum / intervalCount) {
				intervalCount = intervalPos = 0;
				intervalSum = 0.0;
			}
			int window = std::min(CLOCK_WINDOW, std::max(4 * clockPpqn, 8));
			if (intervalCount == window) {
				intervalSum -= intervals[intervalPos];
				intervalCount--;
			}
			intervals[intervalPos] = sinceClock;
			intervalSum += sinceClock;
			intervalCount++;
			intervalPos = (intervalPos + 1) % window;
			clockBpm = 60.0 * intervalCount / (intervalSum * clockPpqn);  // per sample, scaled below
		}
		sinceClock = 0;
		return beat;
	}

	// exp2 / log2 only when the tempo CV or the tempo changes; lights at control rate.
	float bpmVolts = NAN;
	double bpmFromVolts = 120.0;
	double bpmOutTempo = NAN;
	float bpmOutVolts = 0.f;
	static constexpr int CONTROL_DIV = 16;
	int controlCounter = 0;

	void process(const ProcessArgs &args) override;
};

void MU::process(const ProcessArgs &args) {
	float dt = args.sampleTime;

	// Tempo.
	bool clockBeat = false;
	double knobBpm = std::round(params[BPM_PARAM].getValue()) + std::round(100.f * params[BPMFINE_PARAM].getValue()) / 100.0;
	if (clockFollow && inputs[BPM_INPUT].isConnected()) {
		clockBeat = processClock(inputs[BPM_INPUT].getVoltage());
		tempo = intervalCount > 0 ? clamp(clockBpm * args.sampleRate, (double)BPM_MIN, (double)BPM_MAX) : knobBpm;
	}
	else {
		if (sinceClock >= 0)
			resetClock();
		// In double: the tempo passed down a chain keeps its precision.
		if (!clockFollow && inputs[BPM_INPUT].isConnected()) {
			float v = inputs[BPM_INPUT].getVoltage();
			if (v != bpmVolts) {
				bpmVolts = v;
				bpmFromVolts = clamp(120.0 * std::exp2(clamp((double)v, -7.0, 3.1)), (double)BPM_MIN, (double)BPM_MAX);
			}
			tempo = bpmFromVolts;
		}
		else
			tempo = knobBpm;
	}
	bpm = tempo;
	displayLength = std::round(params[STEPLENGTH_PARAM].getValue()) + std::round(params[STEPLENGTHFINE_PARAM].getValue() * 100.f) * 0.01f;

	// Start: the button (on the next clock edge when following a clock) or the start input.
	bool start = false;
	if (startTrigger.process(params[START_PARAM].getValue())) {
		startLightPulse.trigger(0.1f);
		if (clockFollow && inputs[BPM_INPUT].isConnected())
			startArmed = true;
		else
			start = true;
	}
	if (startArmed && clockBeat) {
		startArmed = false;
		start = true;
	}
	if (activateTrigger.process(inputs[ACTIVATE_INPUT].getVoltage(), 0.1f, 1.f))
		start = true;

	if (start && !isActive) {
		isActive = true;
		count = 0;
		double exact = displayLength / 100.0 * args.sampleRate * 60.0 / tempo + carry;
		stepTicks = std::max((long)std::round(exact), 1L);
		carry = clamp(exact - stepTicks, -1.0, 1.0);
		play = random::uniform() < params[STEPPROBA_PARAM].getValue();
		alt = random::uniform() < params[ALTEOSTEPPROBA_PARAM].getValue();
	}

	if (cvModeTrigger.process(params[CVSTACK_PARAM].getValue()))
		cvStack = !cvStack;
	if (trigModeTrigger.process(params[TRIGSTACK_PARAM].getValue()))
		trigStack = !trigStack;
	if (muteTrigger.process(params[MUTE_PARAM].getValue()))
		mute = !mute;

	float noteLength = clamp(params[NOTELENGTH_PARAM].getValue() + inputs[NOTELENGTH_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
	int numTrigs = (int)std::round(clamp(params[NUMTRIGS_PARAM].getValue() + inputs[NUMTRIGS_INPUT].getVoltage() * 6.4f, 1.f, 64.f));
	float dist = clamp(params[DISTTRIGS_PARAM].getValue() + inputs[DISTTRIGS_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
	float offset = clamp(params[OFFSET_PARAM].getValue() + inputs[OFFSET_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
	float cv = clamp(params[CV_PARAM].getValue() + inputs[CV_INPUT].getVoltage(), -10.f, 10.f);
	displayNoteLength = noteLength * 100.f;
	displayStepProba = params[STEPPROBA_PARAM].getValue() * 100.f;
	displayAltProba = params[ALTEOSTEPPROBA_PARAM].getValue() * 100.f;
	displayNumTrigs = numTrigs;
	displayDistTrigs = dist * 100.f;
	displayOffset = offset * 100.f;
	displayCV = cv;

	if (inhibitTrigger.process(inputs[INHIBIT_INPUT].getVoltage(), 0.1f, 1.f))
		isActive = false;

	bool ownGate = false;
	if (isActive) {
		// The step's samples are 0 .. stepTicks - 1 and the next step starts one
		// sample after the end-of-step, through the cable. The next μ passes
		// this one's second-to-last sample, so the gate is low from there: the
		// next step's gate is a new edge. Ratchets keep a low sample between them.
		long last = stepTicks - 1;
		long gateEnd = std::max(last - 1, 1L);
		long offsetTicks = (long)(offset * stepTicks);
		long distTicks = (long)(dist * stepTicks);
		long gateTicks = std::max((long)std::round(noteLength * stepTicks), (long)std::round(1e-3f * args.sampleRate));
		if (play && count >= offsetTicks) {
			long k = distTicks > 0 ? (count - offsetTicks) / distTicks : 0;
			long trigStart = offsetTicks + k * distTicks;
			long len = gateTicks;
			if (distTicks > 0 && k < numTrigs - 1)
				len = std::min(len, std::max(distTicks - 1, 1L));
			ownGate = k < numTrigs && count < trigStart + len && count < gateEnd;
		}
		bool upstream = trigStack && !mute && count < gateEnd && inputs[GATEBRIDGE_INPUT].getVoltage() >= 1.f;
		outputs[GATEBRIDGE_OUTPUT].setVoltage(((ownGate && !mute) || upstream) ? 10.f : 0.f);
		outputs[CVBRIDGE_OUTPUT].setVoltage(clamp(cv + (cvStack ? inputs[CVBRIDGE_INPUT].getVoltage() : 0.f), -10.f, 10.f));
		if (count >= last) {
			isActive = false;
			eosPulse.trigger(1e-3f);
		}
		count++;
	}
	else {
		outputs[GATEBRIDGE_OUTPUT].setVoltage(clamp(inputs[GATEBRIDGE_INPUT].getVoltage(), 0.f, 10.f));
		// The chain's first μ shows its own CV while idle, so the end of the
		// chain never drops to 0 V where the loop starts again.
		outputs[CVBRIDGE_OUTPUT].setVoltage(inputs[CVBRIDGE_INPUT].isConnected() ? inputs[CVBRIDGE_INPUT].getVoltage() : cv);
	}

	bool pulse = eosPulse.process(dt);
	outputs[EOSTEP_OUTPUT].setVoltage((pulse && !alt) ? 10.f : 0.f);
	outputs[ALTEOSTEP_OUTPUT].setVoltage((pulse && alt) ? 10.f : 0.f);
	if (tempo != bpmOutTempo) {
		bpmOutTempo = tempo;
		bpmOutVolts = (float)std::log2(tempo / 120.0);
	}
	outputs[BPM_OUTPUT].setVoltage(bpmOutVolts);

	// Lights, at control rate (the pulses they show last 1 ms or more).
	if (--controlCounter >= 0)
		return;
	controlCounter = CONTROL_DIV - 1;
	const float lightDt = dt * CONTROL_DIV;
	const float lambda = 13.333333f * lightDt;
	bool gateLit = ownGate && !mute;
	float l = lights[NORMEOS_LIGHT].getBrightness();
	lights[NORMEOS_LIGHT].setBrightness((pulse && !alt) ? 1.f : l - l * lambda);
	l = lights[ALTEOS_LIGHT].getBrightness();
	lights[ALTEOS_LIGHT].setBrightness((pulse && alt) ? 1.f : l - l * lambda);
	lights[START_LIGHT].setBrightness((startArmed || startLightPulse.process(lightDt)) ? 1.f : 0.f);
	l = lights[GATE_LIGHT].getBrightness();
	lights[GATE_LIGHT].setBrightness(gateLit ? 1.f : l - 4.f * l * lambda);
	lights[GATE_LIGHT + 1].setBrightness(0.f);
	l = lights[GATE_LIGHT + 2].getBrightness();
	lights[GATE_LIGHT + 2].setBrightness((isActive && !gateLit) ? 1.f : l - 4.f * l * lambda);
	lights[MUTE_LIGHT].setBrightness(mute ? 1.f : 0.f);
	lights[CVSTACK_LIGHT].setBrightness(cvStack ? 1.f : 0.f);
	lights[TRIGSTACK_LIGHT].setBrightness(trigStack ? 1.f : 0.f);
}

struct LabelMICROWidget : TransparentWidget {
  float *value = NULL;
	const char *format = NULL;
	const char *header = "Have fun !!!";

  LabelMICROWidget() {

  };

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			nvgTextLetterSpacing(args.vg, -2.0f);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT);
			if (header) {
				nvgFontSize(args.vg, 12.0f);
				nvgText(args.vg, 0.0f, 0.0f, header, NULL);
			}
			if (value && format) {
				char display[128];
				snprintf(display, sizeof(display), format, *value);
				nvgFontSize(args.vg, 16.0f);
				nvgText(args.vg, 0.0f, 15.0f, display, NULL);
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct BidooBlueTrimpotWithDisplay : BidooBlueTrimpot {
	LabelMICROWidget *lblDisplay = NULL;
	float *valueForDisplay = NULL;
	const char *format = NULL;
	const char *header = NULL;

	void onHover(const event::Hover& e) override {
		if (lblDisplay && valueForDisplay && format) {
			lblDisplay->value = valueForDisplay;
			lblDisplay->format = format;
		}
		if (lblDisplay && header) lblDisplay->header = header;
		BidooBlueTrimpot::onHover(e);
	}
};

struct TinyPJ301MPortWithDisplay : TinyPJ301MPort {
	LabelMICROWidget *lblDisplay = NULL;
	float *valueForDisplay = NULL;
	const char *format = NULL;
	const char *header = NULL;

	void onHover(const event::Hover& e) override {
		if (lblDisplay && valueForDisplay && format) {
			lblDisplay->value = valueForDisplay;
			lblDisplay->format = format;
		}
		if (lblDisplay && header) lblDisplay->header = header;
		TinyPJ301MPort::onHover(e);
	}
};

struct MUWidget : BidooWidget {
	MUWidget(MU *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/MU.svg"));

		LabelMICROWidget *display = new LabelMICROWidget();
		display->box.pos = Vec(4,37);
		addChild(display);

		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(34, 15), module, MU::GATE_LIGHT));

		addParam(createParam<LEDButton>(Vec(5, 5), module, MU::START_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(11, 11), module, MU::START_LIGHT));

		addParam(createParam<LEDButton>(Vec(52, 5), module, MU::MUTE_PARAM));
		addChild(createLight<SmallLight<RedLight>>(Vec(58, 11), module, MU::MUTE_LIGHT));

		static const float portX0[2] = {14.0f, 41.0f};
		static const float portY0[5] = {60.0f, 83.0f, 106.0f, 145.0f, 166.0f};

		static const float portX1[2] = {15.0f, 45.0f};
		static const float portY1[8] = {191.0f, 215.0f, 239.0f, 263.0f, 287.0f, 311.0f, 335.0f, 359.0f};


		BidooBlueTrimpotWithDisplay* bpm = createParam<BidooBlueTrimpotWithDisplay>(Vec(portX0[0], portY0[0]), module, MU::BPM_PARAM);
		bpm->lblDisplay = display;
		bpm->valueForDisplay = module ? &module->bpm : NULL;
		bpm->format = "%2.2f";
		bpm->header = "BPM";
		addParam(bpm);
		BidooBlueTrimpotWithDisplay* bpmFine =  createParam<BidooBlueTrimpotWithDisplay>(Vec(portX0[0], portY0[1]), module, MU::BPMFINE_PARAM);
		bpmFine->lblDisplay = display;
		bpmFine->valueForDisplay = module ? &module->bpm : NULL;
		bpmFine->format = "%2.2f";
		bpmFine->header = "BPM";
		addParam(bpmFine);

		BidooBlueTrimpotWithDisplay* stepLength =  createParam<BidooBlueTrimpotWithDisplay>(Vec(portX0[1], portY0[0]), module, MU::STEPLENGTH_PARAM);
		stepLength->lblDisplay = display;
		stepLength->valueForDisplay = module ? &module->displayLength : NULL;
		stepLength->format = "%2.2f %%";
		stepLength->header = "Step len.";
		addParam(stepLength);
		BidooBlueTrimpotWithDisplay* stepLengthFine =  createParam<BidooBlueTrimpotWithDisplay>(Vec(portX0[1], portY0[1]), module, MU::STEPLENGTHFINE_PARAM);
		stepLengthFine->lblDisplay = display;
		stepLengthFine->valueForDisplay = module ? &module->displayLength : NULL;
		stepLengthFine->format = "%2.2f %%";
		stepLengthFine->header = "Step len.";
		addParam(stepLengthFine);

		BidooBlueTrimpotWithDisplay* noteLength =  createParam<BidooBlueTrimpotWithDisplay>(Vec(portX0[0], portY0[2]), module, MU::NOTELENGTH_PARAM);
		noteLength->lblDisplay = display;
		noteLength->valueForDisplay = module ? &module->displayNoteLength : NULL;
		noteLength->format = "%2.2f %%";
		noteLength->header = "Trigs len.";
		addParam(noteLength);

		BidooBlueTrimpotWithDisplay* offset =  createParam<BidooBlueTrimpotWithDisplay>(Vec(portX0[1], portY0[2]), module, MU::OFFSET_PARAM);
		offset->lblDisplay = display;
		offset->valueForDisplay = module ? &module->displayOffset : NULL;
		offset->format = "%2.2f %%";
		offset->header = "Trigs offset";
		addParam(offset);

		BidooBlueTrimpotWithDisplay* cv =  createParam<BidooBlueTrimpotWithDisplay>(Vec(portX0[0]+14, portY0[2]+19), module, MU::CV_PARAM);
		cv->lblDisplay = display;
		cv->valueForDisplay = module ? &module->displayCV : NULL;
		cv->format = "%2.2f V";
		cv->header = "CV";
		addParam(cv);

		BidooBlueTrimpotWithDisplay* stepProb =  createParam<BidooBlueTrimpotWithDisplay>(Vec(portX0[0], portY0[3]), module, MU::STEPPROBA_PARAM);
		stepProb->lblDisplay = display;
		stepProb->valueForDisplay = module ? &module->displayStepProba : NULL;
		stepProb->format = "%2.2f %%";
		stepProb->header = "Step prob.";
		addParam(stepProb);

		BidooBlueTrimpotWithDisplay* altOutProb =  createParam<BidooBlueTrimpotWithDisplay>(Vec(portX0[1], portY0[3]), module, MU::ALTEOSTEPPROBA_PARAM);
		altOutProb->lblDisplay = display;
		altOutProb->valueForDisplay = module ? &module->displayAltProba : NULL;
		altOutProb->format = "%2.2f %%";
		altOutProb->header = "Alt out prob.";
		addParam(altOutProb);

		BidooBlueTrimpotWithDisplay* numTrig =  createParam<BidooBlueTrimpotWithDisplay>(Vec(portX0[0], portY0[4]), module, MU::NUMTRIGS_PARAM);
		numTrig->lblDisplay = display;
		numTrig->valueForDisplay = module ? &module->displayNumTrigs : NULL;
		numTrig->format = "%2.0f";
		numTrig->header = "Trigs count";
		addParam(numTrig);

		BidooBlueTrimpotWithDisplay* distTrig =  createParam<BidooBlueTrimpotWithDisplay>(Vec(portX0[1], portY0[4]), module, MU::DISTTRIGS_PARAM);
		distTrig->lblDisplay = display;
		distTrig->valueForDisplay = module ? &module->displayDistTrigs : NULL;
		distTrig->format = "%2.2f %%";
		distTrig->header = "Trigs Dist.";
		addParam(distTrig);

		TinyPJ301MPortWithDisplay* bpmIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[0]), module, MU::BPM_INPUT);
		bpmIn->lblDisplay = display;
		bpmIn->valueForDisplay = module ? &module->inputs[MU::BPM_INPUT].value : NULL;
		bpmIn->format = "%2.2f V";
		bpmIn->header = "BPM";
		addInput(bpmIn);
		TinyPJ301MPortWithDisplay* bpmOut = createOutput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[0]), module, MU::BPM_OUTPUT);
		bpmOut->lblDisplay = display;
		bpmOut->valueForDisplay = module ? &module->outputs[MU::BPM_OUTPUT].value : NULL;
		bpmOut->format = "%2.2f V";
		bpmOut->header = "BPM";
		addOutput(bpmOut);

		TinyPJ301MPortWithDisplay* activateIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[1]), module, MU::ACTIVATE_INPUT);
		activateIn->lblDisplay = display;
		activateIn->valueForDisplay = module ? &module->inputs[MU::ACTIVATE_INPUT].value : NULL;
		activateIn->format = "%2.2f V";
		activateIn->header = "Step start";
		addInput(activateIn);
		TinyPJ301MPortWithDisplay* activateOUT = createOutput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[1]), module, MU::EOSTEP_OUTPUT);
		activateOUT->lblDisplay = display;
		activateOUT->valueForDisplay = module ? &module->outputs[MU::EOSTEP_OUTPUT].value : NULL;
		activateOUT->format = "%2.2f V";
		activateOUT->header = "Step end";
		addOutput(activateOUT);

		addChild(createLight<SmallLight<GreenLight>>(Vec(portX1[1]-11, portY1[1]+5), module, MU::NORMEOS_LIGHT));

		TinyPJ301MPortWithDisplay* activateAltOUT = createOutput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[2]), module, MU::ALTEOSTEP_OUTPUT);
		activateAltOUT->lblDisplay = display;
		activateAltOUT->valueForDisplay = module ? &module->outputs[MU::ALTEOSTEP_OUTPUT].value : NULL;
		activateAltOUT->format = "%2.2f V";
		activateAltOUT->header = "Alt step end";
		addOutput(activateAltOUT);

		addChild(createLight<SmallLight<GreenLight>>(Vec(portX1[1]-11, portY1[2]+5), module, MU::ALTEOS_LIGHT));

		TinyPJ301MPortWithDisplay* inhibitIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[2]), module, MU::INHIBIT_INPUT);
		inhibitIn->lblDisplay = display;
		inhibitIn->valueForDisplay = module ? &module->inputs[MU::INHIBIT_INPUT].value : NULL;
		inhibitIn->format = "%2.2f V";
		inhibitIn->header = "Inhibit step";
		addInput(inhibitIn);

		TinyPJ301MPortWithDisplay* noteLengthIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[3]), module, MU::NOTELENGTH_INPUT);
		noteLengthIn->lblDisplay = display;
		noteLengthIn->valueForDisplay = module ? &module->inputs[MU::NOTELENGTH_INPUT].value : NULL;
		noteLengthIn->format = "%2.2f V";
		noteLengthIn->header = "Trigs len.";
		addInput(noteLengthIn);

		TinyPJ301MPortWithDisplay* offsetIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[3]), module, MU::OFFSET_INPUT);
		offsetIn->lblDisplay = display;
		offsetIn->valueForDisplay = module ? &module->inputs[MU::OFFSET_INPUT].value : NULL;
		offsetIn->format = "%2.2f V";
		offsetIn->header = "Offset mod";
		addInput(offsetIn);

		TinyPJ301MPortWithDisplay* cvIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0]+14, portY1[4]), module, MU::CV_INPUT);
		cvIn->lblDisplay = display;
		cvIn->valueForDisplay = module ? &module->inputs[MU::CV_INPUT].value : NULL;
		cvIn->format = "%2.2f V";
		cvIn->header = "CV mod";
		addInput(cvIn);

		TinyPJ301MPortWithDisplay* numTrigsIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[5]), module, MU::NUMTRIGS_INPUT);
		numTrigsIn->lblDisplay = display;
		numTrigsIn->valueForDisplay = module ? &module->inputs[MU::NUMTRIGS_INPUT].value : NULL;
		numTrigsIn->format = "%2.2f V";
		numTrigsIn->header = "Trigs count";
		addInput(numTrigsIn);

		TinyPJ301MPortWithDisplay* distTrigsIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[5]), module, MU::DISTTRIGS_INPUT);
		distTrigsIn->lblDisplay = display;
		distTrigsIn->valueForDisplay = module ? &module->inputs[MU::DISTTRIGS_INPUT].value : NULL;
		distTrigsIn->format = "%2.2f V";
		distTrigsIn->header = "Trigs Dist.";
		addInput(distTrigsIn);

		TinyPJ301MPortWithDisplay* gateBridgeIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[6]), module, MU::GATEBRIDGE_INPUT);
		gateBridgeIn->lblDisplay = display;
		gateBridgeIn->valueForDisplay = module ? &module->inputs[MU::GATEBRIDGE_INPUT].value : NULL;
		gateBridgeIn->format = "%2.2f V";
		gateBridgeIn->header = "Gate";
		addInput(gateBridgeIn);

		TinyPJ301MPortWithDisplay* gateBridgeOut = createOutput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[6]), module, MU::GATEBRIDGE_OUTPUT);
		gateBridgeOut->lblDisplay = display;
		gateBridgeOut->valueForDisplay = module ? &module->outputs[MU::GATEBRIDGE_OUTPUT].value : NULL;
		gateBridgeOut->format = "%2.2f V";
		gateBridgeOut->header = "Gate";
		addOutput(gateBridgeOut);

		addParam(createParam<MiniLEDButton>(Vec(portX1[1]-11, portY1[6]+5), module, MU::TRIGSTACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[1]-11, portY1[6]+5), module, MU::TRIGSTACK_LIGHT));

		TinyPJ301MPortWithDisplay* cvBridgeIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[7]), module, MU::CVBRIDGE_INPUT);
		cvBridgeIn->lblDisplay = display;
		cvBridgeIn->valueForDisplay = module ? &module->inputs[MU::CVBRIDGE_INPUT].value : NULL;
		cvBridgeIn->format = "%2.2f V";
		cvBridgeIn->header = "CV";
		addInput(cvBridgeIn);

		TinyPJ301MPortWithDisplay* cvBridgeOut = createOutput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[7]), module, MU::CVBRIDGE_OUTPUT);
		cvBridgeOut->lblDisplay = display;
		cvBridgeOut->valueForDisplay = module ? &module->outputs[MU::CVBRIDGE_OUTPUT].value : NULL;
		cvBridgeOut->format = "%2.2f V";
		cvBridgeOut->header = "CV";
		addOutput(cvBridgeOut);

		addParam(createParam<MiniLEDButton>(Vec(portX1[1]-11, portY1[7]+5), module, MU::CVSTACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[1]-11, portY1[7]+5), module, MU::CVSTACK_LIGHT));
  }

	void appendContextMenu(Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		MU *m = dynamic_cast<MU*>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator());
		menu->addChild(createMenuLabel("BPM input"));
		menu->addChild(createCheckMenuItem("1V/oct CV (0 V = 120 BPM)", "", [=]() { return !m->clockFollow; }, [=]() { m->setClockFollow(false, m->clockPpqn); }));
		static const int ppqns[] = {1, 2, 4, 8, 12, 24, 48, 96};
		for (int p : ppqns)
			menu->addChild(createCheckMenuItem(rack::string::f("Clock to follow, %d PPQN", p), "", [=]() { return m->clockFollow && m->clockPpqn == p; }, [=]() { m->setClockFollow(true, p); }));
	}
};

Model *modelMU = createModel<MU, MUWidget>("MU");
