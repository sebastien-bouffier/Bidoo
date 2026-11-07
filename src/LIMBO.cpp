// Based on Will Pirkle's courses & Vadim Zavalishin's book

#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"
#include "dep/bidoo-dsp/bidoo/dsp/shaping/TanhPade.h"

using namespace std;

// One-pole zero-delay-feedback stage. G = g / (1 + g) with g = tan(pi fc / fs).
struct FilterStage {
	float mem = 0.0f;

	float filter(float in, float G) {
		float v = (in - mem) * G;
		float out = v + mem;
		mem = out + v;
		return out;
	}
};

// What a channel's filter needs, worked out once per sample for all four
// stages (and both sides): G, the feedback, and the saturation's scaling.
struct LadderCoefs {
	float G = 0.0f, inv = 1.0f, G4 = 0.0f, k = 0.0f;
	float drive = 1.0f, invDrive = 1.0f;
	bool nonLinear = false;

	void set(float g, float kk, float d, bool nl) {
		G = g / (1.0f + g);
		inv = 1.0f / (1.0f + g);
		G4 = G * G * G * G;
		k = kk;
		nonLinear = nl;
		if (nl && d != drive) {
			drive = d;
			invDrive = 1.0f / d;
		}
	}
};

// 4-pole ladder (Zavalishin): the feedback is solved from the stages' states,
// S = G^3 s1 + G^2 s2 + G s3 + s4 (each s divided by 1 + g). Saturated, each
// stage's input goes through tanh(d x) / d: a slope of 1 at rest whatever the
// drive, so the gain boost no longer raises the resonance.
struct LadderFilter {
	FilterStage stages[4];

	float process(float sample, const LadderCoefs &c) {
		float S = 0.0f;
		for (int i = 0; i < 4; i++)
			S = S * c.G + stages[i].mem * c.inv;
		float u = (sample - c.k * S) / (1.0f + c.k * c.G4);
		if (c.nonLinear) {
			for (int i = 0; i < 4; i++)
				u = stages[i].filter(bidoo::dsp::tanhPade(u * c.drive) * c.invDrive, c.G);
		}
		else {
			for (int i = 0; i < 4; i++)
				u = stages[i].filter(u, c.G);
		}
		return u;
	}
};

// Linear up to 8 V, then bends to stay under 10 V (9.95 V at most).
inline float softLimit(float x) {
	return bidooSoftLimit(x);
}

struct LIMBO : BidooModule {
	enum ParamIds {
		CUTOFF_PARAM,
		Q_PARAM,
		CMOD_PARAM,
		MUG_PARAM,
		MODE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_L,
		IN_R,
		CUTOFF_INPUT,
		Q_INPUT,
		MUG_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L,
		OUT_R,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	LadderFilter lFilter[16], rFilter[16];
	// Saturated mode runs at twice the rate: the tanh's harmonics above
	// Nyquist are filtered out instead of folding back
	Halfband2x lOver[16], rOver[16];
	SvfGainCache gainCache[16];
	LadderCoefs coefs[16];
	// the saturation's drive and output gain, worked out when the boost moves
	float lastBoost[16], pre[16], post[16];
	bool wasNonLinear = false;

	LIMBO() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CUTOFF_PARAM, 0.0f, 1.0f, 1.0f, "Cutoff", " Hz", std::pow(2.0f, 9.5f), std::pow(2.0f, 4.5f));
		configParam(Q_PARAM, 0.0f, 1.0f, 0.0f, "Resonance", "%", 0.f, 100.f);
		configParam(MUG_PARAM, 0.0f, 1.0f, 0.0f, "Gain boost", "%", 0.f, 100.f);
		configParam(CMOD_PARAM, -1.0f, 1.0f, 0.0f, "Cutoff CV amount (100% = 1V/oct)", "%", 0.f, 100.f);
		configSwitch(MODE_PARAM, 0.0f, 1.0f, 0.0f, "Mode", {"Linear", "Saturated"});
		configInput(IN_L, "Left (polyphonic)");
		configInput(IN_R, "Right (normalled to left)");
		configInput(CUTOFF_INPUT, "Cutoff CV (1V/oct at 100%)");
		configInput(Q_INPUT, "Resonance CV (10 V = 200%)");
		configInput(MUG_INPUT, "Gain boost CV (10 V = 200%)");
		configOutput(OUT_L, "Left");
		configOutput(OUT_R, "Right");
		configBypass(IN_L, OUT_L);
		configBypass(IN_R, OUT_R);
		for (int c = 0; c < 16; c++) {
			lastBoost[c] = -1.0f;
			pre[c] = post[c] = 1.0f;
		}
	}

	void process(const ProcessArgs &args) override {
		int channels = std::max(std::max(inputs[IN_L].getChannels(), inputs[IN_R].getChannels()), 1);
		bool rightIn = inputs[IN_R].isConnected();
		bool nonLinear = params[MODE_PARAM].getValue() > 0.5f;
		if (nonLinear != wasNonLinear) {
			// the oversampling filters start from rest
			for (int c = 0; c < 16; c++) {
				lOver[c].reset();
				rOver[c].reset();
			}
			wasNonLinear = nonLinear;
		}
		// saturated, the ladder runs at 2x
		float rate = nonLinear ? 2.0f * args.sampleRate : args.sampleRate;
		float fMax = 0.45f * args.sampleRate;
		float knobOct = 4.5f + 9.5f * params[CUTOFF_PARAM].getValue();
		float cmod = params[CMOD_PARAM].getValue();
		float qKnob = params[Q_PARAM].getValue();
		float boostKnob = params[MUG_PARAM].getValue();

		for (int c = 0; c < channels; c++) {
			float fc = clamp(dsp::exp2_taylor5(clamp(knobOct + cmod * inputs[CUTOFF_INPUT].getPolyVoltage(c), 3.0f, 15.0f)), 10.0f, fMax);
			float g = gainCache[c].get(fc, rate);
			float k = 3.5f * clamp(qKnob + inputs[Q_INPUT].getPolyVoltage(c) * 0.2f, 0.0f, 1.0f);
			float boostIn = clamp(boostKnob + inputs[MUG_INPUT].getPolyVoltage(c) * 0.2f, 0.0f, 1.0f);
			float boost = dsp::exp2_taylor5(3.0f * boostIn);
			// saturated: the stages saturate from 1 / d (d = boost / 3, as before);
			// the input is raised by s^3 and the output by s, s = d / tanh(d): the
			// gain and the ceiling of the old stages (each had a slope of s), the
			// resonance of the linear filter
			if (nonLinear && boostIn != lastBoost[c]) {
				lastBoost[c] = boostIn;
				float d = boost / 3.0f;
				float s = d / std::tanh(d);
				pre[c] = s * s * s;
				post[c] = s;
			}
			coefs[c].set(g, k, boost / 3.0f, nonLinear);
			float inGain = nonLinear ? 0.2f * pre[c] : 0.2f;
			float outGain = 5.0f * (nonLinear ? post[c] : boost);

			float inL = inputs[IN_L].getPolyVoltage(c) * inGain;
			float yL = run(lFilter[c], lOver[c], inL, coefs[c]);
			outputs[OUT_L].setVoltage(softLimit(yL * outGain), c);
			if (rightIn) {
				float inR = inputs[IN_R].getPolyVoltage(c) * inGain;
				float yR = run(rFilter[c], rOver[c], inR, coefs[c]);
				outputs[OUT_R].setVoltage(softLimit(yR * outGain), c);
			}
			else {
				// the right side would be the same filter on the same input
				outputs[OUT_R].setVoltage(outputs[OUT_L].getVoltage(c), c);
			}
		}
		outputs[OUT_L].setChannels(channels);
		outputs[OUT_R].setChannels(channels);
	}

	static float run(LadderFilter &f, Halfband2x &over, float x, const LadderCoefs &c) {
		if (!c.nonLinear)
			return f.process(x, c);
		float a, b;
		over.up(x, a, b);
		a = f.process(a, c);
		b = f.process(b, c);
		return over.down(a, b);
	}
};

struct LIMBOWidget : BidooWidget {
	LIMBOWidget(LIMBO *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/LIMBO.svg"));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<BidooHugeBlueKnob>(Vec(32, 61), module, LIMBO::CUTOFF_PARAM));
		addParam(createParam<BidooLargeBlueKnob>(Vec(12, 143), module, LIMBO::Q_PARAM));
		addParam(createParam<BidooLargeBlueKnob>(Vec(71, 143), module, LIMBO::MUG_PARAM));
		addParam(createParam<BidooLargeBlueKnob>(Vec(12, 208), module, LIMBO::CMOD_PARAM));
		addParam(createParam<CKSS>(Vec(82.5f, 217), module, LIMBO::MODE_PARAM));

		addInput(createInput<PJ301MPort>(Vec(7, 283), module, LIMBO::CUTOFF_INPUT));
		addInput(createInput<PJ301MPort>(Vec(48, 283), module, LIMBO::Q_INPUT));
		addInput(createInput<PJ301MPort>(Vec(88.5f, 283), module, LIMBO::MUG_INPUT));

		addInput(createInput<TinyPJ301MPort>(Vec(8.f, 340), module, LIMBO::IN_L));
		addInput(createInput<TinyPJ301MPort>(Vec(8.f+22.f, 340), module, LIMBO::IN_R));

		addOutput(createOutput<TinyPJ301MPort>(Vec(75.f, 340), module, LIMBO::OUT_L));
		addOutput(createOutput<TinyPJ301MPort>(Vec(75.f+22.f, 340), module, LIMBO::OUT_R));
	}
};

Model *modelLIMBO = createModel<LIMBO, LIMBOWidget>("lIMbO");
