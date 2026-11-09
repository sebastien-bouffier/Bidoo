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
				configInput(3 * i + k, rack::string::f("Sum %d, input %d", i + 1, k + 1));
			configOutput(i, rack::string::f("Sum %d", i + 1));
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

		// No screws, as before. The original hand-drawn panel: two columns of three groups, three inputs in a black box
		// over their sum's output in a red one. Old corners kept: the columns sit within 0.4 px of the boxes' centres;
		// the inputs sit high in their boxes, which run down into the joint with the output, as on MS.
		const float kColumnRight = 30.f;   // from the left column to the right
		const float kGroupBelow = 100.f;   // from one group to the next
		const float kInputBelow = 20.f;    // from one input to the next
		const theme::Pos firstOut = theme::specificCorner(15, 120, theme::kTinyJackSize);
		const theme::Pos firstIn = theme::specificCorner(15, 50, theme::kTinyJackSize);
		for (int i = 0; i < SIGMA::NUM_OUTPUTS; i++)
			addOutput(theme::output<TinyPJ301MPort>(firstOut.offset(kColumnRight * (i / 3), kGroupBelow * (i % 3)), module, i));
		for (int i = 0; i < SIGMA::NUM_INPUTS; i++)
			addInput(theme::input<TinyPJ301MPort>(firstIn.offset(kColumnRight * (i / 9), kGroupBelow * ((i % 9) / 3) + kInputBelow * (i % 3)), module, i));
	}
};


Model *modelSIGMA = createModel<SIGMA, SIGMAWidget>("SIGMA");
