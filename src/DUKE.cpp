#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

struct DUKE : BidooModule {
	enum ParamIds {
		SLIDER_PARAM,
		ADONF_PARAM = SLIDER_PARAM + 4,
		NADA_PARAM,
		MIN_PARAM = NADA_PARAM + 4,
		MAX_PARAM = MIN_PARAM + 4,
		TYPE_PARAM = MAX_PARAM + 4,
		NUM_PARAMS = TYPE_PARAM + 4,
	};
	enum InputIds {
		SLIDER_INPUT,
		NUM_INPUTS = SLIDER_INPUT + 4
	};
	enum OutputIds {
		CV_OUTPUT,
		NUM_OUTPUTS = CV_OUTPUT + 4
	};
	enum LightIds {
		NUM_LIGHTS
	};

	dsp::SchmittTrigger aDonfTrigger;
	dsp::SchmittTrigger nadaTrigger;

	DUKE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(ADONF_PARAM, "All sliders to the top");
		configButton(NADA_PARAM, "All sliders to the bottom");
		for (int i = 0; i < 4; i++) {
			std::string n = std::to_string(i + 1);
			configParam(MAX_PARAM + i, 0.0f, 10.0f, 10.0f, "Max " + n, " V");
			configParam(MIN_PARAM + i, 0.0f, 10.0f, 0.0f, "Min " + n, " V");
			configSwitch(TYPE_PARAM + i, 0.0f, 1.0f, 0.0f, "Range " + n, {"0 to 10 V", "-5 to 5 V"});
			configParam(SLIDER_PARAM + i, 0.0f, 10.0f, 0.0f, "Slider " + n, "%", 0.f, 10.f);
			configInput(SLIDER_INPUT + i, "Slider " + n + " CV (added, 0-10 V: full travel; polyphonic)");
			configOutput(CV_OUTPUT + i, "CV " + n);
		}
	}

	void process(const ProcessArgs &args) override {
		if (aDonfTrigger.process(params[ADONF_PARAM].getValue())) {
			for (int i = 0; i < 4; i++)
				params[SLIDER_PARAM + i].setValue(10.f);
		}
		if (nadaTrigger.process(params[NADA_PARAM].getValue())) {
			for (int i = 0; i < 4; i++)
				params[SLIDER_PARAM + i].setValue(0.f);
		}
		for (int i = 0; i < 4; i++) {
			float offset = 5.0f * params[TYPE_PARAM + i].getValue();
			float min = params[MIN_PARAM + i].getValue() - offset;
			float max = params[MAX_PARAM + i].getValue() - offset;
			float slider = params[SLIDER_PARAM + i].getValue();
			// one output channel per channel of the slider's CV
			int channels = std::max(1, inputs[SLIDER_INPUT + i].getChannels());
			for (int c = 0; c < channels; c++)
				outputs[CV_OUTPUT + i].setVoltage(min + clamp(slider + inputs[SLIDER_INPUT + i].getVoltage(c), 0.0f, 10.0f) * (max - min) / 10.0f, c);
			outputs[CV_OUTPUT + i].setChannels(channels);
		}
	}
};

struct DUKEWidget : BidooWidget {
	DUKEWidget(DUKE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/DUKE.svg"));

		// No screws, as before. The original hand-drawn panel: the two buttons between their arrows on the left; four
		// columns of max and min knobs, a range switch, a slider joined by a line to its CV jack, and an OUT box. Old
		// corners kept: each column's controls sit within 0.4 px of their OUT box's centre and label (the slider's line
		// is drawn 0.65 px right of the jacks, hidden where it meets them).
		const float kColumnRight[4] = {0.f, 34.f, 69.f, 104.f};   // from the first column, as the old code had them
		const theme::Size kCKSSSize = {14.f, 20.64111f};            // CKSS
		const theme::Size kSliderSize = {19.84258f, 76.53516f};     // LEDSliderGreen
		const theme::Pos adonf = theme::specificCorner(7, 190, theme::kCKD6Size);
		addParam(theme::param<BlueCKD6>(adonf, module, DUKE::ADONF_PARAM));
		addParam(theme::param<BlueCKD6>(adonf.down(40.f), module, DUKE::NADA_PARAM));
		const theme::Pos max = theme::specificCorner(34, 51, theme::kBlueKnobSize);
		const theme::Pos type = theme::specificCorner(42, 139, kCKSSSize);
		const theme::Pos slider = theme::specificCorner(39, 184, kSliderSize);
		const theme::Pos cv = theme::specificCorner(37, 281, theme::kJackSize);
		for (int i = 0; i < 4; i++) {
			const float dx = kColumnRight[i];
			addParam(theme::param<BidooBlueKnob>(max.right(dx), module, DUKE::MAX_PARAM + i));
			addParam(theme::param<BidooBlueKnob>(max.offset(dx, 44.f), module, DUKE::MIN_PARAM + i));
			addParam(theme::param<CKSS>(type.right(dx), module, DUKE::TYPE_PARAM + i));
			addParam(theme::param<LEDSliderGreen>(slider.right(dx), module, DUKE::SLIDER_PARAM + i));
			addInput(theme::input<PJ301MPort>(cv.right(dx), module, DUKE::SLIDER_INPUT + i));
			addOutput(theme::output<PJ301MPort>(cv.offset(dx, 49.f), module, DUKE::CV_OUTPUT + i));
		}
	}
};

Model *modelDUKE = createModel<DUKE, DUKEWidget>("dUKe");
