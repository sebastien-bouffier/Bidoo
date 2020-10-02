#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "dsp/ringbuffer.hpp"
#include "dep/filters/pitchshifter.h"

#define BUFF_SIZE 262144

using namespace std;

struct FREIN : Module {
	enum ParamIds {
		TRIG_PARAM,
		START_PARAM,
		SPEED_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		TRIG_INPUT,
		START_INPUT,
		SPEED_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		BREAK_LIGHT,
		NUM_LIGHTS = BREAK_LIGHT + 3
	};

	dsp::SchmittTrigger trigTrigger;
	dsp::SchmittTrigger startTrigger;
	dsp::DoubleRingBuffer<float, BUFF_SIZE> in_Buffer;
	bool breakOn = false;
	float breakSamples = 0.0f;
	float speed = 1.0f;
	float speedUp = 0.0f;
	float head = 0.0f;

	FREIN() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(TRIG_PARAM, 0.0f, 1.0f, 0.0f, "Trig");
		configParam(SPEED_PARAM, 0.5f, 3.0f, 1.0f, "Speed");
	}

	void process(const ProcessArgs &args) override {

		if (trigTrigger.process(params[TRIG_PARAM].getValue() + inputs[TRIG_INPUT].getVoltage())) {
			breakOn = true;
			breakSamples = args.sampleRate*clamp(params[SPEED_PARAM].getValue() + inputs[SPEED_INPUT].getVoltage(),0.5f,3.0f);
			speedUp = 1.0f/breakSamples;
			speed = 1.0f;
			head = 0.0f;
			in_Buffer.clear();
		}

		if (trigTrigger.process(params[START_PARAM].getValue() + inputs[START_INPUT].getVoltage())) {
			in_Buffer.clear();
			breakOn = false;
			speed = 0.0f;
			speedUp = 0.05f;
		}

		in_Buffer.push(inputs[INPUT].getVoltage());

		if (breakOn) {
			if (speed > 0.0f) {
				int xi = head;
				float xf = head - xi;
				float crossfaded = crossfade(*(in_Buffer.startData()+xi), *(in_Buffer.startData()+xi+1), xf);
				if (speed<=0.1f) {
					outputs[OUTPUT].setVoltage(crossfaded*10.0f*speed);
				}
				else {
					outputs[OUTPUT].setVoltage(crossfaded);
				}
				speed = max(speed-speedUp,0.0f);
				head = min (head+speed,(float)in_Buffer.size());
				lights[BREAK_LIGHT].setBrightness(0.0f);
				lights[BREAK_LIGHT+1].setBrightness(0.0f);
				lights[BREAK_LIGHT+2].setBrightness(1.0f);
			}
			else
			{
				outputs[OUTPUT].setVoltage(0.0f);
				lights[BREAK_LIGHT].setBrightness(1.0f);
				lights[BREAK_LIGHT+1].setBrightness(0.0f);
				lights[BREAK_LIGHT+2].setBrightness(0.0f);
				in_Buffer.clear();
			}
		}
		else {
			if (speed<1.0f) {
				outputs[OUTPUT].setVoltage(*in_Buffer.startData()*speed);
				lights[BREAK_LIGHT].setBrightness(0.0f);
				lights[BREAK_LIGHT+1].setBrightness(0.0f);
				lights[BREAK_LIGHT+2].setBrightness(1.0f);
				speed = min(speed+speedUp,1.0f);
			}
			else {
				lights[BREAK_LIGHT].setBrightness(0.0f);
				lights[BREAK_LIGHT+1].setBrightness(1.0f);
				lights[BREAK_LIGHT+2].setBrightness(0.0f);
				outputs[OUTPUT].setVoltage(*in_Buffer.startData());
			}
			in_Buffer.startIncr(1);
		}
	}
};

struct FREINWidget : ModuleWidget {
	FREINWidget(FREIN *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/FREIN.svg")));

		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(19.5f, 20.0f), module, FREIN::BREAK_LIGHT));

		addParam(createParam<BlueCKD6>(Vec(8.5f, 50), module, FREIN::TRIG_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(15, 90.f), module, FREIN::TRIG_INPUT));
		addParam(createParam<BlueCKD6>(Vec(8.5f, 129.f), module, FREIN::START_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(15, 169.f), module, FREIN::START_INPUT));
		addParam(createParam<BidooBlueKnob>(Vec(7.5f, 208.f), module, FREIN::SPEED_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(15, 248.f), module, FREIN::SPEED_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10, 283.0f), module, FREIN::INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10, 330), module, FREIN::OUTPUT));
	}
};

Model *modelFREIN = createModel<FREIN, FREINWidget>("FREIN");
