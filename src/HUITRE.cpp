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
		json_object_set_new(rootJ, "currentPattern", json_integer(currentPattern));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *modeJ = json_object_get(rootJ, "mode");
		if (modeJ) mode = json_is_true(modeJ);
		if (json_t *patJ = json_object_get(rootJ, "currentPattern"))
			currentPattern = nextPattern = clamp((int)json_integer_value(patJ), 0, 8 - 1);
	}

	void onReset() override {
		currentPattern = nextPattern = 0;
		mode = false;
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

HUITREWidget::HUITREWidget(HUITRE *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/HUITRE.svg"));

	theme::addScrews(this);

	// The panel of tools/panels/huitre.py: the gate mode button under the title; eight rows of a pattern button, the
	// PTN, CV1 and CV2 trimpots and a tiny gate out in the GATE strip; STEP and the three outputs in their grid boxes.
	const float w = box.size.x;
	const int n = 4;   // the bottom row's columns, the same in huitre.py

	// The buttons are the house round button, momentary as before. The pattern buttons over STEP's column (they were
	// 3.6 px left); each lit (orange) while its pattern plays and blinking while it waits for the next measure, from
	// the module's lights (green and red on the old bezels).
	auto lit = [=](int lightId) { return module && module->lights[lightId].getBrightness() > 0.5f; };
	auto button = [&](theme::Pos p, int paramId, std::function<bool()> litBy) {
		BidooRoundButton *b = theme::param<BidooRoundButton>(p, module, paramId);
		b->momentary = true;
		b->litBy = litBy;
		addParam(b);
	};
	const theme::Pos stepCol = theme::jackAt(0, n, 0, w);
	// the mode button, lit while the pattern outs give gates (green on the old light, blue for triggers): 7 px lower
	// than before, so that its label fits under the screw. kModeY is the same in huitre.py (MODE_Y)
	const float kModeY = 16.5f + theme::kCaptionCapHeight + theme::kSmallLabelAbove;
	button(theme::sameCol(stepCol, kModeY), HUITRE::MODE_PARAM, [=]() { return lit(HUITRE::MODE_LIGHT + 1); });

	// the rows: 4 px lower than before (kRowsDown, the room for the mode button's label), 33 px apart; each from the
	// PTN trimpot's old corner, the gate outs at their old column (the old buttons were 1 px above the trimpots' row,
	// the jacks 0.9 px below)
	const float kRowsDown = 4.f;       // the same in huitre.py (ROWS_DOWN)
	const float kRowBelow = 33.f;
	const float kTrimpotRight = 27.f;  // from one trimpot to the next
	const theme::Pos ptn = theme::specificCorner(45, 52, theme::kTrimpotSize).down(kRowsDown);
	const theme::Pos gate = theme::specificCorner(130, 0, theme::kTinyJackSize);
	for (int i = 0; i < 8; i++) {
		const theme::Pos row = ptn.down(kRowBelow * i);
		const int playing = HUITRE::PATTERN_LIGHT + i * 3 + 1, waiting = HUITRE::PATTERN_LIGHT + i * 3;
		button(theme::colRow(stepCol, row), HUITRE::TRIG_PARAM + i,
			[=]() { return lit(playing) || (lit(waiting) && std::fmod(system::getTime(), 0.5) < 0.25); });
		addParam(theme::param<BidooBlueTrimpot>(row, module, HUITRE::PATTERN_PARAM+i));
		addParam(theme::param<BidooBlueTrimpot>(row.right(kTrimpotRight), module, HUITRE::CV1_PARAM+i));
		addParam(theme::param<BidooBlueTrimpot>(row.right(2.f * kTrimpotRight), module, HUITRE::CV2_PARAM+i));
		// tiny: eight full jacks 33 px apart would not fit their column between CV2 and the panel's edge
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(gate, row), module, HUITRE::PATTERNTRIG_OUTPUT+i));
	}

	// STEP and the three outputs on the grid (the old jacks were 6.1, 2.3, 2.5 and 6.2 px off these columns)
	addInput(theme::input<PJ301MPort>(theme::jackAt(0, n, 0, w), module, HUITRE::MEASURE_INPUT));
	addOutput(theme::output<PJ301MPort>(theme::jackAt(1, n, 0, w), module, HUITRE::PATTERN_OUTPUT));
	addOutput(theme::output<PJ301MPort>(theme::jackAt(2, n, 0, w), module, HUITRE::CV1_OUTPUT));
	addOutput(theme::output<PJ301MPort>(theme::jackAt(3, n, 0, w), module, HUITRE::CV2_OUTPUT));
}

Model *modelHUITRE = createModel<HUITRE, HUITREWidget>("HUITre");
