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
		for (int i = 0; i < NUM_OUTPUTS; i++) {
			for (int k = 0; k < 3; k++)
				configInput(3 * i + k, string::f("Sum %d, input %d", i + 1, k + 1));
			configOutput(i, string::f("Sum %d", i + 1));
		}
	}

	// Polyphonic: each output has as many channels as its widest input; a
	// mono input is added to every channel.
	void process(const ProcessArgs &args) override {
		for (int i = 0; i < NUM_OUTPUTS; i++) {
			int channels = 0;
			for (int k = 0; k < 3; k++)
				channels = std::max(channels, inputs[3 * i + k].getChannels());
			for (int c = 0; c < channels; c++) {
				float sum = 0.f;
				for (int k = 0; k < 3; k++)
					sum += inputs[3 * i + k].getPolyVoltage(c);
				outputs[i].setVoltage(sum, c);
			}
			outputs[i].setChannels(channels);
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
