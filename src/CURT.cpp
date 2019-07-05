#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "dsp/ringbuffer.hpp"
#include "dep/filters/pitchshifter.h"
#include "dsp/fir.hpp"

#define BUFF_SIZE 256
#define OVERLAP 4

using namespace std;

struct CURT : Module {
	enum ParamIds {
		PITCH_PARAM,
		MODE_PARAM,
		BUFF_SIZE_PARAM,
		OVERLAP_PARAM,
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

	dsp::DoubleRingBuffer<float,BUFF_SIZE> in_Buffer;
	dsp::DoubleRingBuffer<float, 2*BUFF_SIZE> out_Buffer;
	float bins[OVERLAP][BUFF_SIZE];
	int index=-1;
	size_t readSteps=0;
	size_t writeSteps=0;
	dsp::SchmittTrigger modeTrigger;
	bool mode=0;
	size_t overlap, buff_size;

	CURT() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PITCH_PARAM, 0.2f, 2.0f, 1.0f, "Pitch");
		// configParam(MODE_PARAM, 0.f, 1.f, 0.f, "Mode");
		// configParam(BUFF_SIZE_PARAM, 6.0f, 8.0f, 8.0f, "Buffer size");
		// configParam(OVERLAP_PARAM, 1.0f, 4.0f, 2.0f, "Overlap");

		overlap = OVERLAP;
		buff_size = BUFF_SIZE;
		for(int i=0; i<OVERLAP; i++) {
			memset(bins[i], 0, sizeof(bins[i]));
		}
		for(int i=0; i<BUFF_SIZE; i++) {
			in_Buffer.push(0.0f);
		}
		for(int i=0; i<2*BUFF_SIZE; i++) {
			out_Buffer.push(0.0f);
		}
	}

	~CURT() {

	}
	//
	// void updateBuff() {
	// 	while (in_Buffer.size()<buff_size) {
	// 		in_Buffer.push(0.0f);
	// 	}
	// 	while (in_Buffer.size()>buff_size) {
	// 		in_Buffer.startIncr(1);
	// 	}
	// 	while (out_Buffer.size()<2*buff_size) {
	// 		in_Buffer.push(0.0f);
	// 	}
	// 	while (out_Buffer.size()>2*buff_size) {
	// 		in_Buffer.startIncr(1);
	// 	}
	// }

	// json_t *dataToJson() override {
	// 	json_t *rootJ = json_object();
	// 	json_object_set_new(rootJ, "mode", json_boolean(mode));
	// 	return rootJ;
	// }
	//
	// void dataFromJson(json_t *rootJ) override {
	// 	json_t *modeJ = json_object_get(rootJ, "mode");
	// 	if (modeJ)
	// 		mode = json_is_true(modeJ);
	// }

	void process(const ProcessArgs &args) override;
};

void CURT::process(const ProcessArgs &args) {
	// if (modeTrigger.process(params[MODE_PARAM].value)) {
	// 	mode = !mode;
	// }
	//
	// if ((size_t)params[BUFF_SIZE_PARAM].value != buff_size) {
	// 	buff_size = pow(2.0f, params[BUFF_SIZE_PARAM].getValue());
	// 	updateBuff();
	// }
	//
	// overlap = params[OVERLAP_PARAM].getValue();

	in_Buffer.startIncr(1);
	in_Buffer.push(inputs[INPUT].isConnected() ? inputs[INPUT].getVoltage() : 0.f);

	readSteps++;

	if (readSteps>=(buff_size/overlap)) {
		index=(index+1)%overlap;
		for(size_t i=0; i<buff_size; i++) {
			bins[index][i]=*(in_Buffer.startData()+i);
		}
		dsp::blackmanHarrisWindow(bins[index],buff_size);
		readSteps = 0;
	}

	writeSteps++;

	if ((writeSteps>=((float)buff_size*params[PITCH_PARAM].getValue()/(float)overlap))) {
		if ((index%2==0) || (mode)) {
			for(size_t i=0; i<buff_size; i++) {
				out_Buffer.data[out_Buffer.mask(out_Buffer.end-buff_size+i)] += bins[index][i];
			}
		}
		else
		{
			for(size_t i=0; i<buff_size; i++) {
				out_Buffer.data[out_Buffer.mask(out_Buffer.end-buff_size+i)] += bins[index][buff_size-i-1];
			}
		}
		writeSteps = 0;
	}

	outputs[OUTPUT].setVoltage(*out_Buffer.startData());
	out_Buffer.startIncr(1);
	out_Buffer.push(0.0f);
}

struct CURTWidget : ModuleWidget {
	CURTWidget(CURT *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/CURT.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		addParam(createParam<BidooBlueKnob>(Vec(8, 90), module, CURT::PITCH_PARAM));
		// addParam(createParam<BlueCKD6>(Vec(8, 175.0f), module, CURT::MODE_PARAM));
		// addParam(createParam<BidooBlueSnapTrimpot>(Vec(2, 205), module, CURT::BUFF_SIZE_PARAM));
		// addParam(createParam<BidooBlueSnapTrimpot>(Vec(24, 205), module, CURT::OVERLAP_PARAM));

		addInput(createInput<PJ301MPort>(Vec(10, 140.0f), module, CURT::PITCH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10, 245.66f), module, CURT::INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10, 299), module, CURT::OUTPUT));
	}
};

Model *modelCURT = createModel<CURT, CURTWidget>("cuRt");
