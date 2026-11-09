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

		theme::addScrews(this);

		// The original hand-drawn panel: the cutoff knob, RES and GAIN either side below it, F.MOD under RES and the mode
		// switch beside it; three boxed CV jacks; the stereo in and out. Old corners kept (specific) except where a
		// control was off its label or box: GAIN mirrored from RES about the cutoff knob, the CV jacks on their boxes'
		// centres, each stereo jack under its L or R label.
		const theme::Size kCKSSSize = {14.f, 20.64111f};   // CKSS
		const theme::Pos cutoff = theme::specificCorner(32, 61, theme::kHugeKnobSize);
		const theme::Pos q = theme::specificCorner(12, 143, theme::kLargeKnobSize);
		const theme::Pos mug = q.right(2.f * (cutoff.x - q.x));   // RES mirrored about the cutoff knob
		addParam(theme::param<BidooHugeBlueKnob>(cutoff, module, LIMBO::CUTOFF_PARAM));
		addParam(theme::param<BidooLargeBlueKnob>(q, module, LIMBO::Q_PARAM));
		addParam(theme::param<BidooLargeBlueKnob>(mug, module, LIMBO::MUG_PARAM));
		addParam(theme::param<BidooLargeBlueKnob>(theme::specificCorner(12, 208, theme::kLargeKnobSize), module, LIMBO::CMOD_PARAM));
		addParam(theme::param<CKSS>(theme::specificCorner(82.5f, 217, kCKSSSize), module, LIMBO::MODE_PARAM));

		const float kBoxPitch = 40.844f;   // from one CV box's centre to the next
		const theme::Pos cutoffCv = theme::colRow(theme::specific(19.156f, 0), theme::specificCorner(7, 283, theme::kJackSize));   // the FREQ box's centre
		addInput(theme::input<PJ301MPort>(cutoffCv, module, LIMBO::CUTOFF_INPUT));
		addInput(theme::input<PJ301MPort>(cutoffCv.right(kBoxPitch), module, LIMBO::Q_INPUT));
		addInput(theme::input<PJ301MPort>(cutoffCv.right(2.f * kBoxPitch), module, LIMBO::MUG_INPUT));

		// Each stereo jack under its L or R label (the panel spaces the IN labels 23.5 px apart and the OUT ones 21.5)
		const theme::Pos stereoRow = theme::specificCorner(8, 340, theme::kTinyJackSize);
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specific(14.594f, 0), stereoRow), module, LIMBO::IN_L));
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specific(38.125f, 0), stereoRow), module, LIMBO::IN_R));
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(theme::specific(82.281f, 0), stereoRow), module, LIMBO::OUT_L));
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(theme::specific(103.812f, 0), stereoRow), module, LIMBO::OUT_R));
	}
};

Model *modelLIMBO = createModel<LIMBO, LIMBOWidget>("lIMbO");
