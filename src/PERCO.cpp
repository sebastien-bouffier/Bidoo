// Based on Will Pirkle's courses & Vadim Zavalishin's book

#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/resampler.hpp"

using namespace std;

#define pi 3.14159265359


struct MultiFilter {
	float q;
	float freq;
	float smpRate;
	float hp = 0.0f, bp = 0.0f, lp = 0.0f, mem1 = 0.0f, mem2 = 0.0f;

	void setParams(float freq, float q, float smpRate) {
		this->freq = freq;
		this->q = q;
		this->smpRate = smpRate;
	}

	void calcOutput(float sample) {
		float g = tan(pi*freq / smpRate);
		float R = 1.0f / (2.0f*q);
		hp = (sample - (2.0f*R + g)*mem1 - mem2) / (1.0f + 2.0f * R * g + g * g);
		bp = g * hp + mem1;
		lp = g * bp + mem2;
		mem1 = g * hp + bp;
		mem2 = g * bp + lp;
	}

};

struct PERCO : Module {
	enum ParamIds {
		CUTOFF_PARAM,
		Q_PARAM,
		CMOD_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN,
		CUTOFF_INPUT,
		Q_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_LP,
		OUT_BP,
		OUT_HP,
		NUM_OUTPUTS
	};
	enum LightIds {
		LEARN_LIGHT,
		NUM_LIGHTS
	};

	MultiFilter filter;

	PERCO() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CUTOFF_PARAM, 0.f, 1.f, 1.f, "Center Freq.", " Hz", params[CUTOFF_PARAM].getValue(), 20.f * 410.58f);//freqC > max 8211.6 Hz min 22ish
		configParam(Q_PARAM, .1f, 1.f, .1f, "Q factor", "", params[Q_PARAM].getValue(), 20.f);
		configParam(CMOD_PARAM, -1.f, 1.f, 0.f, "Freq. Mod", "%", 0.f, 100.f);
	}

	void process(const ProcessArgs &args) override {
		float cfreq = std::pow(2.0f, rescale(clamp(params[CUTOFF_PARAM].getValue() + params[CMOD_PARAM].getValue() * inputs[CUTOFF_INPUT].getVoltage() / 5.0f, 0.0f, 1.0f), 0.0f, 1.0f, 4.5f, 13.0f));
		float q = 10.0f * clamp(params[Q_PARAM].getValue() + inputs[Q_INPUT].getVoltage() * 0.2f, 0.1f, 1.0f);
		filter.setParams(cfreq, q, args.sampleRate);
		float in = inputs[IN].getVoltage() * 0.2f; //normalise to -1/+1 we consider VCV Rack standard is #+5/-5V on VCO1
		//filtering
		filter.calcOutput(in);
		outputs[OUT_LP].setVoltage(filter.lp * 5.0f);
		outputs[OUT_HP].setVoltage(filter.hp * 5.0f);
		outputs[OUT_BP].setVoltage(filter.bp * 5.0f);
	}

};

struct PERCOWidget : ModuleWidget {
	PERCOWidget(PERCO *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/PERCO.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<BidooHugeBlueKnob>(Vec(31, 61), module, PERCO::CUTOFF_PARAM));
		addParam(createParam<BidooLargeBlueKnob>(Vec(12, 143), module, PERCO::Q_PARAM));
		addParam(createParam<BidooLargeBlueKnob>(Vec(71, 143), module, PERCO::CMOD_PARAM));
		///Innies
		addInput(createInput<PJ301MPort>(Vec(10, 276), module, PERCO::IN));
		addInput(createInput<PJ301MPort>(Vec(48, 276), module, PERCO::CUTOFF_INPUT));
		addInput(createInput<PJ301MPort>(Vec(85, 276), module, PERCO::Q_INPUT));
		///Outies
		addOutput(createOutput<PJ301MPort>(Vec(10, 320), module, PERCO::OUT_LP));
		addOutput(createOutput<PJ301MPort>(Vec(48, 320), module, PERCO::OUT_BP));
		addOutput(createOutput<PJ301MPort>(Vec(85, 320), module, PERCO::OUT_HP));
	}
};

Model *modelPERCO = createModel<PERCO, PERCOWidget>("pErCO");
