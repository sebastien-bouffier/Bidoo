#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"

using namespace std;

struct RABBIT : BidooModule {
	enum ParamIds {
		BITOFF_PARAM,
		BITREV_PARAM = BITOFF_PARAM + 8,
		NUM_PARAMS = BITREV_PARAM + 8
	};
	enum InputIds {
		L_INPUT,
		R_INPUT,
		BITOFF_INPUT,
		BITREV_INPUT = BITOFF_INPUT + 8,
		NUM_INPUTS = BITREV_INPUT + 8
	};
	enum OutputIds {
		L_OUTPUT,
		R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		BITOFF_LIGHTS,
		BITREV_LIGHTS = BITOFF_LIGHTS + 8,
		NUM_LIGHTS = BITREV_LIGHTS + 8
	};

	dsp::SchmittTrigger bitOffTrigger[8], bitRevTrigger[8];

	bool bitOff[8];
	bool bitRev[8];
	float crushMix = 0.f;   // 0: the input clean, 1: crushed

	RABBIT() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < 8; i++) {
			std::string bit = i == 7 ? "Bit 8 (sign)" : "Bit " + std::to_string(i + 1);
			configButton(BITOFF_PARAM + i, bit + " off");
			configButton(BITREV_PARAM + i, bit + " reverse");
			configInput(BITOFF_INPUT + i, bit + " off toggle trigger");
			configInput(BITREV_INPUT + i, bit + " reverse toggle trigger");
		}
		configInput(L_INPUT, "Left (polyphonic)");
		configInput(R_INPUT, "Right (polyphonic)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configBypass(L_INPUT, L_OUTPUT);
		configBypass(R_INPUT, R_OUTPUT);
		memset(&bitOff, 0, 8 * sizeof(bool));
		memset(&bitRev, 0, 8 * sizeof(bool));
	}

	void onReset() override {
		memset(&bitOff, 0, 8 * sizeof(bool));
		memset(&bitRev, 0, 8 * sizeof(bool));
	}

	void onRandomize() override {
		for (int i=0; i<8; i++) {
			bitOff[i]=random::uniform()>0.5f;
			bitRev[i]=random::uniform()>0.5f;
		}
	}

	// sign + magnitude over +-5 V: bit 8 is the sign, bits 1-7 the level in
	// 127 steps, so a bit turned off pulls the signal towards 0 V
	static unsigned char encode(float v) {
		float a = std::min(std::fabs(v) * (127.0f / 5.0f), 127.0f);
		return (unsigned char)std::lround(a) | (v < 0.0f ? 0x80 : 0);
	}

	static float decode(unsigned char b) {
		float a = (b & 0x7F) * (5.0f / 127.0f);
		return (b & 0x80) ? -a : a;
	}

	unsigned char crush(unsigned char b) const {
		for (int i = 0; i < 8; i++) {
			if (bitOff[i])
				b &= ~(1 << i);
			else if (bitRev[i])
				b ^= (1 << i);
		}
		return b;
	}

	void process(const ProcessArgs &args) override {
		for (int i = 0; i < 8; i++) {
			if (bitOffTrigger[i].process(params[BITOFF_PARAM + i].getValue() + inputs[BITOFF_INPUT + i].getVoltage()))
				bitOff[i] = !bitOff[i];
			if (bitRevTrigger[i].process(params[BITREV_PARAM + i].getValue() + inputs[BITREV_INPUT + i].getVoltage()))
				bitRev[i] = !bitRev[i];
			lights[BITOFF_LIGHTS + i].setBrightness(bitOff[i] ? 1.0f : 0.0f);
			lights[BITREV_LIGHTS + i].setBrightness(bitRev[i] ? 1.0f : 0.0f);
		}
		// with no bit switched the input passes clean; the crushing fades in
		// (5 ms) as soon as one bit is off or reversed
		bool active = false;
		for (int i = 0; i < 8; i++)
			active = active || bitOff[i] || bitRev[i];
		float step = 1.f / (0.005f * args.sampleRate);
		crushMix = active ? std::min(crushMix + step, 1.f) : std::max(crushMix - step, 0.f);
		for (int side = 0; side < 2; side++) {
			int channels = std::max(1, inputs[L_INPUT + side].getChannels());
			for (int c = 0; c < channels; c++) {
				float in = inputs[L_INPUT + side].getVoltage(c);
				float out = crushMix > 0.f ? in + (decode(crush(encode(in))) - in) * crushMix : in;
				outputs[L_OUTPUT + side].setVoltage(out, c);
			}
			outputs[L_OUTPUT + side].setChannels(channels);
		}
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		for (int i = 0; i < 8; i++) {
			json_object_set_new(rootJ, ("bitOff" + to_string(i)).c_str(), json_boolean(bitOff[i]));
			json_object_set_new(rootJ, ("bitRev" + to_string(i)).c_str(), json_boolean(bitRev[i]));
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		for (int i = 0; i < 8; i++) {
			json_t *jbitOff = json_object_get(rootJ, ("bitOff" + to_string(i)).c_str());
			if (jbitOff) {
				bitOff[i] = json_boolean_value(jbitOff);
			}
			json_t *jbitRev = json_object_get(rootJ, ("bitRev" + to_string(i)).c_str());
			if (jbitRev) {
				bitRev[i] = json_boolean_value(jbitRev);
			}
		}
	}

};

template <typename BASE>
struct RabbitLight : BASE {
	RabbitLight() {
		this->box.size = mm2px(Vec(6.0f, 6.0f));
	}
};

struct RABBITWidget : BidooWidget {
	RABBITWidget(RABBIT *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/RABBIT.svg"));

		// No screws, as before. The original hand-drawn panel: eight rows of an OFF jack and button, a REV button and
		// jack; the stereo in and out. The buttons keep their old corners; each row's jacks sit on their buttons' row
		// (the old code had them 0.87 px below it), and each stereo jack under its L or R label.
		const float kBitBelow = 32.f;
		const theme::Size kBezelSize = {21.26028f, 21.25987f};   // LEDLightBezel
		const theme::Pos offButton = theme::specificCorner(27, 50, kBezelSize);
		const theme::Pos revButton = theme::specificCorner(57, 50, kBezelSize);
		const theme::Pos offCvCol = theme::specificCorner(8, 54, theme::kTinyJackSize);
		const theme::Pos revCvCol = theme::specificCorner(83, 54, theme::kTinyJackSize);
		for (int i = 0; i < 8; i++) {
			const theme::Pos off = offButton.down(kBitBelow * i);
			addParam(theme::place(createLightParam<LEDLightBezel<RedLight>>(Vec(), module, RABBIT::BITOFF_PARAM + i, RABBIT::BITOFF_LIGHTS + i), off));
			addInput(theme::input<TinyPJ301MPort>(theme::colRow(offCvCol, theme::specific(0, off.y)), module, RABBIT::BITOFF_INPUT + i));
			addInput(theme::input<TinyPJ301MPort>(theme::colRow(revCvCol, theme::specific(0, off.y)), module, RABBIT::BITREV_INPUT + i));
			addParam(theme::place(createLightParam<LEDLightBezel<BlueLight>>(Vec(), module, RABBIT::BITREV_PARAM + i, RABBIT::BITREV_LIGHTS + i), revButton.down(kBitBelow * i)));
		}
		const theme::Pos stereoRow = theme::specificCorner(8, 340, theme::kTinyJackSize);
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specific(13.906f, 0), stereoRow), module, RABBIT::L_INPUT));
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specific(37.438f, 0), stereoRow), module, RABBIT::R_INPUT));
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(theme::specific(67.281f, 0), stereoRow), module, RABBIT::L_OUTPUT));
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(theme::specific(88.812f, 0), stereoRow), module, RABBIT::R_OUTPUT));
	}
};



Model *modelRABBIT = createModel<RABBIT, RABBITWidget>("rabBIT");
