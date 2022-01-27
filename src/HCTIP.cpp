#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "dsp/ringbuffer.hpp"
#include "dep/filters/pitchshifter.h"

#define BUFF_SIZE 2048

using namespace std;

struct HCTIP : BidooModule {
	enum ParamIds {
		PITCH_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		PITCH_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	dsp::DoubleRingBuffer<float, BUFF_SIZE> in_Buffer;
	dsp::DoubleRingBuffer<float, BUFF_SIZE> out_Buffer;
	PitchShifter *pShifter;
	bool first = true;

	HCTIP() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PITCH_PARAM, 0.5f, 2.0f, 1.0f, "Pitch");
		pShifter = new PitchShifter();
	}

	void process(const ProcessArgs &args) override {
		if (first) {
			pShifter->init(BUFF_SIZE, 8, args.sampleRate);
			first = false;
		}
		in_Buffer.push(inputs[INPUT].getVoltage() / 10.0f);

		if (in_Buffer.full()) {
			pShifter->process(clamp(params[PITCH_PARAM].getValue() + inputs[PITCH_INPUT].getVoltage(), 0.5f, 2.0f), in_Buffer.startData(), out_Buffer.endData());
			out_Buffer.endIncr(BUFF_SIZE);
			in_Buffer.clear();
		}

		if (out_Buffer.size() > 0) {
			outputs[OUTPUT].setVoltage(*out_Buffer.startData() * 5.0f);
			out_Buffer.startIncr(1);
		}
	}

	~HCTIP() {
		delete pShifter;
	}
};

struct HCTIPWidget : BidooWidget {
	HCTIPWidget(HCTIP *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/HCTIP.svg"));

		addParam(createParam<BidooBlueKnob>(Vec(8, 70), module, HCTIP::PITCH_PARAM));
		addInput(createInput<PJ301MPort>(Vec(10, 130.f), module, HCTIP::PITCH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10, 283.0f), module, HCTIP::INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10, 330), module, HCTIP::OUTPUT));
	}
};

Model *modelHCTIP = createModel<HCTIP, HCTIPWidget>("HCTIP");
