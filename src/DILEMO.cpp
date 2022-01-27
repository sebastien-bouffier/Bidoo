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

	bool in1AND = false;
	bool in2AND = false;
	bool in1OR = false;
	bool in2OR = false;
	bool in1XOR = false;
	bool in2XOR = false;
	bool inNOT = false;

	DILEMO() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(THRESHOLD_PARAM, 0.0f, 10.0f, 0.0f);
  }

	void process(const ProcessArgs &args) override;
};


void DILEMO::process(const ProcessArgs &args) {
  in1AND = inputs[IN1_AND].getVoltage()>params[THRESHOLD_PARAM].getValue() ? true : false;
	in2AND = inputs[IN2_AND].getVoltage()>params[THRESHOLD_PARAM].getValue() ? true : false;
	in1OR = inputs[IN1_OR].getVoltage()>params[THRESHOLD_PARAM].getValue() ? true : false;
	in2OR = inputs[IN2_OR].getVoltage()>params[THRESHOLD_PARAM].getValue() ? true : false;
	in1XOR = inputs[IN1_XOR].getVoltage()>params[THRESHOLD_PARAM].getValue() ? true : false;
	in2XOR = inputs[IN2_XOR].getVoltage()>params[THRESHOLD_PARAM].getValue() ? true : false;
	inNOT = inputs[IN_NOT].getVoltage()>params[THRESHOLD_PARAM].getValue() ? true : false;

	outputs[AND_OUT].setVoltage(in1AND && in2AND ? 10.0f : 0.0f);
	outputs[NAND_OUT].setVoltage(!(in1AND && in2AND) ? 10.0f : 0.0f);
	outputs[OR_OUT].setVoltage(in1OR || in2OR ? 10.0f : 0.0f);
	outputs[NOR_OUT].setVoltage(!(in1OR || in2OR) ? 10.0f : 0.0f);
	outputs[XOR_OUT].setVoltage(in1XOR ^ in2XOR ? 10.0f : 0.0f);
	outputs[XNOR_OUT].setVoltage(!(in1XOR ^ in2XOR) ? 10.0f : 0.0f);
	outputs[NOT_OUT].setVoltage(!inNOT ? 10.0f : 0.0f);
}

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
