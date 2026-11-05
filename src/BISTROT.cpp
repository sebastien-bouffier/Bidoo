#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

// An 8-bit ADC and DAC in sign + magnitude over +-5 V: bit 8 is the sign,
// bits 1-7 the level in 127 steps.
struct BISTROT : BidooModule {
	enum ParamIds {
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		ADCCLOCK_INPUT,
		DACCLOCK_INPUT,
		BIT_INPUT,
		NUM_INPUTS = BIT_INPUT + 8
	};
	enum OutputIds {
		OUTPUT,
		BIT_OUTPUT,
		NUM_OUTPUTS  = BIT_OUTPUT + 8
	};
	enum LightIds {
		BIT_INPUT_LIGHTS,
		BIT_OUTPUT_LIGHTS = BIT_INPUT_LIGHTS + 8,
		NUM_LIGHTS = BIT_OUTPUT_LIGHTS + 8
	};

	dsp::SchmittTrigger adcClockTrigger[PORT_MAX_CHANNELS], dacClockTrigger[PORT_MAX_CHANNELS];
	unsigned char in[PORT_MAX_CHANNELS] = {};
	unsigned char out[PORT_MAX_CHANNELS] = {};

	BISTROT() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(INPUT, "Audio (polyphonic, +-5 V full scale)");
		configInput(ADCCLOCK_INPUT, "ADC clock (unpatched: every sample)");
		configInput(DACCLOCK_INPUT, "DAC clock (unpatched: every sample)");
		configOutput(OUTPUT, "Audio");
		for (int i = 0; i < 8; i++) {
			std::string bit = i == 7 ? "Bit 8 (sign)" : "Bit " + std::to_string(i + 1);
			configOutput(BIT_OUTPUT + i, "ADC " + bit);
			configInput(BIT_INPUT + i, "DAC " + bit + " (high from 1 V; unpatched: follows the ADC)");
		}
	}

	static unsigned char encode(float v) {
		float a = std::min(std::fabs(v) * (127.0f / 5.0f), 127.0f);
		return (unsigned char)std::lround(a) | (v < 0.0f ? 0x80 : 0);
	}

	static float decode(unsigned char b) {
		float a = (b & 0x7F) * (5.0f / 127.0f);
		return (b & 0x80) ? -a : a;
	}

	void process(const ProcessArgs &args) override;
};

void BISTROT::process(const ProcessArgs &args) {
	int channels = std::max(1, inputs[INPUT].getChannels());
	for (int i = 0; i < 8; i++)
		channels = std::max(channels, inputs[BIT_INPUT + i].getChannels());

	for (int c = 0; c < channels; c++) {
		if (!inputs[ADCCLOCK_INPUT].isConnected() || adcClockTrigger[c].process(inputs[ADCCLOCK_INPUT].getPolyVoltage(c)))
			in[c] = encode(inputs[INPUT].getVoltage(c));
		for (int i = 0; i < 8; i++)
			outputs[BIT_OUTPUT + i].setVoltage((in[c] >> i) & 1 ? 10.0f : 0.0f, c);

		if (!inputs[DACCLOCK_INPUT].isConnected() || dacClockTrigger[c].process(inputs[DACCLOCK_INPUT].getPolyVoltage(c))) {
			unsigned char b = 0;
			for (int i = 0; i < 8; i++) {
				bool high = inputs[BIT_INPUT + i].isConnected() ? inputs[BIT_INPUT + i].getPolyVoltage(c) >= 1.0f : ((in[c] >> i) & 1);
				if (high)
					b |= 1 << i;
			}
			out[c] = b;
		}
		outputs[OUTPUT].setVoltage(decode(out[c]), c);
	}
	outputs[OUTPUT].setChannels(channels);
	for (int i = 0; i < 8; i++)
		outputs[BIT_OUTPUT + i].setChannels(channels);

	// the lights show the first channel
	for (int i = 0; i < 8; i++) {
		lights[BIT_INPUT_LIGHTS + i].setBrightness((in[0] >> i) & 1);
		lights[BIT_OUTPUT_LIGHTS + i].setBrightness((out[0] >> i) & 1);
	}
}

struct BISTROTWidget : BidooWidget {
	BISTROTWidget(BISTROT *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/BISTROT.svg"));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

    addInput(createInput<PJ301MPort>(Vec(29.0f, 46.0f), module, BISTROT::ADCCLOCK_INPUT));
    addInput(createInput<PJ301MPort>(Vec(67.0f, 46.0f), module, BISTROT::DACCLOCK_INPUT));

    for (int i = 0; i<8; i++) {
      addChild(createLight<SmallLight<RedLight>>(Vec(18.0f, 97.5f + 26.0f * i), module, BISTROT::BIT_INPUT_LIGHTS + i));
			addOutput(createOutput<TinyPJ301MPort>(Vec(33.5f, 93.0f + 26.0f * i), module, BISTROT::BIT_OUTPUT + i));
			addInput(createInput<TinyPJ301MPort>(Vec(71.5f, 93.0f + 26.0f * i), module, BISTROT::BIT_INPUT + i));
			addChild(createLight<SmallLight<BlueLight>>(Vec(95.0f, 97.5f + 26.0f * i), module, BISTROT::BIT_OUTPUT_LIGHTS + i));
    }

		addInput(createInput<PJ301MPort>(Vec(29.0f, 330.0f), module, BISTROT::INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(67.0f, 330.0f), module, BISTROT::OUTPUT));
	}
};

Model *modelBISTROT = createModel<BISTROT, BISTROTWidget>("BISTROT");
