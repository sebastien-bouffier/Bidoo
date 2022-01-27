#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

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

	MU() : BidooModule() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(BPM_PARAM, 0.0f, 800.0f, 117.0f);
		configParam(BPMFINE_PARAM, 0.0f, 0.99f, 0.0f);
		configParam(STEPLENGTH_PARAM, 0.0f, 1600.0f, 100.0f);
		configParam(STEPLENGTHFINE_PARAM, -0.5f, 0.5f, 0.0f);
		configParam(NOTELENGTH_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(STEPPROBA_PARAM, 0.0f, 1.0f, 1.0f);
		configParam(ALTEOSTEPPROBA_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(NUMTRIGS_PARAM, 1.0f, 64.0f, 1.0f);
		configParam(DISTTRIGS_PARAM, 0.0f, 1.0f, 1.0f);
		configParam(CV_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(START_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(CVSTACK_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(TRIGSTACK_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(MUTE_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(OFFSET_PARAM, 0.0f, 1.0f, 0.0f);
	}

	void process(const ProcessArgs &args) override;
};

void MU::process(const ProcessArgs &args) {
	const float invLightLambda = 13.333333333333333333333f;
	float invESR = 1 / args.sampleRate;

	bpm = inputs[BPM_INPUT].active ? rescale(clamp(inputs[BPM_INPUT].value,0.0f,10.0f),0.0f,10.0f,1.0f,800.99f) : (round(params[BPM_PARAM].value)+round(100*params[BPMFINE_PARAM].value)/100);
	displayLength = round(params[STEPLENGTH_PARAM].value) + round(params[STEPLENGTHFINE_PARAM].value*100) * 0.01f;

	bool wasActive = isActive;
	if (startTrigger.process(params[START_PARAM].value))
	{
		lights[START_LIGHT].value = 10.0f;
		isActive = true;
	}
	if (activateTrigger.process(inputs[ACTIVATE_INPUT].value))
	{
		isActive = true;
	}

	if (!wasActive && isActive) {
		initTicks = (displayLength/100) * args.sampleRate * 60 / bpm;
		ticks = initTicks;
		count = 0;
		play = (random::uniform() + params[STEPPROBA_PARAM].value)>=1;
		alt = (random::uniform() + params[ALTEOSTEPPROBA_PARAM].value)>1;
		lights[GATE_LIGHT].value = 0.0f;
		lights[GATE_LIGHT+1].value = 0.0f;
		lights[GATE_LIGHT+2].value = 10.0f;
	}

	if (cvModeTrigger.process(params[CVSTACK_PARAM].value))
	{
		cvStack = !cvStack;
	}

	if (trigModeTrigger.process(params[TRIGSTACK_PARAM].value))
	{
		trigStack = !trigStack;
	}

	if (muteTrigger.process(params[MUTE_PARAM].value))
	{
		mute = !mute;
	}

	numTrigs = clamp(params[NUMTRIGS_PARAM].value + rescale(clamp(inputs[NUMTRIGS_INPUT].value,-10.0f,10.0f),-10.0f,10.0f,-64.0f,64.0f),0.0f,64.0f);
	distRetrig = clamp(params[DISTTRIGS_PARAM].value + rescale(clamp(inputs[DISTTRIGS_INPUT].value,-10.0f,10.0f),-10.0f,10.0f,-1.0f,1.0f), 0.0f,1.0f) * initTicks;
	gateTicks = initTicks * clamp(params[NOTELENGTH_PARAM].value + rescale(clamp(inputs[NOTELENGTH_INPUT].value,-10.0f,10.0f),-10.0f,10.0f,-1.0f,1.0f),0.0f,1.0f);
	displayNoteLength = clamp(params[NOTELENGTH_PARAM].value + rescale(clamp(inputs[NOTELENGTH_INPUT].value,-10.0f,10.0f),-10.0f,10.0f,-1.0f,1.0f),0.0f,1.0f) * 100;
	displayStepProba = params[STEPPROBA_PARAM].value * 100;
	displayAltProba = params[ALTEOSTEPPROBA_PARAM].value * 100;
	displayDistTrigs = params[DISTTRIGS_PARAM].value * 100;
	displayNumTrigs = numTrigs;
	displayOffset = clamp(params[OFFSET_PARAM].value + rescale(clamp(inputs[OFFSET_INPUT].value, 0.0f,10.0f), 0.0f, 10.0f, 0.0f,1.0f), 0.0f, 1.0f) * 100;

	if (inhibateTrigger.process(inputs[INHIBIT_INPUT].value))
	{
		isActive = false;
	}

	if (isActive && (ticks >= 0)) {
		if (ticks <= 0) {
			isActive = false;
			eosPulse.trigger(invESR);
			outputs[GATEBRIDGE_OUTPUT].value = 0.0f;
			outputs[CVBRIDGE_OUTPUT].value = 0.0f;
		}
		else {
			int offset = displayOffset * initTicks * 0.01f;
			int mult = ((distRetrig > 0) && (count>offset))  ? ((count-offset) / distRetrig) : 0;
			if (play && (mult < numTrigs) && (count >= (offset + mult * distRetrig)) && (count <= (offset + (mult * distRetrig) + gateTicks))) {
				outputs[GATEBRIDGE_OUTPUT].value = mute ? 0.0f : 10.0f;
				lights[GATE_LIGHT].value = mute ? 0.0f : 10.0f;
				lights[GATE_LIGHT+1].value = 0.0f;
				lights[GATE_LIGHT+2].value = !mute ? 0.0f : 10.0f;
			}
			else {
				outputs[GATEBRIDGE_OUTPUT].value = (trigStack && !mute && (inputs[GATEBRIDGE_INPUT].value > 0.0f)) ? clamp(inputs[GATEBRIDGE_INPUT].value,0.0f,10.0f) : 0.0f;
				lights[GATE_LIGHT].value = 0.0f;
				lights[GATE_LIGHT+1].value = 0.0f;
				lights[GATE_LIGHT+2].value = 10.0f;
			}
			outputs[CVBRIDGE_OUTPUT].value = clamp(params[CV_PARAM].value + clamp(inputs[CV_INPUT].value,-10.0f,10.0f) + (cvStack ? inputs[CVBRIDGE_INPUT].value : 0.0f),0.0f,10.0f);
		}
		ticks--;
		count++;
	}
	else {
		outputs[GATEBRIDGE_OUTPUT].value = clamp(inputs[GATEBRIDGE_INPUT].value,0.0f,10.0f);
		outputs[CVBRIDGE_OUTPUT].value = inputs[CVBRIDGE_INPUT].value;
	}

	bool pulse = eosPulse.process(invESR);

	outputs[EOSTEP_OUTPUT].value = !alt ? (pulse ? 10.0f : 0.0f) : 0.0f;
	outputs[ALTEOSTEP_OUTPUT].value = alt ? (pulse ? 10.0f : 0.0f) : 0.0f;
	outputs[BPM_OUTPUT].value = rescale(bpm,1.0f,800.99f,0.0f,10.0f);

	lights[NORMEOS_LIGHT].value = (!alt && pulse) ? 10.0f : (lights[NORMEOS_LIGHT].value - lights[NORMEOS_LIGHT].value * invLightLambda * invESR);
	lights[ALTEOS_LIGHT].value = (alt && pulse) ? 10.0f : (lights[ALTEOS_LIGHT].value - lights[ALTEOS_LIGHT].value * invLightLambda * invESR);
	lights[START_LIGHT].value = lights[START_LIGHT].value - lights[START_LIGHT].value * invLightLambda * invESR;
	if (outputs[GATEBRIDGE_OUTPUT].value == 0.0f) lights[GATE_LIGHT].value -= 4 * lights[GATE_LIGHT].value * invLightLambda * invESR;
	if (!isActive) lights[GATE_LIGHT+2].value -= 4 * lights[GATE_LIGHT+2].value * invLightLambda * invESR;
	if (!isActive) lights[GATE_LIGHT].value -= 4 * lights[GATE_LIGHT].value * invLightLambda * invESR;
	lights[MUTE_LIGHT].value = mute ? 10.0f : 0.0f;
	lights[CVSTACK_LIGHT].value = cvStack ? 10.0f : 0.0f;
	lights[TRIGSTACK_LIGHT].value = trigStack ? 10.0f : 0.0f;
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
		cv->valueForDisplay = module ? &module->params[MU::CV_PARAM].value : NULL;
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
		activateOUT->valueForDisplay = module ? &module->outputs[MU::BPM_OUTPUT].value : NULL;
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
};

Model *modelMU = createModel<MU, MUWidget>("MU");
