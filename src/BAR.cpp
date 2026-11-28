#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "BarCompressor.hpp"

using namespace std;

struct BAR : BidooModule {
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

	// what the display reads: the loudest channel and the deepest reduction
	float rms_L = -96.3f, vu_L = -96.3f, peakL = -96.3f, in_L_dBFS = -96.3f;
	float rms_R = -96.3f, vu_R = -96.3f, peakR = -96.3f, in_R_dBFS = -96.3f;
	float SC_rms_L = -96.3f, SC_vu_L = -96.3f, SC_peakL = -96.3f, SC_in_L_dBFS = -96.3f;
	float SC_rms_R = -96.3f, SC_vu_R = -96.3f, SC_peakR = -96.3f, SC_in_R_dBFS = -96.3f;
	float gaindB = 0.0f, ratio = 1.0f, threshold = 0.0f, knee = 0.0f;
	float attackTime = 10.0f, releaseTime = 10.0f, makeup = 0.0f, mix = 1.0f;
	int lookAhead = 0;

	// one stereo-linked compressor per channel
	BarSettings settings;
	BarPeakHold hold[PORT_MAX_CHANNELS];
	BarGain comp[PORT_MAX_CHANNELS];
	BarDelay delayL[PORT_MAX_CHANNELS], delayR[PORT_MAX_CHANNELS];
	BarCatcher catcher[PORT_MAX_CHANNELS];
	BarMeter meter[4];
	int lastChannels = 1;
	dsp::SchmittTrigger bypassTrigger;
	bool bypass = false;
	float bypassAmount = 0.0f;

	// the top of the knob is the limit, shown as the infinity sign
	struct RatioQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			return getValue() >= getMaxValue() ? "\xE2\x88\x9E" : ParamQuantity::getDisplayValueString();
		}
	};

	BAR() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(THRESHOLD_PARAM, -93.6f, 0.0f, 0.0f, "Threshold", " dB");
		configParam<RatioQuantity>(RATIO_PARAM, 1.0f, 20.0f, 1.0f, "Ratio", ":1");
		configParam(ATTACK_PARAM, 1.0f, 100.0f, 10.0f, "Attack", " ms");
		configParam(RELEASE_PARAM, 1.0f, 300.0f, 10.0f, "Release", " ms");
		configParam(KNEE_PARAM, 0.0f, 24.0f, 6.0f, "Knee", " dB");
		configParam(MAKEUP_PARAM, 0.0f, 60.0f, 0.0f, "Make-up gain", " dB");
		configParam(MIX_PARAM, 0.0f, 1.0f, 1.0f, "Mix", "%", 0.0f, 100.0f);
		configParam(LOOKAHEAD_PARAM, 0.0f, 200.0f, 0.0f, "Lookahead", "% of the attack");
		configButton(BYPASS_PARAM, "Bypass");
		configInput(IN_L_INPUT, "Left (polyphonic: one linked L/R compressor per channel)");
		configInput(IN_R_INPUT, "Right (follows left when unpatched)");
		configInput(SC_L_INPUT, "Sidechain left (mono drives every channel)");
		configInput(SC_R_INPUT, "Sidechain right (follows left when unpatched)");
		configOutput(OUT_L_OUTPUT, "Left (polyphonic)");
		configOutput(OUT_R_OUTPUT, "Right (polyphonic)");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
			delayL[c].resize(48000.0f);
			delayR[c].resize(48000.0f);
			catcher[c].resize(48000.0f);
		}
		settings.infiniteTop = true;
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		for (int c = 0; c < PORT_MAX_CHANNELS; c++) {
			delayL[c].resize(e.sampleRate);
			delayR[c].resize(e.sampleRate);
			catcher[c].resize(e.sampleRate);
		}
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "bypass", json_boolean(bypass));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		bypass = json_boolean_value(json_object_get(rootJ, "bypass"));
		bypassAmount = bypass ? 1.0f : 0.0f;
	}

	void process(const ProcessArgs &args) override;

};

void BAR::process(const ProcessArgs &args) {
	if (bypassTrigger.process(params[BYPASS_PARAM].getValue())) {
		bypass = !bypass;
	}
	lights[BYPASS_LIGHT].setBrightness(bypass ? 1.0f : 0.0f);

	settings.update(params[THRESHOLD_PARAM].getValue(), params[RATIO_PARAM].getValue(), params[ATTACK_PARAM].getValue(),
		params[RELEASE_PARAM].getValue(), params[KNEE_PARAM].getValue(), params[MAKEUP_PARAM].getValue(),
		params[MIX_PARAM].getValue(), params[LOOKAHEAD_PARAM].getValue(), args.sampleRate);
	threshold = settings.threshold; ratio = settings.ratio; attackTime = settings.attack; releaseTime = settings.release;
	knee = settings.knee; makeup = settings.makeup; mix = settings.mix; lookAhead = (int)settings.lookAhead;
	settings.glide();

	bool inConnected = inputs[IN_L_INPUT].isConnected() || inputs[IN_R_INPUT].isConnected();
	bool sc = inputs[SC_L_INPUT].isConnected() || inputs[SC_R_INPUT].isConnected();
	bool rConnected = inputs[IN_R_INPUT].isConnected(), scRConnected = inputs[SC_R_INPUT].isConnected();
	int channels = std::max({1, inputs[IN_L_INPUT].getChannels(), inputs[IN_R_INPUT].getChannels()});
	// channels coming back start from rest, not from audio left in their delays
	for (int c = lastChannels; c < channels; c++) {
		hold[c] = BarPeakHold();
		comp[c] = BarGain();
		delayL[c].resize(args.sampleRate);
		delayR[c].resize(args.sampleRate);
		catcher[c].reset();
	}
	lastChannels = channels;

	// bypassed, the lookahead delay stays: the audio is neither skipped nor
	// repeated when the bypass goes on or off
	float fadeDelay = 1.0f / (0.01f * args.sampleRate);
	int delay = settings.delay;
	float fade = 1.0f / (0.005f * args.sampleRate);
	bypassAmount = bypass ? std::min(bypassAmount + fade, 1.0f) : std::max(bypassAmount - fade, 0.0f);
	// the gain with no reduction (make-up and mix only): quiet channels skip the dB maths
	float restGain = barDbToGain(settings.makeupNow) * settings.mixNow + (1.0f - settings.mixNow);
	float mL = 0.0f, mR = 0.0f, mSL = 0.0f, mSR = 0.0f, deepest = 0.0f;

	for (int c = 0; c < channels; c++) {
		float inL = inputs[IN_L_INPUT].getNormalPolyVoltage(0.0f, c);
		float inR = rConnected ? inputs[IN_R_INPUT].getPolyVoltage(c) : inL;
		float scL = inputs[SC_L_INPUT].getNormalPolyVoltage(0.0f, c);
		float scR = scRConnected ? inputs[SC_R_INPUT].getPolyVoltage(c) : scL;

		// stereo linked: the louder side drives both
		float detect = sc ? std::max(std::fabs(scL), std::fabs(scR)) : std::max(std::fabs(inL), std::fabs(inR));
		float level = hold[c].process(detect, settings.fall);
		float reduction;
		if (level < settings.quietBelow && comp[c].reduction > -1e-4f) {
			// under the threshold and the knee, nothing to release: no reduction
			comp[c].reduction = 0.0f;
			reduction = 0.0f;
		}
		else {
			reduction = comp[c].process(barToDbFast(level), settings);
		}

		float dL = delayL[c].process(inL, delay, fadeDelay);
		float dR = delayR[c].process(inR, delay, fadeDelay);

		// at the top of the RATIO knob (the limit) with lookahead, the catcher follows the level
		// ahead of the delayed audio and holds the delayed audio under the threshold
		if (settings.limit && delayL[c].longest() > 0) {
			catcher[c].running = true;
			float catchGain = catcher[c].process(detect, settings.thresholdLin, delayL[c].longest() + 1,
				std::min(delayL[c].shortest() + 1, settings.attackSamples), settings.cRel);
			if (catchGain < 1.0f)
				reduction = std::min(reduction, 6.02059991f * barFastLog2(catchGain));
		}
		else if (catcher[c].running) {
			catcher[c].reset();
		}
		deepest = std::min(deepest, reduction);
		float g = reduction == 0.0f ? restGain
			: barDbToGain(settings.makeupNow + reduction) * settings.mixNow + (1.0f - settings.mixNow);
		g += (1.0f - g) * bypassAmount;

		outputs[OUT_L_OUTPUT].setVoltage(bidooSoftLimit(dL * g), c);
		outputs[OUT_R_OUTPUT].setVoltage(bidooSoftLimit(dR * g), c);

		if (std::fabs(inL) > std::fabs(mL)) mL = inL;
		if (std::fabs(inR) > std::fabs(mR)) mR = inR;
		if (std::fabs(scL) > std::fabs(mSL)) mSL = scL;
		if (std::fabs(scR) > std::fabs(mSR)) mSR = scR;
	}
	outputs[OUT_L_OUTPUT].setChannels(channels);
	outputs[OUT_R_OUTPUT].setChannels(channels);
	gaindB = settings.makeupNow + deepest;

	if (inConnected) {
		meter[0].process(mL, args.sampleRate);
		meter[1].process(mR, args.sampleRate);
	}
	else {
		meter[0].clear();
		meter[1].clear();
	}
	if (sc) {
		meter[2].process(mSL, args.sampleRate);
		meter[3].process(mSR, args.sampleRate);
	}
	else {
		meter[2].clear();
		meter[3].clear();
	}
	rms_L = meter[0].rmsDb; vu_L = meter[0].vuDb; peakL = meter[0].peakDb; in_L_dBFS = meter[0].inDb;
	rms_R = meter[1].rmsDb; vu_R = meter[1].vuDb; peakR = meter[1].peakDb; in_R_dBFS = meter[1].inDb;
	SC_rms_L = meter[2].rmsDb; SC_vu_L = meter[2].vuDb; SC_peakL = meter[2].peakDb; SC_in_L_dBFS = meter[2].inDb;
	SC_rms_R = meter[3].rmsDb; SC_vu_R = meter[3].vuDb; SC_peakR = meter[3].peakDb; SC_in_R_dBFS = meter[3].inDb;
}

struct BARDisplay : TransparentWidget {
	BAR *module;
	float height = 150.0f;
	float width = 15.0f;
	float spacer = 3.0f;
	float width2 = width/2;
	float spacer2 = spacer/2;
	float widthSpacer =  width + spacer;

	BARDisplay() {

	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
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

				nvgFillColor(args.vg, theme::kWavePink);
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

				nvgFillColor(args.vg, theme::kLitOrange);
				nvgBeginPath(args.vg);
				if (inL>rmsL+3.0f)
					nvgRoundedRect(args.vg,width+spacer2-width2,max(height-inL+1.0f,0.f),width2,inL-rmsL-2.0f,0.0f);
				if (inR>rmsR+3.0f)
					nvgRoundedRect(args.vg,3.0f*widthSpacer-spacer2,max(height-inR+1.0f,0.f),width2,inR-rmsR-2.0f,0.0f);
				nvgFill(args.vg);
				nvgClosePath(args.vg);

				nvgFillColor(args.vg, theme::kKeyWhite);
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,width+spacer2-width2,height-rmsL,width2,rmsL,0.0f);
				nvgRoundedRect(args.vg,3.0f*widthSpacer-spacer2,height-rmsR,width2,rmsR,0.0f);
				nvgFill(args.vg);
				nvgClosePath(args.vg);



				nvgFillColor(args.vg, theme::kWavePink);
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

				nvgFillColor(args.vg, theme::kLitOrange);
				nvgBeginPath(args.vg);
				if (SC_inL>SC_rmsL+3.0f)
					nvgRoundedRect(args.vg,widthSpacer+width2+spacer2,max(height-SC_inL+1.0f,0.f),width2,SC_inL-SC_rmsL-2.0f,0.0f);
				if (SC_inR>SC_rmsR+3.0f)
					nvgRoundedRect(args.vg,2.0f*widthSpacer-spacer2,max(height-SC_inR+1.0f,0.f),width2,SC_inR-SC_rmsR-2.0f,0.0f);
				nvgFill(args.vg);
				nvgClosePath(args.vg);

				nvgFillColor(args.vg, theme::kKeyWhite);
				nvgBeginPath(args.vg);
				nvgRoundedRect(args.vg,widthSpacer+width2+spacer2,height-SC_rmsL,width2,SC_rmsL,0.0f);
				nvgRoundedRect(args.vg,2.0f*widthSpacer-spacer2,height-SC_rmsR,width2,SC_rmsR,0.0f);
				nvgFill(args.vg);
				nvgClosePath(args.vg);

			}
			else {
				nvgSave(args.vg);
				nvgStrokeWidth(args.vg, 0.0f);

				nvgFillColor(args.vg, theme::kWavePink);
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

				nvgFillColor(args.vg, theme::kLitOrange);
				nvgBeginPath(args.vg);
				if (inL>rmsL+3.0f)
					nvgRoundedRect(args.vg,widthSpacer,max(height-inL+1.0f,0.f),width,inL-rmsL-2.0f,0.0f);
				if (inR>rmsR+3.0f)
					nvgRoundedRect(args.vg,2.0f*widthSpacer,max(height-inR+1.0f,0.f),width,inR-rmsR-2.0f,0.0f);
				nvgFill(args.vg);
				nvgClosePath(args.vg);

				nvgFillColor(args.vg, theme::kKeyWhite);
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
			std::string sTresh,sRat,sAtt,sKnee,sMu,sMix,sLa;
			sTresh = "TdB";
			sRat = "R";
			sAtt = "A/Rms";
			sKnee = "K";
			sMu = "GdB";
			sMix = "M";
			sLa = "L%";
			snprintf(tTresh, sizeof(tTresh), "%2.1f", module->threshold);
			if (module->ratio >= module->paramQuantities[BAR::RATIO_PARAM]->getMaxValue())
				snprintf(tRatio, sizeof(tRatio), "\xE2\x88\x9E:1");
			else
				snprintf(tRatio, sizeof(tTresh), "%2.0f:1", module->ratio);
			snprintf(tAtt, sizeof(tTresh), "%1.0f/%1.0f", module->attackTime,module->releaseTime);
			snprintf(tRel, sizeof(tTresh), "%3.0f", module->releaseTime);
			snprintf(tKnee, sizeof(tTresh), "%2.1f", module->knee);
			snprintf(tMakeUp, sizeof(tTresh), "%2.1f", module->makeup);
			snprintf(tMix, sizeof(tTresh), "%1.0f/%1.0f", (1-module->mix)*100,module->mix*100);
			snprintf(tLookAhead, sizeof(tTresh), "%3i", module->lookAhead);
			nvgFontSize(args.vg, 14.0f);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgText(args.vg, 8.0f, height+20.0f, sTresh.c_str(), NULL);
			nvgText(args.vg, 50.0f, height+20.0f, sRat.c_str(), NULL);
			nvgText(args.vg, 96.0f, height+20.0f, sAtt.c_str(), NULL);
			nvgText(args.vg, 8.0f, height+52.0f, sKnee.c_str(), NULL);
			nvgText(args.vg, 40.0f, height+52.0f, sMu.c_str(), NULL);
			nvgText(args.vg, 75.0f, height+52.0f, sMix.c_str(), NULL);
			nvgText(args.vg, 107.0f, height+52.0f, sLa.c_str(), NULL);

			nvgText(args.vg, 8.0f, height+31.0f, tTresh, NULL);
			nvgText(args.vg, 50.0f, height+31.0f, tRatio, NULL);
			nvgText(args.vg, 96.0f, height+31.0f, tAtt, NULL);
			nvgText(args.vg, 8.0f, height+63.0f, tKnee, NULL);
			nvgText(args.vg, 40.0f, height+63.0f, tMakeUp, NULL);
			nvgText(args.vg, 75.0f, height+63.0f, tMix, NULL);
			nvgText(args.vg, 107.0f, height+63.0f, tLookAhead, NULL);
			nvgRestore(args.vg);
		}
		Widget::drawLayer(args, layer);
	}

};

struct BARWidget : BidooWidget {
	BARWidget(BAR *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/BAR.svg"));

		theme::addScrews(this);

		// The panel of tools/panels/bar.py: the old arrangement, the bypass button, the meters and the trimpots at their old
		// corners (specific: they are the panel's picture of the module), the stereo jacks centred in their boxes.
		const float w = box.size.x;

		// The meters and readouts: drawn from the display's corner, as before, the meters 150 px down and the readouts to
		// 253. The screen behind them is drawn by the panel (bar.py SIDE, SCREEN_BOTTOM: 6 px from the sides, to 259)
		if (module) {
			BARDisplay *display = new BARDisplay();
			display->module = module;
			display->box = theme::specificRect(12, 40, 110, 213);
			addChild(display);
		}

		// The bypass button beside the title, at the old light's centre: a BidooRoundButton, momentary, lit while the
		// module is bypassed, as mINIBar's. 14 px: the room between the title and the right screw is 22 px (20 would
		// leave 1 px a side); 5 px to the title, 3 to the screw
		const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};     // the old SmallLight: its centre
		const float kBypassButton = 14.f;
		const theme::Pos bypassAt = theme::specificCorner(91, 11, kSmallLightSize);
		BidooRoundButton *bypassButton = theme::param<BidooRoundButton>(bypassAt, module, BAR::BYPASS_PARAM);
		bypassButton->momentary = true;
		bypassButton->box.size = Vec(kBypassButton, kBypassButton);   // smaller: placed again at the same centre
		theme::place(bypassButton, bypassAt);
		bypassButton->litBy = [=]() { return module && module->lights[BAR::BYPASS_LIGHT].getBrightness() > 0.5f; };
		addParam(bypassButton);

		// The trimpots: two rows of four under the screen, from the old first corner
		const float kTrimpotPitch = 32.f;   // across
		const float kTrimpotRowBelow = 26.f;
		const theme::Pos firstTrimpot = theme::specificCorner(10, 265, theme::kTrimpotSize);
		const int trimpotIds[8] = {BAR::THRESHOLD_PARAM, BAR::RATIO_PARAM, BAR::ATTACK_PARAM, BAR::RELEASE_PARAM,
			BAR::KNEE_PARAM, BAR::MAKEUP_PARAM, BAR::MIX_PARAM, BAR::LOOKAHEAD_PARAM};
		for (int i = 0; i < 8; i++)
			addParam(theme::param<BidooBlueTrimpot>(firstTrimpot.offset(kTrimpotPitch * (i % 4), kTrimpotRowBelow * (i / 4)), module, trimpotIds[i]));

		// The stereo IN, SC and OUT: three boxes 6 px from the sides with the theme's gap between them, the tiny L and R
		// jacks (six full jacks don't fit across 9 HP) 18 px apart centred in each, at the old height (corner 339). The
		// same in tools/panels/bar.py (SIDE, STEREO_RIGHT, STEREO_Y)
		const float kSide = 6.f;
		const float kStereoRight = 18.f;   // from L to R in a box
		const float kStereoY = 339.f + theme::kTinyJackSize.h * 0.5f;
		const float kBoxW = (w - 2.f * kSide - 2.f * theme::kBoxGap) / 3.f;
		auto left = [&](int i) { return theme::specific(kSide + kBoxW * (i + 0.5f) + theme::kBoxGap * i - kStereoRight * 0.5f, kStereoY); };
		addInput(theme::input<TinyPJ301MPort>(left(0), module, BAR::IN_L_INPUT));
		addInput(theme::input<TinyPJ301MPort>(left(0).right(kStereoRight), module, BAR::IN_R_INPUT));
		addInput(theme::input<TinyPJ301MPort>(left(1), module, BAR::SC_L_INPUT));
		addInput(theme::input<TinyPJ301MPort>(left(1).right(kStereoRight), module, BAR::SC_R_INPUT));
		addOutput(theme::output<TinyPJ301MPort>(left(2), module, BAR::OUT_L_OUTPUT));
		addOutput(theme::output<TinyPJ301MPort>(left(2).right(kStereoRight), module, BAR::OUT_R_OUTPUT));
	}
};

Model *modelBAR = createModel<BAR, BARWidget>("baR");
