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
		for (int i = 0; i < 8; i++) {
		}
	}

	void onRandomize() override {
		randomizeSlidesSkips();
	}
};

struct DTROYWidget : MetroWidget<DTROY> {
	ParamWidget *stepsParam, *scaleParam, *rootNoteParam, *sensitivityParam,
	 *gateTimeParam, *slideTimeParam, *playModeParam, *countModeParam, *patternParam,
		*pitchParams[8], *pulseParams[8], *typeParams[8], *slideParams[8], *skipParams[8],
		 *pitchRndParams[8], *pulseProbParams[8], *accentParams[8], *rndAccentParams[8];

	void appendContextMenu(ui::Menu *menu) override;

	DTROYWidget(DTROY *module);
};

DTROYWidget::DTROYWidget(DTROY *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/DTROY.svg"));

	theme::addScrews(this);

	// The original hand-drawn panel. Left: the clock, run, reset and steps row with their jacks, the key, scale, gate
	// and slide knobs with theirs, each joined by a line; play, count and the pattern knob joined by a line to its
	// jack; the screen; transpose, copy and the shift arrows; G1, G2, GATE and V/O in their boxes. Right: the eight
	// steps (pitch knob, pulse count and gate mode sliders, slide and skip buttons, step output), a dense grid kept at
	// its old corners. On the left each control and jack now sits on the line the panel draws between them (the old
	// code had them 0.15 to 0.8 px off; the lines of the two rows differ by up to 0.5 px, so each row follows its own),
	// the pattern jack and knob on their line, and the bottom jacks on their boxes' centres.
	const theme::Size kButtonSize = {17.9996f, 18.f};                 // LEDButton
	const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};     // SmallLight
	const float kLedIn = 6.f;   // from a button's corner to its light's
	const theme::Pos topLine[4] = {theme::specific(32.594f, 0), theme::specific(70.f, 0), theme::specific(107.531f, 0), theme::specific(147.f, 0)};   // the top row's lines (the second column has none: it takes the middle row's)
	const theme::Pos midLine[4] = {theme::specific(32.562f, 0), theme::specific(70.f, 0), theme::specific(108.f, 0), theme::specific(147.219f, 0)};   // the middle row's lines

	{
		MetroDisplay<DTROY> *display = new MetroDisplay<DTROY>();
		display->module = module;
		display->box = theme::specificRect(20, 217, 250, 60);
		addChild(display);
	}

	addParam(theme::param<BidooRoundBlackKnob>(theme::colRow(topLine[0], theme::specificCorner(0, 36, theme::kBlueKnobSize)), module, DTROY::CLOCK_PARAM));
	addParam(theme::param<LEDButton>(theme::specificCorner(61, 40, kButtonSize), module, DTROY::RUN_PARAM));
	addChild(theme::light<SmallLight<GreenLight>>(theme::specificCorner(61 + kLedIn, 40 + kLedIn, kSmallLightSize), module, DTROY::RUNNING_LIGHT));
	const theme::Pos reset = theme::colRow(topLine[2], theme::specificCorner(0, 40, kButtonSize));
	addParam(theme::param<LEDButton>(reset, module, DTROY::RESET_PARAM));
	addChild(theme::light<SmallLight<GreenLight>>(theme::specificCorner(reset.x - kButtonSize.w * 0.5f + kLedIn, 40 + kLedIn, kSmallLightSize), module, DTROY::RESET_LIGHT));
	stepsParam = theme::param<BidooBlueSnapKnob>(theme::colRow(topLine[3], theme::specificCorner(0, 36, theme::kBlueKnobSize)), module, DTROY::STEPS_PARAM);
	addParam(stepsParam);

	const theme::Pos topJacks = theme::specificCorner(0, 69, theme::kJackSize);
	addInput(theme::input<PJ301MPort>(theme::colRow(topLine[0], topJacks), module, DTROY::CLOCK_INPUT));
	addInput(theme::input<PJ301MPort>(theme::colRow(topLine[1], topJacks), module, DTROY::EXT_CLOCK_INPUT));
	addInput(theme::input<PJ301MPort>(theme::colRow(topLine[2], topJacks), module, DTROY::RESET_INPUT));
	addInput(theme::input<PJ301MPort>(theme::colRow(topLine[3], topJacks), module, DTROY::STEPS_INPUT));

	const theme::Pos midKnobs = theme::specificCorner(0, 116, theme::kBlueKnobSize);
	rootNoteParam = theme::param<BidooBlueSnapKnob>(theme::colRow(midLine[0], midKnobs), module, DTROY::ROOT_NOTE_PARAM);
	addParam(rootNoteParam);
	scaleParam = theme::param<BidooBlueSnapKnob>(theme::colRow(midLine[1], midKnobs), module, DTROY::SCALE_PARAM);
	addParam(scaleParam);
	gateTimeParam = theme::param<BidooBlueKnob>(theme::colRow(midLine[2], midKnobs), module, DTROY::GATE_TIME_PARAM);
	addParam(gateTimeParam);
	slideTimeParam = theme::param<BidooBlueKnob>(theme::colRow(midLine[3], midKnobs), module, DTROY::SLIDE_TIME_PARAM);
	addParam(slideTimeParam);

	const theme::Pos midJacks = theme::specificCorner(0, 149, theme::kJackSize);
	addInput(theme::input<PJ301MPort>(theme::colRow(midLine[0], midJacks), module, DTROY::ROOT_NOTE_INPUT));
	addInput(theme::input<PJ301MPort>(theme::colRow(midLine[1], midJacks), module, DTROY::SCALE_INPUT));
	addInput(theme::input<PJ301MPort>(theme::colRow(midLine[2], midJacks), module, DTROY::GATE_TIME_INPUT));
	addInput(theme::input<PJ301MPort>(theme::colRow(midLine[3], midJacks), module, DTROY::SLIDE_TIME_INPUT));

	playModeParam = theme::param<BlueCKD6>(theme::specificCorner(19, 196, theme::kCKD6Size), module, DTROY::PLAY_MODE_PARAM);
	addParam(playModeParam);
	countModeParam = theme::param<BlueCKD6>(theme::specificCorner(57, 196, theme::kCKD6Size), module, DTROY::COUNT_MODE_PARAM);
	addParam(countModeParam);
	const theme::Pos patternLine = theme::specific(0, 210.469f);   // the line from the pattern jack to its knob
	addInput(theme::input<PJ301MPort>(theme::colRow(theme::specificCorner(96, 0, theme::kJackSize), patternLine), module, DTROY::PATTERN_INPUT));
	patternParam = theme::param<BidooRoundBlackSnapKnob>(theme::colRow(theme::specificCorner(134, 0, theme::kBlueKnobSize), patternLine), module, DTROY::PATTERN_PARAM);
	addParam(patternParam);

	const float portX1[8] = {200, 238, 276, 315, 353, 392, 430, 469};   // the steps' columns, as the old panel spaces them

	sensitivityParam = theme::param<BidooBlueTrimpot>(theme::specificCorner(portX1[6] + 21, 18, theme::kTrimpotSize), module, DTROY::SENSITIVITY_PARAM);
	addParam(sensitivityParam);
	addInput(theme::input<TinyPJ301MPort>(theme::specificCorner(portX1[6] - 10, 18, theme::kTinyJackSize), module, DTROY::SENSITIVITY_INPUT));

	addInput(theme::input<PJ301MPort>(theme::specificCorner(20, 286, theme::kJackSize), module, DTROY::TRANSPOSE_INPUT));
	addParam(theme::param<BlueCKD6>(theme::specificCorner(57, 285, theme::kCKD6Size), module, DTROY::COPY_PARAM));
	addChild(theme::light<SmallLight<GreenLight>>(theme::specificCorner(81, 283, kSmallLightSize), module, DTROY::COPY_LIGHT));

	addParam(theme::atCorner(createParam<LeftBtn>(Vec(), module, DTROY::LEFT_PARAM), 104, 290));
	addParam(theme::atCorner(createParam<RightBtn>(Vec(), module, DTROY::RIGHT_PARAM), 134, 290));
	addParam(theme::atCorner(createParam<UpBtn>(Vec(), module, DTROY::UP_PARAM), 119, 282));
	addParam(theme::atCorner(createParam<DownBtn>(Vec(), module, DTROY::DOWN_PARAM), 119, 297));

	for (int i = 0; i < 8; i++) {
		pitchParams[i] = theme::param<BidooBlueKnob>(theme::specificCorner(portX1[i] - 3, 36, theme::kBlueKnobSize), module, DTROY::TRIG_PITCH_PARAM + i);
		addParam(pitchParams[i]);
		pulseParams[i] = theme::atCorner(createParam<BidooSlidePotLong>(Vec(), module, DTROY::TRIG_COUNT_PARAM + i), portX1[i] + 2, 87);
		addParam(pulseParams[i]);
		typeParams[i] = theme::atCorner(createParam<BidooSlidePotShort>(Vec(), module, DTROY::TRIG_TYPE_PARAM + i), portX1[i] + 2, 204);
		addParam(typeParams[i]);
		slideParams[i] = theme::param<LEDButton>(theme::specificCorner(portX1[i] + 2, 297, kButtonSize), module, DTROY::TRIG_SLIDE_PARAM + i);
		addParam(slideParams[i]);
		addChild(theme::light<SmallLight<BlueLight>>(theme::specificCorner(portX1[i] + 2 + kLedIn, 297 + kLedIn, kSmallLightSize), module, DTROY::SLIDES_LIGHTS + i));
		skipParams[i] = theme::param<LEDButton>(theme::specificCorner(portX1[i] + 2, 321, kButtonSize), module, DTROY::TRIG_SKIP_PARAM + i);
		addParam(skipParams[i]);
		addChild(theme::light<SmallLight<BlueLight>>(theme::specificCorner(portX1[i] + 2 + kLedIn, 321 + kLedIn, kSmallLightSize), module, DTROY::SKIPS_LIGHTS + i));
		addOutput(theme::output<TinyPJ301MPort>(theme::specificCorner(portX1[i] + 4, 344, theme::kTinyJackSize), module, DTROY::STEP_OUTPUT + i));
	}

	const theme::Pos boxRow = theme::specificCorner(0, 330, theme::kJackSize);   // the boxes' centres
	addInput(theme::input<PJ301MPort>(theme::colRow(theme::specific(32.281f, 0), boxRow), module, DTROY::EXTGATE1_INPUT));
	addInput(theme::input<PJ301MPort>(theme::colRow(theme::specific(70.375f, 0), boxRow), module, DTROY::EXTGATE2_INPUT));
	addOutput(theme::output<PJ301MPort>(theme::colRow(theme::specific(107.562f, 0), boxRow), module, DTROY::GATE_OUTPUT));
	addOutput(theme::output<PJ301MPort>(theme::colRow(theme::specific(146.094f, 0), boxRow), module, DTROY::PITCH_OUTPUT));
}

void DTROYWidget::appendContextMenu(ui::Menu *menu) {
	BidooWidget::appendContextMenu(menu);
	DTROY *m = dynamic_cast<DTROY*>(this->module);
	if (!m)
		return;
	appendMetroMenu(menu, m);
}

Model *modelDTROY = createModel<DTROY, DTROYWidget>("dTrOY");
