#include "Bidoo.hpp"
#include "BidooComponents.hpp"


using namespace std;

struct DUKE : Module {
	enum ParamIds {
		SLIDER_PARAM,
		MIN_PARAM = SLIDER_PARAM + 4,
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

	DUKE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {	}

	void step() override;
};


void DUKE::step() {
	for (int i = 0; i < 4; i ++) {
			float min = params[MIN_PARAM + i].value - 5 * params[TYPE_PARAM + i].value;
			float max = params[MAX_PARAM + i].value - 5 * params[TYPE_PARAM + i].value;
			outputs[CV_OUTPUT + i].value = min + clampf(params[SLIDER_PARAM + i].value + inputs[SLIDER_INPUT + i].value, 0 , 10) * (max - min) / 10;
	}
}

DUKEWidget::DUKEWidget() {
	DUKE *module = new DUKE();
	setModule(module);
	box.size = Vec(15*12, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/DUKE.svg")));
		addChild(panel);
	}

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));


	static const float portX0[4] = {29, 67, 105, 140};

	for (int i = 0; i < 4; i++) {
		addParam(createParam<RoundSmallBlackKnob>(Vec(portX0[i]-1, 52), module, DUKE::MAX_PARAM + i, 0.0, 10.0, 10));
		addParam(createParam<RoundSmallBlackKnob>(Vec(portX0[i]-1, 95), module, DUKE::MIN_PARAM + i, 0.0, 10.0, 0));
		addParam(createParam<CKSS>(Vec(portX0[i]+6, 139), module, DUKE::TYPE_PARAM + i, 0.0, 1.0, 0.0));
		addParam(createParam<BidooLongSlider>(Vec(portX0[i]+4, 176), module, DUKE::SLIDER_PARAM + i, 0.0, 10.0, 0));
		addInput(createInput<PJ301MPort>(Vec(portX0[i]+1, 284), module, DUKE::SLIDER_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(portX0[i]+1, 323), module, DUKE::CV_OUTPUT));
	}
}
