#include "plugin.hpp"
#include "BidooComponents.hpp"
#include <math.h>

using namespace std;

struct DILEMO : BidooModule {
	enum ParamIds {
		THRESHOLD_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN1_AND,
		IN2_AND,
		IN_NOT,
    IN1_OR,
		IN2_OR,
		IN1_XOR,
		IN2_XOR,
		NUM_INPUTS
	};
	enum OutputIds {
		AND_OUT,
		NAND_OUT,
		OR_OUT,
		NOR_OUT,
		XOR_OUT,
		XNOR_OUT,
		NOT_OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	// each input's state per channel; goes high above threshold + 0.1 V and low at or below
	// the threshold, so 0/10 V gates work at the default 0 V threshold
	bool high[NUM_INPUTS][PORT_MAX_CHANNELS] = {};

	DILEMO() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(THRESHOLD_PARAM, 0.0f, 10.0f, 0.0f, "Threshold", " V");
		configInput(IN1_AND, "AND input 1");
		configInput(IN2_AND, "AND input 2");
		configInput(IN_NOT, "NOT input");
		configInput(IN1_OR, "OR input 1");
		configInput(IN2_OR, "OR input 2");
		configInput(IN1_XOR, "XOR input 1");
		configInput(IN2_XOR, "XOR input 2");
		configOutput(AND_OUT, "AND");
		configOutput(NAND_OUT, "NAND");
		configOutput(OR_OUT, "OR");
		configOutput(NOR_OUT, "NOR");
		configOutput(XOR_OUT, "XOR");
		configOutput(XNOR_OUT, "XNOR");
		configOutput(NOT_OUT, "NOT");
	}

	bool read(int input, int c, float threshold) {
		float v = inputs[input].getPolyVoltage(c);
		bool &h = high[input][c];
		if (h ? v <= threshold : v > threshold + 0.1f)
			h = !h;
		return h;
	}

	// a gate and its inverse, polyphonic over the channels of its inputs
	void gate(int in1, int in2, int out, int outInv, int op, float threshold) {
		int channels = std::max(1, std::max(inputs[in1].getChannels(), in2 >= 0 ? inputs[in2].getChannels() : 0));
		for (int c = 0; c < channels; c++) {
			bool a = read(in1, c, threshold);
			bool r;
			if (op == 0)
				r = a && read(in2, c, threshold);
			else if (op == 1)
				r = a || read(in2, c, threshold);
			else if (op == 2)
				r = a != read(in2, c, threshold);
			else
				r = a;
			if (out >= 0)
				outputs[out].setVoltage(r ? 10.0f : 0.0f, c);
			outputs[outInv].setVoltage(r ? 0.0f : 10.0f, c);
		}
		if (out >= 0)
			outputs[out].setChannels(channels);
		outputs[outInv].setChannels(channels);
	}

	void process(const ProcessArgs &args) override {
		float threshold = params[THRESHOLD_PARAM].getValue();
		gate(IN1_AND, IN2_AND, AND_OUT, NAND_OUT, 0, threshold);
		gate(IN1_OR, IN2_OR, OR_OUT, NOR_OUT, 1, threshold);
		gate(IN1_XOR, IN2_XOR, XOR_OUT, XNOR_OUT, 2, threshold);
		gate(IN_NOT, -1, -1, NOT_OUT, 3, threshold);
	}
};

struct DILEMOWidget : BidooWidget {
	DILEMOWidget(DILEMO *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/DILEMO.svg"));

		const int inputXAnchor = 7;
		const int inputYAnchor = 79;
		const int outputXAnchor = 44;
		const int outputYAnchor = 79;
		const int inputYOffset = 43;

		addParam(createParam<BidooBlueKnob>(Vec(22.5,30), module, DILEMO::THRESHOLD_PARAM));

  	addInput(createInput<PJ301MPort>(Vec(inputXAnchor,inputYAnchor), module, DILEMO::IN1_AND));
		addInput(createInput<PJ301MPort>(Vec(inputXAnchor,inputYAnchor+inputYOffset), module, DILEMO::IN2_AND));
		addInput(createInput<PJ301MPort>(Vec(inputXAnchor,inputYAnchor+2*inputYOffset), module, DILEMO::IN1_OR));
		addInput(createInput<PJ301MPort>(Vec(inputXAnchor,inputYAnchor+3*inputYOffset), module, DILEMO::IN2_OR));
		addInput(createInput<PJ301MPort>(Vec(inputXAnchor,inputYAnchor+4*inputYOffset), module, DILEMO::IN1_XOR));
		addInput(createInput<PJ301MPort>(Vec(inputXAnchor,inputYAnchor+5*inputYOffset), module, DILEMO::IN2_XOR));
		addInput(createInput<PJ301MPort>(Vec(inputXAnchor,inputYAnchor+6*inputYOffset), module, DILEMO::IN_NOT));

  	addOutput(createOutput<PJ301MPort>(Vec(outputXAnchor,outputYAnchor), module, DILEMO::AND_OUT));
		addOutput(createOutput<PJ301MPort>(Vec(outputXAnchor,outputYAnchor+inputYOffset), module, DILEMO::NAND_OUT));
		addOutput(createOutput<PJ301MPort>(Vec(outputXAnchor,outputYAnchor+2*inputYOffset), module, DILEMO::OR_OUT));
		addOutput(createOutput<PJ301MPort>(Vec(outputXAnchor,outputYAnchor+3*inputYOffset), module, DILEMO::NOR_OUT));
		addOutput(createOutput<PJ301MPort>(Vec(outputXAnchor,outputYAnchor+4*inputYOffset), module, DILEMO::XOR_OUT));
		addOutput(createOutput<PJ301MPort>(Vec(outputXAnchor,outputYAnchor+5*inputYOffset), module, DILEMO::XNOR_OUT));
		addOutput(createOutput<PJ301MPort>(Vec(outputXAnchor,outputYAnchor+6*inputYOffset), module, DILEMO::NOT_OUT));
  }
};

Model *modelDILEMO = createModel<DILEMO, DILEMOWidget>("DilEMO");
