#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <ctime>

using namespace std;

struct LATE : BidooModule {
	enum ParamIds {
		SWING_PARAM,
		CVCOEFF_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		SWING_INPUT,
		CLOCK_INPUT,
		RESET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		CLOCK_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	bool odd = true;
	bool armed = false;
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::Timer resetTimer;
	dsp::PulseGenerator pulse;
	clock_t tCurrent = clock();
	clock_t tPrevious = clock();


	LATE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SWING_PARAM, 0, 9, 0, "Swing");
		configParam(CVCOEFF_PARAM, -1.0f, 1.0f, 0.0f, "Placeholder");
	}

	void process(const ProcessArgs &args) override {
		outputs[CLOCK_OUTPUT].setVoltage(0.f);
		clock_t now = clock();

		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage())) {
			odd = false;
			resetTimer.reset();
			tPrevious = tCurrent;
			tCurrent = now;
			pulse.trigger(1e-3f);
			armed = false;
		}

		if ((resetTimer.process(args.sampleTime)>1e-3) && clockTrigger.process(inputs[CLOCK_INPUT].getVoltage())) {
			tPrevious = tCurrent;
			tCurrent = now;
			if (odd) {
				pulse.trigger(1e-3f);
				odd = false;
				armed = false;
			} else {
				armed = true;
			}
		}

		float lag = rescale(clamp(params[SWING_PARAM].getValue() + params[CVCOEFF_PARAM].getValue() * inputs[SWING_INPUT].getVoltage(), 0.0f, 9.0f), 0.0f, 10.0f, 0.0f, (float)tCurrent - (float)tPrevious);

		if (armed && !odd && (((float)now - (float)tCurrent) >= lag)) {
			pulse.trigger(1e-3f);
			armed = false;
			odd = true;
		}

		outputs[CLOCK_OUTPUT].setVoltage(pulse.process(args.sampleTime) ? 10.0f : 0.0f);
	}

};

struct LATEWidget : BidooWidget {
	LATEWidget(LATE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/LATE.svg"));

		addParam(createParam<BidooBlueKnob>(Vec(8, 70), module, LATE::SWING_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(13, 105), module, LATE::CVCOEFF_PARAM));

		addInput(createInput<PJ301MPort>(Vec(10, 130), module, LATE::SWING_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10, 236), module, LATE::RESET_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10, 283.0f), module, LATE::CLOCK_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(10, 330), module, LATE::CLOCK_OUTPUT));
	}
};

Model *modelLATE = createModel<LATE, LATEWidget>("lATe");
