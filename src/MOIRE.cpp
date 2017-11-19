#include "Bidoo.hpp"
#include "BidooComponents.hpp"
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
		CONTROLS_PARAMS,
		NUM_PARAMS = CONTROLS_PARAMS + 16
	};
	enum InputIds {
		NUM_INPUTS
	};
	enum OutputIds {
		CV_OUTPUTS,
		NUM_OUTPUTS = CV_OUTPUTS + 16
	};
	enum LightIds {
		NUM_LIGHTS
	};

	float scenes[16][16] = {{0}};
	int currentScene = 0;
	int  targetScene = 0;
	float currentValues[16] = {0};

	MOIRE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {	}

	void step() override;

	json_t *toJson() override {
		json_t *rootJ = json_object();
		// scenes
		json_t *scenesJ = json_array();
		for (int i = 0; i < 16; i++) {
			json_t *sceneJ = json_array();
			for (int j = 0; j < 16; j++) {
				json_t *controlJ = json_real(scenes[i][j]);
				json_array_append_new(sceneJ, controlJ);
			}
			json_array_append_new(scenesJ, sceneJ);
		}
		json_object_set_new(rootJ, "scenes", scenesJ);

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
	}
};


void MOIRE::step() {
	currentScene = (int)params[CURRENTSCENE_PARAM].value;
	targetScene = (int)params[TARGETSCENE_PARAM].value;

	for (int i = 0 ; i < 16; i++) {
		outputs[CV_OUTPUTS + i].value = params[CONTROLS_PARAMS + i].value - 5 * params[VOLTAGE_PARAM].value;
	}
}

struct MOIRECKD6 : CKD6 {
	void onMouseDown(EventMouseDown &e) override {
		MOIREWidget *parent = dynamic_cast<MOIREWidget*>(this->parent);
		MOIRE *module = dynamic_cast<MOIRE*>(this->module);
		if (parent && module) {
			if (this->paramId == MOIRE::ADONF_PARAM) {
				parent->slider->setValue(10);
				module->params[MOIRE::MORPH_PARAM].value = 10;
			} else if (this->paramId == MOIRE::NADA_PARAM) {
				parent->slider->setValue(0);
				module->params[MOIRE::MORPH_PARAM].value = 0;
			}
			else if (this->paramId == MOIRE::SAVE_PARAM) {
				for (int i = 0 ; i < 16; i++) {
					module->scenes[module->targetScene][i] = module->params[MOIRE::CONTROLS_PARAMS + i].value;
				}
			}
		}
		CKD6::onMouseDown(e);
	}
};

struct MOIRELongSlider : BidooLongSlider {
	void onChange(EventChange &e) override {
		MOIREWidget *parent = dynamic_cast<MOIREWidget*>(this->parent);
		MOIRE *module = dynamic_cast<MOIRE*>(this->module);
		if (parent && module) {
			for (int i = 0; i < 16; i++) {
				parent->controls[i]->setValue((module->scenes[module->targetScene][i] - module->scenes[module->currentScene][i])*this->value/10 + module->scenes[module->currentScene][i]);
			}
		}
		BidooLongSlider::onChange(e);
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

	addParam(createParam<MOIRECKD6>(Vec(portX0[5], portY0[0]+18), module, MOIRE::SAVE_PARAM, 0.0, 1.0, 0.0));

  addParam(createParam<Trimpot>(Vec(portX0[0], portY0[1]+16), module, MOIRE::TARGETSCENE_PARAM, 0.0, 15.1, 0));
	MOIREDisplay *displayTarget = new MOIREDisplay();
	displayTarget->box.pos = Vec(50,portY0[2]-21);
	displayTarget->box.size = Vec(20, 20);
	displayTarget->value = &module->targetScene;
	addChild(displayTarget);
	addParam(createParam<Trimpot>(Vec(portX0[0], portY0[6]-5), module, MOIRE::CURRENTSCENE_PARAM, 0.0, 15.1, 0));
	MOIREDisplay *displayCurrent = new MOIREDisplay();
	displayCurrent->box.pos = Vec(50,portY0[5]+19);
	displayCurrent->box.size = Vec(20, 20);
	displayCurrent->value = &module->currentScene;
	addChild(displayCurrent);

	addParam(createParam<MOIRECKD6>(Vec(portX0[0]-5, portY0[3]-3), module, MOIRE::ADONF_PARAM, 0.0, 1.0, 0.0));
	addParam(createParam<MOIRECKD6>(Vec(portX0[0]-5, portY0[4]+5), module, MOIRE::NADA_PARAM, 0.0, 1.0, 0.0));

	slider = createParam<MOIRELongSlider>(Vec(portX0[0]+32, portY0[2]+12), module, MOIRE::MORPH_PARAM, 0.0, 10.0, 0);
	addParam(slider);

	addParam(createParam<CKSS>(Vec(40, 279), module, MOIRE::VOLTAGE_PARAM, 0.0, 1.0, 0.0));

	for (int i = 0; i < 16; i++) {
		controls[i] = createParam<BidooColoredKnob>(Vec(portX0[i%4+5], portY0[int(i/4) + 2]), module, MOIRE::CONTROLS_PARAMS + i, 0.0, 10, 0);
		addParam(controls[i]);
		addOutput(createOutput<PJ301MPort>(Vec(portX0[i%4+5]+2, portY0[int(i/4) + 7]), module, MOIRE::CV_OUTPUTS + i));
	}
};
