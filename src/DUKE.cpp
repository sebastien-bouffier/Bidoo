#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

struct DUKE : Module {
	enum ParamIds {
		SLIDER_PARAM,
		ADONF_PARAM = SLIDER_PARAM + 4,
		NADA_PARAM,
		MIN_PARAM = NADA_PARAM + 4,
		MAX_PARAM = MIN_PARAM + 4,
		TYPE_PARAM = MAX_PARAM + 4,
		NUM_PARAMS = TYPE_PARAM + 4,
	};
	enum InputIds {
		SLIDER_INPUT,
		NUM_INPUTS = SLIDER_INPUT + 4
	};
	enum OutputIds {
		CV_OUTPUT,
		NUM_OUTPUTS = CV_OUTPUT + 4
	};
	enum LightIds {
		NUM_LIGHTS
	};

	dsp::SchmittTrigger aDonfTrigger;
	dsp::SchmittTrigger nadaTrigger;

	DUKE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(ADONF_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(NADA_PARAM, 0.0f, 1.0f, 0.0f);

		for (int i = 0; i < 4; i++) {
			configParam(MAX_PARAM + i, 0.0f, 10.0f, 10.0f);
			configParam(MIN_PARAM + i, 0.0f, 10.0f, 0.0f);
      configParam(TYPE_PARAM + i, 0.0f, 1.0f, 0.0f);
      configParam(SLIDER_PARAM + i, 0.0f, 10.0f, 0.0f);
		}
  }

	void process(const ProcessArgs &args) override;
};


void DUKE::process(const ProcessArgs &args) {
	if (aDonfTrigger.process(params[ADONF_PARAM].value)) {
		for (int i = 0; i < 4; i ++) {
				params[SLIDER_PARAM + i].setValue(10.f);
		}
	}

	if (nadaTrigger.process(params[NADA_PARAM].value)) {
		for (int i = 0; i < 4; i ++) {
				params[SLIDER_PARAM + i].setValue(0.f);
		}
	}

	for (int i = 0; i < 4; i ++) {
			float min = params[MIN_PARAM + i].getValue() - 5.0f * params[TYPE_PARAM + i].getValue();
			float max = params[MAX_PARAM + i].getValue() - 5.0f * params[TYPE_PARAM + i].getValue();
			outputs[CV_OUTPUT + i].setVoltage(min + clamp(params[SLIDER_PARAM + i].getValue() + inputs[SLIDER_INPUT + i].getVoltage(), 0.0f , 10.0f) * (max - min) / 10.0f);
	}
}

struct DUKEWidget : ModuleWidget {
	DUKEWidget(DUKE *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/DUKE.svg")));

		static const float portX0[4] = {36.0f, 70.0f, 105.0f, 140.0f};
		addParam(createParam<BlueCKD6>(Vec(portX0[0]-29.0f, 190.0f), module, DUKE::ADONF_PARAM));
		addParam(createParam<BlueCKD6>(Vec(portX0[0]-29.0f, 230.0f), module, DUKE::NADA_PARAM));

		for (int i = 0; i < 4; i++) {
			addParam(createParam<BidooBlueKnob>(Vec(portX0[i]-2.0f, 51.0f), module, DUKE::MAX_PARAM + i));
			addParam(createParam<BidooBlueKnob>(Vec(portX0[i]-2.0f, 95.0f), module, DUKE::MIN_PARAM + i));
			addParam(createParam<CKSS>(Vec(portX0[i]+6.0f, 139.0f), module, DUKE::TYPE_PARAM + i));
			addParam(createParam<LEDSliderGreen>(Vec(portX0[i]+4.0f, 176.0f), module, DUKE::SLIDER_PARAM + i));
			addInput(createInput<PJ301MPort>(Vec(portX0[i]+1.0f, 284.0f), module, DUKE::SLIDER_INPUT + i));
			addOutput(createOutput<PJ301MPort>(Vec(portX0[i]+1.0f, 323.0f), module, DUKE::CV_OUTPUT + i));
		}
	}
};

Model *modelDUKE = createModel<DUKE, DUKEWidget>("dUKe");
