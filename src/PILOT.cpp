#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <sstream>
#include <iomanip>

using namespace std;

struct PILOT : Module {
	enum ParamIds {
		BOTTOMSCENE_PARAM,
		TOPSCENE_PARAM,
		BOTTOMSCENEPLUS_PARAM,
		BOTTOMSCENEMINUS_PARAM,
		BOTTOMSCENERND_PARAM,
		TOPSCENEPLUS_PARAM,
		TOPSCENEMINUS_PARAM,
		TOPSCENERND_PARAM,
		MORPH_PARAM,
		GOBOTTOM_PARAM,
		GOTOP_PARAM,
		SAVEBOTTOM_PARAM,
		SAVETOP_PARAM,
		MOVETYPE_PARAM,
		MOVENEXT_PARAM,
		SPEED_PARAM,
		RNDTOP_PARAM,
		FRNDTOP_PARAM,
		RNDBOTTOM_PARAM,
		FRNDBOTTOM_PARAM,
		WEOM_PARAM,
		TYPE_PARAMS,
		CONTROLS_PARAMS = TYPE_PARAMS + 16,
		VOLTAGE_PARAMS = CONTROLS_PARAMS + 16,
		NUM_PARAMS = VOLTAGE_PARAMS + 16
	};
	enum InputIds {
		BOTTOMSCENE_INPUT,
		TOPSCENE_INPUT,
		MORPH_INPUT,
		BOTTOMSCENEPLUS_INPUT,
		BOTTOMSCENEMINUS_INPUT,
		BOTTOMSCENERND_INPUT,
		TOPSCENEPLUS_INPUT,
		TOPSCENEMINUS_INPUT,
		TOPSCENERND_INPUT,
		SPEED_INPUT,
		GOBOTTOM_INPUT,
		GOTOP_INPUT,
		MOVENEXT_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CV_OUTPUTS,
		NUM_OUTPUTS = CV_OUTPUTS + 16
	};
	enum LightIds {
		WEOM_LIGHT,
		TYPE_LIGHTS= WEOM_LIGHT + 3,
		VOLTAGE_LIGHTS = TYPE_LIGHTS + 16*3,
		MORPH_LIGHTS = VOLTAGE_LIGHTS + 16*3,
		NUM_LIGHTS = MORPH_LIGHTS + 16
	};

	float scenes[16][16] = {{0.0f}};
	int controlTypes[16] = {0};
	int voltageTypes[16] = {0};
	bool controlFocused[16] = {false};
	bool morphFocused = false;
	int topScene = 0;
	int bottomScene = 0;
	float morph = 0.0f;
	bool pulses[16] = {false};
	bool fired[16] = {false};
	float speed = 1e-3f;
	int moveType = 5;
	bool moving = false;
	bool forward = false;
	bool rev = false;
	bool waitEOM = true;

	dsp::SchmittTrigger typeTriggers[16];
	dsp::SchmittTrigger voltageTriggers[16];
	dsp::SchmittTrigger bottomScenePlusTrigger;
	dsp::SchmittTrigger bottomSceneMinusTrigger;
	dsp::SchmittTrigger bottomSceneRndTrigger;
	dsp::SchmittTrigger bottomSceneGoTrigger;
	dsp::SchmittTrigger bottomSceneSaveTrigger;
	dsp::SchmittTrigger topScenePlusTrigger;
	dsp::SchmittTrigger topSceneMinusTrigger;
	dsp::SchmittTrigger topSceneRndTrigger;
	dsp::SchmittTrigger topSceneGoTrigger;
	dsp::SchmittTrigger topSceneSaveTrigger;
	dsp::SchmittTrigger moveNextTrigger;
	dsp::SchmittTrigger moveTypeTrigger;
	dsp::SchmittTrigger rndTopTrigger;
	dsp::SchmittTrigger frndTopTrigger;
	dsp::SchmittTrigger rndBottomTrigger;
	dsp::SchmittTrigger frndBottomTrigger;
	dsp::SchmittTrigger weomTrigger;
	dsp::PulseGenerator gatePulses[16];


	PILOT() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(BOTTOMSCENE_PARAM, 0.0f, 15.0f, 0.0f);
		configParam(BOTTOMSCENEPLUS_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(BOTTOMSCENEMINUS_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(BOTTOMSCENERND_PARAM, 0.0f, 10.0f, 0.0f);
    configParam(SAVEBOTTOM_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(TOPSCENE_PARAM, 0.0f, 15.0f, 0.0f);
		configParam(TOPSCENEPLUS_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(TOPSCENEMINUS_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(TOPSCENERND_PARAM, 0.0f, 10.0f, 0.0f);
    configParam(SAVETOP_PARAM, 0.0f, 10.0f, 0.0f);

    configParam(GOBOTTOM_PARAM, 0.0f, 10.0f, 0.0f);
    configParam(GOTOP_PARAM, 0.0f, 10.0f, 0.0f);
    configParam(MORPH_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(MOVETYPE_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(MOVENEXT_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(SPEED_PARAM, 1e-9f, 1.0f, 0.4f);
		configParam(WEOM_PARAM, 0.0f, 10.0f, 0.0f);

		configParam(RNDTOP_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(FRNDTOP_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(RNDBOTTOM_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(FRNDBOTTOM_PARAM, 0.0f, 10.0f, 0.0f);

  	for (int i = 0; i < 16; i++) {
      configParam(CONTROLS_PARAMS + i, 0.0f, 1.0f, 0.0f);
      configParam(TYPE_PARAMS + i, 0.0f, 10.0f,  0.0f);
			configParam(VOLTAGE_PARAMS + i, 0.0f, 10.0f, 0.0f);
  	}
  }

  void process(const ProcessArgs &args) override;

	void onReset() override {
		for (int i=0; i<16; i++) {
			for (int j=0; j<16; j++) {
				scenes[i][j]=0.0f;
			}
			controlTypes[i] = 0.0f;
		}
		moveType = 5;
	}

	void randomizeScene(int scene) {
		for (int i = 0; i < 16; i++) {
				scenes[scene][i]=random::uniform();
		}
	}

	void frandomizeScene(int scene) {
		for (int i = 0; i < 16; i++) {
				scenes[scene][i]=random::uniform();
				controlTypes[i] = clamp(floor(random::uniform()*3.0f), 0.0f,2.0f);
		}
	}

	void randomizeScenes() {
		for (int i = 0; i < 16; i++) {
			randomizeScene(i);
			controlTypes[i] = clamp(floor(random::uniform()*3.0f), 0.0f,2.0f);
		}
	}

	void randomizeTargetScene() {
		for (int i = 0; i < 16; i++) {
				frandomizeScene(topScene);
		}
	}

	void onRandomize() override {
		randomizeScenes();
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		json_object_set_new(rootJ, "moveType", json_integer(moveType));
		json_object_set_new(rootJ, "WEOM", json_boolean(waitEOM));

		json_t *scenesJ = json_array();
		json_t *typesJ = json_array();
		json_t *voltagesJ = json_array();
		for (int i = 0; i < 16; i++) {
			json_t *sceneJ = json_array();
			for (int j = 0; j < 16; j++) {
				json_t *controlJ = json_real(scenes[i][j]);
				json_array_append_new(sceneJ, controlJ);
			}
			json_array_append_new(scenesJ, sceneJ);

			json_t *typeJ = json_integer(controlTypes[i]);
			json_array_append_new(typesJ, typeJ);
			json_t *voltageJ = json_integer(voltageTypes[i]);
			json_array_append_new(voltagesJ, voltageJ);
		}
		json_object_set_new(rootJ, "scenes", scenesJ);
		json_object_set_new(rootJ, "types", typesJ);
		json_object_set_new(rootJ, "voltages", voltagesJ);

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *moveTypeJ = json_object_get(rootJ, "moveType");
		if (moveTypeJ)
			moveType = json_integer_value(moveTypeJ);

		json_t *weomJ = json_object_get(rootJ, "WEOM");
		if (weomJ)
			waitEOM = json_boolean_value(weomJ);

		json_t *scenesJ = json_object_get(rootJ, "scenes");
		json_t *typesJ = json_object_get(rootJ, "types");
		json_t *voltagesJ = json_object_get(rootJ, "voltages");
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
				json_t *voltageJ = json_array_get(voltagesJ, i);
				if (voltageJ) {
					voltageTypes[i] = json_integer_value(voltageJ);
				}
			}
		}
	}
};

void PILOT::process(const ProcessArgs &args) {
	if (weomTrigger.process(params[WEOM_PARAM].getValue())) {
		waitEOM = !waitEOM;
	}
	lights[WEOM_LIGHT].setBrightness(waitEOM?0:1);
	lights[WEOM_LIGHT+1].setBrightness(waitEOM?0:1);
	lights[WEOM_LIGHT+2].setBrightness(waitEOM?1:0);

	speed = powf(clamp(params[SPEED_PARAM].getValue()+inputs[SPEED_INPUT].getVoltage()/10.0f,1e-9f,1.0f),16.0f);

	if (moveTypeTrigger.process(params[MOVETYPE_PARAM].getValue())) {
		moveType = (moveType+1)%6;
	}

	if ((moveNextTrigger.process(params[MOVENEXT_PARAM].getValue()+inputs[MOVENEXT_INPUT].getVoltage())) && ((waitEOM && !moving) || (!waitEOM))) {
		moving = true;
		forward = !forward;
		if (forward) {
			if (moveType==0) {
				params[TOPSCENE_PARAM].setValue(int(params[BOTTOMSCENE_PARAM].getValue()+1.0f)%16);
			}
			else if (moveType==1) {
				if (params[BOTTOMSCENE_PARAM].getValue()==0.0f) {
					params[TOPSCENE_PARAM].setValue(15.0f);
				}
				else {
					params[TOPSCENE_PARAM].setValue(int(params[BOTTOMSCENE_PARAM].getValue()-1.0f));
				}
			}
			else if (moveType==2) {
				if (!rev) {
					params[TOPSCENE_PARAM].setValue(clamp(int(params[BOTTOMSCENE_PARAM].getValue()+1.0f),0,15));
				}
				else if (rev) {
					params[TOPSCENE_PARAM].setValue(clamp(int(params[BOTTOMSCENE_PARAM].getValue()-1.0f),0,15));
				}

				if ((params[TOPSCENE_PARAM].getValue() == 0.0f) || (params[TOPSCENE_PARAM].getValue() == 15.0f)) {
					rev= !rev;
				}
			}
			else if (moveType==3) {
				params[TOPSCENE_PARAM].setValue(clamp(floor(random::uniform()*16.0f), 0.0f, 15.0f));
			}
			else if (moveType==4) {
				float dice = random::uniform();
				if (dice<0.5f) {
					params[TOPSCENE_PARAM].setValue(int(params[BOTTOMSCENE_PARAM].getValue()+1.0f)%16);
				}
				else if ((dice >= 0.5f) && (dice < 0.75f)) {
					params[TOPSCENE_PARAM].setValue(int(params[BOTTOMSCENE_PARAM].getValue()-1.0f)%16);
				}
				else {
					params[TOPSCENE_PARAM].setValue(params[BOTTOMSCENE_PARAM].getValue());
				}
			}
		}
		else {
			if (moveType==0) {
				params[BOTTOMSCENE_PARAM].setValue(int(params[TOPSCENE_PARAM].getValue()+1.0f)%16);
			}
			else if (moveType==1) {
				if (params[TOPSCENE_PARAM].getValue()==0.0f) {
					params[BOTTOMSCENE_PARAM].setValue(15.0f);
				}
				else {
					params[BOTTOMSCENE_PARAM].setValue(int(params[TOPSCENE_PARAM].getValue()-1.0f));
				}
			}
			else if (moveType==2) {
				if (!rev) {
					params[BOTTOMSCENE_PARAM].setValue(clamp(int(params[TOPSCENE_PARAM].getValue()+1.0f),0,15));
				}
				else if (rev) {
					params[BOTTOMSCENE_PARAM].setValue(clamp(int(params[TOPSCENE_PARAM].getValue()-1.0f),0,15));
				}

				if ((params[BOTTOMSCENE_PARAM].getValue() == 0.0f) || (params[BOTTOMSCENE_PARAM].getValue() == 15.0f)) {
					rev= !rev;
				}
			}
			else if (moveType==3) {
				params[BOTTOMSCENE_PARAM].setValue(clamp(floor(random::uniform()*16.0f), 0.0f, 15.0f));
			}
			else if (moveType==4) {
				float dice = random::uniform();
				if (dice<0.5f) {
					params[BOTTOMSCENE_PARAM].setValue(int(params[TOPSCENE_PARAM].getValue()+1.0f)%16);
				}
				else if ((dice >= 0.5f) && (dice < 0.75f)) {
					params[BOTTOMSCENE_PARAM].setValue(int(params[TOPSCENE_PARAM].getValue()-1.0f)%16);
				}
				else {
					params[BOTTOMSCENE_PARAM].setValue(params[TOPSCENE_PARAM].getValue());
				}
			}
		}
	}

	if (inputs[TOPSCENE_INPUT].isConnected()) {
		topScene = clamp(floor(inputs[TOPSCENE_INPUT].getVoltage() * 1.6f), 0.0f, 15.0f);
	}
	else {
		if (topScenePlusTrigger.process(params[TOPSCENEPLUS_PARAM].getValue()+inputs[TOPSCENEPLUS_INPUT].getVoltage()))
		{
			params[TOPSCENE_PARAM].setValue(clamp((int)(params[TOPSCENE_PARAM].getValue() + 1.0f)%16, 0, 15));
		} else if (topSceneMinusTrigger.process(params[TOPSCENEMINUS_PARAM].getValue()+inputs[TOPSCENEMINUS_INPUT].getVoltage()))
		{
			int next = (int)(params[TOPSCENE_PARAM].getValue() - 1.0f);
			if (next == -1) {
				params[TOPSCENE_PARAM].setValue(15);
			}
			else {
				params[TOPSCENE_PARAM].setValue(clamp((int)(params[TOPSCENE_PARAM].getValue() - 1.0f), 0, 15));
			}
		} else if (topSceneRndTrigger.process(params[TOPSCENERND_PARAM].getValue()+inputs[TOPSCENERND_INPUT].getVoltage()))
		{
			params[TOPSCENE_PARAM].setValue(clamp(floor(random::uniform()*16.0f), 0.0f, 15.0f));
		}
		topScene = params[TOPSCENE_PARAM].getValue();
	}

	if (inputs[BOTTOMSCENE_INPUT].isConnected()) {
		bottomScene = clamp(floor(inputs[BOTTOMSCENE_INPUT].getVoltage() * 1.6f), 0.0f, 15.0f);
	}
	else {
		if (bottomScenePlusTrigger.process(params[BOTTOMSCENEPLUS_PARAM].getValue()+inputs[BOTTOMSCENEPLUS_INPUT].getVoltage()))
		{
			params[BOTTOMSCENE_PARAM].setValue(clamp((int)(params[BOTTOMSCENE_PARAM].getValue() + 1.0f)%16, 0, 15));
		} else if (bottomSceneMinusTrigger.process(params[BOTTOMSCENEMINUS_PARAM].getValue()+inputs[BOTTOMSCENEMINUS_INPUT].getVoltage()))
		{
			int next = (int)(params[BOTTOMSCENE_PARAM].getValue() - 1.0f);
			if (next == -1) {
				params[BOTTOMSCENE_PARAM].setValue(15);
			}
			else {
				params[BOTTOMSCENE_PARAM].setValue(clamp((int)(params[BOTTOMSCENE_PARAM].getValue() - 1.0f), 0, 15));
			}
		} else if (bottomSceneRndTrigger.process(params[BOTTOMSCENERND_PARAM].getValue()+inputs[BOTTOMSCENERND_INPUT].getVoltage()))
		{
			params[BOTTOMSCENE_PARAM].setValue(clamp(floor(random::uniform()*16.0f), 0.0f, 15.0f));
		}
		bottomScene = params[BOTTOMSCENE_PARAM].getValue();
	}

	if (rndTopTrigger.process(params[RNDTOP_PARAM].getValue())) {
		randomizeScene(topScene);
	}

	if (frndTopTrigger.process(params[FRNDTOP_PARAM].getValue())) {
		frandomizeScene(topScene);
	}

	if (rndBottomTrigger.process(params[RNDBOTTOM_PARAM].getValue())) {
		randomizeScene(bottomScene);
	}

	if (frndBottomTrigger.process(params[FRNDBOTTOM_PARAM].getValue())) {
		frandomizeScene(bottomScene);
	}

	if (topSceneSaveTrigger.process(params[SAVETOP_PARAM].getValue())) {
		for (int i = 0; i < 16; i++) {
			scenes[topScene][i] = params[CONTROLS_PARAMS+i].getValue();
		}
	}

	if (bottomSceneSaveTrigger.process(params[SAVEBOTTOM_PARAM].getValue())) {
		for (int i = 0; i < 16; i++) {
			scenes[bottomScene][i] = params[CONTROLS_PARAMS+i].getValue();
		}
	}

	if (inputs[MORPH_INPUT].isConnected()) {
		morph = clamp(inputs[MORPH_INPUT].getVoltage()/10.0f + params[MORPH_PARAM].getValue(), 0.0f, 1.0f);

		if (topSceneGoTrigger.process(params[GOTOP_PARAM].getValue()+inputs[GOTOP_INPUT].getVoltage())) {
			params[MORPH_PARAM].setValue(1.0f);
		}

		if (bottomSceneGoTrigger.process(params[GOBOTTOM_PARAM].getValue()+inputs[GOBOTTOM_INPUT].getVoltage())) {
			params[MORPH_PARAM].setValue(0.0f);
		}
	}
	else {
		if (topSceneGoTrigger.process(params[GOTOP_PARAM].getValue()+inputs[GOTOP_INPUT].getVoltage())) {
			params[MORPH_PARAM].setValue(1.0f);
		}

		if (bottomSceneGoTrigger.process(params[GOBOTTOM_PARAM].getValue()+inputs[GOBOTTOM_INPUT].getVoltage())) {
			params[MORPH_PARAM].setValue(0.0f);
		}

		if (moving && !morphFocused) {
			params[MORPH_PARAM].setValue(clamp(params[MORPH_PARAM].getValue()+speed*(forward?1.0f:-1.0f),0.0f,1.0f));
			if ((params[MORPH_PARAM].getValue() == 0.0f) || (params[MORPH_PARAM].getValue() == 1.0f)) {
				moving = false;
			}
		}

		morph = params[MORPH_PARAM].getValue();
	}

	for (int i = 0; i < 16; i++) {
		if (typeTriggers[i].process(params[TYPE_PARAMS + i].getValue())) {
			controlTypes[i] = (controlTypes[i]+1)%3;
		}
		if (voltageTriggers[i].process(params[VOLTAGE_PARAMS + i].getValue())) {
			voltageTypes[i] = voltageTypes[i] == 0 ? 1 : 0;
		}
		lights[TYPE_LIGHTS+ i*3].setBrightness(controlTypes[i] == 1 ? 1 : 0);
		lights[TYPE_LIGHTS+ i*3 + 1].setBrightness(controlTypes[i] != 0 ? 1 : 0);
		lights[TYPE_LIGHTS+ i*3 + 2].setBrightness(controlTypes[i] == 0 ? 1 : 0);
		lights[VOLTAGE_LIGHTS+ i*3].setBrightness(voltageTypes[i] == 1 ? 1 : 0);
		lights[VOLTAGE_LIGHTS+ i*3 + 1].setBrightness(voltageTypes[i] == 1 ? 1 : 0);
		lights[VOLTAGE_LIGHTS+ i*3 + 2].setBrightness(voltageTypes[i] == 0 ? 1 : 0);

		lights[MORPH_LIGHTS+ i].setBrightness(((morph*16.0f >= i) && (morph*16.0f <= i+1))? 1 : 0);

		if (!controlFocused[i]) {
			if (controlTypes[i] != 1) {
				params[CONTROLS_PARAMS+i].setValue(rescale(morph,0.0f,1.0f,scenes[bottomScene][i],scenes[topScene][i]));
			}
			else {
				if (morph == 1.0f) {
					params[CONTROLS_PARAMS+i].setValue(scenes[topScene][i]);
				}
				else if (morph == 0.0f) {
					params[CONTROLS_PARAMS+i].setValue(scenes[bottomScene][i]);
				}
			}
		}

		if ((morph==1.0f) && !fired[i]) {
			gatePulses[i].trigger(params[CONTROLS_PARAMS+i].getValue());
			fired[i] = true;
		}
		else if ((morph==0.0f) && !fired[i]) {
			gatePulses[i].trigger(params[CONTROLS_PARAMS+i].getValue());
			fired[i] = true;
		}
		else if ((morph>0.0f) && (morph<1.0f)) {
			fired[i] = false;
		}

		pulses[i] = gatePulses[i].process(args.sampleTime);

		if (controlTypes[i] != 2) {
			outputs[CV_OUTPUTS + i].setVoltage(params[CONTROLS_PARAMS+i].getValue()*10.0f-5.0f*voltageTypes[i]);
		}
		else {
			outputs[CV_OUTPUTS + i].setVoltage(pulses[i] ? 10.0f-5.0f*voltageTypes[i] : 0.0f);
		}
	}

}

struct PILOTWidget : ModuleWidget {
	ParamWidget *controls[16];
	ParamWidget *morphButton;
	void appendContextMenu(ui::Menu *menu) override;
  PILOTWidget(PILOT *module);
};

struct PILOTColoredKnob : BidooLargeColoredKnob {
	void setValueNoEngine(float value) {
		float newValue = clamp(value, fminf(this->paramQuantity->getMinValue(), this->paramQuantity->getMaxValue()), fmaxf(this->paramQuantity->getMinValue(), this->paramQuantity->getMaxValue()));
		if (this->paramQuantity->getValue() != newValue) {
			this->paramQuantity->setValue(newValue);
		}
	};

	void onDragStart(const event::DragStart &e) override {
		RoundKnob::onDragStart(e);
		PILOT *module = dynamic_cast<PILOT*>(this->paramQuantity->module);
		module->controlFocused[this->paramQuantity->paramId - PILOT::PILOT::CONTROLS_PARAMS] = true;
	}
};

struct PILOTMoveTypeDisplay : TransparentWidget {
	shared_ptr<Font> font;
	int *value;

	PILOTMoveTypeDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	std::string displayPlayMode() {
		switch(*value){
			case 0: return "►";
			case 1: return "◄";
			case 2: return "►◄";
			case 3: return "►*";
			case 4: return "►?";
			case 5: return "Ф";
			default: return "";
		}
	}

	void draw(const DrawArgs &args) override {
			nvgFontSize(args.vg, 18.0f);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			if (value) {
				nvgText(args.vg, 0.0f, 0.0f, displayPlayMode().c_str(), NULL);
			}
	}
};


struct PILOTDisplay : TransparentWidget {
	shared_ptr<Font> font;
	int *value;

	PILOTDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void drawMessage(NVGcontext *vg, Vec pos) {
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
		drawMessage(args.vg, Vec(0, 20));
	}
};

struct PILOTMorphKnob : BidooHugeRedKnob {
	void onButton(const event::Button &e) override {
			PILOT *module = dynamic_cast<PILOT*>(this->paramQuantity->module);

			if (e.action == GLFW_PRESS) {
				module->morphFocused = true;
				for (int i = 0 ; i < 16; i++) {
					module->controlFocused[i] = false;
				}
			}
			else if (e.action == GLFW_RELEASE) {
				module->morphFocused = false;
			}

			e.consume(this);
			BidooHugeRedKnob::onButton(e);
		}
};

PILOTWidget::PILOTWidget(PILOT *module) {
	setModule(module);
	setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/PILOT.svg")));

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	const int morphXAnchor = 100;
	const int morphYAnchor = 128;
	const int morphLigthsXAnchor = morphXAnchor + 73;
	const int morphLigthsYAnchor = morphYAnchor - 90;
	const int morphLigth_spacer = 12;
	const int sceneXAnchor = morphXAnchor - 85;
	const int topSceneYAnchor = morphYAnchor - 80;
	const int bottomSceneYAnchor = morphYAnchor + 95;
	const int xDisplayOffset = 4;
	const int yDisplayOffset = -8;
	const int minusXOffset = -18;
	const int plusXOffset = 32;
	const int inputSceneYOffset = 20;

	morphButton = createParam<PILOTMorphKnob>(Vec(morphXAnchor-13, morphYAnchor-12), module, PILOT::MORPH_PARAM);
	addParam(morphButton);

	addInput(createInput<TinyPJ301MPort>(Vec(morphXAnchor - 20 -15, morphYAnchor + 8), module, PILOT::MORPH_INPUT));

	addParam(createParam<BidooBlueSnapTrimpot>(Vec(sceneXAnchor+5, topSceneYAnchor - 2), module, PILOT::TOPSCENE_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor+7, topSceneYAnchor-inputSceneYOffset - 2), module, PILOT::TOPSCENE_INPUT));
	PILOTDisplay *displayTop = new PILOTDisplay();
	displayTop->box.pos = Vec(morphXAnchor+xDisplayOffset,topSceneYAnchor+yDisplayOffset);
	displayTop->box.size = Vec(20, 20);
	displayTop->value = module ? &module->topScene : NULL;
	addChild(displayTop);

	addParam(createParam<BidooBlueSnapTrimpot>(Vec(sceneXAnchor+5, bottomSceneYAnchor - 2), module, PILOT::BOTTOMSCENE_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor+7, bottomSceneYAnchor+inputSceneYOffset + 2 ), module, PILOT::BOTTOMSCENE_INPUT));
	PILOTDisplay *displayBottom = new PILOTDisplay();
	displayBottom->box.pos = Vec(morphXAnchor+xDisplayOffset,bottomSceneYAnchor+yDisplayOffset);
	displayBottom->box.size = Vec(20, 20);
	displayBottom->value = module ? &module->bottomScene : NULL;
	addChild(displayBottom);

	addParam(createParam<LeftBtn>(Vec(morphXAnchor + minusXOffset, topSceneYAnchor), module, PILOT::TOPSCENEMINUS_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(morphXAnchor + minusXOffset - 20, topSceneYAnchor + 1), module, PILOT::TOPSCENEMINUS_INPUT));
	addParam(createParam<RightBtn>(Vec(morphXAnchor + plusXOffset, topSceneYAnchor), module, PILOT::TOPSCENEPLUS_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(morphXAnchor + plusXOffset + 20, topSceneYAnchor + 1), module, PILOT::TOPSCENEPLUS_INPUT));
	addParam(createParam<RndBtn>(Vec(morphXAnchor + 7, topSceneYAnchor + 20), module, PILOT::TOPSCENERND_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(morphXAnchor + 7, topSceneYAnchor + 40), module, PILOT::TOPSCENERND_INPUT));
	addParam(createParam<BlueCKD6>(Vec(morphXAnchor + 1, topSceneYAnchor - 35), module, PILOT::SAVETOP_PARAM));
	addParam(createParam<BlueCKD6>(Vec(morphXAnchor + 31, topSceneYAnchor - 35), module, PILOT::RNDTOP_PARAM));
	addParam(createParam<BlueCKD6>(Vec(morphXAnchor + 61, topSceneYAnchor - 35), module, PILOT::FRNDTOP_PARAM));

	addParam(createParam<LeftBtn>(Vec(morphXAnchor + minusXOffset, bottomSceneYAnchor ), module, PILOT::BOTTOMSCENEMINUS_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(morphXAnchor + minusXOffset - 20, bottomSceneYAnchor + 1), module, PILOT::BOTTOMSCENEMINUS_INPUT));
	addParam(createParam<RightBtn>(Vec(morphXAnchor + plusXOffset, bottomSceneYAnchor), module, PILOT::BOTTOMSCENEPLUS_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(morphXAnchor + plusXOffset + 20, bottomSceneYAnchor + 1), module, PILOT::BOTTOMSCENEPLUS_INPUT));
	addParam(createParam<RndBtn>(Vec(morphXAnchor + 7, bottomSceneYAnchor - 20), module, PILOT::BOTTOMSCENERND_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(morphXAnchor + 7, bottomSceneYAnchor - 40), module, PILOT::BOTTOMSCENERND_INPUT));
	addParam(createParam<BlueCKD6>(Vec(morphXAnchor + 1, bottomSceneYAnchor + 25), module, PILOT::SAVEBOTTOM_PARAM));
	addParam(createParam<BlueCKD6>(Vec(morphXAnchor + 31, bottomSceneYAnchor + 25), module, PILOT::RNDBOTTOM_PARAM));
	addParam(createParam<BlueCKD6>(Vec(morphXAnchor + 61, bottomSceneYAnchor + 25), module, PILOT::FRNDBOTTOM_PARAM));

	addParam(createParam<BlueCKD6>(Vec(sceneXAnchor, topSceneYAnchor + 32), module, PILOT::GOTOP_PARAM));
	addParam(createParam<BlueCKD6>(Vec(sceneXAnchor,bottomSceneYAnchor - 45), module, PILOT::GOBOTTOM_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor + 7, topSceneYAnchor + 65), module, PILOT::GOTOP_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor + 7, bottomSceneYAnchor - 65), module, PILOT::GOBOTTOM_INPUT));

	const int controlsSpacer = 57;
	const int outputsSpacer = 27;
	const int led_offset = 6;
	const int diffYled_offset = 20;
	const int controlsXAnchor = 202;
	const int controlsYAnchor = 40;
	const int outputsXAnchor = 265;
	const int outputsYAnchor = 262;
	const int ledsXAnchor = controlsXAnchor + 38;
	const int ledsYAnchor = controlsYAnchor;

	for (int i = 0; i < 16; i++) {
		addChild(createLight<MediumLight<RedLight>>(Vec(morphLigthsXAnchor, morphLigthsYAnchor + (16-i) * morphLigth_spacer), module, PILOT::MORPH_LIGHTS + i));

		controls[i] = createParam<PILOTColoredKnob>(Vec(controlsXAnchor + controlsSpacer *(i%4), controlsYAnchor + controlsSpacer * floor(i/4)), module, PILOT::CONTROLS_PARAMS + i);
		addParam(controls[i]);
		addParam(createParam<BidooLEDButton>(Vec(ledsXAnchor + controlsSpacer * (i%4), ledsYAnchor + controlsSpacer * floor(i/4)), module, PILOT::TYPE_PARAMS + i));
		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(ledsXAnchor + led_offset + controlsSpacer * (i%4), ledsYAnchor + led_offset + controlsSpacer * floor(i/4)), module, PILOT::TYPE_LIGHTS + i*3));
		addParam(createParam<BidooLEDButton>(Vec(ledsXAnchor + controlsSpacer * (i%4), diffYled_offset + ledsYAnchor + controlsSpacer * floor(i/4)), module, PILOT::VOLTAGE_PARAMS + i));
		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(ledsXAnchor + led_offset + controlsSpacer * (i%4), diffYled_offset + ledsYAnchor + led_offset + controlsSpacer * floor(i/4)), module, PILOT::VOLTAGE_LIGHTS + i*3));
		addOutput(createOutput<PJ301MPort>(Vec(outputsXAnchor + outputsSpacer * (i%4), outputsYAnchor + outputsSpacer * floor(i/4)), module, PILOT::CV_OUTPUTS + i));
	}

	const int controlsXOffest = 40;
	const int controlsYOffest = 40;

	addParam(createParam<BlueCKD6>(Vec(sceneXAnchor, outputsYAnchor + controlsYOffest), module, PILOT::MOVETYPE_PARAM));
	addParam(createParam<BidooBlueKnob>(Vec(sceneXAnchor + controlsXOffest, outputsYAnchor + controlsYOffest), module, PILOT::SPEED_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor + 2*controlsXOffest, outputsYAnchor + controlsYOffest + 6), module, PILOT::SPEED_INPUT));
	addParam(createParam<BlueCKD6>(Vec(sceneXAnchor + 3*controlsXOffest, outputsYAnchor + controlsYOffest), module, PILOT::MOVENEXT_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor + 4*controlsXOffest, outputsYAnchor + controlsYOffest + 6), module, PILOT::MOVENEXT_INPUT));

	PILOTMoveTypeDisplay *displayMoveType = new PILOTMoveTypeDisplay();
	displayMoveType->box.pos = Vec(sceneXAnchor+14,outputsYAnchor + controlsYOffest-9);
	displayMoveType->box.size = Vec(20, 20);
	displayMoveType->value = module ? &module->moveType : NULL;
	addChild(displayMoveType);

	addParam(createParam<BidooLEDButton>(Vec(70, 340), module, PILOT::WEOM_PARAM));
	addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(70+6, 340+6), module, PILOT::WEOM_LIGHT));
}

struct PILOTItem : MenuItem {
	PILOT *module;
	void onAction(const event::Action &e) override {
			module->randomizeTargetScene();
	}
};

void PILOTWidget::appendContextMenu(ui::Menu *menu) {
	PILOT *module = dynamic_cast<PILOT*>(this->module);
	assert(module);
	menu->addChild(construct<MenuLabel>());
	menu->addChild(construct<PILOTItem>(&MenuItem::text, "Randomize top scene only", &PILOTItem::module, module));
}

Model *modelPILOT = createModel<PILOT, PILOTWidget>("PILOT");
