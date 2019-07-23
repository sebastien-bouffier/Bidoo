#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/ringbuffer.hpp"

using namespace std;

struct MINIBAR : Module {
	enum ParamIds {
		THRESHOLD_PARAM,
		RATIO_PARAM,
		ATTACK_PARAM,
		RELEASE_PARAM,
		KNEE_PARAM,
		MAKEUP_PARAM,
		MIX_PARAM,
		LOOKAHEAD_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_L_INPUT,
		SC_L_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	dsp::DoubleRingBuffer<float,16384> vu_L_Buffer;
	dsp::DoubleRingBuffer<float,512> rms_L_Buffer;
	float runningVU_L_Sum = 1e-6f, runningRMS_L_Sum = 1e-6f, rms_L = -96.3f, vu_L = -96.3f, peakL = -96.3f;
	float in_L_dBFS = 1e-6f;
	float dist = 0.0f, gain = 1.0f, gaindB = 1.0f, ratio = 1.0f, threshold = 1.0f, knee = 0.0f;
	float attackTime = 0.0f, releaseTime = 0.0f, makeup = 1.0f, previousPostGain = 1.0f, mix = 1.0f;
	int indexVU = 0, indexRMS = 0, lookAheadWriteIndex=0;
	int maxIndexVU = 0, maxIndexRMS = 0, maxLookAheadWriteIndex=0;
	int lookAhead;
	float buffL[20000] = {0.0f};

	MINIBAR() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(THRESHOLD_PARAM, -93.6f, 0.0f, 0.0f, "Threshold");
		configParam(RATIO_PARAM, 1.0f, 20.0f, 1.0f, "Ratio");
		configParam(ATTACK_PARAM, 1.0f, 100.0f, 10.0f, "Attack");
		configParam(RELEASE_PARAM, 1.0f, 300.0f, 10.0f, "Release");
		configParam(KNEE_PARAM, 0.0f, 24.0f, 6.0f, "Knee");
		configParam(MAKEUP_PARAM, 0.0f, 60.0f, 0.0f, "Make up");
		configParam(MIX_PARAM, 0.0f, 1.0f, 1.0f, "Mix");
		configParam(LOOKAHEAD_PARAM, 0.0f, 200.0f, 0.0f, "Lookahead");
	}

	void process(const ProcessArgs &args) override;

};

void MINIBAR::process(const ProcessArgs &args) {
	if (indexVU>=16384) {
		runningVU_L_Sum -= *vu_L_Buffer.startData();
		vu_L_Buffer.startIncr(1);
		indexVU--;
	}

	if (indexRMS>=512) {
		runningRMS_L_Sum -= *rms_L_Buffer.startData();
		rms_L_Buffer.startIncr(1);
		indexRMS--;
	}

	indexVU++;
	indexRMS++;

	buffL[lookAheadWriteIndex]=inputs[IN_L_INPUT].getVoltage();

	if (!inputs[SC_L_INPUT].active && inputs[IN_L_INPUT].active)
		in_L_dBFS = max(20.0f*log10((abs(inputs[IN_L_INPUT].getVoltage())+1e-6f) * 0.2f), -96.3f);
	else if (inputs[SC_L_INPUT].active)
		in_L_dBFS = max(20.0f*log10((abs(inputs[SC_L_INPUT].getVoltage())+1e-6f) * 0.2f), -96.3f);
	else
		in_L_dBFS = -96.3f;

	float data_L = in_L_dBFS*in_L_dBFS;

	if (!vu_L_Buffer.full()) {
		vu_L_Buffer.push(data_L);
	}
	if (!rms_L_Buffer.full()) {
		rms_L_Buffer.push(data_L);
	}

	runningVU_L_Sum += data_L;
	runningRMS_L_Sum += data_L;
	rms_L = clamp(-1 * sqrtf(runningRMS_L_Sum/512), -96.3f,0.0f);
	vu_L = clamp(-1 * sqrtf(runningVU_L_Sum/16384), -96.3f,0.0f);
	threshold = params[THRESHOLD_PARAM].getValue();
	attackTime = params[ATTACK_PARAM].getValue();
	releaseTime = params[RELEASE_PARAM].getValue();
	ratio = params[RATIO_PARAM].getValue();
	knee = params[KNEE_PARAM].getValue();
	makeup = params[MAKEUP_PARAM].getValue();

	if (in_L_dBFS>peakL)
		peakL=in_L_dBFS;
	else
		peakL -= 50.0f / args.sampleRate;

	float slope = 1.0f/ratio-1.0f;
	float maxIn = in_L_dBFS;
	float dist = maxIn-threshold;
	float gcurve = 0.0f;

	if (dist<-1.0f*knee/2.0f)
		gcurve = maxIn;
	else if ((dist > -1.0f * knee * 0.5f) && (dist < knee * 0.5f)) {
		gcurve = maxIn + slope * pow(dist + knee *0.5f, 2.0f) / (2.0f * knee);
	} else {
		gcurve = maxIn + slope * dist;
	}

	float preGain = gcurve - maxIn;
	float postGain = 0.0f;
	float cAtt = exp(-1.0f/(attackTime * args.sampleRate * 0.001f));
	float cRel = exp(-1.0f/(releaseTime * args.sampleRate * 0.001f));

	if (preGain>previousPostGain) {
		postGain = cAtt * previousPostGain + (1.0f-cAtt) * preGain;
	} else {
		postGain = cRel * previousPostGain + (1.0f-cRel) * preGain;
	}

	previousPostGain = postGain;
	gaindB = makeup + postGain;
	gain = pow(10.0f, gaindB/20.0f);

	mix = params[MIX_PARAM].getValue();
	lookAhead = params[LOOKAHEAD_PARAM].getValue();

	int nbSamples = clamp(floor(lookAhead * attackTime * args.sampleRate * 0.000001f),0.0f,19999.0f);
	int readIndex;
	if (lookAheadWriteIndex-nbSamples>=0)
	  readIndex = (lookAheadWriteIndex-nbSamples)%20000;
	else {
		readIndex = 20000 - abs(lookAheadWriteIndex-nbSamples);
	}

	outputs[OUT_L_OUTPUT].setVoltage(buffL[readIndex] * (gain*mix + (1.0f - mix)));

	lookAheadWriteIndex = (lookAheadWriteIndex+1)%20000;
}

struct MINIBARDisplay : TransparentWidget {
	MINIBAR *module;
	std::shared_ptr<Font> font;
	MINIBARDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

void draw(NVGcontext *vg) override {
	float height = 250.0f;
	float width = 8.0f;
	float spacer = 2.0f;
	float vuL = rescale(module->vu_L,-97.0f,0.0f,0.0f,height);
	float rmsL = rescale(module->rms_L,-97.0f,0.0f,0.0f,height);
	float threshold = rescale(module->threshold,0.0f,-97.0f,0.0f,height);
	float gain = rescale(1-(module->gaindB-module->makeup),-97.0f,0.0f,97.0f,0.0f);
	float makeup = rescale(module->makeup,0.0f,60.0f,0.0f,60.0f);
	float peakL = rescale(module->peakL,0.0f,-97.0f,0.0f,height);
	float inL = rescale(module->in_L_dBFS,-97.0f,0.0f,0.0f,height);
	nvgSave(vg);
	nvgStrokeWidth(vg, 0.0f);
	nvgBeginPath(vg);
	nvgFillColor(vg, BLUE_BIDOO);
	nvgRoundedRect(vg,0.0f,height-vuL,width,vuL,0.0f);
	nvgFill(vg);
	nvgClosePath(vg);
	nvgBeginPath(vg);
	nvgFillColor(vg, LIGHTBLUE_BIDOO);
	nvgRoundedRect(vg,width+spacer,height-rmsL,width,rmsL,0.0f);
	nvgFill(vg);
	nvgClosePath(vg);

	nvgFillColor(vg, RED_BIDOO);
	nvgBeginPath(vg);
	nvgRoundedRect(vg,width+spacer,peakL,width,2.0f,0.0f);
	nvgFill(vg);
	nvgClosePath(vg);

	nvgFillColor(vg, ORANGE_BIDOO);
	nvgBeginPath(vg);
	if (inL>rmsL+3.0f)
		nvgRoundedRect(vg,width+spacer,height-inL+1.0f,width,inL-rmsL-2.0f,0.0f);

	nvgFill(vg);
	nvgClosePath(vg);

	nvgStrokeWidth(vg, 2.0f);
	nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
	nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
	nvgBeginPath(vg);
	nvgMoveTo(vg, width+spacer, threshold);
	nvgLineTo(vg, 2*width+spacer, threshold);

	nvgClosePath(vg);
	nvgStroke(vg);
	nvgFill(vg);

	float offset = 2.0f;
	nvgStrokeWidth(vg, 0.5f);
	nvgFillColor(vg, YELLOW_BIDOO);
	nvgStrokeColor(vg, YELLOW_BIDOO);
	nvgBeginPath(vg);
	nvgRoundedRect(vg,2.0f*(width+spacer)+offset,70.0f,width,-gain-makeup,0.0f);
	nvgClosePath(vg);
	nvgStroke(vg);
	nvgFill(vg);
	nvgRestore(vg);
}
};

struct MINIBARWidget : ModuleWidget {
	MINIBARWidget(MINIBAR *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MINIBAR.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		if (module) {
			MINIBARDisplay *display = new MINIBARDisplay();
			display->module = module;
			display->box.pos = Vec(39.0f, 45.0f);
			display->box.size = Vec(70.0f, 70.0f);
			addChild(display);
		}

		addParam(createParam<BidooBlueTrimpot>(Vec(2.0f,37.0f), module, MINIBAR::THRESHOLD_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(15.0f,72.0f), module, MINIBAR::RATIO_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(2.0f,107.0f), module, MINIBAR::ATTACK_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(15.0f,142.0f), module, MINIBAR::RELEASE_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(2.0f,177.0f), module, MINIBAR::KNEE_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(15.0f,212.0f), module, MINIBAR::MAKEUP_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(2.0f,247.0f), module, MINIBAR::MIX_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(15.0f,282.0f), module, MINIBAR::LOOKAHEAD_PARAM));

		addInput(createInput<TinyPJ301MPort>(Vec(6.0f, 323.0f), module, MINIBAR::IN_L_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(30.0f, 323.0f), module, MINIBAR::SC_L_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(54.0f, 323.0f), module, MINIBAR::OUT_L_OUTPUT));
	}
};

Model *modelMINIBAR = createModel<MINIBAR, MINIBARWidget>("mINIBar");