#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/ringbuffer.hpp"
#include <cfloat>

using namespace std;

struct BAR : Module {
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
		IN_R_INPUT,
		SC_L_INPUT,
		SC_R_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};
	DoubleRingBuffer<float,16384> vu_L_Buffer, vu_R_Buffer;
	DoubleRingBuffer<float,512> rms_L_Buffer, rms_R_Buffer;
	float runningVU_L_Sum = 1e-6f, runningRMS_L_Sum = 1e-6f, rms_L = -96.3f, vu_L = -96.3f, peakL = -96.3f;
	float runningVU_R_Sum = 1e-6f, runningRMS_R_Sum = 1e-6f, rms_R = -96.3f, vu_R = -96.3f, peakR = -96.3f;
	float in_L_dBFS = 1e-6f;
	float in_R_dBFS = 1e-6f;
	float dist = 0, gain = 1, gaindB = 1, ratio = 1, threshold = 1, knee = 0;
	float attackTime = 0, releaseTime = 0, makeup = 1, previousPostGain = 1.0f, mix = 1;
	int indexVU = 0, indexRMS = 0, lookAheadWriteIndex=0;
	int maxIndexVU = 0, maxIndexRMS = 0, maxLookAheadWriteIndex=0;
	int lookAhead;
	float buffL[20000] = {0},buffR[20000] = {0};
	BAR() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
	}

	void step() override;

};

void BAR::step() {
	if (indexVU>=16384) {
		runningVU_L_Sum -= *vu_L_Buffer.startData();
		runningVU_R_Sum -= *vu_R_Buffer.startData();
		vu_L_Buffer.startIncr(1);
		vu_R_Buffer.startIncr(1);
		indexVU--;
	}

	if (indexRMS>=512) {
		runningRMS_L_Sum -= *rms_L_Buffer.startData();
		runningRMS_R_Sum -= *rms_R_Buffer.startData();
		rms_L_Buffer.startIncr(1);
		rms_R_Buffer.startIncr(1);
		indexRMS--;
	}

	indexVU++;
	indexRMS++;

	buffL[lookAheadWriteIndex]=inputs[IN_L_INPUT].value;
	buffR[lookAheadWriteIndex]=inputs[IN_R_INPUT].value;

	if (!inputs[SC_L_INPUT].active && inputs[IN_L_INPUT].active)
		in_L_dBFS = max(20*log10((abs(inputs[IN_L_INPUT].value)+1e-6f)/5), -96.3f);
	else if (inputs[SC_L_INPUT].active)
		in_L_dBFS = max(20*log10((abs(inputs[SC_L_INPUT].value)+1e-6f)/5), -96.3f);
	else
		in_L_dBFS = -96.3f;

	if (!inputs[SC_R_INPUT].active && inputs[IN_R_INPUT].active)
		in_R_dBFS = max(20*log10((abs(inputs[IN_R_INPUT].value)+1e-6f)/5), -96.3f);
	else if (inputs[SC_R_INPUT].active)
		in_R_dBFS = max(20*log10((abs(inputs[SC_R_INPUT].value)+1e-6f)/5), -96.3f);
	else
		in_R_dBFS = -96.3f;

	float data_L = in_L_dBFS*in_L_dBFS;

	if (!vu_L_Buffer.full()) {
		vu_L_Buffer.push(data_L);
	}
	if (!rms_L_Buffer.full()) {
		rms_L_Buffer.push(data_L);
	}

	float data_R = in_R_dBFS*in_R_dBFS;
	if (!vu_R_Buffer.full()) {
		vu_R_Buffer.push(data_R);
	}
	if (!rms_R_Buffer.full()) {
		rms_R_Buffer.push(data_R);
	}

	runningVU_L_Sum += data_L;
	runningRMS_L_Sum += data_L;
	runningVU_R_Sum += data_R;
	runningRMS_R_Sum += data_R;
	rms_L = clampf(-1 * sqrtf(runningRMS_L_Sum/512), -96.3f,0.0f);
	vu_L = clampf(-1 * sqrtf(runningVU_L_Sum/16384), -96.3f,0.0f);
	rms_R = clampf(-1 * sqrtf(runningRMS_R_Sum/512), -96.3f,0.0f);
	vu_R = clampf(-1 * sqrtf(runningVU_R_Sum/16384), -96.3f,0.0f);
	threshold = params[THRESHOLD_PARAM].value;
	attackTime = params[ATTACK_PARAM].value;
	releaseTime = params[RELEASE_PARAM].value;
	ratio = params[RATIO_PARAM].value;
	knee = params[KNEE_PARAM].value;
	makeup = params[MAKEUP_PARAM].value;

	if (in_L_dBFS>peakL)
		peakL=in_L_dBFS;
	else
		peakL -= 50/engineGetSampleRate();

	if (in_R_dBFS>peakR)
		peakR=in_R_dBFS;
	else
		peakR -= 50/engineGetSampleRate();

	float slope = 1/ratio-1;
	float maxIn = max(in_L_dBFS,in_R_dBFS);
	float dist = maxIn-threshold;
	float gcurve = 0.0f;

	if (dist<-1*knee/2)
		gcurve = maxIn;
	else if ((dist > -1*knee/2) && (dist < knee/2)) {
		gcurve = maxIn + slope * pow(dist + knee/2,2)/(2 * knee);
	} else {
		gcurve = maxIn + slope * dist;
	}

	float preGain = gcurve - maxIn;
	float postGain = 0.0f;
	float cAtt = exp(-1/(attackTime*engineGetSampleRate()/1000));
	float cRel = exp(-1/(releaseTime*engineGetSampleRate()/1000));

	if (preGain>previousPostGain) {
		postGain = cAtt * previousPostGain + (1-cAtt) * preGain;
	} else {
		postGain = cRel * previousPostGain + (1-cRel) * preGain;
	}

	previousPostGain = postGain;
	gaindB = makeup + postGain;
	gain = pow(10, gaindB/20);

	mix = params[MIX_PARAM].value;
	lookAhead = params[LOOKAHEAD_PARAM].value;

	int nbSamples = clampi(floor(lookAhead*attackTime*engineGetSampleRate()/100000),0,19999);
	int readIndex;
	if (lookAheadWriteIndex-nbSamples>=0)
	  readIndex = (lookAheadWriteIndex-nbSamples)%20000;
	else {
		readIndex = 20000 - abs(lookAheadWriteIndex-nbSamples);
	}

	outputs[OUT_L_OUTPUT].value = buffL[readIndex] * (gain*mix + (1-mix));
	outputs[OUT_R_OUTPUT].value = buffR[readIndex] * (gain*mix + (1-mix));

	lookAheadWriteIndex = (lookAheadWriteIndex+1)%20000;
}

struct BARDisplay : TransparentWidget {
	BAR *module;
	std::shared_ptr<Font> font;
	BARDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

void draw(NVGcontext *vg) override {
	float height = 150;
	float width = 15;
	float spacer = 3;
	float vuL = rescalef(module->vu_L,-97,0,0,height);
	float rmsL = rescalef(module->rms_L,-97,0,0,height);
	float vuR = rescalef(module->vu_R,-97,0,0,height);
	float rmsR = rescalef(module->rms_R,-97,0,0,height);
	float threshold = rescalef(module->threshold,0,-97,0,height);
	float gain = rescalef(1-(module->gaindB-module->makeup),-97,0,97,0);
	float makeup = rescalef(module->makeup,0,60,0,60);
	float peakL = rescalef(module->peakL,0,-97,0,height);
	float peakR = rescalef(module->peakR,0,-97,0,height);
	float inL = rescalef(module->in_L_dBFS,-97,0,0,height);
	float inR = rescalef(module->in_R_dBFS,-97,0,0,height);
	nvgStrokeWidth(vg, 0);
	nvgBeginPath(vg);
	nvgFillColor(vg, BLUE_BIDOO);
	nvgRoundedRect(vg,0,height-vuL,width,vuL,0);
	nvgRoundedRect(vg,3*(width+spacer),height-vuR,width,vuR,0);
	nvgFill(vg);
	nvgClosePath(vg);
	nvgBeginPath(vg);
	nvgFillColor(vg, LIGHTBLUE_BIDOO);
	nvgRoundedRect(vg,width+spacer,height-rmsL,width,rmsL,0);
	nvgRoundedRect(vg,2*(width+spacer),height-rmsR,width,rmsR,0);
	nvgFill(vg);
	nvgClosePath(vg);

	nvgFillColor(vg, RED_BIDOO);
	nvgBeginPath(vg);
	nvgRoundedRect(vg,width+spacer,peakL,width,2,0);
	nvgRoundedRect(vg,2*(width+spacer),peakR,width,2,0);
	nvgFill(vg);
	nvgClosePath(vg);

	nvgFillColor(vg, ORANGE_BIDOO);
	nvgBeginPath(vg);
	if (inL>rmsL+3)
		nvgRoundedRect(vg,width+spacer,height-inL+1,width,inL-rmsL-2,0);
	if (inR>rmsR+3)
		nvgRoundedRect(vg,2*(width+spacer),height-inR+1,width,inR-rmsR-2,0);
	nvgFill(vg);
	nvgClosePath(vg);

	nvgStrokeWidth(vg, 0.5);
	nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
	nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 255));
	nvgBeginPath(vg);
	nvgMoveTo(vg, width+spacer+5, threshold);
	nvgLineTo(vg, 3*width+2*spacer-5, threshold);
	//nvgRoundedRect(vg,22,threshold+50,22,2,0);
	{
		nvgMoveTo(vg, width+spacer, threshold-3);
		nvgLineTo(vg, width+spacer, threshold+3);
		nvgLineTo(vg, width+spacer+5, threshold);
		nvgLineTo(vg, width+spacer, threshold-3);
		nvgMoveTo(vg, 3*width+2*spacer, threshold-3);
		nvgLineTo(vg, 3*width+2*spacer, threshold+3);
		nvgLineTo(vg, 3*width+2*spacer-5, threshold);
		nvgLineTo(vg, 3*width+2*spacer, threshold-3);
	}
	nvgClosePath(vg);
	nvgStroke(vg);
	nvgFill(vg);

	float offset = 11;
	nvgStrokeWidth(vg, 0.5);
	nvgFillColor(vg, YELLOW_BIDOO);
	nvgStrokeColor(vg, YELLOW_BIDOO);
	nvgBeginPath(vg);
	nvgRoundedRect(vg,4*(width+spacer)+offset,70,width,-gain-makeup,0);
	nvgMoveTo(vg, 5*(width+spacer)+7+offset, 70-3);
	nvgLineTo(vg, 5*(width+spacer)+7+offset, 70+3);
	nvgLineTo(vg, 5*(width+spacer)+2+offset, 70);
	nvgLineTo(vg, 5*(width+spacer)+7+offset, 70-3);
	nvgClosePath(vg);
	nvgStroke(vg);
	nvgFill(vg);

	char tTresh[128],tRatio[128],tAtt[128],tRel[128],tKnee[128],tMakeUp[128],tMix[128],tLookAhead[128];
	snprintf(tTresh, sizeof(tTresh), "%2.1f", module->threshold);
	snprintf(tRatio, sizeof(tTresh), "%2.0f:1", module->ratio);
	snprintf(tAtt, sizeof(tTresh), "%1.0f/%1.0f", module->attackTime,module->releaseTime);
	snprintf(tRel, sizeof(tTresh), "%3.0f", module->releaseTime);
	snprintf(tKnee, sizeof(tTresh), "%2.1f", module->knee);
	snprintf(tMakeUp, sizeof(tTresh), "%2.1f", module->makeup);
	snprintf(tMix, sizeof(tTresh), "%1.0f/%1.0f", (1-module->mix)*100,module->mix*100);
	snprintf(tLookAhead, sizeof(tTresh), "%3i", module->lookAhead);
	nvgFontSize(vg, 14);
	nvgFontFaceId(vg, font->handle);
	nvgTextLetterSpacing(vg, -2);
	nvgFillColor(vg, YELLOW_BIDOO);
	nvgTextAlign(vg, NVG_ALIGN_CENTER);
	nvgText(vg, 8, height+31, tTresh, NULL);
	nvgText(vg, 50, height+31, tRatio, NULL);
	nvgText(vg, 96, height+31, tAtt, NULL);
	nvgText(vg, 8, height+63, tKnee, NULL);
	nvgText(vg, 40, height+63, tMakeUp, NULL);
	nvgText(vg, 75, height+63, tMix, NULL);
	nvgText(vg, 107, height+63, tLookAhead, NULL);
}
};

BARWidget::BARWidget() {
	BAR *module = new BAR();
	setModule(module);
	box.size = Vec(15*9, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/BAR.svg")));
		addChild(panel);
	}

	BARDisplay *display = new BARDisplay();
	display->module = module;
	display->box.pos = Vec(12, 40);
	display->box.size = Vec(110, 70);
	addChild(display);

	addParam(createParam<BidooBlueTrimpot>(Vec(10,265), module, BAR::THRESHOLD_PARAM, -93.6, 0, 0));
	addParam(createParam<BidooBlueTrimpot>(Vec(42,265), module, BAR::RATIO_PARAM, 1, 20, 0));
	addParam(createParam<BidooBlueTrimpot>(Vec(74,265), module, BAR::ATTACK_PARAM, 1, 100, 10));
	addParam(createParam<BidooBlueTrimpot>(Vec(106,265), module, BAR::RELEASE_PARAM, 1, 300, 10));
	addParam(createParam<BidooBlueTrimpot>(Vec(10,291), module, BAR::KNEE_PARAM, 0, 24, 6));
	addParam(createParam<BidooBlueTrimpot>(Vec(42,291), module, BAR::MAKEUP_PARAM, 0, 60, 0));
	addParam(createParam<BidooBlueTrimpot>(Vec(74,291), module, BAR::MIX_PARAM, 0, 1, 1));
	addParam(createParam<BidooBlueTrimpot>(Vec(106,291), module, BAR::LOOKAHEAD_PARAM, 0, 200, 0));
 	//Changed ports opposite way around
	addInput(createInput<TinyPJ301MPort>(Vec(24, 319), module, BAR::IN_L_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(24, 339), module, BAR::IN_R_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(66, 319), module, BAR::SC_L_INPUT));
	addInput(createInput<TinyPJ301MPort>(Vec(66, 339), module, BAR::SC_R_INPUT));
	addOutput(createOutput<TinyPJ301MPort>(Vec(109, 319), module, BAR::OUT_L_OUTPUT));
	addOutput(createOutput<TinyPJ301MPort>(Vec(109, 339), module, BAR::OUT_R_OUTPUT));

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));
}
