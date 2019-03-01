#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "dsp/ringbuffer.hpp"
#include "dep/filters/pitchshifter.h"

#define BUFF_SIZE 128

using namespace std;

struct CURT : Module {
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

	CURT() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
	}

	~CURT() {

	}

	void step() override;
};


void CURT::step() {
	in_Buffer.push(inputs[INPUT].value/10.0f);

	if (in_Buffer.full()) {
		//pShifter->process(clamp(params[PITCH_PARAM].value + inputs[PITCH_INPUT].value ,0.5f,2.0f), in_Buffer.startData(), out_Buffer.endData());
		out_Buffer.endIncr(BUFF_SIZE);
		in_Buffer.clear();
	}

	if (out_Buffer.size()>0) {
		outputs[OUTPUT].value = *out_Buffer.startData() * 5.0f;
		out_Buffer.startIncr(1);
	}

}

struct CURTWidget : ModuleWidget {
	CURTWidget(CURT *module) : ModuleWidget(module) {
		setPanel(SVG::load(assetPlugin(plugin, "res/CURT.svg")));

		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));


		addParam(ParamWidget::create<BidooBlueKnob>(Vec(8, 100), module, CURT::PITCH_PARAM, 0.5f, 2.0f, 1.0f));

		addInput(Port::create<PJ301MPort>(Vec(10, 150.66f), Port::INPUT, module, CURT::PITCH_INPUT));

		addInput(Port::create<PJ301MPort>(Vec(10, 242.66f), Port::INPUT, module, CURT::INPUT));

		addOutput(Port::create<PJ301MPort>(Vec(10, 299), Port::OUTPUT, module, CURT::OUTPUT));
	}
};

Model *modelCURT = Model::create<CURT, CURTWidget>("Bidoo", "cuRt", "cuRt .......", EFFECT_TAG);
