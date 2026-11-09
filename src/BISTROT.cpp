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

		theme::addScrews(this);

		// The original hand-drawn panel: the two clock jacks in their boxes, two strips of eight tiny jacks (bits out on
		// the red one, bits in on the black one) with a light beside each, IN and OUT. Old corners kept: every jack sits
		// within 0.3 px of its box or strip, and each light on its jack's row.
		const float kBitBelow = 26.f;     // from one bit to the next
		const float kRightColumn = 38.f;  // from the A/D column to the D/A column
		const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};   // SmallLight
		const theme::Pos adcClock = theme::specificCorner(29, 46, theme::kJackSize);
		addInput(theme::input<PJ301MPort>(adcClock, module, BISTROT::ADCCLOCK_INPUT));
		addInput(theme::input<PJ301MPort>(adcClock.right(kRightColumn), module, BISTROT::DACCLOCK_INPUT));
		const theme::Pos bitOut = theme::specificCorner(33.5f, 93, theme::kTinyJackSize);
		const theme::Pos inLight = theme::specificCorner(18, 97.5f, kSmallLightSize);
		const theme::Pos outLight = theme::specificCorner(95, 97.5f, kSmallLightSize);
		for (int i = 0; i < 8; i++) {
			addChild(theme::light<SmallLight<RedLight>>(inLight.down(kBitBelow * i), module, BISTROT::BIT_INPUT_LIGHTS + i));
			addOutput(theme::output<TinyPJ301MPort>(bitOut.down(kBitBelow * i), module, BISTROT::BIT_OUTPUT + i));
			addInput(theme::input<TinyPJ301MPort>(bitOut.offset(kRightColumn, kBitBelow * i), module, BISTROT::BIT_INPUT + i));
			addChild(theme::light<SmallLight<BlueLight>>(outLight.down(kBitBelow * i), module, BISTROT::BIT_OUTPUT_LIGHTS + i));
		}
		const theme::Pos in = theme::specificCorner(29, 330, theme::kJackSize);
		addInput(theme::input<PJ301MPort>(in, module, BISTROT::INPUT));
		addOutput(theme::output<PJ301MPort>(in.right(kRightColumn), module, BISTROT::OUTPUT));
	}
};

Model *modelBISTROT = createModel<BISTROT, BISTROTWidget>("BISTROT");
