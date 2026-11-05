#include "plugin.hpp"
#include "BidooComponents.hpp"
#include <math.h>

using namespace std;

struct LOURDE : BidooModule {
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


	bool state[PORT_MAX_CHANNELS] = {};
	// knob + CV of the first channel, for the displays
	float shown[4] = {};

	LOURDE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < 3; i++) {
			configParam(WEIGHT1 + i, -5.0f, 5.0f, 0.0f, "Weight " + std::to_string(i + 1));
			configInput(IN1 + i, "Input " + std::to_string(i + 1));
			configInput(INWEIGHT1 + i, "Weight " + std::to_string(i + 1) + " CV");
		}
		configParam(OUTFLOOR, -10.0f, 10.0f, 0.0f, "Threshold");
		configInput(INFLOOR, "Threshold CV");
		configOutput(OUT, "Gate (high while the weighted sum reaches the threshold)");
	}

	void process(const ProcessArgs &args) override;
};

// 0.1 either side of the threshold, so a sum sitting on it doesn't chatter
static const float LOURDE_HYSTERESIS = 0.1f;

void LOURDE::process(const ProcessArgs &args) {
	int channels = 1;
	for (int i = 0; i < 3; i++)
		channels = std::max(channels, inputs[IN1 + i].getChannels());
	for (int c = 0; c < channels; c++) {
		float w[3], sum = 0.f;
		for (int i = 0; i < 3; i++) {
			w[i] = clamp(params[WEIGHT1 + i].getValue() + inputs[INWEIGHT1 + i].getPolyVoltage(c), -5.0f, 5.0f);
			sum += w[i] * inputs[IN1 + i].getPolyVoltage(c);
		}
		float floor = clamp(params[OUTFLOOR].getValue() + inputs[INFLOOR].getPolyVoltage(c), -10.0f, 10.0f);
		if (state[c] ? sum < floor - LOURDE_HYSTERESIS : sum >= floor + LOURDE_HYSTERESIS)
			state[c] = !state[c];
		outputs[OUT].setVoltage(state[c] ? 10.0f : 0.0f, c);
		if (c == 0) {
			shown[0] = w[0];
			shown[1] = w[1];
			shown[2] = w[2];
			shown[3] = floor;
		}
	}
	outputs[OUT].setChannels(channels);
}

struct LourdeValueDisplay : TransparentWidget {
  float *value = nullptr;

  LourdeValueDisplay() {

  };

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (value) {
	      char display[128];
	  		snprintf(display, sizeof(display), "%2.2f", *value);
	  		nvgFontSize(args.vg, 14.0f);
	  		nvgFillColor(args.vg, YELLOW_BIDOO);
	  		nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
	  		nvgRotate(args.vg,-1.0f * M_PI_2);
	  		nvgText(args.vg, 0.0f, 0.0f, display, NULL);
	    }
		}
		Widget::drawLayer(args, layer);
	}

};

struct LOURDEWidget : BidooWidget {
	LOURDEWidget(LOURDE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/LOURDE.svg"));

  	addInput(createInput<PJ301MPort>(Vec(25.5,77), module, LOURDE::IN1));
    addInput(createInput<PJ301MPort>(Vec(25.5,157), module, LOURDE::IN2));
    addInput(createInput<PJ301MPort>(Vec(25.5,237), module, LOURDE::IN3));

    addInput(createInput<TinyPJ301MPort>(Vec(56.0f, 37.0f), module, LOURDE::INWEIGHT1));
    addInput(createInput<TinyPJ301MPort>(Vec(56.0f, 117.0f), module, LOURDE::INWEIGHT2));
    addInput(createInput<TinyPJ301MPort>(Vec(56.0f, 197.0f), module, LOURDE::INWEIGHT3));

    addParam(createParam<BidooBlueKnob>(Vec(22.5,30), module, LOURDE::WEIGHT1));
    addParam(createParam<BidooBlueKnob>(Vec(22.5,110), module, LOURDE::WEIGHT2));
    addParam(createParam<BidooBlueKnob>(Vec(22.5,190), module, LOURDE::WEIGHT3));

		LourdeValueDisplay *displayW1 = new LourdeValueDisplay();
		displayW1->box.pos = Vec(15,45);
		displayW1->value = module ? &module->shown[0] : NULL;
		addChild(displayW1);

		LourdeValueDisplay *displayW2 = new LourdeValueDisplay();
		displayW2->box.pos = Vec(15,125);
		displayW2->value = module ? &module->shown[1] : NULL;
		addChild(displayW2);

		LourdeValueDisplay *displayW3 = new LourdeValueDisplay();
		displayW3->box.pos = Vec(15,205);
		displayW3->value = module ? &module->shown[2] : NULL;
		addChild(displayW3);

    addParam(createParam<BidooBlueKnob>(Vec(22.5,282), module, LOURDE::OUTFLOOR));

		LourdeValueDisplay *displayOF = new LourdeValueDisplay();
		displayOF->box.pos = Vec(15,296);
		displayOF->value = module ? &module->shown[3] : NULL;
		addChild(displayOF);

    addInput(createInput<TinyPJ301MPort>(Vec(56.0f, 289.0f), module, LOURDE::INFLOOR));

  	addOutput(createOutput<PJ301MPort>(Vec(25.5,330), module, LOURDE::OUT));
  }
};

Model *modelLOURDE = createModel<LOURDE, LOURDEWidget>("LoURdE");
