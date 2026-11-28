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

		// No screws, as before. The panel of tools/panels/sigma.py: the old arrangement, two columns of three groups, three
		// inputs in a black box over their sum's output in a red one. Tiny jacks: twelve full jacks in their boxes don't fit
		// one above the other. The columns on the grid (n = 2): the jacks in their boxes' centre, 0.25 px nearer the centre.
		// The outputs boxed as any jack, the last group's on the bottom row, the groups 112 px apart (they were 100: the
		// first 9.5 px higher, the second 2.5 px and the third 14.5 px lower); the inputs 19 px apart (they were 20), the
		// last 41 px above its output, in their box 4 px above the output's.
		const float w = box.size.x;
		const int n = 2;
		const float kGroupPitch = 112.f;     // GROUP_PITCH in tools/panels/sigma.py
		const float kInputPitch = 19.f;      // IN_PITCH in tools/panels/sigma.py
		const float kLastInputAbove = theme::kBoxAboveJack + theme::kBoxGap + 12.f;   // LAST_IN_ABOVE in tools/panels/sigma.py
		for (int i = 0; i < SIGMA::NUM_OUTPUTS; i++) {
			const theme::Pos out = theme::jackAt(i / 3, n, 0, w).down(-kGroupPitch * (2 - i % 3));
			addOutput(theme::output<TinyPJ301MPort>(out, module, i));
			for (int m = 0; m < 3; m++)
				addInput(theme::input<TinyPJ301MPort>(out.down(-kLastInputAbove - kInputPitch * (2 - m)), module, 9 * (i / 3) + 3 * (i % 3) + m));
		}
	}
};


Model *modelSIGMA = createModel<SIGMA, SIGMAWidget>("SIGMA");
