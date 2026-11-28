#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <sstream>
#include <iomanip>
#include <atomic>
#include "dep/quantizer.hpp"

using namespace std;

enum waitEOMType { waitExt, hesitate, whatever};
enum controlType { cv, cvJump, gate, voJump, voSlide};

struct PILOT : BidooModule {
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
		BANK_PARAM,
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
		BANK_INPUT,
		SPEED_INPUT,
		GOBOTTOM_INPUT,
		GOTOP_INPUT,
		MOVENEXT_INPUT,
		RESET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CV_OUTPUTS,
		POLY_OUTPUT = CV_OUTPUTS + 16,
		NUM_OUTPUTS
	};
	enum LightIds {
		WEOM_LIGHT,
		RECORD_LIGHT = WEOM_LIGHT + 3,
		CURVE_LIGHT = RECORD_LIGHT + 3,
		TYPE_LIGHTS = CURVE_LIGHT + 3,
		VOLTAGE_LIGHTS = TYPE_LIGHTS + 16*3,
		NUM_LIGHTS = VOLTAGE_LIGHTS + 16*3
	};

	float scenes[16][16][16] = {{{0.0f}}};
	int controlTypes[16] = {0};
	bool controlOverrideTypes[16] = {true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true };
	int voltageTypes[16] = {0};
	int controlRootNotes[16] = {0};
	int controlScales[16] = {0};
	bool controlFocused[16] = {false};
	// what the engine last wrote to each control: a param that differs was moved from outside (MIDI-Map, automation)
	float lastWritten[16] = {0.0f};
	float values[16] = {0.0f};
	int currentFocus = -1;
	bool morphFocused = false;
	int topScene = 0;
	int bottomScene = 0;
	int oldTopScene = 0;
	int oldBottomScene = 0;
	int bank = 0;
	float morph = 0.0f;
	double morphPos = 0.0;
	bool pulses[16] = {false};
	bool wasAtEnd = false;   // MORPH at 0 or 1 on the previous sample (yellow WAIT gates)
	float speed = 1e-3f;
	int moveType = 5;
	bool moving = false;
	// the direction of the last move: true when MORPH went to the top scene. A new module sits on the bottom
	// scene (MORPH 0), so the first move goes to the top scene.
	bool forward = false;
	bool changeDir =false;
	bool rev = false;
	int waitEOM = 0;
	bool curve = false;
	int length = 15;
	int recordingStatus = 0;
	bool reset = false;
	int copyBankId = -1;
	bool copyBankArmed = false;
	// UI -> audio thread: copy fillValues into one control's scenes (bank * 16 + control, -1 none)
	enum CtrlFill { FILL_RAND, FILL_RAMP_UP, FILL_RAMP_DOWN, FILL_SIN, FILL_INIT };
	std::atomic<int> ctrlFillRequest {-1};
	float fillValues[16] = {};
	// UI -> audio thread: paste or full-randomize one scene of the current bank (kind * 16 + scene, -1 none)
	enum SceneReq { SCENE_PASTE, SCENE_FRANDOM };
	std::atomic<int> sceneRequest {-1};
	int copySceneId = -1;
	int copySceneBankId = -1;
	bool showTapes = false;

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

	quantizer::Quantizer quant;

	// Quantized voltages per control (the knob, the bottom and the top scene),
	// searched again only when the value, the root or the scale changes.
	struct QuantCache {
		float in = NAN;
		int root = -100, scale = -1;
		float out = 0.f;
	};
	QuantCache qCur[16], qBot[16], qTop[16];

	float quantizeCached(QuantCache &c, float v, int root, int sc) {
		if (v != c.in || root != c.root || sc != c.scale) {
			c.in = v;
			c.root = root;
			c.scale = sc;
			c.out = std::get<0>(quant.closestVoltageInScale(v, root, sc));
		}
		return c.out;
	}

	// Lights at control rate; the move time only when its knob or CV moves.
	static constexpr int CONTROL_DIV = 16;
	int controlCounter = 0;
	float moveTimeIn = NAN;
	float moveTime = 1.f;

	std::string labels[16] = {"","","","","","","","","","","","","","","",""};

	PILOT() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(BOTTOMSCENE_PARAM, 0.0f, 15.0f, 0.0f, "Bottom scene", "", 0.f, 1.f, 1.f);
		configParam(TOPSCENE_PARAM, 0.0f, 15.0f, 0.0f, "Top scene", "", 0.f, 1.f, 1.f);
		paramQuantities[BOTTOMSCENE_PARAM]->snapEnabled = true;
		paramQuantities[TOPSCENE_PARAM]->snapEnabled = true;
		configButton(BOTTOMSCENEPLUS_PARAM, "Bottom scene +");
		configButton(BOTTOMSCENEMINUS_PARAM, "Bottom scene -");
		configButton(BOTTOMSCENERND_PARAM, "Bottom scene: random");
		configButton(SAVEBOTTOM_PARAM, "Save the controls to the bottom scene");
		configButton(TOPSCENEPLUS_PARAM, "Top scene +");
		configButton(TOPSCENEMINUS_PARAM, "Top scene -");
		configButton(TOPSCENERND_PARAM, "Top scene: random");
		configButton(SAVETOP_PARAM, "Save the controls to the top scene");
		configButton(GOBOTTOM_PARAM, "Go to the bottom scene");
		configButton(GOTOP_PARAM, "Go to the top scene");
		configParam(MORPH_PARAM, 0.0f, 1.0f, 0.0f, "Morph", "%", 0.f, 100.f);
		configButton(MOVETYPE_PARAM, "Move type");
		configButton(MOVENEXT_PARAM, "Move");
		// knob 0: a move takes 60 s, knob 1: 1 ms
		configParam(SPEED_PARAM, 0.0f, 1.0f, 0.5f, "Move time", " s", 1.f / 60000.f, 60.f);
		configButton(WEOM_PARAM, "Wait mode");
		configParam(LENGTH_PARAM, 0.0f, 15.0f, 15.0f, "Length", " scenes", 0.f, 1.f, 1.f);
		paramQuantities[LENGTH_PARAM]->snapEnabled = true;
		configButton(CURVE_PARAM, "Curve on/off");
		configButton(RECORD_PARAM, "Record");
		configButton(RNDTOP_PARAM, "Randomise the top scene");
		configButton(FRNDTOP_PARAM, "Randomise the top scene and the control types");
		configButton(RNDBOTTOM_PARAM, "Randomise the bottom scene");
		configButton(FRNDBOTTOM_PARAM, "Randomise the bottom scene and the control types");
		configParam(BANK_PARAM, 0.0f, 15.0f, 0.0f, "Bank", "", 0.f, 1.f, 1.f);
		paramQuantities[BANK_PARAM]->snapEnabled = true;
		configParam(AX_PARAM, -1.0f, 2.0f, 0.5f, "Curve point A, X");
		configParam(AY_PARAM, -5.0f, 5.0f, 0.5f, "Curve point A, Y");
		configParam(BX_PARAM, -1.0f, 2.0f, 0.5f, "Curve point B, X");
		configParam(BY_PARAM, -5.0f, 5.0f, 0.5f, "Curve point B, Y");
		for (int i = 0; i < 16; i++) {
			std::string n = "Control " + std::to_string(i + 1);
			configParam(CONTROLS_PARAMS + i, 0.0f, 1.0f, 0.0f, n, "%", 0.f, 100.f);
			configButton(TYPE_PARAMS + i, n + " type");
			configButton(VOLTAGE_PARAMS + i, n + " range (0-10 V or -5-5 V)");
			configOutput(CV_OUTPUTS + i, n);
		}
		configOutput(POLY_OUTPUT, "All 16 controls (polyphonic)");
		configInput(BOTTOMSCENE_INPUT, "Bottom scene (0-10 V over the 16 scenes)");
		configInput(TOPSCENE_INPUT, "Top scene (0-10 V over the 16 scenes)");
		configInput(MORPH_INPUT, "Morph (0-10 V)");
		configInput(BOTTOMSCENEPLUS_INPUT, "Bottom scene + trigger");
		configInput(BOTTOMSCENEMINUS_INPUT, "Bottom scene - trigger");
		configInput(BOTTOMSCENERND_INPUT, "Bottom scene random trigger");
		configInput(TOPSCENEPLUS_INPUT, "Top scene + trigger");
		configInput(TOPSCENEMINUS_INPUT, "Top scene - trigger");
		configInput(TOPSCENERND_INPUT, "Top scene random trigger");
		configInput(AX_INPUT, "Curve point A, X CV");
		configInput(AY_INPUT, "Curve point A, Y CV");
		configInput(BX_INPUT, "Curve point B, X CV");
		configInput(BY_INPUT, "Curve point B, Y CV");
		configInput(BANK_INPUT, "Bank (0-10 V over the 16 banks)");
		configInput(SPEED_INPUT, "Move time CV (0-10 V: full range)");
		configInput(GOBOTTOM_INPUT, "Go to the bottom scene trigger");
		configInput(GOTOP_INPUT, "Go to the top scene trigger");
		configInput(MOVENEXT_INPUT, "Move trigger");
		configInput(RESET_INPUT, "Reset trigger");
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

	void syncWritten() {
		for (int i = 0; i < 16; i++)
			lastWritten[i] = params[CONTROLS_PARAMS + i].getValue();
	}

	// UI thread: the values are worked out here (random included) so the fill can be undone
	void requestCtrlFill(int kind, int c) {
		c = clamp(c, 0, 15);
		const int b = clamp(bank, 0, 15);
		const int len = clamp(length, 0, 15);
		history::ModuleChange *h = new history::ModuleChange;
		h->name = "fill control";
		h->moduleId = id;
		h->oldModuleJ = APP->engine->moduleToJson(this);
		h->newModuleJ = json_deep_copy(h->oldModuleJ);
		json_t *bankJ = json_array_get(json_object_get(json_object_get(h->newModuleJ, "data"), "banks"), b);
		for (int i = 0; i < 16; i++) {
			float v = scenes[b][i][c];
			switch (kind) {
				case FILL_RAND: v = random::uniform(); break;
				case FILL_RAMP_UP: v = (i > 0 && i <= len) ? (float)i / len : 0.0f; break;
				case FILL_RAMP_DOWN: v = (i == 0) ? 1.0f : (i < len ? (float)(len - i) / len : 0.0f); break;
				// one scene: the formula's ends are sin(pi) = 0, avoid 0/0
				case FILL_SIN: v = (i <= len && len > 0) ? sin((float)(len - i) * M_PI / len) : 0.0f; break;
				case FILL_INIT: if (i <= len) v = 0.0f; break;
				default: break;
			}
			fillValues[i] = v;
			json_array_set_new(json_array_get(bankJ, i), c, json_real(v));
		}
		APP->history->push(h);
		ctrlFillRequest.store(b * 16 + c);
	}

	// audio thread only
	void fillCtrl(int b, int c) {
		for (int i = 0; i < 16; i++)
			scenes[b][i][c] = fillValues[i];
	}

	void onReset() override {
		forward = false;
		for (int i=0; i<16; i++) {
			for (int j=0; j<16; j++) {
				for (int k=0; k<16; k++) {
					scenes[i][j][k]=0.0f;
				}
			}
			controlTypes[i] = 0;
			controlOverrideTypes[i] = true;
			voltageTypes[i] = 0;
			controlRootNotes[i] = 0;
			controlScales[i] = 0;
			labels[i] = "";
			controlFocused[i] = false;
		}
		currentFocus = -1;
		moveType = 5;
		syncWritten();
	}

	// Pastes the copied scene into this bank's scene; nothing before a copy.
	void pasteScene(int scene) {
		if (copySceneBankId < 0 || copySceneId < 0)
			return;
		for (int i=0; i<16;i++)
			scenes[bank][scene][i] = scenes[copySceneBankId][copySceneId][i];
	}

	void randomizeScene(int scene) {
		for (int i = 0; i < 16; i++) {
				scenes[bank][scene][i]=random::uniform();
		}
	}

	void frandomizeScene(int scene) {
		for (int i = 0; i < 16; i++) {
				scenes[bank][scene][i]=random::uniform();
				// every type, note modes included
				controlTypes[i] = random::u32() % 5;
		}
	}

	void randomizeScenes() {
		for (int i = 0; i < 16; i++) {
			randomizeScene(i);
			controlTypes[i] = random::u32() % 5;
		}
	}

	// UI thread: process() applies it, as scenes and controlTypes belong to the audio thread
	void requestScene(int kind, int scene) {
		sceneRequest.store(kind * 16 + clamp(scene, 0, 15));
	}

	void randomizeTargetScene() {
		// one pass already randomizes every control of the scene
		requestScene(SCENE_FRANDOM, topScene);
	}

	void onRandomize() override {
		randomizeScenes();
		syncWritten();
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();

		json_object_set_new(rootJ, "moveType", json_integer(moveType));
		json_object_set_new(rootJ, "WEOM", json_integer(waitEOM));
		json_object_set_new(rootJ, "CURVE", json_boolean(curve));
		json_object_set_new(rootJ, "SHOWTAPES", json_boolean(showTapes));

		for(size_t i=0; i<16; i++) {
      json_object_set_new(rootJ, ("label" + to_string(i)).c_str(), json_string(labels[i].c_str()));
    }

		json_t *banksJ = json_array();
		json_t *typesJ = json_array();
		json_t *voltagesJ = json_array();
		json_t *rootNotesJ = json_array();
		json_t *scalesJ = json_array();
		json_t *overrideTypesJ = json_array();

		for(int b=0; b<16; b++) {
			json_t *bankJ = json_array();
			for (int i = 0; i < 16; i++) {
				json_t *sceneJ = json_array();
				for (int j = 0; j < 16; j++) {
					json_t *controlJ = json_real(scenes[b][i][j]);
					json_array_append_new(sceneJ, controlJ);
				}
				json_array_append_new(bankJ, sceneJ);
			}
			json_array_append_new(banksJ, bankJ);
			json_t *typeJ = json_integer(controlTypes[b]);
			json_array_append_new(typesJ, typeJ);
			json_t *voltageJ = json_integer(voltageTypes[b]);
			json_array_append_new(voltagesJ, voltageJ);
			json_t *rootNoteJ = json_integer(controlRootNotes[b]);
			json_array_append_new(rootNotesJ, rootNoteJ);
			json_t *scaleJ = json_integer(controlScales[b]);
			json_array_append_new(scalesJ, scaleJ);
			json_t *overrideTypeJ = json_boolean(controlOverrideTypes[b]);
			json_array_append_new(overrideTypesJ, overrideTypeJ);
		}
		json_object_set_new(rootJ, "banks", banksJ);
		json_object_set_new(rootJ, "types", typesJ);
		json_object_set_new(rootJ, "voltages", voltagesJ);
		json_object_set_new(rootJ, "roots", rootNotesJ);
		json_object_set_new(rootJ, "scales", scalesJ);
		json_object_set_new(rootJ, "overrides", overrideTypesJ);

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		// the params are loaded: the next move leaves the scene MORPH sits on (the first move after a load
		// skipped a scene when MORPH was at 0)
		forward = params[MORPH_PARAM].getValue() >= 0.5f;
		json_t *moveTypeJ = json_object_get(rootJ, "moveType");
		if (moveTypeJ)
			moveType = clamp((int)json_integer_value(moveTypeJ), 0, 5);

		json_t *weomJ = json_object_get(rootJ, "WEOM");
		if (weomJ)
			waitEOM = clamp((int)json_integer_value(weomJ), 0, 2);

		json_t *curveJ = json_object_get(rootJ, "CURVE");
		if (curveJ)
			curve = json_boolean_value(curveJ);

		json_t *showTapesJ = json_object_get(rootJ, "SHOWTAPES");
		if (showTapesJ)
			showTapes = json_boolean_value(showTapesJ);

		for(size_t i=0; i<16; i++) {
      json_t *labelJ=json_object_get(rootJ, ("label"+ to_string(i)).c_str());
      if (json_is_string(labelJ)) {
        labels[i] = json_string_value(labelJ);
      }
    }

		json_t *banksJ = json_object_get(rootJ, "banks");
		json_t *typesJ = json_object_get(rootJ, "types");
		json_t *voltagesJ = json_object_get(rootJ, "voltages");
		json_t *rootsJ = json_object_get(rootJ, "roots");
		json_t *scalesJ = json_object_get(rootJ, "scales");
		json_t *overridesJ = json_object_get(rootJ, "overrides");

		if (banksJ && typesJ) {
			for (int i = 0; i < 16; i++) {
				json_t *bankJ = json_array_get(banksJ, i);
				if (bankJ) {
					for (int j = 0; j < 16; j++) {
						json_t *sceneJ = json_array_get(bankJ, j);
						for (int k=0;k<16; k++) {
							json_t *controlJ = json_array_get(sceneJ, k);
							if (controlJ) {
								scenes[i][j][k] = json_number_value(controlJ);
							}
						}
					}
				}
				json_t *typeJ = json_array_get(typesJ, i);
				if (typeJ) {
					controlTypes[i] = clamp((int)json_integer_value(typeJ), 0, 4);
				}
				json_t *voltageJ = json_array_get(voltagesJ, i);
				if (voltageJ) {
					voltageTypes[i] = clamp((int)json_integer_value(voltageJ), 0, 1);
				}
				json_t *rootJ = json_array_get(rootsJ, i);
				if (rootJ) {
					controlRootNotes[i] = clamp((int)json_integer_value(rootJ), -1, 11);
				}
				json_t *scaleJ = json_array_get(scalesJ, i);
				if (scaleJ) {
					controlScales[i] = clamp((int)json_integer_value(scaleJ), 0, quantizer::numScales - 1);
				}
				json_t *overrideJ = json_array_get(overridesJ, i);
				if (overrideJ) {
					controlOverrideTypes[i] = json_boolean_value(overrideJ);
				}
			}
		}
		syncWritten();
	}

};

void PILOT::process(const ProcessArgs &args) {
	bool control = --controlCounter < 0;
	if (control)
		controlCounter = CONTROL_DIV - 1;
	changeDir =false;
	oldTopScene = topScene;
	oldBottomScene = bottomScene;

	bank = clamp((int)params[BANK_PARAM].getValue() + (int)std::floor(inputs[BANK_INPUT].getVoltage() * 1.6f), 0, 15);

	if (copyBankArmed) {
		if (copyBankId >= 0) {
			for (int i=0; i<16;i++) {
				for (int j=0; j<16; j++) {
					scenes[bank][i][j] = scenes[copyBankId][i][j];
				}
			}
		}
		copyBankArmed = false;
	}

	int fillReq = ctrlFillRequest.exchange(-1);
	if (fillReq >= 0)
		fillCtrl(fillReq / 16, fillReq % 16);

	int sceneReq = sceneRequest.exchange(-1);
	if (sceneReq >= 0) {
		if (sceneReq / 16 == SCENE_PASTE)
			pasteScene(sceneReq % 16);
		else
			frandomizeScene(sceneReq % 16);
	}

	if (moveTypeTrigger.process(params[MOVETYPE_PARAM].getValue())) {
		moveType = (moveType+1)%6;
	}

	if ((resetTrigger.process(inputs[RESET_INPUT].getVoltage())) && (moveType==0)) {
		reset = true;
	}

	if (weomTrigger.process(params[WEOM_PARAM].getValue())) {
		waitEOM = (waitEOM+1)%3;
	}
	if (control) {
		lights[WEOM_LIGHT].setBrightness(waitEOM!=waitExt?1:0);
		lights[WEOM_LIGHT+1].setBrightness(waitEOM==hesitate?1:0);
		lights[WEOM_LIGHT+2].setBrightness(waitEOM==waitExt?1:0);
	}

	if (curveTrigger.process(params[CURVE_PARAM].getValue())) {
		curve = !curve;
	}
	if (control) {
		lights[CURVE_LIGHT].setBrightness(curve?0:1);
		lights[CURVE_LIGHT+1].setBrightness(curve?0:1);
		lights[CURVE_LIGHT+2].setBrightness(curve?1:0);
	}

	if (recordingTrigger.process(params[RECORD_PARAM].getValue())) {
		if ((recordingStatus==0) && (moveType==0) && (waitEOM==waitExt)) {
			recordingStatus=1;
		}
	}

	float mt = clamp(params[SPEED_PARAM].getValue() + inputs[SPEED_INPUT].getVoltage() / 10.0f, 0.0f, 1.0f);
	if (mt != moveTimeIn) {
		moveTimeIn = mt;
		moveTime = 60.f * std::pow(1.f / 60000.f, mt);
	}
	speed = args.sampleTime / moveTime;

	length = params[LENGTH_PARAM].getValue();

	if ((reset) && (moveType==0)) {
			params[BOTTOMSCENE_PARAM].setValue(0);
			params[TOPSCENE_PARAM].setValue(int(params[BOTTOMSCENE_PARAM].getValue()+1.0f)%(length+1));
			params[MORPH_PARAM].setValue(0.0f);
			forward = false;
			reset=false;
			moving = true;
	}
	else if ((moveNextTrigger.process(params[MOVENEXT_PARAM].getValue()+inputs[MOVENEXT_INPUT].getVoltage())) && ((waitEOM==waitExt && !moving) || (waitEOM!=waitExt))) {
		moving = true;
		forward = !forward;
		changeDir = true;
		if (forward) {
			if (moveType==0) {
				params[TOPSCENE_PARAM].setValue(int(params[BOTTOMSCENE_PARAM].getValue()+1.0f)%(length+1));
				if ((params[TOPSCENE_PARAM].getValue()==0.0f) && (recordingStatus>0)) {
					recordingStatus=(recordingStatus+1)%4;
					if (recordingStatus==0) {
						if (currentFocus >= 0) controlFocused[currentFocus]=false;
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
					if (params[BOTTOMSCENE_PARAM].getValue()>0.0f) {
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
						if (currentFocus >= 0) controlFocused[currentFocus]=false;
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
						params[BOTTOMSCENE_PARAM].setValue(int(params[TOPSCENE_PARAM].getValue()-1.0f));
					}
					else {
						params[BOTTOMSCENE_PARAM].setValue(length);
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
			params[TOPSCENE_PARAM].setValue(clamp((int)(params[TOPSCENE_PARAM].getValue() + 1.0f)%(length+1), 0, length));
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
		topScene = clamp((int)params[TOPSCENE_PARAM].getValue(), 0, 15);
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
		bottomScene = clamp((int)params[BOTTOMSCENE_PARAM].getValue(), 0, 15);
	}

	if ((recordingStatus == 0) && ((oldTopScene != topScene) || (oldBottomScene != bottomScene))) {
		for (int i = 0 ; i < 16; i++) {
			if (!controlOverrideTypes[i]) controlFocused[i] = false;
		}
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
			scenes[bank][topScene][i] = params[CONTROLS_PARAMS+i].getValue();
			controlFocused[i]=false;
		}
		currentFocus = -1;
	}

	if (bottomSceneSaveTrigger.process(params[SAVEBOTTOM_PARAM].getValue())) {
		for (int i = 0; i < 16; i++) {
			scenes[bank][bottomScene][i] = params[CONTROLS_PARAMS+i].getValue();
			controlFocused[i]=false;
		}
		currentFocus = -1;
	}

	if (control) {
		const float lightTime = args.sampleTime * CONTROL_DIV;
		if (recordingStatus == 1) {
			lights[RECORD_LIGHT].setBrightness(0);
			lights[RECORD_LIGHT+1].setBrightness(0);
			lights[RECORD_LIGHT+2].setBrightness(lights[RECORD_LIGHT+2].getBrightness()<0.1f ? 1 : lights[RECORD_LIGHT+2].getBrightness()-lights[RECORD_LIGHT+2].getBrightness()*2.0f*lightTime);
		}
		else if (recordingStatus == 2) {
			lights[RECORD_LIGHT].setBrightness(lights[RECORD_LIGHT].getBrightness()<0.1f ? 1 : lights[RECORD_LIGHT].getBrightness()-lights[RECORD_LIGHT].getBrightness()*4.0f*lightTime);
			lights[RECORD_LIGHT+1].setBrightness(lights[RECORD_LIGHT+1].getBrightness()<0.1f ? 1 : lights[RECORD_LIGHT+1].getBrightness()-lights[RECORD_LIGHT+1].getBrightness()*4.0f*lightTime);
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
			// in double: a 60 s move adds 3e-7 per sample, too fine for a float
			if ((float)morphPos != params[MORPH_PARAM].getValue())
				morphPos = params[MORPH_PARAM].getValue();
			morphPos = clamp(morphPos + (double)speed * (forward ? 1.0 : -1.0), 0.0, 1.0);
			params[MORPH_PARAM].setValue((float)morphPos);
			if ((params[MORPH_PARAM].getValue() == 0.0f) || (params[MORPH_PARAM].getValue() == 1.0f)) {
				moving = false;
			}
		}

		morph = params[MORPH_PARAM].getValue();
	}

	// yellow WAIT: a gate control fires once when MORPH reaches an end
	bool atEnd = (morph == 0.0f) || (morph == 1.0f);
	bool arrived = atEnd && !wasAtEnd;
	wasAtEnd = atEnd;

	// CURVE: the same shape for every control
	const float curveMorph = curve ? getYBez(morph) : 0.0f;

	for (int i = 0; i < 16; i++) {
		// an outside move takes the control over, as a mouse grab does
		if (!controlFocused[i] && std::fabs(params[CONTROLS_PARAMS+i].getValue() - lastWritten[i]) > 1e-4f) {
			controlFocused[i] = true;
			currentFocus = i;
		}

		if (recordingStatus==3 && i==currentFocus) {
			if (params[MORPH_PARAM].getValue() == 0.0f) {
				scenes[bank][bottomScene][i]=params[CONTROLS_PARAMS+i].getValue();
			}
			else if (params[MORPH_PARAM].getValue() == 1.0f) {
				scenes[bank][topScene][i]=params[CONTROLS_PARAMS+i].getValue();
			}
		}

		if (typeTriggers[i].process(params[TYPE_PARAMS + i].getValue())) {
			controlTypes[i] = (controlTypes[i]+1)%5;
		}
		if (voltageTriggers[i].process(params[VOLTAGE_PARAMS + i].getValue())) {
			voltageTypes[i] = voltageTypes[i] == 0 ? 1 : 0;
		}

		if (control) {
			lights[TYPE_LIGHTS+ i*3].setBrightness((controlTypes[i] == cvJump) || (controlTypes[i] == voJump) || (controlTypes[i] == voSlide) ? 1 : 0);
			lights[TYPE_LIGHTS+ i*3 + 1].setBrightness((controlTypes[i] == cvJump) || (controlTypes[i] == gate) ? 1 : ((controlTypes[i] == voSlide) ? 0.5f : 0));
			lights[TYPE_LIGHTS+ i*3 + 2].setBrightness(controlTypes[i] == cv ? 1 : 0);
			lights[VOLTAGE_LIGHTS+ i*3].setBrightness(voltageTypes[i] == 1 ? 1 : 0);
			lights[VOLTAGE_LIGHTS+ i*3 + 1].setBrightness(voltageTypes[i] == 1 ? 1 : 0);
			lights[VOLTAGE_LIGHTS+ i*3 + 2].setBrightness(voltageTypes[i] == 0 ? 1 : 0);
		}

		if (!controlFocused[i]) {
			if ((controlTypes[i] == cv) || (controlTypes[i] == voSlide)) {
				if (!curve) {
					if (scenes[bank][bottomScene][i] != scenes[bank][topScene][i]) {
						params[CONTROLS_PARAMS+i].setValue(clamp(rescale(morph,0.0f,1.0f,scenes[bank][bottomScene][i],scenes[bank][topScene][i]),0.0f,1.0f));
					}
					else {
						params[CONTROLS_PARAMS+i].setValue(scenes[bank][bottomScene][i]);
					}
				}
				else {
					if (scenes[bank][bottomScene][i] != scenes[bank][topScene][i]) {
						params[CONTROLS_PARAMS+i].setValue(rescale(curveMorph,0.0f,1.0f,scenes[bank][bottomScene][i],scenes[bank][topScene][i]));
					}
					else {
						params[CONTROLS_PARAMS+i].setValue(scenes[bank][bottomScene][i]);
					}
				}
			}
			else {
				if (morph == 1.0f || (changeDir && (waitEOM==whatever) && forward)) {
					params[CONTROLS_PARAMS+i].setValue(scenes[bank][topScene][i]);
				}
				else if (morph == 0.0f || (changeDir && (waitEOM==whatever) && !forward)) {
					params[CONTROLS_PARAMS+i].setValue(scenes[bank][bottomScene][i]);
				}
			}
		}
		lastWritten[i] = params[CONTROLS_PARAMS+i].getValue();

		pulses[i] = gatePulses[i].process(args.sampleTime) && (params[CONTROLS_PARAMS+i].getValue()>0.0f);

		if ((changeDir && waitEOM!=hesitate) || (waitEOM==hesitate && arrived)) {
			if (params[CONTROLS_PARAMS+i].getValue()>0.0f)  {
				gatePulses[i].trigger(powf(params[CONTROLS_PARAMS+i].getValue(),8.0f));
			}
			else {
				gatePulses[i].reset();
				pulses[i] = false;
			}
		}

		if (controlTypes[i] >= voJump) {
			if (controlFocused[i] || (controlTypes[i] == voSlide)) {
				outputs[CV_OUTPUTS + i].setVoltage(quantizeCached(qCur[i], params[CONTROLS_PARAMS+i].getValue()*10.0f-4.0f, controlRootNotes[i], controlScales[i]));
			}
			else {
				if ((scenes[bank][bottomScene][i]==scenes[bank][topScene][i]) || (bottomScene==topScene)) {
					outputs[CV_OUTPUTS + i].setVoltage(quantizeCached(qBot[i], scenes[bank][bottomScene][i]*10.0f-4.0f, controlRootNotes[i], controlScales[i]));
				}
				else {
					outputs[CV_OUTPUTS + i].setVoltage(rescale(params[CONTROLS_PARAMS+i].getValue(),scenes[bank][bottomScene][i],scenes[bank][topScene][i],
					quantizeCached(qBot[i], scenes[bank][bottomScene][i]*10.0f-4.0f, controlRootNotes[i], controlScales[i]),quantizeCached(qTop[i], scenes[bank][topScene][i]*10.0f-4.0f, controlRootNotes[i], controlScales[i])));
				}
			}
		}
		else if (controlTypes[i] == gate) {
			outputs[CV_OUTPUTS + i].setVoltage(pulses[i] ? 10.0f-5.0f*voltageTypes[i] : 0.0f);
		}
		else {
			outputs[CV_OUTPUTS + i].setVoltage(params[CONTROLS_PARAMS+i].getValue()*10.0f-5.0f*voltageTypes[i]);
		}
		outputs[POLY_OUTPUT].setVoltage(outputs[CV_OUTPUTS + i].getVoltage(), i);
	}
	outputs[POLY_OUTPUT].setChannels(16);
}

struct PILOTLabelDisplay : SvgWidget {
	PILOT *module;
	int id;

	PILOTLabelDisplay() {
		setSvg(Svg::load(asset::plugin(pluginInstance, "res/TAPE.svg")));
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (module && (layer == 1)) {
			nvgFontSize(args.vg, 8);
			nvgTextLetterSpacing(args.vg, -1);
  		nvgFillColor(args.vg, ORANGE_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
  		nvgText(args.vg, 19, 4, module->labels[id].c_str(), NULL);
		}
		Widget::drawLayer(args, layer);
	}
};

struct PILOTWidget : BidooWidget {
	ParamWidget *controls[16];
	ParamWidget *morphButton;
	PILOTLabelDisplay *labels[16];
	void appendContextMenu(ui::Menu *menu) override;
	void draw(const DrawArgs& args) override;
	void step() override;
  PILOTWidget(PILOT *module);
};

struct PILOTMoveTypeDisplay : TransparentWidget {
	int *value;

	PILOTMoveTypeDisplay() {

	}

	const char *displayPlayMode() {
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

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			nvgFontSize(args.vg, 18.0f);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			if (value) {
				if (*value>2) {
					nvgText(args.vg, 0.0f, 14, displayPlayMode(), NULL);
				}
				else {
					nvgText(args.vg, 0.0f, 12, displayPlayMode(), NULL);
				}
			}
		}
		Widget::drawLayer(args, layer);
	}

};


struct PILOTDisplay : TransparentWidget {
	int *value;

	PILOTDisplay() {

	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (value) {
	      nvgFontSize(args.vg, 18);
	  		nvgTextLetterSpacing(args.vg, -2);
	  		nvgFillColor(args.vg, YELLOW_BIDOO);
	  		char s[16];
	  		snprintf(s, sizeof(s), "%02d", *value + 1);
	  		nvgText(args.vg, 0, 14, s, NULL);
	    }
		}
		Widget::drawLayer(args, layer);
	}

};

struct PILOTNoteDisplay : TransparentWidget {
	PILOT *module;

	PILOTNoteDisplay() {

	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if ((module) && (module->currentFocus>=0) && (module->controlTypes[module->currentFocus]>=3)) {
	      nvgFontSize(args.vg, 18);
	  		nvgTextLetterSpacing(args.vg, -2);
	  		nvgFillColor(args.vg, YELLOW_BIDOO);
				nvgText(args.vg, 0, 12, module->quant.noteName(module->outputs[PILOT::CV_OUTPUTS+module->currentFocus].getVoltage()).c_str(), NULL);
	    }
		}
		Widget::drawLayer(args, layer);
	}

};

struct PILOTCurveDisplay : TransparentWidget {
	PILOT *module;

	PILOTCurveDisplay() {
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
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
				for(float i=0 ; i<=1; i=i+0.01) {
					if (i == 0) {
						if (module->curve) {
							nvgMoveTo(args.vg, module->getXBez(i)*box.size.x,box.size.y - module->getYBez(i)*box.size.y);
						}
						else {
							nvgMoveTo(args.vg, i*box.size.x,box.size.y - i*box.size.y);
						}
					}
					else {
						if (module->curve) {
							nvgLineTo(args.vg, module->getXBez(i)*box.size.x,box.size.y - module->getYBez(i)*box.size.y);
						}
						else {
							nvgLineTo(args.vg, i*box.size.x,box.size.y - i*box.size.y);
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
		Widget::drawLayer(args, layer);
	}

};

struct PILOTMorphKnob : BidooHugeRedKnob {

	PILOTMorphKnob() {
		smooth = false;
	}

	void onDragStart(const DragStartEvent& e) override {
		// no module in the library browser
		if (PILOT *module = getParamQuantity() ? dynamic_cast<PILOT*>(getParamQuantity()->module) : nullptr)
			module->morphFocused = true;
		//e.consume(this);
		BidooHugeRedKnob::onDragStart(e);
	}

	void onDragEnd(const DragEndEvent& e) override {
		if (PILOT *module = getParamQuantity() ? dynamic_cast<PILOT*>(getParamQuantity()->module) : nullptr)
			module->morphFocused = false;
		//e.consume(this);
		BidooHugeRedKnob::onDragEnd(e);
	}

	void onButton(const event::Button &e) override {
			if (PILOT *module = getParamQuantity() ? dynamic_cast<PILOT*>(getParamQuantity()->module) : nullptr) {
				for (int i = 0 ; i < 16; i++) {
					module->controlFocused[i] = false;
				}
			}
			//e.consume(this);
			BidooHugeRedKnob::onButton(e);
		}
};

// The knob colour follows its value; a focused knob (blink) fades its alpha
// 255 -> 165 over 0.5 s, then stays at 255 for 0.25 s. The framebuffer is only
// redrawn when the colour changes.
struct BidooLargeColoredKnob : RoundKnob {
	bool *blink=NULL;
	unsigned int color = 0;
	bool colorValid = false;
	std::vector<NSVGshape*> colorShapes;

	BidooLargeColoredKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/ColoredLargeKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/ColoredLargeKnobBidoo-bg.svg")));
		shadow->opacity = 0.0f;
		if (bg->svg && bg->svg->handle) {
			for (NSVGshape *shape = bg->svg->handle->shapes; shape != NULL; shape = shape->next) {
				if (!strcmp(shape->id, "bidooKnob") || !strcmp(shape->id, "bidooInterior"))
					colorShapes.push_back(shape);
			}
		}
	}

	unsigned int currentColor() {
		float v = getParamQuantity()->getValue();
		unsigned int c = (((unsigned int)42+(unsigned int)(v*210)) | (((unsigned int)87-(unsigned int)(v*80)) << 8) | (((unsigned int)117-(unsigned int)(v*10)) << 16));
		unsigned int tFade = 255;
		if (blink && *blink) {
			double phase = std::fmod(system::getTime(), 0.75);
			if (phase < 0.5)
				tFade = 255 - (unsigned int)(phase / 0.5 * 90.0);
		}
		return (c & 0xFFFFFF) | (tFade << 24);
	}

	void step() override {
		if (getParamQuantity()) {
			unsigned int c = currentColor();
			if (!colorValid || c != color) {
				color = c;
				colorValid = true;
				fb->setDirty();
			}
		}
		RoundKnob::step();
	}

	void draw(const DrawArgs& args) override {
		// The background SVG is shared by the 16 knobs: set this knob's colour
		// before its framebuffer (re)renders.
		if (getParamQuantity() && colorValid) {
			for (NSVGshape *shape : colorShapes)
				shape->fill.color = color;
		}
		RoundKnob::draw(args);
	}
};

// The fills are worked out on the UI thread (with undo) and copied in at the top of process()
struct CtrlRandMenuItem : ui::MenuItem {
	ParamQuantity* param = NULL;
	void onAction(const ActionEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(param->module);
		if (module)
			module->requestCtrlFill(PILOT::FILL_RAND, param->paramId - PILOT::CONTROLS_PARAMS);
	}
};

struct CtrlRampUpMenuItem : ui::MenuItem {
	ParamQuantity* param = NULL;
	void onAction(const ActionEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(param->module);
		if (module)
			module->requestCtrlFill(PILOT::FILL_RAMP_UP, param->paramId - PILOT::CONTROLS_PARAMS);
	}
};

struct CtrlRampDownMenuItem : ui::MenuItem {
	ParamQuantity* param = NULL;
	void onAction(const ActionEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(param->module);
		if (module)
			module->requestCtrlFill(PILOT::FILL_RAMP_DOWN, param->paramId - PILOT::CONTROLS_PARAMS);
	}
};

struct CtrlSinMenuItem : ui::MenuItem {
	ParamQuantity* param = NULL;
	void onAction(const ActionEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(param->module);
		if (module)
			module->requestCtrlFill(PILOT::FILL_SIN, param->paramId - PILOT::CONTROLS_PARAMS);
	}
};

struct CtrlInitMenuItem : ui::MenuItem {
	ParamQuantity* param = NULL;
	void onAction(const ActionEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(param->module);
		if (module)
			module->requestCtrlFill(PILOT::FILL_INIT, param->paramId - PILOT::CONTROLS_PARAMS);
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

struct CtrlOverrideTypeItem : ui::MenuItem {
	ParamQuantity* param = NULL;

	void onAction(const ActionEvent& e) override {
		PILOT *module = dynamic_cast<PILOT*>(param->module);
		module->controlOverrideTypes[param->paramId - PILOT::PILOT::CONTROLS_PARAMS] = !module->controlOverrideTypes[param->paramId - PILOT::PILOT::CONTROLS_PARAMS];
	}
};

struct PILOTColoredKnob : BidooLargeColoredKnob {

	PILOTColoredKnob () {
		smooth = false;
	}

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

		CtrlRandMenuItem* itemRnd = new CtrlRandMenuItem;
		itemRnd->text = "Randomize";
		itemRnd->param = this->getParamQuantity();
		menu->addChild(itemRnd);

		CtrlInitMenuItem* itemInit = new CtrlInitMenuItem;
		itemInit->text = "Init";
		itemInit->param = this->getParamQuantity();
		menu->addChild(itemInit);

		CtrlOverrideTypeItem* itemOverrideType = new CtrlOverrideTypeItem;
		itemOverrideType->text = "Const. override" ;
		PILOT *mod = dynamic_cast<PILOT*>(this->module);
		if (mod->controlOverrideTypes[this->paramId - PILOT::PILOT::CONTROLS_PARAMS]) itemOverrideType->rightText = CHECKMARK_STRING;
		itemOverrideType->param = this->getParamQuantity();
		menu->addChild(itemOverrideType);

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
			PILOT *module = getParamQuantity() ? dynamic_cast<PILOT*>(getParamQuantity()->module) : nullptr;
			if (!module)
				return;
			module->controlFocused[this->getParamQuantity()->paramId - PILOT::PILOT::CONTROLS_PARAMS] = true;
			module->currentFocus = this->getParamQuantity()->paramId - PILOT::PILOT::CONTROLS_PARAMS;
		}

	}
};

struct PilotBankBtn : BidooBlueSnapKnob {
	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			PILOT *mod = getParamQuantity() ? static_cast<PILOT*>(getParamQuantity()->module) : nullptr;
			if (mod && e.key == GLFW_KEY_C)
				mod->copyBankId = mod->bank;
			if (mod && e.key == GLFW_KEY_V)
				mod->copyBankArmed = true;
		}
		BidooBlueSnapKnob::onHoverKey(e);
	}
};

struct PILOTCopyTopSceneItem : MenuItem {
	PILOT *module;
	void onAction(const event::Action &e) override {
		module->copySceneBankId = module->bank;
		module->copySceneId = module->topScene;
	}
};

struct PILOTPasteTopSceneItem : MenuItem {
	PILOT *module;
	void onAction(const event::Action &e) override {
		module->requestScene(PILOT::SCENE_PASTE, module->topScene);
	}
};

struct PilotTopSceneBtn : BidooBlueSnapTrimpot {
	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			// no module in the library browser
			PILOT *mod = getParamQuantity() ? static_cast<PILOT*>(getParamQuantity()->module) : nullptr;
			if (mod && e.key == GLFW_KEY_C) {
				mod->copySceneBankId = mod->bank;
				mod->copySceneId = mod->topScene;
			}

			if (mod && e.key == GLFW_KEY_V) {
				mod->requestScene(PILOT::SCENE_PASTE, mod->topScene);
			}
		}
		BidooBlueSnapTrimpot::onHoverKey(e);
	}

	void appendContextMenu(ui::Menu *menu) override {
		BidooBlueSnapTrimpot::appendContextMenu(menu);
		PILOT *module = dynamic_cast<PILOT*>(this->module);
		assert(module);
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<PILOTCopyTopSceneItem>(&MenuItem::text, "Copy scene (over+C)", &PILOTCopyTopSceneItem::module, module));
		menu->addChild(construct<PILOTPasteTopSceneItem>(&MenuItem::text, "Paste scene (over+V)", &PILOTPasteTopSceneItem::module, module));
	}
};

struct PILOTCopyBottomSceneItem : MenuItem {
	PILOT *module;
	void onAction(const event::Action &e) override {
		module->copySceneBankId = module->bank;
		module->copySceneId = module->bottomScene;
	}
};

struct PILOTPasteBottomSceneItem : MenuItem {
	PILOT *module;
	void onAction(const event::Action &e) override {
		module->requestScene(PILOT::SCENE_PASTE, module->bottomScene);
	}
};

struct PilotBottomSceneBtn : BidooBlueSnapTrimpot {
	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			// no module in the library browser
			PILOT *mod = getParamQuantity() ? static_cast<PILOT*>(getParamQuantity()->module) : nullptr;
			if (mod && e.key == GLFW_KEY_C) {
				mod->copySceneBankId = mod->bank;
				mod->copySceneId = mod->bottomScene;
			}

			if (mod && e.key == GLFW_KEY_V) {
				mod->requestScene(PILOT::SCENE_PASTE, mod->bottomScene);
			}
		}
		BidooBlueSnapTrimpot::onHoverKey(e);
	}

	void appendContextMenu(ui::Menu *menu) override {
		BidooBlueSnapTrimpot::appendContextMenu(menu);
		PILOT *module = dynamic_cast<PILOT*>(this->module);
		assert(module);
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<PILOTCopyBottomSceneItem>(&MenuItem::text, "Copy scene (over+C)", &PILOTCopyBottomSceneItem::module, module));
		menu->addChild(construct<PILOTPasteBottomSceneItem>(&MenuItem::text, "Paste scene (over+V)", &PILOTPasteBottomSceneItem::module, module));
	}
};


PILOTWidget::PILOTWidget(PILOT *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/PILOT.svg"));

	theme::addScrews(this);

	// The panel of tools/panels/pilot.py: the old arrangement. Left: MORPHING (CURVE, SPEED, the curve screen and its
	// trimpots), BANK and the SEQUENCER row; middle: the two scenes and the morph knob between them; right: the 16 knobs
	// with their type and voltage buttons, the 16 outputs and POLY. The controls keep their old places (specific: the old
	// corners, or steps from them), snapped onto the rows and columns they share; TRIG and RESET's jacks (full jacks
	// now), POLY and the outputs' last row go on the bottom row.
	const float w = box.size.x;
	const theme::Size kLEDButtonSize = {17.9996f, 18.f};   // BidooLEDButton
	const theme::Size kSmallBtnSize = {16.f, 16.f};        // SaveBtn, Rnd2Btn, RndBtn, LeftBtn, RightBtn
	auto button = [&](theme::Pos p, int paramId) {         // the house round button, momentary as the old CKD6s
		BidooRoundButton *b = theme::param<BidooRoundButton>(p, module, paramId);
		b->momentary = true;
		addParam(b);
	};
	// a button whose RGB light says which of several states (the control types, the voltage ranges, the curve, the wait
	// modes, recording): the old LED button kept with its light, on the button's centre (it was 0.05 px off)
	auto ledButton = [&](theme::Pos p, int paramId, int lightId) {
		addParam(theme::param<BidooLEDButton>(p, module, paramId));
		addChild(theme::light<SmallLight<RedGreenBlueLight>>(p, module, lightId));
	};

	// ---- Left, MORPHING: CURVE and SPEED on the SPEED knob's row (CURVE's button was 0.76 px higher, SPEED's CV jack
	// 1.26 px); SPEED and its CV jack 2 px right of their old corners, so its label clears CURVE's (SPEED_X in pilot.py)
	const theme::Pos speed = theme::specificCorner(42, 60, theme::kBlueKnobSize);
	ledButton(theme::colRow(theme::specificCorner(15, 65, kLEDButtonSize), speed), PILOT::CURVE_PARAM, PILOT::CURVE_LIGHT);
	addParam(theme::param<BidooBlueKnob>(speed, module, PILOT::SPEED_PARAM));
	addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specificCorner(82, 0, theme::kTinyJackSize), speed), module, PILOT::SPEED_INPUT));

	// the curve display, 6 px inside the panel's screen (CURVE_SCREEN in pilot.py), the focused control's note in its corner
	const theme::Area curveDisplay = theme::specificRect(16, 102, 78, 78);
	PILOTCurveDisplay *displayCurve = new PILOTCurveDisplay();
	displayCurve->box = curveDisplay;
	displayCurve->module = module ? module : NULL;
	addChild(displayCurve);
	PILOTNoteDisplay *displayNote = new PILOTNoteDisplay();
	displayNote->box = theme::specificRect(curveDisplay.x, curveDisplay.y, 20, 20);
	displayNote->module = module ? module : NULL;
	addChild(displayNote);

	// AX, AY, BX, BY under it, each over its tiny CV jack (the trimpots were 0.1 px right of their jacks' columns)
	const float kCurvePitch = 24.f;   // CURVE_COLS in pilot.py
	const theme::Pos curveCv = theme::specificCorner(12, 215, theme::kTinyJackSize);
	const theme::Pos curveTrim = theme::colRow(curveCv, theme::specificCorner(10, 192, theme::kTrimpotSize));
	for (int i = 0; i < 4; i++) {
		addParam(theme::param<BidooBlueTrimpot>(curveTrim.right(kCurvePitch * i), module, PILOT::AX_PARAM + i));
		addInput(theme::input<TinyPJ301MPort>(curveCv.right(kCurvePitch * i), module, PILOT::AX_INPUT + i));
	}

	// BANK: its number's display in the panel's screen, the knob, its CV jack on the knob's row (0.26 px lower)
	const theme::Pos bank = theme::specificCorner(46, 250, theme::kBlueKnobSize);
	PILOTDisplay *displayBank = new PILOTDisplay();
	displayBank->box = theme::specificRect(13, 256.5f, 20, 20);
	displayBank->value = module ? &module->bank : NULL;
	addChild(displayBank);
	addParam(theme::param<PilotBankBtn>(bank, module, PILOT::BANK_PARAM));
	addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specificCorner(84, 0, theme::kTinyJackSize), bank), module, PILOT::BANK_INPUT));

	// ---- The SEQUENCER row, on the LENGTH knob's row (TYPE, WAIT, TRIG and REC were 0.76 px higher): TYPE and TRIG round
	// buttons where the CKD6s were, WAIT and REC LED buttons; the move type and length displays under TYPE and LENGTH
	const theme::Pos length = theme::specificCorner(55, 310, theme::kBlueKnobSize);
	button(theme::colRow(theme::specificCorner(15, 310, theme::kCKD6Size), length), PILOT::MOVETYPE_PARAM);
	addParam(theme::param<BidooBlueSnapKnob>(length, module, PILOT::LENGTH_PARAM));
	ledButton(theme::colRow(theme::specificCorner(99, 315, kLEDButtonSize), length), PILOT::WEOM_PARAM, PILOT::WEOM_LIGHT);
	button(theme::colRow(theme::specificCorner(135, 310, theme::kCKD6Size), length), PILOT::MOVENEXT_PARAM);
	ledButton(theme::colRow(theme::specificCorner(259, 315, kLEDButtonSize), length), PILOT::RECORD_PARAM, PILOT::RECORD_LIGHT);
	PILOTMoveTypeDisplay *displayMoveType = new PILOTMoveTypeDisplay();
	displayMoveType->box = theme::specificRect(29, 343, 20, 20);   // the text is centred on its left edge: TYPE's column
	displayMoveType->value = module ? &module->moveType : NULL;
	addChild(displayMoveType);
	PILOTDisplay *displayLength = new PILOTDisplay();
	displayLength->box = theme::specificRect(60, 343, 20, 20);
	displayLength->value = module ? &module->length : NULL;
	addChild(displayLength);
	// TRIG and RESET's jacks: full jacks in boxes on the bottom row, at the old tiny jacks' columns (17.5 px lower);
	// POLY at its old column, on the bottom row too (7.85 px higher)
	addInput(theme::input<PJ301MPort>(theme::sameCol(theme::specificCorner(175, 0, theme::kTinyJackSize), theme::row(0)), module, PILOT::MOVENEXT_INPUT));
	addInput(theme::input<PJ301MPort>(theme::sameCol(theme::specificCorner(215, 0, theme::kTinyJackSize), theme::row(0)), module, PILOT::RESET_INPUT));
	addOutput(theme::output<PJ301MPort>(theme::sameCol(theme::specificCorner(288, 0, theme::kJackSize), theme::row(0)), module, PILOT::POLY_OUTPUT));

	// ---- The scenes, the bottom one 161 px under the top one (SCENE_DOWN in pilot.py): the number's display in the
	// panel's screen; SAVE, RND and FULL RND; the scene trimpot and the -, + and RND buttons on its row (0.4 px higher);
	// their tiny CV jacks under them, on their columns (0.1 px right for the trimpot's, 0.5 px left for the buttons')
	const float kSceneDown = 161.f;
	const float kSceneBtnPitch = 20.f;
	const theme::Pos sceneTrim = theme::specificCorner(119, 49, theme::kTrimpotSize);   // the top scene's trimpot
	struct SceneIds { int *value; int save, rnd, frnd, minus, plus, rndBtn, cv, minusCv, plusCv, rndCv; };
	const SceneIds scenes[2] = {
		{module ? &module->topScene : NULL, PILOT::SAVETOP_PARAM, PILOT::RNDTOP_PARAM, PILOT::FRNDTOP_PARAM,
			PILOT::TOPSCENEMINUS_PARAM, PILOT::TOPSCENEPLUS_PARAM, PILOT::TOPSCENERND_PARAM,
			PILOT::TOPSCENE_INPUT, PILOT::TOPSCENEMINUS_INPUT, PILOT::TOPSCENEPLUS_INPUT, PILOT::TOPSCENERND_INPUT},
		{module ? &module->bottomScene : NULL, PILOT::SAVEBOTTOM_PARAM, PILOT::RNDBOTTOM_PARAM, PILOT::FRNDBOTTOM_PARAM,
			PILOT::BOTTOMSCENEMINUS_PARAM, PILOT::BOTTOMSCENEPLUS_PARAM, PILOT::BOTTOMSCENERND_PARAM,
			PILOT::BOTTOMSCENE_INPUT, PILOT::BOTTOMSCENEMINUS_INPUT, PILOT::BOTTOMSCENEPLUS_INPUT, PILOT::BOTTOMSCENERND_INPUT},
	};
	for (int s = 0; s < 2; s++) {
		const SceneIds &ids = scenes[s];
		const float dy = kSceneDown * s;
		const theme::Pos trim = sceneTrim.down(dy);
		const theme::Pos save = theme::specificCorner(142, 29 + dy, kSmallBtnSize);
		const theme::Pos cv = theme::colRow(trim, theme::specificCorner(0, 72 + dy, theme::kTinyJackSize));
		PILOTDisplay *display = new PILOTDisplay();
		display->box = theme::specificRect(120.7f, 29 + dy, 20, 20);
		display->value = ids.value;
		addChild(display);
		addParam(theme::param<SaveBtn>(save, module, ids.save));
		addParam(theme::param<Rnd2Btn>(save.right(kSceneBtnPitch), module, ids.rnd));
		addParam(theme::param<Rnd2Btn>(save.right(2.f * kSceneBtnPitch), module, ids.frnd));
		if (s == 0)
			addParam(theme::param<PilotTopSceneBtn>(trim, module, PILOT::TOPSCENE_PARAM));
		else
			addParam(theme::param<PilotBottomSceneBtn>(trim, module, PILOT::BOTTOMSCENE_PARAM));
		addParam(theme::param<LeftBtn>(theme::colRow(save, trim), module, ids.minus));
		addParam(theme::param<RightBtn>(theme::colRow(save.right(kSceneBtnPitch), trim), module, ids.plus));
		addParam(theme::param<RndBtn>(theme::colRow(save.right(2.f * kSceneBtnPitch), trim), module, ids.rndBtn));
		addInput(theme::input<TinyPJ301MPort>(cv, module, ids.cv));
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(save, cv), module, ids.minusCv));
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(save.right(kSceneBtnPitch), cv), module, ids.plusCv));
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(save.right(2.f * kSceneBtnPitch), cv), module, ids.rndCv));
	}

	// the morph knob at its old corner; its CV jack on the scenes' column and the knob's row (1.1 px right, 0.45 px
	// lower); the go to top and bottom buttons on the scenes' column (0.6 px right) at the old CKD6s' rows, their CV jacks
	const theme::Pos morph = theme::specificCorner(144, 111, theme::kHugeKnobSize);
	morphButton = theme::param<PILOTMorphKnob>(morph, module, PILOT::MORPH_PARAM);
	addParam(morphButton);
	addInput(theme::input<TinyPJ301MPort>(theme::colRow(sceneTrim, morph), module, PILOT::MORPH_INPUT));
	button(theme::colRow(sceneTrim, theme::specificCorner(0, 96, theme::kCKD6Size)), PILOT::GOTOP_PARAM);
	button(theme::colRow(sceneTrim, theme::specificCorner(0, 155, theme::kCKD6Size)), PILOT::GOBOTTOM_PARAM);
	addInput(theme::input<TinyPJ301MPort>(theme::specificCorner(104, 121, theme::kTinyJackSize), module, PILOT::GOTOP_INPUT));
	addInput(theme::input<TinyPJ301MPort>(theme::specificCorner(104, 143, theme::kTinyJackSize), module, PILOT::GOBOTTOM_INPUT));

	// ---- Right: the 16 knobs at their old corners in the white control area (KNOB0, KNOB_PITCH in pilot.py), each with
	// its masking tape above (hidden unless shown from the menu), its type button and its voltage button 20 px under it
	const float kKnobPitch = 57.f;
	const float kVoltageBelow = 20.f;
	const float kTapeAbove = 18.f;   // the tape's top above the knob's
	const theme::Pos knob0 = theme::specificCorner(208, 35, theme::kLargeKnobSize);   // BidooLargeColoredKnob (12.7 mm)
	const theme::Pos type0 = theme::specificCorner(246, 35, kLEDButtonSize);
	// the outputs: 27 px apart, one 25 px box each; the last column's box on the white area's right edge (0.35 px left),
	// the last row at its old height, 350 (below the theme's 342, to keep the matrix 7 px clear of the control area).
	// OUT_PITCH, OUT_LAST_ROW, OUT_X0 in pilot.py
	const float kOutPitch = 27.f;
	const float kOutBox = 25.f;
	const float kOutLastRow = 350.f;
	const theme::Pos out0 = theme::specific(w - theme::kSideMargin - kOutBox * 0.5f - 3.f * kOutPitch, kOutLastRow - 3.f * kOutPitch);
	for (int i = 0; i < 16; i++) {
		const int c = i % 4, r = i / 4;
		const theme::Pos knob = knob0.offset(kKnobPitch * c, kKnobPitch * r);
		controls[i] = theme::param<PILOTColoredKnob>(knob, module, PILOT::CONTROLS_PARAMS + i);
		if (module) {
			dynamic_cast<PILOTColoredKnob*>(controls[i])->blink=&module->controlFocused[i];
		}
		addParam(controls[i]);

		labels[i] = theme::atCorner(createWidget<PILOTLabelDisplay>(Vec()), knob.left, knob.top - kTapeAbove);
		labels[i]->box.size = Vec(55, 20);
		labels[i]->module = module ? module : NULL;
		labels[i]->id=i;
		addChild(labels[i]);
		labels[i]->setVisible(module ? module->showTapes : false);

		const theme::Pos type = type0.offset(kKnobPitch * c, kKnobPitch * r);
		ledButton(type, PILOT::TYPE_PARAMS + i, PILOT::TYPE_LIGHTS + i*3);
		ledButton(type.down(kVoltageBelow), PILOT::VOLTAGE_PARAMS + i, PILOT::VOLTAGE_LIGHTS + i*3);
		addOutput(theme::output<PJ301MPort>(out0.offset(kOutPitch * c, kOutPitch * r), module, PILOT::CV_OUTPUTS + i));
	}
}

struct PILOTItem : MenuItem {
	PILOT *module;
	void onAction(const event::Action &e) override {
			module->randomizeTargetScene();
	}
};

struct PILOTCopyBankItem : MenuItem {
	PILOT *module;
	void onAction(const event::Action &e) override {
		module->copyBankId = module->bank;
	}
};

struct PILOTPasteBankItem : MenuItem {
	PILOT *module;
	void onAction(const event::Action &e) override {
		module->copyBankArmed = true;
	}
};

struct PILOTShowTapesItem : MenuItem {
	PILOT *module;
	PILOTWidget *pWidget;
	void onAction(const event::Action &e) override {
		module->showTapes = !module->showTapes;
		for (int i = 0; i < 16; i++) {
			pWidget->labels[i]->setVisible(module->showTapes);
		}
	}
};

struct PILOTToggleOverrideTypesItem : MenuItem {
	PILOT *module;
	void onAction(const event::Action &e) override {
		for (int i = 0; i < 16; i++) {
			module->controlOverrideTypes[i] = !module->controlOverrideTypes[i];
		}
	}
};

struct PilotlabelTextField : TextField {
	PILOT *module;
	int id;

	PilotlabelTextField()
	{
		this->box.pos.x = 60;
		this->box.size.x = 159;
		this->multiline = false;
	}

	void onChange(const event::Change& e) override {
		module->labels[id] = text;
	};

};

void PILOTWidget::appendContextMenu(ui::Menu *menu) {
	BidooWidget::appendContextMenu(menu);
	PILOT *module = dynamic_cast<PILOT*>(this->module);
	assert(module);
	menu->addChild(new MenuSeparator());
	menu->addChild(construct<PILOTItem>(&MenuItem::text, "Randomize top scene only", &PILOTItem::module, module));
	menu->addChild(construct<PILOTCopyBankItem>(&MenuItem::text, "Copy bank (over+C)", &PILOTCopyBankItem::module, module));
	menu->addChild(construct<PILOTPasteBankItem>(&MenuItem::text, "Paste bank (over+V)", &PILOTPasteBankItem::module, module));
	menu->addChild(construct<PILOTShowTapesItem>(&MenuItem::text, "Show/Hide masking tape", &PILOTShowTapesItem::module, module, &PILOTShowTapesItem::pWidget, this));
	menu->addChild(construct<PILOTToggleOverrideTypesItem>(&MenuItem::text, "Toggle override types", &PILOTToggleOverrideTypesItem::module, module));

	for (int i = 0; i < 16; i++) {
		auto holder = new rack::Widget;
		holder->box.size.x = 220;
		holder->box.size.y = 20;

		auto lab = new rack::Label;
		lab->text = "Label " + std::to_string(i + 1) +  " : ";
		lab->box.size = 65;
		holder->addChild(lab);

		auto textfield = new PilotlabelTextField();
		textfield->module = module;
		textfield->id = i;
		textfield->text = module->labels[i];
		holder->addChild(textfield);

		menu->addChild(holder);
	}
}

void PILOTWidget::step() {
	BidooWidget::step();
}

void PILOTWidget::draw(const DrawArgs& args) {
	ModuleWidget::draw(args);
}

Model *modelPILOT = createModel<PILOT, PILOTWidget>("PILOT");
