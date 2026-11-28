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
	float bpmVolts = 0.f, bpmFromVolts = 120.f;   // BPM CV: exp2 only when it moves

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
		configParam(BPM_PARAM, 1.f, 350.f, 60.f, "BPM")->snapEnabled = true;
		configParam(BPMFINE_PARAM, 0.f, 0.99f, 0.f, "Fine");
		configParam(BEATS_PARAM,  1.f, 32.f, 4.f, "Beats per measure")->snapEnabled = true;
		configSwitch(REF_PARAM, 1.f, 4.f, 2.f, "Note value", {"/2", "/4", "/8", "/16"});
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
		configOutput(OUT_ONEHUNDREDTWENTYEIGHTH, "/128: 128 pulses per quarter, 32 per 16th-note step, the clock of zOù MAï's Fast clock mode");
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
	float base;
	if (inputs[BPM_INPUT].isConnected()) {
		float v = clamp(inputs[BPM_INPUT].getVoltage(), -10.f, 10.f);
		if (v != bpmVolts) {
			bpmVolts = v;
			bpmFromVolts = 120.f * std::exp2(v);
		}
		base = bpmFromVolts;
	}
	else
		base = std::round(params[BPM_PARAM].getValue());
	float fine = std::round(100.f * (params[BPMFINE_PARAM].getValue() + rescale(clamp(inputs[BPMFINE_INPUT].getVoltage(), 0.f, 10.f), 0.f, 10.f, 0.f, 0.99f))) * 0.01f;
	bpm = clamp(base + fine, 1.f, 350.f);

	// Grid of each output, in quarter notes. The bar and the beat follow the
	// note value; /128 ticks 128 times per quarter (32 per 16th-note step), for zOu MAi's Fast clock mode.
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

struct TOCANTEWidget : BidooWidget {
	TOCANTEWidget(TOCANTE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/TOCANTE.svg"));

		// No screws, as before. The panel of tools/panels/tocante.py: the screen; three columns, each a knob or button
		// joined by the panel's line to its jack(s) below, at their old rows (specific: the panel's picture of the
		// module); the nine outputs in their grid boxes.
		const float w = box.size.x;

		// the displays where they were, in the panel's screen (which keeps its old margin of 5 for them)
		TOCANTEDisplay *display = new TOCANTEDisplay();
		display->module = module;
		display->box = theme::specificRect(11, 53, 50, 85);
		addChild(display);

		TOCANTEMeasureDisplay *mDisplay = new TOCANTEMeasureDisplay();
		mDisplay->module = module;
		mDisplay->box = theme::specificRect(92, 68, 25, 25);
		addChild(mDisplay);

		// The three columns, 35 px apart about the panel's centre (the old controls were 0 to 0.26 px off them), each
		// row from its old corner. The CV jacks stay tiny: a full one would touch the knob above it and the label
		// below, and RESET's and RUN's input and output are 16 px apart.
		const float kColumnRight = 35.f;   // the same in tools/panels/tocante.py
		const float kForkOut = 8.f;        // RESET's and RUN's input and output either side of their column (the old
		                                   // forks were 0.7 px left of their lines), the same in tools/panels/tocante.py
		const theme::Pos mid = theme::specific(w * 0.5f, 0);
		const theme::Pos col[3] = {mid.right(-kColumnRight), mid, mid.right(kColumnRight)};
		const float knobY = theme::specificCorner(3, 90, theme::kBlueKnobSize).y;
		const float cvY = theme::specificCorner(10, 125, theme::kTinyJackSize).y;
		const float lowY = theme::specificCorner(3, 155, theme::kBlueKnobSize).y;   // FINE, RESET and RUN (the
		                                                                            // buttons were 0.87 px lower)
		const float lowCvY = theme::specificCorner(10, 190, theme::kTinyJackSize).y;

		addParam(theme::param<BidooBlueKnob>(theme::sameCol(col[0], knobY), module, TOCANTE::BPM_PARAM));
		addParam(theme::param<BidooBlueSnapKnob>(theme::sameCol(col[1], knobY), module, TOCANTE::BEATS_PARAM));
		addParam(theme::param<BidooBlueSnapKnob>(theme::sameCol(col[2], knobY), module, TOCANTE::REF_PARAM));
		addInput(theme::input<TinyPJ301MPort>(theme::sameCol(col[0], cvY), module, TOCANTE::BPM_INPUT));
		addInput(theme::input<TinyPJ301MPort>(theme::sameCol(col[1], cvY), module, TOCANTE::BEATS_INPUT));
		addInput(theme::input<TinyPJ301MPort>(theme::sameCol(col[2], cvY), module, TOCANTE::REF_INPUT));

		addParam(theme::param<BidooBlueKnob>(theme::sameCol(col[0], lowY), module, TOCANTE::BPMFINE_PARAM));
		addInput(theme::input<TinyPJ301MPort>(theme::sameCol(col[0], lowCvY), module, TOCANTE::BPMFINE_INPUT));
		// RESET and RUN: the house round button, momentary as before, lit (orange) by the light the module keeps
		// (the reset's flash, the running state), which used to light the bezel
		const int buttonIds[2] = {TOCANTE::RESET_PARAM, TOCANTE::RUN_PARAM};
		const int lightIds[2] = {TOCANTE::RESET_LIGHT, TOCANTE::RUNNING_LIGHT};
		const int inIds[2] = {TOCANTE::RESET_INPUT, TOCANTE::RUN_INPUT};
		const int outIds[2] = {TOCANTE::OUT_RESET, TOCANTE::OUT_RUN};
		for (int i = 0; i < 2; i++) {
			BidooRoundButton *b = theme::param<BidooRoundButton>(theme::sameCol(col[i + 1], lowY), module, buttonIds[i]);
			b->momentary = true;
			const int lightId = lightIds[i];
			b->litBy = [=]() { return module && module->lights[lightId].getBrightness() > 0.5f; };
			addParam(b);
			const theme::Pos fork = theme::sameCol(col[i + 1], lowCvY);
			addInput(theme::input<TinyPJ301MPort>(fork.right(-kForkOut), module, inIds[i]));
			addOutput(theme::output<TinyPJ301MPort>(fork.right(kForkOut), module, outIds[i]));
		}

		// the nine outputs: one grid box each, three columns on rows 2 to 0 (the old rows were 2 and 1 px higher,
		// the outer columns 4.2 px further out)
		const int boxIds[9] = {TOCANTE::OUT_MEASURE, TOCANTE::OUT_QUARTER, TOCANTE::OUT_EIGHTH, TOCANTE::OUT_BEAT, TOCANTE::OUT_TRIPLET,
			TOCANTE::OUT_SIXTEENTH, TOCANTE::OUT_THIRTYSECOND, TOCANTE::OUT_SIXTYFOURTH, TOCANTE::OUT_ONEHUNDREDTWENTYEIGHTH};
		for (int i = 0; i < 9; i++)
			addOutput(theme::output<PJ301MPort>(theme::jackAt(i / 3, 3, 2 - i % 3, w), module, boxIds[i]));
	}
};

Model *modelTOCANTE = createModel<TOCANTE, TOCANTEWidget>("tOCAnTe");
