#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <sstream>
#include <iomanip>
#include "dep/quantizer.hpp"

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
		LENGTH_PARAM,
		AX_PARAM,
		AY_PARAM,
		BX_PARAM,
		BY_PARAM,
		CURVE_PARAM,
		RECORD_PARAM,
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
		AX_INPUT,
		AY_INPUT,
		BX_INPUT,
		BY_INPUT,
		SPEED_INPUT,
		GOBOTTOM_INPUT,
		GOTOP_INPUT,
		MOVENEXT_INPUT,
		RESET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CV_OUTPUTS,
		NUM_OUTPUTS = CV_OUTPUTS + 16
	};
	enum LightIds {
		WEOM_LIGHT,
		RECORD_LIGHT = WEOM_LIGHT + 3,
		CURVE_LIGHT = RECORD_LIGHT + 3,
		TYPE_LIGHTS = CURVE_LIGHT + 3,
		VOLTAGE_LIGHTS = TYPE_LIGHTS + 16*3,
		NUM_LIGHTS = VOLTAGE_LIGHTS + 16*3
	};

	float scenes[16][16] = {{0.0f}};
	int controlTypes[16] = {0};
	int voltageTypes[16] = {0};
	int controlRootNotes[16] = {0};
	int controlScales[16] = {0};
	bool controlFocused[16] = {false};
	int currentFocus = -1;
	bool morphFocused = false;
	int topScene = 0;
	int bottomScene = 0;
	float morph = 0.0f;
	bool pulses[16] = {false};
	float speed = 1e-3f;
	int moveType = 5;
	bool moving = false;
	bool forward = true;
	bool changeDir =false;
	bool rev = false;
	int waitEOM = 0;
	bool curve = false;
	int length = 15;
	int recordingStatus = 0;
	bool reset = false;

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
	dsp::SchmittTrigger curveTrigger;
	dsp::SchmittTrigger recordingTrigger;
	dsp::SchmittTrigger resetTrigger;
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
		configParam(SPEED_PARAM, 1e-9f, 1.0f, 0.5f);
		configParam(WEOM_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(LENGTH_PARAM, 0.0f, 15.0f, 15.0f);
		configParam(CURVE_PARAM, 0.0f, 10.0f, 15.0f);
		configParam(RECORD_PARAM, 0.0f, 10.0f, 0.0f);

		configParam(RNDTOP_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(FRNDTOP_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(RNDBOTTOM_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(FRNDBOTTOM_PARAM, 0.0f, 10.0f, 0.0f);

		configParam(AX_PARAM, -1.0f, 2.0f, 0.5f);
		configParam(AY_PARAM, -5.0f, 5.0f, 0.5f);
		configParam(BX_PARAM, -1.0f, 2.0f, 0.5f);
		configParam(BY_PARAM, -5.0f, 5.0f, 0.5f);

  	for (int i = 0; i < 16; i++) {
      configParam(CONTROLS_PARAMS + i, 0.0f, 1.0f, 0.0f);
      configParam(TYPE_PARAMS + i, 0.0f, 10.0f,  0.0f);
			configParam(VOLTAGE_PARAMS + i, 0.0f, 10.0f, 0.0f);
  	}
  }

	float getXBez(float pos)
	{
		float a = pos * pos;
		float b = pos * pos * pos;
		return clamp(b + (3*a - 3*b)*(params[BX_PARAM].getValue()+inputs[BX_INPUT].getVoltage()) + (3*b - 6*a + 3*pos)*(params[AX_PARAM].getValue()+inputs[AX_INPUT].getVoltage()),0.0f,1.0f);
	}

	float getYBez(float pos)
	{
		float a = pos * pos;
		float b = pos * pos * pos;
		return clamp(b + (3*a - 3*b)*(params[BY_PARAM].getValue()+inputs[BY_INPUT].getVoltage()) + (3*b - 6*a + 3*pos)*(params[AY_PARAM].getValue()+inputs[AY_INPUT].getVoltage()),0.0f,1.0f);
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
		json_object_set_new(rootJ, "WEOM", json_integer(waitEOM));
		json_object_set_new(rootJ, "CURVE", json_boolean(curve));

		json_t *scenesJ = json_array();
		json_t *typesJ = json_array();
		json_t *voltagesJ = json_array();
		json_t *rootNotesJ = json_array();
		json_t *scalesJ = json_array();

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
			json_t *rootNoteJ = json_integer(controlRootNotes[i]);
			json_array_append_new(rootNotesJ, rootNoteJ);
			json_t *scaleJ = json_integer(controlScales[i]);
			json_array_append_new(scalesJ, scaleJ);
		}
		json_object_set_new(rootJ, "scenes", scenesJ);
		json_object_set_new(rootJ, "types", typesJ);
		json_object_set_new(rootJ, "voltages", voltagesJ);
		json_object_set_new(rootJ, "roots", rootNotesJ);
		json_object_set_new(rootJ, "scales", scalesJ);

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *moveTypeJ = json_object_get(rootJ, "moveType");
		if (moveTypeJ)
			moveType = json_integer_value(moveTypeJ);

		json_t *weomJ = json_object_get(rootJ, "WEOM");
		if (weomJ)
			waitEOM = json_integer_value(weomJ);

		json_t *curveJ = json_object_get(rootJ, "CURVE");
		if (curveJ)
			curve = json_boolean_value(curveJ);

		json_t *scenesJ = json_object_get(rootJ, "scenes");
		json_t *typesJ = json_object_get(rootJ, "types");
		json_t *voltagesJ = json_object_get(rootJ, "voltages");
		json_t *rootsJ = json_object_get(rootJ, "roots");
		json_t *scalesJ = json_object_get(rootJ, "scales");

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
				json_t *rootJ = json_array_get(rootsJ, i);
				if (rootJ) {
					controlRootNotes[i] = json_integer_value(rootJ);
				}
				json_t *scaleJ = json_array_get(scalesJ, i);
				if (scaleJ) {
					controlScales[i] = json_integer_value(scaleJ);
				}
			}
		}
	}

};

void PILOT::process(const ProcessArgs &args) {
	changeDir =false;

	if (moveTypeTrigger.process(params[MOVETYPE_PARAM].getValue())) {
		moveType = (moveType+1)%6;
	}

	if ((resetTrigger.process(inputs[RESET_INPUT].getVoltage())) && (moveType==0)) {
		reset = true;
	}

	if (weomTrigger.process(params[WEOM_PARAM].getValue())) {
		waitEOM = (waitEOM+1)%3;
	}
	lights[WEOM_LIGHT].setBrightness(waitEOM>0?1:0);
	lights[WEOM_LIGHT+1].setBrightness(waitEOM==1?1:0);
	lights[WEOM_LIGHT+2].setBrightness(waitEOM==0?1:0);

	if (curveTrigger.process(params[CURVE_PARAM].getValue())) {
		curve = !curve;
	}
	lights[CURVE_LIGHT].setBrightness(curve?0:1);
	lights[CURVE_LIGHT+1].setBrightness(curve?0:1);
	lights[CURVE_LIGHT+2].setBrightness(curve?1:0);

	if (recordingTrigger.process(params[RECORD_PARAM].getValue())) {
		if ((recordingStatus==0) && (moveType==0) && (waitEOM==0)) {
			recordingStatus=1;
		}
	}

	speed = powf(clamp(params[SPEED_PARAM].getValue()+inputs[SPEED_INPUT].getVoltage()/10.0f,1e-9f,1.0f),16.0f);

	length = params[LENGTH_PARAM].getValue();

	if ((reset) && (moveType==0)) {
			params[BOTTOMSCENE_PARAM].setValue(0);
			params[TOPSCENE_PARAM].setValue(int(params[BOTTOMSCENE_PARAM].getValue()+1.0f)%(length+1));
			params[MORPH_PARAM].setValue(0.0f);
			forward = false;
			reset=false;
			moving = true;
	}
	else if ((moveNextTrigger.process(params[MOVENEXT_PARAM].getValue()+inputs[MOVENEXT_INPUT].getVoltage())) && ((waitEOM==0 && !moving) || (waitEOM>0))) {
		moving = true;
		forward = !forward;
		changeDir = true;
		if (forward) {
			if (moveType==0) {
				params[TOPSCENE_PARAM].setValue(int(params[BOTTOMSCENE_PARAM].getValue()+1.0f)%(length+1));
				if ((params[TOPSCENE_PARAM].getValue()==0.0f) && (recordingStatus>0)) {
					recordingStatus=(recordingStatus+1)%4;
					if (recordingStatus==0) {
						controlFocused[currentFocus]=false;
					}
				}
			}
			else if (moveType==1) {
				if (params[BOTTOMSCENE_PARAM].getValue()==0.0f) {
					params[TOPSCENE_PARAM].setValue(length);
				}
				else {
					params[TOPSCENE_PARAM].setValue(int(params[BOTTOMSCENE_PARAM].getValue()-1.0f));
				}
			}
			else if (moveType==2) {
				if (!rev) {
					params[TOPSCENE_PARAM].setValue(clamp(int(params[BOTTOMSCENE_PARAM].getValue()+1.0f),0,length));
				}
				else if (rev) {
					params[TOPSCENE_PARAM].setValue(clamp(int(params[BOTTOMSCENE_PARAM].getValue()-1.0f),0,length));
				}

				if ((params[TOPSCENE_PARAM].getValue() == 0.0f) || (params[TOPSCENE_PARAM].getValue() == (float)length)) {
					rev= !rev;
				}
			}
			else if (moveType==3) {
				params[TOPSCENE_PARAM].setValue(clamp(floor(random::uniform()*(length+1)), 0.0f, (float)length));
			}
			else if (moveType==4) {
				float dice = random::uniform();
				if (dice<0.5f) {
					params[TOPSCENE_PARAM].setValue(int(params[BOTTOMSCENE_PARAM].getValue()+1.0f)%(length+1));
				}
				else if ((dice >= 0.5f) && (dice < 0.75f)) {
					if (params[TOPSCENE_PARAM].getValue()>0.0f) {
						params[TOPSCENE_PARAM].setValue(int(params[BOTTOMSCENE_PARAM].getValue()-1.0f));
					}
					else {
						params[TOPSCENE_PARAM].setValue(length);
					}
				}
				else {
					params[TOPSCENE_PARAM].setValue(params[BOTTOMSCENE_PARAM].getValue());
				}
			}
		}
		else {
			if (moveType==0) {
				params[BOTTOMSCENE_PARAM].setValue(int(params[TOPSCENE_PARAM].getValue()+1.0f)%(length+1));
				if ((params[BOTTOMSCENE_PARAM].getValue()==0.0f) && (recordingStatus>0)) {
					recordingStatus=(recordingStatus+1)%4;
					if (recordingStatus==0) {
						controlFocused[currentFocus]=false;
					}
				}
			}
			else if (moveType==1) {
				if (params[TOPSCENE_PARAM].getValue()==0.0f) {
					params[BOTTOMSCENE_PARAM].setValue(length);
				}
				else {
					params[BOTTOMSCENE_PARAM].setValue(int(params[TOPSCENE_PARAM].getValue()-1.0f));
				}
			}
			else if (moveType==2) {
				if (!rev) {
					params[BOTTOMSCENE_PARAM].setValue(clamp(int(params[TOPSCENE_PARAM].getValue()+1.0f),0,length));
				}
				else if (rev) {
					params[BOTTOMSCENE_PARAM].setValue(clamp(int(params[TOPSCENE_PARAM].getValue()-1.0f),0,length));
				}

				if ((params[BOTTOMSCENE_PARAM].getValue() == 0.0f) || (params[BOTTOMSCENE_PARAM].getValue() == (float)length)) {
					rev= !rev;
				}
			}
			else if (moveType==3) {
				params[BOTTOMSCENE_PARAM].setValue(clamp(floor(random::uniform()*(length+1)), 0.0f, (float)length));
			}
			else if (moveType==4) {
				float dice = random::uniform();
				if (dice<0.5f) {
					params[BOTTOMSCENE_PARAM].setValue(int(params[TOPSCENE_PARAM].getValue()+1.0f)%(length+1));
				}
				else if ((dice >= 0.5f) && (dice < 0.75f)) {
					if (params[TOPSCENE_PARAM].getValue()>0.0f) {
						params[TOPSCENE_PARAM].setValue(int(params[BOTTOMSCENE_PARAM].getValue()-1.0f));
					}
					else {
						params[TOPSCENE_PARAM].setValue(length);
					}
				}
				else {
					params[BOTTOMSCENE_PARAM].setValue(params[TOPSCENE_PARAM].getValue());
				}
			}
		}
	}


	if (inputs[TOPSCENE_INPUT].isConnected()) {
		topScene = clamp(floor(inputs[TOPSCENE_INPUT].getVoltage() * 1.6f), 0.0f, (float)length);
	}
	else {
		if (topScenePlusTrigger.process(params[TOPSCENEPLUS_PARAM].getValue()+inputs[TOPSCENEPLUS_INPUT].getVoltage()))
		{
			params[TOPSCENE_PARAM].setValue(clamp((int)(params[TOPSCENE_PARAM].getValue() + 1.0f)%16, 0, length));
		}
		else if (topSceneMinusTrigger.process(params[TOPSCENEMINUS_PARAM].getValue()+inputs[TOPSCENEMINUS_INPUT].getVoltage()))
		{
			int next = (int)(params[TOPSCENE_PARAM].getValue() - 1.0f);
			if (next == -1) {
				params[TOPSCENE_PARAM].setValue(length);
			}
			else {
				params[TOPSCENE_PARAM].setValue(clamp((int)(params[TOPSCENE_PARAM].getValue() - 1.0f), 0, length));
			}
		}
		else if (topSceneRndTrigger.process(params[TOPSCENERND_PARAM].getValue()+inputs[TOPSCENERND_INPUT].getVoltage()))
		{
			params[TOPSCENE_PARAM].setValue(clamp(floor(random::uniform()*(length+1)), 0.0f, (float)length));
		}
		topScene = params[TOPSCENE_PARAM].getValue();
	}

	if (inputs[BOTTOMSCENE_INPUT].isConnected()) {
		bottomScene = clamp(floor(inputs[BOTTOMSCENE_INPUT].getVoltage() * 1.6f), 0.0f, (float)length);
	}
	else {
		if (bottomScenePlusTrigger.process(params[BOTTOMSCENEPLUS_PARAM].getValue()+inputs[BOTTOMSCENEPLUS_INPUT].getVoltage()))
		{
			params[BOTTOMSCENE_PARAM].setValue(clamp((int)(params[BOTTOMSCENE_PARAM].getValue() + 1.0f)%(length+1), 0, length));
		}
		else if (bottomSceneMinusTrigger.process(params[BOTTOMSCENEMINUS_PARAM].getValue()+inputs[BOTTOMSCENEMINUS_INPUT].getVoltage()))
		{
			int next = (int)(params[BOTTOMSCENE_PARAM].getValue() - 1.0f);
			if (next == -1) {
				params[BOTTOMSCENE_PARAM].setValue(length);
			}
			else {
				params[BOTTOMSCENE_PARAM].setValue(clamp((int)(params[BOTTOMSCENE_PARAM].getValue() - 1.0f), 0, length));
			}
		}
		else if (bottomSceneRndTrigger.process(params[BOTTOMSCENERND_PARAM].getValue()+inputs[BOTTOMSCENERND_INPUT].getVoltage()))
		{
			params[BOTTOMSCENE_PARAM].setValue(clamp(floor(random::uniform()*(length+1)), 0.0f, (float)length));
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
			controlFocused[i]=false;
		}
		currentFocus = -1;
	}

	if (bottomSceneSaveTrigger.process(params[SAVEBOTTOM_PARAM].getValue())) {
		for (int i = 0; i < 16; i++) {
			scenes[bottomScene][i] = params[CONTROLS_PARAMS+i].getValue();
			controlFocused[i]=false;
		}
		currentFocus = -1;
	}

	if (recordingStatus == 1) {
		lights[RECORD_LIGHT].setBrightness(0);
		lights[RECORD_LIGHT+1].setBrightness(0);
		lights[RECORD_LIGHT+2].setBrightness(lights[RECORD_LIGHT+2].getBrightness()<0.1f ? 1 : lights[RECORD_LIGHT+2].getBrightness()-lights[RECORD_LIGHT+2].getBrightness()*2.0f*args.sampleTime);
	}
	else if (recordingStatus == 2) {
		lights[RECORD_LIGHT].setBrightness(lights[RECORD_LIGHT].getBrightness()<0.1f ? 1 : lights[RECORD_LIGHT].getBrightness()-lights[RECORD_LIGHT].getBrightness()*4.0f*args.sampleTime);
		lights[RECORD_LIGHT+1].setBrightness(lights[RECORD_LIGHT+1].getBrightness()<0.1f ? 1 : lights[RECORD_LIGHT+1].getBrightness()-lights[RECORD_LIGHT+1].getBrightness()*4.0f*args.sampleTime);
		lights[RECORD_LIGHT+2].setBrightness(0);
	}
	else if (recordingStatus == 3) {
		lights[RECORD_LIGHT].setBrightness(1);
		lights[RECORD_LIGHT+1].setBrightness(0);
		lights[RECORD_LIGHT+2].setBrightness(0);
	}
	else {
		lights[RECORD_LIGHT].setBrightness(0);
		lights[RECORD_LIGHT+1].setBrightness(0);
		lights[RECORD_LIGHT+2].setBrightness(0);
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
		if (recordingStatus==3 && i==currentFocus) {
			if (params[MORPH_PARAM].getValue() == 0.0f) {
				scenes[bottomScene][i]=params[CONTROLS_PARAMS+i].getValue();
			}
			else if (params[MORPH_PARAM].getValue() == 1.0f) {
				scenes[topScene][i]=params[CONTROLS_PARAMS+i].getValue();
			}
		}

		if (typeTriggers[i].process(params[TYPE_PARAMS + i].getValue())) {
			controlTypes[i] = (controlTypes[i]+1)%5;
		}
		if (voltageTriggers[i].process(params[VOLTAGE_PARAMS + i].getValue())) {
			voltageTypes[i] = voltageTypes[i] == 0 ? 1 : 0;
		}

		lights[TYPE_LIGHTS+ i*3].setBrightness((controlTypes[i] == 1) || (controlTypes[i] == 3) || (controlTypes[i] == 4) ? 1 : 0);
		lights[TYPE_LIGHTS+ i*3 + 1].setBrightness((controlTypes[i] == 1) || (controlTypes[i] == 2) ? 1 : ((controlTypes[i] == 4) ? 0.5f : 0));
		lights[TYPE_LIGHTS+ i*3 + 2].setBrightness(controlTypes[i] == 0 ? 1 : 0);
		lights[VOLTAGE_LIGHTS+ i*3].setBrightness(voltageTypes[i] == 1 ? 1 : 0);
		lights[VOLTAGE_LIGHTS+ i*3 + 1].setBrightness(voltageTypes[i] == 1 ? 1 : 0);
		lights[VOLTAGE_LIGHTS+ i*3 + 2].setBrightness(voltageTypes[i] == 0 ? 1 : 0);

		if (!controlFocused[i]) {
			if ((controlTypes[i] != 1) && (controlTypes[i] != 4)) {
				if (!curve) {
					if (scenes[bottomScene][i] != scenes[topScene][i]) {
						params[CONTROLS_PARAMS+i].setValue(clamp(rescale(morph,0.0f,1.0f,scenes[bottomScene][i],scenes[topScene][i]),0.0f,1.0f));
					}
					else {
						params[CONTROLS_PARAMS+i].setValue(scenes[bottomScene][i]);
					}
				}
				else {
					if (scenes[bottomScene][i] != scenes[topScene][i]) {
						params[CONTROLS_PARAMS+i].setValue(rescale(getYBez(morph),0.0f,1.0f,scenes[bottomScene][i],scenes[topScene][i]));
					}
					else {
						params[CONTROLS_PARAMS+i].setValue(scenes[bottomScene][i]);
					}
				}
			}
			else {
				if (morph == 1.0f || (changeDir && (waitEOM==1) && forward)) {
					params[CONTROLS_PARAMS+i].setValue(scenes[topScene][i]);
				}
				else if (morph == 0.0f || (changeDir && (waitEOM==1) && !forward)) {
					params[CONTROLS_PARAMS+i].setValue(scenes[bottomScene][i]);
				}
			}
		}

		if ((changeDir && waitEOM<2) || (waitEOM==2 && ((morph==0.0f) || (morph==1.0f)) && !pulses[i])) {
			gatePulses[i].reset();
			gatePulses[i].trigger(powf(params[CONTROLS_PARAMS+i].getValue(),8.0f));
		}

		pulses[i] = gatePulses[i].process(args.sampleTime);

		if (controlTypes[i] >= 3) {
			if (controlFocused[i] || (controlTypes[i] == 4)) {
				outputs[CV_OUTPUTS + i].setVoltage(quantizer::closestVoltageInScale(params[CONTROLS_PARAMS+i].getValue()*10.0f-4.0f, controlRootNotes[i], controlScales[i]));
			}
			else {
				if ((scenes[bottomScene][i]==scenes[topScene][i]) || (bottomScene==topScene)) {
					outputs[CV_OUTPUTS + i].setVoltage(quantizer::closestVoltageInScale(scenes[bottomScene][i]*10.0f-4.0f, controlRootNotes[i], controlScales[i]));
				}
				else {
					outputs[CV_OUTPUTS + i].setVoltage(rescale(params[CONTROLS_PARAMS+i].getValue(),scenes[bottomScene][i],scenes[topScene][i],
					quantizer::closestVoltageInScale(scenes[bottomScene][i]*10.0f-4.0f, controlRootNotes[i], controlScales[i]),quantizer::closestVoltageInScale(scenes[topScene][i]*10.0f-4.0f, controlRootNotes[i], controlScales[i])));
				}
			}
		}
		else if (controlTypes[i] == 2) {
			outputs[CV_OUTPUTS + i].setVoltage(pulses[i] ? 10.0f-5.0f*voltageTypes[i] : 0.0f);
		}
		else {
			outputs[CV_OUTPUTS + i].setVoltage(params[CONTROLS_PARAMS+i].getValue()*10.0f-5.0f*voltageTypes[i]);
		}
	}

}

struct PILOTWidget : ModuleWidget {
	ParamWidget *controls[16];
	ParamWidget *morphButton;
	void appendContextMenu(ui::Menu *menu) override;
	void draw(const DrawArgs& args) override;
	void step() override;
  PILOTWidget(PILOT *module);
};

struct PILOTMoveTypeDisplay : TransparentWidget {
	int *value;

	std::string fontPath = "res/DejaVuSansMono.ttf";

	PILOTMoveTypeDisplay() {

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
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, fontPath));
		nvgGlobalTint(args.vg, color::WHITE);
		nvgFontSize(args.vg, 18.0f);
		nvgFillColor(args.vg, YELLOW_BIDOO);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
		if (value) {
			nvgText(args.vg, 0.0f, 14, displayPlayMode().c_str(), NULL);
		}
	}
};


struct PILOTDisplay : TransparentWidget {
	int *value;

	std::string fontPath = "res/DejaVuSansMono.ttf";

	PILOTDisplay() {

	}

	void draw(const DrawArgs &args) override {
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, fontPath));
		nvgGlobalTint(args.vg, color::WHITE);
		if (value) {
      nvgFontSize(args.vg, 18);
  		nvgFontFaceId(args.vg, font->handle);
  		nvgTextLetterSpacing(args.vg, -2);
  		nvgFillColor(args.vg, YELLOW_BIDOO);
  		std::stringstream ss;
  		ss << std::setw(2) << std::setfill('0') << *value + 1;
  		std::string s = ss.str();
  		nvgText(args.vg, 0, 14, s.c_str(), NULL);
    }
	}
};

struct PILOTNoteDisplay : TransparentWidget {
	PILOT *module;

	std::string fontPath = "res/DejaVuSansMono.ttf";

	PILOTNoteDisplay() {

	}

	void draw(const DrawArgs &args) override {
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, fontPath));
		nvgGlobalTint(args.vg, color::WHITE);
		if ((module) && (module->currentFocus>=0) && (module->controlTypes[module->currentFocus]>=3)) {
      nvgFontSize(args.vg, 18);
  		nvgFontFaceId(args.vg, font->handle);
  		nvgTextLetterSpacing(args.vg, -2);
  		nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgText(args.vg, 0, 12, quantizer::noteName(module->outputs[PILOT::CV_OUTPUTS+module->currentFocus].getVoltage()).c_str(), NULL);
    }
	}
};

struct PILOTCurveDisplay : TransparentWidget {
	PILOT *module;

	PILOTCurveDisplay() {
	}

	void draw(const DrawArgs &args) override {
		nvgGlobalTint(args.vg, color::WHITE);
		if (module) {
			if (module->curve) {
				nvgStrokeColor(args.vg, SCHEME_BLUE);
			}
			else {
				nvgStrokeColor(args.vg, YELLOW_BIDOO);
			}
			nvgStrokeWidth(args.vg, 2);
			nvgSave(args.vg);
			nvgBeginPath(args.vg);
			for( float i = 0 ; i <= 1 ; i =i+0.01 ) {
				if (i == 0) {
					if (module->curve) {
						nvgMoveTo(args.vg, module->getXBez(i)*box.size.x,box.size.x - module->getYBez(i)*box.size.x);
					}
					else {
						nvgMoveTo(args.vg, i*box.size.x,box.size.x - i*box.size.x);
					}
				}
				else {
					if (module->curve) {
						nvgLineTo(args.vg, module->getXBez(i)*box.size.y,box.size.y - module->getYBez(i)*box.size.y);
					}
					else {
						nvgLineTo(args.vg, i*box.size.x,box.size.x - i*box.size.x);
					}
				}
			}
			nvgStroke(args.vg);

			nvgBeginPath(args.vg);
			nvgStrokeColor(args.vg, RED_BIDOO);
			nvgFillColor(args.vg, RED_BIDOO);
			nvgStrokeWidth(args.vg, 2);
			if (module->curve) {
				nvgCircle(args.vg, module->getXBez(module->morph)*box.size.x, box.size.y - module->getYBez(module->morph)*box.size.y,3);
			}
			else {
				nvgCircle(args.vg, module->morph*box.size.x, box.size.y - module->morph*box.size.y,3);
			}
			nvgStroke(args.vg);
			nvgFill(args.vg);

			nvgRestore(args.vg);
		}
	}
};

struct PILOTMorphKnob : BidooHugeRedKnob {
	void onDragStart(const DragStartEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(this->getParamQuantity()->module);
		module->morphFocused = true;
		e.consume(this);
		BidooHugeRedKnob::onDragStart(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(this->getParamQuantity()->module);
		module->morphFocused = false;
		e.consume(this);
		BidooHugeRedKnob::onDragEnd(e);
	}

	void onButton(const event::Button &e) override {
			PILOT *module = dynamic_cast<PILOT*>(this->getParamQuantity()->module);
			for (int i = 0 ; i < 16; i++) {
				module->controlFocused[i] = false;
			}
			e.consume(this);
			BidooHugeRedKnob::onButton(e);
		}
};

struct BidooLargeColoredKnob : RoundKnob {
	bool *blink=NULL;
	int frame=0;
	unsigned int tFade=255;

	BidooLargeColoredKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/ColoredLargeKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/ColoredLargeKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
	}

	void draw(const DrawArgs& args) override {
		if (getParamQuantity()) {
			for (NSVGshape *shape = bg->svg->handle->shapes; shape != NULL; shape = shape->next) {
				std::string str(shape->id);
				if ((str == "bidooKnob") || (str == "bidooInterior")) {
					shape->fill.color = (((unsigned int)42+(unsigned int)(getParamQuantity()->getValue()*210)) | (((unsigned int)87-(unsigned int)(getParamQuantity()->getValue()*80)) << 8) | (((unsigned int)117-(unsigned int)(getParamQuantity()->getValue()*10)) << 16));
					if (!*blink) {
						tFade = 255;
					}
					else {
						if (++frame <= 30) {
							tFade -= frame*3;
						}
						else if (++frame<60) {
							tFade = 255;
						}
						else {
							tFade = 255;
							frame = 0;
						}
					}
					shape->fill.color |= (unsigned int)(tFade) << 24;
				}
			}
		}
		RoundKnob::draw(args);
	}
};


struct CtrlRampUpMenuItem : ui::MenuItem {
	ParamQuantity* param = NULL;
	void onAction(const ActionEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(param->module);
		for (int i = 0 ; i < 16; i++) {
			if (i==0) {
				module->scenes[i][param->paramId - PILOT::PILOT::CONTROLS_PARAMS] = 0.0f;
			}
			else if (i<=module->length) {
				module->scenes[i][param->paramId - PILOT::PILOT::CONTROLS_PARAMS] = (float)i/(module->length);
			}
			else {
				module->scenes[i][param->paramId - PILOT::PILOT::CONTROLS_PARAMS] = 0.0f;
			}
		}
	}
};

struct CtrlRampDownMenuItem : ui::MenuItem {
	ParamQuantity* param = NULL;
	void onAction(const ActionEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(param->module);
		for (int i = 0 ; i < 16; i++) {
			if (i==0) {
				module->scenes[i][param->paramId - PILOT::PILOT::CONTROLS_PARAMS] = 1.0f;
			}
			else if (i<module->length) {
				module->scenes[i][param->paramId - PILOT::PILOT::CONTROLS_PARAMS] = (float)(module->length-i)/(module->length);
			}
			else {
				module->scenes[i][param->paramId - PILOT::PILOT::CONTROLS_PARAMS] = 0.0f;
			}
		}
	}
};

struct CtrlSinMenuItem : ui::MenuItem {
	ParamQuantity* param = NULL;
	void onAction(const ActionEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(param->module);
		for (int i = 0 ; i < 16; i++) {
			if (i<=module->length) {
				module->scenes[i][param->paramId - PILOT::PILOT::CONTROLS_PARAMS] = sin((float)(module->length-i)*M_PI/(module->length));
			}
			else {
				module->scenes[i][param->paramId - PILOT::PILOT::CONTROLS_PARAMS] = 0.0f;
			}
		}
	}
};

struct CtrlInitMenuItem : ui::MenuItem {
	ParamQuantity* param = NULL;
	void onAction(const ActionEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(param->module);
		for (int i = 0 ; i < 16; i++) {
			if (i<=module->length) {
				module->scenes[i][param->paramId - PILOT::PILOT::CONTROLS_PARAMS] = 0.0f;
			}
		}
	}
};

struct CtrlRootNoteItem : ui::MenuItem {
	int rootNote=0;
	ParamQuantity* param = NULL;

	void onAction(const ActionEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(param->module);
		module->controlRootNotes[param->paramId - PILOT::PILOT::CONTROLS_PARAMS] = rootNote;
	}
};

struct CtrlScaleItem : ui::MenuItem {
	int scale=0;
	ParamQuantity* param = NULL;

	void onAction(const ActionEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(param->module);
		module->controlScales[param->paramId - PILOT::PILOT::CONTROLS_PARAMS] = scale;
	}
};

struct CtrlRootNoteMenuItem : ui::MenuItem {
	ParamQuantity* param = NULL;
	ui::Menu* createChildMenu() override {
		ui::Menu* menu = new ui::Menu;

		for (int i=0; i < quantizer::numNotes; i++) {
			CtrlRootNoteItem* rItem = new CtrlRootNoteItem;
			rItem->text = quantizer::rootNotes[i].label;
			rItem->rootNote = quantizer::rootNotes[i].note;
			rItem->param = param;
			PILOT *module = dynamic_cast<PILOT*>(param->module);
			if (module->controlRootNotes[param->paramId - PILOT::PILOT::CONTROLS_PARAMS] == quantizer::rootNotes[i].note) rItem->rightText = CHECKMARK_STRING;
			menu->addChild(rItem);
		}

		return menu;
	}
};

struct CtrlScaleMenuItem : ui::MenuItem {
	ParamQuantity* param = NULL;
	ui::Menu* createChildMenu() override {
		ui::Menu* menu = new ui::Menu;

		for (int i=0; i < quantizer::numScales; i++) {
			CtrlScaleItem* rItem = new CtrlScaleItem;
			rItem->text = quantizer::scales[i].label;
			rItem->scale = i;
			rItem->param = param;
			PILOT *module = dynamic_cast<PILOT*>(param->module);
			if (module->controlScales[param->paramId - PILOT::PILOT::CONTROLS_PARAMS] == i) rItem->rightText = CHECKMARK_STRING;
			menu->addChild(rItem);
		}

		return menu;
	}
};

struct PILOTColoredKnob : BidooLargeColoredKnob {

	void setValueNoEngine(float value) {
		float newValue = clamp(value, fminf(this->getParamQuantity()->getMinValue(), this->getParamQuantity()->getMaxValue()), fmaxf(this->getParamQuantity()->getMinValue(), this->getParamQuantity()->getMaxValue()));
		if (this->getParamQuantity()->getValue() != newValue) {
			this->getParamQuantity()->setValue(newValue);
		}
	};

	void appendContextMenu(ui::Menu *menu) override {
		CtrlRampUpMenuItem* itemUp = new CtrlRampUpMenuItem;
		itemUp->text = "Ramp Up";
		itemUp->param = this->getParamQuantity();
		menu->addChild(itemUp);

		CtrlRampDownMenuItem* itemDown = new CtrlRampDownMenuItem;
		itemDown->text = "Ramp Down";
		itemDown->param = this->getParamQuantity();
		menu->addChild(itemDown);

		CtrlSinMenuItem* itemSin = new CtrlSinMenuItem;
		itemSin->text = "Sinus";
		itemSin->param = this->getParamQuantity();
		menu->addChild(itemSin);

		CtrlInitMenuItem* itemInit = new CtrlInitMenuItem;
		itemInit->text = "Init";
		itemInit->param = this->getParamQuantity();
		menu->addChild(itemInit);

		CtrlRootNoteMenuItem* itemRootNote = new CtrlRootNoteMenuItem;
		itemRootNote->text = "Root note";
		itemRootNote->rightText = RIGHT_ARROW;
		itemRootNote->param = this->getParamQuantity();
		menu->addChild(itemRootNote);

		CtrlScaleMenuItem* itemScale = new CtrlScaleMenuItem;
		itemScale->text = "Scale";
		itemScale->rightText = RIGHT_ARROW;
		itemScale->param = this->getParamQuantity();
		menu->addChild(itemScale);
	}

	void onButton(const ButtonEvent& e) override {
		RoundKnob::onButton(e);

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			PILOT *module = dynamic_cast<PILOT*>(this->getParamQuantity()->module);
			module->controlFocused[this->getParamQuantity()->paramId - PILOT::PILOT::CONTROLS_PARAMS] = true;
			module->currentFocus = this->getParamQuantity()->paramId - PILOT::PILOT::CONTROLS_PARAMS;
		}

	}
};

PILOTWidget::PILOTWidget(PILOT *module) {
	setModule(module);
	setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/PILOT.svg")));

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	const int morphXAnchor = 144;
	const int morphYAnchor = 111;
	const int jumpersXOffset = -30;
	const int topJumpYOffset = -15;
	const int bottomJumpYOffset = 44;
	const int morphInputXOffset = -24;
	const int morphInputYOffset = 21;
	const int jumpInputXOffset = -10;
	const int jumpTopInputYOffset = 25;
	const int jumpBottomInputYOffset = -12;

	morphButton = createParam<PILOTMorphKnob>(Vec(morphXAnchor, morphYAnchor), module, PILOT::MORPH_PARAM);
	addParam(morphButton);
	addInput(createInput<TinyPJ301MPort>(Vec(morphXAnchor + morphInputXOffset, morphYAnchor + morphInputYOffset), module, PILOT::MORPH_INPUT));
	addParam(createParam<BlueCKD6>(Vec(morphXAnchor+jumpersXOffset, morphYAnchor+topJumpYOffset), module, PILOT::GOTOP_PARAM));
	addParam(createParam<BlueCKD6>(Vec(morphXAnchor+jumpersXOffset, morphYAnchor+bottomJumpYOffset), module, PILOT::GOBOTTOM_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(morphXAnchor+jumpersXOffset+jumpInputXOffset, morphYAnchor+topJumpYOffset+jumpTopInputYOffset), module, PILOT::GOTOP_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(morphXAnchor+jumpersXOffset+jumpInputXOffset, morphYAnchor+bottomJumpYOffset+jumpBottomInputYOffset), module, PILOT::GOBOTTOM_INPUT));


	const int sceneXAnchor = 119;
	const int topSceneYAnchor = 29;
	const int bottomSceneYAnchor = 190;
	const int ctrlSceneYOffset = 20;
	const int inputSceneYOffset = 43;
	const int ctrlSceneXOffset = 20;
	const int inputSceneXOffset = 20;
	const int inputRoundBtnDrift = 2;
	const int inputSquBtnDrift = 4;
	const int sqBtnYDrift = 2;
	const int sqBtnDrift = 3;
	const int dispOffset = 2;

	PILOTDisplay *displayTop = new PILOTDisplay();
	displayTop->box.pos = Vec(sceneXAnchor+dispOffset,topSceneYAnchor);
	displayTop->box.size = Vec(20, 20);
	displayTop->value = module ? &module->topScene : NULL;
	addChild(displayTop);
	addParam(createParam<SaveBtn>(Vec(sceneXAnchor + ctrlSceneXOffset + sqBtnDrift, topSceneYAnchor), module, PILOT::SAVETOP_PARAM));
	addParam(createParam<Rnd2Btn>(Vec(sceneXAnchor + 2*ctrlSceneXOffset+ sqBtnDrift, topSceneYAnchor), module, PILOT::RNDTOP_PARAM));
	addParam(createParam<Rnd2Btn>(Vec(sceneXAnchor + 3*ctrlSceneXOffset+ sqBtnDrift, topSceneYAnchor), module, PILOT::FRNDTOP_PARAM));
	addParam(createParam<BidooBlueSnapTrimpot>(Vec(sceneXAnchor, topSceneYAnchor+ ctrlSceneYOffset), module, PILOT::TOPSCENE_PARAM));
	addParam(createParam<LeftBtn>(Vec(sceneXAnchor + ctrlSceneXOffset+ sqBtnDrift, topSceneYAnchor + ctrlSceneYOffset+sqBtnYDrift), module, PILOT::TOPSCENEMINUS_PARAM));
	addParam(createParam<RightBtn>(Vec(sceneXAnchor + 2*ctrlSceneXOffset+ sqBtnDrift, topSceneYAnchor + ctrlSceneYOffset+sqBtnYDrift), module, PILOT::TOPSCENEPLUS_PARAM));
	addParam(createParam<RndBtn>(Vec(sceneXAnchor + 3*ctrlSceneXOffset+ sqBtnDrift, topSceneYAnchor + ctrlSceneYOffset+sqBtnYDrift), module, PILOT::TOPSCENERND_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor+inputRoundBtnDrift, topSceneYAnchor + inputSceneYOffset), module, PILOT::TOPSCENE_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor + inputSceneXOffset+inputSquBtnDrift, topSceneYAnchor + inputSceneYOffset), module, PILOT::TOPSCENEMINUS_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor + 2*inputSceneXOffset+inputSquBtnDrift, topSceneYAnchor + inputSceneYOffset), module, PILOT::TOPSCENEPLUS_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor + 3*inputSceneXOffset+inputSquBtnDrift, topSceneYAnchor + inputSceneYOffset), module, PILOT::TOPSCENERND_INPUT));

	PILOTDisplay *displayBottom = new PILOTDisplay();
	displayBottom->box.pos = Vec(sceneXAnchor+dispOffset,bottomSceneYAnchor);
	displayBottom->box.size = Vec(20, 20);
	displayBottom->value = module ? &module->bottomScene : NULL;
	addChild(displayBottom);
	addParam(createParam<SaveBtn>(Vec(sceneXAnchor + ctrlSceneXOffset + sqBtnDrift, bottomSceneYAnchor), module, PILOT::SAVEBOTTOM_PARAM));
	addParam(createParam<Rnd2Btn>(Vec(sceneXAnchor + 2*ctrlSceneXOffset+ sqBtnDrift, bottomSceneYAnchor), module, PILOT::RNDBOTTOM_PARAM));
	addParam(createParam<Rnd2Btn>(Vec(sceneXAnchor + 3*ctrlSceneXOffset+ sqBtnDrift, bottomSceneYAnchor), module, PILOT::FRNDBOTTOM_PARAM));
	addParam(createParam<BidooBlueSnapTrimpot>(Vec(sceneXAnchor, bottomSceneYAnchor+ ctrlSceneYOffset), module, PILOT::BOTTOMSCENE_PARAM));
	addParam(createParam<LeftBtn>(Vec(sceneXAnchor + ctrlSceneXOffset+ sqBtnDrift, bottomSceneYAnchor + ctrlSceneYOffset+sqBtnYDrift), module, PILOT::BOTTOMSCENEMINUS_PARAM));
	addParam(createParam<RightBtn>(Vec(sceneXAnchor + 2*ctrlSceneXOffset+ sqBtnDrift, bottomSceneYAnchor + ctrlSceneYOffset+sqBtnYDrift), module, PILOT::BOTTOMSCENEPLUS_PARAM));
	addParam(createParam<RndBtn>(Vec(sceneXAnchor + 3*ctrlSceneXOffset+ sqBtnDrift, bottomSceneYAnchor + ctrlSceneYOffset+sqBtnYDrift), module, PILOT::BOTTOMSCENERND_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor+inputRoundBtnDrift, bottomSceneYAnchor + inputSceneYOffset), module, PILOT::BOTTOMSCENE_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor + inputSceneXOffset+inputSquBtnDrift, bottomSceneYAnchor + inputSceneYOffset), module, PILOT::BOTTOMSCENEMINUS_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor + 2*inputSceneXOffset+inputSquBtnDrift, bottomSceneYAnchor + inputSceneYOffset), module, PILOT::BOTTOMSCENEPLUS_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(sceneXAnchor + 3*inputSceneXOffset+inputSquBtnDrift, bottomSceneYAnchor + inputSceneYOffset), module, PILOT::BOTTOMSCENERND_INPUT));

	const int controlsSpacer = 57;
	const int outputsSpacer = 27;
	const int led_offset = 6;
	const int diffYled_offset = 20;
	const int controlsXAnchor = 208;
	const int controlsYAnchor = 35;
	const int outputsXAnchor = 335;
	const int outputsYAnchor = 257;
	const int ledsXAnchor = controlsXAnchor + 38;
	const int ledsYAnchor = controlsYAnchor;

	for (int i = 0; i < 16; i++) {
		controls[i] = createParam<PILOTColoredKnob>(Vec(controlsXAnchor + controlsSpacer *(i%4), controlsYAnchor + controlsSpacer * floor(i/4)), module, PILOT::CONTROLS_PARAMS + i);
		if (module) {
			dynamic_cast<PILOTColoredKnob*>(controls[i])->blink=&module->controlFocused[i];
		}
		addParam(controls[i]);
		addParam(createParam<BidooLEDButton>(Vec(ledsXAnchor + controlsSpacer * (i%4), ledsYAnchor + controlsSpacer * floor(i/4)), module, PILOT::TYPE_PARAMS + i));
		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(ledsXAnchor + led_offset + controlsSpacer * (i%4), ledsYAnchor + led_offset + controlsSpacer * floor(i/4)), module, PILOT::TYPE_LIGHTS + i*3));
		addParam(createParam<BidooLEDButton>(Vec(ledsXAnchor + controlsSpacer * (i%4), diffYled_offset + ledsYAnchor + controlsSpacer * floor(i/4)), module, PILOT::VOLTAGE_PARAMS + i));
		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(ledsXAnchor + led_offset + controlsSpacer * (i%4), diffYled_offset + ledsYAnchor + led_offset + controlsSpacer * floor(i/4)), module, PILOT::VOLTAGE_LIGHTS + i*3));
		addOutput(createOutput<PJ301MPort>(Vec(outputsXAnchor + outputsSpacer * (i%4), outputsYAnchor + outputsSpacer * floor(i/4)), module, PILOT::CV_OUTPUTS + i));
	}

	const int curveXAnchor = 16;
	const int curveYAnchor = 102;
	const int curveCtrlYOffset = 90;
	const int curveCtrlXOffset = 24;
	const int curveCtrlXAnchor = 10;
	const int curveTypeXAnchor = 15;
	const int curveTypeYAnchor = 65;
	const int morphSpeedXAnchor = 40;
	const int morphSpeedYAnchor = 60;
	const int controlsXOffest = 40;
	const int curveInputYOffset = 23;
	const int curveInputXOffset = 2;

	addParam(createParam<BidooLEDButton>(Vec(curveTypeXAnchor, curveTypeYAnchor), module, PILOT::CURVE_PARAM));
	addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(curveTypeXAnchor+6, curveTypeYAnchor+6), module, PILOT::CURVE_LIGHT));

	addParam(createParam<BidooBlueKnob>(Vec(morphSpeedXAnchor, morphSpeedYAnchor), module, PILOT::SPEED_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(morphSpeedXAnchor + controlsXOffest, morphSpeedYAnchor + 6), module, PILOT::SPEED_INPUT));

	PILOTCurveDisplay *displayCurve = new PILOTCurveDisplay();
	displayCurve->box.pos = Vec(curveXAnchor, curveYAnchor);
	displayCurve->box.size = Vec(78, 78);
	displayCurve->module = module ? module : NULL;
	addChild(displayCurve);
	addParam(createParam<BidooBlueTrimpot>(Vec(curveCtrlXAnchor , curveYAnchor+curveCtrlYOffset), module, PILOT::AX_PARAM));
	addParam(createParam<BidooBlueTrimpot>(Vec(curveCtrlXAnchor + curveCtrlXOffset, curveYAnchor+curveCtrlYOffset), module, PILOT::AY_PARAM));
	addParam(createParam<BidooBlueTrimpot>(Vec(curveCtrlXAnchor + 2*curveCtrlXOffset, curveYAnchor+curveCtrlYOffset), module, PILOT::BX_PARAM));
	addParam(createParam<BidooBlueTrimpot>(Vec(curveCtrlXAnchor + 3*curveCtrlXOffset, curveYAnchor+curveCtrlYOffset), module, PILOT::BY_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(curveCtrlXAnchor +curveInputXOffset, curveYAnchor+curveCtrlYOffset+curveInputYOffset), module, PILOT::AX_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(curveCtrlXAnchor + curveCtrlXOffset+curveInputXOffset, curveYAnchor+curveCtrlYOffset+curveInputYOffset), module, PILOT::AY_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(curveCtrlXAnchor + 2*curveCtrlXOffset+curveInputXOffset, curveYAnchor+curveCtrlYOffset+curveInputYOffset), module, PILOT::BX_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(curveCtrlXAnchor + 3*curveCtrlXOffset+curveInputXOffset, curveYAnchor+curveCtrlYOffset+curveInputYOffset), module, PILOT::BY_INPUT));


	const int controlsYOffest = 5;

	const int seqXAnchor = 15;
	const int seqYAnchor = 310;
	const int seqDispOffset = 33;

	addParam(createParam<BlueCKD6>(Vec(seqXAnchor, seqYAnchor), module, PILOT::MOVETYPE_PARAM));
	addParam(createParam<BidooBlueSnapKnob>(Vec(seqXAnchor + controlsXOffest, seqYAnchor), module, PILOT::LENGTH_PARAM));
	addParam(createParam<BidooLEDButton>(Vec(seqXAnchor + 2*controlsXOffest+4, seqYAnchor +controlsYOffest), module, PILOT::WEOM_PARAM));
	addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(seqXAnchor + 2*controlsXOffest+6+4, seqYAnchor+controlsYOffest+6), module, PILOT::WEOM_LIGHT));
	addParam(createParam<BlueCKD6>(Vec(seqXAnchor + 3*controlsXOffest, seqYAnchor), module, PILOT::MOVENEXT_PARAM));
	addInput(createInput<TinyPJ301MPort>(Vec(seqXAnchor + 4*controlsXOffest, seqYAnchor+controlsYOffest+2), module, PILOT::MOVENEXT_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(seqXAnchor + 5*controlsXOffest, seqYAnchor+controlsYOffest+2), module, PILOT::RESET_INPUT));
	addParam(createParam<BidooLEDButton>(Vec(seqXAnchor + 6*controlsXOffest+4, seqYAnchor +controlsYOffest), module, PILOT::RECORD_PARAM));
	addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(seqXAnchor + 6*controlsXOffest+6+4, seqYAnchor+controlsYOffest+6), module, PILOT::RECORD_LIGHT));

	PILOTMoveTypeDisplay *displayMoveType = new PILOTMoveTypeDisplay();
	displayMoveType->box.pos = Vec(seqXAnchor+13,seqYAnchor+seqDispOffset);
	displayMoveType->box.size = Vec(20, 20);
	displayMoveType->value = module ? &module->moveType : NULL;
	addChild(displayMoveType);

	PILOTDisplay *displayLength = new PILOTDisplay();
	displayLength->box.pos = Vec(seqXAnchor+ controlsXOffest+6,seqYAnchor+seqDispOffset);
	displayLength->box.size = Vec(20, 20);
	displayLength->value = module ? &module->length : NULL;
	addChild(displayLength);

	PILOTNoteDisplay *displayNote = new PILOTNoteDisplay();
	displayNote->box.pos = Vec(curveXAnchor,curveYAnchor);
	displayNote->box.size = Vec(20, 20);
	displayNote->module = module ? module : NULL;
	addChild(displayNote);
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

void PILOTWidget::step() {
	for (int i = 0; i < 16; i++) {
		dynamic_cast<PILOTColoredKnob*>(controls[i])->fb->setDirty();
	}
	ModuleWidget::step();
}

void PILOTWidget::draw(const DrawArgs& args) {
	ModuleWidget::draw(args);
}

Model *modelPILOT = createModel<PILOT, PILOTWidget>("PILOT");
