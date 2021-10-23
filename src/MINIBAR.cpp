#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/ringbuffer.hpp"
#include "dsp/digital.hpp"

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
		BYPASS_PARAM,
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
		BYPASS_LIGHT,
		NUM_LIGHTS
	};

	dsp::DoubleRingBuffer<float,16384> vu_L_Buffer;
	dsp::DoubleRingBuffer<float,512> rms_L_Buffer;
	float runningVU_L_Sum = 1e-6f, runningRMS_L_Sum = 1e-6f, rms_L = -96.3f, vu_L = -96.3f, peakL = -96.3f;
	float in_L_dBFS = 1e-6f;

	dsp::DoubleRingBuffer<float,16384> SC_vu_L_Buffer;
	dsp::DoubleRingBuffer<float,512> SC_rms_L_Buffer;
	float SC_runningVU_L_Sum = 1e-6f, SC_runningRMS_L_Sum = 1e-6f, SC_rms_L = -96.3f, SC_vu_L = -96.3f, SC_peakL = -96.3f;
	float SC_in_L_dBFS = 1e-6f;

	float dist = 0.0f, gain = 1.0f, gaindB = 1.0f, ratio = 1.0f, threshold = 1.0f, knee = 0.0f;
	float attackTime = 0.0f, releaseTime = 0.0f, makeup = 1.0f, previousPostGain = 1.0f, mix = 1.0f, mixDisplay = 1.0f;
	int indexVU = 0, indexRMS = 0, lookAheadWriteIndex=0;
	int maxIndexVU = 0, maxIndexRMS = 0, maxLookAheadWriteIndex=0;
	float lookAhead;
	float buffL[20000] = {0.0f};
	dsp::SchmittTrigger bypassTrigger;
	bool bypass = false;

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
		configParam(BYPASS_PARAM, 0.0f, 1.0f, 0.0f, "Bypass");
	}

	void process(const ProcessArgs &args) override;

};

void MINIBAR::process(const ProcessArgs &args) {
	if (bypassTrigger.process(params[BYPASS_PARAM].getValue())) {
		bypass = !bypass;
	}
	lights[BYPASS_LIGHT].setBrightness(bypass ? 1.0f : 0.0f);

	if (indexVU>=16384) {
		runningVU_L_Sum -= *vu_L_Buffer.startData();
		vu_L_Buffer.startIncr(1);
		SC_runningVU_L_Sum -= *SC_vu_L_Buffer.startData();
		SC_vu_L_Buffer.startIncr(1);
		indexVU--;
	}

	if (indexRMS>=512) {
		runningRMS_L_Sum -= *rms_L_Buffer.startData();
		rms_L_Buffer.startIncr(1);
		SC_runningRMS_L_Sum -= *SC_rms_L_Buffer.startData();
		SC_rms_L_Buffer.startIncr(1);
		indexRMS--;
	}

	indexVU++;
	indexRMS++;

	buffL[lookAheadWriteIndex]=inputs[IN_L_INPUT].getVoltage();

	if (inputs[IN_L_INPUT].isConnected())
		in_L_dBFS = max(20.0f*log10((abs(inputs[IN_L_INPUT].getVoltage())+1e-6f) * 0.2f), -96.3f);
	else
		in_L_dBFS = -96.3f;

	if (inputs[SC_L_INPUT].isConnected())
		SC_in_L_dBFS = max(20.0f*log10((abs(inputs[SC_L_INPUT].getVoltage())+1e-6f) * 0.2f), -96.3f);
	else
		SC_in_L_dBFS = -96.3f;

	float data_L = in_L_dBFS*in_L_dBFS;
	float SC_data_L = SC_in_L_dBFS*SC_in_L_dBFS;

	if (!vu_L_Buffer.full()) {
		vu_L_Buffer.push(data_L);
		SC_vu_L_Buffer.push(SC_data_L);
	}
	if (!rms_L_Buffer.full()) {
		rms_L_Buffer.push(data_L);
		SC_rms_L_Buffer.push(SC_data_L);
	}

	runningVU_L_Sum += data_L;
	runningRMS_L_Sum += data_L;
	rms_L = clamp(-1 * sqrtf(runningRMS_L_Sum/512), -96.3f,0.0f);
	vu_L = clamp(-1 * sqrtf(runningVU_L_Sum/16384), -96.3f,0.0f);

	SC_runningVU_L_Sum += SC_data_L;
	SC_runningRMS_L_Sum += SC_data_L;
	SC_rms_L = clamp(-1 * sqrtf(SC_runningRMS_L_Sum/512), -96.3f,0.0f);
	SC_vu_L = clamp(-1 * sqrtf(SC_runningVU_L_Sum/16384), -96.3f,0.0f);

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

	if (SC_in_L_dBFS>SC_peakL)
		SC_peakL=SC_in_L_dBFS;
	else
		SC_peakL -= 50.0f / args.sampleRate;

	float slope = 1.0f/ratio-1.0f;
	float maxIn = inputs[SC_L_INPUT].isConnected() ? SC_in_L_dBFS : in_L_dBFS;
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
	mixDisplay = mix*100.f;
	lookAhead = floor(params[LOOKAHEAD_PARAM].getValue());

	int nbSamples = clamp(floor(lookAhead * attackTime * args.sampleRate * 0.000001f),0.0f,19999.0f);
	int readIndex;
	if (lookAheadWriteIndex-nbSamples>=0)
	  readIndex = (lookAheadWriteIndex-nbSamples)%20000;
	else {
		readIndex = 20000 - abs(lookAheadWriteIndex-nbSamples);
	}

	outputs[OUT_L_OUTPUT].setVoltage(buffL[readIndex] * (bypass ? 1.0f : (gain*mix + (1.0f - mix))));

	lookAheadWriteIndex = (lookAheadWriteIndex+1)%20000;
}

struct MINIBARDisplay : TransparentWidget {
	MINIBAR *module;
	float height = 230.0f;
	float width = 10.0f;
	float spacer = 2.0f;

	std::string fontPath = "res/DejaVuSansMono.ttf";

	MINIBARDisplay() {

	}

void draw(const DrawArgs &args) override {
	std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, fontPath));
	nvgGlobalTint(args.vg, color::WHITE);
	float vuL = rescale(module->vu_L,-97.0f,0.0f,0.0f,height);
	float rmsL = rescale(module->rms_L,-97.0f,0.0f,0.0f,height);
	float peakL = clamp(rescale(module->peakL,0.0f,-97.0f,0.0f,height),0.f,height);
	float inL = rescale(module->in_L_dBFS,-97.0f,0.0f,0.0f,height);

	float SC_vuL = rescale(module->SC_vu_L,-97.0f,0.0f,0.0f,height);
	float SC_rmsL = rescale(module->SC_rms_L,-97.0f,0.0f,0.0f,height);
	float SC_peakL = clamp(rescale(module->SC_peakL,0.0f,-97.0f,0.0f,height),0.f,height);
	float SC_inL = rescale(module->SC_in_L_dBFS,-97.0f,0.0f,0.0f,height);

	float threshold = rescale(module->threshold,0.0f,-97.0f,0.0f,height);
	float gain = rescale(1-(module->gaindB-module->makeup),-97.0f,0.0f,97.0f,0.0f);
	float makeup = rescale(module->makeup,0.0f,60.0f,0.0f,60.0f);

	bool sc = module->inputs[MINIBAR::SC_L_INPUT].isConnected();

	if (sc) {
		nvgSave(args.vg);
		nvgStrokeWidth(args.vg, 0.0f);
		nvgFillColor(args.vg, BLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,0.0f,height-vuL,width/2,vuL,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, RED_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,width/2+spacer/2,peakL,width/2,2.0f,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, ORANGE_BIDOO);
		nvgBeginPath(args.vg);
		if (inL>rmsL+3.0f)
			nvgRoundedRect(args.vg,width/2+spacer/2,max(height-inL+1.0f,0.f),width/2,inL-rmsL-2.0f,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, LIGHTBLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,width/2+spacer/2,height-rmsL,width/2,rmsL,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgStrokeWidth(args.vg, 0.0f);
		nvgFillColor(args.vg, BLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,width+spacer,height-SC_vuL,width/2,SC_vuL,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, RED_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,1.5f*(width+spacer),SC_peakL,width/2,2.0f,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, ORANGE_BIDOO);
		nvgBeginPath(args.vg);
		if (inL>rmsL+3.0f)
			nvgRoundedRect(args.vg,1.5f*(width+spacer),max(height-SC_inL+1.0f,0.f),width/2,SC_inL-SC_rmsL-2.0f,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, LIGHTBLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,1.5f*(width+spacer),height-SC_rmsL,width/2,SC_rmsL,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);
	}
	else {
		nvgSave(args.vg);
		nvgStrokeWidth(args.vg, 0.0f);
		nvgFillColor(args.vg, BLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,0.0f,height-vuL,width,vuL,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, RED_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,width+spacer,peakL,width,2.0f,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, ORANGE_BIDOO);
		nvgBeginPath(args.vg);
		if (inL>rmsL+3.0f)
			nvgRoundedRect(args.vg,width+spacer,max(height-inL+1.0f,0.f),width,inL-rmsL-2.0f,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);

		nvgFillColor(args.vg, LIGHTBLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg,width+spacer,height-rmsL,width,rmsL,0.0f);
		nvgFill(args.vg);
		nvgClosePath(args.vg);
	}



	nvgStrokeWidth(args.vg, 2.0f);
	nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 255));
	nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 255));
	nvgBeginPath(args.vg);
	nvgMoveTo(args.vg, width+spacer, threshold);
	if (sc) {
		nvgLineTo(args.vg, 2*width+1.5f*spacer, threshold);
	}
	else {
		nvgLineTo(args.vg, 2*width+spacer, threshold);
	}
	nvgClosePath(args.vg);
	nvgStroke(args.vg);
	nvgFill(args.vg);

	float offset = 1.0f;
	nvgStrokeWidth(args.vg, 0.5f);
	nvgFillColor(args.vg, YELLOW_BIDOO);
	nvgStrokeColor(args.vg, YELLOW_BIDOO);
	nvgBeginPath(args.vg);
	nvgRoundedRect(args.vg,2.0f*(width+spacer)+offset,70.0f,width,-gain-makeup,0.0f);
	nvgClosePath(args.vg);
	nvgStroke(args.vg);
	nvgFill(args.vg);
	nvgRestore(args.vg);
}
};

struct LabelMICROBARWidget : TransparentWidget {
	float *value = NULL;
	const char *format = NULL;
	const char *header = "Ready";
	const char *tail = NULL;

	std::string fontPath = "res/DejaVuSansMono.ttf";

	LabelMICROBARWidget() {

	};

	void draw(const DrawArgs &args) override {
		std::shared_ptr<Font> font = APP->window->loadFont(asset::plugin(pluginInstance, fontPath));
		nvgGlobalTint(args.vg, color::WHITE);
		nvgFillColor(args.vg, YELLOW_BIDOO);
		nvgTextAlign(args.vg, NVG_ALIGN_LEFT);
		if (header) {
			nvgFontSize(args.vg, 12.0f);
			nvgText(args.vg, 0.0f, 0.0f, header, NULL);
		}
		if (value && format && tail) {
			char display[64];
			snprintf(display, sizeof(display), format, *value);
			nvgFontSize(args.vg, 12.0f);
			nvgText(args.vg, 0.0f, 10.0f, strcat(display,tail), NULL);
		}
	}
};

struct MicrobarTrimpotWithDisplay : BidooBlueTrimpot {
	LabelMICROBARWidget *lblDisplay = NULL;
	float *valueForDisplay = NULL;
	const char *format = NULL;
	const char *header = NULL;
	const char *tail = NULL;

	void onEnter(const event::Enter &e) override {
		if (lblDisplay && valueForDisplay && format && tail) {
			lblDisplay->value = valueForDisplay;
			lblDisplay->format = format;
			lblDisplay->tail = tail;
		}
		if (lblDisplay && header) lblDisplay->header = header;
		BidooBlueTrimpot::onEnter(e);
	}
};

struct MINIBARWidget : ModuleWidget {
	MINIBARWidget(MINIBAR *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/MINIBAR.svg")));

		if (module) {
			MINIBARDisplay *display = new MINIBARDisplay();
			display->module = module;
			display->box.pos = Vec(34.0f, 45.0f);
			display->box.size = Vec(70.0f, 70.0f);
			addChild(display);
		}

		LabelMICROBARWidget *display = new LabelMICROBARWidget();
		display->box.pos = Vec(31.5f, 287);
		addChild(display);

		addParam(createParam<MiniLEDButton>(Vec(34.0f, 15.0f), module, MINIBAR::BYPASS_PARAM));
		addChild(createLight<SmallLight<RedLight>>(Vec(34.0f, 15.0f), module, MINIBAR::BYPASS_LIGHT));

		MicrobarTrimpotWithDisplay* tresh = createParam<MicrobarTrimpotWithDisplay>(Vec(2.0f,37.0f), module, MINIBAR::THRESHOLD_PARAM);
		tresh->lblDisplay = display;
		tresh->valueForDisplay = module ? &module->threshold : NULL;
		tresh->format = "%2.1f";
		tresh->header = "Tresh.";
		tresh->tail = "dB";
		addParam(tresh);

		MicrobarTrimpotWithDisplay* ratio = createParam<MicrobarTrimpotWithDisplay>(Vec(2.0f,72.0f), module, MINIBAR::RATIO_PARAM);
		ratio->lblDisplay = display;
		ratio->valueForDisplay = module ? &module->ratio : NULL;
		ratio->format = "%1.0f:1";
		ratio->header = "Ratio";
		ratio->tail = " ";
		addParam(ratio);

		MicrobarTrimpotWithDisplay* attack = createParam<MicrobarTrimpotWithDisplay>(Vec(2.0f,107.0f), module, MINIBAR::ATTACK_PARAM);
		attack->lblDisplay = display;
		attack->valueForDisplay = module ? &module->attackTime : NULL;
		attack->format = "%1.0f";
		attack->header = "Attack";
		attack->tail = "ms";
		addParam(attack);

		MicrobarTrimpotWithDisplay* release = createParam<MicrobarTrimpotWithDisplay>(Vec(2.0f,142.0f), module, MINIBAR::RELEASE_PARAM);
		release->lblDisplay = display;
		release->valueForDisplay = module ? &module->releaseTime : NULL;
		release->format = "%1.0f";
		release->header = "Release";
		release->tail = "ms";
		addParam(release);

		MicrobarTrimpotWithDisplay* knee = createParam<MicrobarTrimpotWithDisplay>(Vec(2.0f,177.0f), module, MINIBAR::KNEE_PARAM);
		knee->lblDisplay = display;
		knee->valueForDisplay = module ? &module->knee : NULL;
		knee->format = "%1.1f";
		knee->header = "Knee";
		knee->tail = "dB";
		addParam(knee);

		MicrobarTrimpotWithDisplay* makeup = createParam<MicrobarTrimpotWithDisplay>(Vec(2.0f,212.0f), module, MINIBAR::MAKEUP_PARAM);
		makeup->lblDisplay = display;
		makeup->valueForDisplay = module ? &module->makeup : NULL;
		makeup->format = "%1.1f";
		makeup->header = "Gain";
		makeup->tail = "dB";
		addParam(makeup);

		MicrobarTrimpotWithDisplay* mix = createParam<MicrobarTrimpotWithDisplay>(Vec(2.0f,247.0f), module, MINIBAR::MIX_PARAM);
		mix->lblDisplay = display;
		mix->valueForDisplay = module ? &module->mixDisplay : NULL;
		mix->format = "%1.0f%";
		mix->header = "Mix";
		mix->tail = "%";
		addParam(mix);

		MicrobarTrimpotWithDisplay* looka = createParam<MicrobarTrimpotWithDisplay>(Vec(2.0f,282.0f), module, MINIBAR::LOOKAHEAD_PARAM);
		looka->lblDisplay = display;
		looka->valueForDisplay = module ? &module->lookAhead : NULL;
		looka->format = "%1.0f";
		looka->header = "Look.";
		looka->tail = "%";
		addParam(looka);

		addInput(createInput<TinyPJ301MPort>(Vec(6.0f, 340.0f), module, MINIBAR::IN_L_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(30.0f, 340.0f), module, MINIBAR::SC_L_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(54.0f, 340.0f), module, MINIBAR::OUT_L_OUTPUT));
	}
};

Model *modelMINIBAR = createModel<MINIBAR, MINIBARWidget>("mINIBar");
