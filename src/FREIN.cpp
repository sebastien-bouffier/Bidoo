#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "SampleRead.hpp"
#include <vector>

using namespace std;

struct FREIN : BidooModule {
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

	enum State { PLAY, BREAK, STOPPED, START, CATCHUP };

	dsp::SchmittTrigger trigTrigger;
	dsp::SchmittTrigger startTrigger;
	State state = PLAY;
	float speed = 1.0f;
	float speedStep = 0.0f;
	double head = 0.0;      // read position, in samples since the break or start
	long long written = 0;  // samples written since the break or start
	float catchup = 0.0f;   // crossfade from the delayed tape to the live input
	// one tape per channel; the read lags the input by at most half the
	// longest time (3 s), so 1.5 s of tape is enough
	std::vector<float> tape[PORT_MAX_CHANNELS];
	int tapeSize = 1;
	// Every change of state (a break or a start, even in the middle of one)
	// crossfades over 5 ms from the last output.
	State lastState = PLAY;
	float lastOut[PORT_MAX_CHANNELS] = {};
	float held[PORT_MAX_CHANNELS] = {};
	int xfLeft = 0, xfLen = 1;

	FREIN() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(TRIG_PARAM, "Break (tape stop)");
		configButton(START_PARAM, "Start (tape spin-up)");
		configParam(SPEED_PARAM, 0.5f, 3.0f, 1.0f, "Stop/start time", " s");
		configInput(INPUT, "Audio (polyphonic)");
		configInput(TRIG_INPUT, "Break trigger");
		configInput(START_INPUT, "Start trigger");
		configInput(SPEED_INPUT, "Time CV (0-10 V: +0-2.5 s)");
		configOutput(OUTPUT, "Audio");
		configBypass(INPUT, OUTPUT);
		resize(48000.f);
	}

	void resize(float sampleRate) {
		tapeSize = (int)(1.5f * sampleRate) + 8;
		for (auto &t : tape)
			t.assign(tapeSize, 0.0f);
		state = lastState = PLAY;
		speed = 1.0f;
		xfLeft = 0;
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		resize(e.sampleRate);
	}

	// 4-point Hermite on the tape (varispeed).
	float read(int c, double pos) const {
		long long i = (long long)pos;
		float f = (float)(pos - (double)i);
		const std::vector<float> &t = tape[c];
		return sampleread::hermite(t[(i + tapeSize - 1) % tapeSize], t[i % tapeSize], t[(i + 1) % tapeSize], t[(i + 2) % tapeSize], f);
	}

	void process(const ProcessArgs &args) override {
		float time = clamp(params[SPEED_PARAM].getValue() + inputs[SPEED_INPUT].getVoltage() * 0.25f, 0.5f, 3.0f);
		bool restarted = false;
		if (trigTrigger.process(params[TRIG_PARAM].getValue() + inputs[TRIG_INPUT].getVoltage())) {
			restarted = true;
			state = BREAK;
			speed = 1.0f;
			speedStep = 1.0f / (args.sampleRate * time);
			head = 0.0;
			written = 0;
		}
		if (startTrigger.process(params[START_PARAM].getValue() + inputs[START_INPUT].getVoltage()) && state != PLAY && state != CATCHUP) {
			restarted = true;
			state = START;
			speed = 0.0f;
			speedStep = 1.0f / (args.sampleRate * time);
			head = 0.0;
			written = 0;
		}

		int channels = std::max(1, inputs[INPUT].getChannels());
		if (restarted || state != lastState) {
			for (int c = 0; c < channels; c++)
				held[c] = lastOut[c];
			xfLen = std::max(1, (int)(0.005f * args.sampleRate));
			xfLeft = xfLen;
		}
		bool tapeRuns = state == BREAK || state == START || state == CATCHUP;
		if (tapeRuns) {
			int w = (int)(written % tapeSize);
			for (int c = 0; c < channels; c++)
				tape[c][w] = inputs[INPUT].getVoltage(c);
		}

		// fades in and out over the slowest tenth of the speed
		float gain = std::min(1.0f, speed * 10.0f);
		// back to the live input: an equal-power crossfade (the tape lags it)
		float tapeGain = 1.f, liveGain = 0.f;
		if (state == CATCHUP) {
			tapeGain = std::cos(0.5f * float(M_PI) * catchup);
			liveGain = std::sin(0.5f * float(M_PI) * catchup);
		}
		float xf = xfLeft > 0 ? 1.f - (float)xfLeft / (float)xfLen : 1.f;
		for (int c = 0; c < channels; c++) {
			float out;
			switch (state) {
				case PLAY: out = inputs[INPUT].getVoltage(c); break;
				case STOPPED: out = 0.0f; break;
				case CATCHUP: out = read(c, head) * tapeGain + inputs[INPUT].getVoltage(c) * liveGain; break;
				default: out = read(c, head) * gain; break;
			}
			if (xfLeft > 0)
				out = held[c] + (out - held[c]) * xf;
			lastOut[c] = out;
			outputs[OUTPUT].setVoltage(out, c);
		}
		if (xfLeft > 0)
			xfLeft--;
		lastState = state;
		outputs[OUTPUT].setChannels(channels);

		if (tapeRuns) {
			head += speed;
			written++;
		}
		if (state == BREAK) {
			speed -= speedStep;
			if (speed <= 0.0f) {
				speed = 0.0f;
				state = STOPPED;
			}
		}
		else if (state == START) {
			speed += speedStep;
			if (speed >= 1.0f) {
				// up to speed: the tape now lags the input, crossfade back to it
				speed = 1.0f;
				state = CATCHUP;
				catchup = 0.0f;
			}
		}
		else if (state == CATCHUP) {
			catchup += 1.0f / (0.01f * args.sampleRate);
			if (catchup >= 1.0f)
				state = PLAY;
		}

		bool moving = state == BREAK || state == START || state == CATCHUP;
		lights[BREAK_LIGHT].setBrightness(state == STOPPED ? 1.0f : 0.0f);
		lights[BREAK_LIGHT + 1].setBrightness(state == PLAY ? 1.0f : 0.0f);
		lights[BREAK_LIGHT + 2].setBrightness(moving ? 1.0f : 0.0f);
	}
};

struct FREINWidget : BidooWidget {
	FREINWidget(FREIN *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/FREIN.svg"));

		// No screws, as before. The original hand-drawn panel: one column down the middle, where the panel draws the lines
		// from each control to its CV jack and centres every label. The column is the stop button's (the panel's centre);
		// each control keeps its old top.
		const float kGroupPitch = 79.f;   // from one control and its CV jack to the next
		const float kBoxPitch = 47.f;     // from the IN box to the OUT box
		const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};   // SmallLight
		const theme::Pos stop = theme::specificCorner(8.5f, 50, theme::kCKD6Size);
		const theme::Pos stopCv = theme::colRow(stop, theme::specificCorner(15, 90, theme::kTinyJackSize));
		const theme::Pos speed = theme::colRow(stop, theme::specificCorner(7.5f, 208, theme::kBlueKnobSize));
		const theme::Pos in = theme::colRow(stop, theme::specificCorner(10, 283, theme::kJackSize));
		// the light keeps its old corner, 0.05 px left of the column
		addChild(theme::light<SmallLight<RedGreenBlueLight>>(theme::specificCorner(19.5f, 20, kSmallLightSize), module, FREIN::BREAK_LIGHT));

		addParam(theme::param<BlueCKD6>(stop, module, FREIN::TRIG_PARAM));
		addInput(theme::input<TinyPJ301MPort>(stopCv, module, FREIN::TRIG_INPUT));
		addParam(theme::param<BlueCKD6>(stop.down(kGroupPitch), module, FREIN::START_PARAM));
		addInput(theme::input<TinyPJ301MPort>(stopCv.down(kGroupPitch), module, FREIN::START_INPUT));
		addParam(theme::param<BidooBlueKnob>(speed, module, FREIN::SPEED_PARAM));
		addInput(theme::input<TinyPJ301MPort>(stopCv.down(2.f * kGroupPitch), module, FREIN::SPEED_INPUT));
		addInput(theme::input<PJ301MPort>(in, module, FREIN::INPUT));
		addOutput(theme::output<PJ301MPort>(in.down(kBoxPitch), module, FREIN::OUTPUT));
	}
};

Model *modelFREIN = createModel<FREIN, FREINWidget>("FREIN");
