#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "dsp/ringbuffer.hpp"
#include "dep/filters/fftfilter.h"

#define BUFF_SIZE 128

using namespace std;

struct FFILTR : Module {
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

	DoubleRingBuffer<float,BUFF_SIZE> in_Buffer;
	DoubleRingBuffer<float,BUFF_SIZE> out_Buffer;
	FFTFilter *pShifter = NULL;

	FFILTR() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
		pShifter = new FFTFilter(BUFF_SIZE, 4, engineGetSampleRate());
	}

	~FFILTR() {
		delete pShifter;
	}

	void step() override;
};


void FFILTR::step() {
	in_Buffer.push(inputs[INPUT].value/10.0f);

	if (in_Buffer.full()) {
		pShifter->process(clamp(params[PITCH_PARAM].value + inputs[PITCH_INPUT].value ,1.0f,32.0), in_Buffer.startData(), out_Buffer.endData());
		out_Buffer.endIncr(BUFF_SIZE);
		in_Buffer.clear();
	}

	if (out_Buffer.size()>0) {
		outputs[OUTPUT].value = *out_Buffer.startData() * 10.0f;
		out_Buffer.startIncr(1);
	}

}

struct FFILTRWidget : ModuleWidget {
	FFILTRWidget(FFILTR *module) : ModuleWidget(module) {
		setPanel(SVG::load(assetPlugin(plugin, "res/FFILTR.svg")));

		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));


		addParam(ParamWidget::create<BidooBlueKnob>(Vec(8, 100), module, FFILTR::PITCH_PARAM, 2.0f, 32.0f, 32.0f));

		addInput(Port::create<PJ301MPort>(Vec(10, 150.66f), Port::INPUT, module, FFILTR::PITCH_INPUT));

		addInput(Port::create<PJ301MPort>(Vec(10, 242.66f), Port::INPUT, module, FFILTR::INPUT));

		addOutput(Port::create<PJ301MPort>(Vec(10, 299), Port::OUTPUT, module, FFILTR::OUTPUT));
	}
};

Model *modelFFILTR = Model::create<FFILTR, FFILTRWidget>("Bidoo", "FFILTR", "FFILTR linear phase filter", FILTER_TAG);
