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
		// the wider of IN and CLK; a mono IN or CLK feeds every channel
		int channels = std::max({1, inputs[CV_INPUT].getChannels(), inputs[CLOCK_INPUT].getChannels()});
		for (int c = 0; c < channels; c++) {
			if (sampleTrigger[c].process(inputs[CLOCK_INPUT].getPolyVoltage(c))) {
				for (int i = 5; i > 0; i--)
					stages[i][c] = stages[i - 1][c];
				stages[0][c] = inputs[CV_INPUT].getPolyVoltage(c);
			}
			for (int i = 0; i < 6; i++)
				outputs[ONE_OUTPUT + i].setVoltage(stages[i][c], c);
		}
		for (int i = 0; i < 6; i++)
			outputs[ONE_OUTPUT + i].setChannels(channels);
	}

	void onReset() override {
		for (int i = 0; i < 6; i++)
			for (int c = 0; c < PORT_MAX_CHANNELS; c++)
				stages[i][c] = 0.f;
		for (int c = 0; c < PORT_MAX_CHANNELS; c++)
			sampleTrigger[c].reset();
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
					// a damaged patch must not put any voltage on the outputs: finite, within Rack's +-12 V
					stages[i][c] = clamp(json_number_value(json_array_get(st, i * PORT_MAX_CHANNELS + c)), -12.f, 12.f);
	}
};

struct LAMBDAWidget : BidooWidget {
	LAMBDAWidget(LAMBDA *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/LAMBDA.svg"));

		// No screws, as before. The panel of tools/panels/lambda_.py: the old arrangement, eight boxes down the middle,
		// CLK and IN then the six outputs, one jack each on the panel's centre (they were 0.15 px left). The boxes keep the
		// theme's 25 px above the jack and 15 below, 2 px apart, so that eight fit under the title: 42 px from one jack to
		// the next (were 43.875), the last on the bottom row.
		const float w = box.size.x;
		const float kBoxBelow = 15.f, kBoxGap = 2.f;   // BOX_BELOW, BOX_GAP in tools/panels/lambda_.py
		const float kJackPitch = theme::kBoxAboveJack + kBoxBelow + kBoxGap;
		const theme::Pos last = theme::jackAt(0, 1, 0, w);
		auto jack = [&](int i) { return last.down(-kJackPitch * (7 - i)); };
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
