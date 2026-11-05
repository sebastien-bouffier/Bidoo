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

	// what the display reads
	float rms_L = -96.3f, vu_L = -96.3f, peakL = -96.3f, in_L_dBFS = -96.3f;
	float rms_R = -96.3f, vu_R = -96.3f, peakR = -96.3f, in_R_dBFS = -96.3f;
	float SC_rms_L = -96.3f, SC_vu_L = -96.3f, SC_peakL = -96.3f, SC_in_L_dBFS = -96.3f;
	float SC_rms_R = -96.3f, SC_vu_R = -96.3f, SC_peakR = -96.3f, SC_in_R_dBFS = -96.3f;
	float gaindB = 0.0f, ratio = 1.0f, threshold = 0.0f, knee = 0.0f;
	float attackTime = 10.0f, releaseTime = 10.0f, makeup = 0.0f, mix = 1.0f;
	int lookAhead = 0;

	BarSettings settings;
	BarPeakHold hold;
	BarGain comp;
	BarDelay delayL, delayR;
	BarMeter meter[4];
	dsp::SchmittTrigger bypassTrigger;
	bool bypass = false;
	float bypassAmount = 0.0f;

	BAR() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(THRESHOLD_PARAM, -93.6f, 0.0f, 0.0f, "Threshold", " dB");
		configParam(RATIO_PARAM, 1.0f, 20.0f, 1.0f, "Ratio", ":1");
		configParam(ATTACK_PARAM, 1.0f, 100.0f, 10.0f, "Attack", " ms");
		configParam(RELEASE_PARAM, 1.0f, 300.0f, 10.0f, "Release", " ms");
		configParam(KNEE_PARAM, 0.0f, 24.0f, 6.0f, "Knee", " dB");
		configParam(MAKEUP_PARAM, 0.0f, 60.0f, 0.0f, "Make-up gain", " dB");
		configParam(MIX_PARAM, 0.0f, 1.0f, 1.0f, "Mix", "%", 0.0f, 100.0f);
		configParam(LOOKAHEAD_PARAM, 0.0f, 200.0f, 0.0f, "Lookahead", "% of the attack");
		configButton(BYPASS_PARAM, "Bypass");
		configInput(IN_L_INPUT, "Left");
		configInput(IN_R_INPUT, "Right (follows left when unpatched)");
		configInput(SC_L_INPUT, "Sidechain left");
		configInput(SC_R_INPUT, "Sidechain right (follows left when unpatched)");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		delayL.resize(48000.0f);
		delayR.resize(48000.0f);
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		delayL.resize(e.sampleRate);
		delayR.resize(e.sampleRate);
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

	bool inConnected = inputs[IN_L_INPUT].isConnected() || inputs[IN_R_INPUT].isConnected();
	bool sc = inputs[SC_L_INPUT].isConnected() || inputs[SC_R_INPUT].isConnected();
	float inL = inputs[IN_L_INPUT].getVoltage();
	float inR = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltage() : inL;
	float scL = inputs[SC_L_INPUT].getVoltage();
	float scR = inputs[SC_R_INPUT].isConnected() ? inputs[SC_R_INPUT].getVoltage() : scL;

	// stereo linked: the louder side drives both
	float level = hold.process(sc ? std::max(std::fabs(scL), std::fabs(scR)) : std::max(std::fabs(inL), std::fabs(inR)), settings.bucket);
	float reduction = comp.process(barToDb(level), settings);
	gaindB = settings.makeup + reduction;
	float g = barDbToGain(gaindB) * settings.mix + (1.0f - settings.mix);

	float fade = 1.0f / (0.005f * args.sampleRate);
	bypassAmount = bypass ? std::min(bypassAmount + fade, 1.0f) : std::max(bypassAmount - fade, 0.0f);
	g += (1.0f - g) * bypassAmount;

	// bypassed: no lookahead delay either (the delay crossfades to 0)
	float fadeDelay = 1.0f / (0.01f * args.sampleRate);
	int delay = bypass ? 0 : settings.delay;
	outputs[OUT_L_OUTPUT].setVoltage(bidooSoftLimit(delayL.process(inL, delay, fadeDelay) * g));
	outputs[OUT_R_OUTPUT].setVoltage(bidooSoftLimit(delayR.process(inR, delay, fadeDelay) * g));

	if (inConnected) {
		meter[0].process(inL, args.sampleRate);
		meter[1].process(inR, args.sampleRate);
	}
	else {
		meter[0].clear();
		meter[1].clear();
	}
	if (sc) {
		meter[2].process(scL, args.sampleRate);
		meter[3].process(scR, args.sampleRate);
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
				if (SC_inL>SC_rmsL+3.0f)
					nvgRoundedRect(args.vg,widthSpacer+width2+spacer2,max(height-SC_inL+1.0f,0.f),width2,SC_inL-SC_rmsL-2.0f,0.0f);
				if (SC_inR>SC_rmsR+3.0f)
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
			std::string sTresh,sRat,sAtt,sKnee,sMu,sMix,sLa;
			sTresh = "TdB";
			sRat = "R";
			sAtt = "A/Rms";
			sKnee = "K";
			sMu = "GdB";
			sMix = "M";
			sLa = "L%";
			snprintf(tTresh, sizeof(tTresh), "%2.1f", module->threshold);
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
