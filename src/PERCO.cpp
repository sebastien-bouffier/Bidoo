// Based on Will Pirkle's courses & Vadim Zavalishin's book

#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"

// The cutoff knob: the same law as before (8 Hz at 0, 1 octave per 10%) up to
// 80% (2.1 kHz), then the last 20% reach 21.6 kHz instead of 8.4 kHz (kept
// under 0.45 x the sample rate).
static constexpr float PERCO_KNEE = 0.8f;
static constexpr float PERCO_TOP = 6.369f;  // octaves above C4 at the top: 21.6 kHz

inline float percoKnobPitch(float v) {
	float pitch = v * 10.f - 5.f;
	if (v > PERCO_KNEE)
		pitch += (v - PERCO_KNEE) / (1.f - PERCO_KNEE) * (PERCO_TOP - 5.f);
	return pitch;
}

struct PercoCutoffQuantity : ParamQuantity {
	float getDisplayValue() override {
		return dsp::FREQ_C4 * std::pow(2.f, percoKnobPitch(getValue()));
	}
	void setDisplayValue(float hz) override {
		float pitch = std::log2(std::max(hz, 1.f) / dsp::FREQ_C4);
		float atKnee = percoKnobPitch(PERCO_KNEE);
		float v = pitch <= atKnee ? (pitch + 5.f) / 10.f
			: PERCO_KNEE + (pitch - atKnee) / (PERCO_TOP - atKnee) * (1.f - PERCO_KNEE);
		setValue(clamp(v, 0.f, 1.f));
	}
};

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
		NUM_LIGHTS
	};

	Svf filter[PORT_MAX_CHANNELS];
	SvfGainCache gain[PORT_MAX_CHANNELS];

	PERCO() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<PercoCutoffQuantity>(CUTOFF_PARAM, 0.f, 1.f, 1.f, "Cutoff", " Hz");
		configParam(Q_PARAM, .1f, 1.f, .1f, "Resonance", "%", 0.f, 100.f);
		configParam(CMOD_PARAM, -1.f, 1.f, 0.f, "Cutoff CV amount", "%", 0.f, 100.f);
		configInput(IN, "Audio (polyphonic)");
		configInput(CUTOFF_INPUT, "Cutoff CV (1V/oct at 100%)");
		configInput(Q_INPUT, "Resonance CV");
		configOutput(OUT_LP, "Low-pass");
		configOutput(OUT_BP, "Band-pass");
		configOutput(OUT_HP, "High-pass");
	}

	void process(const ProcessArgs &args) override {
		// as many channels as the busiest input
		int channels = std::max(1, std::max(inputs[IN].getChannels(), std::max(inputs[CUTOFF_INPUT].getChannels(), inputs[Q_INPUT].getChannels())));
		float freqCvParam = dsp::quadraticBipolar(params[CMOD_PARAM].getValue());
		float freqParam = percoKnobPitch(params[CUTOFF_PARAM].getValue());
		float qParam = params[Q_PARAM].getValue();

		for (int c = 0; c < channels; c++) {
			float pitch = freqParam + inputs[CUTOFF_INPUT].getPolyVoltage(c) * freqCvParam;
			// svfGain keeps it under 0.45 x the sample rate
			float cutoff = std::max(dsp::FREQ_C4 * dsp::exp2_taylor5(clamp(pitch, -10.f, 10.f)), 1.f);
			float q = 10.0f * clamp(qParam + inputs[Q_INPUT].getPolyVoltage(c) * 0.2f, 0.1f, 1.0f);
			filter[c].set(gain[c].get(cutoff, args.sampleRate), q);
			filter[c].process(inputs[IN].getPolyVoltage(c));
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
