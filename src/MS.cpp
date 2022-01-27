#include "plugin.hpp"
#include "BidooComponents.hpp"

using namespace std;

struct MS : BidooModule {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		L_INPUT,
		R_INPUT,
		M_INPUT,
		S_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		L_OUTPUT,
		R_OUTPUT,
		M_OUTPUT,
		S_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	MS() { config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS); }

	void process(const ProcessArgs &args) override {
		outputs[S_OUTPUT].setVoltage(0.5f * (inputs[L_INPUT].getVoltage() - inputs[R_INPUT].getVoltage()));
		outputs[M_OUTPUT].setVoltage(0.5f * (inputs[L_INPUT].getVoltage() + inputs[R_INPUT].getVoltage()));
		outputs[L_OUTPUT].setVoltage(inputs[M_INPUT].getVoltage() + inputs[S_INPUT].getVoltage());
		outputs[R_OUTPUT].setVoltage(inputs[M_INPUT].getVoltage() - inputs[S_INPUT].getVoltage());
	}
};

struct MSWidget : BidooWidget {
	MSWidget(MS *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/MS.svg"));

		addInput(createInput<PJ301MPort>(Vec(10, 30), module, MS::L_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10, 70), module, MS::R_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10, 110), module, MS::M_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10, 150), module, MS::S_OUTPUT));

		addInput(createInput<PJ301MPort>(Vec(10, 190), module, MS::M_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10, 230), module, MS::S_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10, 270), module, MS::L_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10, 310), module, MS::R_OUTPUT));
	}
};

Model *modelMS = createModel<MS, MSWidget>("MS");
