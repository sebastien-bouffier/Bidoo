#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/ringbuffer.hpp"
#include "dep/freeverb/revmodel.hpp"
#include "dep/filters/pitchshifter.h"
#include "dsp/digital.hpp"

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

	dsp::DoubleRingBuffer<float, REIBUFF_SIZE> in_Buffer;
	dsp::DoubleRingBuffer<float, 2 * REIBUFF_SIZE> pin_Buffer;
	revmodel revprocessor;
	dsp::SchmittTrigger freezeTrigger;
	bool freeze = false;
	PitchShifter *pShifter;
	int delay = 0;
	bool first = true;

	REI() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SIZE_PARAM, 0.f, 2.f, .5f, "Room Size", "m", 0.f, 100.f);
		configParam(DAMP_PARAM, 0.f, 1.f, .5f, "Damping", "%", 0.f, 70.f);
		configParam(WIDTH_PARAM, 0.f, 1.f, .5f, "Width", "%", 0.f, 100.f);
		configParam(DRY_PARAM, 0.f, 1.f, .5f, "Dry", "%", 0.f, 100.f);
		configParam(WET_PARAM, 0.f, 1.f, .5f, "Wet", "%", 0.f, 100.f);
		configParam(SHIMM_PARAM, 0.f, 1.f, 0.f, "Feedback", "%", 0.f, 100.f);
		configParam(SHIMMPITCH_PARAM, .5f, 4.f, 1.f, "Pitch shift", "x");
		configParam(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze");
		configSwitch(CLIPPING_PARAM, 0, 1, 0, "Clipping", {"Tanh", "Digi"});

		pShifter = new PitchShifter();
	}

	~REI() {
		delete pShifter;
	}

	void process(const ProcessArgs &args) override {
		if (first) {
			pShifter->init(REIBUFF_SIZE, 4, args.sampleRate);
			first = false;
		}

		float outL = 0.0f, outR = 0.0f;
		float wOutL = 0.0f, wOutR = 0.0f;
		float inL = 0.0f, inR = 0.0f;

		revprocessor.setdamp(clamp(params[DAMP_PARAM].getValue() + inputs[DAMP_INPUT].getVoltage(), 0.0f, 1.0f));
		revprocessor.setroomsize(clamp(params[SIZE_PARAM].getValue() + inputs[SIZE_INPUT].getVoltage(), 0.0f, 1.0f));
		revprocessor.setwet(clamp(params[WET_PARAM].getValue()+rescale(inputs[WET_INPUT].getVoltage(),0.0f,10.0f,0.0f,1.0f), 0.0f, 1.0f));
		revprocessor.setdry(clamp(params[DRY_PARAM].getValue()+rescale(inputs[DRY_INPUT].getVoltage(),0.0f,10.0f,0.0f,1.0f), 0.0f, 1.0f));
		revprocessor.setwidth(clamp(params[WIDTH_PARAM].getValue() + inputs[WIDTH_INPUT].getVoltage(), 0.0f, 1.0f));

		if (freezeTrigger.process(params[FREEZE_PARAM].getValue() + inputs[FREEZE_INPUT].getVoltage())) freeze = !freeze;
		lights[FREEZE_LIGHT].setBrightness(freeze ? 10 : 0);

		revprocessor.setmode(freeze ? 1.0 : 0.0);

		inL = inputs[IN_L_INPUT].getVoltage()*0.1f;
		inR = inputs[IN_R_INPUT].getVoltage()*0.1f;

		float fact = clamp(params[SHIMM_PARAM].getValue() + rescale(inputs[SHIMM_INPUT].getVoltage(), 0.0f, 10.0f, 0.0f, 1.0f), 0.0f, 1.0f)*3.0f;

		if (pin_Buffer.size() > REIBUFF_SIZE) {
			revprocessor.process(inL, inR, fact*(*pin_Buffer.startData()), outL, outR, wOutL, wOutR);
			pin_Buffer.startIncr(1);
		} else {
			revprocessor.process(inL, inR, 0.0f, outL, outR, wOutL, wOutR);
		}

		if (params[CLIPPING_PARAM].getValue() == 1.0f) {
			outL = clamp(outL, -7.0f, 7.0f);
			outR = clamp(outR, -7.0f, 7.0f);
		} else {
			outL = tanh(outL / 5.0f)*7.0f;
			outR = tanh(outR / 5.0f)*7.0f;
		}

		in_Buffer.push((outL + outR)*0.05f);

		if (in_Buffer.full()) {
			pShifter->process(clamp(params[SHIMMPITCH_PARAM].getValue() + inputs[SHIMMPITCH_INPUT].getVoltage(), 0.5f, 4.0f), in_Buffer.startData(), pin_Buffer.endData());
			pin_Buffer.endIncr(in_Buffer.size());
			in_Buffer.clear();
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
