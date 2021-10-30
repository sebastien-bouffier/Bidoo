#include "plugin.hpp"
#include "BidooComponents.hpp"

using namespace std;

struct TEGUMENT : Module {
	enum ParamIds {
		RISE1_PARAM,
		RISECV1_PARAM,
		FALL1_PARAM,
		FALLCV1_PARAM,
		RISE2_PARAM,
		RISECV2_PARAM,
		FALL2_PARAM,
		FALLCV2_PARAM,
		RISEEXP1_PARAM,
		FALLEXP1_PARAM,
		RISEEXP2_PARAM,
		FALLEXP2_PARAM,
		CYCLE1_PARAM,
		CYCLE2_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN1_INPUT,
		TRIG1_INPUT,
		VOCT1_INPUT,
		RISECV1_INPUT,
		FALLCV1_INPUT,
		BOTHCV1_INPUT,
		IN2_INPUT,
		TRIG2_INPUT,
		VOCT2_INPUT,
		RISECV2_INPUT,
		FALLCV2_INPUT,
		BOTHCV2_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		PULSE1_OUTPUT,
		END1_OUTPUT,
		OUT1_OUTPUT,
		BP2_OUTPUT,
		END2_OUTPUT,
		OUT2_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ONE_LIGHT,
		TWO_LIGHT,
		NUM_LIGHTS
	};

	dsp::SchmittTrigger trig1Trigger, trig2Trigger;
	int oscStep1 = 0;
	float rise1 = 0.0f;
	float fall1 = 0.0f;
	float rise1CV = 0.0f;
	float fall1CV = 0.0f;
	float pitch1 = 0.0f;

	int oscStep2 = 0;
	float rise2 = 0.0f;
	float fall2 = 0.0f;
	float rise2CV = 0.0f;
	float fall2CV = 0.0f;
	float pitch2 = 0.0f;

	float out1 = 0.0f;
	float pulse1 = 0.0f;
	float end1 = 0.0f;

	float out2 = 0.0f;
	float bp2 = 0.0f;
	float end2 = 0.0f;

	TEGUMENT() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		configParam(RISE1_PARAM, 0.f, 1.f, 0.f, "Rise");
		configParam(FALL1_PARAM, 0.f, 1.f, 0.f, "Fall");
		configParam(RISECV1_PARAM, -1.f, 1.f, 0.f, "Rise CV");
		configParam(FALLCV1_PARAM, -1.f, 1.f, 0.f, "Fall CV");
		configParam(RISEEXP1_PARAM, 0.f, 1.f, 0.f, "Exp");
		configParam(FALLEXP1_PARAM, 0.f, 1.f, 0.f, "Exp");
		configParam(CYCLE1_PARAM, 0.f, 1.f, 0.f, "Cycle");
		configParam(RISE2_PARAM, 0.f, 1.f, 0.f, "Rise");
		configParam(FALL2_PARAM, 0.f, 1.f, 0.f, "Fall");
		configParam(RISECV2_PARAM, -1.f, 1.f, 0.f, "Rise CV");
		configParam(FALLCV2_PARAM, -1.f, 1.f, 0.f, "Fall CV");
		configParam(RISEEXP2_PARAM, 0.f, 1.f, 0.f, "Exp");
		configParam(FALLEXP2_PARAM, 0.f, 1.f, 0.f, "Exp");
		configParam(CYCLE2_PARAM, 0.f, 1.f, 0.f, "Cycle");
	}

	void process(const ProcessArgs &args) override {
		pitch1 = dsp::FREQ_C4 * dsp::approxExp2_taylor5(inputs[VOCT1_INPUT].getVoltage() + 30) / 1073741824;

		rise1CV = params[RISEEXP1_PARAM].getValue() == 0 ? (params[RISECV1_PARAM].getValue() * rescale(clamp(inputs[RISECV1_INPUT].getVoltage() + inputs[BOTHCV1_INPUT].getVoltage(),-10.0f,10.0f),-10.f,10.f,-1.f, 1.f))	: (params[RISECV1_PARAM].getValue() * out1/10.0f);
		fall1CV = params[FALLEXP1_PARAM].getValue() == 0 ? (params[FALLCV1_PARAM].getValue() * rescale(clamp(inputs[FALLCV1_INPUT].getVoltage() + inputs[BOTHCV1_INPUT].getVoltage(),-10.0f,10.0f),-10.f,10.f,-1.f, 1.f))	: (params[FALLCV1_PARAM].getValue() * out1/10.0f);
		rise1 = std::pow(params[RISE1_PARAM].getValue() + rise1CV,4.f);
		fall1 = std::pow(params[FALL1_PARAM].getValue() + fall1CV,4.f);

		float in1 = inputs[IN1_INPUT].getVoltage();

		if (((inputs[TRIG1_INPUT].isConnected() && trig1Trigger.process(inputs[TRIG1_INPUT].getVoltage())) || (params[CYCLE1_PARAM].getValue()==1)) && ((oscStep1==0) || (oscStep1==3))) {
			oscStep1=1;
		}

		end1=10.f;

		if ((inputs[IN1_INPUT].isConnected() && (in1 > out1)) || (oscStep1==1)) {
			if (rise1==0.f)	{
				out1=in1;
				oscStep1=2;
			}
			else {
				out1=out1+args.sampleTime/(rise1*(inputs[VOCT1_INPUT].isConnected() ? 1.0f/pitch1 : 1));
				end1=0.f;
			}
			if (inputs[IN1_INPUT].isConnected() && (out1 >= in1)) {
				out1=in1;
			}
			if ((oscStep1==1) && (out1 >= 8)) {
				oscStep1=2;
				out1=8;
			}
		}
		else if ((inputs[IN1_INPUT].isConnected() && (in1 < out1)) || (oscStep1==2)) {
			if (fall1==0.f)	{
				out1=in1;
				oscStep1=3;
			}
			else {
				out1=out1-args.sampleTime/(fall1*(inputs[VOCT1_INPUT].isConnected() ? 1.0f/pitch1 : 1));
				end1=0.f;
			}
			if (inputs[IN1_INPUT].isConnected() && (out1 <= in1)) {
				out1=in1;
			}
			if ((oscStep1==2) && (out1 <= 0)) {
				oscStep1=3;
				out1=0;
			}
		}

		outputs[OUT1_OUTPUT].setVoltage(out1);
		outputs[END1_OUTPUT].setVoltage(end1);
		outputs[PULSE1_OUTPUT].setVoltage(out1>5.0f?10.0f:0.0f);

		pitch2 = dsp::FREQ_C4 * dsp::approxExp2_taylor5(inputs[VOCT2_INPUT].getVoltage() + 30) / 1073741824;

		rise2CV = params[RISEEXP2_PARAM].getValue() == 0 ? (params[RISECV2_PARAM].getValue() * rescale(clamp(inputs[RISECV2_INPUT].getVoltage() + inputs[BOTHCV2_INPUT].getVoltage(),-10.0f,10.0f),-10.f,10.f,-1.f, 1.f))	: (params[RISECV2_PARAM].getValue() * out2/10.0f);
		fall2CV = params[FALLEXP2_PARAM].getValue() == 0 ? (params[FALLCV2_PARAM].getValue() * rescale(clamp(inputs[FALLCV2_INPUT].getVoltage() + inputs[BOTHCV2_INPUT].getVoltage(),-10.0f,10.0f),-10.f,10.f,-1.f, 1.f))	: (params[FALLCV2_PARAM].getValue() * out2/10.0f);
		rise2 = std::pow(params[RISE2_PARAM].getValue() + rise2CV,4.f);
		fall2 = std::pow(params[FALL2_PARAM].getValue() + fall2CV,4.f);

		float in2 = inputs[IN2_INPUT].getVoltage();

		if (((inputs[TRIG2_INPUT].isConnected() && trig2Trigger.process(inputs[TRIG2_INPUT].getVoltage())) || (params[CYCLE2_PARAM].getValue()==1)) && ((oscStep2==0) || (oscStep2==3))) {
			oscStep2=1;
		}

		end2=10.f;

		if ((inputs[IN2_INPUT].isConnected() && (in2 > out2)) || (oscStep2==1)) {
			if (rise2==0.f)	{
				out2=in2;
				oscStep2=2;
			}
			else {
				out2=out2+args.sampleTime/(rise2*(inputs[VOCT2_INPUT].isConnected() ? 1.0f/pitch2 : 1));
				end2=0.f;
			}
			if (inputs[IN2_INPUT].isConnected() && (out2 >= in2)) {
				out2=in2;
			}
			if ((oscStep2==1) && (out2 >= 8)) {
				oscStep2=2;
				out2=8;
			}
		}
		else if ((inputs[IN2_INPUT].isConnected() && (in2 < out2)) || (oscStep2==2)) {
			if (fall2==0.f)	{
				out2=in2;
				oscStep2=3;
			}
			else {
				out2=out2-args.sampleTime/(fall2*(inputs[VOCT2_INPUT].isConnected() ? 1.0f/pitch2 : 1));
				end2=0.f;
			}
			if (inputs[IN2_INPUT].isConnected() && (out2 <= in2)) {
				out2=in2;
			}
			if ((oscStep2==2) && (out2 <= 0)) {
				oscStep2=3;
				out2=0;
			}
		}

		outputs[OUT2_OUTPUT].setVoltage(out2);
		outputs[END2_OUTPUT].setVoltage(end2);
		outputs[BP2_OUTPUT].setVoltage(out2-5.0f);
	}
};

struct TEGUMENTWidget : ModuleWidget {
	TEGUMENTWidget(TEGUMENT *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/TEGUMENT.svg")));

		const float xPilars[6] = {10-2.5f,45-2.5f,80-2.5f,120-2.5f,155-2.5f,190-2.5f};
		const int yPilars[6] = {60,110,160,208,260,320};
		const int portOffset = 3;
		const int ckssOffset = 8;

		addParam(createParam<BidooBlueKnob>(Vec(xPilars[0], yPilars[0]), module, TEGUMENT::RISE1_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(xPilars[2], yPilars[0]), module, TEGUMENT::FALL1_PARAM));

		addParam(createParam<BidooBlueKnob>(Vec(xPilars[0], yPilars[1]), module, TEGUMENT::RISECV1_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(xPilars[2], yPilars[1]), module, TEGUMENT::FALLCV1_PARAM));

		addInput(createInput<PJ301MPort>(Vec(xPilars[0]+portOffset, yPilars[2]+portOffset), module, TEGUMENT::RISECV1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[1]+portOffset, yPilars[2]+portOffset), module, TEGUMENT::BOTHCV1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[2]+portOffset, yPilars[2]+portOffset), module, TEGUMENT::FALLCV1_INPUT));

		addParam(createParam<CKSS>(Vec(xPilars[0]+ckssOffset, yPilars[3]+5), module, TEGUMENT::RISEEXP1_PARAM));
		addParam(createParam<CKSS>(Vec(xPilars[1]+ckssOffset, yPilars[3]+5), module, TEGUMENT::CYCLE1_PARAM));
		addParam(createParam<CKSS>(Vec(xPilars[2]+ckssOffset, yPilars[3]+5), module, TEGUMENT::FALLEXP1_PARAM));

		addInput(createInput<PJ301MPort>(Vec(xPilars[0]+portOffset, yPilars[4]+portOffset), module, TEGUMENT::VOCT1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[1]+portOffset, yPilars[4]+portOffset), module, TEGUMENT::IN1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[2]+portOffset, yPilars[4]+portOffset), module, TEGUMENT::TRIG1_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(xPilars[0]+portOffset, yPilars[5]+portOffset), module, TEGUMENT::OUT1_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(xPilars[1]+portOffset, yPilars[5]+portOffset), module, TEGUMENT::PULSE1_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(xPilars[2]+portOffset, yPilars[5]+portOffset), module, TEGUMENT::END1_OUTPUT));

		addParam(createParam<BidooBlueKnob>(Vec(xPilars[3], yPilars[0]), module, TEGUMENT::RISE2_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(xPilars[5], yPilars[0]), module, TEGUMENT::FALL2_PARAM));

		addParam(createParam<BidooBlueKnob>(Vec(xPilars[3], yPilars[1]), module, TEGUMENT::RISECV2_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(xPilars[5], yPilars[1]), module, TEGUMENT::FALLCV2_PARAM));

		addInput(createInput<PJ301MPort>(Vec(xPilars[3]+portOffset, yPilars[2]+portOffset), module, TEGUMENT::RISECV2_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[4]+portOffset, yPilars[2]+portOffset), module, TEGUMENT::BOTHCV2_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[5]+portOffset, yPilars[2]+portOffset), module, TEGUMENT::FALLCV2_INPUT));

		addParam(createParam<CKSS>(Vec(xPilars[3]+ckssOffset, yPilars[3]+5), module, TEGUMENT::RISEEXP2_PARAM));
		addParam(createParam<CKSS>(Vec(xPilars[4]+ckssOffset, yPilars[3]+5), module, TEGUMENT::CYCLE2_PARAM));
		addParam(createParam<CKSS>(Vec(xPilars[5]+ckssOffset, yPilars[3]+5), module, TEGUMENT::FALLEXP2_PARAM));

		addInput(createInput<PJ301MPort>(Vec(xPilars[3]+portOffset, yPilars[4]+portOffset), module, TEGUMENT::VOCT2_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[4]+portOffset, yPilars[4]+portOffset), module, TEGUMENT::IN2_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[5]+portOffset, yPilars[4]+portOffset), module, TEGUMENT::TRIG2_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(xPilars[3]+portOffset, yPilars[5]+portOffset), module, TEGUMENT::OUT2_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(xPilars[4]+portOffset, yPilars[5]+portOffset), module, TEGUMENT::BP2_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(xPilars[5]+portOffset, yPilars[5]+portOffset), module, TEGUMENT::END2_OUTPUT));
	}
};

Model *modelTEGUMENT = createModel<TEGUMENT, TEGUMENTWidget>("TEGumEnt");
