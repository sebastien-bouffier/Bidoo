#pragma once
#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"

// The expander of the two ZouSeq modules: TRAITS names the module it serves
// and its panel. The message layout is the one ZouSeq::process reads.
template <typename TRAITS>
struct ZouSeqExpander : BidooModule {
	using Traits = TRAITS;

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
	dsp::SchmittTrigger fillInput[8];
	dsp::SchmittTrigger forceInput[8];
	dsp::SchmittTrigger killInput[8];
	dsp::SchmittTrigger rotLeftTrigger[8];
	dsp::SchmittTrigger rotRightTrigger[8];
	dsp::SchmittTrigger trspTypeTrigger[8];


	bool forceTrigs[8] = {0};
	bool killTrigs[8] = {0};
	bool fills[8] = {0};
	float dice[8] = {0.0f};
	static constexpr int NUM_PATTERNS = 16;
	int rotLen[NUM_PATTERNS][8] = {{0}};
	int rotShift[NUM_PATTERNS][8] = {{0}};
	float trspType[8] = {1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f,1.0f};
	int currentPattern = 0;
	// lights every 16 samples, as the main module
	dsp::ClockDivider lightDivider;

	ZouSeqExpander() {
		config(NUM_PARAMS, NUM_INPUTS, 0, NUM_LIGHTS);
		lightDivider.setDivision(16);

		for (int i=0; i<NUM_PATTERNS; i++) {
			for (int j=0; j<8;j++) {
				rotLen[i][j] = 16;
				rotShift[i][j] = 1;
			}
		}

		leftExpander.producerMessage = leftMessages[0];
		leftExpander.consumerMessage = leftMessages[1];

		for (int i=0; i<8; i++) {
			std::string t = "Track " + std::to_string(i + 1) + " ";
			configButton(FILL_PARAM+i, t + "fill");
			configButton(FORCE_PARAM+i, t + "force trigs");
			configButton(KILL_PARAM+i, t + "kill trigs");
			configParam(DICE_PARAM+i, -1.0f, 1.0f, 0.0f, t + "dice offset", "%", 0.f, 100.f);
			configParam(ROTSHIFT_PARAM+i, 1.0f, 64.0f, 1.0f, t + "rotation steps")->snapEnabled = true;
			configParam(ROTLEN_PARAM+i, 1.0f, 64.0f, 16.0f, t + "rotation length")->snapEnabled = true;
			configButton(TRSPTYPE_PARAM+i, t + "transpose mode (octaves / semitones)");
			configInput(FILL_INPUT+i, t + "fill gate");
			configInput(FORCE_INPUT+i, t + "force gate");
			configInput(KILL_INPUT+i, t + "kill gate");
			configInput(TRSP_INPUT+i, t + "transpose");
			configInput(DICE_INPUT+i, t + "dice offset");
			configInput(ROTLEFT_INPUT+i, t + "rotate left");
			configInput(ROTRIGHT_INPUT+i, t + "rotate right");
		}

	}

	void onReset(const ResetEvent& e) override {
		for (int i=0; i<NUM_PATTERNS; i++) {
			for (int j=0; j<8;j++) {
				rotLen[i][j] = 16;
				rotShift[i][j] = 1;
			}
		}
		for (int i=0; i<8; i++) {
			params[ROTLEN_PARAM+i].setValue(16);
			params[ROTSHIFT_PARAM+i].setValue(1);
			fills[i] = forceTrigs[i] = killTrigs[i] = false;
			trspType[i] = 1.0f;
		}

		Module::onReset(e);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "currentPattern", json_integer(currentPattern));
		for (size_t i = 0; i<8; i++)
			json_object_set_new(rootJ, ("trspType" + to_string(i)).c_str(), json_real(trspType[i]));
		// Only what differs from the defaults.
		for (size_t i = 0; i<NUM_PATTERNS; i++) {
			json_t *patternJ = json_object();
			for (size_t j = 0; j < 8; j++) {
				if (rotShift[i][j] == 1 && rotLen[i][j] == 16)
					continue;
				json_t *trackJ = json_object();
				if (rotShift[i][j] != 1)
					json_object_set_new(trackJ, "rotShift", json_integer(rotShift[i][j]));
				if (rotLen[i][j] != 16)
					json_object_set_new(trackJ, "rotLen", json_integer(rotLen[i][j]));
				json_object_set_new(patternJ, ("track" + to_string(j)).c_str() , trackJ);
			}
			if (json_object_size(patternJ) > 0)
				json_object_set_new(rootJ, ("pattern" + to_string(i)).c_str(), patternJ);
			else
				json_decref(patternJ);
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *currentPatternJ = json_object_get(rootJ, "currentPattern");
		if (currentPatternJ)
			currentPattern = clamp((int)json_integer_value(currentPatternJ), 0, NUM_PATTERNS - 1);

		for (size_t i=0; i<8;i++) {
			json_t *trspTypeJ = json_object_get(rootJ, ("trspType" + to_string(i)).c_str());
			if (trspTypeJ)
				trspType[i] = json_number_value(trspTypeJ) == 12.0 ? 12.0f : 1.0f;
		}
		for (size_t i=0; i<NUM_PATTERNS;i++) {
			for (size_t j=0; j<8;j++) {
				rotLen[i][j] = 16;
				rotShift[i][j] = 1;
			}
			json_t *patternJ = json_object_get(rootJ, ("pattern" + to_string(i)).c_str());
			if (patternJ){
				for(size_t j=0; j<8;j++) {
					json_t *trackJ = json_object_get(patternJ, ("track" + to_string(j)).c_str());
					if (trackJ) {
						json_t *rotLenJ = json_object_get(trackJ, "rotLen");
						if (rotLenJ)
							rotLen[i][j] = clamp((int)json_integer_value(rotLenJ), 1, 64);
						json_t *rotShiftJ = json_object_get(trackJ, "rotShift");
						if (rotShiftJ)
							rotShift[i][j] = clamp((int)json_integer_value(rotShiftJ), 1, 64);
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

	// With a cable the input sets the state (high above 1 V, low under 0.1 V);
	// without one the button toggles it. Returns true when the state must flip.
	bool gateState(dsp::SchmittTrigger &input, dsp::SchmittTrigger &button, int inputId, int paramId, bool state) {
		bool pressed = button.process(params[paramId].getValue());
		if (inputs[inputId].isConnected()) {
			input.process(inputs[inputId].getVoltage(), 0.1f, 1.f);
			return input.isHigh() != state;
		}
		return pressed;
	}

	void process(const ProcessArgs &args) override {
		if (leftExpander.module && (leftExpander.module->model == TRAITS::parentModel())) {
			float *messagesFromZou = (float*)leftExpander.consumerMessage;
			if (messagesFromZou[0] != currentPattern) {
				currentPattern = messagesFromZou[0];
				updateParams();
			}
			leftExpander.module->rightExpander.messageFlipRequested = true;

			updateValues();

			float *messagesToZou = (float*)leftExpander.module->rightExpander.producerMessage;
			const bool lightsNow = lightDivider.process();
			for (int i=0; i<8; i++) {
				if (gateState(fillInput[i], fillTrigger[i], FILL_INPUT+i, FILL_PARAM+i, fills[i]))
					fills[i] = !fills[i];
				messagesToZou[i]=fills[i];
				if (lightsNow)
					lights[FILL_LIGHT+i*3+1].setBrightness(fills[i]?1.0f:0.0f);

				if (gateState(forceInput[i], forceTrigger[i], FORCE_INPUT+i, FORCE_PARAM+i, forceTrigs[i]))
					forceTrigs[i] = !forceTrigs[i];
				messagesToZou[i+8]=forceTrigs[i];
				if (lightsNow)
					lights[FORCE_LIGHT+i*3+1].setBrightness(forceTrigs[i]?1.0f:0.0f);

				if (gateState(killInput[i], killTrigger[i], KILL_INPUT+i, KILL_PARAM+i, killTrigs[i]))
					killTrigs[i] = !killTrigs[i];
				messagesToZou[i+16]=killTrigs[i];
				if (lightsNow)
					lights[KILL_LIGHT+i*3+1].setBrightness(killTrigs[i]?1.0f:0.0f);

				if (trspTypeTrigger[i].process(params[TRSPTYPE_PARAM+i].getValue())) {
					if (trspType[i] == 12.0f) {
						trspType[i] = 1.0f;
					}
					else {
						trspType[i] = 12.0f;
					}
				}
				if (lightsNow) {
					lights[TRSPTYPE_LIGHT+i*3+1].setBrightness(trspType[i] == 1.0f ? 0.0f : 1.0f);
					lights[TRSPTYPE_LIGHT+i*3+2].setBrightness(trspType[i] == 1.0f ? 1.0f : 0.0f);
				}

				if (inputs[TRSP_INPUT+i].isConnected()) {
					messagesToZou[i+24]=inputs[TRSP_INPUT+i].getVoltage()/trspType[i];
				}
				else {
					messagesToZou[i+24]=0.0f;
				}

				messagesToZou[i+32]=clamp(rescale(inputs[DICE_INPUT+i].getVoltage(),-10.0f,10.0f,-1.0f,1.0f) + params[DICE_PARAM+i].getValue(),-1.0f,1.0f);

				messagesToZou[i+40]=rotLeftTrigger[i].process(inputs[ROTLEFT_INPUT+i].getVoltage()) ? params[ROTSHIFT_PARAM+i].getValue() : 0.0f;

				messagesToZou[i+48]=rotRightTrigger[i].process(inputs[ROTRIGHT_INPUT+i].getVoltage()) ? params[ROTSHIFT_PARAM+i].getValue() : 0.0f;

				messagesToZou[i+56]=params[ROTLEN_PARAM+i].getValue();
			}
		}
		else {
			// not beside its sequencer: the rotation knobs still go into the current pattern, so a patch saved
			// with the expander apart keeps them
			updateValues();
		}
	}

};

template <typename E>
struct ZouSeqExpanderWidget : BidooWidget {


	ZouSeqExpanderWidget(E *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, E::Traits::PANEL));
		// The expander's widget: eight rows of fill, force and kill buttons over their inputs, the dice knob and input,
		// the transpose button over its input, the rotation trimpots and inputs. The panel is tools/panels/zouseq_panel.py
		// (expander), which draws the boxes and labels at these places (its names for them in brackets).

		theme::addScrews(this);

		// The rows, 38 px apart (ROW_PITCH). In a row: the buttons (BUTTON_Y0, 0.8 px higher than before), the inputs
		// under them at their old height (JACK_Y0), the rest on the middle line (MID_Y0, the inputs' old height; the
		// knob and trimpots were 0.3 and 0.1 px lower)
		const float kRowPitch = 38.0f;
		const theme::Pos button = theme::specific(0.0f, 64.5f);
		const theme::Pos mid = theme::specificCorner(0.0f, 69.0f, theme::kTinyJackSize);
		// The columns: FILL 1.5 px left and FORCE 1 px right of before, to leave the track numbers room between their
		// buttons (FILL_X, FORCE_X); KILL, DICE, TRSP and the rotation at their old corners
		const theme::Pos fill = theme::specificCorner(6.0f, 76.0f, theme::kTinyJackSize);
		const theme::Pos force = theme::specificCorner(32.0f, 76.0f, theme::kTinyJackSize);
		const theme::Pos kill = theme::specificCorner(54.5f, 76.0f, theme::kTinyJackSize);
		const theme::Pos trsp = theme::specificCorner(125.0f, 76.0f, theme::kTinyJackSize);
		const theme::Size kSmallKnobSize = {mm2px(8.0f), mm2px(8.0f)};   // BidooSmallBlueKnob
		const theme::Pos diceKnob = theme::colRow(theme::specificCorner(73.5f, 65.0f, kSmallKnobSize), mid);
		const theme::Pos dice = theme::colRow(theme::specificCorner(101.5f, 69.0f, theme::kTinyJackSize), mid);
		const theme::Pos rotShift = theme::colRow(theme::specificCorner(151.0f, 67.0f, theme::kTrimpotSize), mid);
		const theme::Pos rotLen = theme::colRow(theme::specificCorner(179.2f, 67.0f, theme::kTrimpotSize), mid);
		const theme::Pos rotLeft = theme::colRow(theme::specificCorner(207.25f, 69.0f, theme::kTinyJackSize), mid);
		const theme::Pos rotRight = theme::colRow(theme::specificCorner(230.75f, 69.0f, theme::kTinyJackSize), mid);

		// FILL, FORCE and KILL: BidooRoundButtons, momentary as the old bezels, lit by their state (the old green light).
		// 16 px (BUTTON_SIZE): at 20 a button and the box of the input under it don't fit in a 38 px row
		const float kButtonSize = 16.0f;
		auto stateButton = [&](theme::Pos p, int paramId, int lightId) {
			BidooRoundButton *b = createParam<BidooRoundButton>(Vec(), module, paramId);
			b->box.size = Vec(kButtonSize, kButtonSize);
			b->momentary = true;
			b->litBy = [=]() { return module && module->lights[lightId].getBrightness() > 0.5f; };
			addParam(theme::place(b, p));
		};

		for (int i = 0; i < 8; i++) {
			const float dy = kRowPitch * i;
			stateButton(theme::colRow(fill, button).down(dy), E::FILL_PARAM + i, E::FILL_LIGHT + i * 3 + 1);
			addInput(theme::input<TinyPJ301MPort>(fill.down(dy), module, E::FILL_INPUT + i));
			stateButton(theme::colRow(force, button).down(dy), E::FORCE_PARAM + i, E::FORCE_LIGHT + i * 3 + 1);
			addInput(theme::input<TinyPJ301MPort>(force.down(dy), module, E::FORCE_INPUT + i));
			stateButton(theme::colRow(kill, button).down(dy), E::KILL_PARAM + i, E::KILL_LIGHT + i * 3 + 1);
			addInput(theme::input<TinyPJ301MPort>(kill.down(dy), module, E::KILL_INPUT + i));

			addParam(theme::param<BidooSmallBlueKnob>(diceKnob.down(dy), module, E::DICE_PARAM + i));
			addInput(theme::input<TinyPJ301MPort>(dice.down(dy), module, E::DICE_INPUT + i));

			// the transpose mode keeps its bezel and its two colours (blue: semitones, green: octaves), 0.3 px left onto
			// its input's column (TRSP_X)
			addParam(theme::place(createLightParam<SmallLEDLightBezel<RedGreenBlueLight>>(Vec(), module, E::TRSPTYPE_PARAM + i, E::TRSPTYPE_LIGHT + i * 3), theme::colRow(trsp, button).down(dy)));
			addInput(theme::input<TinyPJ301MPort>(trsp.down(dy), module, E::TRSP_INPUT + i));

			addParam(theme::param<BidooBlueSnapTrimpot>(rotShift.down(dy), module, E::ROTSHIFT_PARAM + i));
			addParam(theme::param<BidooBlueSnapTrimpot>(rotLen.down(dy), module, E::ROTLEN_PARAM + i));
			addInput(theme::input<TinyPJ301MPort>(rotLeft.down(dy), module, E::ROTLEFT_INPUT + i));
			addInput(theme::input<TinyPJ301MPort>(rotRight.down(dy), module, E::ROTRIGHT_INPUT + i));
		}
	}

};
