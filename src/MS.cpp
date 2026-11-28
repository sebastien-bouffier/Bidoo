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

		// No screws, as before. The panel of tools/panels/ms.py: the old arrangement, four tall boxes down the middle, two
		// jacks each 40 px apart as before, on the panel's centre. The boxes are the theme's (25 px above the upper jack, 17
		// below the lower), 3 px apart, the last on the bottom row: the jacks moved 5 to 20 px down from their old places.
		const float w = box.size.x;
		const float kJackPitch = 40.f;   // ROW_GAP in tools/panels/ms.py
		const float kBoxGap = 3.f;       // BOX_GAP in tools/panels/ms.py
		const float kBoxPitch = theme::kBoxAboveJack + kJackPitch + theme::kBoxBelowJack + kBoxGap;
		const theme::Pos last = theme::jackAt(0, 1, 0, w);
		auto upper = [&](int box) { return last.down(-kJackPitch - kBoxPitch * (3 - box)); };
		addInput(theme::input<PJ301MPort>(upper(0), module, MS::L_INPUT));
		addInput(theme::input<PJ301MPort>(upper(0).down(kJackPitch), module, MS::R_INPUT));
		addOutput(theme::output<PJ301MPort>(upper(1), module, MS::M_OUTPUT));
		addOutput(theme::output<PJ301MPort>(upper(1).down(kJackPitch), module, MS::S_OUTPUT));

		addInput(theme::input<PJ301MPort>(upper(2), module, MS::M_INPUT));
		addInput(theme::input<PJ301MPort>(upper(2).down(kJackPitch), module, MS::S_INPUT));
		addOutput(theme::output<PJ301MPort>(upper(3), module, MS::L_OUTPUT));
		addOutput(theme::output<PJ301MPort>(last, module, MS::R_OUTPUT));
	}
};

Model *modelMS = createModel<MS, MSWidget>("MS");
