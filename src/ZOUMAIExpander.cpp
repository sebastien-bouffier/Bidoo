#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"

struct ZOUMAIExpander : BidooModule {
	enum ParamIds {
		FILL_PARAM,
		FORCE_PARAM = FILL_PARAM + 8,
		KILL_PARAM = FORCE_PARAM + 8,
		DICE_PARAM = KILL_PARAM + 8,
		ROTSHIFT_PARAM = DICE_PARAM + 8,
		ROTLEN_PARAM = ROTSHIFT_PARAM + 8,
		TRSPTYPE_PARAM = ROTLEN_PARAM + 8,
		NUM_PARAMS = TRSPTYPE_PARAM + 8
	};

	enum InputIds {
		FILL_INPUT,
		FORCE_INPUT = FILL_INPUT + 8,
		KILL_INPUT = FORCE_INPUT + 8,
		TRSP_INPUT = KILL_INPUT + 8,
		DICE_INPUT = TRSP_INPUT + 8,
		ROTLEFT_INPUT = DICE_INPUT + 8,
		ROTRIGHT_INPUT = ROTLEFT_INPUT + 8,
		NUM_INPUTS = ROTRIGHT_INPUT + 8
	};

	enum LightIds {
		FILL_LIGHT,
		FORCE_LIGHT = FILL_LIGHT + 8*3,
		KILL_LIGHT = FORCE_LIGHT + 8*3,
		TRSPTYPE_LIGHT = KILL_LIGHT + 8*3,
		NUM_LIGHTS = TRSPTYPE_LIGHT + 8*3
	};

	float leftMessages[2][1] = {};

	dsp::SchmittTrigger fillTrigger[8];
	dsp::SchmittTrigger forceTrigger[8];
	dsp::SchmittTrigger killTrigger[8];
	dsp::SchmittTrigger rotLeftTrigger[8];
	dsp::SchmittTrigger rotRightTrigger[8];
	dsp::SchmittTrigger trspTypeTrigger[8];


	bool forceTrigs[8] = {0};
	bool killTrigs[8] = {0};
	bool fills[8] = {0};
	float dice[8] = {0.0f};
	int rotLen[8][8] = {{0}};
	int rotShift[8][8] = {{0}};
	float trspType[8] = {1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f};
	int currentPattern = 0;

	ZOUMAIExpander() {
		config(NUM_PARAMS, NUM_INPUTS, 0, NUM_LIGHTS);

		leftExpander.producerMessage = leftMessages[0];
		leftExpander.consumerMessage = leftMessages[1];

		for (int i=0; i<8; i++) {
			configParam(FILL_PARAM+i, 0.0f, 1.0f, 0.0f);
			configParam(FORCE_PARAM+i, 0.0f, 1.0f, 0.0f);
			configParam(KILL_PARAM+i, 0.0f, 1.0f, 0.0f);
			configParam(DICE_PARAM+i, -1.0f, 1.0f, 0.0f);
			configParam(ROTSHIFT_PARAM+i, 1.0f, 64.0f, 1.0f);
			configParam(ROTLEN_PARAM+i, 1.0f, 64.0f, 16.0f);
			configParam(TRSPTYPE_PARAM+i, 0.0f, 1.0f, 0.0f);
		}

	}

	void onReset(const ResetEvent& e) override {
		for (int i=0; i<8; i++) {
			for (int j=0; j<8;j++) {
				rotLen[i][j] = 16;
				rotShift[i][j] = 1;
			}
			params[ROTLEN_PARAM+i].setValue(16);
			params[ROTSHIFT_PARAM+i].setValue(1);
		}

		Module::onReset(e);
	}

	void onRandomize(const RandomizeEvent& e) override {
		for (int i=0; i<8; i++) {
			rotLen[currentPattern][i] = 1+random::uniform()*63.0f;
			rotShift[currentPattern][i] = 1+random::uniform()*63.0f;
			params[ROTLEN_PARAM+i].setValue(rotLen[currentPattern][i]);
			params[ROTSHIFT_PARAM+i].setValue(rotShift[currentPattern][i]);
		}

		Module::onRandomize(e);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "currentPattern", json_integer(currentPattern));
		for (size_t i = 0; i<8; i++) {
			json_object_set_new(rootJ, ("trspType" + to_string(i)).c_str(), json_real(trspType[i]));
			json_t *patternJ = json_object();
			for (size_t j = 0; j < 8; j++) {
				json_t *trackJ = json_object();
				json_object_set_new(trackJ, "rotShift", json_integer(rotShift[i][j]));
				json_object_set_new(trackJ, "rotLen", json_integer(rotLen[i][j]));
				json_object_set_new(patternJ, ("track" + to_string(j)).c_str() , trackJ);
			}
			json_object_set_new(rootJ, ("pattern" + to_string(i)).c_str(), patternJ);
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *currentPatternJ = json_object_get(rootJ, "currentPattern");
		if (currentPatternJ)
			currentPattern = json_integer_value(currentPatternJ);

		for (size_t i=0; i<8;i++) {
			json_t *trspTypeJ = json_object_get(rootJ, ("trspType" + to_string(i)).c_str());
			if (trspTypeJ)
				trspType[i] = json_number_value(trspTypeJ);
			json_t *patternJ = json_object_get(rootJ, ("pattern" + to_string(i)).c_str());
			if (patternJ){
				for(size_t j=0; j<8;j++) {
					json_t *trackJ = json_object_get(patternJ, ("track" + to_string(j)).c_str());
					if (trackJ) {
						json_t *rotLenJ = json_object_get(trackJ, "rotLen");
						if (rotLenJ)
							rotLen[i][j] = json_integer_value(rotLenJ);
						json_t *rotShiftJ = json_object_get(trackJ, "rotShift");
						if (rotShiftJ)
							rotShift[i][j] = json_integer_value(rotShiftJ);
					}
				}
			}
		}
		updateParams();
	}

	void updateParams() {
		for (int i=0; i<8; i++) {
			params[ROTLEN_PARAM+i].setValue(rotLen[currentPattern][i]);
			params[ROTSHIFT_PARAM+i].setValue(rotShift[currentPattern][i]);
		}
	}

	void updateValues() {
		for (int i=0; i<8; i++) {
			rotLen[currentPattern][i] = params[ROTLEN_PARAM+i].getValue();
			rotShift[currentPattern][i] = params[ROTSHIFT_PARAM+i].getValue();
		}
	}

	void process(const ProcessArgs &args) override {
		if (leftExpander.module && (leftExpander.module->model == modelZOUMAI)) {
			float *messagesFromZou = (float*)leftExpander.consumerMessage;
			if (messagesFromZou[0] != currentPattern) {
				currentPattern = messagesFromZou[0];
				updateParams();
			}
			leftExpander.module->rightExpander.messageFlipRequested = true;

			updateValues();

			float *messagesToZou = (float*)leftExpander.module->rightExpander.producerMessage;
			for (int i=0; i<8; i++) {
				if (inputs[FILL_INPUT+i].isConnected()) {
					if (((inputs[FILL_INPUT+i].getVoltage() > 0.0f) && !fills[i]) || ((inputs[FILL_INPUT+i].getVoltage() == 0.0f) && fills[i])) fills[i]=!fills[i];
				}
				if (fillTrigger[i].process(params[FILL_PARAM+i].getValue())) {
					fills[i] = !fills[i];
				}
				messagesToZou[i]=fills[i];
				lights[FILL_LIGHT+i*3+1].setBrightness(fills[i]?1.0f:0.0f);

				if (inputs[FORCE_INPUT+i].isConnected()) {
					if (((inputs[FORCE_INPUT+i].getVoltage() > 0.0f) && !forceTrigs[i]) || ((inputs[FORCE_INPUT+i].getVoltage() == 0.0f) && forceTrigs[i])) forceTrigs[i]=!forceTrigs[i];
				}
				if (forceTrigger[i].process(params[FORCE_PARAM+i].getValue())) {
					forceTrigs[i] = !forceTrigs[i];
				}
				messagesToZou[i+8]=forceTrigs[i];
				lights[FORCE_LIGHT+i*3+1].setBrightness(forceTrigs[i]?1.0f:0.0f);

				if (inputs[KILL_INPUT+i].isConnected()) {
					if (((inputs[KILL_INPUT+i].getVoltage() > 0.0f) && !killTrigs[i]) || ((inputs[KILL_INPUT+i].getVoltage() == 0.0f) && killTrigs[i])) killTrigs[i]=!killTrigs[i];
				}
				if (killTrigger[i].process(params[KILL_PARAM+i].getValue())) {
					killTrigs[i] = !killTrigs[i];
				}
				messagesToZou[i+16]=killTrigs[i];
				lights[KILL_LIGHT+i*3+1].setBrightness(killTrigs[i]?1.0f:0.0f);

				if (trspTypeTrigger[i].process(params[TRSPTYPE_PARAM+i].getValue())) {
					if (trspType[i] == 12.0f) {
						trspType[i] = 1.0f;
					}
					else {
						trspType[i] = 12.0f;
					}
				}
				lights[TRSPTYPE_LIGHT+i*3+1].setBrightness(trspType[i] == 1.0f ? 0.0f : 1.0f);
				lights[TRSPTYPE_LIGHT+i*3+2].setBrightness(trspType[i] == 1.0f ? 1.0f : 0.0f);

				if (inputs[TRSP_INPUT+i].isConnected()) {
					messagesToZou[i+24]=inputs[TRSP_INPUT+i].getVoltage()/trspType[i];
				}
				else {
					messagesToZou[i+24]=0.0f;
				}

				messagesToZou[i+32]=clamp(rescale(inputs[TRSP_INPUT+i].getVoltage(),-10.0f,10.0f,-1.0f,1.0f) + params[DICE_PARAM+i].getValue(),-1.0f,1.0f);

				messagesToZou[i+40]=rotLeftTrigger[i].process(inputs[ROTLEFT_INPUT+i].getVoltage()) ? params[ROTSHIFT_PARAM+i].getValue() : 0.0f;

				messagesToZou[i+48]=rotRightTrigger[i].process(inputs[ROTRIGHT_INPUT+i].getVoltage()) ? params[ROTSHIFT_PARAM+i].getValue() : 0.0f;

				messagesToZou[i+56]=params[ROTLEN_PARAM+i].getValue();
			}
		}
	}

};

struct ZOUMAIExpanderWidget : BidooWidget {


	ZOUMAIExpanderWidget(ZOUMAIExpander *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/ZOUMAIExpander.svg"));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		const float xInputAnchor = 7.5f;
		const float xOffset = 23.50f;
		const float xControlAnchor = 10.0f;
		const float yInputAnchor = 76.0f;
		const float yControlAnchor = 56.0f;
		const float yButtonOffset = 4.0f;
		const float yOffset = 38.0f;


		for (int i = 0; i<8; i++) {
			addInput(createInput<TinyPJ301MPort>(Vec(xInputAnchor, yInputAnchor+i*yOffset), module, ZOUMAIExpander::FILL_INPUT+i));
			addParam(createLightParam<SmallLEDLightBezel<RedGreenBlueLight>>(Vec(xControlAnchor, yControlAnchor+i*yOffset+yButtonOffset), module, ZOUMAIExpander::FILL_PARAM+i, ZOUMAIExpander::FILL_LIGHT+i*3));

			addInput(createInput<TinyPJ301MPort>(Vec(xInputAnchor+xOffset, yInputAnchor+i*yOffset), module, ZOUMAIExpander::FORCE_INPUT+i));
			addParam(createLightParam<SmallLEDLightBezel<RedGreenBlueLight>>(Vec(xControlAnchor+xOffset, yControlAnchor+i*yOffset+yButtonOffset), module, ZOUMAIExpander::FORCE_PARAM+i, ZOUMAIExpander::FORCE_LIGHT+i*3));

			addInput(createInput<TinyPJ301MPort>(Vec(xInputAnchor+2*xOffset, yInputAnchor+i*yOffset), module, ZOUMAIExpander::KILL_INPUT+i));
			addParam(createLightParam<SmallLEDLightBezel<RedGreenBlueLight>>(Vec(xControlAnchor+2*xOffset, yControlAnchor+i*yOffset+yButtonOffset), module, ZOUMAIExpander::KILL_PARAM+i, ZOUMAIExpander::KILL_LIGHT+i*3));

			addInput(createInput<TinyPJ301MPort>(Vec(xInputAnchor+4*xOffset, yInputAnchor+i*yOffset - 7.0f), module, ZOUMAIExpander::DICE_INPUT+i));
			addParam(createParam<BidooSmallBlueKnob>(Vec(xControlAnchor+3*xOffset-7, yControlAnchor+i*yOffset+9), module, ZOUMAIExpander::DICE_PARAM+i));

			addParam(createLightParam<SmallLEDLightBezel<RedGreenBlueLight>>(Vec(xControlAnchor+5*xOffset, yControlAnchor+i*yOffset+yButtonOffset), module, ZOUMAIExpander::TRSPTYPE_PARAM+i, ZOUMAIExpander::TRSPTYPE_LIGHT+i*3));
			addInput(createInput<TinyPJ301MPort>(Vec(xInputAnchor + 5.0f*xOffset, yInputAnchor + i*yOffset), module, ZOUMAIExpander::TRSP_INPUT+i));

			addParam(createParam<BidooBlueSnapTrimpot>(Vec(xControlAnchor+6.0f*xOffset, yControlAnchor+i*yOffset+11.0f), module, ZOUMAIExpander::ROTSHIFT_PARAM+i));
			addParam(createParam<BidooBlueSnapTrimpot>(Vec(xControlAnchor+7.2f*xOffset, yControlAnchor+i*yOffset+11.0f), module, ZOUMAIExpander::ROTLEN_PARAM+i));

			addInput(createInput<TinyPJ301MPort>(Vec(xInputAnchor + 8.5*xOffset, yInputAnchor + i*yOffset-7.0f), module, ZOUMAIExpander::ROTLEFT_INPUT+i));
			addInput(createInput<TinyPJ301MPort>(Vec(xInputAnchor + 9.5*xOffset, yInputAnchor+ i*yOffset-7.0f), module, ZOUMAIExpander::ROTRIGHT_INPUT+i));

		}

	}

};

Model *modelZOUMAIExpander = createModel<ZOUMAIExpander, ZOUMAIExpanderWidget>("ZOUMAI-Expander");
