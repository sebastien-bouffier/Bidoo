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
		NUM_OUTPUTS
	};
	enum LightIds {
		RUNNING_LIGHT,
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
	dsp::PulseGenerator gatePulse;
	dsp::PulseGenerator gatePulse_triplets;
	dsp::SchmittTrigger runningTrigger;
	dsp::SchmittTrigger resetTrigger;
	bool running = true;
	bool reset = false;
	float runningLight = 0.0f;
	bool pulseEven = false, pulseTriplets = false;
	float bpm = 0.0f;

	TOCANTE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(BPM_PARAM, 1.f, 350.f, 60.f, "BPM");
		configParam(BPMFINE_PARAM, 0.f, 0.99f, 0.f, "Fine");
		configParam(BEATS_PARAM,  1.f, 32.f, 4.f, "Beats per measure");
		configParam(REF_PARAM, 1.f, 4.f, 2.f, "Note value");
	}

	void process(const ProcessArgs &args) override;

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "running", json_boolean(running));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *runningJ = json_object_get(rootJ, "running");
		if (runningJ){
			running = json_is_true(runningJ);
		}
}

};

void TOCANTE::process(const ProcessArgs &args) {
	if (runningTrigger.process(params[RUN_PARAM].value)) {
		running = !running;
		if(running){
			currentStep = 0;
		}
	}

	if (resetTrigger.process(params[RESET_PARAM].value)){
		currentStep = 0;
	}

	ref = clamp(powf(2.0f,params[REF_PARAM].value+(int)rescale(clamp(inputs[REF_INPUT].value,0.0f,10.0f),0.0f,10.0f,0.0f,3.0f)),2.0f,16.0f);
	beats = clamp(params[BEATS_PARAM].value+rescale(clamp(inputs[BEATS_INPUT].value,0.0f,10.0f),0.0f,10.0f,0.0f,32.0f),1.0f,32.0f);
	bpm = clamp(round(params[BPM_PARAM].value+rescale(clamp(inputs[BPM_INPUT].value,0.0f,10.0f),0.0f,10.0f,0.0f,350.0f)) + round(100*(params[BPMFINE_PARAM].value+rescale(clamp(inputs[BPMFINE_INPUT].value,0.0f,10.0f),0.0f,10.0f,0.0f,0.99f))) * 0.01f, 1.0f, 350.0f);
	stepsPerSixteenth =  floor(args.sampleRate / bpm * 60 * ref / 32) * 2;
	stepsPerEighth = stepsPerSixteenth * 2;
	stepsPerQuarter = stepsPerEighth * 2;
	stepsPerTriplet = floor(stepsPerQuarter / 3);
	stepsPerBeat = stepsPerSixteenth * 16 / ref;
	stepsPerMeasure = beats*stepsPerBeat;

	if ((stepsPerSixteenth>0) && ((currentStep % stepsPerSixteenth) == 0)) {
		gatePulse.trigger(1e-3f);
	}

	if ((stepsPerTriplet>0) && ((currentStep % stepsPerTriplet) == 0) && (currentStep <= (stepsPerMeasure-100))) {
		gatePulse_triplets.trigger(1e-3f);
	}

	pulseEven = gatePulse.process(args.sampleTime);
	pulseTriplets = gatePulse_triplets.process(args.sampleTime);

	outputs[OUT_MEASURE].value = (currentStep == 0) ? 10.0f : 0.0f;
	outputs[OUT_BEAT].value = (pulseEven && (currentStep % stepsPerBeat == 0)) ? 10.0f : 0.0f;
	outputs[OUT_TRIPLET].value = (pulseTriplets && (currentStep % stepsPerTriplet == 0)) ? 10.0f : 0.0f;
	outputs[OUT_QUARTER].value = (pulseEven && (currentStep % stepsPerQuarter == 0)) ? 10.0f : 0.0f;
	outputs[OUT_EIGHTH].value = (pulseEven && (currentStep % stepsPerEighth == 0)) ? 10.0f : 0.0f;
	outputs[OUT_SIXTEENTH].value = (pulseEven && (currentStep % stepsPerSixteenth == 0)) ? 10.0f : 0.0f;
	if (running) {
		currentStep = floor((currentStep + 1) % stepsPerMeasure);
	}
	lights[RUNNING_LIGHT].value = running ? 1.0 : 0.0;
}

struct TOCANTEDisplay : TransparentWidget {
	TOCANTE *module;
	std::shared_ptr<Font> font;

	TOCANTEDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void draw(const DrawArgs &args) override {
		if (module != NULL) {
			char tBPM[128],tBeats[128];
			snprintf(tBPM, sizeof(tBPM), "%1.2f BPM", module->bpm);
			snprintf(tBeats, sizeof(tBeats), "%1i/%1i", module->beats, module->ref);
			//nvgSave(args.vg);
			nvgFontSize(args.vg, 16.0f);
			nvgFontFaceId(args.vg, font->handle);
			nvgTextLetterSpacing(args.vg, -2.0f);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgText(args.vg, 0.0f, 0.0f, tBPM, NULL);
			nvgText(args.vg, 0.0f, 15.0f, tBeats, NULL);
			//nvgRestore(args.vg);
		}
	}
};

struct TOCANTEWidget : ModuleWidget {
	TOCANTEWidget(TOCANTE *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/TOCANTE.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		TOCANTEDisplay *display = new TOCANTEDisplay();
		display->module = module;
		display->box.pos = Vec(16.0f, 55.0f);
		display->box.size = Vec(50.0f, 85.0f);
		addChild(display);

		addParam(createParam<LEDButton>(Vec(78, 185), module, TOCANTE::RUN_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(84, 190), module, TOCANTE::RUNNING_LIGHT));
		addParam(createParam<BlueCKD6>(Vec(39, 180), module, TOCANTE::RESET_PARAM));

		addParam(createParam<BidooBlueKnob>(Vec(3.0f,90.0f), module, TOCANTE::BPM_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(3.0f,155.0f), module, TOCANTE::BPMFINE_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(37.0f,90.0f), module, TOCANTE::BEATS_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(72.0f,90.0f), module, TOCANTE::REF_PARAM));

		addInput(createInput<TinyPJ301MPort>(Vec(10, 125), module, TOCANTE::BPM_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(10, 190), module, TOCANTE::BPMFINE_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(45, 125), module, TOCANTE::BEATS_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(80, 125), module, TOCANTE::REF_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(18.0f, 230.0f), module, TOCANTE::OUT_MEASURE));
		addOutput(createOutput<PJ301MPort>(Vec(62.0f, 230.0f), module, TOCANTE::OUT_BEAT));
		addOutput(createOutput<PJ301MPort>(Vec(18.0f, 280.0f), module, TOCANTE::OUT_QUARTER));
		addOutput(createOutput<PJ301MPort>(Vec(62.0f, 280.0f), module, TOCANTE::OUT_TRIPLET));
		addOutput(createOutput<PJ301MPort>(Vec(18.0f, 331.0f), module, TOCANTE::OUT_EIGHTH));
		addOutput(createOutput<PJ301MPort>(Vec(62.0f, 331.0f), module, TOCANTE::OUT_SIXTEENTH));
	}
};

Model *modelTOCANTE = createModel<TOCANTE, TOCANTEWidget>("tOCAnTe");
