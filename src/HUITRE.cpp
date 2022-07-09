#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <sstream>
#include <iomanip>

using namespace std;

struct HUITRE : BidooModule {
	enum ParamIds {
		TRIG_PARAM,
		PATTERN_PARAM = TRIG_PARAM + 8,
		CV1_PARAM = PATTERN_PARAM + 8,
		CV2_PARAM = CV1_PARAM + 8,
		MODE_PARAM = CV2_PARAM + 8,
		NUM_PARAMS
	};
	enum InputIds {
		MEASURE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		PATTERN_OUTPUT,
		CV1_OUTPUT,
		CV2_OUTPUT,
		PATTERNTRIG_OUTPUT,
		NUM_OUTPUTS = PATTERNTRIG_OUTPUT + 8
	};
	enum LightIds {
		PATTERN_LIGHT,
		MODE_LIGHT = PATTERN_LIGHT+16*3,
		NUM_LIGHTS =  MODE_LIGHT + 3
	};

	dsp::SchmittTrigger patTriggers[8];
	dsp::SchmittTrigger syncTrigger;
	dsp::SchmittTrigger modeTrigger;
	int currentPattern = 0;
	int nextPattern = 0;
	dsp::PulseGenerator gatePulse;
	bool pulse=false;
	bool mode = false;

	HUITRE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MODE_PARAM, 0.0f, 1.0f, 0.0f);
		for (int i = 0; i < 8; i++) {
			configParam(TRIG_PARAM+i, 0.0f, 10.0f, 0.0f);
			configParam(PATTERN_PARAM+i, 0.0f, 10.0f,0.00f+ i*10.0f/8.0f);
			configParam(CV1_PARAM+i, 0.0f, 10.0f, 0.0f);
			configParam(CV2_PARAM+i, 0.0f, 10.0f, 0.0f);
		}
  }

  void process(const ProcessArgs &args) override;


	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "mode", json_boolean(mode));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *modeJ = json_object_get(rootJ, "mode");
		if (modeJ) mode = json_is_true(modeJ);
	}
};

void HUITRE::process(const ProcessArgs &args) {
	for (int i = 0; i < 8; i++) {
		if (patTriggers[i].process(params[TRIG_PARAM+i].getValue())) {
			nextPattern = i;
		}
	}

	if (modeTrigger.process(params[MODE_PARAM].getValue())) {
		mode=!mode;
	}

	lights[MODE_LIGHT].setBrightness(0);
	lights[MODE_LIGHT + 1].setBrightness(mode ? 1 : 0);
	lights[MODE_LIGHT + 2].setBrightness(mode ? 0 : 1);

	if ((syncTrigger.process(inputs[MEASURE_INPUT].getVoltage())) && (currentPattern != nextPattern)) {
		currentPattern = nextPattern;
		gatePulse.trigger(1e-3f);
	}
	pulse = gatePulse.process(args.sampleTime);

	for (int i = 0; i < 8; i++) {
		if (i==currentPattern) {
			lights[PATTERN_LIGHT+(i*3)].setBrightness(0.0f);
			lights[PATTERN_LIGHT+(i*3)+1].setBrightness(1.0f);
			lights[PATTERN_LIGHT+(i*3)+2].setBrightness(0.0f);

			if (mode) {
				outputs[PATTERNTRIG_OUTPUT+currentPattern].setVoltage(10.0f);
			}
			else if (pulse) {
				outputs[PATTERNTRIG_OUTPUT+currentPattern].setVoltage(10.0f);
			}
			else {
				outputs[PATTERNTRIG_OUTPUT+i].setVoltage(0.0f);
			}
		}
		else if (i==nextPattern) {
			lights[PATTERN_LIGHT+(i*3)].setBrightness(1.0f);
			lights[PATTERN_LIGHT+(i*3)+1].setBrightness(0.0f);
			lights[PATTERN_LIGHT+(i*3)+2].setBrightness(0.0f);
		}
		else {
			lights[PATTERN_LIGHT+(i*3)].setBrightness(0.0f);
			lights[PATTERN_LIGHT+(i*3)+1].setBrightness(0.0f);
			lights[PATTERN_LIGHT+(i*3)+2].setBrightness(0.0f);
			outputs[PATTERNTRIG_OUTPUT+i].setVoltage(0.0f);
		}
	}

	outputs[PATTERN_OUTPUT].setVoltage(params[PATTERN_PARAM+currentPattern].getValue());
	outputs[CV1_OUTPUT].setVoltage(params[CV1_PARAM+currentPattern].getValue());
	outputs[CV2_OUTPUT].setVoltage(params[CV2_PARAM+currentPattern].getValue());
}

struct HUITREWidget : BidooWidget {
  HUITREWidget(HUITRE *module);
};

template <typename BASE>
struct HUITRELight : BASE {
	HUITRELight() {
		this->box.size = mm2px(Vec(6.0f, 6.0f));
	}
};

HUITREWidget::HUITREWidget(HUITRE *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/HUITRE.svg"));

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	addParam(createParam<LEDButton>(Vec(13.0f, 24.f), module, HUITRE::MODE_PARAM));
	addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(13.0f+6.0f, 24.f+6.0f), module, HUITRE::MODE_LIGHT));

	addInput(createInput<PJ301MPort>(Vec(7, 330), module, HUITRE::MEASURE_INPUT));
	addOutput(createOutput<PJ301MPort>(Vec(44.0f, 330), module, HUITRE::PATTERN_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(81.5f, 330), module, HUITRE::CV1_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(118.5f, 330), module, HUITRE::CV2_OUTPUT));

	for (int i = 0; i < 8; i++) {
		addParam(createParam<LEDBezel>(Vec(11.0f, 50 + i*33), module, HUITRE::TRIG_PARAM + i));
		addChild(createLight<HUITRELight<RedGreenBlueLight>>(Vec(12.8f, 51.8f + i*33), module, HUITRE::PATTERN_LIGHT + i*3));
		addParam(createParam<BidooBlueTrimpot>(Vec(45,52 + i*33), module, HUITRE::PATTERN_PARAM+i));
		addParam(createParam<BidooBlueTrimpot>(Vec(72,52 + i*33), module, HUITRE::CV1_PARAM+i));
		addParam(createParam<BidooBlueTrimpot>(Vec(99,52 + i*33), module, HUITRE::CV2_PARAM+i));
		addOutput(createOutput<TinyPJ301MPort>(Vec(130, 55 + i*33), module, HUITRE::PATTERNTRIG_OUTPUT+i));
	}
}

Model *modelHUITRE = createModel<HUITRE, HUITREWidget>("HUITre");
