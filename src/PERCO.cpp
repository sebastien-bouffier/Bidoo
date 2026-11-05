// Based on Will Pirkle's courses & Vadim Zavalishin's book

#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"

struct PERCO : BidooModule {
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

	Svf filter[PORT_MAX_CHANNELS];
	SvfGainCache gain[PORT_MAX_CHANNELS];

	PERCO() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CUTOFF_PARAM, 0.f, 1.f, 1.f, "Cutoff", " Hz", std::pow(2, 10.f), dsp::FREQ_C4 / std::pow(2, 5.f));
		configParam(Q_PARAM, .1f, 1.f, .1f, "Resonance", "%", 0.f, 100.f);
		configParam(CMOD_PARAM, -1.f, 1.f, 0.f, "Cutoff CV amount", "%", 0.f, 100.f);
		configInput(IN, "Audio");
		configInput(CUTOFF_INPUT, "Cutoff CV (1V/oct at 100%)");
		configInput(Q_INPUT, "Resonance CV");
		configOutput(OUT_LP, "Low-pass");
		configOutput(OUT_BP, "Band-pass");
		configOutput(OUT_HP, "High-pass");
	}

	void process(const ProcessArgs &args) override {
		int channels = std::max(1, inputs[IN].getChannels());
		float freqCvParam = dsp::quadraticBipolar(params[CMOD_PARAM].getValue());
		float freqParam = params[CUTOFF_PARAM].getValue() * 10.f - 5.f;
		float qParam = params[Q_PARAM].getValue();

		for (int c = 0; c < channels; c++) {
			float pitch = freqParam + inputs[CUTOFF_INPUT].getPolyVoltage(c) * freqCvParam;
			float cutoff = clamp(dsp::FREQ_C4 * dsp::exp2_taylor5(clamp(pitch, -10.f, 10.f)), 1.f, dsp::FREQ_C4 * 32.f);
			float q = 10.0f * clamp(qParam + inputs[Q_INPUT].getPolyVoltage(c) * 0.2f, 0.1f, 1.0f);
			filter[c].set(gain[c].get(cutoff, args.sampleRate), q);
			filter[c].process(inputs[IN].getVoltage(c));
			outputs[OUT_LP].setVoltage(bidooSoftLimit(filter[c].lp), c);
			outputs[OUT_BP].setVoltage(bidooSoftLimit(filter[c].bp), c);
			outputs[OUT_HP].setVoltage(bidooSoftLimit(filter[c].hp), c);
		}
		for (int i = 0; i < NUM_OUTPUTS; i++)
			outputs[i].setChannels(channels);
	}

};

struct PERCOWidget : BidooWidget {
	PERCOWidget(PERCO *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/PERCO.svg"));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<BidooHugeBlueKnob>(Vec(31, 61), module, PERCO::CUTOFF_PARAM));
		addParam(createParam<BidooLargeBlueKnob>(Vec(12, 143), module, PERCO::Q_PARAM));
		addParam(createParam<BidooLargeBlueKnob>(Vec(71, 143), module, PERCO::CMOD_PARAM));
		///Innies
		addInput(createInput<PJ301MPort>(Vec(10, 283), module, PERCO::IN));
		addInput(createInput<PJ301MPort>(Vec(48, 283), module, PERCO::CUTOFF_INPUT));
		addInput(createInput<PJ301MPort>(Vec(85, 283), module, PERCO::Q_INPUT));
		///Outies
		addOutput(createOutput<PJ301MPort>(Vec(10, 330), module, PERCO::OUT_LP));
		addOutput(createOutput<PJ301MPort>(Vec(48, 330), module, PERCO::OUT_BP));
		addOutput(createOutput<PJ301MPort>(Vec(85, 330), module, PERCO::OUT_HP));
	}
};

Model *modelPERCO = createModel<PERCO, PERCOWidget>("pErCO");
