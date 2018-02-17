#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <sstream>
#include <iomanip>
#include "engine.hpp"

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
	int controlsTypes[16] = {0};
	bool controlFocused[16] = {false};

	SchmittTrigger typeTriggers[16];

	MOIRE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {	}

	void step() override;

	void randomizeTargetScene() {
		for (int i = 0; i < 16; i++) {
			scenes[targetScene][i]=randomf()*10;
		}
	}

	json_t *toJson() override {
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

			json_t *typeJ = json_integer(controlsTypes[i]);
			json_array_append_new(typesJ, typeJ);
		}
		json_object_set_new(rootJ, "scenes", scenesJ);
		json_object_set_new(rootJ, "types", typesJ);

		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		// scenes
		json_t *scenesJ = json_object_get(rootJ, "scenes");
		if (scenesJ) {
			for (int i = 0; i < 16; i++) {
				json_t *sceneJ = json_array_get(scenesJ, i);
				if (sceneJ) {
					for (int j = 0; j < 16; j++) {
						json_t *controlJ = json_array_get(sceneJ, j);
						if (controlJ) {
							scenes[i][j] = json_real_value(controlJ);
						}
					}
				}
			}
		}

		//controlTypes
		json_t *typesJ = json_object_get(rootJ, "types");
		if (typesJ) {
			for (int i = 0; i < 16; i++) {
				json_t *typeJ = json_array_get(typesJ, i);
				if (typeJ) {
					controlsTypes[i] = json_integer_value(typeJ);
				}
			}
		}
	}
};

void MOIRE::step() {
	targetScene = clampf(floor(inputs[TARGETSCENE_INPUT].value * 1.6f) + params[TARGETSCENE_PARAM].value , 0.0f, 15.0f);
	currentScene = clampf(floor(inputs[CURRENTSCENE_INPUT].value * 1.6f) + params[CURRENTSCENE_PARAM].value , 0.0f, 15.0f);

	for (int i = 0; i < 16; i++) {
		if (typeTriggers[i].process(params[TYPE_PARAMS + i].value)) {
			controlsTypes[i] = controlsTypes[i] == 0 ? 1 : 0;
		}
		lights[TYPE_LIGHTS + i].value = controlsTypes[i];
	}

	float coeff = clampf(inputs[MORPH_INPUT].value + params[MORPH_PARAM].value, 0.0f, 10.0f);

	for (int i = 0 ; i < 16; i++) {
		if (!controlFocused[i]) {
			if (controlsTypes[i] == 0) {
				currentValues[i] = rescalef(coeff,0.0f,10.0f,scenes[currentScene][i],scenes[targetScene][i]);
			} else {
				if (coeff >= 9.98f) {
					currentValues[i] = scenes[targetScene][i];
				}
				else {
					currentValues[i] = scenes[currentScene][i];
				}
			}
		}
		else {
			currentValues[i] = params[CONTROLS_PARAMS + i].value;
		}
		outputs[CV_OUTPUTS + i].value = currentValues[i] - 5.0f * params[VOLTAGE_PARAM].value;
	}
}

struct MOIRECKD6 : BlueCKD6 {
	void onMouseDown(EventMouseDown &e) override {
		MOIREWidget *parent = dynamic_cast<MOIREWidget*>(this->parent);
		MOIRE *module = dynamic_cast<MOIRE*>(this->module);
		if (parent && module) {
			if (this->paramId == MOIRE::ADONF_PARAM) {
				parent->morphButton->setValue(10.0f);
				for (int i = 0; i<16; i++){
					parent->controls[i]->setValue(module->scenes[module->targetScene][i]);
					module->controlFocused[i] = false;
				}
			} else if (this->paramId == MOIRE::NADA_PARAM) {
				parent->morphButton->setValue(0.0f);
				for (int i = 0; i<16; i++){
					parent->controls[i]->setValue(module->scenes[module->currentScene][i]);
					module->controlFocused[i] = false;
				}
			}
			else if (this->paramId == MOIRE::SAVE_PARAM) {
				for (int i = 0 ; i < 16; i++) {
					module->scenes[module->targetScene][i] = parent->controls[i]->value;
				}
			}
		}
		BlueCKD6::onMouseDown(e);
	}
};

struct MOIREDisplay : TransparentWidget {
	shared_ptr<Font> font;
	int *value;

	MOIREDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void drawMessage(NVGcontext *vg, Vec pos) {
		nvgFontSize(vg, 18);
		nvgFontFaceId(vg, font->handle);
		nvgTextLetterSpacing(vg, -2);
		nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0xff));
		std::stringstream ss;
		ss << std::setw(2) << std::setfill('0') << *value + 1;
		std::string s = ss.str();
		nvgText(vg, pos.x + 2, pos.y + 2, s.c_str(), NULL);
	}

	void draw(NVGcontext *vg) override {
		drawMessage(vg, Vec(0, 20));
	}
};

struct MOIREColoredKnob : BidooColoredKnob {
	void setValueNoEngine(float value) {
		float newValue = clampf(value, fminf(minValue, maxValue), fmaxf(minValue, maxValue));
		if (this->value != newValue) {
			this->value = newValue;
			this->dirty=true;
		}
	};

	void onDragStart(EventDragStart &e) override {
		RoundKnob::onDragStart(e);
		MOIRE *module = dynamic_cast<MOIRE*>(this->module);
		module->controlFocused[this->paramId - MOIRE::MOIRE::CONTROLS_PARAMS] = true;
	}
};

struct MOIREMorphKnob : BidooMorphKnob {
	void onMouseDown(EventMouseDown &e) override {
			MOIRE *module = dynamic_cast<MOIRE*>(this->module);
			for (int i = 0 ; i < 16; i++) {
				module->controlFocused[i] = false;
			}
			BidooMorphKnob::onMouseDown(e);
		}
};

MOIREWidget::MOIREWidget() {
	MOIRE *module = new MOIRE();
	setModule(module);
	box.size = Vec(15*15, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/MOIRE.svg")));
		addChild(panel);
	}

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));

	static const float portX0[10] = {20,34,48,62,76,90,120,150,180,210};
	static const float portY0[12] = {20,50,80,110,140,170,200,230,260,290,320,350};

	addParam(createParam<MOIRECKD6>(Vec(portX0[5], portY0[0]+18), module, MOIRE::SAVE_PARAM, 0.0f, 1.0f, 0.0f));

  addParam(createParam<BidooBlueTrimpot>(Vec(portX0[0], portY0[1]+16), module, MOIRE::TARGETSCENE_PARAM, 0.0f, 15.1f, 0.0f));
	MOIREDisplay *displayTarget = new MOIREDisplay();
	displayTarget->box.pos = Vec(50,portY0[2]-21);
	displayTarget->box.size = Vec(20, 20);
	displayTarget->value = &module->targetScene;
	addChild(displayTarget);
	addParam(createParam<BidooBlueTrimpot>(Vec(portX0[0], portY0[6]-5), module, MOIRE::CURRENTSCENE_PARAM, 0.0f, 15.1f, 0.0f));
	MOIREDisplay *displayCurrent = new MOIREDisplay();
	displayCurrent->box.pos = Vec(50,portY0[5]+19);
	displayCurrent->box.size = Vec(20, 20);
	displayCurrent->value = &module->currentScene;
	addChild(displayCurrent);

	addParam(createParam<MOIRECKD6>(Vec(portX0[0]-5, portY0[3]-3), module, MOIRE::ADONF_PARAM, 0.0f, 1.0f, 0.0f));
	addParam(createParam<MOIRECKD6>(Vec(portX0[0]-5, portY0[4]+5), module, MOIRE::NADA_PARAM, 0.0f, 1.0f, 0.0f));

	addInput(createInput<TinyPJ301MPort>(Vec(portX0[0]+2, portY0[0]+21), module, MOIRE::TARGETSCENE_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(portX0[0]+2, portY0[7]-6), module, MOIRE::CURRENTSCENE_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(portX0[0]+33.6, portY0[7]-6), module, MOIRE::MORPH_INPUT));
	morphButton = createParam<MOIREMorphKnob>(Vec(portX0[0]+27, portY0[3]+15), module, MOIRE::MORPH_PARAM, 0.0f, 10.0f, 0.0f);
	addParam(morphButton);

	addParam(createParam<CKSS>(Vec(40, 279), module, MOIRE::VOLTAGE_PARAM, 0.0f, 1.0f, 0.0f));
	for (int i = 0; i < 16; i++) {
		controls[i] = createParam<MOIREColoredKnob>(Vec(portX0[i%4+5], portY0[int(i/4) + 2]), module, MOIRE::CONTROLS_PARAMS + i, 0.0f, 10.0f, 0.0f);
		addParam(controls[i]);
		addParam(createParam<MiniLEDButton>(Vec(portX0[i%4+5]+24, portY0[int(i/4) + 2]+24), module, MOIRE::TYPE_PARAMS + i, 0.0f, 1.0f,  0.0f));
		addChild(createLight<SmallLight<RedLight>>(Vec(portX0[i%4+5]+24, portY0[int(i/4) + 2]+25), module, MOIRE::TYPE_LIGHTS + i));
		addOutput(createOutput<PJ301MPort>(Vec(portX0[i%4+5]+2, portY0[int(i/4) + 7]), module, MOIRE::CV_OUTPUTS + i));
	}
};

void MOIREWidget::step() {
	MOIRE *module = dynamic_cast<MOIRE*>(this->module);
	for (int i = 0; i < 16; i++) {
		if (!module->controlFocused[i]){
			MOIREColoredKnob* knob = dynamic_cast<MOIREColoredKnob*>(controls[i]);
			engineSetParam(module, controls[i]->paramId, module->currentValues[i]);
			knob->setValueNoEngine(module->currentValues[i]);
		}
	}
	ModuleWidget::step();
}

struct MOIRERandTargetSceneItem : MenuItem {
	MOIRE *moireModule;
	void onAction(EventAction &e) override {
		moireModule->randomizeTargetScene();
	}
};

Menu *MOIREWidget::createContextMenu() {
	Menu *menu = ModuleWidget::createContextMenu();

	MenuLabel *spacerLabel = new MenuLabel();
	menu->addChild(spacerLabel);

	MOIREWidget *dtroyWidget = dynamic_cast<MOIREWidget*>(this);
	assert(dtroyWidget);

	MOIRE *moireModule = dynamic_cast<MOIRE*>(module);
	assert(moireModule);

	MOIRERandTargetSceneItem *randomizeTargetSceneItem = new MOIRERandTargetSceneItem();
	randomizeTargetSceneItem->text = "Randomize target scene";
	randomizeTargetSceneItem->moireModule = moireModule;
	menu->addChild(randomizeTargetSceneItem);

	return menu;
}
