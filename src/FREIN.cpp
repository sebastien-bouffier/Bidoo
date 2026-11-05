#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
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
		state = PLAY;
		speed = 1.0f;
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		resize(e.sampleRate);
	}

	float read(int c, double pos) const {
		long long i = (long long)pos;
		float f = (float)(pos - (double)i);
		float a = tape[c][i % tapeSize], b = tape[c][(i + 1) % tapeSize];
		return a + (b - a) * f;
	}

	void process(const ProcessArgs &args) override {
		float time = clamp(params[SPEED_PARAM].getValue() + inputs[SPEED_INPUT].getVoltage() * 0.25f, 0.5f, 3.0f);
		if (trigTrigger.process(params[TRIG_PARAM].getValue() + inputs[TRIG_INPUT].getVoltage())) {
			state = BREAK;
			speed = 1.0f;
			speedStep = 1.0f / (args.sampleRate * time);
			head = 0.0;
			written = 0;
		}
		if (startTrigger.process(params[START_PARAM].getValue() + inputs[START_INPUT].getVoltage()) && state != PLAY && state != CATCHUP) {
			state = START;
			speed = 0.0f;
			speedStep = 1.0f / (args.sampleRate * time);
			head = 0.0;
			written = 0;
		}

		int channels = std::max(1, inputs[INPUT].getChannels());
		bool tapeRuns = state == BREAK || state == START || state == CATCHUP;
		if (tapeRuns) {
			int w = (int)(written % tapeSize);
			for (int c = 0; c < channels; c++)
				tape[c][w] = inputs[INPUT].getVoltage(c);
		}

		// fades in and out over the slowest tenth of the speed
		float gain = std::min(1.0f, speed * 10.0f);
		for (int c = 0; c < channels; c++) {
			float out;
			switch (state) {
				case PLAY: out = inputs[INPUT].getVoltage(c); break;
				case STOPPED: out = 0.0f; break;
				case CATCHUP: out = read(c, head) + (inputs[INPUT].getVoltage(c) - read(c, head)) * catchup; break;
				default: out = read(c, head) * gain; break;
			}
			outputs[OUTPUT].setVoltage(out, c);
		}
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

		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(19.5f, 20.0f), module, FREIN::BREAK_LIGHT));

		addParam(createParam<BlueCKD6>(Vec(8.5f, 50), module, FREIN::TRIG_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(15, 90.f), module, FREIN::TRIG_INPUT));
		addParam(createParam<BlueCKD6>(Vec(8.5f, 129.f), module, FREIN::START_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(15, 169.f), module, FREIN::START_INPUT));
		addParam(createParam<BidooBlueKnob>(Vec(7.5f, 208.f), module, FREIN::SPEED_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(15, 248.f), module, FREIN::SPEED_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10, 283.0f), module, FREIN::INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10, 330), module, FREIN::OUTPUT));
	}
};

Model *modelFREIN = createModel<FREIN, FREINWidget>("FREIN");
