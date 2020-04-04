#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/ringbuffer.hpp"
#include "dep/freeverb/revmodel.hpp"
#include "dep/filters/pitchshifter.h"
#include "dsp/digital.hpp"

#define REIBUFF_SIZE 1024

using namespace std;

struct REI : Module {
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
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
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
		configParam(SIZE_PARAM, 0.f, 1.f, .5f, "Room Size", "m", 0.f, 100.f);
		configParam(DAMP_PARAM, 0.f, 1.f, .5f, "Damping", "%", 0.f, 70.f);
		configParam(WIDTH_PARAM, 0.f, 1.f, .5f, "Width", "%", 0.f, 100.f);
		configParam(DRY_PARAM, 0.f, 1.f, .5f, "Dry", "%", 0.f, 100.f);
		configParam(WET_PARAM, 0.f, 1.f, .5f, "Wet", "%", 0.f, 100.f);
		configParam(SHIMM_PARAM, 0.f, 1.f, 0.f, "Feedback", "%", 0.f, 100.f);
		configParam(SHIMMPITCH_PARAM, .5f, 4.f, 2.f, "Pitch shift", " Hz"); //Could use sdt::pow TODO find out what pitch it is, to make calibration
		configParam(FREEZE_PARAM, 0.f, 10.f, 0.f, "Freeze");//non momentary might work better? if using on/off
		configParam(CLIPPING_PARAM, 0.f, 1.f, 1.f, "Clipping");//Unsure which is on/off

		pShifter = new PitchShifter();
	}

	~REI() {
		delete pShifter;
	}

	void process(const ProcessArgs &args) override {
		if (first) {
			pShifter->init(REIBUFF_SIZE, 8, args.sampleRate);
			first = false;
		}

		float outL = 0.0f, outR = 0.0f;
		float wOutL = 0.0f, wOutR = 0.0f;
		float inL = 0.0f, inR = 0.0f;

		revprocessor.setdamp(clamp(params[DAMP_PARAM].getValue() + inputs[DAMP_INPUT].getVoltage(), 0.0f, 1.0f));
		revprocessor.setroomsize(clamp(params[SIZE_PARAM].getValue() + inputs[SIZE_INPUT].getVoltage(), 0.0f, 1.0f));
		revprocessor.setwet(clamp(params[WET_PARAM].getValue(), 0.0f, 1.0f));
		revprocessor.setdry(clamp(params[DRY_PARAM].getValue(), 0.0f, 1.0f));
		revprocessor.setwidth(clamp(params[WIDTH_PARAM].getValue() + inputs[WIDTH_INPUT].getVoltage(), 0.0f, 1.0f));

		if (freezeTrigger.process(params[FREEZE_PARAM].getValue() + inputs[FREEZE_INPUT].getVoltage())) freeze = !freeze;

		revprocessor.setmode(freeze ? 1.0 : 0.0);

		inL = inputs[IN_L_INPUT].getVoltage();
		inR = inputs[IN_R_INPUT].getVoltage();

		float fact = clamp(params[SHIMM_PARAM].getValue() + rescale(inputs[SHIMM_INPUT].getVoltage(), 0.0f, 10.0f, 0.0f, 1.0f), 0.0f, 1.0f);

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

		in_Buffer.push((outL + outR) / 2.0f);

		if (in_Buffer.full()) {
			pShifter->process(clamp(params[SHIMMPITCH_PARAM].getValue() + inputs[SHIMMPITCH_INPUT].getVoltage(), 0.5f, 4.0f), in_Buffer.startData(), pin_Buffer.endData());
			pin_Buffer.endIncr(in_Buffer.size());
			in_Buffer.clear();
		}

		outputs[OUT_L_OUTPUT].setVoltage(outL);
		outputs[OUT_R_OUTPUT].setVoltage(outR);
	}

};

struct REIWidget : ModuleWidget {
	REIWidget(REI *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/REI.svg")));

		addParam(createParam<BidooBlueKnob>(Vec(13, 45), module, REI::SIZE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 85), module, REI::DAMP_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 125), module, REI::WIDTH_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 165), module, REI::DRY_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(63, 165), module, REI::WET_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 205), module, REI::SHIMM_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(13, 245), module, REI::SHIMMPITCH_PARAM));
		addParam(createParam<BlueCKD6>(Vec(13, 285), module, REI::FREEZE_PARAM));

		addParam(createParam<CKSS>(Vec(75.0f, 15.0f), module, REI::CLIPPING_PARAM));
		//Innies
		addInput(createInput<PJ301MPort>(Vec(65.0f, 47.0f), module, REI::SIZE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 87.0f), module, REI::DAMP_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 127.0f), module, REI::WIDTH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 207.0f), module, REI::SHIMM_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 247.0f), module, REI::SHIMMPITCH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(65.0f, 287.0f), module, REI::FREEZE_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(8.0f, 340.0f), module, REI::IN_L_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(8.0f+22.0f, 340.0f), module, REI::IN_R_INPUT));
		//Outies
		addOutput(createOutput<TinyPJ301MPort>(Vec(60.0f, 340.0f), module, REI::OUT_L_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(60.0f+22.0f, 340.0f), module, REI::OUT_R_OUTPUT));
	}
};

Model *modelREI = createModel<REI, REIWidget>("REI");
