#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "MetroSeq.hpp"

using namespace std;

// 8 stages with pitch, pulses, gate type, slide and skip; the engine is
// shared with BORDL (MetroSeq.hpp).
struct DTROY : BidooModule {
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
	MetroEngine<DTROY> engine;

	DTROY() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configMetro(*this);
		configButton(PLAY_MODE_PARAM, "Play mode (forward, backward, ping-pong, random, brownian)");
		configButton(COUNT_MODE_PARAM, "Count mode (steps or pulses)");
		for (int i = 0; i < 8; i++) {
			configButton(TRIG_SLIDE_PARAM + i, rack::string::f("Stage %d slide", i + 1));
			configButton(TRIG_SKIP_PARAM + i, rack::string::f("Stage %d skip", i + 1));
		}
	}

	void process(const ProcessArgs &args) override {
		engine.process(*this, args);
	}

	json_t *dataToJson() override {
		return engine.toJson(BidooModule::dataToJson());
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		engine.fromJson(rootJ);
	}

	void onReset() override {
		engine.reset();
	}

	void onRandomize() override {
		randomizeSlidesSkips();
	}

	void randomizeSlidesSkips() {
		MetroPattern &p = engine.patterns[engine.selectedPattern];
		for (int i = 0; i < 8; i++) {
			p.stageSlide[i] = random::uniform() > 0.8f;
			p.stageSkip[i] = random::uniform() > 0.85f;
		}
	}

	void randomizePitch() {
		for (int i = 0; i < 8; i++)
			params[TRIG_PITCH_PARAM + i].setValue(random::uniform() * 10.f - 4.f);
	}

	void randomizeGates() {
		for (int i = 0; i < 8; i++) {
			params[TRIG_COUNT_PARAM + i].setValue((int)(random::uniform() * 7.f + 1.f));
			params[TRIG_TYPE_PARAM + i].setValue((int)(random::uniform() * 6.f));
		}
	}
};

struct DTROYDisplay : TransparentWidget {
	DTROY *module;

	DTROYDisplay() {

	}

	// No strings built per frame: labels are static, numbers go through snprintf.
	void drawMessage(NVGcontext *vg, Vec pos, const char *note, const char *playMode, const char *selectedPattern, const char *playedPattern, const char *steps, const char *scale) {
		nvgFontSize(vg, 18.0f);
		nvgFillColor(vg, YELLOW_BIDOO);
		nvgText(vg, pos.x + 3.0f, pos.y + 8.0f, playMode, NULL);
		nvgFontSize(vg, 14.0f);
		nvgText(vg, pos.x + 114.0f, pos.y + 7.0f, selectedPattern, NULL);

		nvgText(vg, pos.x + 30.0f, pos.y + 7.0f, steps, NULL);
		nvgText(vg, pos.x + 3.0f, pos.y + 21.0f, note, NULL);
		nvgText(vg, pos.x + 25.0f, pos.y + 21.0f, scale, NULL);

		// The played pattern blinks: 0.5 s shown, 0.25 s hidden, whatever the frame rate.
		if (std::fmod(system::getTime(), 0.75) < 0.5)
			nvgText(vg, pos.x + 90.0f, pos.y + 7.0f, playedPattern, NULL);
	}

	static const char *displayRootNote(int value) {
		return quantizer::rootNotes[value+1].label.c_str();
	}

	static const char *displayScale(int value) {
		return quantizer::scales[value].label.c_str();
	}

	static const char *displayPlayMode(int value) {
		switch(value){
			case 0: return "►";
			case 1: return "◄";
			case 2: return "►◄";
			case 3: return "►*";
			case 4: return "►?";
			default: return "";
		}
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (module) {
				const MetroPattern &p = module->engine.patterns[module->engine.selectedPattern];
				char steps[16], selectedPattern[8], playedPattern[8];
				snprintf(steps, sizeof(steps), "%s%d", p.countMode == 0 ? "steps:" : "pulses:", p.numberOfStepsParam);
				snprintf(selectedPattern, sizeof(selectedPattern), "P%d", module->engine.selectedPattern + 1);
				snprintf(playedPattern, sizeof(playedPattern), "P%d", module->engine.playedPattern + 1);
				drawMessage(args.vg, Vec(0.0f, 20.0f), displayRootNote(p.rootNote), displayPlayMode(p.playMode), selectedPattern, playedPattern, steps, displayScale(p.scale));
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct DTROYWidget : BidooWidget {
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

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	{
		DTROYDisplay *display = new DTROYDisplay();
		display->module = module;
		display->box.pos = Vec(20.0f, 217.0f);
		display->box.size = Vec(250.0f, 60.0f);
		addChild(display);
	}

	addParam(createParam<BidooRoundBlackKnob>(Vec(17.0f, 36.0f), module, DTROY::CLOCK_PARAM));
	addParam(createParam<LEDButton>(Vec(61.0f, 40.0f), module, DTROY::RUN_PARAM));
	addChild(createLight<SmallLight<GreenLight>>(Vec(67.0f, 46.0f), module, DTROY::RUNNING_LIGHT));
	addParam(createParam<LEDButton>(Vec(99.0f, 40.0f), module, DTROY::RESET_PARAM));
	addChild(createLight<SmallLight<GreenLight>>(Vec(105.0f, 46.0f), module, DTROY::RESET_LIGHT));
	stepsParam = createParam<BidooBlueSnapKnob>(Vec(132.0f, 36.0f), module, DTROY::STEPS_PARAM);
	addParam(stepsParam);

	static const float portX0[4] = {20.0f, 58.0f, 96.0f, 135.0f};
 	addInput(createInput<PJ301MPort>(Vec(portX0[0], 69.0f), module, DTROY::CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 69.0f), module, DTROY::EXT_CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 69.0f), module, DTROY::RESET_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[3], 69.0f), module, DTROY::STEPS_INPUT));

	rootNoteParam = createParam<BidooBlueSnapKnob>(Vec(portX0[0]-2.0f, 116.0f), module, DTROY::ROOT_NOTE_PARAM);
	addParam(rootNoteParam);
	scaleParam = createParam<BidooBlueSnapKnob>(Vec(portX0[1]-2.0f, 116.0f), module, DTROY::SCALE_PARAM);
	addParam(scaleParam);
	gateTimeParam = createParam<BidooBlueKnob>(Vec(portX0[2]-2.0f, 116.0f), module, DTROY::GATE_TIME_PARAM);
	addParam(gateTimeParam);
	slideTimeParam = createParam<BidooBlueKnob>(Vec(portX0[3]-2.0f, 116.0f), module, DTROY::SLIDE_TIME_PARAM);
	addParam(slideTimeParam);

	addInput(createInput<PJ301MPort>(Vec(portX0[0], 149.0f), module, DTROY::ROOT_NOTE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 149.0f), module, DTROY::SCALE_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 149.0f), module, DTROY::GATE_TIME_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[3], 149.0f), module, DTROY::SLIDE_TIME_INPUT));

	playModeParam = createParam<BlueCKD6>(Vec(portX0[0]-1.0f, 196.0f), module, DTROY::PLAY_MODE_PARAM);
	addParam(playModeParam);
	countModeParam = createParam<BlueCKD6>(Vec(portX0[1]-1.0f, 196.0f), module, DTROY::COUNT_MODE_PARAM);
	addParam(countModeParam);
	addInput(createInput<PJ301MPort>(Vec(portX0[2], 198.0f), module, DTROY::PATTERN_INPUT));
	patternParam = createParam<BidooRoundBlackSnapKnob>(Vec(portX0[3]-1,196.0f), module, DTROY::PATTERN_PARAM);
	addParam(patternParam);

	static const float portX1[8] = {200.0f, 238.0f, 276.0f, 315.0f, 353.0f, 392.0f, 430.0f, 469.0f};

	sensitivityParam = createParam<BidooBlueTrimpot>(Vec(portX1[6]+21.0f, 18.0f), module, DTROY::SENSITIVITY_PARAM);
	addParam(sensitivityParam);
	addInput(createInput<TinyPJ301MPort>(Vec(portX1[6]-10.f, 18.0f), module, DTROY::SENSITIVITY_INPUT));

	addInput(createInput<PJ301MPort>(Vec(portX0[0], 286.0f), module, DTROY::TRANSPOSE_INPUT));
	addParam(createParam<BlueCKD6>(Vec(portX0[1]-1.0f, 285.0f), module, DTROY::COPY_PARAM));
	addChild(createLight<SmallLight<GreenLight>>(Vec(portX0[1]+23.0f, 283.0f), module, DTROY::COPY_LIGHT));

	addParam(createParam<LeftBtn>(Vec(104.0f, 290.0f), module, DTROY::LEFT_PARAM));
	addParam(createParam<RightBtn>(Vec(134.0f, 290.0f), module, DTROY::RIGHT_PARAM));
	addParam(createParam<UpBtn>(Vec(119.0f, 282.0f), module, DTROY::UP_PARAM));
	addParam(createParam<DownBtn>(Vec(119.0f, 297.0f), module, DTROY::DOWN_PARAM));

	for (int i = 0; i < 8; i++) {
		pitchParams[i] = createParam<BidooBlueKnob>(Vec(portX1[i]-3.0f, 36.0f), module, DTROY::TRIG_PITCH_PARAM + i);
		addParam(pitchParams[i]);
		pulseParams[i] = createParam<BidooSlidePotLong>(Vec(portX1[i]+2.0f, 87.0f), module, DTROY::TRIG_COUNT_PARAM + i);
		addParam(pulseParams[i]);
		typeParams[i] = createParam<BidooSlidePotShort>(Vec(portX1[i]+2.0f, 204.0f), module, DTROY::TRIG_TYPE_PARAM + i);
		addParam(typeParams[i]);
		slideParams[i] = createParam<LEDButton>(Vec(portX1[i]+2.0f, 297.0f), module, DTROY::TRIG_SLIDE_PARAM + i);
		addParam(slideParams[i]);
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[i]+8.0f, 303.0f), module, DTROY::SLIDES_LIGHTS + i));
		skipParams[i] = createParam<LEDButton>(Vec(portX1[i]+2.0f, 321.0f), module, DTROY::TRIG_SKIP_PARAM + i);
		addParam(skipParams[i]);
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX1[i]+8.0f, 327.0f), module, DTROY::SKIPS_LIGHTS + i));
		addOutput(createOutput<TinyPJ301MPort>(Vec(portX1[i]+4.0f, 344.0f), module, DTROY::STEP_OUTPUT + i));
	}

	addInput(createInput<PJ301MPort>(Vec(portX0[0], 330.0f), module, DTROY::EXTGATE1_INPUT));
	addInput(createInput<PJ301MPort>(Vec(portX0[1], 330.0f), module, DTROY::EXTGATE2_INPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[2]-1, 330.0f), module, DTROY::GATE_OUTPUT));
	addOutput(createOutput<PJ301MPort>(Vec(portX0[3]-1, 330.0f), module, DTROY::PITCH_OUTPUT));
}

void DTROYWidget::appendContextMenu(ui::Menu *menu) {
	BidooWidget::appendContextMenu(menu);
	DTROY *m = dynamic_cast<DTROY*>(this->module);
	if (!m)
		return;
	menu->addChild(new MenuSeparator());
	menu->addChild(createMenuItem("Randomize pitches", "", [=]() { m->randomizePitch(); }));
	menu->addChild(createMenuItem("Randomize gates", "", [=]() { m->randomizeGates(); }));
	menu->addChild(createMenuItem("Randomize slides & skips", "", [=]() { m->randomizeSlidesSkips(); }));
	menu->addChild(createBoolPtrMenuItem("Stage outputs on every pulse", "", &m->engine.stepOutputsMode));
}

Model *modelDTROY = createModel<DTROY, DTROYWidget>("dTrOY");
