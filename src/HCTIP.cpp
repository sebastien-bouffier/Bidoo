#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "Svf.hpp"
#include "dep/filters/pitchshifter.h"

using namespace std;

struct HCTIP : BidooModule {
	enum ParamIds {
		PITCH_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		PITCH_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	// one shifter per polyphonic channel, all made with the sample rate (about 140 kB each at 48 kHz)
	PitchShifter *pShifter[16] = {};
	// paused (nothing patched, or the channel unused): its buffers are cleared before it plays again
	bool paused[16];

	HCTIP() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PITCH_PARAM, -12.f, 12.f, 0.f, "Pitch", " semitones");
		configInput(INPUT, "Audio (polyphonic: one shifter per channel)");
		configInput(PITCH_INPUT, "Pitch CV (1V/oct, polyphonic: per channel; with a mono input, one shifted copy per channel)");
		for (bool &p : paused)
			p = true;
		configOutput(OUTPUT, "Audio (37 ms late: the analysis window)");
		configBypass(INPUT, OUTPUT);
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		// the same frequency resolution and hop time at any rate: 2048 at 44.1 /
		// 48 kHz, 4096 at 88.2 / 96 kHz, 8192 at 176.4 / 192 kHz
		int size = e.sampleRate >= 160000.f ? 8192 : (e.sampleRate >= 80000.f ? 4096 : 2048);
		for (int c = 0; c < 16; c++) {
			delete pShifter[c];
			pShifter[c] = new PitchShifter();
			pShifter[c]->init(size, 8, e.sampleRate);
			pShifter[c]->phaseLock = true;
			paused[c] = true;
		}
	}

	// the phase-locked shifter's gain per semitone from -12 to +24, measured
	// on sines from 110 Hz to 1760 Hz (within 3% across them): divided out.
	// Measured before the shifter divided out its windows' overlap (3 at 8x):
	// that part is gone, the rest of the table is the phase locking's own.
	static float levelFix(float semis) {
		static const float gain[37] = {
			1.791f, 1.919f, 2.018f, 2.101f, 2.191f, 2.250f, 2.359f, 2.381f, 2.562f, 2.658f, 2.739f, 2.763f,
			2.997f,
			2.853f, 2.920f, 3.027f, 3.053f, 3.113f, 3.190f, 3.185f, 3.270f, 3.296f, 3.322f, 3.341f, 3.437f,
			3.393f, 3.399f, 3.424f, 3.452f, 3.455f, 3.472f, 3.494f, 3.494f, 3.501f, 3.511f, 3.531f, 3.563f
		};
		float x = clamp(semis, -12.f, 24.f) + 12.f;
		int i = std::min((int)x, 35);
		return 3.0f / (gain[i] + (gain[i + 1] - gain[i]) * (x - i));
	}

	void process(const ProcessArgs &args) override {
		if (!pShifter[0])
			return;
		if (!outputs[OUTPUT].isConnected() || !inputs[INPUT].isConnected()) {
			outputs[OUTPUT].setChannels(1);
			outputs[OUTPUT].setVoltage(0.f);
			for (bool &p : paused)
				p = true;
			return;
		}
		// as many channels as the audio or the pitch CV has: a mono input with a polyphonic pitch CV
		// is a harmoniser (one shifted copy per CV channel)
		const int channels = std::max(1, std::max(inputs[INPUT].getChannels(), inputs[PITCH_INPUT].getChannels()));
		for (int c = channels; c < 16; c++)
			paused[c] = true;
		for (int c = 0; c < channels; c++) {
			if (paused[c]) {
				pShifter[c]->clear();
				paused[c] = false;
			}
			float semis = params[PITCH_PARAM].getValue() + inputs[PITCH_INPUT].getPolyVoltage(c) * 12.f;
			semis = clamp(semis, -12.f, 24.f);
			float ratio = dsp::exp2_taylor5(semis / 12.f);
			// one 256-sample hop of FFT work at a time instead of eight at once
			float y = pShifter[c]->step(ratio, inputs[INPUT].getPolyVoltage(c) * 0.2f);
			outputs[OUTPUT].setVoltage(bidooSoftLimit(y * 5.0f * levelFix(semis)), c);
		}
		outputs[OUTPUT].setChannels(channels);
	}

	~HCTIP() {
		for (PitchShifter *p : pShifter)
			delete p;
	}
};

struct HCTIPWidget : BidooWidget {
	HCTIPWidget(HCTIP *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/HCTIP.svg"));

		// No screws, as before. The original hand-drawn panel: one column down the middle, where the panel draws the line
		// from the knob to its CV jack and centres the IN and OUT labels. Each row keeps its old top; the column is the
		// panel's centre.
		const float kBoxPitch = 47.f;   // from the IN box to the OUT box
		const theme::Pos pitch = theme::sameRow(theme::specificCorner(8, 70, theme::kBlueKnobSize), box.size.x * 0.5f);
		const theme::Pos pitchCv = theme::colRow(pitch, theme::specificCorner(10, 130, theme::kJackSize));
		const theme::Pos in = theme::colRow(pitch, theme::specificCorner(10, 283, theme::kJackSize));
		addParam(theme::param<BidooBlueKnob>(pitch, module, HCTIP::PITCH_PARAM));
		addInput(theme::input<PJ301MPort>(pitchCv, module, HCTIP::PITCH_INPUT));
		addInput(theme::input<PJ301MPort>(in, module, HCTIP::INPUT));
		addOutput(theme::output<PJ301MPort>(in.down(kBoxPitch), module, HCTIP::OUTPUT));
	}
};

Model *modelHCTIP = createModel<HCTIP, HCTIPWidget>("HCTIP");
