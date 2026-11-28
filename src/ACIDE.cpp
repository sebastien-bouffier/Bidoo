// aCiDe: an acid bass voice, oscillator, diode ladder, envelopes, accent, slide and VCA. The sound is all in
// bidoo-dsp (synth/AcidVoice.h, filters/DiodeLadder.h, copied unchanged in src/dep/bidoo-dsp); this is the panel and the
// jacks. Polyphonic: one voice per channel of V/OCT or GATE (each with its own accent, slide and CVs), a mono cable
// acting on every voice; with mono cables it is the mono voice it always was.
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

	bidoo::dsp::AcidVoice voices[16];
	dsp::SchmittTrigger gateIn[16], accentIn[16], slideIn[16];
	int channels = 1;
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
		configInput(VOCT_INPUT, "V/O (0 V = C4; polyphonic: one voice per channel)");
		configInput(GATE_INPUT, "Gate (polyphonic: one voice per channel)");
		configInput(ACCENT_INPUT, "Accent (high at the note's start; a gate or a CV above 1 V; polyphonic: per voice)");
		configInput(SLIDE_INPUT, "Slide (high when the gate falls or at the next gate: the notes are tied and glide; polyphonic: per voice)");
		configInput(CUTOFF_INPUT, "Cutoff CV, 1 V/oct (polyphonic: per voice)");
		configInput(RES_INPUT, "Resonance CV (10 V = full range; polyphonic: per voice)");
		configInput(ENVMOD_INPUT, "Env mod CV (10 V = full range; polyphonic: per voice)");
		configInput(DECAY_INPUT, "Decay CV (10 V = full range; polyphonic: per voice)");
		configOutput(OUT_OUTPUT, "Audio (one channel per voice)");
		prepare(APP->engine->getSampleRate());
	}

	void prepare(float fs) {
		for (auto &v : voices)
			v.prepare(fs);
		tick = 0;
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		prepare(e.sampleRate);
	}

	void onReset(const ResetEvent &e) override {
		BidooModule::onReset(e);
		for (auto &v : voices)
			v.reset();
	}

	float cv(int input, int c) {
		const float v = inputs[input].getPolyVoltage(c);
		return std::isfinite(v) ? v : 0.f;
	}

	void control(int c) {
		bidoo::dsp::AcidVoice &voice = voices[c];
		const float res = clamp(params[RES_PARAM].getValue() + cv(RES_INPUT, c) * 0.1f, 0.f, 1.f);
		const float env = clamp(params[ENVMOD_PARAM].getValue() + cv(ENVMOD_INPUT, c) * 0.1f, 0.f, 1.f);
		const float decay = clamp(params[DECAY_PARAM].getValue() + cv(DECAY_INPUT, c) * 0.1f, 0.f, 1.f);
		voice.setSquare(params[WAVE_PARAM].getValue() > 0.5f);
		voice.setTune(params[TUNE_PARAM].getValue() / 12.f);
		voice.setCutoff(200.f * std::pow(12.5f, params[CUTOFF_PARAM].getValue()) * std::pow(2.f, clamp(cv(CUTOFF_INPUT, c), -10.f, 10.f)));
		voice.setResonance(res);
		voice.setEnvMod(env);
		voice.setDecay(0.2f * std::pow(10.f, decay));
		voice.setAccent(params[ACCENT_PARAM].getValue());
	}

	void process(const ProcessArgs &args) override {
		// as many voices as V/OCT or GATE has channels; a voice that comes back starts fresh
		const int n = clamp(std::max(inputs[VOCT_INPUT].getChannels(), inputs[GATE_INPUT].getChannels()), 1, 16);
		for (int c = channels; c < n; c++) {
			voices[c].reset();
			gateIn[c].reset();
			accentIn[c].reset();
			slideIn[c].reset();
		}
		const bool newVoices = n > channels;
		channels = n;
		if (--tick <= 0 || newVoices) {
			tick = kControl;
			for (int c = 0; c < n; c++)
				control(c);
			volumeStep = (params[VOLUME_PARAM].getValue() - volume) / kControl;
		}
		volume += volumeStep;
		for (int c = 0; c < n; c++) {
			bidoo::dsp::AcidVoice &voice = voices[c];
			gateIn[c].process(inputs[GATE_INPUT].getPolyVoltage(c), 0.1f, 1.f);
			accentIn[c].process(inputs[ACCENT_INPUT].getPolyVoltage(c), 0.5f, 1.f);
			slideIn[c].process(inputs[SLIDE_INPUT].getPolyVoltage(c), 0.5f, 1.f);
			float y = voice.process(clamp(cv(VOCT_INPUT, c), -10.f, 10.f), gateIn[c].isHigh(), accentIn[c].isHigh(), slideIn[c].isHigh());
			if (!std::isfinite(y) || !voice.isFinite()) {
				voice.reset();
				y = 0.f;
			}
			outputs[OUT_OUTPUT].setVoltage(bidooSoftLimit(y * volume * kVolts), c);
		}
		outputs[OUT_OUTPUT].setChannels(n);
	}
};

struct ACIDEWidget : BidooWidget {
	ACIDEWidget(ACIDE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/ACIDE.svg"));

		theme::addScrews(this);

		// Placed as tools/panels/acide.py draws them: the knobs on its KX/KY columns and rows, specific to this panel
		// (the grid's columns don't fall there), the jacks on its JX columns and on the grid's two jack rows.
		static const float kx[3] = {27.f, 75.f, 123.f};   // acide.py KX
		static const float ky[3] = {72.f, 142.f, 212.f};  // acide.py KY
		static const float jx[4] = {21.f, 57.f, 93.f, 129.f};   // acide.py JX
		auto knob = [&](int c, int r) { return theme::specific(kx[c], ky[r]); };
		addParam(theme::param<BidooBlueKnob>(knob(0, 0), module, ACIDE::TUNE_PARAM));
		addParam(theme::param<CKSS>(knob(1, 0), module, ACIDE::WAVE_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(2, 0), module, ACIDE::VOLUME_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(0, 1), module, ACIDE::CUTOFF_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(1, 1), module, ACIDE::RES_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(2, 1), module, ACIDE::ENVMOD_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(0, 2), module, ACIDE::DECAY_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(1, 2), module, ACIDE::ACCENT_PARAM));
		addOutput(theme::output<PJ301MPort>(knob(2, 2), module, ACIDE::OUT_OUTPUT));   // in the red box where the ninth knob would be

		static const int top[4] = {ACIDE::VOCT_INPUT, ACIDE::GATE_INPUT, ACIDE::ACCENT_INPUT, ACIDE::SLIDE_INPUT};
		static const int low[4] = {ACIDE::CUTOFF_INPUT, ACIDE::RES_INPUT, ACIDE::ENVMOD_INPUT, ACIDE::DECAY_INPUT};
		for (int i = 0; i < 4; i++) {
			addInput(theme::input<PJ301MPort>(theme::specific(jx[i], theme::row(1)), module, top[i]));
			addInput(theme::input<PJ301MPort>(theme::specific(jx[i], theme::row(0)), module, low[i]));
		}
	}
};

Model *modelACIDE = createModel<ACIDE, ACIDEWidget>("ACIDE");
