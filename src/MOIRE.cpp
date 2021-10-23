#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <sstream>
#include <iomanip>

using namespace std;

struct MOIRE : Module {
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
		NUM_OUTPUTS = CV_OUTPUTS + 16
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

	dsp::SchmittTrigger typeTriggers[16];
	dsp::SchmittTrigger rndTrigger;

	MOIRE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
    configParam(SAVE_PARAM, 0.0f, 1.0f, 0.0f);
    configParam(TARGETSCENE_PARAM, 0.0f, 15.1f, 0.0f);
    configParam(CURRENTSCENE_PARAM, 0.0f, 15.1f, 0.0f);
    configParam(ADONF_PARAM, 0.0f, 1.0f, 0.0f);
    configParam(NADA_PARAM, 0.0f, 1.0f, 0.0f);
    configParam(MORPH_PARAM, 0.0f, 10.0f, 0.0f);
    configParam(VOLTAGE_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(RND_PARAM, 0.0f, 1.0f, 0.0f);

  	for (int i = 0; i < 16; i++) {
      configParam(CONTROLS_PARAMS + i, 0.0f, 10.0f, 0.0f);
      configParam(TYPE_PARAMS + i, 0.0f, 1.0f,  0.0f);
  	}
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
				scenes[targetScene][i]=random::uniform()*10;
				controlTypes[i] = (random::uniform()>0.5f) ? 1 : 0;
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
		}
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		// scenes
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
	targetScene = clamp(floor(inputs[TARGETSCENE_INPUT].getVoltage() * 1.5f) + params[TARGETSCENE_PARAM].getValue() , 0.0f, 15.0f);
	currentScene = clamp(floor(inputs[CURRENTSCENE_INPUT].getVoltage() * 1.5f) + params[CURRENTSCENE_PARAM].getValue() , 0.0f, 15.0f);

	if (rndTrigger.process(params[RND_PARAM].getValue())) {
		randomizeTargetScene();
	}

	for (int i = 0; i < 16; i++) {
		if (typeTriggers[i].process(params[TYPE_PARAMS + i].getValue())) {
			controlTypes[i] = controlTypes[i] == 0 ? 1 : 0;
		}
		lights[TYPE_LIGHTS + i].setBrightness(controlTypes[i]);
	}

	float coeff = clamp(inputs[MORPH_INPUT].getVoltage() + params[MORPH_PARAM].getValue(), 0.0f, 10.0f);

	for (int i = 0 ; i < 16; i++) {
		if (!controlFocused[i]) {
			if (controlTypes[i] == 0) {
				currentValues[i] = rescale(coeff,0.0f,10.0f,scenes[currentScene][i],scenes[targetScene][i]);
			} else {
				if (coeff == 10.f) {
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
	}
}

struct MOIREWidget : ModuleWidget {
	ParamWidget *controls[16];
	ParamWidget *morphButton;
	void step() override;
	void appendContextMenu(ui::Menu *menu) override;
  MOIREWidget(MOIRE *module);
};

struct MOIRECKD6 : BlueCKD6 {
	void onButton(const event::Button &e) override {
		MOIREWidget *parent = dynamic_cast<MOIREWidget*>(this->parent);
		MOIRE *module = dynamic_cast<MOIRE*>(this->getParamQuantity()->module);
		if (parent && module) {
			if (this->getParamQuantity()->paramId == MOIRE::ADONF_PARAM) {
				parent->morphButton->getParamQuantity()->setValue(10.0f);
				for (int i = 0; i<16; i++){
					parent->controls[i]->getParamQuantity()->setValue(module->scenes[module->targetScene][i]);
					module->controlFocused[i] = false;
				}
			} else if (this->getParamQuantity()->paramId == MOIRE::NADA_PARAM) {
				parent->morphButton->getParamQuantity()->setValue(0.0f);
				for (int i = 0; i<16; i++){
					parent->controls[i]->getParamQuantity()->setValue(module->scenes[module->currentScene][i]);
					module->controlFocused[i] = false;
				}
			}
			else if (this->getParamQuantity()->paramId == MOIRE::SAVE_PARAM) {
				for (int i = 0 ; i < 16; i++) {
					module->scenes[module->targetScene][i] = parent->controls[i]->getParamQuantity()->getValue();
				}
			}
		}
		BlueCKD6::onButton(e);
	}
};

struct MOIREDisplay : TransparentWidget {
	int *value;

	std::string fontPath = "res/DejaVuSansMono.ttf";

	MOIREDisplay() {

	}

	void drawMessage(NVGcontext *vg, Vec pos) {
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, fontPath));
    if (value) {
      nvgFontSize(vg, 18);
  		nvgFontFaceId(vg, font->handle);
  		nvgTextLetterSpacing(vg, -2);
  		nvgFillColor(vg, YELLOW_BIDOO);
  		std::stringstream ss;
  		ss << std::setw(2) << std::setfill('0') << *value + 1;
  		std::string s = ss.str();
  		nvgText(vg, pos.x + 2, pos.y + 2, s.c_str(), NULL);
    }
	}

	void draw(const DrawArgs &args) override {
		nvgGlobalTint(args.vg, color::WHITE);
		drawMessage(args.vg, Vec(0, 20));
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
	setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MOIRE.svg")));

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	static const float portX0[10] = {20,34,48,62,76,90,120,150,180,210};
	static const float portY0[12] = {20,50,80,110,140,170,200,230,260,290,320,350};

	addParam(createParam<MOIRECKD6>(Vec(portX0[5], portY0[0]+18), module, MOIRE::SAVE_PARAM));
	addParam(createParam<MOIRECKD6>(Vec(portX0[5]+30, portY0[0]+18), module, MOIRE::RND_PARAM));

  addParam(createParam<BidooBlueSnapTrimpot>(Vec(portX0[0], portY0[1]+16), module, MOIRE::TARGETSCENE_PARAM));
	MOIREDisplay *displayTarget = new MOIREDisplay();
	displayTarget->box.pos = Vec(50,portY0[2]-21);
	displayTarget->box.size = Vec(20, 20);
	displayTarget->value = module ? &module->targetScene : NULL;
	addChild(displayTarget);
	addParam(createParam<BidooBlueSnapTrimpot>(Vec(portX0[0], portY0[6]-5), module, MOIRE::CURRENTSCENE_PARAM));
	MOIREDisplay *displayCurrent = new MOIREDisplay();
	displayCurrent->box.pos = Vec(50,portY0[5]+19);
	displayCurrent->box.size = Vec(20, 20);
	displayCurrent->value = module ? &module->currentScene : NULL;
	addChild(displayCurrent);

	addParam(createParam<MOIRECKD6>(Vec(portX0[0]-5, portY0[3]-3), module, MOIRE::ADONF_PARAM));
	addParam(createParam<MOIRECKD6>(Vec(portX0[0]-5, portY0[4]+5), module, MOIRE::NADA_PARAM));

	addInput(createInput<TinyPJ301MPort>(Vec(portX0[0]+2, portY0[0]+21), module, MOIRE::TARGETSCENE_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(portX0[0]+2, portY0[7]-6), module, MOIRE::CURRENTSCENE_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(portX0[0]+33.2, portY0[7]-6), module, MOIRE::MORPH_INPUT));

	morphButton = createParam<MOIREMorphKnob>(Vec(portX0[0]+26, portY0[3]+14), module, MOIRE::MORPH_PARAM);
	addParam(morphButton);

	addParam(createParam<CKSS>(Vec(40, 279), module, MOIRE::VOLTAGE_PARAM));

	for (int i = 0; i < 16; i++) {
		controls[i] = createParam<MOIREColoredKnob>(Vec(portX0[i%4+5]+1, portY0[int(i/4) + 2] + 2), module, MOIRE::CONTROLS_PARAMS + i);
		addParam(controls[i]);
		addParam(createParam<MiniLEDButton>(Vec(portX0[i%4+5]+24, portY0[int(i/4) + 2]+24), module, MOIRE::TYPE_PARAMS + i));
		addChild(createLight<SmallLight<RedLight>>(Vec(portX0[i%4+5]+24, portY0[int(i/4) + 2]+25), module, MOIRE::TYPE_LIGHTS + i));
		addOutput(createOutput<PJ301MPort>(Vec(portX0[i%4+5]+2, portY0[int(i/4) + 7]), module, MOIRE::CV_OUTPUTS + i));
	}
}

struct MoireItem : MenuItem {
	MOIRE *module;
	void onAction(const event::Action &e) override {
			module->randomizeTargetScene();
	}
};

void MOIREWidget::appendContextMenu(ui::Menu *menu) {
	MOIRE *module = dynamic_cast<MOIRE*>(this->module);
	assert(module);
	menu->addChild(construct<MenuLabel>());
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
	ModuleWidget::step();
}

Model *modelMOIRE = createModel<MOIRE, MOIREWidget>("MOiRE");
