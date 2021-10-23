#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/ringbuffer.hpp"
#include "dsp/digital.hpp"

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
		BYPASS_PARAM,
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
		BYPASS_LIGHT,
		NUM_LIGHTS
	};

	dsp::DoubleRingBuffer<float,16384> vu_L_Buffer, vu_R_Buffer;
	dsp::DoubleRingBuffer<float,512> rms_L_Buffer, rms_R_Buffer;
	float runningVU_L_Sum = 1e-6f, runningRMS_L_Sum = 1e-6f, rms_L = -96.3f, vu_L = -96.3f, peakL = -96.3f;
	float runningVU_R_Sum = 1e-6f, runningRMS_R_Sum = 1e-6f, rms_R = -96.3f, vu_R = -96.3f, peakR = -96.3f;

	float in_L_dBFS = 1e-6f;
	float in_R_dBFS = 1e-6f;

	dsp::DoubleRingBuffer<float,16384> SC_vu_L_Buffer, SC_vu_R_Buffer;
	dsp::DoubleRingBuffer<float,512> SC_rms_L_Buffer, SC_rms_R_Buffer;
	float SC_runningVU_L_Sum = 1e-6f, SC_runningRMS_L_Sum = 1e-6f, SC_rms_L = -96.3f, SC_vu_L = -96.3f, SC_peakL = -96.3f;
	float SC_runningVU_R_Sum = 1e-6f, SC_runningRMS_R_Sum = 1e-6f, SC_rms_R = -96.3f, SC_vu_R = -96.3f, SC_peakR = -96.3f;

	float SC_in_L_dBFS = 1e-6f;
	float SC_in_R_dBFS = 1e-6f;

	float dist = 0.0f, gain = 1.0f, gaindB = 1.0f, ratio = 1.0f, threshold = 1.0f, knee = 0.0f;
	float attackTime = 0.0f, releaseTime = 0.0f, makeup = 1.0f, previousPostGain = 1.0f, mix = 1.0f;
	int indexVU = 0, indexRMS = 0, lookAheadWriteIndex=0;
	int maxIndexVU = 0, maxIndexRMS = 0, maxLookAheadWriteIndex=0;
	int lookAhead;
	float buffL[20000] = {0.0f}, buffR[20000] = {0.0f};
	dsp::SchmittTrigger bypassTrigger;
	bool bypass = false;

	BAR() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(THRESHOLD_PARAM, -93.6f, 0.0f, 0.0f, "Threshold");
		configParam(RATIO_PARAM, 1.0f, 20.0f, 1.0f, "Ratio");
		configParam(ATTACK_PARAM, 1.0f, 100.0f, 10.0f, "Attack");
		configParam(RELEASE_PARAM, 1.0f, 300.0f, 10.0f, "Release");
		configParam(KNEE_PARAM, 0.0f, 24.0f, 6.0f, "Knee");
		configParam(MAKEUP_PARAM, 0.0f, 60.0f, 0.0f, "Make up");
		configParam(MIX_PARAM, 0.0f, 1.0f, 1.0f, "Mix");
		configParam(LOOKAHEAD_PARAM, 0.0f, 200.0f, 0.0f, "Lookahead");
		configParam(BYPASS_PARAM, 0.0f, 1.0f, 0.0f, "Bypass");
	}

	void process(const ProcessArgs &args) override;

};

void BAR::process(const ProcessArgs &args) {
	if (bypassTrigger.process(params[BYPASS_PARAM].getValue())) {
		bypass = !bypass;
	}
	lights[BYPASS_LIGHT].setBrightness(bypass ? 1.0f : 0.0f);

	if (indexVU>=16384) {
		runningVU_L_Sum -= *vu_L_Buffer.startData();
		runningVU_R_Sum -= *vu_R_Buffer.startData();
		vu_L_Buffer.startIncr(1);
		vu_R_Buffer.startIncr(1);
		SC_runningVU_L_Sum -= *SC_vu_L_Buffer.startData();
		SC_runningVU_R_Sum -= *SC_vu_R_Buffer.startData();
		SC_vu_L_Buffer.startIncr(1);
		SC_vu_R_Buffer.startIncr(1);
		indexVU--;
	}

	if (indexRMS>=512) {
		runningRMS_L_Sum -= *rms_L_Buffer.startData();
		runningRMS_R_Sum -= *rms_R_Buffer.startData();
		rms_L_Buffer.startIncr(1);
		rms_R_Buffer.startIncr(1);
		SC_runningRMS_L_Sum -= *SC_rms_L_Buffer.startData();
		SC_runningRMS_R_Sum -= *SC_rms_R_Buffer.startData();
		SC_rms_L_Buffer.startIncr(1);
		SC_rms_R_Buffer.startIncr(1);
		indexRMS--;
	}

	indexVU++;
	indexRMS++;

	buffL[lookAheadWriteIndex]=inputs[IN_L_INPUT].getVoltage();
	buffR[lookAheadWriteIndex]=inputs[IN_R_INPUT].getVoltage();

	if (inputs[IN_L_INPUT].isConnected())
		in_L_dBFS = max(20.0f*log10((abs(inputs[IN_L_INPUT].getVoltage())+1e-6f) * 0.2f), -96.3f);
	else
		in_L_dBFS = -96.3f;

	if (inputs[SC_L_INPUT].isConnected())
		SC_in_L_dBFS = max(20.0f*log10((abs(inputs[SC_L_INPUT].getVoltage())+1e-6f) * 0.2f), -96.3f);
	else
		SC_in_L_dBFS = -96.3f;

	if (inputs[IN_R_INPUT].isConnected())
		in_R_dBFS = max(20.0f*log10((abs(inputs[IN_R_INPUT].getVoltage())+1e-6f) * 0.2f), -96.3f);
	else
		in_R_dBFS = -96.3f;

	if (inputs[SC_R_INPUT].isConnected())
		SC_in_R_dBFS = max(20.0f*log10((abs(inputs[SC_R_INPUT].getVoltage())+1e-6f) * 0.2f), -96.3f);
	else
		SC_in_R_dBFS = -96.3f;

	float data_L = in_L_dBFS*in_L_dBFS;
	float data_R = in_R_dBFS*in_R_dBFS;

	float SC_data_L = SC_in_L_dBFS*SC_in_L_dBFS;
	float SC_data_R = SC_in_R_dBFS*SC_in_R_dBFS;

	if (!vu_L_Buffer.full()) {
		vu_L_Buffer.push(data_L);
		vu_R_Buffer.push(data_R);
		SC_vu_L_Buffer.push(SC_data_L);
		SC_vu_R_Buffer.push(SC_data_R);
	}

	if (!rms_L_Buffer.full()) {
		rms_L_Buffer.push(data_L);
		rms_R_Buffer.push(data_R);
		SC_rms_L_Buffer.push(SC_data_L);
		SC_rms_R_Buffer.push(SC_data_R);
	}

	runningVU_L_Sum += data_L;
	runningRMS_L_Sum += data_L;
	runningVU_R_Sum += data_R;
	runningRMS_R_Sum += data_R;
	rms_L = clamp(-1 * sqrtf(runningRMS_L_Sum/512), -96.3f,0.0f);
	vu_L = clamp(-1 * sqrtf(runningVU_L_Sum/16384), -96.3f,0.0f);
	rms_R = clamp(-1 * sqrtf(runningRMS_R_Sum/512), -96.3f,0.0f);
	vu_R = clamp(-1 * sqrtf(runningVU_R_Sum/16384), -96.3f,0.0f);

	SC_runningVU_L_Sum += SC_data_L;
	SC_runningRMS_L_Sum += SC_data_L;
	SC_runningVU_R_Sum += SC_data_R;
	SC_runningRMS_R_Sum += SC_data_R;
	SC_rms_L = clamp(-1 * sqrtf(SC_runningRMS_L_Sum/512), -96.3f,0.0f);
	SC_vu_L = clamp(-1 * sqrtf(SC_runningVU_L_Sum/16384), -96.3f,0.0f);
	SC_rms_R = clamp(-1 * sqrtf(SC_runningRMS_R_Sum/512), -96.3f,0.0f);
	SC_vu_R = clamp(-1 * sqrtf(SC_runningVU_R_Sum/16384), -96.3f,0.0f);

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

	if (in_R_dBFS>peakR)
		peakR=in_R_dBFS;
	else
		peakR -= 50.0f / args.sampleRate;

	if (SC_in_L_dBFS>SC_peakL)
		SC_peakL=SC_in_L_dBFS;
	else
		SC_peakL -= 50.0f / args.sampleRate;

	if (SC_in_R_dBFS>SC_peakR)
		SC_peakR=SC_in_R_dBFS;
	else
		SC_peakR -= 50.0f / args.sampleRate;

	float slope = 1.0f/ratio-1.0f;
	float maxIn = (inputs[SC_L_INPUT].isConnected() || inputs[SC_R_INPUT].isConnected()) ? max(SC_in_L_dBFS,SC_in_R_dBFS) : max(in_L_dBFS,in_R_dBFS);
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

	outputs[OUT_L_OUTPUT].setVoltage(buffL[readIndex] * (bypass ? 1.0f : (gain*mix + (1.0f - mix))));
	outputs[OUT_R_OUTPUT].setVoltage(buffR[readIndex] * (bypass ? 1.0f : (gain*mix + (1.0f - mix))));

	lookAheadWriteIndex = (lookAheadWriteIndex+1)%20000;
}

struct BARDisplay : TransparentWidget {
	BAR *module;
	float height = 150.0f;
	float width = 15.0f;
	float spacer = 3.0f;
	float width2 = width/2;
	float spacer2 = spacer/2;
	float widthSpacer =  width + spacer;

	std::string fontPath = "res/DejaVuSansMono.ttf";

	BARDisplay() {

	}

void draw(const DrawArgs &args) override {
	std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, fontPath));
	nvgGlobalTint(args.vg, color::WHITE);
	float vuL = rescale(module->vu_L,-97.0f,0.0f,0.0f,height);
	float rmsL = rescale(module->rms_L,-97.0f,0.0f,0.0f,height);
	float vuR = rescale(module->vu_R,-97.0f,0.0f,0.0f,height);
	float rmsR = rescale(module->rms_R,-97.0f,0.0f,0.0f,height);

	float SC_vuL = rescale(module->SC_vu_L,-97.0f,0.0f,0.0f,height);
	float SC_rmsL = rescale(module->SC_rms_L,-97.0f,0.0f,0.0f,height);
	float SC_vuR = rescale(module->SC_vu_R,-97.0f,0.0f,0.0f,height);
	float SC_rmsR = rescale(module->SC_rms_R,-97.0f,0.0f,0.0f,height);

	float threshold = rescale(module->threshold,0.0f,-97.0f,0.0f,height);
	float gain = rescale(1-(module->gaindB-module->makeup),-97.0f,0.0f,97.0f,0.0f);
	float makeup = rescale(module->makeup,0.0f,60.0f,0.0f,60.0f);

	float peakL = clamp(rescale(module->peakL,0.0f,-97.0f,0.0f,height),0.f,height);
	float peakR = clamp(rescale(module->peakR,0.0f,-97.0f,0.0f,height),0.f,height);
	float inL = rescale(module->in_L_dBFS,-97.0f,0.0f,0.0f,height);
	float inR = rescale(module->in_R_dBFS,-97.0f,0.0f,0.0f,height);

	float SC_peakL = clamp(rescale(module->SC_peakL,0.0f,-97.0f,0.0f,height),0.f,height);
	float SC_peakR = clamp(rescale(module->SC_peakR,0.0f,-97.0f,0.0f,height),0.f,height);
	float SC_inL = rescale(module->SC_in_L_dBFS,-97.0f,0.0f,0.0f,height);
	float SC_inR = rescale(module->SC_in_R_dBFS,-97.0f,0.0f,0.0f,height);

	bool sc = module->inputs[BAR::SC_L_INPUT].isConnected() || module->inputs[BAR::SC_R_INPUT].isConnected();

	if (sc) {
		nvgSave(args.vg);
		nvgStrokeWidth(args.vg, 0.0f);

		nvgFillColor(args.vg, BLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,0.0f,height-vuL,width2,vuL,0.0f);
		nvgRoundedRect(args.vg,3.0f*widthSpacer+width2,height-vuR,width2,vuR,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, RED_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,width+spacer2-width2,peakL,width/2,2.0f,0.0f);
		nvgRoundedRect(args.vg,3.0f*widthSpacer-spacer2,peakR,width2,2.0f,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, ORANGE_BIDOO);
		nvgBeginPath(args.vg);
		if (inL>rmsL+3.0f)
			nvgRoundedRect(args.vg,width+spacer2-width2,max(height-inL+1.0f,0.f),width2,inL-rmsL-2.0f,0.0f);
		if (inR>rmsR+3.0f)
			nvgRoundedRect(args.vg,3.0f*widthSpacer-spacer2,max(height-inR+1.0f,0.f),width2,inR-rmsR-2.0f,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, LIGHTBLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,width+spacer2-width2,height-rmsL,width2,rmsL,0.0f);
		nvgRoundedRect(args.vg,3.0f*widthSpacer-spacer2,height-rmsR,width2,rmsR,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);



		nvgFillColor(args.vg, BLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,widthSpacer,height-SC_vuL,width2,SC_vuL,0.0f);
		nvgRoundedRect(args.vg,2.0f*widthSpacer+width2,height-SC_vuR,width2,SC_vuR,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, RED_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,widthSpacer+width2+spacer2,SC_peakL,width2,2.0f,0.0f);
		nvgRoundedRect(args.vg,2.0f*widthSpacer-spacer2,SC_peakR,width2,2.0f,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, ORANGE_BIDOO);
		nvgBeginPath(args.vg);
		if (inL>rmsL+3.0f)
			nvgRoundedRect(args.vg,widthSpacer+width2+spacer2,max(height-SC_inL+1.0f,0.f),width2,SC_inL-SC_rmsL-2.0f,0.0f);
		if (inR>rmsR+3.0f)
			nvgRoundedRect(args.vg,2.0f*widthSpacer-spacer2,max(height-SC_inR+1.0f,0.f),width2,SC_inR-SC_rmsR-2.0f,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, LIGHTBLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,widthSpacer+width2+spacer2,height-SC_rmsL,width2,SC_rmsL,0.0f);
		nvgRoundedRect(args.vg,2.0f*widthSpacer-spacer2,height-SC_rmsR,width2,SC_rmsR,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

	}
	else {
		nvgSave(args.vg);
		nvgStrokeWidth(args.vg, 0.0f);

		nvgFillColor(args.vg, BLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,0.0f,height-vuL,width,vuL,0.0f);
		nvgRoundedRect(args.vg,3.0f*widthSpacer,height-vuR,width,vuR,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, RED_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,widthSpacer,peakL,width,2.0f,0.0f);
		nvgRoundedRect(args.vg,2.0f*widthSpacer,peakR,width,2.0f,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, ORANGE_BIDOO);
		nvgBeginPath(args.vg);
		if (inL>rmsL+3.0f)
			nvgRoundedRect(args.vg,widthSpacer,max(height-inL+1.0f,0.f),width,inL-rmsL-2.0f,0.0f);
		if (inR>rmsR+3.0f)
			nvgRoundedRect(args.vg,2.0f*widthSpacer,max(height-inR+1.0f,0.f),width,inR-rmsR-2.0f,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, LIGHTBLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,widthSpacer,height-rmsL,width,rmsL,0.0f);
		nvgRoundedRect(args.vg,2.0f*widthSpacer,height-rmsR,width,rmsR,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);
	}


	nvgStrokeWidth(args.vg, 0.5f);
	nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 255));
	nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 255));
	nvgBeginPath(args.vg);
	nvgMoveTo(args.vg, width+spacer+5.0f, threshold);
	nvgLineTo(args.vg, 3.0f*width+2.0f*spacer-5.0f, threshold);
	{
		nvgMoveTo(args.vg, widthSpacer, threshold-3.0f);
		nvgLineTo(args.vg, widthSpacer, threshold+3.0f);
		nvgLineTo(args.vg, widthSpacer+5.0f, threshold);
		nvgLineTo(args.vg, widthSpacer, threshold-3.0f);
		nvgMoveTo(args.vg, 3.0f*width+2.0f*spacer, threshold-3.0f);
		nvgLineTo(args.vg, 3*width+2*spacer, threshold+3.0f);
		nvgLineTo(args.vg, 3.0f*width+2.0f*spacer-5.0f, threshold);
		nvgLineTo(args.vg, 3.0f*width+2.0f*spacer, threshold-3.0f);
	}
	nvgClosePath(args.vg);
	nvgStroke(args.vg);
	nvgFill(args.vg);

	float offset = 11.0f;
	nvgStrokeWidth(args.vg, 0.5f);
	nvgFillColor(args.vg, YELLOW_BIDOO);
	nvgStrokeColor(args.vg, YELLOW_BIDOO);
	nvgBeginPath(args.vg);
	nvgRoundedRect(args.vg,4.0f*widthSpacer+offset,70.0f,width,-gain-makeup,0.0f);
	nvgMoveTo(args.vg, 5.0f*widthSpacer+7.0f+offset, 70.0f-3.0f);
	nvgLineTo(args.vg, 5.0f*widthSpacer+7.0f+offset, 70.0f+3.0f);
	nvgLineTo(args.vg, 5.0f*widthSpacer+2.0f+offset, 70.0f);
	nvgLineTo(args.vg, 5.0f*widthSpacer+7.0f+offset, 70.0f-3.0f);
	nvgClosePath(args.vg);
	nvgStroke(args.vg);
	nvgFill(args.vg);

	char tTresh[128],tRatio[128],tAtt[128],tRel[128],tKnee[128],tMakeUp[128],tMix[128],tLookAhead[128];
	snprintf(tTresh, sizeof(tTresh), "%2.1f", module->threshold);
	snprintf(tRatio, sizeof(tTresh), "%2.0f:1", module->ratio);
	snprintf(tAtt, sizeof(tTresh), "%1.0f/%1.0f", module->attackTime,module->releaseTime);
	snprintf(tRel, sizeof(tTresh), "%3.0f", module->releaseTime);
	snprintf(tKnee, sizeof(tTresh), "%2.1f", module->knee);
	snprintf(tMakeUp, sizeof(tTresh), "%2.1f", module->makeup);
	snprintf(tMix, sizeof(tTresh), "%1.0f/%1.0f", (1-module->mix)*100,module->mix*100);
	snprintf(tLookAhead, sizeof(tTresh), "%3i", module->lookAhead);
	nvgFontSize(args.vg, 14.0f);
	// nvgFontFaceId(args.vg, font->handle);
	// nvgTextLetterSpacing(args.vg, -2.0f);
	nvgFillColor(args.vg, YELLOW_BIDOO);
	nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
	nvgText(args.vg, 8.0f, height+31.0f, tTresh, NULL);
	nvgText(args.vg, 50.0f, height+31.0f, tRatio, NULL);
	nvgText(args.vg, 96.0f, height+31.0f, tAtt, NULL);
	nvgText(args.vg, 8.0f, height+63.0f, tKnee, NULL);
	nvgText(args.vg, 40.0f, height+63.0f, tMakeUp, NULL);
	nvgText(args.vg, 75.0f, height+63.0f, tMix, NULL);
	nvgText(args.vg, 107.0f, height+63.0f, tLookAhead, NULL);
	nvgRestore(args.vg);
}
};

struct BARWidget : ModuleWidget {
	BARWidget(BAR *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/BAR.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		if (module) {
			BARDisplay *display = new BARDisplay();
			display->module = module;
			display->box.pos = Vec(12.0f, 40.0f);
			display->box.size = Vec(110.0f, 70.0f);
			addChild(display);
		}

		addParam(createParam<MiniLEDButton>(Vec(91.0f, 11.0f), module, BAR::BYPASS_PARAM));
		addChild(createLight<SmallLight<RedLight>>(Vec(91.0f, 11.0f), module, BAR::BYPASS_LIGHT));

		addParam(createParam<BidooBlueTrimpot>(Vec(10.0f,265.0f), module, BAR::THRESHOLD_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(42.0f,265.0f), module, BAR::RATIO_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(74.0f,265.0f), module, BAR::ATTACK_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(106.0f,265.0f), module, BAR::RELEASE_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(10.0f,291.0f), module, BAR::KNEE_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(42.0f,291.0f), module, BAR::MAKEUP_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(74.0f,291.0f), module, BAR::MIX_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(106.0f,291.0f), module, BAR::LOOKAHEAD_PARAM));

		addInput(createInput<TinyPJ301MPort>(Vec(5.0f, 340.0f), module, BAR::IN_L_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(5.0f+22.f, 340.0f), module, BAR::IN_R_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(49.0f, 340.0f), module, BAR::SC_L_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(49.0f+22.f, 340.0f), module, BAR::SC_R_INPUT));

		addOutput(createOutput<TinyPJ301MPort>(Vec(93.0f, 340.0f), module, BAR::OUT_L_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(93.0f+22.0f, 340.0f), module, BAR::OUT_R_OUTPUT));
	}
};

Model *modelBAR = createModel<BAR, BARWidget>("baR");
