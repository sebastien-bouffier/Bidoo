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

	///Tooltip
	struct tpCycle : ParamQuantity {
		std::string getDisplayValueString() override {
			if (getValue() < 1.f)
				return "Push";//press
			else
				return "Pushed";//pressed
		}
	};
	///Tooltip

	RABBIT() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int i = 0; i < 8; i++) {
			configParam<tpCycle>(BITOFF_PARAM + i, 0.f, 1.f, 0.f, "Bit " + std::to_string(i + 1));
			configParam<tpCycle>(BITREV_PARAM + i, 0.f, 1.f, 0.f, "Bit " + std::to_string(i + 1) + " Reverse");
		}

		memset(&bitOff, 0, 8 * sizeof(bool));
		memset(&bitRev, 0, 8 * sizeof(bool));
	}

	~RABBIT() {
	}

	void onRandomize() override {
		for (int i=0; i<8; i++) {
			bitOff[i]=random::uniform()>0.5f;
			bitRev[i]=random::uniform()>0.5f;
		}
	}

	void process(const ProcessArgs &args) override {
		float in_L = clamp(inputs[L_INPUT].getVoltage(), -10.0f, 10.0f);
		float in_R = clamp(inputs[R_INPUT].getVoltage(), -10.0f, 10.0f);

		in_L = roundf(clamp(in_L / 20.0f + 0.5f, 0.0f, 1.0f) * 255);//std
		in_R = roundf(clamp(in_R / 20.0f + 0.5f, 0.0f, 1.0f) * 255);

		unsigned char red_L = in_L;
		unsigned char red_R = in_R;

		for (int i = 0; i < 8; i++) {

			if (bitOffTrigger[i].process(params[BITOFF_PARAM + i].getValue() + inputs[BITOFF_INPUT + i].getVoltage())) {
				bitOff[i] = !bitOff[i];
			}

			if (bitRevTrigger[i].process(params[BITREV_PARAM + i].getValue() + inputs[BITREV_INPUT + i].getVoltage())) {
				bitRev[i] = !bitRev[i];
			}

			if (bitOff[i]) {
				red_L &= ~(1 << i);
				red_R &= ~(1 << i);
			} else {
				if (bitRev[i]) {
					red_L ^= ~(1 << i);
					red_R ^= ~(1 << i);
				}
			}

			lights[BITOFF_LIGHTS + i].setBrightness(bitOff[i] ? 1.0f : 0.0f);//red
			lights[BITREV_LIGHTS + i].setBrightness(bitRev[i] ? 1.0f : 0.0f);//blue
		}

		outputs[L_OUTPUT].setVoltage(clamp(((((float)red_L / 255.0f)) - 0.5f)*20.0f, -10.0f, 10.0f));
		outputs[R_OUTPUT].setVoltage(clamp(((((float)red_R / 255.0f)) - 0.5f)*20.0f, -10.0f, 10.0f));
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

		for (int i = 0; i < 8; i++) {
			addParam(createLightParam<LEDLightBezel<RedLight>>(Vec(27.0f, 50.0f + 32.0f * i), module, RABBIT::BITOFF_PARAM + i, RABBIT::BITOFF_LIGHTS + i));

			addInput(createInput<TinyPJ301MPort>(Vec(8.0f, 54.0f + 32.0f * i), module, RABBIT::BITOFF_INPUT + i));
			addInput(createInput<TinyPJ301MPort>(Vec(83.0f, 54.0f + 32.0f * i), module, RABBIT::BITREV_INPUT + i));

			addParam(createLightParam<LEDLightBezel<BlueLight>>(Vec(57.0f, 50.0f + 32.0f * i), module, RABBIT::BITREV_PARAM + i, RABBIT::BITREV_LIGHTS + i));
		}

		addInput(createInput<TinyPJ301MPort>(Vec(8.0f, 340.0f), module, RABBIT::L_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(8.f+22.0f, 340.0f), module, RABBIT::R_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(60.0f, 340.0f), module, RABBIT::L_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(60.0f+22.0f, 340.0f), module, RABBIT::R_OUTPUT));
	}
};



Model *modelRABBIT = createModel<RABBIT, RABBITWidget>("rabBIT");
