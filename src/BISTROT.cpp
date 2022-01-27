#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

struct BISTROT : BidooModule {
	enum ParamIds {
		LINK_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
    ADCCLOCK_INPUT,
    DACCLOCK_INPUT,
		BIT_INPUT,
		NUM_INPUTS = BIT_INPUT + 8
	};
	enum OutputIds {
		OUTPUT,
    BIT_OUTPUT,
		NUM_OUTPUTS  = BIT_OUTPUT + 8
	};
	enum LightIds {
    LINK_LIGHT,
    BIT_INPUT_LIGHTS,
    BIT_OUTPUT_LIGHTS = BIT_INPUT_LIGHTS + 8,
		NUM_LIGHTS = BIT_OUTPUT_LIGHTS + 8
	};

  dsp::SchmittTrigger linkTrigger, acdClockTrigger, dacClockTrigger;

  unsigned char in = 0;
  unsigned char out = 0;

	BISTROT() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
	}

	~BISTROT() {
  }

	void process(const ProcessArgs &args) override;
};


void BISTROT::process(const ProcessArgs &args) {
    if ((!inputs[ADCCLOCK_INPUT].isConnected()) || (acdClockTrigger.process(inputs[ADCCLOCK_INPUT].getVoltage())))
    {
      in = roundf(clamp(clamp(inputs[INPUT].getVoltage(),-10.0f,10.0f) / 20.0f + 0.5f, 0.0f, 1.0f) * 255);
    }

    for (int i = 0 ; i != 8 ; i++)
    {
      int bitValue = ((in & (1U << i)) != 0);
      lights[BIT_INPUT_LIGHTS+i].setBrightness(1-bitValue);
      outputs[BIT_OUTPUT+i].setVoltage((1-bitValue) * 10);
    }

    if ((!inputs[DACCLOCK_INPUT].isConnected()) || (dacClockTrigger.process(inputs[DACCLOCK_INPUT].getVoltage())))
    {
      for (int i = 0 ; i != 8 ; i++)
      {
        if ((inputs[BIT_INPUT+i].isConnected()) && (inputs[BIT_INPUT+i].getVoltage() != 0)) {
          out |= 1U << i;
        }
				else {
					out &= ~(1U << i);
				}
        lights[BIT_OUTPUT_LIGHTS+i].setBrightness((out >> i) & 1U);
      }
    }
		outputs[OUTPUT].setVoltage(-1.0f * clamp(((((float)out/255.0f))-0.5f)*10.0f,-10.0f,10.0f));
}

struct BISTROTWidget : BidooWidget {
	BISTROTWidget(BISTROT *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/BISTROT.svg"));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

    addInput(createInput<PJ301MPort>(Vec(29.0f, 46.0f), module, BISTROT::ADCCLOCK_INPUT));
    addInput(createInput<PJ301MPort>(Vec(67.0f, 46.0f), module, BISTROT::DACCLOCK_INPUT));

    for (int i = 0; i<8; i++) {
      addChild(createLight<SmallLight<RedLight>>(Vec(18.0f, 97.5f + 26.0f * i), module, BISTROT::BIT_INPUT_LIGHTS + i));
			addOutput(createOutput<TinyPJ301MPort>(Vec(33.5f, 93.0f + 26.0f * i), module, BISTROT::BIT_OUTPUT + i));
			addInput(createInput<TinyPJ301MPort>(Vec(71.5f, 93.0f + 26.0f * i), module, BISTROT::BIT_INPUT + i));
			addChild(createLight<SmallLight<BlueLight>>(Vec(95.0f, 97.5f + 26.0f * i), module, BISTROT::BIT_OUTPUT_LIGHTS + i));
    }

		addInput(createInput<PJ301MPort>(Vec(29.0f, 330.0f), module, BISTROT::INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(67.0f, 330.0f), module, BISTROT::OUTPUT));
	}
};

Model *modelBISTROT = createModel<BISTROT, BISTROTWidget>("BISTROT");
