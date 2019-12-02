#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

struct MU : Module {
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

	dsp::PulseGenerator eosPulse;
	dsp::SchmittTrigger activateTrigger;
	dsp::SchmittTrigger inhibateTrigger;
	dsp::SchmittTrigger cvModeTrigger;
	dsp::SchmittTrigger trigModeTrigger;
	dsp::SchmittTrigger muteTrigger;
	dsp::SchmittTrigger startTrigger;
	bool isActive = false;
	int ticks = 0;
	int initTicks = 0;
	int gateTicks = 0;
	float bpm = 0.1f;
	int numTrigs = 1;
	int distRetrig = 0;
	bool play = false;
	bool alt = false;
	int count = 0;
	float displayLength = 0.0f;
	float displayNoteLength = 0.0f;
	float displayStepProba = 0.0f;
	float displayAltProba = 0.0f;
	float displayNumTrigs = 0.0f;
	float displayDistTrigs = 0.0f;
	float displayCV = 0.0f;
	float displayOffset = 0.0f;
	bool cvStack = false;
	bool trigStack = false;
	bool mute = false;

	MU() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(START_PARAM, 0.f, 10.f, 0.f);
		configParam(MUTE_PARAM, 0.f, 10.f, 0.f);
		configParam(BPM_PARAM, 1.f, 800.f, 117.f);
		configParam(BPMFINE_PARAM, 0.f, 0.99f, 0.f);
		configParam(STEPLENGTH_PARAM, 0.f, 1600.f, 100.f);
		configParam(STEPLENGTHFINE_PARAM, -.5f, .5f, 0.f);
		configParam(NOTELENGTH_PARAM, 0.f, 1.f, 1.f);
		configParam(OFFSET_PARAM, 0.f, 1.f, 0.f);
		configParam(CV_PARAM, 0.f, 10.f, 0.f);
		configParam(STEPPROBA_PARAM, 0.f, 1.f, 1.f);
		configParam(ALTEOSTEPPROBA_PARAM, 0.f, 1.f, 0.f);
		configParam(NUMTRIGS_PARAM, 1.f, 64.f, 1.f);
		configParam(DISTTRIGS_PARAM, 0.f, 1.f, 1.f);
		configParam(TRIGSTACK_PARAM, 0.f, 10.f, 0.f);
		configParam(CVSTACK_PARAM, 0.f, 10.f, 0.f);

	}

	void process(const ProcessArgs &args) override {
		const float invLightLambda = 13.333333333333333333333f;
		float invESR = 1 / args.sampleRate;

		bpm = inputs[BPM_INPUT].active ? rescale(clamp(inputs[BPM_INPUT].getVoltage(), 0.0f, 10.0f), 0.0f, 10.0f, 1.0f, 800.99f) : (round(params[BPM_PARAM].getValue()) + round(100 * params[BPMFINE_PARAM].getValue()) / 100);
		displayLength = round(params[STEPLENGTH_PARAM].getValue()) + round(params[STEPLENGTHFINE_PARAM].getValue() * 100) * 0.01f;

		bool wasActive = isActive;
		if (startTrigger.process(params[START_PARAM].getValue())) {
			lights[START_LIGHT].setBrightness(10.0f);
			isActive = true;
		}
		if (activateTrigger.process(inputs[ACTIVATE_INPUT].getVoltage())) {
			isActive = true;
		}

		if (!wasActive && isActive) {
			initTicks = (displayLength / 100) * args.sampleTime * 60 / bpm;
			ticks = initTicks;
			count = 0;
			play = (random::uniform() + params[STEPPROBA_PARAM].getValue()) >= 1;		//randomize????
			alt = (random::uniform() + params[ALTEOSTEPPROBA_PARAM].getValue()) > 1;
			lights[GATE_LIGHT].setBrightness(0.0f);
			lights[GATE_LIGHT + 1].setBrightness(0.0f);
			lights[GATE_LIGHT + 2].setBrightness(10.0f);
		}

		if (cvModeTrigger.process(params[CVSTACK_PARAM].getValue())) {
			cvStack = !cvStack;
		}

		if (trigModeTrigger.process(params[TRIGSTACK_PARAM].getValue())) {
			trigStack = !trigStack;
		}

		if (muteTrigger.process(params[MUTE_PARAM].getValue())) {
			mute = !mute;
		}

		numTrigs = clamp(params[NUMTRIGS_PARAM].getValue() + rescale(clamp(inputs[NUMTRIGS_INPUT].getVoltage(), -10.0f, 10.0f), -10.0f, 10.0f, -64.0f, 64.0f), 0.0f, 64.0f);
		distRetrig = clamp(params[DISTTRIGS_PARAM].getValue() + rescale(clamp(inputs[DISTTRIGS_INPUT].getVoltage(), -10.0f, 10.0f), -10.0f, 10.0f, -1.0f, 1.0f), 0.0f, 1.0f) * initTicks;
		gateTicks = initTicks * clamp(params[NOTELENGTH_PARAM].getValue() + rescale(clamp(inputs[NOTELENGTH_INPUT].getVoltage(), -10.0f, 10.0f), -10.0f, 10.0f, -1.0f, 1.0f), 0.0f, 1.0f);
		displayNoteLength = clamp(params[NOTELENGTH_PARAM].getValue() + rescale(clamp(inputs[NOTELENGTH_INPUT].getVoltage(), -10.0f, 10.0f), -10.0f, 10.0f, -1.0f, 1.0f), 0.0f, 1.0f) * 100;
		displayStepProba = params[STEPPROBA_PARAM].getValue() * 100;
		displayAltProba = params[ALTEOSTEPPROBA_PARAM].getValue() * 100;
		displayDistTrigs = params[DISTTRIGS_PARAM].getValue() * 100;
		displayNumTrigs = numTrigs;
		displayOffset = clamp(params[OFFSET_PARAM].getValue() + rescale(clamp(inputs[OFFSET_INPUT].getVoltage(), 0.0f, 10.0f), 0.0f, 10.0f, 0.0f, 1.0f), 0.0f, 1.0f) * 100;

		if (inhibateTrigger.process(inputs[INHIBIT_INPUT].getVoltage())) {
			isActive = false;
		}

		if (isActive && (ticks >= 0)) {
			if (ticks <= 0) {
				isActive = false;
				eosPulse.trigger(10 / args.sampleTime);
				outputs[GATEBRIDGE_OUTPUT].setVoltage(0.0f);
				outputs[CVBRIDGE_OUTPUT].setVoltage(0.0f);
			} else {
				int offset = displayOffset * initTicks * 0.01f;
				int mult = ((distRetrig > 0) && (count > offset)) ? ((count - offset) / distRetrig) : 0;
				if (play && (mult < numTrigs) && (count >= (offset + mult * distRetrig)) && (count <= (offset + (mult * distRetrig) + gateTicks))) {
					outputs[GATEBRIDGE_OUTPUT].setVoltage(mute ? 0.0f : 10.0f);
					lights[GATE_LIGHT].setBrightness(mute ? 0.0f : 10.0f);
					lights[GATE_LIGHT + 1].setBrightness(0.0f);
					lights[GATE_LIGHT + 2].setBrightness(!mute ? 0.0f : 10.0f);
				} else {
					outputs[GATEBRIDGE_OUTPUT].setVoltage((trigStack && !mute && (inputs[GATEBRIDGE_INPUT].getVoltage() > 0.0f)) ? clamp(inputs[GATEBRIDGE_INPUT].getVoltage(), 0.0f, 10.0f) : 0.0f);
					lights[GATE_LIGHT].setBrightness(0.0f);
					lights[GATE_LIGHT + 1].setBrightness(0.0f);
					lights[GATE_LIGHT + 2].setBrightness(10.0f);
				}
				outputs[CVBRIDGE_OUTPUT].setVoltage(clamp(params[CV_PARAM].getValue() + clamp(inputs[CV_INPUT].getVoltage(), -10.0f, 10.0f) + (cvStack ? inputs[CVBRIDGE_INPUT].getVoltage() : 0.0f), 0.0f, 10.0f));
			}
			ticks--;
			count++;
		} else {
			outputs[GATEBRIDGE_OUTPUT].setVoltage(clamp(inputs[GATEBRIDGE_INPUT].getVoltage(), 0.0f, 10.0f));
			outputs[CVBRIDGE_OUTPUT].setVoltage(inputs[CVBRIDGE_INPUT].getVoltage());
		}

		bool pulse = eosPulse.process(1 / args.sampleRate);

		outputs[EOSTEP_OUTPUT].setVoltage(!alt ? (pulse ? 10.0f : 0.0f) : 0.0f);
		outputs[ALTEOSTEP_OUTPUT].setVoltage(alt ? (pulse ? 10.0f : 0.0f) : 0.0f);
		outputs[BPM_OUTPUT].setVoltage(rescale(bpm, 1.0f, 800.99f, 0.0f, 10.0f));

		lights[NORMEOS_LIGHT].setBrightness((!alt && pulse) ? 10.0f : (lights[NORMEOS_LIGHT].getBrightness() - lights[NORMEOS_LIGHT].getBrightness() * invLightLambda * invESR));
		lights[ALTEOS_LIGHT].setBrightness((alt && pulse) ? 10.0f : (lights[ALTEOS_LIGHT].getBrightness() - lights[ALTEOS_LIGHT].getBrightness() * invLightLambda * invESR));
		lights[START_LIGHT].setBrightness(lights[START_LIGHT].getBrightness() - lights[START_LIGHT].getBrightness() * invLightLambda * invESR);


  	if (outputs[GATEBRIDGE_OUTPUT].getVoltage() == 0.0f)
			lights[GATE_LIGHT].setBrightness(lights[GATE_LIGHT].getBrightness() - 4 * lights[GATE_LIGHT].getBrightness() * invLightLambda * invESR);
  	if (!isActive) lights[GATE_LIGHT+2].setBrightness(lights[GATE_LIGHT+2].getBrightness() - 4 * lights[GATE_LIGHT+2].getBrightness() * invLightLambda * invESR);
  	if (!isActive) lights[GATE_LIGHT].setBrightness(lights[GATE_LIGHT].getBrightness() - 4 * lights[GATE_LIGHT].getBrightness() * invLightLambda * invESR);

  	lights[MUTE_LIGHT].setBrightness(mute ? 10.0f : 0.0f);
  	lights[CVSTACK_LIGHT].setBrightness(cvStack ? 10.0f : 0.0f);
    lights[TRIGSTACK_LIGHT].setBrightness(trigStack ? 10.0f : 0.0f);
	}
};

struct LabelMICROWidget : TransparentWidget {
	float value = 0.f;
	const char *format = NULL;
	const char *header = "Have fun !!!";
	std::shared_ptr<Font> font;

	LabelMICROWidget() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	};

	void draw(const DrawArgs &args) override {
		nvgFillColor(args.vg, YELLOW_BIDOO);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT);
		if (header) {
			nvgFontSize(args.vg, 12.0f);
			nvgText(args.vg, 0.0f, 0.0f, header, NULL);
		}
		if (value && format) {
			char display[128];
			snprintf(display, sizeof(display), format, value);
			nvgFontSize(args.vg, 16.0f);
			nvgText(args.vg, 0.0f, 15.0f, display, NULL);
		}
	}
};

struct TinyPJ301MPortWithDisplay : TinyPJ301MPort {
	LabelMICROWidget *lblDisplay = NULL;
	const char *format = NULL;
	const char *header = NULL;
	Port *port = NULL;

	//;
	void onEnter(const event::Enter &e) override {
		if (lblDisplay && port && format) {
			lblDisplay->value = port->getVoltage();
			lblDisplay->format = format;
		}
		if (lblDisplay && header) lblDisplay->header = header;
		TinyPJ301MPort::onEnter(e);
	}
};

struct BidooBlueTrimpotWithDisplay : BidooBlueTrimpot {
	LabelMICROWidget *lblDisplay = NULL;
	Param *param = NULL;
	const char *format = NULL;
	const char *header = NULL;

	void onEnter(const event::Enter &e) override {
		if (lblDisplay && param && format) {
			lblDisplay->value = param->getValue();
			lblDisplay->format = format;
		}
		if (lblDisplay && header) lblDisplay->header = header;
		BidooBlueTrimpot::onEnter(e);
	}
};

struct BidooBlueTrimpotWithFloatDisplay : BidooBlueTrimpot {
	LabelMICROWidget *lblDisplay = NULL;
	float *valueForDisplay = NULL;
	const char *format = NULL;
	const char *header = NULL;

	void onEnter(const event::Enter &e) override {
		if (lblDisplay && valueForDisplay && format) {
			lblDisplay->value = *valueForDisplay;
			lblDisplay->format = format;
		}
		if (lblDisplay && header) lblDisplay->header = header;
		BidooBlueTrimpot::onEnter(e);
	}
};

struct MUWidget : ModuleWidget {
	MUWidget(MU *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MU.svg")));

		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(34, 15), module, MU::GATE_LIGHT));

		addParam(createParam<LEDButton>(Vec(5, 5), module, MU::START_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(11, 11), module, MU::START_LIGHT));

		addParam(createParam<LEDButton>(Vec(52, 5), module, MU::MUTE_PARAM));
		addChild(createLight<SmallLight<RedLight>>(Vec(58, 11), module, MU::MUTE_LIGHT));

		static const float portX0[2] = { 14.0f, 41.0f };
		static const float portY0[5] = { 60.0f, 83.0f, 106.0f, 145.0f, 166.0f };

		static const float portX1[2] = { 15.0f, 45.0f };
		static const float portY1[8] = { 191.0f, 215.0f, 239.0f, 263.0f, 287.0f, 311.0f, 335.0f, 359.0f };

		LabelMICROWidget *display = new LabelMICROWidget();
		display->box.pos = Vec(4, 37);
		addChild(display);

		BidooBlueTrimpotWithFloatDisplay* bpm = createParam<BidooBlueTrimpotWithFloatDisplay>(Vec(portX0[0], portY0[0]), module, MU::BPM_PARAM);
		bpm->lblDisplay = display;
		bpm->valueForDisplay = module ? &module->bpm : NULL;
		bpm->format = "%2.2f";
		bpm->header = "BPM";
		addParam(bpm);
		BidooBlueTrimpotWithFloatDisplay* bpmFine = createParam<BidooBlueTrimpotWithFloatDisplay>(Vec(portX0[0], portY0[1]), module, MU::BPMFINE_PARAM);
		bpmFine->lblDisplay = display;
		bpmFine->valueForDisplay = module ? &module->bpm : NULL;
		bpmFine->format = "%2.2f";
		bpmFine->header = "BPM";
		addParam(bpmFine);

		BidooBlueTrimpotWithFloatDisplay* stepLength = createParam<BidooBlueTrimpotWithFloatDisplay>(Vec(portX0[1], portY0[0]), module, MU::STEPLENGTH_PARAM);
		stepLength->lblDisplay = display;
		stepLength->valueForDisplay = module ? &module->displayLength : NULL;
		stepLength->format = "%2.2f %%";
		stepLength->header = "Step len.";
		addParam(stepLength);
		BidooBlueTrimpotWithFloatDisplay* stepLengthFine = createParam<BidooBlueTrimpotWithFloatDisplay>(Vec(portX0[1], portY0[1]), module, MU::STEPLENGTHFINE_PARAM);
		stepLengthFine->lblDisplay = display;
		stepLengthFine->valueForDisplay = module ? &module->displayLength : NULL;
		stepLengthFine->format = "%2.2f %%";
		stepLengthFine->header = "Step len.";
		addParam(stepLengthFine);

		BidooBlueTrimpotWithFloatDisplay* noteLength = createParam<BidooBlueTrimpotWithFloatDisplay>(Vec(portX0[0], portY0[2]), module, MU::NOTELENGTH_PARAM);
		noteLength->lblDisplay = display;
		noteLength->valueForDisplay = module ? &module->displayNoteLength : NULL;
		noteLength->format = "%2.2f %%";
		noteLength->header = "Trigs len.";
		addParam(noteLength);

		BidooBlueTrimpotWithFloatDisplay* offset = createParam<BidooBlueTrimpotWithFloatDisplay>(Vec(portX0[1], portY0[2]), module, MU::OFFSET_PARAM);
		offset->lblDisplay = display;
		offset->valueForDisplay = module ? &module->displayOffset : NULL;
		offset->format = "%2.2f %%";
		offset->header = "Trigs offset";
		addParam(offset);

		BidooBlueTrimpotWithDisplay* cv = createParam<BidooBlueTrimpotWithDisplay>(Vec(portX0[0] + 14, portY0[2] + 19), module, MU::CV_PARAM);
		cv->lblDisplay = display;
		cv->param = module ? &module->params[MU::CV_PARAM] : NULL;
		cv->format = "%2.2f V";
		cv->header = "CV";
		addParam(cv);

		BidooBlueTrimpotWithFloatDisplay* stepProb = createParam<BidooBlueTrimpotWithFloatDisplay>(Vec(portX0[0], portY0[3]), module, MU::STEPPROBA_PARAM);
		stepProb->lblDisplay = display;
		stepProb->valueForDisplay = module ? &module->displayStepProba : NULL;
		stepProb->format = "%2.2f %%";
		stepProb->header = "Step prob.";
		addParam(stepProb);

		BidooBlueTrimpotWithFloatDisplay* altOutProb = createParam<BidooBlueTrimpotWithFloatDisplay>(Vec(portX0[1], portY0[3]), module, MU::ALTEOSTEPPROBA_PARAM);
		altOutProb->lblDisplay = display;
		altOutProb->valueForDisplay = module ? &module->displayAltProba : NULL;
		altOutProb->format = "%2.2f %%";
		altOutProb->header = "Alt out prob.";
		addParam(altOutProb);

		BidooBlueTrimpotWithFloatDisplay* numTrig = createParam<BidooBlueTrimpotWithFloatDisplay>(Vec(portX0[0], portY0[4]), module, MU::NUMTRIGS_PARAM);
		numTrig->lblDisplay = display;
		numTrig->valueForDisplay = module ? &module->displayNumTrigs : NULL;
		numTrig->format = "%2.0f";
		numTrig->header = "Trigs count";
		addParam(numTrig);

		BidooBlueTrimpotWithFloatDisplay* distTrig = createParam<BidooBlueTrimpotWithFloatDisplay>(Vec(portX0[1], portY0[4]), module, MU::DISTTRIGS_PARAM);
		distTrig->lblDisplay = display;
		distTrig->valueForDisplay = module ? &module->displayDistTrigs : NULL;
		distTrig->format = "%2.2f %%";
		distTrig->header = "Trigs Dist.";
		addParam(distTrig);

		TinyPJ301MPortWithDisplay* bpmIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[0]), module, MU::BPM_INPUT);
		bpmIn->lblDisplay = display;
		bpmIn->port = module ? &module->inputs[MU::BPM_INPUT] : NULL;
		bpmIn->format = "%2.2f V";
		bpmIn->header = "BPM";
		addInput(bpmIn);

		TinyPJ301MPortWithDisplay* bpmOut = createOutput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[0]), module, MU::BPM_OUTPUT);
		bpmOut->lblDisplay = display;
		bpmOut->port = module ? &module->outputs[MU::BPM_OUTPUT] : NULL;
		bpmOut->format = "%2.2f V";
		bpmOut->header = "BPM";
		addOutput(bpmOut);

		TinyPJ301MPortWithDisplay* activateIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[1]), module, MU::ACTIVATE_INPUT);
		activateIn->lblDisplay = display;
		activateIn->port = module ? &module->inputs[MU::ACTIVATE_INPUT] : NULL;
		activateIn->format = "%2.2f V";
		activateIn->header = "Step start";
		addInput(activateIn);

		TinyPJ301MPortWithDisplay* activateOUT = createOutput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[1]), module, MU::EOSTEP_OUTPUT);
		activateOUT->lblDisplay = display;
		activateOUT->port = module ? &module->outputs[MU::EOSTEP_OUTPUT] : NULL;
		activateOUT->format = "%2.2f V";
		activateOUT->header = "Step end";
		addOutput(activateOUT);

		addChild(createLight<SmallLight<GreenLight>>(Vec(portX1[1] - 11, portY1[1] + 5), module, MU::NORMEOS_LIGHT));

		TinyPJ301MPortWithDisplay* activateAltOUT = createOutput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[2]), module, MU::ALTEOSTEP_OUTPUT);
		activateAltOUT->lblDisplay = display;
		activateAltOUT->port = module ? &module->outputs[MU::ALTEOSTEP_OUTPUT] : NULL;
		activateAltOUT->format = "%2.2f V";
		activateAltOUT->header = "Alt step end";
		addOutput(activateAltOUT);

		addChild(createLight<SmallLight<GreenLight>>(Vec(portX1[1] - 11, portY1[2] + 5), module, MU::ALTEOS_LIGHT));

		TinyPJ301MPortWithDisplay* inhibitIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[2]), module, MU::INHIBIT_INPUT);
		inhibitIn->lblDisplay = display;
		inhibitIn->port = module ? &module->inputs[MU::INHIBIT_INPUT] : NULL;
		inhibitIn->format = "%2.2f V";
		inhibitIn->header = "Inhibit step";
		addInput(inhibitIn);

		TinyPJ301MPortWithDisplay* noteLengthIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[3]), module, MU::NOTELENGTH_INPUT);
		noteLengthIn->lblDisplay = display;
		noteLengthIn->port = module ? &module->inputs[MU::NOTELENGTH_INPUT] : NULL;
		noteLengthIn->format = "%2.2f V";
		noteLengthIn->header = "Trigs len.";
		addInput(noteLengthIn);

		TinyPJ301MPortWithDisplay* offsetIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[3]), module, MU::OFFSET_INPUT);
		offsetIn->lblDisplay = display;
		offsetIn->port = module ? &module->inputs[MU::OFFSET_INPUT] : NULL;
		offsetIn->format = "%2.2f V";
		offsetIn->header = "Offset mod";
		addInput(offsetIn);

		TinyPJ301MPortWithDisplay* cvIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0] + 14, portY1[4]), module, MU::CV_INPUT);
		cvIn->lblDisplay = display;
		cvIn->port = module ? &module->inputs[MU::CV_INPUT] : NULL;
		cvIn->format = "%2.2f V";
		cvIn->header = "CV mod";
		addInput(cvIn);

		TinyPJ301MPortWithDisplay* numTrigsIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[5]), module, MU::NUMTRIGS_INPUT);
		numTrigsIn->lblDisplay = display;
		numTrigsIn->port = module ? &module->inputs[MU::NUMTRIGS_INPUT] : NULL;
		numTrigsIn->format = "%2.2f V";
		numTrigsIn->header = "Trigs count";
		addInput(numTrigsIn);

		TinyPJ301MPortWithDisplay* distTrigsIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[5]), module, MU::DISTTRIGS_INPUT);
		distTrigsIn->lblDisplay = display;
		distTrigsIn->port = module ? &module->inputs[MU::DISTTRIGS_INPUT] : NULL;
		distTrigsIn->format = "%2.2f V";
		distTrigsIn->header = "Trigs Dist.";
		addInput(distTrigsIn);

		TinyPJ301MPortWithDisplay* gateBridgeIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[6]), module, MU::GATEBRIDGE_INPUT);
		gateBridgeIn->lblDisplay = display;
		gateBridgeIn->port = module ? &module->inputs[MU::GATEBRIDGE_INPUT] : NULL;
		gateBridgeIn->format = "%2.2f V";
		gateBridgeIn->header = "Gate";
		addInput(gateBridgeIn);

		TinyPJ301MPortWithDisplay* gateBridgeOut = createOutput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[6]), module, MU::GATEBRIDGE_OUTPUT);
		gateBridgeOut->lblDisplay = display;
		gateBridgeOut->port = module ? &module->outputs[MU::GATEBRIDGE_OUTPUT] : NULL;
		gateBridgeOut->format = "%2.2f V";
		gateBridgeOut->header = "Gate";
		addOutput(gateBridgeOut);

		addParam(createParam<MiniLEDButton>(Vec(portX1[1] - 11, portY1[6] + 5), module, MU::TRIGSTACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[1] - 11, portY1[6] + 5), module, MU::TRIGSTACK_LIGHT));

		TinyPJ301MPortWithDisplay* cvBridgeIn = createInput<TinyPJ301MPortWithDisplay>(Vec(portX1[0], portY1[7]), module, MU::CVBRIDGE_INPUT);
		cvBridgeIn->lblDisplay = display;
		cvBridgeIn->port = module ? &module->inputs[MU::CVBRIDGE_INPUT] : NULL;
		cvBridgeIn->format = "%2.2f V";
		cvBridgeIn->header = "CV";
		addInput(cvBridgeIn);

		TinyPJ301MPortWithDisplay* cvBridgeOut = createOutput<TinyPJ301MPortWithDisplay>(Vec(portX1[1], portY1[7]), module, MU::CVBRIDGE_OUTPUT);
		cvBridgeOut->lblDisplay = display;
		cvBridgeOut->port = module ? &module->outputs[MU::CVBRIDGE_OUTPUT] : NULL;
		cvBridgeOut->format = "%2.2f V";
		cvBridgeOut->header = "CV";
		addOutput(cvBridgeOut);


		addParam(createParam<MiniLEDButton>(Vec(portX1[1] - 11, portY1[7] + 5), module, MU::CVSTACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[1] - 11, portY1[7] + 5), module, MU::CVSTACK_LIGHT));
	}
};

Model *modelMU = createModel<MU, MUWidget>("MU");
