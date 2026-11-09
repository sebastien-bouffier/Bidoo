#include "plugin.hpp"
#include "BidooComponents.hpp"

using namespace std;

struct LAMBDA : BidooModule {
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

	dsp::SchmittTrigger sampleTrigger[PORT_MAX_CHANNELS];
	// six stages per channel, saved with the patch
	float stages[6][PORT_MAX_CHANNELS] = {};

	LAMBDA() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(CLOCK_INPUT, "Clock (polyphonic: one per channel)");
		configInput(CV_INPUT, "CV (polyphonic)");
		for (int i = 0; i < 6; i++)
			configOutput(ONE_OUTPUT + i, "Stage " + std::to_string(i + 1));
	}

	void process(const ProcessArgs &args) override {
		int channels = std::max(1, inputs[CV_INPUT].getChannels());
		for (int c = 0; c < channels; c++) {
			if (sampleTrigger[c].process(inputs[CLOCK_INPUT].getPolyVoltage(c))) {
				for (int i = 5; i > 0; i--)
					stages[i][c] = stages[i - 1][c];
				stages[0][c] = inputs[CV_INPUT].getVoltage(c);
			}
			for (int i = 0; i < 6; i++)
				outputs[ONE_OUTPUT + i].setVoltage(stages[i][c], c);
		}
		for (int i = 0; i < 6; i++)
			outputs[ONE_OUTPUT + i].setChannels(channels);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_t *st = json_array();
		for (int i = 0; i < 6; i++)
			for (int c = 0; c < PORT_MAX_CHANNELS; c++)
				json_array_append_new(st, json_real(stages[i][c]));
		json_object_set_new(rootJ, "stages", st);
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *st = json_object_get(rootJ, "stages");
		if (st)
			for (int i = 0; i < 6; i++)
				for (int c = 0; c < PORT_MAX_CHANNELS; c++)
					stages[i][c] = json_number_value(json_array_get(st, i * PORT_MAX_CHANNELS + c));
	}
};

struct LAMBDAWidget : BidooWidget {
	LAMBDAWidget(LAMBDA *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/LAMBDA.svg"));

		// No screws, as before. The original hand-drawn panel: eight boxes down the middle, 43.875 px apart. Each jack sits
		// at the same height in its box, under its label (the old code had them 1.5 px up to 1.25 px down from that).
		// The column keeps its old x: between the black boxes' labels (21.75) and the red ones' (22.5), which the panel
		// draws 0.75 px apart.
		const float kBoxPitch = 43.875f;
		const theme::Pos first = theme::specificCorner(10.5f, 29.5f, theme::kJackSize);
		auto jack = [&](int i) { return first.down(kBoxPitch * i); };
		addInput(theme::input<PJ301MPort>(jack(0), module, LAMBDA::CLOCK_INPUT));
		addInput(theme::input<PJ301MPort>(jack(1), module, LAMBDA::CV_INPUT));

		addOutput(theme::output<PJ301MPort>(jack(2), module, LAMBDA::ONE_OUTPUT));
		addOutput(theme::output<PJ301MPort>(jack(3), module, LAMBDA::TWO_OUTPUT));
		addOutput(theme::output<PJ301MPort>(jack(4), module, LAMBDA::THREE_OUTPUT));
		addOutput(theme::output<PJ301MPort>(jack(5), module, LAMBDA::FOUR_OUTPUT));
		addOutput(theme::output<PJ301MPort>(jack(6), module, LAMBDA::FIVE_OUTPUT));
		addOutput(theme::output<PJ301MPort>(jack(7), module, LAMBDA::SIX_OUTPUT));
	}
};

Model *modelLAMBDA = createModel<LAMBDA, LAMBDAWidget>("lambda");
