#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "MetroSeq.hpp"

using namespace std;

// 8 stages with pitch, pulses, gate type, slide and skip; the engine is
// shared with BORDL (MetroSeq.hpp).
struct DTROY : MetroModule<DTROY> {
	enum ParamIds {
		CLOCK_PARAM,
		RUN_PARAM,
		RESET_PARAM,
		STEPS_PARAM,
		SLIDE_TIME_PARAM,
		GATE_TIME_PARAM,
		ROOT_NOTE_PARAM,
		SCALE_PARAM,
		PLAY_MODE_PARAM,
		COUNT_MODE_PARAM,
		PATTERN_PARAM,
		SENSITIVITY_PARAM,
		TRIG_COUNT_PARAM = SENSITIVITY_PARAM + 8,
		TRIG_TYPE_PARAM = TRIG_COUNT_PARAM + 8,
		TRIG_PITCH_PARAM = TRIG_TYPE_PARAM + 8,
		TRIG_SLIDE_PARAM = TRIG_PITCH_PARAM + 8,
		TRIG_SKIP_PARAM = TRIG_SLIDE_PARAM + 8,
		LEFT_PARAM = TRIG_SKIP_PARAM + 8,
		RIGHT_PARAM,
		UP_PARAM,
		DOWN_PARAM,
		COPY_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		CLOCK_INPUT,
		EXT_CLOCK_INPUT,
		RESET_INPUT,
		STEPS_INPUT,
		SLIDE_TIME_INPUT,
		GATE_TIME_INPUT,
		ROOT_NOTE_INPUT,
		SCALE_INPUT,
		EXTGATE1_INPUT,
		EXTGATE2_INPUT,
		PATTERN_INPUT,
		TRANSPOSE_INPUT,
		SENSITIVITY_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		GATE_OUTPUT,
		PITCH_OUTPUT,
		STEP_OUTPUT,
		NUM_OUTPUTS = STEP_OUTPUT + 8
	};
	enum LightIds {
		RUNNING_LIGHT,
		RESET_LIGHT,
		STEPS_LIGHTS, // 8: each step's flash, not drawn: MetroSeq fades it into the slide and skip lights
		SLIDES_LIGHTS = STEPS_LIGHTS + 8,
		SKIPS_LIGHTS = SLIDES_LIGHTS + 8,
		COPY_LIGHT = SKIPS_LIGHTS + 8,
		NUM_LIGHTS
	};

	static constexpr bool EXTENDED = false;

	DTROY() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configMetro(*this);
	}

	void onRandomize(const RandomizeEvent &e) override {
		// the knobs first, as Rack does, then the slides and skips
		MetroModule<DTROY>::onRandomize(e);
		randomizeSlidesSkips();
	}
};

struct DTROYWidget : MetroWidget<DTROY> {
	void appendContextMenu(ui::Menu *menu) override;

	DTROYWidget(DTROY *module);
};

DTROYWidget::DTROYWidget(DTROY *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/DTROY.svg"));

	theme::addScrews(this);

	// Migrated from the hand-drawn panel (tools/panels/dtroy.py), keeping its arrangement. Left: the section shared
	// with BORDL (metroLeftControls in MetroSeq.hpp) over G1, G2, GATE and V/O in their boxes. Right: the eight
	// stages, a dense grid at its old corners: pitch knob, pulse count and gate mode sliders, slide and skip buttons,
	// the tiny stage output on its red square.
	metroLeftControls(this, module);

	// the bottom row: one box each, on the left section's columns (they were 0.1 to 1 px off them)
	const int boxIds[4] = {DTROY::EXTGATE1_INPUT, DTROY::EXTGATE2_INPUT, DTROY::GATE_OUTPUT, DTROY::PITCH_OUTPUT};
	for (int i = 0; i < 4; i++) {
		const theme::Pos p = theme::specific(metroPanel::kCol[i], theme::row(0));
		if (i < 2)
			addInput(theme::input<PJ301MPort>(p, module, boxIds[i]));
		else
			addOutput(theme::output<PJ301MPort>(p, module, boxIds[i]));
	}

	// the stages' columns, as the old panel spaces them (STAGE_X in tools/panels/dtroy.py): the sliders', buttons' and
	// outputs' centres; the pitch knobs were 0.76 px right of them, the outputs 0.5
	const float kStageLeft[8] = {200, 238, 276, 315, 353, 392, 430, 469};
	const float kStageCentre = 11.f;   // from a column's left to its centre: the sliders' 18 px
	const theme::Pos pitchRow = theme::specificCorner(0, 36, theme::kBlueKnobSize);
	const theme::Pos slideRow = theme::specific(0, 297 + 9.f), skipRow = theme::specific(0, 321 + 9.f);   // the old 18 px LEDButtons' centres (SLIDE_Y, SKIP_Y)
	const theme::Pos outRow = theme::specificCorner(0, 344, theme::kTinyJackSize);
	for (int i = 0; i < 8; i++) {
		const theme::Pos stage = theme::specific(kStageLeft[i] + kStageCentre, 0);
		addParam(theme::param<BidooBlueKnob>(theme::colRow(stage, pitchRow), module, DTROY::TRIG_PITCH_PARAM + i));
		addParam(theme::atCorner(createParam<BidooSlidePotLong>(Vec(), module, DTROY::TRIG_COUNT_PARAM + i), kStageLeft[i] + 2, 87));
		addParam(theme::atCorner(createParam<BidooSlidePotShort>(Vec(), module, DTROY::TRIG_TYPE_PARAM + i), kStageLeft[i] + 2, 204));
		// slide and skip: round buttons, momentary as before, lit by their lights (the state, flipped while the stage plays)
		const int lights[2] = {DTROY::SLIDES_LIGHTS + i, DTROY::SKIPS_LIGHTS + i};
		const int ids[2] = {DTROY::TRIG_SLIDE_PARAM + i, DTROY::TRIG_SKIP_PARAM + i};
		const theme::Pos rows[2] = {slideRow, skipRow};
		for (int j = 0; j < 2; j++) {
			BidooRoundButton *b = theme::param<BidooRoundButton>(theme::colRow(stage, rows[j]), module, ids[j]);
			b->momentary = true;
			const int lightId = lights[j];
			b->litBy = [=]() { return module && module->lights[lightId].getBrightness() > 0.5f; };
			addParam(b);
		}
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(stage, outRow), module, DTROY::STEP_OUTPUT + i));
	}

	// the pitch sensitivity: its tiny jack and trimpot on the PITCH bracket's line (they were 0.16 px higher and
	// 1.94 px lower), at their old columns
	const theme::Pos pitchLine = theme::specific(0, 25.66f);   // PITCH_LINE in tools/panels/dtroy.py
	addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specificCorner(kStageLeft[6] - 10, 0, theme::kTinyJackSize), pitchLine), module, DTROY::SENSITIVITY_INPUT));
	addParam(theme::param<BidooBlueTrimpot>(theme::colRow(theme::specificCorner(kStageLeft[6] + 21, 0, theme::kTrimpotSize), pitchLine), module, DTROY::SENSITIVITY_PARAM));
}

void DTROYWidget::appendContextMenu(ui::Menu *menu) {
	BidooWidget::appendContextMenu(menu);
	DTROY *m = dynamic_cast<DTROY*>(this->module);
	if (!m)
		return;
	appendMetroMenu(menu, m);
}

Model *modelDTROY = createModel<DTROY, DTROYWidget>("dTrOY");
