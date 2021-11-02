#include "plugin.hpp"
#include "BidooComponents.hpp"

using namespace std;

struct LAMBDA : Module {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		CV_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		ONE_OUTPUT,
		TWO_OUTPUT,
		THREE_OUTPUT,
		FOUR_OUTPUT,
		FIVE_OUTPUT,
		SIX_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	dsp::SchmittTrigger sampleTrigger;

	LAMBDA() { config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS); }

	void process(const ProcessArgs &args) override {
		if (sampleTrigger.process(inputs[CLOCK_INPUT].getVoltage())) {
			outputs[SIX_OUTPUT].setVoltage(outputs[FIVE_OUTPUT].getVoltage());
			outputs[FIVE_OUTPUT].setVoltage(outputs[FOUR_OUTPUT].getVoltage());
			outputs[FOUR_OUTPUT].setVoltage(outputs[THREE_OUTPUT].getVoltage());
			outputs[THREE_OUTPUT].setVoltage(outputs[TWO_OUTPUT].getVoltage());
			outputs[TWO_OUTPUT].setVoltage(outputs[ONE_OUTPUT].getVoltage());
			outputs[ONE_OUTPUT].setVoltage(inputs[CV_INPUT].getVoltage());
		}
	}
};

struct LAMBDAWidget : ModuleWidget {
	LAMBDAWidget(LAMBDA *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LAMBDA.svg")));

		addInput(createInput<PJ301MPort>(Vec(10.5f, 31), module, LAMBDA::CLOCK_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10.5f, 74), module, LAMBDA::CV_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(10.5f, 116.0f), module, LAMBDA::ONE_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10.5f, 161.0f), module, LAMBDA::TWO_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10.5f, 205.0f), module, LAMBDA::THREE_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10.5f, 249.0f), module, LAMBDA::FOUR_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10.5f, 293.0f), module, LAMBDA::FIVE_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10.5f, 336.0f), module, LAMBDA::SIX_OUTPUT));
	}
};

Model *modelLAMBDA = createModel<LAMBDA, LAMBDAWidget>("lambda");
