#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dep/freeverb/revmodel.hpp"
#include "dep/filters/pitchshifter.h"
#include "dsp/digital.hpp"
#include "Svf.hpp"

#define REIBUFF_SIZE 512

using namespace std;

struct REI : BidooModule {
	enum ParamIds {
		SIZE_PARAM,
		DAMP_PARAM,
		FREEZE_PARAM,
		WIDTH_PARAM,
		DRY_PARAM,
		WET_PARAM,
		SHIMM_PARAM,
		SHIMMPITCH_PARAM,
		CLIPPING_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_L_INPUT,
		IN_R_INPUT,
		SIZE_INPUT,
		DAMP_INPUT,
		FREEZE_INPUT,
		WIDTH_INPUT,
		SHIMM_INPUT,
		SHIMMPITCH_INPUT,
		DRY_INPUT,
		WET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		FREEZE_LIGHT,
		NUM_LIGHTS
	};

	revmodel revprocessor;
	dsp::SchmittTrigger freezeTrigger;
	bool freeze = false;
	PitchShifter *pShifter = nullptr;
	float shimmer = 0.0f;
	// freeverb values last set, so its coefficients are worked out only on a change
	float applied[6] = {-1.f, -1.f, -1.f, -1.f, -1.f, -1.f};

	REI() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SIZE_PARAM, 0.f, 1.f, .5f, "Room size", "%", 0.f, 100.f);
		configParam(DAMP_PARAM, 0.f, 1.f, .5f, "Damping", "%", 0.f, 100.f);
		configParam(WIDTH_PARAM, 0.f, 1.f, .5f, "Width", "%", 0.f, 100.f);
		configParam(DRY_PARAM, 0.f, 1.f, .5f, "Dry", "%", 0.f, 100.f);
		configParam(WET_PARAM, 0.f, 1.f, .5f, "Wet", "%", 0.f, 100.f);
		configParam(SHIMM_PARAM, 0.f, 1.f, 0.f, "Shimmer feedback", "%", 0.f, 100.f);
		configParam(SHIMMPITCH_PARAM, -12.f, 24.f, 12.f, "Shimmer pitch", " semitones");
		paramQuantities[SHIMMPITCH_PARAM]->snapEnabled = true;
		configButton(FREEZE_PARAM, "Freeze");
		configSwitch(CLIPPING_PARAM, 0, 1, 0, "Output limit", {"Soft (under 10 V)", "Hard clip at 10 V"});
		configInput(IN_L_INPUT, "Left");
		configInput(IN_R_INPUT, "Right (follows left when unpatched)");
		configInput(SIZE_INPUT, "Room size CV (0-10 V: full range)");
		configInput(DAMP_INPUT, "Damping CV (0-10 V: full range)");
		configInput(FREEZE_INPUT, "Freeze toggle trigger");
		configInput(WIDTH_INPUT, "Width CV (0-10 V: full range)");
		configInput(SHIMM_INPUT, "Shimmer feedback CV (0-10 V: full range)");
		configInput(SHIMMPITCH_INPUT, "Shimmer pitch CV (1V/oct)");
		configInput(DRY_INPUT, "Dry CV (0-10 V: full range)");
		configInput(WET_INPUT, "Wet CV (0-10 V: full range)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
	}

	~REI() {
		delete pShifter;
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		// comb and allpass lengths scaled from 44.1 kHz
		revprocessor.setsamplerate(e.sampleRate);
		for (float &a : applied)
			a = -1.f;
		delete pShifter;
		pShifter = new PitchShifter();
		pShifter->init(REIBUFF_SIZE, 4, e.sampleRate);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "freeze", json_boolean(freeze));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		freeze = json_boolean_value(json_object_get(rootJ, "freeze"));
	}

	void process(const ProcessArgs &args) override {
		if (freezeTrigger.process(params[FREEZE_PARAM].getValue() + inputs[FREEZE_INPUT].getVoltage()))
			freeze = !freeze;
		lights[FREEZE_LIGHT].setBrightness(freeze ? 1.f : 0.f);

		float values[6] = {
			clamp(params[DAMP_PARAM].getValue() + inputs[DAMP_INPUT].getVoltage() * 0.1f, 0.0f, 1.0f),
			clamp(params[SIZE_PARAM].getValue() + inputs[SIZE_INPUT].getVoltage() * 0.1f, 0.0f, 1.0f),
			clamp(params[WET_PARAM].getValue() + inputs[WET_INPUT].getVoltage() * 0.1f, 0.0f, 1.0f),
			clamp(params[DRY_PARAM].getValue() + inputs[DRY_INPUT].getVoltage() * 0.1f, 0.0f, 1.0f),
			clamp(params[WIDTH_PARAM].getValue() + inputs[WIDTH_INPUT].getVoltage() * 0.1f, 0.0f, 1.0f),
			freeze ? 1.0f : 0.0f
		};
		for (int k = 0; k < 6; k++) {
			if (values[k] == applied[k])
				continue;
			applied[k] = values[k];
			switch (k) {
				case 0: revprocessor.setdamp(values[k]); break;
				case 1: revprocessor.setroomsize(values[k]); break;
				case 2: revprocessor.setwet(values[k]); break;
				case 3: revprocessor.setdry(values[k]); break;
				case 4: revprocessor.setwidth(values[k]); break;
				case 5: revprocessor.setmode(values[k]); break;
			}
		}

		float inL = inputs[IN_L_INPUT].getVoltage() * 0.1f;
		float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() * 0.1f : inL;
		float fact = clamp(params[SHIMM_PARAM].getValue() + inputs[SHIMM_INPUT].getVoltage() * 0.1f, 0.0f, 1.0f) * 3.0f;

		float outL = 0.0f, outR = 0.0f, wOutL = 0.0f, wOutR = 0.0f;
		revprocessor.process(inL, inR, fact * shimmer, outL, outR, wOutL, wOutR);

		// shimmer: the output pitch shifted back into the reverb, the FFT work
		// spread over 128-sample hops
		if (pShifter) {
			float semis = params[SHIMMPITCH_PARAM].getValue() + inputs[SHIMMPITCH_INPUT].getVoltage() * 12.f;
			float ratio = clamp(dsp::exp2_taylor5(semis / 12.f), 0.25f, 4.0f);
			shimmer = pShifter->step(ratio, (std::tanh(outL / 5.0f) + std::tanh(outR / 5.0f)) * 0.35f);
		}

		// x5: 100% dry is unity, 100% wet about as loud on broadband material
		outL *= 5.0f;
		outR *= 5.0f;
		if (params[CLIPPING_PARAM].getValue() > 0.5f) {
			outL = clamp(outL, -10.0f, 10.0f);
			outR = clamp(outR, -10.0f, 10.0f);
		}
		else {
			outL = bidooSoftLimit(outL);
			outR = bidooSoftLimit(outR);
		}
		outputs[OUT_L_OUTPUT].setVoltage(outL);
		outputs[OUT_R_OUTPUT].setVoltage(outR);
	}

};

template <typename BASE>
struct REILight : BASE {
	REILight() {
		this->box.size = mm2px(Vec(6.f, 6.f));
	}
};

struct REIWidget : BidooWidget {
	REIWidget(REI *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/REI.svg"));

		const float yAnchor = 38.0f;
		const float inputsOffset = 32.0f;
		const float groupsOffset = 64.0f;
		const float xInputsAnchor = 33.0f;
		const float xInputsOffset = 24.0f;

		addParam(createParam<BidooBlueKnob>(Vec(13, yAnchor), module, REI::SIZE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(63, yAnchor), module, REI::DAMP_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor, yAnchor+inputsOffset), module, REI::SIZE_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor+xInputsOffset, yAnchor+inputsOffset), module, REI::DAMP_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(13, yAnchor+groupsOffset), module, REI::WIDTH_PARAM));
		addParam(createParam<LEDBezel>(Vec(67, yAnchor+groupsOffset+4), module, REI::FREEZE_PARAM));
		addChild(createLight<REILight<BlueLight>>(Vec(68.8f, yAnchor+groupsOffset+5.8f), module, REI::FREEZE_LIGHT));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor, yAnchor+groupsOffset+inputsOffset), module, REI::WIDTH_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor+xInputsOffset, yAnchor+groupsOffset+inputsOffset), module, REI::FREEZE_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(13, yAnchor+2*groupsOffset), module, REI::SHIMM_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(63, yAnchor+2*groupsOffset), module, REI::SHIMMPITCH_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor, yAnchor+2*groupsOffset+inputsOffset), module, REI::SHIMM_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor+xInputsOffset, yAnchor+2*groupsOffset+inputsOffset), module, REI::SHIMMPITCH_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(13, yAnchor+3*groupsOffset), module, REI::DRY_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(63, yAnchor+3*groupsOffset), module, REI::WET_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor, yAnchor+3*groupsOffset+inputsOffset), module, REI::DRY_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(xInputsAnchor+xInputsOffset, yAnchor+3*groupsOffset+inputsOffset), module, REI::WET_INPUT));

		addParam(createParam<CKSS>(Vec(21.0f, 289.0f), module, REI::CLIPPING_PARAM));

		addInput(createInput<TinyPJ301MPort>(Vec(8.0f, 340.0f), module, REI::IN_L_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(8.0f+22.0f, 340.0f), module, REI::IN_R_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(60.0f, 340.0f), module, REI::OUT_L_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(60.0f+22.0f, 340.0f), module, REI::OUT_R_OUTPUT));
	}
};

Model *modelREI = createModel<REI, REIWidget>("REI");
