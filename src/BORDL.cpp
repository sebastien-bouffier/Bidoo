#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "MetroSeq.hpp"

using namespace std;

// DTROY with a gate probability, a random pitch and an accent per stage; the
// engine is shared with DTROY (MetroSeq.hpp).
struct BORDL : BidooModule {
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
	MetroEngine<BORDL> engine;

	BORDL() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configMetro(*this);
		configButton(PLAY_MODE_PARAM, "Play mode (forward, backward, ping-pong, random, brownian)");
		configButton(COUNT_MODE_PARAM, "Count mode (steps or pulses)");
		configOutput(ACC_OUTPUT, "Accent");
		for (int i = 0; i < 8; i++) {
			configButton(TRIG_SLIDE_PARAM + i, string::f("Stage %d slide", i + 1));
			configButton(TRIG_SKIP_PARAM + i, string::f("Stage %d skip", i + 1));
			configParam(TRIG_GATEPROB_PARAM + i, 0.f, 1.f, 1.f, string::f("Stage %d probability", i + 1), "%", 0.f, 100.f);
			configParam(TRIG_PITCHRND_PARAM + i, 0.f, 1.f, 0.f, string::f("Stage %d random pitch (100%% = +-5 V)", i + 1), "%", 0.f, 100.f);
			configParam(TRIG_ACCENT_PARAM + i, 0.f, 10.f, 0.f, string::f("Stage %d accent", i + 1), " V");
			configParam(TRIG_RNDACCENT_PARAM + i, 0.f, 1.f, 0.f, string::f("Stage %d random accent (100%% = +-5 V)", i + 1), "%", 0.f, 100.f);
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

	void randomizeSlidesSkips() {
		for (int i = 0; i < 8; i++) {
			engine.patterns[engine.selectedPattern].stageSlide[i] = random::uniform() > 0.8f;
			engine.patterns[engine.selectedPattern].stageSkip[i] = random::uniform() > 0.85f;
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

struct BORDLDisplay : TransparentWidget {
	BORDL *module;

	BORDLDisplay() {

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
				drawMessage(args.vg, Vec(0.0f, 20.0f), displayRootNote(module->engine.rootNote(*module, p)), displayPlayMode(p.playMode), selectedPattern, playedPattern, steps, displayScale(p.scale));
			}
		}
		Widget::drawLayer(args, layer);
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

struct BORDLWidget : BidooWidget {
	ParamWidget *stepsParam, *scaleParam, *rootNoteParam, *sensitivityParam,
	 *gateTimeParam, *slideTimeParam, *playModeParam, *countModeParam, *patternParam,
		*pitchParams[8], *pulseParams[8], *typeParams[8], *slideParams[8], *skipParams[8],
		 *pitchRndParams[8], *pulseProbParams[8], *accentParams[8], *rndAccentParams[8];

	void appendContextMenu(ui::Menu *menu) override;

	BORDLWidget(BORDL *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/BORDL.svg"));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			BORDLDisplay *display = new BORDLDisplay();
			display->module = module;
			display->box.pos = Vec(20.0f, 217.0f);
			display->box.size = Vec(250.0f, 60.0f);
			addChild(display);
		}

		static const float portX0[4] = {20.0f, 58.0f, 96.0f, 135.0f};

		addParam(createParam<BidooRoundBlackKnob>(Vec(portX0[0]-2.0f, 36.0f), module, BORDL::CLOCK_PARAM));
		addParam(createParam<LEDButton>(Vec(61.0f, 40.0f), module, BORDL::RUN_PARAM));
		addChild(createLight<SmallLight<GreenLight>>(Vec(67.0f, 46.0f), module, BORDL::RUNNING_LIGHT));
		addParam(createParam<LEDButton>(Vec(99.0f, 40.0f), module, BORDL::RESET_PARAM));
		addChild(createLight<SmallLight<GreenLight>>(Vec(105.0f, 46.0f), module, BORDL::RESET_LIGHT));
		stepsParam = createParam<BidooBlueSnapKnob>(Vec(portX0[3]-2.0f, 36.0f), module, BORDL::STEPS_PARAM);
		addParam(stepsParam);


	 	addInput(createInput<PJ301MPort>(Vec(portX0[0], 69.0f),  module, BORDL::CLOCK_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1], 69.0f),  module, BORDL::EXT_CLOCK_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[2], 69.0f),  module, BORDL::RESET_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[3], 69.0f),  module, BORDL::STEPS_INPUT));

		rootNoteParam = createParam<BidooBlueSnapKnob>(Vec(portX0[0]-2.0f, 116.0f), module, BORDL::ROOT_NOTE_PARAM);
		addParam(rootNoteParam);
		scaleParam = createParam<BidooBlueSnapKnob>(Vec(portX0[1]-2.0f, 116.0f), module, BORDL::SCALE_PARAM);
		addParam(scaleParam);
		gateTimeParam = createParam<BidooBlueKnob>(Vec(portX0[2]-2.0f, 116.0f), module, BORDL::GATE_TIME_PARAM);
		addParam(gateTimeParam);
		slideTimeParam = createParam<BidooBlueKnob>(Vec(portX0[3]-2.0f, 116.0f), module, BORDL::SLIDE_TIME_PARAM);
		addParam(slideTimeParam);

		addInput(createInput<PJ301MPort>(Vec(portX0[0], 149.0f),  module, BORDL::ROOT_NOTE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1], 149.0f),  module, BORDL::SCALE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[2], 149.0f),  module, BORDL::GATE_TIME_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[3], 149.0f),  module, BORDL::SLIDE_TIME_INPUT));

		addParam(createLightParam<VCVLightBezel<RedGreenBlueLight>>(Vec(portX0[0]+3.0f, 199.0f), module, BORDL::PLAY_MODE_PARAM, BORDL::PLAYMODE_LIGHT));
		addParam(createLightParam<VCVLightBezel<RedGreenBlueLight>>(Vec(portX0[1]+3.0f, 199.0f), module, BORDL::COUNT_MODE_PARAM, BORDL::COUNTMODE_LIGHT));


		addInput(createInput<PJ301MPort>(Vec(portX0[2], 198.0f),  module, BORDL::PATTERN_INPUT));
		patternParam = createParam<BidooRoundBlackSnapKnob>(Vec(portX0[3],196.0f), module, BORDL::PATTERN_PARAM);
		addParam(patternParam);

		static const float portX1[8] = {200.0f, 241.0f, 282.0f, 323.0f, 364.0f, 405.0f, 446.0f, 487.0f};

		sensitivityParam = createParam<BidooBlueTrimpot>(Vec(portX1[0]-24.0f, 30.0f), module, BORDL::SENSITIVITY_PARAM);
		addParam(sensitivityParam);
		addInput(createInput<TinyPJ301MPort>(Vec(portX1[0]-22.0f, 52.0f), module, BORDL::SENSITIVITY_INPUT));

		addInput(createInput<PJ301MPort>(Vec(portX0[0], 286.0f),  module, BORDL::TRANSPOSE_INPUT));
		addParam(createParam<BlueCKD6>(Vec(portX0[1]-1.0f, 285.0f), module, BORDL::COPY_PARAM));
		addChild(createLight<SmallLight<GreenLight>>(Vec(portX0[1]+23.0f, 283.0f), module, BORDL::COPY_LIGHT));

		addParam(createParam<LeftBtn>(Vec(104.0f, 290.0f), module, BORDL::LEFT_PARAM));
		addParam(createParam<RightBtn>(Vec(134.0f, 290.0f), module, BORDL::RIGHT_PARAM));
		addParam(createParam<UpBtn>(Vec(119.0f, 282.0f), module, BORDL::UP_PARAM));
		addParam(createParam<DownBtn>(Vec(119.0f, 297.0f), module, BORDL::DOWN_PARAM));

		for (int i = 0; i < 8; i++) {
			pitchParams[i] = createParam<BidooBlueKnob>(Vec(portX1[i]+1.0f, 56.0f), module, BORDL::TRIG_PITCH_PARAM + i);
			addParam(pitchParams[i]);
			pitchRndParams[i] = createParam<BidooBlueTrimpot>(Vec(portX1[i]+27.0f, 81.0f), module, BORDL::TRIG_PITCHRND_PARAM + i);
			addParam(pitchRndParams[i]);
			accentParams[i] = createParam<BidooBlueKnob>(Vec(portX1[i]+1.0f, 110.0f), module, BORDL::TRIG_ACCENT_PARAM + i);
			addParam(accentParams[i]);
			rndAccentParams[i] = createParam<BidooBlueTrimpot>(Vec(portX1[i]+27.0f, 135.0f), module, BORDL::TRIG_RNDACCENT_PARAM + i);
			addParam(rndAccentParams[i]);
			{
				BORDLPitchDisplay *displayPitch = new BORDLPitchDisplay();
				displayPitch->module = module;
				displayPitch->box.pos = Vec(portX1[i]+16.0f, 52.0f);
				displayPitch->box.size = Vec(20.0f, 10.0f);
				displayPitch->index = i;
				addChild(displayPitch);
			}
			pulseParams[i] = createParam<BidooBlueSnapKnob>(Vec(portX1[i]+1.0f, 188.0f), module, BORDL::TRIG_COUNT_PARAM + i);
			addParam(pulseParams[i]);
			pulseProbParams[i] = createParam<BidooBlueTrimpot>(Vec(portX1[i]+27.0f, 213.0f), module, BORDL::TRIG_GATEPROB_PARAM + i);
			addParam(pulseProbParams[i]);
			{
				BORDLPulseDisplay *displayPulse = new BORDLPulseDisplay();
				displayPulse->module = module;
				displayPulse->box.pos = Vec(portX1[i]+15.0f, 179.0f);
				displayPulse->box.size = Vec(20.0f, 10.0f);
				displayPulse->index = i;
				addChild(displayPulse);
			}
			typeParams[i] = createParam<BidooBlueSnapTrimpot>(Vec(portX1[i]+6.5f, 267.0f), module, BORDL::TRIG_TYPE_PARAM + i);
			addParam(typeParams[i]);
			{
				BORDLGateDisplay *displayGate = new BORDLGateDisplay();
				displayGate->module = module;
				displayGate->box.pos = Vec(portX1[i]+5.0f, 250.0f);
				displayGate->box.size = Vec(20.0f, 10.0f);
				displayGate->index = i;
				addChild(displayGate);
			}

			const float btnOffset = 3.5f;
			addParam(createLightParam<SmallLEDLightBezel<BlueLight>>(Vec(portX1[i]+7.0f + btnOffset, 295.0f + btnOffset), module, BORDL::TRIG_SLIDE_PARAM + i, BORDL::SLIDES_LIGHTS + i));
			addParam(createLightParam<SmallLEDLightBezel<BlueLight>>(Vec(portX1[i]+7.0f + btnOffset, 316.0f + btnOffset), module, BORDL::TRIG_SKIP_PARAM + i, BORDL::SKIPS_LIGHTS + i));

			addOutput(createOutput<TinyPJ301MPort>(Vec(portX1[i]+9.0f, 344.0f), module, BORDL::STEP_OUTPUT + i));
		}

		addInput(createInput<PJ301MPort>(Vec(9.5f, 330.0f), module, BORDL::EXTGATE1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(42.5f, 330.0f), module, BORDL::EXTGATE2_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(76.f, 330.0f), module, BORDL::GATE_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(109.f, 330.0f), module, BORDL::PITCH_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(142.0f, 330.0f), module, BORDL::ACC_OUTPUT));
	}
};

void BORDLWidget::appendContextMenu(ui::Menu *menu) {
	BidooWidget::appendContextMenu(menu);
	BORDL *m = dynamic_cast<BORDL*>(this->module);
	if (!m)
		return;
	menu->addChild(new MenuSeparator());
	menu->addChild(createMenuItem("Randomize pitches", "", [=]() { m->randomizePitch(); }));
	menu->addChild(createMenuItem("Randomize gates", "", [=]() { m->randomizeGates(); }));
	menu->addChild(createMenuItem("Randomize slides & skips", "", [=]() { m->randomizeSlidesSkips(); }));
	menu->addChild(createBoolPtrMenuItem("Stage outputs on every pulse", "", &m->engine.stepOutputsMode));
}

Model *modelBORDL = createModel<BORDL, BORDLWidget>("bordL");
