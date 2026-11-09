#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "MetroSeq.hpp"

using namespace std;

// DTROY with a gate probability, a random pitch and an accent per stage; the
// engine is shared with DTROY (MetroSeq.hpp).
struct BORDL : MetroModule<BORDL> {
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
		TRIG_GATEPROB_PARAM = TRIG_SKIP_PARAM + 8,
		TRIG_PITCHRND_PARAM = TRIG_GATEPROB_PARAM + 8,
		TRIG_ACCENT_PARAM = TRIG_PITCHRND_PARAM + 8,
		TRIG_RNDACCENT_PARAM = TRIG_ACCENT_PARAM + 8,
		LEFT_PARAM = TRIG_RNDACCENT_PARAM + 8,
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
		ACC_OUTPUT,
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
		PLAYMODE_LIGHT,
		COUNTMODE_LIGHT = PLAYMODE_LIGHT + 3,
		NUM_LIGHTS = COUNTMODE_LIGHT + 3
	};

	static constexpr bool EXTENDED = true;

	BORDL() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configMetro(*this);
		configOutput(ACC_OUTPUT, "Accent");
		for (int i = 0; i < 8; i++) {
			configParam(TRIG_GATEPROB_PARAM + i, 0.f, 1.f, 1.f, rack::string::f("Stage %d probability", i + 1), "%", 0.f, 100.f);
			configParam(TRIG_PITCHRND_PARAM + i, 0.f, 1.f, 0.f, rack::string::f("Stage %d random pitch (100%% = +-5 V)", i + 1), "%", 0.f, 100.f);
			configParam(TRIG_ACCENT_PARAM + i, 0.f, 10.f, 0.f, rack::string::f("Stage %d accent", i + 1), " V");
			configParam(TRIG_RNDACCENT_PARAM + i, 0.f, 1.f, 0.f, rack::string::f("Stage %d random accent (100%% = +-5 V)", i + 1), "%", 0.f, 100.f);
		}
	}
};

struct BORDLGateDisplay : TransparentWidget {
	BORDL *module;

	int index;

	BORDLGateDisplay() {

	}

	void drawGate(const DrawArgs &args, Vec pos) {
		if (module) {
			int gateType = (int)module->params[BORDL::TRIG_TYPE_PARAM+index].getValue();
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgFontSize(args.vg, 16.0f);
			nvgTextLetterSpacing(args.vg, -2.0f);
			if (gateType == 0) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,22.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
			}
			else if (gateType == 1) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,22.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,6.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgFill(args.vg);
			}
			else if (gateType == 2) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,6.0f,6.0f,0.0f);
				nvgRoundedRect(args.vg,pos.x+8.0f,pos.y,6.0f,6.0f,0.0f);
				nvgRoundedRect(args.vg,pos.x+16.0f,pos.y,6.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgFill(args.vg);
			}
			else if (gateType == 3) {
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,pos.x,pos.y,22.0f,6.0f,0.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgFill(args.vg);
			}
			else if (gateType == 4) {
			  nvgText(args.vg, pos.x+11.0f, pos.y+8.0f, "G1", NULL);
			}
			else if (gateType == 5) {
			  nvgText(args.vg, pos.x+11.0f, pos.y+8.0f, "G2", NULL);
			}
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			drawGate(args, Vec(0.0f, 0.0f));
		}
		Widget::drawLayer(args, layer);
	}

};

struct BORDLPulseDisplay : TransparentWidget {
	BORDL *module;

	int index;

	BORDLPulseDisplay() {

	}

	void drawPulse(const DrawArgs &args, Vec pos) {
		if (module) {
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgFontSize(args.vg, 16.0f);
			nvgTextLetterSpacing(args.vg, -2.0f);
			char tCount[128];
			snprintf(tCount, sizeof(tCount), "%1i", (int)module->params[BORDL::TRIG_COUNT_PARAM+index].getValue());
			nvgText(args.vg, pos.x, pos.y, tCount, NULL);
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			drawPulse(args, Vec(0.0f, 0.0f));
		}
		Widget::drawLayer(args, layer);
	}

};

struct BORDLPitchDisplay : TransparentWidget {
	BORDL *module;

	int index;

	BORDLPitchDisplay() {

	}

	void drawPitch(const DrawArgs &args, Vec pos) {
		if (module) {
			nvgStrokeWidth(args.vg, 3.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgFontSize(args.vg, 16.0f);
			const MetroPattern &p = module->engine.patterns[module->engine.selectedPattern];
			float v = module->engine.quantize(*module, p, module->params[BORDL::TRIG_PITCH_PARAM + index].getValue());
			nvgText(args.vg, pos.x, pos.y-9.0f, module->engine.quant.noteName(v).c_str(), NULL);
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			drawPitch(args, Vec(0.0f, 0.0f));
		}
		Widget::drawLayer(args, layer);
	}

};

struct BORDLWidget : MetroWidget<BORDL> {
	ParamWidget *stepsParam, *scaleParam, *rootNoteParam, *sensitivityParam,
	 *gateTimeParam, *slideTimeParam, *playModeParam, *countModeParam, *patternParam,
		*pitchParams[8], *pulseParams[8], *typeParams[8], *slideParams[8], *skipParams[8],
		 *pitchRndParams[8], *pulseProbParams[8], *accentParams[8], *rndAccentParams[8];

	void appendContextMenu(ui::Menu *menu) override;

	BORDLWidget(BORDL *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/BORDL.svg"));

		theme::addScrews(this);

		// The original hand-drawn panel, dTrOY's left section with a fifth output. Left: the clock, run, reset and
		// steps row with their jacks, the key, scale, gate and slide knobs with theirs, each joined by a line; play,
		// count and the pattern knob joined by a line to its jack; the screen; transpose, copy and the shift arrows;
		// five jacks in their boxes. Right: the eight steps (pitch and accent knobs with their random trimpots, pulse
		// count and probability, gate type, slide and skip, step output), a dense grid kept at its old corners. On the
		// left each control and jack now sits on the line the panel draws between them (the old code had them up to
		// 1 px off; each row follows its own lines), the pattern jack and knob on their line, and the bottom jacks on
		// their boxes' centres.
		const theme::Size kButtonSize = {17.9996f, 18.f};                 // LEDButton
		const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};     // SmallLight
		const float kLedIn = 6.f;   // from a button's corner to its light's
		const theme::Pos topLine[4] = {theme::specific(32.844f, 0), theme::specific(70.281f, 0), theme::specific(107.844f, 0), theme::specific(147.281f, 0)};   // the top row's lines (the second column has none: it takes the middle row's)
		const theme::Pos midLine[4] = {theme::specific(32.875f, 0), theme::specific(70.281f, 0), theme::specific(108.281f, 0), theme::specific(147.5f, 0)};   // the middle row's lines

		{
			MetroDisplay<BORDL> *display = new MetroDisplay<BORDL>();
			display->module = module;
			display->box = theme::specificRect(20, 217, 250, 60);
			addChild(display);
		}

		addParam(theme::param<BidooRoundBlackKnob>(theme::colRow(topLine[0], theme::specificCorner(0, 36, theme::kBlueKnobSize)), module, BORDL::CLOCK_PARAM));
		addParam(theme::param<LEDButton>(theme::specificCorner(61, 40, kButtonSize), module, BORDL::RUN_PARAM));
		addChild(theme::light<SmallLight<GreenLight>>(theme::specificCorner(61 + kLedIn, 40 + kLedIn, kSmallLightSize), module, BORDL::RUNNING_LIGHT));
		const theme::Pos reset = theme::colRow(topLine[2], theme::specificCorner(0, 40, kButtonSize));
		addParam(theme::param<LEDButton>(reset, module, BORDL::RESET_PARAM));
		addChild(theme::light<SmallLight<GreenLight>>(theme::specificCorner(reset.x - kButtonSize.w * 0.5f + kLedIn, 40 + kLedIn, kSmallLightSize), module, BORDL::RESET_LIGHT));
		stepsParam = theme::param<BidooBlueSnapKnob>(theme::colRow(topLine[3], theme::specificCorner(0, 36, theme::kBlueKnobSize)), module, BORDL::STEPS_PARAM);
		addParam(stepsParam);

		const theme::Pos topJacks = theme::specificCorner(0, 69, theme::kJackSize);
		addInput(theme::input<PJ301MPort>(theme::colRow(topLine[0], topJacks), module, BORDL::CLOCK_INPUT));
		addInput(theme::input<PJ301MPort>(theme::colRow(topLine[1], topJacks), module, BORDL::EXT_CLOCK_INPUT));
		addInput(theme::input<PJ301MPort>(theme::colRow(topLine[2], topJacks), module, BORDL::RESET_INPUT));
		addInput(theme::input<PJ301MPort>(theme::colRow(topLine[3], topJacks), module, BORDL::STEPS_INPUT));

		const theme::Pos midKnobs = theme::specificCorner(0, 116, theme::kBlueKnobSize);
		rootNoteParam = theme::param<BidooBlueSnapKnob>(theme::colRow(midLine[0], midKnobs), module, BORDL::ROOT_NOTE_PARAM);
		addParam(rootNoteParam);
		scaleParam = theme::param<BidooBlueSnapKnob>(theme::colRow(midLine[1], midKnobs), module, BORDL::SCALE_PARAM);
		addParam(scaleParam);
		gateTimeParam = theme::param<BidooBlueKnob>(theme::colRow(midLine[2], midKnobs), module, BORDL::GATE_TIME_PARAM);
		addParam(gateTimeParam);
		slideTimeParam = theme::param<BidooBlueKnob>(theme::colRow(midLine[3], midKnobs), module, BORDL::SLIDE_TIME_PARAM);
		addParam(slideTimeParam);

		const theme::Pos midJacks = theme::specificCorner(0, 149, theme::kJackSize);
		addInput(theme::input<PJ301MPort>(theme::colRow(midLine[0], midJacks), module, BORDL::ROOT_NOTE_INPUT));
		addInput(theme::input<PJ301MPort>(theme::colRow(midLine[1], midJacks), module, BORDL::SCALE_INPUT));
		addInput(theme::input<PJ301MPort>(theme::colRow(midLine[2], midJacks), module, BORDL::GATE_TIME_INPUT));
		addInput(theme::input<PJ301MPort>(theme::colRow(midLine[3], midJacks), module, BORDL::SLIDE_TIME_INPUT));

		addParam(theme::atCorner(createLightParam<VCVLightBezel<RedGreenBlueLight>>(Vec(), module, BORDL::PLAY_MODE_PARAM, BORDL::PLAYMODE_LIGHT), 23, 199));
		addParam(theme::atCorner(createLightParam<VCVLightBezel<RedGreenBlueLight>>(Vec(), module, BORDL::COUNT_MODE_PARAM, BORDL::COUNTMODE_LIGHT), 61, 199));

		const theme::Pos patternLine = theme::specific(0, 210.562f);   // the line from the pattern jack to its knob
		addInput(theme::input<PJ301MPort>(theme::colRow(theme::specificCorner(96, 0, theme::kJackSize), patternLine), module, BORDL::PATTERN_INPUT));
		patternParam = theme::param<BidooRoundBlackSnapKnob>(theme::colRow(theme::specificCorner(135, 0, theme::kBlueKnobSize), patternLine), module, BORDL::PATTERN_PARAM);
		addParam(patternParam);

		const float portX1[8] = {200, 241, 282, 323, 364, 405, 446, 487};   // the steps' columns

		sensitivityParam = theme::atCorner(createParam<BidooBlueTrimpot>(Vec(), module, BORDL::SENSITIVITY_PARAM), portX1[0]-24.0f, 30.0f);
		addParam(sensitivityParam);
		addInput(theme::atCorner(createInput<TinyPJ301MPort>(Vec(), module, BORDL::SENSITIVITY_INPUT), portX1[0]-22.0f, 52.0f));

		addInput(theme::atCorner(createInput<PJ301MPort>(Vec(), module, BORDL::TRANSPOSE_INPUT), 20, 286.0f));
		addParam(theme::atCorner(createParam<BlueCKD6>(Vec(), module, BORDL::COPY_PARAM), 58-1.0f, 285.0f));
		addChild(theme::atCorner(createLight<SmallLight<GreenLight>>(Vec(), module, BORDL::COPY_LIGHT), 58+23.0f, 283.0f));

		addParam(theme::atCorner(createParam<LeftBtn>(Vec(), module, BORDL::LEFT_PARAM), 104.0f, 290.0f));
		addParam(theme::atCorner(createParam<RightBtn>(Vec(), module, BORDL::RIGHT_PARAM), 134.0f, 290.0f));
		addParam(theme::atCorner(createParam<UpBtn>(Vec(), module, BORDL::UP_PARAM), 119.0f, 282.0f));
		addParam(theme::atCorner(createParam<DownBtn>(Vec(), module, BORDL::DOWN_PARAM), 119.0f, 297.0f));

		for (int i = 0; i < 8; i++) {
			pitchParams[i] = theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, BORDL::TRIG_PITCH_PARAM + i), portX1[i]+1.0f, 56.0f);
			addParam(pitchParams[i]);
			pitchRndParams[i] = theme::atCorner(createParam<BidooBlueTrimpot>(Vec(), module, BORDL::TRIG_PITCHRND_PARAM + i), portX1[i]+27.0f, 81.0f);
			addParam(pitchRndParams[i]);
			accentParams[i] = theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, BORDL::TRIG_ACCENT_PARAM + i), portX1[i]+1.0f, 110.0f);
			addParam(accentParams[i]);
			rndAccentParams[i] = theme::atCorner(createParam<BidooBlueTrimpot>(Vec(), module, BORDL::TRIG_RNDACCENT_PARAM + i), portX1[i]+27.0f, 135.0f);
			addParam(rndAccentParams[i]);
			{
				BORDLPitchDisplay *displayPitch = new BORDLPitchDisplay();
				displayPitch->module = module;
				displayPitch->box = theme::specificRect(portX1[i]+16.0f, 52.0f, 20.0f, 10.0f);
				displayPitch->index = i;
				addChild(displayPitch);
			}
			pulseParams[i] = theme::atCorner(createParam<BidooBlueSnapKnob>(Vec(), module, BORDL::TRIG_COUNT_PARAM + i), portX1[i]+1.0f, 188.0f);
			addParam(pulseParams[i]);
			pulseProbParams[i] = theme::atCorner(createParam<BidooBlueTrimpot>(Vec(), module, BORDL::TRIG_GATEPROB_PARAM + i), portX1[i]+27.0f, 213.0f);
			addParam(pulseProbParams[i]);
			{
				BORDLPulseDisplay *displayPulse = new BORDLPulseDisplay();
				displayPulse->module = module;
				displayPulse->box = theme::specificRect(portX1[i]+15.0f, 179.0f, 20.0f, 10.0f);
				displayPulse->index = i;
				addChild(displayPulse);
			}
			typeParams[i] = theme::atCorner(createParam<BidooBlueSnapTrimpot>(Vec(), module, BORDL::TRIG_TYPE_PARAM + i), portX1[i]+6.5f, 267.0f);
			addParam(typeParams[i]);
			{
				BORDLGateDisplay *displayGate = new BORDLGateDisplay();
				displayGate->module = module;
				displayGate->box = theme::specificRect(portX1[i]+5.0f, 250.0f, 20.0f, 10.0f);
				displayGate->index = i;
				addChild(displayGate);
			}

			const float btnOffset = 3.5f;
			addParam(theme::atCorner(createLightParam<SmallLEDLightBezel<BlueLight>>(Vec(), module, BORDL::TRIG_SLIDE_PARAM + i, BORDL::SLIDES_LIGHTS + i), portX1[i]+7.0f + btnOffset, 295.0f + btnOffset));
			addParam(theme::atCorner(createLightParam<SmallLEDLightBezel<BlueLight>>(Vec(), module, BORDL::TRIG_SKIP_PARAM + i, BORDL::SKIPS_LIGHTS + i), portX1[i]+7.0f + btnOffset, 316.0f + btnOffset));

			addOutput(theme::atCorner(createOutput<TinyPJ301MPort>(Vec(), module, BORDL::STEP_OUTPUT + i), portX1[i]+9.0f, 344.0f));
		}

		const theme::Pos boxRow = theme::specificCorner(0, 330, theme::kJackSize);   // the boxes' centres
		addInput(theme::input<PJ301MPort>(theme::colRow(theme::specific(21.844f, 0), boxRow), module, BORDL::EXTGATE1_INPUT));
		addInput(theme::input<PJ301MPort>(theme::colRow(theme::specific(55.031f, 0), boxRow), module, BORDL::EXTGATE2_INPUT));
		addOutput(theme::output<PJ301MPort>(theme::colRow(theme::specific(88.219f, 0), boxRow), module, BORDL::GATE_OUTPUT));
		addOutput(theme::output<PJ301MPort>(theme::colRow(theme::specific(121.406f, 0), boxRow), module, BORDL::PITCH_OUTPUT));
		addOutput(theme::output<PJ301MPort>(theme::colRow(theme::specific(154.594f, 0), boxRow), module, BORDL::ACC_OUTPUT));
	}
};

void BORDLWidget::appendContextMenu(ui::Menu *menu) {
	BidooWidget::appendContextMenu(menu);
	BORDL *m = dynamic_cast<BORDL*>(this->module);
	if (!m)
		return;
	appendMetroMenu(menu, m);
}

Model *modelBORDL = createModel<BORDL, BORDLWidget>("bordL");
