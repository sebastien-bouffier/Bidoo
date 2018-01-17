#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <ctime>

using namespace std;

struct LATE : Module {
	enum ParamIds {
		SWING_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		SWING_INPUT,
		CLOCK_INPUT,
		RESET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CLOCK_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	bool odd = true;
	bool armed = false;
	SchmittTrigger clockTrigger;
	SchmittTrigger resetTrigger;
	clock_t tCurrent = clock();
	clock_t tPrevious = clock();


	LATE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {	}

	void step() override;
};


void LATE::step() {
	outputs[CLOCK_OUTPUT].value = 0;
	clock_t now = clock();

	if (resetTrigger.process(inputs[RESET_INPUT].value)) {
		odd=true;
	}

	if (clockTrigger.process(inputs[CLOCK_INPUT].value)) {
		tPrevious = tCurrent;
		tCurrent = now;
		if (odd) {
			outputs[CLOCK_OUTPUT].value = 10;
			odd = false;
			armed = false;
		}
		else {
			armed = true;
		}
	}

	float lag = rescalef(clampf(params[SWING_PARAM].value + inputs[SWING_INPUT].value,0,10),0,10,0,(float)tCurrent-(float)tPrevious);

	if (armed && !odd && (((float)now - (float)tCurrent) >= lag)) {
		outputs[CLOCK_OUTPUT].value = 10;
		armed = false;
		odd = true;
	}
}

LATEWidget::LATEWidget() {
	LATE *module = new LATE();
	setModule(module);
	box.size = Vec(15*3, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/LATE.svg")));
		addChild(panel);
	}

	addParam(createParam<BidooBlueKnob>(Vec(9, 70), module, LATE::SWING_PARAM, 0, 10, 0));
	addInput(createInput<PJ301MPort>(Vec(10, 110), module, LATE::SWING_INPUT));

	addInput(createInput<PJ301MPort>(Vec(10, 173), module, LATE::RESET_INPUT));

	addInput(createInput<PJ301MPort>(Vec(10, 235), module, LATE::CLOCK_INPUT));

	addOutput(createOutput<PJ301MPort>(Vec(10, 299), module, LATE::CLOCK_OUTPUT));

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));
}
