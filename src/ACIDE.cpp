// aCiDe: a mono acid bass voice, oscillator, diode ladder, envelopes, accent, slide and VCA. The sound is all in
// bidoo-dsp (synth/AcidVoice.h, filters/DiodeLadder.h, copied unchanged in src/dep/bidoo-dsp); this is the panel and the
// jacks. One voice: a polyphonic cable plays its channel 1.
#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"
#include "dep/bidoo-dsp/bidoo/dsp/synth/AcidVoice.h"

struct ACIDE : BidooModule {
	enum ParamIds {
		TUNE_PARAM,
		WAVE_PARAM,
		VOLUME_PARAM,
		CUTOFF_PARAM,
		RES_PARAM,
		ENVMOD_PARAM,
		DECAY_PARAM,
		ACCENT_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		VOCT_INPUT,
		GATE_INPUT,
		ACCENT_INPUT,
		SLIDE_INPUT,
		CUTOFF_INPUT,
		RES_INPUT,
		ENVMOD_INPUT,
		DECAY_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	static constexpr float kVolts = 5.f;
	static constexpr int kControl = 16;  // knobs and CV are read every 16 samples: the voice glides between them

	bidoo::dsp::AcidVoice voice;
	dsp::SchmittTrigger gateIn, accentIn, slideIn;
	int tick = 0;
	float volume = 0.f, volumeStep = 0.f;

	ACIDE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(TUNE_PARAM, -12.f, 12.f, 0.f, "Tune", " semitones");
		configSwitch(WAVE_PARAM, 0.f, 1.f, 0.f, "Wave", {"Saw", "Square"});
		configParam(VOLUME_PARAM, 0.f, 1.f, 0.8f, "Volume", "%", 0.f, 100.f);
		configParam(CUTOFF_PARAM, 0.f, 1.f, 0.4f, "Cutoff", " Hz", 12.5f, 200.f);
		configParam(RES_PARAM, 0.f, 1.f, 0.5f, "Resonance", "%", 0.f, 100.f);
		configParam(ENVMOD_PARAM, 0.f, 1.f, 0.5f, "Env mod", "%", 0.f, 100.f);
		configParam(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay", " s", 10.f, 0.2f);
		configParam(ACCENT_PARAM, 0.f, 1.f, 0.5f, "Accent", "%", 0.f, 100.f);
		configInput(VOCT_INPUT, "V/oct (0 V = C4; polyphonic: channel 1)");
		configInput(GATE_INPUT, "Gate");
		configInput(ACCENT_INPUT, "Accent (high at the note's start; a gate or a CV above 1 V)");
		configInput(SLIDE_INPUT, "Slide (high when the gate falls or at the next gate: the notes are tied and glide)");
		configInput(CUTOFF_INPUT, "Cutoff CV, 1 V/oct");
		configInput(RES_INPUT, "Resonance CV (10 V = full range)");
		configInput(ENVMOD_INPUT, "Env mod CV (10 V = full range)");
		configInput(DECAY_INPUT, "Decay CV (10 V = full range)");
		configOutput(OUT_OUTPUT, "Audio");
		prepare(APP->engine->getSampleRate());
	}

	void prepare(float fs) {
		voice.prepare(fs);
		tick = 0;
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		prepare(e.sampleRate);
	}

	void onReset(const ResetEvent &e) override {
		BidooModule::onReset(e);
		voice.reset();
	}

	float cv(int input) {
		const float v = inputs[input].getVoltage();
		return std::isfinite(v) ? v : 0.f;
	}

	void control() {
		const float res = clamp(params[RES_PARAM].getValue() + cv(RES_INPUT) * 0.1f, 0.f, 1.f);
		const float env = clamp(params[ENVMOD_PARAM].getValue() + cv(ENVMOD_INPUT) * 0.1f, 0.f, 1.f);
		const float decay = clamp(params[DECAY_PARAM].getValue() + cv(DECAY_INPUT) * 0.1f, 0.f, 1.f);
		voice.setSquare(params[WAVE_PARAM].getValue() > 0.5f);
		voice.setTune(params[TUNE_PARAM].getValue() / 12.f);
		voice.setCutoff(200.f * std::pow(12.5f, params[CUTOFF_PARAM].getValue()) * std::pow(2.f, clamp(cv(CUTOFF_INPUT), -10.f, 10.f)));
		voice.setResonance(res);
		voice.setEnvMod(env);
		voice.setDecay(0.2f * std::pow(10.f, decay));
		voice.setAccent(params[ACCENT_PARAM].getValue());
		volumeStep = (params[VOLUME_PARAM].getValue() - volume) / kControl;
	}

	void process(const ProcessArgs &args) override {
		if (--tick <= 0) {
			tick = kControl;
			control();
		}
		gateIn.process(inputs[GATE_INPUT].getVoltage(), 0.1f, 1.f);
		accentIn.process(inputs[ACCENT_INPUT].getVoltage(), 0.5f, 1.f);
		slideIn.process(inputs[SLIDE_INPUT].getVoltage(), 0.5f, 1.f);
		float y = voice.process(clamp(cv(VOCT_INPUT), -10.f, 10.f), gateIn.isHigh(), accentIn.isHigh(), slideIn.isHigh());
		if (!std::isfinite(y) || !voice.isFinite()) {
			voice.reset();
			y = 0.f;
		}
		volume += volumeStep;
		outputs[OUT_OUTPUT].setVoltage(bidooSoftLimit(y * volume * kVolts));
	}
};

struct ACIDEWidget : BidooWidget {
	ACIDEWidget(ACIDE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/ACIDE.svg"));

		theme::addScrews(this);

		static const float kx[3] = {27.f, 75.f, 123.f};
		static const float ky[3] = {72.f, 142.f, 212.f};
		addParam(createParamCentered<BidooBlueKnob>(Vec(kx[0], ky[0]), module, ACIDE::TUNE_PARAM));
		addParam(createParamCentered<CKSS>(Vec(kx[1], ky[0]), module, ACIDE::WAVE_PARAM));
		addParam(createParamCentered<BidooBlueKnob>(Vec(kx[2], ky[0]), module, ACIDE::VOLUME_PARAM));
		addParam(createParamCentered<BidooBlueKnob>(Vec(kx[0], ky[1]), module, ACIDE::CUTOFF_PARAM));
		addParam(createParamCentered<BidooBlueKnob>(Vec(kx[1], ky[1]), module, ACIDE::RES_PARAM));
		addParam(createParamCentered<BidooBlueKnob>(Vec(kx[2], ky[1]), module, ACIDE::ENVMOD_PARAM));
		addParam(createParamCentered<BidooBlueKnob>(Vec(kx[0], ky[2]), module, ACIDE::DECAY_PARAM));
		addParam(createParamCentered<BidooBlueKnob>(Vec(kx[1], ky[2]), module, ACIDE::ACCENT_PARAM));
		addOutput(createOutputCentered<PJ301MPort>(Vec(kx[2], ky[2]), module, ACIDE::OUT_OUTPUT));

		static const float jx[4] = {21.f, 57.f, 93.f, 129.f};
		static const int top[4] = {ACIDE::VOCT_INPUT, ACIDE::GATE_INPUT, ACIDE::ACCENT_INPUT, ACIDE::SLIDE_INPUT};
		static const int low[4] = {ACIDE::CUTOFF_INPUT, ACIDE::RES_INPUT, ACIDE::ENVMOD_INPUT, ACIDE::DECAY_INPUT};
		for (int i = 0; i < 4; i++) {
			addInput(createInputCentered<PJ301MPort>(Vec(jx[i], theme::kSecondRowY), module, top[i]));
			addInput(createInputCentered<PJ301MPort>(Vec(jx[i], theme::kBottomRowY), module, low[i]));
		}
	}
};

Model *modelACIDE = createModel<ACIDE, ACIDEWidget>("ACIDE");
