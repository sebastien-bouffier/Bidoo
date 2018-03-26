#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/samplerate.hpp"

using namespace std;

struct LOURDE : Module {
	enum ParamIds {
		WEIGHT1,
		WEIGHT2,
		WEIGHT3,
		OUTFLOOR,
		NUM_PARAMS
	};
	enum InputIds {
		IN1,
		IN2,
		IN3,
    INWEIGHT1,
		INWEIGHT2,
		INWEIGHT3,
    INFLOOR,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};


	LOURDE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {

	}

	void step() override;
};


void LOURDE::step() {
  float sum = clamp(params[WEIGHT1].value+inputs[INWEIGHT1].value,-5.0f,5.0f)*inputs[IN1].value + clamp(params[WEIGHT2].value+inputs[INWEIGHT2].value,-5.0f,5.0f)*inputs[IN2].value + clamp(params[WEIGHT3].value+inputs[INWEIGHT3].value,-5.0f,5.0f)*inputs[IN3].value;
	outputs[OUT].value = sum >= clamp(params[OUTFLOOR].value+inputs[INFLOOR].value,-10.0f,10.0f) ? 10.0f : 0.0f;
}


struct LOURDEWidget : ModuleWidget {
	LOURDEWidget(LOURDE *module) : ModuleWidget(module) {
		setPanel(SVG::load(assetPlugin(plugin, "res/LOURDE.svg")));

  	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
  	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
  	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
  	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

  	addInput(Port::create<PJ301MPort>(Vec(25.5,85), Port::INPUT, module, LOURDE::IN1));
    addInput(Port::create<PJ301MPort>(Vec(25.5,155), Port::INPUT, module, LOURDE::IN2));
    addInput(Port::create<PJ301MPort>(Vec(25.5,225), Port::INPUT, module, LOURDE::IN3));

    addInput(Port::create<TinyPJ301MPort>(Vec(56.0f, 57.0f), Port::INPUT, module, LOURDE::INWEIGHT1));
    addInput(Port::create<TinyPJ301MPort>(Vec(56.0f, 127.0f), Port::INPUT, module, LOURDE::INWEIGHT2));
    addInput(Port::create<TinyPJ301MPort>(Vec(56.0f, 197.0f), Port::INPUT, module, LOURDE::INWEIGHT3));

    addParam(ParamWidget::create<BidooBlueKnob>(Vec(22.5,50), module, LOURDE::WEIGHT1, -5.0f, 5.0f, 0.0f));
    addParam(ParamWidget::create<BidooBlueKnob>(Vec(22.5,120), module, LOURDE::WEIGHT2, -5.0f, 5.0f, 0.0f));
    addParam(ParamWidget::create<BidooBlueKnob>(Vec(22.5,190), module, LOURDE::WEIGHT3, -5.0f, 5.0f, 0.0f));

    addParam(ParamWidget::create<BidooBlueKnob>(Vec(22.5,270), module, LOURDE::OUTFLOOR, -10.0f, 10.0f, 0.0f));
    addInput(Port::create<TinyPJ301MPort>(Vec(56.0f, 277.0f), Port::INPUT, module, LOURDE::INFLOOR));

  	addOutput(Port::create<PJ301MPort>(Vec(25.5,320), Port::OUTPUT, module, LOURDE::OUT));
  }
};


Model *modelLOURDE= Model::create<LOURDE, LOURDEWidget>("Bidoo", "LoURdE", "LoURdE gate", LOGIC_TAG);
