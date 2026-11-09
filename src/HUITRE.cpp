#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <sstream>
#include <iomanip>

using namespace std;

struct HUITRE : BidooModule {
	enum ParamIds {
		TRIG_PARAM,
		PATTERN_PARAM = TRIG_PARAM + 8,
		CV1_PARAM = PATTERN_PARAM + 8,
		CV2_PARAM = CV1_PARAM + 8,
		MODE_PARAM = CV2_PARAM + 8,
		NUM_PARAMS
	};
	enum InputIds {
		MEASURE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		PATTERN_OUTPUT,
		CV1_OUTPUT,
		CV2_OUTPUT,
		PATTERNTRIG_OUTPUT,
		NUM_OUTPUTS = PATTERNTRIG_OUTPUT + 8
	};
	enum LightIds {
		PATTERN_LIGHT,
		MODE_LIGHT = PATTERN_LIGHT+16*3,
		NUM_LIGHTS =  MODE_LIGHT + 3
	};

	dsp::SchmittTrigger patTriggers[8];
	dsp::SchmittTrigger syncTrigger;
	dsp::SchmittTrigger modeTrigger;
	int currentPattern = 0;
	int nextPattern = 0;
	dsp::PulseGenerator gatePulse;
	bool pulse=false;
	bool mode = false;

	dsp::ClockDivider lightDivider;

	HUITRE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		lightDivider.setDivision(16);
		configButton(MODE_PARAM, "Pattern outputs: trigger or gate");
		for (int i = 0; i < 8; i++) {
			std::string n = "Pattern " + std::to_string(i + 1);
			configButton(TRIG_PARAM+i, "Select " + n);
			configParam(PATTERN_PARAM+i, 0.0f, 10.0f,0.00f+ i*10.0f/8.0f, n + " CV", " V");
			configParam(CV1_PARAM+i, 0.0f, 10.0f, 0.0f, n + " CV 1", " V");
			configParam(CV2_PARAM+i, 0.0f, 10.0f, 0.0f, n + " CV 2", " V");
			configOutput(PATTERNTRIG_OUTPUT+i, n + " trigger (or gate)");
		}
		configInput(MEASURE_INPUT, "Measure (the selection changes on it; unpatched: at once)");
		configOutput(PATTERN_OUTPUT, "Pattern CV");
		configOutput(CV1_OUTPUT, "CV 1");
		configOutput(CV2_OUTPUT, "CV 2");
  }

  void process(const ProcessArgs &args) override;


	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "mode", json_boolean(mode));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *modeJ = json_object_get(rootJ, "mode");
		if (modeJ) mode = json_is_true(modeJ);
	}
};

void HUITRE::process(const ProcessArgs &args) {
	for (int i = 0; i < 8; i++) {
		if (patTriggers[i].process(params[TRIG_PARAM+i].getValue())) {
			nextPattern = i;
		}
	}

	if (modeTrigger.process(params[MODE_PARAM].getValue())) {
		mode=!mode;
	}

	bool lightsNow = lightDivider.process();
	if (lightsNow) {
		lights[MODE_LIGHT].setBrightness(0);
		lights[MODE_LIGHT + 1].setBrightness(mode ? 1 : 0);
		lights[MODE_LIGHT + 2].setBrightness(mode ? 0 : 1);
	}

	bool measure = inputs[MEASURE_INPUT].isConnected() ? syncTrigger.process(inputs[MEASURE_INPUT].getVoltage()) : true;
	if (measure && (currentPattern != nextPattern)) {
		currentPattern = nextPattern;
		gatePulse.trigger(1e-3f);
	}
	pulse = gatePulse.process(args.sampleTime);

	for (int i = 0; i < 8; i++) {
		if (i==currentPattern) {
			if (lightsNow) {
				lights[PATTERN_LIGHT+(i*3)].setBrightness(0.0f);
				lights[PATTERN_LIGHT+(i*3)+1].setBrightness(1.0f);
				lights[PATTERN_LIGHT+(i*3)+2].setBrightness(0.0f);
			}

			if (mode) {
				outputs[PATTERNTRIG_OUTPUT+currentPattern].setVoltage(10.0f);
			}
			else if (pulse) {
				outputs[PATTERNTRIG_OUTPUT+currentPattern].setVoltage(10.0f);
			}
			else {
				outputs[PATTERNTRIG_OUTPUT+i].setVoltage(0.0f);
			}
		}
		else if (i==nextPattern) {
			if (lightsNow) {
				lights[PATTERN_LIGHT+(i*3)].setBrightness(1.0f);
				lights[PATTERN_LIGHT+(i*3)+1].setBrightness(0.0f);
				lights[PATTERN_LIGHT+(i*3)+2].setBrightness(0.0f);
			}
		}
		else {
			if (lightsNow) {
				lights[PATTERN_LIGHT+(i*3)].setBrightness(0.0f);
				lights[PATTERN_LIGHT+(i*3)+1].setBrightness(0.0f);
				lights[PATTERN_LIGHT+(i*3)+2].setBrightness(0.0f);
			}
			outputs[PATTERNTRIG_OUTPUT+i].setVoltage(0.0f);
		}
	}

	outputs[PATTERN_OUTPUT].setVoltage(params[PATTERN_PARAM+currentPattern].getValue());
	outputs[CV1_OUTPUT].setVoltage(params[CV1_PARAM+currentPattern].getValue());
	outputs[CV2_OUTPUT].setVoltage(params[CV2_PARAM+currentPattern].getValue());
}

struct HUITREWidget : BidooWidget {
  HUITREWidget(HUITRE *module);
};

template <typename BASE>
struct HUITRELight : BASE {
	HUITRELight() {
		this->box.size = mm2px(Vec(6.0f, 6.0f));
	}
};

HUITREWidget::HUITREWidget(HUITRE *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/HUITRE.svg"));

	theme::addScrews(this);

	// The original hand-drawn panel: the mode button under GATE/TRIG; eight rows of a step button, the PTN, CV1 and CV2
	// trimpots under their labels and a gate jack in the GATE column; STEP and the three outputs in their boxes. Each
	// row now shares the trimpots' row (the old code had the button 1 px above it and the jack 0.9 px below), and the
	// bottom jacks sit on their boxes' centres (up to 0.6 px off). Columns keep their old corners.
	const theme::Size kButtonSize = {17.9996f, 18.f};                 // LEDButton
	const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};     // SmallLight
	const theme::Size kBezelSize = {21.26028f, 21.25987f};            // LEDBezel
	const theme::Size kStepLightSize = {mm2px(6.f), mm2px(6.f)};      // HUITRELight
	const float kLightIn = 6.f;      // from the mode button's corner to its light's
	const float kBezelLightIn = 1.8f;
	const float kRowBelow = 33.f;
	addParam(theme::param<LEDButton>(theme::specificCorner(13, 24, kButtonSize), module, HUITRE::MODE_PARAM));
	addChild(theme::light<SmallLight<RedGreenBlueLight>>(theme::specificCorner(13 + kLightIn, 24 + kLightIn, kSmallLightSize), module, HUITRE::MODE_LIGHT));

	const theme::Pos boxCol[4] = {theme::specific(19.156f, 0), theme::specific(56.438f, 0), theme::specific(93.719f, 0), theme::specific(130.969f, 0)};   // the boxes' centres
	const theme::Pos ioRow = theme::specificCorner(7, 330, theme::kJackSize);
	addInput(theme::input<PJ301MPort>(theme::colRow(boxCol[0], ioRow), module, HUITRE::MEASURE_INPUT));
	addOutput(theme::output<PJ301MPort>(theme::colRow(boxCol[1], ioRow), module, HUITRE::PATTERN_OUTPUT));
	addOutput(theme::output<PJ301MPort>(theme::colRow(boxCol[2], ioRow), module, HUITRE::CV1_OUTPUT));
	addOutput(theme::output<PJ301MPort>(theme::colRow(boxCol[3], ioRow), module, HUITRE::CV2_OUTPUT));

	const theme::Pos ptn = theme::specificCorner(45, 52, theme::kTrimpotSize);
	const float kTrimpotRight = 27.f;
	for (int i = 0; i < 8; i++) {
		const theme::Pos row = ptn.down(kRowBelow * i);
		const theme::Pos step = theme::colRow(theme::specificCorner(11, 0, kBezelSize), theme::specific(0, row.y));
		addParam(theme::param<LEDBezel>(step, module, HUITRE::TRIG_PARAM + i));
		addChild(theme::light<HUITRELight<RedGreenBlueLight>>(theme::specificCorner(11 + kBezelLightIn, step.y - kBezelSize.h * 0.5f + kBezelLightIn, kStepLightSize), module, HUITRE::PATTERN_LIGHT + i*3));
		addParam(theme::param<BidooBlueTrimpot>(row, module, HUITRE::PATTERN_PARAM+i));
		addParam(theme::param<BidooBlueTrimpot>(row.right(kTrimpotRight), module, HUITRE::CV1_PARAM+i));
		addParam(theme::param<BidooBlueTrimpot>(row.right(2.f * kTrimpotRight), module, HUITRE::CV2_PARAM+i));
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(theme::specificCorner(130, 0, theme::kTinyJackSize), theme::specific(0, row.y)), module, HUITRE::PATTERNTRIG_OUTPUT+i));
	}
}

Model *modelHUITRE = createModel<HUITRE, HUITREWidget>("HUITre");
