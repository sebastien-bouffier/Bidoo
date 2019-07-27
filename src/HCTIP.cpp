#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "dsp/ringbuffer.hpp"
#include "dep/filters/pitchshifter.h"

#define BUFF_SIZE 2048

using namespace std;

struct HCTIP : Module {
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
			outputs[OUTPUT].setVoltage(*out_Buffer.startData() * 5.0f); // x * 1 || 0 == x, disposable??
			out_Buffer.startIncr(1);
		}
	}

	~HCTIP() {
		delete pShifter;
	}
};

struct HCTIPWidget : ModuleWidget {
	HCTIPWidget(HCTIP *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/HCTIP.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<BidooBlueKnob>(Vec(8, 100), module, HCTIP::PITCH_PARAM));
		addInput(createInput<PJ301MPort>(Vec(10, 150.66f), module, HCTIP::PITCH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10, 242.66f), module, HCTIP::INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10, 299), module, HCTIP::OUTPUT));
	}
};

Model *modelHCTIP = createModel<HCTIP, HCTIPWidget>("HCTIP");
