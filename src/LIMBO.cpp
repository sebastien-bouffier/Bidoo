// Based on Will Pirkle's courses & Vadim Zavalishin's book

#include "plugin.hpp"
#include "BidooComponents.hpp"

using namespace std;

// One-pole zero-delay-feedback stage. G = g / (1 + g) with g = tan(pi fc / fs).
struct FilterStage {
	float mem = 0.0f;

	float filter(float sample, float G, float gain, bool nonLinear) {
		float in = nonLinear ? std::tanh(sample * gain) / std::tanh(gain) : sample;
		float v = (in - mem) * G;
		float out = v + mem;
		mem = out + v;
		return out;
	}
};

// 4-pole ladder (Zavalishin): the feedback is solved from the stages' states,
// S = G^3 s1 + G^2 s2 + G s3 + s4 (each s divided by 1 + g).
struct LadderFilter {
	FilterStage stages[4];

	float process(float sample, float g, float k, float gain, bool nonLinear) {
		float G = g / (1.0f + g);
		float inv = 1.0f / (1.0f + g);
		float S = 0.0f;
		for (int i = 0; i < 4; i++)
			S = S * G + stages[i].mem * inv;
		float G4 = G * G * G * G;
		float u = (sample - k * S) / (1.0f + k * G4);
		for (int i = 0; i < 4; i++)
			u = stages[i].filter(u, G, gain, nonLinear);
		return u;
	}
};

// Linear up to 8 V, then bends to stay under 10 V (9.95 V at most).
inline float softLimit(float x) {
	float a = std::fabs(x);
	if (a <= 8.0f)
		return x;
	return std::copysign(8.0f + 1.95f * std::tanh((a - 8.0f) * 0.5f), x);
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
		LEARN_LIGHT,
		NUM_LIGHTS
	};

	LadderFilter lFilter[16], rFilter[16];

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
	}

	void process(const ProcessArgs &args) override {
		int channels = std::max(std::max(inputs[IN_L].getChannels(), inputs[IN_R].getChannels()), 1);
		bool rightIn = inputs[IN_R].isConnected();
		bool nonLinear = params[MODE_PARAM].getValue() > 0.5f;
		float fMax = 0.45f * args.sampleRate;
		float knobOct = 4.5f + 9.5f * params[CUTOFF_PARAM].getValue();
		float cmod = params[CMOD_PARAM].getValue();

		for (int c = 0; c < channels; c++) {
			float fc = clamp(dsp::exp2_taylor5(clamp(knobOct + cmod * inputs[CUTOFF_INPUT].getPolyVoltage(c), 3.0f, 15.0f)), 10.0f, fMax);
			float g = std::tan(float(M_PI) * fc * args.sampleTime);
			float k = 3.5f * clamp(params[Q_PARAM].getValue() + inputs[Q_INPUT].getPolyVoltage(c) * 0.2f, 0.0f, 1.0f);
			float boost = dsp::exp2_taylor5(3.0f * clamp(params[MUG_PARAM].getValue() + inputs[MUG_INPUT].getPolyVoltage(c) * 0.2f, 0.0f, 1.0f));
			float gain = boost / 3.0f;
			float outGain = 5.0f * (nonLinear ? 1.0f : boost);

			float inL = inputs[IN_L].getVoltage(c) * 0.2f;
			float inR = (rightIn ? inputs[IN_R].getVoltage(c) : inputs[IN_L].getVoltage(c)) * 0.2f;
			outputs[OUT_L].setVoltage(softLimit(lFilter[c].process(inL, g, k, gain, nonLinear) * outGain), c);
			outputs[OUT_R].setVoltage(softLimit(rFilter[c].process(inR, g, k, gain, nonLinear) * outGain), c);
		}
		outputs[OUT_L].setChannels(channels);
		outputs[OUT_R].setChannels(channels);
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
