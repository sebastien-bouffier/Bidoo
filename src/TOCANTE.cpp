#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;


struct TOCANTE : BidooModule {
	enum ParamIds {
		BPM_PARAM,
		BPMFINE_PARAM,
		BEATS_PARAM,
		REF_PARAM,
		RUN_PARAM,
		RESET_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		BPM_INPUT,
		BPMFINE_INPUT,
		BEATS_INPUT,
		REF_INPUT,
		RUN_INPUT,
		RESET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_MEASURE,
		OUT_BEAT,
		OUT_TRIPLET,
		OUT_QUARTER,
		OUT_EIGHTH,
		OUT_SIXTEENTH,
		OUT_THIRTYSECOND,
		OUT_SIXTYFOURTH,
		OUT_ONEHUNDREDTWENTYEIGHTH,
		OUT_RESET,
		OUT_RUN,
		NUM_OUTPUTS
	};
	enum LightIds {
		RUNNING_LIGHT,
		RESET_LIGHT,
		NUM_LIGHTS
	};

	// Outputs that tick on a grid, OUT_MEASURE..OUT_ONEHUNDREDTWENTYEIGHTH.
	static const int NUM_GRIDS = OUT_ONEHUNDREDTWENTYEIGHTH + 1;

	// Shown by the displays.
	int ref = 4;
	int beats = 4;
	int count = 0;
	float bpm = 60.f;

	bool running = false;
	bool restart = false;
	// Position in the bar, in quarter notes. Kept across tempo changes.
	double position = 0.0;
	float resetLight = 0.f;

	dsp::SchmittTrigger runningTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::PulseGenerator pulses[NUM_GRIDS];
	dsp::PulseGenerator resetPulse;

	TOCANTE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(BPM_PARAM, 1.f, 350.f, 60.f, "BPM");
		configParam(BPMFINE_PARAM, 0.f, 0.99f, 0.f, "Fine");
		configParam(BEATS_PARAM,  1.f, 32.f, 4.f, "Beats per measure");
		configParam(REF_PARAM, 1.f, 4.f, 2.f, "Note value");
		configButton(RUN_PARAM, "Run");
		configButton(RESET_PARAM, "Reset");
		configInput(BPM_INPUT, "BPM (1V/oct, 0 V = 120 BPM, replaces the knob)");
		configInput(BPMFINE_INPUT, "Fine");
		configInput(BEATS_INPUT, "Beats per measure");
		configInput(REF_INPUT, "Note value");
		configOutput(OUT_MEASURE, "Measure");
		configOutput(OUT_BEAT, "Beat");
		configOutput(OUT_TRIPLET, "Eighth triplet");
		configOutput(OUT_QUARTER, "Quarter");
		configOutput(OUT_EIGHTH, "Eighth");
		configOutput(OUT_SIXTEENTH, "Sixteenth");
		configOutput(OUT_THIRTYSECOND, "Thirty-second");
		configOutput(OUT_SIXTYFOURTH, "Sixty-fourth");
		configOutput(OUT_ONEHUNDREDTWENTYEIGHTH, "enCORE clock (128 per quarter)");
		configInput(RUN_INPUT, "Run toggle trigger");
		configInput(RESET_INPUT, "Reset trigger");
		configOutput(OUT_RESET, "Reset");
		configOutput(OUT_RUN, "Run gate");
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "running", json_boolean(running));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *runningJ = json_object_get(rootJ, "running");
		if (runningJ) {
			running = json_is_true(runningJ);
			restart = running;
		}
	}

	void onReset() override {
		running = false;
		position = 0.0;
	}

	int lightCounter = 0;  // lights every 16 samples

	void process(const ProcessArgs &args) override;
};

void TOCANTE::process(const ProcessArgs &args) {
	ref = 1 << clamp((int)std::round(params[REF_PARAM].getValue()) + (int)rescale(clamp(inputs[REF_INPUT].getVoltage(), 0.f, 10.f), 0.f, 10.f, 0.f, 3.f), 1, 4);
	beats = clamp((int)(params[BEATS_PARAM].getValue() + rescale(clamp(inputs[BEATS_INPUT].getVoltage(), 0.f, 10.f), 0.f, 10.f, 0.f, 32.f)), 1, 32);
	float base = inputs[BPM_INPUT].isConnected() ? 120.f * std::exp2(clamp(inputs[BPM_INPUT].getVoltage(), -10.f, 10.f)) : std::round(params[BPM_PARAM].getValue());
	float fine = std::round(100.f * (params[BPMFINE_PARAM].getValue() + rescale(clamp(inputs[BPMFINE_INPUT].getVoltage(), 0.f, 10.f), 0.f, 10.f, 0.f, 0.99f))) * 0.01f;
	bpm = clamp(base + fine, 1.f, 350.f);

	// Grid of each output, in quarter notes. The bar and the beat follow the
	// note value; the enCORE clock ticks 128 times per quarter (32 per step).
	double beatLength = 4.0 / ref;
	double barLength = beats * beatLength;
	const double grids[NUM_GRIDS] = {barLength, beatLength, 1.0 / 3.0, 1.0, 1.0 / 2.0, 1.0 / 4.0, 1.0 / 8.0, 1.0 / 16.0, 1.0 / 128.0};
	double quartersPerSecond = bpm / 60.0 * beatLength;

	if (runningTrigger.process(params[RUN_PARAM].getValue() + inputs[RUN_INPUT].getVoltage())) {
		running = !running;
		if (running)
			restart = true;
	}
	if (resetTrigger.process(params[RESET_PARAM].getValue() + inputs[RESET_INPUT].getVoltage()))
		restart = true;

	bool fire[NUM_GRIDS] = {};
	if (restart) {
		restart = false;
		position = 0.0;
		resetPulse.trigger(1e-3f);
		resetLight = 1.f;
		if (running)
			std::fill(fire, fire + NUM_GRIDS, true);
	}
	else if (running) {
		double previous = position;
		position += quartersPerSecond * args.sampleTime;
		if (position >= barLength) {
			// Bar line: every grid starts again from here.
			position -= barLength;
			if (position >= barLength)
				position = 0.0;
			std::fill(fire, fire + NUM_GRIDS, true);
		}
		else {
			for (int i = 0; i < NUM_GRIDS; i++)
				fire[i] = std::floor(position / grids[i]) > std::floor(previous / grids[i]);
		}
	}
	else {
		// A bar length change while stopped must not leave the position past the end.
		if (position >= barLength)
			position = 0.0;
	}

	for (int i = 0; i < NUM_GRIDS; i++) {
		if (fire[i]) {
			// 1 ms, but never more than half the period so fast clocks still drop low.
			float period = grids[i] / quartersPerSecond;
			pulses[i].trigger(std::min(1e-3f, 0.5f * period));
		}
		bool high = pulses[i].process(args.sampleTime);
		outputs[i].setVoltage(running && high ? 10.f : 0.f);
	}
	outputs[OUT_RESET].setVoltage(resetPulse.process(args.sampleTime) ? 10.f : 0.f);
	outputs[OUT_RUN].setVoltage(running ? 10.f : 0.f);

	count = beats - std::min((int)(position / beatLength), beats - 1);
	resetLight = std::max(0.f, resetLight - args.sampleTime * 4.f);
	if (--lightCounter < 0) {
		lightCounter = 15;
		lights[RESET_LIGHT].setBrightness(resetLight);
		lights[RUNNING_LIGHT].setBrightness(running ? 1.f : 0.f);
	}
}

struct TOCANTEDisplay : TransparentWidget {
	TOCANTE *module;

	TOCANTEDisplay() {

	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module != NULL) {
				char tBPM[128],tBeats[128];
				snprintf(tBPM, sizeof(tBPM), "%1.2f BPM", module->bpm);
				snprintf(tBeats, sizeof(tBeats), "%1i/%1i", module->beats, module->ref);
				nvgFontSize(args.vg, 16.0f);
				nvgFillColor(args.vg, YELLOW_BIDOO);
				nvgText(args.vg, 0.0f, 0.0f, tBPM, NULL);
				nvgText(args.vg, 0.0f, 15.0f, tBeats, NULL);
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct TOCANTEMeasureDisplay : TransparentWidget {
	TOCANTE *module;

	TOCANTEMeasureDisplay() {

	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module != NULL) {
				char tSteps[128];
				snprintf(tSteps, sizeof(tSteps), "%1i", module->count);

				nvgFontSize(args.vg, 16.0f);
				nvgFillColor(args.vg, YELLOW_BIDOO);
				nvgTextAlign(args.vg, NVG_ALIGN_RIGHT);
				nvgText(args.vg, 0.0f, 0.0f, tSteps, NULL);
			}
		}
		Widget::drawLayer(args, layer);
	}

};

template <typename BASE>
struct TocanteLight : BASE {
	TocanteLight() {
		this->box.size = mm2px(Vec(6.f, 6.f));
	}
};

struct TOCANTEWidget : BidooWidget {
	TOCANTEWidget(TOCANTE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/TOCANTE.svg"));

		// No screws, as before. The original hand-drawn panel: the screen and the measure screen; three columns, each a
		// knob or button joined by the panel's line to its jack(s) below, under BPM / BEATS / BAR and FINE / RESET / RUN;
		// nine outputs in their boxes. The BEATS and BAR knobs and the RESET and RUN buttons now sit on their lines and
		// labels (the old code had them 0.9 to 1 px left); the rest keeps its old corners.
		const theme::Size kBezelSize = {21.26028f, 21.25987f};   // LEDBezel
		const theme::Size kLightSize = {mm2px(6.f), mm2px(6.f)};  // TocanteLight
		const float kLightIn = 1.8f;                             // from a bezel's corner to its light's
		const theme::Pos topLine[3] = {theme::specific(17.688f, 0), theme::specific(52.656f, 0), theme::specific(87.656f, 0)};   // the lines under BPM, BEATS, BAR
		const theme::Pos lowLine[3] = {theme::specific(17.531f, 0), theme::specific(52.531f, 0), theme::specific(87.5f, 0)};     // under FINE, RESET, RUN

		TOCANTEDisplay *display = new TOCANTEDisplay();
		display->module = module;
		display->box = theme::specificRect(11, 53, 50, 85);
		addChild(display);

		TOCANTEMeasureDisplay *mDisplay = new TOCANTEMeasureDisplay();
		mDisplay->module = module;
		mDisplay->box = theme::specificRect(92, 68, 25, 25);
		addChild(mDisplay);

		const theme::Pos reset = theme::colRow(lowLine[1], theme::specificCorner(0, 160, kBezelSize));
		const theme::Pos run = theme::colRow(lowLine[2], theme::specificCorner(0, 160, kBezelSize));
		addParam(theme::param<LEDBezel>(reset, module, TOCANTE::RESET_PARAM));
		addChild(theme::light<TocanteLight<BlueLight>>(theme::specificCorner(reset.x - kBezelSize.w * 0.5f + kLightIn, 160 + kLightIn, kLightSize), module, TOCANTE::RESET_LIGHT));
		addInput(theme::input<TinyPJ301MPort>(theme::specificCorner(36.3f, 190, theme::kTinyJackSize), module, TOCANTE::RESET_INPUT));
		addOutput(theme::output<TinyPJ301MPort>(theme::specificCorner(52.3f, 190, theme::kTinyJackSize), module, TOCANTE::OUT_RESET));

		addParam(theme::param<LEDBezel>(run, module, TOCANTE::RUN_PARAM));
		addChild(theme::light<TocanteLight<BlueLight>>(theme::specificCorner(run.x - kBezelSize.w * 0.5f + kLightIn, 160 + kLightIn, kLightSize), module, TOCANTE::RUNNING_LIGHT));
		addInput(theme::input<TinyPJ301MPort>(theme::specificCorner(71.3f, 190, theme::kTinyJackSize), module, TOCANTE::RUN_INPUT));
		addOutput(theme::output<TinyPJ301MPort>(theme::specificCorner(87.3f, 190, theme::kTinyJackSize), module, TOCANTE::OUT_RUN));

		addParam(theme::param<BidooBlueKnob>(theme::specificCorner(3, 90, theme::kBlueKnobSize), module, TOCANTE::BPM_PARAM));
		addParam(theme::param<BidooBlueKnob>(theme::specificCorner(3, 155, theme::kBlueKnobSize), module, TOCANTE::BPMFINE_PARAM));
		addParam(theme::param<BidooBlueSnapKnob>(theme::colRow(topLine[1], theme::specificCorner(0, 90, theme::kBlueKnobSize)), module, TOCANTE::BEATS_PARAM));
		addParam(theme::param<BidooBlueSnapKnob>(theme::colRow(topLine[2], theme::specificCorner(0, 90, theme::kBlueKnobSize)), module, TOCANTE::REF_PARAM));

		const float kColumnRight = 35.f;
		const theme::Pos bpmCv = theme::specificCorner(10, 125, theme::kTinyJackSize);
		addInput(theme::input<TinyPJ301MPort>(bpmCv, module, TOCANTE::BPM_INPUT));
		addInput(theme::input<TinyPJ301MPort>(theme::specificCorner(10, 190, theme::kTinyJackSize), module, TOCANTE::BPMFINE_INPUT));
		addInput(theme::input<TinyPJ301MPort>(bpmCv.right(kColumnRight), module, TOCANTE::BEATS_INPUT));
		addInput(theme::input<TinyPJ301MPort>(bpmCv.right(2 * kColumnRight), module, TOCANTE::REF_INPUT));

		const float kBoxBelow = 47.f;
		const float outLeft[3] = {7, 40.5f, 74};   // the three columns of boxed outputs, within 0.3 px of their boxes
		const int outIds[9] = {TOCANTE::OUT_MEASURE, TOCANTE::OUT_QUARTER, TOCANTE::OUT_EIGHTH, TOCANTE::OUT_BEAT, TOCANTE::OUT_TRIPLET,
			TOCANTE::OUT_SIXTEENTH, TOCANTE::OUT_THIRTYSECOND, TOCANTE::OUT_SIXTYFOURTH, TOCANTE::OUT_ONEHUNDREDTWENTYEIGHTH};
		for (int i = 0; i < 9; i++)
			addOutput(theme::output<PJ301MPort>(theme::specificCorner(outLeft[i / 3], 236 + kBoxBelow * (i % 3), theme::kJackSize), module, outIds[i]));
	}
};

Model *modelTOCANTE = createModel<TOCANTE, TOCANTEWidget>("tOCAnTe");
