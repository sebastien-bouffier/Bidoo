#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <sstream>
#include <iomanip>

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

	void onRandomize() override {
		randomizeScenes();
	}

	void onReset() override {
		for (int i=0; i<16; i++) {
			for (int j=0; j<16; j++) {
				scenes[i][j]=0.0f;
			}
			controlTypes[i] = 0.0f;
			controlFocused[i] = false;
		}
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
					controlTypes[i] = json_integer_value(typeJ);
				}
			}
		}
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

	MOIREDisplay() {

	}

	void drawMessage(NVGcontext *vg, Vec pos) {
    if (value) {
      nvgFontSize(vg, 18);
  		nvgTextLetterSpacing(vg, -2);
  		nvgFillColor(vg, YELLOW_BIDOO);
  		std::stringstream ss;
  		ss << std::setw(2) << std::setfill('0') << *value + 1;
  		std::string s = ss.str();
  		nvgText(vg, pos.x + 2, pos.y + 2, s.c_str(), NULL);
    }
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			drawMessage(args.vg, Vec(0, 20));
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
		MOIRE *module = dynamic_cast<MOIRE*>(this->getParamQuantity()->module);
		module->controlFocused[this->getParamQuantity()->paramId - MOIRE::MOIRE::CONTROLS_PARAMS] = true;
	}
};

struct MOIREMorphKnob : BidooMorphKnob {
	void onButton(const event::Button &e) override {
			MOIRE *module = dynamic_cast<MOIRE*>(this->getParamQuantity()->module);
			for (int i = 0 ; i < 16; i++) {
				module->controlFocused[i] = false;
			}
			BidooMorphKnob::onButton(e);
		}
};


MOIREWidget::MOIREWidget(MOIRE *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/MOIRE.svg"));

	theme::addScrews(this);

	// The original hand-drawn panel. Left: the target and current scene trimpots with their CV jacks, joined by the
	// panel's lines, the two arrow buttons, the scene screens and the morph knob with its CV jack; the range switch and
	// POLY. Right: the 4 x 4 grid of knobs in the white screen, each with its type button, and the 16 CV outputs below.
	// Old corners kept, except: the trimpots and their jacks, now on the lines the panel draws (they were 0.5 to 0.6 px
	// right), the range switch between its labels (0.75 px left), and the knob grid with its type buttons, now in the
	// outputs' columns, centred in the white screen (it was 1.04 px left).
	const theme::Size kCKSSSize = {14.f, 20.64111f};                  // CKSS
	const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};     // SmallLight
	const theme::Size kMiniButtonSize = {mm2px(1.8f), mm2px(1.8f)};   // MiniLEDButton
	const theme::Size kColoredKnobSize = {mm2px(8.f), mm2px(8.f)};    // BidooColoredKnob
	const theme::Pos sceneLine = theme::specific(29.f, 0);   // the lines from the scene trimpots to their jacks

	const theme::Pos save = theme::specificCorner(90, 38, theme::kCKD6Size);
	addParam(theme::param<BlueCKD6>(save, module, MOIRE::SAVE_PARAM));
	addParam(theme::param<BlueCKD6>(save.right(30), module, MOIRE::RND_PARAM));

	addParam(theme::param<BidooBlueSnapTrimpot>(theme::colRow(sceneLine, theme::specificCorner(0, 66, theme::kTrimpotSize)), module, MOIRE::TARGETSCENE_PARAM));
	MOIREDisplay *displayTarget = new MOIREDisplay();
	displayTarget->box = theme::specificRect(50, 59, 20, 20);
	displayTarget->value = module ? &module->targetScene : NULL;
	addChild(displayTarget);
	addParam(theme::param<BidooBlueSnapTrimpot>(theme::colRow(sceneLine, theme::specificCorner(0, 195, theme::kTrimpotSize)), module, MOIRE::CURRENTSCENE_PARAM));
	MOIREDisplay *displayCurrent = new MOIREDisplay();
	displayCurrent->box = theme::specificRect(50, 189, 20, 20);
	displayCurrent->value = module ? &module->currentScene : NULL;
	addChild(displayCurrent);

	addParam(theme::param<BlueCKD6>(theme::specificCorner(15, 107, theme::kCKD6Size), module, MOIRE::ADONF_PARAM));
	addParam(theme::param<BlueCKD6>(theme::specificCorner(15, 145, theme::kCKD6Size), module, MOIRE::NADA_PARAM));

	addInput(theme::input<TinyPJ301MPort>(theme::colRow(sceneLine, theme::specificCorner(0, 41, theme::kTinyJackSize)), module, MOIRE::TARGETSCENE_INPUT));
	addInput(theme::input<TinyPJ301MPort>(theme::colRow(sceneLine, theme::specificCorner(0, 224, theme::kTinyJackSize)), module, MOIRE::CURRENTSCENE_INPUT));
	addInput(theme::input<TinyPJ301MPort>(theme::specificCorner(20 + 33.2f, 224, theme::kTinyJackSize), module, MOIRE::MORPH_INPUT));

	morphButton = theme::param<MOIREMorphKnob>(theme::specificCorner(46, 124, theme::kBlueKnobSize), module, MOIRE::MORPH_PARAM);
	addParam(morphButton);

	addParam(theme::param<CKSS>(theme::colRow(theme::specific(47.75f, 0), theme::specificCorner(0, 279, kCKSSSize)), module, MOIRE::VOLTAGE_PARAM));   // between its labels
	// under the range switch, level with the last row of outputs
	addOutput(theme::output<PJ301MPort>(theme::specificCorner(35.15f, 320, theme::kJackSize), module, MOIRE::POLY_OUTPUT));

	const float kGridPitch = 30.f;
	const theme::Pos firstOut = theme::specificCorner(92, 230, theme::kJackSize);
	const float kButtonFromKnob = 23.f; // a type button's corner from its knob's
	for (int i = 0; i < 16; i++) {
		const float dx = kGridPitch * (i % 4), dy = kGridPitch * (i / 4);
		const theme::Pos out = firstOut.offset(dx, dy);
		const theme::Pos knob = theme::colRow(out, theme::specificCorner(0, 82 + dy, kColoredKnobSize));   // in its output's column
		const float knobLeft = knob.x - kColoredKnobSize.w * 0.5f;
		controls[i] = theme::param<MOIREColoredKnob>(knob, module, MOIRE::CONTROLS_PARAMS + i);
		addParam(controls[i]);
		addParam(theme::param<MiniLEDButton>(theme::specificCorner(knobLeft + kButtonFromKnob, 104 + dy, kMiniButtonSize), module, MOIRE::TYPE_PARAMS + i));
		addChild(theme::light<SmallLight<RedLight>>(theme::specificCorner(knobLeft + kButtonFromKnob, 105 + dy, kSmallLightSize), module, MOIRE::TYPE_LIGHTS + i));
		addOutput(theme::output<PJ301MPort>(out, module, MOIRE::CV_OUTPUTS + i));
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
	assert(module);
	menu->addChild(new MenuSeparator());
	menu->addChild(construct<MoireItem>(&MenuItem::text, "Randomize target scene only", &MoireItem::module, module));
}

void MOIREWidget::step() {
	MOIRE *module = dynamic_cast<MOIRE*>(this->module);
	for (int i = 0; i < 16; i++) {
		if (module && !module->controlFocused[i]){
			MOIREColoredKnob* knob = dynamic_cast<MOIREColoredKnob*>(controls[i]);
      knob->getParamQuantity()->setValue(module->currentValues[i]);
		}
	}
	BidooWidget::step();
}

Model *modelMOIRE = createModel<MOIRE, MOIREWidget>("MOiRE");
