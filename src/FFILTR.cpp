#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "dsp/ringbuffer.hpp"
#include "dep/filters/vocode.h"

#define BUFF_SIZE 512

using namespace std;

struct FFILTR : Module {
	enum ParamIds {
		CUTOFF_PARAM,
		RES_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		CUTOFF_INPUT,
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
	Vocoder *vocoder = NULL;

	FFILTR() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CUTOFF_PARAM, 0.0f, BUFF_SIZE / 2.0f, BUFF_SIZE / 2.0f, "Cutoff", " Hz", 0.f, 86.132812f);
		configParam(RES_PARAM, 1.0f, 10.0f, 1.0f, "Res");

		vocoder = new Vocoder(BUFF_SIZE, 4, APP->engine->getSampleRate());
	}

	void process(const ProcessArgs &args) override {
		in_Buffer.push(inputs[INPUT].getVoltage() / 10.0f);

		if (in_Buffer.full()) {
			vocoder->process(clamp(params[CUTOFF_PARAM].getValue() + inputs[CUTOFF_INPUT].getVoltage()*BUFF_SIZE*0.05f, 0.0f, BUFF_SIZE / 2.0f), params[RES_PARAM].getValue(), in_Buffer.startData(), out_Buffer.endData());
			out_Buffer.endIncr(BUFF_SIZE);
			in_Buffer.clear();
		}

		if (out_Buffer.size() > 0) {
			outputs[OUTPUT].setVoltage(*out_Buffer.startData() * 5.0f);
			out_Buffer.startIncr(1);
		}

	}

	~FFILTR() {
		//delete pShifter;
	}

};

struct FFILTRWidget : ModuleWidget {
	FFILTRWidget(FFILTR *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/FFILTR.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<BidooBlueKnob>(Vec(8, 100), module, FFILTR::CUTOFF_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(8, 185), module, FFILTR::RES_PARAM));
		addInput(createInput<PJ301MPort>(Vec(10, 150.66f), module, FFILTR::CUTOFF_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10, 242.66f), module, FFILTR::INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10, 299), module, FFILTR::OUTPUT));
	}
};

Model *modelFFILTR = createModel<FFILTR, FFILTRWidget>("FFilTr");
