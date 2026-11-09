#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

// HUITre with 16 slots. The default PTN voltages sit 0.3 V inside the 0.625 V of a
// pattern: slot n is pattern n on the sequencers that round the CV and on those that floor it.
struct SAISON : BidooModule {
	static constexpr int NUM_SLOTS = 16;

	enum ParamIds {
		TRIG_PARAM,
		PATTERN_PARAM = TRIG_PARAM + NUM_SLOTS,
		CV1_PARAM = PATTERN_PARAM + NUM_SLOTS,
		CV2_PARAM = CV1_PARAM + NUM_SLOTS,
		MODE_PARAM = CV2_PARAM + NUM_SLOTS,
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
		NUM_OUTPUTS = PATTERNTRIG_OUTPUT + NUM_SLOTS
	};
	enum LightIds {
		PATTERN_LIGHT,
		MODE_LIGHT = PATTERN_LIGHT + NUM_SLOTS*3,
		NUM_LIGHTS = MODE_LIGHT + 3
	};

	dsp::SchmittTrigger patTriggers[NUM_SLOTS];
	dsp::SchmittTrigger syncTrigger;
	dsp::SchmittTrigger modeTrigger;
	int currentPattern = 0;
	int nextPattern = 0;
	dsp::PulseGenerator gatePulse;
	bool mode = false;

	dsp::ClockDivider lightDivider;

	SAISON() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		lightDivider.setDivision(16);
		configButton(MODE_PARAM, "Pattern outputs: trigger or gate");
		for (int i = 0; i < NUM_SLOTS; i++) {
			std::string n = "Pattern " + std::to_string(i + 1);
			configButton(TRIG_PARAM+i, "Select " + n);
			configParam(PATTERN_PARAM+i, 0.0f, 10.0f, 0.625f * i + 0.3f, n + " CV", " V");
			configParam(CV1_PARAM+i, 0.0f, 10.0f, 0.0f, n + " CV 1", " V");
			configParam(CV2_PARAM+i, 0.0f, 10.0f, 0.0f, n + " CV 2", " V");
			configOutput(PATTERNTRIG_OUTPUT+i, n + " trigger (or gate)");
		}
		configInput(MEASURE_INPUT, "Measure (the selection changes on it; unpatched: at once)");
		configOutput(PATTERN_OUTPUT, "Pattern CV (0.625 V per pattern)");
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

void SAISON::process(const ProcessArgs &args) {
	for (int i = 0; i < NUM_SLOTS; i++) {
		if (patTriggers[i].process(params[TRIG_PARAM+i].getValue())) {
			nextPattern = i;
		}
	}

	if (modeTrigger.process(params[MODE_PARAM].getValue())) {
		mode = !mode;
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
	const bool pulse = gatePulse.process(args.sampleTime);

	for (int i = 0; i < NUM_SLOTS; i++) {
		const bool current = i == currentPattern;
		if (lightsNow) {
			lights[PATTERN_LIGHT+(i*3)].setBrightness(!current && i == nextPattern ? 1.0f : 0.0f);
			lights[PATTERN_LIGHT+(i*3)+1].setBrightness(current ? 1.0f : 0.0f);
			lights[PATTERN_LIGHT+(i*3)+2].setBrightness(0.0f);
		}
		outputs[PATTERNTRIG_OUTPUT+i].setVoltage(current && (mode || pulse) ? 10.0f : 0.0f);
	}

	outputs[PATTERN_OUTPUT].setVoltage(params[PATTERN_PARAM+currentPattern].getValue());
	outputs[CV1_OUTPUT].setVoltage(params[CV1_PARAM+currentPattern].getValue());
	outputs[CV2_OUTPUT].setVoltage(params[CV2_PARAM+currentPattern].getValue());
}

struct SAISONWidget : BidooWidget {
	SAISONWidget(SAISON *module);
};

template <typename BASE>
struct SAISONLight : BASE {
	SAISONLight() {
		this->box.size = mm2px(Vec(6.0f, 6.0f));
	}
};

SAISONWidget::SAISONWidget(SAISON *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/SAISON.svg"));

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	addParam(createParam<LEDButton>(Vec(13.0f, 36.f), module, SAISON::MODE_PARAM));
	addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(13.0f+6.0f, 36.f+6.0f), module, SAISON::MODE_LIGHT));

	addInput(createInput<PJ301MPort>(Vec(82, 330), module, SAISON::MEASURE_INPUT));
	addOutput(createOutput<PJ301MPort>(Vec(119.0f, 330), module, SAISON::PATTERN_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(156.5f, 330), module, SAISON::CV1_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(193.5f, 330), module, SAISON::CV2_OUTPUT));

	// Two columns of eight.
	for (int i = 0; i < SAISON::NUM_SLOTS; i++) {
		const float x = i < 8 ? 0.0f : 150.0f;
		const float y = 12 + (i % 8) * 32;
		addParam(createParam<LEDBezel>(Vec(x + 11.0f, 50 + y), module, SAISON::TRIG_PARAM + i));
		addChild(createLight<SAISONLight<RedGreenBlueLight>>(Vec(x + 12.8f, 51.8f + y), module, SAISON::PATTERN_LIGHT + i*3));
		addParam(createParam<BidooBlueTrimpot>(Vec(x + 45, 52 + y), module, SAISON::PATTERN_PARAM+i));
		addParam(createParam<BidooBlueTrimpot>(Vec(x + 72, 52 + y), module, SAISON::CV1_PARAM+i));
		addParam(createParam<BidooBlueTrimpot>(Vec(x + 99, 52 + y), module, SAISON::CV2_PARAM+i));
		addOutput(createOutput<TinyPJ301MPort>(Vec(x + 130, 55 + y), module, SAISON::PATTERNTRIG_OUTPUT+i));
	}
}

Model *modelSAISON = createModel<SAISON, SAISONWidget>("SAISON");
