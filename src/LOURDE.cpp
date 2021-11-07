#include "plugin.hpp"
#include "BidooComponents.hpp"
#include <math.h>

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


	LOURDE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(WEIGHT1, -5.0f, 5.0f, 0.0f);
    configParam(WEIGHT2, -5.0f, 5.0f, 0.0f);
    configParam(WEIGHT3, -5.0f, 5.0f, 0.0f);
    configParam(OUTFLOOR, -10.0f, 10.0f, 0.0f);
  }

	void process(const ProcessArgs &args) override;
};


void LOURDE::process(const ProcessArgs &args) {
  float sum = clamp(params[WEIGHT1].getValue()+inputs[INWEIGHT1].getVoltage(),-5.0f,5.0f)*inputs[IN1].getVoltage() + clamp(params[WEIGHT2].getValue()+inputs[INWEIGHT2].getVoltage(),-5.0f,5.0f)*inputs[IN2].getVoltage()
	+ clamp(params[WEIGHT3].getValue()+inputs[INWEIGHT3].getVoltage(),-5.0f,5.0f)*inputs[IN3].getVoltage();
	outputs[OUT].setVoltage(sum >= clamp(params[OUTFLOOR].getValue()+inputs[INFLOOR].getVoltage(),-10.0f,10.0f) ? 10.0f : 0.0f);
}

struct LabelDisplayWidget : TransparentWidget {
  Param *value;

  LabelDisplayWidget() {

  };

  void draw(const DrawArgs &args) override
  {
		nvgGlobalTint(args.vg, color::WHITE);
    if (value) {
      char display[128];
  		snprintf(display, sizeof(display), "%2.2f", value->getValue());
  		nvgFontSize(args.vg, 14.0f);
  		nvgFillColor(args.vg, YELLOW_BIDOO);
  		nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
  		nvgRotate(args.vg,-1.0f * M_PI_2);
  		nvgText(args.vg, 0.0f, 0.0f, display, NULL);
    }
  }
};

struct LOURDEWidget : ModuleWidget {
	LOURDEWidget(LOURDE *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LOURDE.svg")));


  	addInput(createInput<PJ301MPort>(Vec(25.5,77), module, LOURDE::IN1));
    addInput(createInput<PJ301MPort>(Vec(25.5,157), module, LOURDE::IN2));
    addInput(createInput<PJ301MPort>(Vec(25.5,237), module, LOURDE::IN3));

    addInput(createInput<TinyPJ301MPort>(Vec(56.0f, 37.0f), module, LOURDE::INWEIGHT1));
    addInput(createInput<TinyPJ301MPort>(Vec(56.0f, 117.0f), module, LOURDE::INWEIGHT2));
    addInput(createInput<TinyPJ301MPort>(Vec(56.0f, 197.0f), module, LOURDE::INWEIGHT3));

    addParam(createParam<BidooBlueKnob>(Vec(22.5,30), module, LOURDE::WEIGHT1));
    addParam(createParam<BidooBlueKnob>(Vec(22.5,110), module, LOURDE::WEIGHT2));
    addParam(createParam<BidooBlueKnob>(Vec(22.5,190), module, LOURDE::WEIGHT3));

		LabelDisplayWidget *displayW1 = new LabelDisplayWidget();
		displayW1->box.pos = Vec(15,45);
		displayW1->value = module ? &module->params[LOURDE::WEIGHT1] : NULL;
		addChild(displayW1);

		LabelDisplayWidget *displayW2 = new LabelDisplayWidget();
		displayW2->box.pos = Vec(15,125);
		displayW2->value = module ? &module->params[LOURDE::WEIGHT2] : NULL;
		addChild(displayW2);

		LabelDisplayWidget *displayW3 = new LabelDisplayWidget();
		displayW3->box.pos = Vec(15,205);
		displayW3->value = module ? &module->params[LOURDE::WEIGHT3] : NULL;
		addChild(displayW3);

    addParam(createParam<BidooBlueKnob>(Vec(22.5,282), module, LOURDE::OUTFLOOR));

		LabelDisplayWidget *displayOF = new LabelDisplayWidget();
		displayOF->box.pos = Vec(15,296);
		displayOF->value = module ? &module->params[LOURDE::OUTFLOOR] : NULL;
		addChild(displayOF);

    addInput(createInput<TinyPJ301MPort>(Vec(56.0f, 289.0f), module, LOURDE::INFLOOR));

  	addOutput(createOutput<PJ301MPort>(Vec(25.5,330), module, LOURDE::OUT));
  }
};

Model *modelLOURDE = createModel<LOURDE, LOURDEWidget>("LoURdE");
