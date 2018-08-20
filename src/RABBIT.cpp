#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

struct RABBIT : Module {
	enum ParamIds {
		BITON_PARAM,
    BITREV_PARAM = BITON_PARAM + 8,
		NUM_PARAMS = BITREV_PARAM + 8
	};
	enum InputIds {
		L_INPUT,
    R_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		L_OUTPUT,
    R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
    BITON_LIGHTS,
    BITREV_LIGHTS = BITON_LIGHTS + 8,
		NUM_LIGHTS = BITREV_LIGHTS + 8
	};

  SchmittTrigger bitOnTrigger[8], bitRevTrigger[8];

  bool bitOn[8];
  bool bitRev[8];

	RABBIT() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
    memset(&bitOn,1,8*sizeof(bool));
    memset(&bitRev,0,8*sizeof(bool));
	}

	~RABBIT() {
  }

	void step() override;
};


void RABBIT::step() {
  float in_L = clamp(inputs[L_INPUT].value,-10.0f,10.0f);
  float in_R = clamp(inputs[R_INPUT].value,-10.0f,10.0f);

  in_L = roundf(clamp(in_L / 20.0f + 0.5f, 0.0f, 1.0f) * 255);
  in_R = roundf(clamp(in_R / 20.0f + 0.5f, 0.0f, 1.0f) * 255);

  unsigned char red_L = in_L;
  unsigned char red_R = in_R;

  for (int i = 0 ; i < 8 ; i++ ) {

    if (bitOnTrigger[i].process(params[BITON_PARAM+i].value))
    {
      bitOn[i] = !bitOn[i];
    }

    if (bitRevTrigger[i].process(params[BITREV_PARAM+i].value))
    {
      bitRev[i] = !bitRev[i];
    }

    if (!bitOn[i]) {
      red_L &= ~(1 << i);
      red_R &= ~(1 << i);
    }
    else {
      if (bitRev[i]) {
        red_L ^= ~(1 << i);
        red_R ^= ~(1 << i);
      }
    }

    lights[BITON_LIGHTS + i].value = bitOn[i] == 1 ? 0.0f : 1.0f;
    lights[BITREV_LIGHTS + i].value = bitRev[i] == 1 ? 1.0f : 0.0f;
  }

  outputs[L_OUTPUT].value = clamp(((((float)red_L/255.0f))-0.5f)*20.0f,-10.0f,10.0f);
  outputs[R_OUTPUT].value = clamp(((((float)red_R/255.0f))-0.5f)*20.0f,-10.0f,10.0f);
}

struct RABBITWidget : ModuleWidget {
	RABBITWidget(RABBIT *module) : ModuleWidget(module) {
		setPanel(SVG::load(assetPlugin(plugin, "res/RABBIT.svg")));

		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

    for (int i = 0; i<8; i++) {
    	addParam(ParamWidget::create<BlueCKD6>(Vec(9.0f, 45.0f + 32.0f * i), module, RABBIT::BITON_PARAM + i, 0.0f, 1.0f, 0.0f));
      addChild(ModuleLightWidget::create<SmallLight<RedLight>>(Vec(42.0f,  56.0f + 32.0f * i), module, RABBIT::BITON_LIGHTS + i));
      addChild(ModuleLightWidget::create<SmallLight<BlueLight>>(Vec(55.0f,  56.0f + 32.0f * i), module, RABBIT::BITREV_LIGHTS + i));
      addParam(ParamWidget::create<BlueCKD6>(Vec(67.0f, 45.0f + 32.0f * i), module, RABBIT::BITREV_PARAM + i, 0.0f, 1.0f, 0.0f));
    }

		addInput(Port::create<TinyPJ301MPort>(Vec(24.0f, 319.0f), Port::INPUT, module, RABBIT::L_INPUT));
		addInput(Port::create<TinyPJ301MPort>(Vec(24.0f, 339.0f), Port::INPUT, module, RABBIT::R_INPUT));
		addOutput(Port::create<TinyPJ301MPort>(Vec(78.0f, 319.0f),Port::OUTPUT, module, RABBIT::L_OUTPUT));
		addOutput(Port::create<TinyPJ301MPort>(Vec(78.0f, 339.0f),Port::OUTPUT, module, RABBIT::R_OUTPUT));
	}
};



Model *modelRABBIT = Model::create<RABBIT, RABBITWidget>("Bidoo", "rabBIT", "rabBIT bit crusher", EFFECT_TAG, DIGITAL_TAG, DISTORTION_TAG);
