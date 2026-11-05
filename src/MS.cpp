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

	MS() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(L_INPUT, "Left");
		configInput(R_INPUT, "Right");
		configInput(M_INPUT, "Mid");
		configInput(S_INPUT, "Side");
		configOutput(M_OUTPUT, "Mid ((L+R)/2)");
		configOutput(S_OUTPUT, "Side ((L-R)/2)");
		configOutput(L_OUTPUT, "Left (M+S)");
		configOutput(R_OUTPUT, "Right (M-S)");
	}

	// Polyphonic: each channel on its own; a mono input reaches every channel.
	void process(const ProcessArgs &args) override {
		int enc = std::max(inputs[L_INPUT].getChannels(), inputs[R_INPUT].getChannels());
		for (int c = 0; c < enc; c++) {
			float l = inputs[L_INPUT].getPolyVoltage(c), r = inputs[R_INPUT].getPolyVoltage(c);
			outputs[M_OUTPUT].setVoltage(0.5f * (l + r), c);
			outputs[S_OUTPUT].setVoltage(0.5f * (l - r), c);
		}
		outputs[M_OUTPUT].setChannels(enc);
		outputs[S_OUTPUT].setChannels(enc);
		int dec = std::max(inputs[M_INPUT].getChannels(), inputs[S_INPUT].getChannels());
		for (int c = 0; c < dec; c++) {
			float m = inputs[M_INPUT].getPolyVoltage(c), s = inputs[S_INPUT].getPolyVoltage(c);
			outputs[L_OUTPUT].setVoltage(m + s, c);
			outputs[R_OUTPUT].setVoltage(m - s, c);
		}
		outputs[L_OUTPUT].setChannels(dec);
		outputs[R_OUTPUT].setChannels(dec);
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
