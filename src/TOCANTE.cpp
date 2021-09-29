#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;


struct TOCANTE : Module {
	enum ParamIds {
		BPM_PARAM,
		BPMFINE_PARAM,
		BEATS_PARAM,
		REF_PARAM,
		RUN_PARAM,
		RESET_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		BPM_INPUT,
		BPMFINE_INPUT,
		BEATS_INPUT,
		REF_INPUT,
		RUN_INPUT,
		RESET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_MEASURE,
		OUT_BEAT,
		OUT_TRIPLET,
		OUT_QUARTER,
		OUT_EIGHTH,
		OUT_SIXTEENTH,
		OUT_RESET,
		OUT_RUN,
		NUM_OUTPUTS
	};
	enum LightIds {
		RUNNING_LIGHT,
		RESET_LIGHT,
		NUM_LIGHTS
	};

	int ref = 2;
	int beats = 1;
	int currentStep = 0;
	int stepsPerMeasure = 1;
	int stepsPerBeat = 1;
	int stepsPerSixteenth = 1;
	int stepsPerEighth = 1;
	int stepsPerQuarter = 1;
	int stepsPerTriplet = 1;
	int count = 0;
	dsp::PulseGenerator gatePulse;
	dsp::PulseGenerator gatePulse_triplets;
	dsp::PulseGenerator gatePulse_Measure;
	dsp::PulseGenerator resetPulse;
	dsp::PulseGenerator runPulse;
	dsp::SchmittTrigger runningTrigger;
	dsp::SchmittTrigger resetTrigger;
	bool running = false;
	bool reset = false;
	float runningLight = 0.0f;
	bool pulseEven = false, pulseTriplets = false, pulseMeasure = false, pulseReset = false, pulseRun = false;
	float bpm = 0.0f;

	TOCANTE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(BPM_PARAM, 1.f, 350.f, 60.f, "BPM");
		configParam(BPMFINE_PARAM, 0.f, 0.99f, 0.f, "Fine");
		configParam(BEATS_PARAM,  1.f, 32.f, 4.f, "Beats per measure");
		configParam(REF_PARAM, 1.f, 4.f, 2.f, "Note value");
	}

	void process(const ProcessArgs &args) override;

};

void TOCANTE::process(const ProcessArgs &args) {
	ref = clamp(powf(2.0f,params[REF_PARAM].getValue()+(int)rescale(clamp(inputs[REF_INPUT].getVoltage(),0.0f,10.0f),0.0f,10.0f,0.0f,3.0f)),2.0f,16.0f);
	beats = clamp(params[BEATS_PARAM].getValue()+rescale(clamp(inputs[BEATS_INPUT].getVoltage(),0.0f,10.0f),0.0f,10.0f,0.0f,32.0f),1.0f,32.0f);
	bpm = clamp(round(params[BPM_PARAM].getValue()+rescale(clamp(inputs[BPM_INPUT].getVoltage(),0.0f,10.0f),0.0f,10.0f,0.0f,350.0f)) + round(100*(params[BPMFINE_PARAM].getValue()+rescale(clamp(inputs[BPMFINE_INPUT].getVoltage(),0.0f,10.0f),0.0f,10.0f,0.0f,0.99f))) * 0.01f, 1.0f, 350.0f);
	stepsPerSixteenth =  floor(args.sampleRate / bpm * 60 * ref / 32) * 2;
	stepsPerEighth = stepsPerSixteenth * 2;
	stepsPerQuarter = stepsPerEighth * 2;
	stepsPerTriplet = floor(stepsPerQuarter / 3);
	stepsPerBeat = stepsPerSixteenth * 16 / ref;
	stepsPerMeasure = beats*stepsPerBeat;

	lights[RESET_LIGHT].setBrightness(lights[RESET_LIGHT].getBrightness()-0.0001f*lights[RESET_LIGHT].getBrightness());

	if (runningTrigger.process(params[RUN_PARAM].getValue())) {
		running = !running;
		if (running) {
			currentStep = 0;
			count = beats;
			resetPulse.trigger(1e-3f);
			lights[RESET_LIGHT].setBrightness(1.0);
		}
		runPulse.trigger(1e-3f);
	}

	if (resetTrigger.process(params[RESET_PARAM].getValue())){
		currentStep = 0;
		count = beats;
		resetPulse.trigger(1e-3f);
		lights[RESET_LIGHT].setBrightness(1.0);
	}

	if ((stepsPerSixteenth>0) && ((currentStep % stepsPerSixteenth) == 0)) {
		gatePulse.trigger(1e-3f);
	}

	if ((stepsPerTriplet>0) && ((currentStep % stepsPerTriplet) == 0) && (currentStep <= (stepsPerMeasure-100))) {
		gatePulse_triplets.trigger(1e-3f);
	}

	if (currentStep == 0) {
		gatePulse_Measure.trigger(1e-3f);
	}

	pulseEven = gatePulse.process(args.sampleTime);
	pulseTriplets = gatePulse_triplets.process(args.sampleTime);
	pulseMeasure = gatePulse_Measure.process(args.sampleTime);
	pulseReset = resetPulse.process(args.sampleTime);
	pulseRun = runPulse.process(args.sampleTime);

	if (currentStep % stepsPerBeat == 0) {
		count--;
	}

	if (pulseMeasure) {
		count = beats;
	}

	outputs[OUT_MEASURE].setVoltage(pulseMeasure ? 10.0f : 0.0f);
	outputs[OUT_BEAT].setVoltage((pulseEven && (currentStep % stepsPerBeat == 0)) ? 10.0f : 0.0f);
	outputs[OUT_TRIPLET].setVoltage((pulseTriplets && (currentStep % stepsPerTriplet == 0)) ? 10.0f : 0.0f);
	outputs[OUT_QUARTER].setVoltage((pulseEven && (currentStep % stepsPerQuarter == 0)) ? 10.0f : 0.0f);
	outputs[OUT_EIGHTH].setVoltage((pulseEven && (currentStep % stepsPerEighth == 0)) ? 10.0f : 0.0f);
	outputs[OUT_SIXTEENTH].setVoltage((pulseEven && (currentStep % stepsPerSixteenth == 0)) ? 10.0f : 0.0f);

	outputs[OUT_RESET].setVoltage(pulseReset ? 10.0f : 0.0f);
	outputs[OUT_RUN].setVoltage(pulseRun ? 10.0f : 0.0f);


	if (running) {
		currentStep = floor((currentStep + 1) % stepsPerMeasure);
	}
	lights[RUNNING_LIGHT].setBrightness(running ? 1.0 : 0.0);
}

struct TOCANTEDisplay : TransparentWidget {
	TOCANTE *module;

	TOCANTEDisplay() {

	}

	void draw(const DrawArgs &args) override {
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
		nvgGlobalTint(args.vg, color::WHITE);
		if (module != NULL) {
			char tBPM[128],tBeats[128];
			snprintf(tBPM, sizeof(tBPM), "%1.2f BPM", module->bpm);
			snprintf(tBeats, sizeof(tBeats), "%1i/%1i", module->beats, module->ref);
			nvgFontSize(args.vg, 16.0f);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgText(args.vg, 0.0f, 0.0f, tBPM, NULL);
			nvgText(args.vg, 0.0f, 15.0f, tBeats, NULL);
		}
	}
};

struct TOCANTEMeasureDisplay : TransparentWidget {
	TOCANTE *module;
	std::shared_ptr<Font> font;

	TOCANTEMeasureDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void draw(const DrawArgs &args) override {
		if (module != NULL) {
			char tSteps[128];
			snprintf(tSteps, sizeof(tSteps), "%1i", module->count);

			nvgFontSize(args.vg, 16.0f);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT);
			nvgText(args.vg, 0.0f, 0.0f, tSteps, NULL);
		}
	}
};

template <typename BASE>
struct TocanteLight : BASE {
	TocanteLight() {
		this->box.size = mm2px(Vec(6.f, 6.f));
	}
};

struct TOCANTEWidget : ModuleWidget {
	TOCANTEWidget(TOCANTE *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/TOCANTE.svg")));

		TOCANTEDisplay *display = new TOCANTEDisplay();
		display->module = module;
		display->box.pos = Vec(11.0f, 53.0f);
		display->box.size = Vec(50.0f, 85.0f);
		addChild(display);

		TOCANTEMeasureDisplay *mDisplay = new TOCANTEMeasureDisplay();
		mDisplay->module = module;
		mDisplay->box.pos = Vec(92.0f, 68.0f);
		mDisplay->box.size = Vec(25.0f, 25.0f);
		addChild(mDisplay);

		addParam(createParam<LEDBezel>(Vec(41, 160), module, TOCANTE::RESET_PARAM));
		addChild(createLight<TocanteLight<BlueLight>>(Vec(43, 162), module, TOCANTE::RESET_LIGHT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(45, 190), module, TOCANTE::OUT_RESET));

		addParam(createParam<LEDBezel>(Vec(76, 160), module, TOCANTE::RUN_PARAM));
		addChild(createLight<TocanteLight<BlueLight>>(Vec(78, 162), module, TOCANTE::RUNNING_LIGHT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(80.0f, 190), module, TOCANTE::OUT_RUN));

		addParam(createParam<BidooBlueKnob>(Vec(3.0f,90.0f), module, TOCANTE::BPM_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(3.0f,155.0f), module, TOCANTE::BPMFINE_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(37.0f,90.0f), module, TOCANTE::BEATS_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(72.0f,90.0f), module, TOCANTE::REF_PARAM));

		addInput(createInput<TinyPJ301MPort>(Vec(10, 125), module, TOCANTE::BPM_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(10, 190), module, TOCANTE::BPMFINE_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(45, 125), module, TOCANTE::BEATS_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(80, 125), module, TOCANTE::REF_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(7.0f, 236.0f), module, TOCANTE::OUT_MEASURE));
		addOutput(createOutput<PJ301MPort>(Vec(73.5f, 236.0f), module, TOCANTE::OUT_BEAT));
		addOutput(createOutput<PJ301MPort>(Vec(7.0f, 283.0f), module, TOCANTE::OUT_QUARTER));
		addOutput(createOutput<PJ301MPort>(Vec(73.5f, 283.0f), module, TOCANTE::OUT_TRIPLET));
		addOutput(createOutput<PJ301MPort>(Vec(7.0f, 330.0f), module, TOCANTE::OUT_EIGHTH));
		addOutput(createOutput<PJ301MPort>(Vec(73.5f, 330.0f), module, TOCANTE::OUT_SIXTEENTH));
	}
};

Model *modelTOCANTE = createModel<TOCANTE, TOCANTEWidget>("tOCAnTe");
