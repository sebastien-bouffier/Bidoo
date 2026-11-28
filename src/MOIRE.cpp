#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

struct MOIRE : BidooModule {
	enum ParamIds {
		CURRENTSCENE_PARAM,
		TARGETSCENE_PARAM,
		MORPH_PARAM,
		ADONF_PARAM,
		NADA_PARAM,
		SAVE_PARAM,
		VOLTAGE_PARAM,
		RND_PARAM,
		TYPE_PARAMS,
		CONTROLS_PARAMS = TYPE_PARAMS + 16,
		NUM_PARAMS = CONTROLS_PARAMS + 16
	};
	enum InputIds {
		TARGETSCENE_INPUT,
		CURRENTSCENE_INPUT,
		MORPH_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CV_OUTPUTS,
		POLY_OUTPUT = CV_OUTPUTS + 16,
		NUM_OUTPUTS
	};
	enum LightIds {
		TYPE_LIGHTS,
		NUM_LIGHTS = TYPE_LIGHTS + 16
	};

	float scenes[16][16] = {{0.0f}};
	int currentScene = 0;
	int  targetScene = 0;
	float currentValues[16] = {0.0f};
	int controlTypes[16] = {0};
	bool controlFocused[16] = {false};
	// what the widget last wrote to each control (UI thread): a param that differs was moved from outside (MIDI-Map, automation)
	float lastWritten[16] = {0.0f};
	dsp::ClockDivider lightDivider;

	dsp::SchmittTrigger typeTriggers[16];
	dsp::SchmittTrigger rndTrigger, saveTrigger, targetTrigger, currentTrigger;

	MOIRE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(SAVE_PARAM, "Save the controls to the target scene");
		configButton(RND_PARAM, "Randomise the target scene");
		configButton(ADONF_PARAM, "Go to the target scene");
		configButton(NADA_PARAM, "Go to the current scene");
		configParam(TARGETSCENE_PARAM, 0.0f, 15.0f, 0.0f, "Target scene", "", 0.f, 1.f, 1.f);
		configParam(CURRENTSCENE_PARAM, 0.0f, 15.0f, 0.0f, "Current scene", "", 0.f, 1.f, 1.f);
		paramQuantities[TARGETSCENE_PARAM]->snapEnabled = true;
		paramQuantities[CURRENTSCENE_PARAM]->snapEnabled = true;
		configParam(MORPH_PARAM, 0.0f, 10.0f, 0.0f, "Morph", "%", 0.f, 10.f);
		configSwitch(VOLTAGE_PARAM, 0.0f, 1.0f, 0.0f, "Output range", {"0 to 10 V", "-5 to 5 V"});
		lightDivider.setDivision(16);
		for (int i = 0; i < 16; i++) {
			std::string n = "Control " + std::to_string(i + 1);
			configParam(CONTROLS_PARAMS + i, 0.0f, 10.0f, 0.0f, n, " V");
			configButton(TYPE_PARAMS + i, n + " morph or jump (a jump changes at MORPH 50%)");
			configOutput(CV_OUTPUTS + i, n);
		}
		configOutput(POLY_OUTPUT, "All 16 controls (polyphonic)");
		configInput(TARGETSCENE_INPUT, "Target scene (0-10 V over the 16 scenes)");
		configInput(CURRENTSCENE_INPUT, "Current scene (0-10 V over the 16 scenes)");
		configInput(MORPH_INPUT, "Morph (0-10 V)");
	}

  void process(const ProcessArgs &args) override;

	void randomizeScenes() {
		for (int i = 0; i < 16; i++) {
			for (int j = 0; j < 16; j++) {
				scenes[i][j]=random::uniform()*10;
			}
			controlTypes[i] = (random::uniform()>0.5f) ? 1 : 0;
		}
	}

	void randomizeTargetScene() {
		for (int i = 0; i < 16; i++) {
				// values only: modes are global, shared by every scene
				scenes[targetScene][i]=random::uniform()*10;
		}
	}

	void syncWritten() {
		for (int i = 0; i < 16; i++)
			lastWritten[i] = params[CONTROLS_PARAMS + i].getValue();
	}

	void onRandomize() override {
		randomizeScenes();
		syncWritten();
	}

	void onReset() override {
		for (int i=0; i<16; i++) {
			for (int j=0; j<16; j++) {
				scenes[i][j]=0.0f;
			}
			controlTypes[i] = 0;
			controlFocused[i] = false;
		}
		syncWritten();
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_t *scenesJ = json_array();
		json_t *typesJ = json_array();
		for (int i = 0; i < 16; i++) {
			json_t *sceneJ = json_array();
			for (int j = 0; j < 16; j++) {
				json_t *controlJ = json_real(scenes[i][j]);
				json_array_append_new(sceneJ, controlJ);
			}
			json_array_append_new(scenesJ, sceneJ);

			json_t *typeJ = json_integer(controlTypes[i]);
			json_array_append_new(typesJ, typeJ);
		}
		json_object_set_new(rootJ, "scenes", scenesJ);
		json_object_set_new(rootJ, "types", typesJ);

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *scenesJ = json_object_get(rootJ, "scenes");
		json_t *typesJ = json_object_get(rootJ, "types");
		if (scenesJ && typesJ) {
			for (int i = 0; i < 16; i++) {
				json_t *sceneJ = json_array_get(scenesJ, i);
				if (sceneJ) {
					for (int j = 0; j < 16; j++) {
						json_t *controlJ = json_array_get(sceneJ, j);
						if (controlJ) {
							scenes[i][j] = json_number_value(controlJ);
						}
					}
				}
				json_t *typeJ = json_array_get(typesJ, i);
				if (typeJ) {
					controlTypes[i] = json_integer_value(typeJ) ? 1 : 0;
				}
			}
		}
		syncWritten();
	}
};

void MOIRE::process(const ProcessArgs &args) {
	targetScene = clamp((int)std::floor(inputs[TARGETSCENE_INPUT].getVoltage() * 1.6f) + (int)params[TARGETSCENE_PARAM].getValue(), 0, 15);
	currentScene = clamp((int)std::floor(inputs[CURRENTSCENE_INPUT].getVoltage() * 1.6f) + (int)params[CURRENTSCENE_PARAM].getValue(), 0, 15);

	// the scene buttons work from the engine, so MIDI and CV mapping reach them
	if (saveTrigger.process(params[SAVE_PARAM].getValue())) {
		for (int i = 0; i < 16; i++)
			scenes[targetScene][i] = currentValues[i];
	}
	if (targetTrigger.process(params[ADONF_PARAM].getValue())) {
		params[MORPH_PARAM].setValue(10.0f);
		for (int i = 0; i < 16; i++)
			controlFocused[i] = false;
	}
	if (currentTrigger.process(params[NADA_PARAM].getValue())) {
		params[MORPH_PARAM].setValue(0.0f);
		for (int i = 0; i < 16; i++)
			controlFocused[i] = false;
	}

	if (rndTrigger.process(params[RND_PARAM].getValue())) {
		randomizeTargetScene();
	}

	bool lightsNow = lightDivider.process();
	for (int i = 0; i < 16; i++) {
		if (typeTriggers[i].process(params[TYPE_PARAMS + i].getValue())) {
			controlTypes[i] = controlTypes[i] == 0 ? 1 : 0;
		}
		if (lightsNow)
			lights[TYPE_LIGHTS + i].setBrightness(controlTypes[i]);
	}

	float coeff = clamp(inputs[MORPH_INPUT].getVoltage() + params[MORPH_PARAM].getValue(), 0.0f, 10.0f);

	for (int i = 0 ; i < 16; i++) {
		if (!controlFocused[i]) {
			if (controlTypes[i] == 0) {
				currentValues[i] = rescale(coeff,0.0f,10.0f,scenes[currentScene][i],scenes[targetScene][i]);
			} else {
				if (coeff >= 5.f) {
					currentValues[i] = scenes[targetScene][i];
				}
				else {
					currentValues[i] = scenes[currentScene][i];
				}
			}
		}
		else {
			currentValues[i] = params[CONTROLS_PARAMS + i].getValue();
		}
		outputs[CV_OUTPUTS + i].setVoltage(currentValues[i] - 5.0f * params[VOLTAGE_PARAM].getValue());
		outputs[POLY_OUTPUT].setVoltage(outputs[CV_OUTPUTS + i].getVoltage(), i);
	}
	outputs[POLY_OUTPUT].setChannels(16);
}

struct MOIREWidget : BidooWidget {
	ParamWidget *controls[16];
	ParamWidget *morphButton;
	void step() override;
	void appendContextMenu(ui::Menu *menu) override;
  MOIREWidget(MOIRE *module);
};

struct MOIREDisplay : TransparentWidget {
	int *value;
	Vec textAt;   // the number's baseline start inside the screen

	MOIREDisplay() {

	}

	void drawMessage(NVGcontext *vg) {
    if (value) {
      nvgFontSize(vg, 18);
  		nvgTextLetterSpacing(vg, -2);
  		nvgFillColor(vg, YELLOW_BIDOO);
  		char s[8];
  		snprintf(s, sizeof(s), "%02d", *value + 1);
  		nvgText(vg, textAt.x, textAt.y, s, NULL);
    }
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			drawMessage(args.vg);
		}
		Widget::drawLayer(args, layer);
	}

};

struct MOIREColoredKnob : BidooColoredKnob {
	void setValueNoEngine(float value) {
		float newValue = clamp(value, fminf(this->getParamQuantity()->getMinValue(), this->getParamQuantity()->getMaxValue()), fmaxf(this->getParamQuantity()->getMinValue(), this->getParamQuantity()->getMaxValue()));
		if (this->getParamQuantity()->getValue() != newValue) {
			this->getParamQuantity()->setValue(newValue);
		}
	};

	void onDragStart(const event::DragStart &e) override {
		RoundKnob::onDragStart(e);
		// no module in the library browser
		ParamQuantity *pq = this->getParamQuantity();
		MOIRE *module = pq ? dynamic_cast<MOIRE*>(pq->module) : nullptr;
		if (!module)
			return;
		module->controlFocused[this->getParamQuantity()->paramId - MOIRE::MOIRE::CONTROLS_PARAMS] = true;
	}
};

struct MOIREMorphKnob : BidooMorphKnob {
	void onButton(const event::Button &e) override {
			// no module in the library browser
			ParamQuantity *pq = this->getParamQuantity();
			MOIRE *module = pq ? dynamic_cast<MOIRE*>(pq->module) : nullptr;
			if (module) {
				for (int i = 0 ; i < 16; i++) {
					module->controlFocused[i] = false;
				}
			}
			BidooMorphKnob::onButton(e);
		}
};


MOIREWidget::MOIREWidget(MOIRE *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/MOIRE.svg"));

	theme::addScrews(this);

	// The panel of tools/panels/moire.py: the old arrangement. Left, symmetric about the morph knob's row: the target
	// and current scenes, each a trimpot beside its screen, an arrow button and a CV jack; the morph knob between the
	// screens; the range switch and POLY. Right: SAVE and RND, the 4 x 4 knobs with their type buttons, the 16 outputs.
	const theme::Size kColoredKnobSize = {mm2px(8.f), mm2px(8.f)};    // BidooColoredKnob
	auto button = [&](theme::Pos p, int paramId) {   // the house round button, momentary as the old ones
		BidooRoundButton *b = theme::param<BidooRoundButton>(p, module, paramId);
		b->momentary = true;
		addParam(b);
		return b;
	};

	// the scenes: the trimpots on the old lines' column (they were 0.55 and 0.35 px lower), the screens and the morph
	// knob on the hourglass's (the knob was 0.24 px left and 1.24 px higher); AXIS, SCENE_X, MORPH_X... in moire.py
	const float kAxis = 140.f;
	const float kSceneFromAxis = 64.95f;   // the screens' and trimpots' centres from the axis
	const float kButtonFromAxis = 19.f;    // the arrow buttons, at the old CKD6s' centres
	const theme::Pos scene = theme::specific(29, kAxis);
	const theme::Pos morph = theme::specific(61, kAxis);
	addParam(theme::param<BidooBlueSnapTrimpot>(scene.down(-kSceneFromAxis), module, MOIRE::TARGETSCENE_PARAM));
	addParam(theme::param<BidooBlueSnapTrimpot>(scene.down(kSceneFromAxis), module, MOIRE::CURRENTSCENE_PARAM));
	button(scene.down(-kButtonFromAxis), MOIRE::ADONF_PARAM);
	button(scene.down(kButtonFromAxis), MOIRE::NADA_PARAM);
	morphButton = theme::param<MOIREMorphKnob>(morph, module, MOIRE::MORPH_PARAM);
	addParam(morphButton);

	// the scene screens the panel draws (SCREEN_W, SCREEN_H in moire.py), the numbers where they were (within 0.05 px)
	const float kScreenW = 30.f, kScreenH = 20.f;
	const Vec kNumberAt = Vec(6.f, 16.f);
	auto display = [&](float side, int *value) {
		MOIREDisplay *d = new MOIREDisplay();
		d->box = theme::specificRect(morph.x - kScreenW * 0.5f, kAxis + side * kSceneFromAxis - kScreenH * 0.5f, kScreenW, kScreenH);
		d->textAt = kNumberAt;
		d->value = value;
		addChild(d);
	};
	display(-1.f, module ? &module->targetScene : NULL);
	display(1.f, module ? &module->currentScene : NULL);

	// the CV jacks, full jacks in one box each where tiny ones were: the target's 3.5 px higher, so its box clears the
	// trimpot (TARGET_CV_Y in moire.py); the current scene's and the morph's on grid row 2 (they were 18.5 px higher)
	addInput(theme::input<PJ301MPort>(theme::colRow(scene, theme::specific(0, 45)), module, MOIRE::TARGETSCENE_INPUT));
	addInput(theme::input<PJ301MPort>(theme::sameCol(scene, theme::row(2)), module, MOIRE::CURRENTSCENE_INPUT));
	addInput(theme::input<PJ301MPort>(theme::sameCol(morph, theme::row(2)), module, MOIRE::MORPH_INPUT));

	// the range switch between its labels (2.7 px lower, centred between the boxes), POLY under it on the bottom row
	// (0.75 px right, 10.15 px lower): SWITCH, POLY_X in moire.py
	const theme::Pos range = theme::specific(47.75f, 292.f);
	addParam(theme::param<CKSS>(range, module, MOIRE::VOLTAGE_PARAM));
	addOutput(theme::output<PJ301MPort>(theme::sameCol(range, theme::row(0)), module, MOIRE::POLY_OUTPUT));

	// right: the knob grid at its old corners, in the outputs' columns; SAVE and RND over the first two columns (0.15 px
	// left), at the old CKD6s' centres. The outputs' rows spread from grid row 2 to the bottom row (8.15 to 10.15 px
	// lower). COL0, PITCH, KNOB_Y0, OUT_PITCH in moire.py
	const float kGridPitch = 30.f;
	const theme::Pos firstCol = theme::specificCorner(92, 230, theme::kJackSize);
	const float kOutPitch = (theme::row(0) - theme::row(2)) / 3.f;
	const theme::Pos save = theme::sameCol(firstCol, theme::specificCorner(90, 38, theme::kCKD6Size).y);
	button(save, MOIRE::SAVE_PARAM);
	button(save.right(kGridPitch), MOIRE::RND_PARAM);

	// each knob's type button: a small round button at the knob's lower right, where the old mini button and its light
	// were, lit (orange) by the type the module keeps in its light
	const float kTypeButton = 10.f;               // its size: the old light was 6 px, 20 px would not fit the grid
	const float kTypeFromKnob = 14.f;             // its centre right of and below the knob's
	for (int i = 0; i < 16; i++) {
		const float dx = kGridPitch * (i % 4), dy = kGridPitch * (i / 4);
		const theme::Pos knob = theme::colRow(firstCol.right(dx), theme::specificCorner(0, 82 + dy, kColoredKnobSize));
		controls[i] = theme::param<MOIREColoredKnob>(knob, module, MOIRE::CONTROLS_PARAMS + i);
		addParam(controls[i]);
		const theme::Pos typeAt = knob.offset(kTypeFromKnob, kTypeFromKnob);
		BidooRoundButton *type = button(typeAt, MOIRE::TYPE_PARAMS + i);
		type->box.size = Vec(kTypeButton, kTypeButton);   // smaller: placed again at the same centre
		theme::place(type, typeAt);
		const int lightId = MOIRE::TYPE_LIGHTS + i;
		type->litBy = [=]() { return module && module->lights[lightId].getBrightness() > 0.5f; };
		addOutput(theme::output<PJ301MPort>(theme::sameCol(firstCol.right(dx), theme::row(2) + kOutPitch * (i / 4)), module, MOIRE::CV_OUTPUTS + i));
	}
}

struct MoireItem : MenuItem {
	MOIRE *module;
	void onAction(const event::Action &e) override {
			module->randomizeTargetScene();
	}
};

void MOIREWidget::appendContextMenu(ui::Menu *menu) {
	BidooWidget::appendContextMenu(menu);
	MOIRE *module = dynamic_cast<MOIRE*>(this->module);
	if (!module)
		return;
	menu->addChild(new MenuSeparator());
	menu->addChild(construct<MoireItem>(&MenuItem::text, "Randomize target scene only", &MoireItem::module, module));
}

void MOIREWidget::step() {
	MOIRE *module = dynamic_cast<MOIRE*>(this->module);
	for (int i = 0; module && i < 16; i++) {
		ParamQuantity *pq = controls[i]->getParamQuantity();
		float v = pq->getValue();
		// an outside move takes the control over, as a mouse grab does
		if (!module->controlFocused[i] && std::fabs(v - module->lastWritten[i]) > 1e-3f)
			module->controlFocused[i] = true;
		if (!module->controlFocused[i]) {
			pq->setValue(module->currentValues[i]);
			v = pq->getValue();
		}
		module->lastWritten[i] = v;
	}
	BidooWidget::step();
}

Model *modelMOIRE = createModel<MOIRE, MOIREWidget>("MOiRE");
