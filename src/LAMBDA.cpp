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
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	dsp::SchmittTrigger sampleTrigger;
	float one=0.f;
	float two=0.f;
	float three=0.f;

	LAMBDA() { config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS); }

	void process(const ProcessArgs &args) override {
		if (sampleTrigger.process(inputs[CLOCK_INPUT].getVoltage())) {
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

		addInput(createInput<PJ301MPort>(Vec(10.5f, 57), module, LAMBDA::CLOCK_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10.5f, 114), module, LAMBDA::CV_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(10.5f, 202.5f), module, LAMBDA::ONE_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10.5f, 257), module, LAMBDA::TWO_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10.5f, 311.5f), module, LAMBDA::THREE_OUTPUT));
	}
};

Model *modelLAMBDA = createModel<LAMBDA, LAMBDAWidget>("lambda");
