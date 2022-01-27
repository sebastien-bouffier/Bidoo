#include "plugin.hpp"
#include "BidooComponents.hpp"
//#include "dsp/digital.hpp"

using namespace std;

struct SIGMA : BidooModule {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		IN_INPUT,
		NUM_INPUTS = IN_INPUT + 18
	};
	enum OutputIds {
		OUT_OUTPUT,
		NUM_OUTPUTS = OUT_OUTPUT + 6
	};
	enum LightIds {
		NUM_LIGHTS
	};

	SIGMA() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	}

	void process(const ProcessArgs &args) override {
		for (int i = 0; i < NUM_OUTPUTS; i++) {
			outputs[i].setVoltage(inputs[3 * i].getVoltage() + inputs[3 * i + 1].getVoltage() + inputs[3 * i + 2].getVoltage());
		}
	}
};

struct SIGMAWidget : BidooWidget {
	SIGMAWidget(SIGMA *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/SIGMA.svg"));

		for (int i = 0; i < SIGMA::NUM_OUTPUTS; i++) {
			addOutput(createOutput<TinyPJ301MPort>(Vec(15.0f + round(i / 3)*30.0f, 120.0f + (i % 3)*100.0f), module, i));
		}

		for (int i = 0; i < SIGMA::NUM_INPUTS; i++) {
			addInput(createInput<TinyPJ301MPort>(Vec(15.0f + round(i / 9)*30.0f, 50.0f + (i % 9)*20.0f + round((i % 9) / 3)*40.0f), module, i));
		}

	}
};


Model *modelSIGMA = createModel<SIGMA, SIGMAWidget>("SIGMA");
