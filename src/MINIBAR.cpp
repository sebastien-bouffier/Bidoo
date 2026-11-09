#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "BarCompressor.hpp"

using namespace std;

struct MINIBAR : BidooModule {
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

	// what the display reads: the loudest channel and the deepest reduction
	float rms_L = -96.3f, vu_L = -96.3f, peakL = -96.3f, in_L_dBFS = -96.3f;
	float SC_rms_L = -96.3f, SC_vu_L = -96.3f, SC_peakL = -96.3f, SC_in_L_dBFS = -96.3f;
	float gaindB = 0.0f, ratio = 1.0f, threshold = 0.0f, knee = 0.0f;
	float attackTime = 10.0f, releaseTime = 10.0f, makeup = 0.0f, mix = 1.0f, mixDisplay = 100.0f;
	float lookAhead = 0.0f;

	BarSettings settings;
	BarPeakHold hold[PORT_MAX_CHANNELS];
	BarGain comp[PORT_MAX_CHANNELS];
	BarDelay delay[PORT_MAX_CHANNELS];
	BarMeter meter, scMeter;
	dsp::SchmittTrigger bypassTrigger;
	bool bypass = false;
	float bypassAmount = 0.0f;

	MINIBAR() {
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
		configInput(IN_L_INPUT, "Audio (one compressor per channel)");
		configInput(SC_L_INPUT, "Sidechain (mono drives every channel)");
		configOutput(OUT_L_OUTPUT, "Audio");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		for (int c = 0; c < PORT_MAX_CHANNELS; c++)
			delay[c].resize(48000.0f);
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		for (int c = 0; c < PORT_MAX_CHANNELS; c++)
			delay[c].resize(e.sampleRate);
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

void MINIBAR::process(const ProcessArgs &args) {
	if (bypassTrigger.process(params[BYPASS_PARAM].getValue())) {
		bypass = !bypass;
	}
	lights[BYPASS_LIGHT].setBrightness(bypass ? 1.0f : 0.0f);

	settings.update(params[THRESHOLD_PARAM].getValue(), params[RATIO_PARAM].getValue(), params[ATTACK_PARAM].getValue(),
		params[RELEASE_PARAM].getValue(), params[KNEE_PARAM].getValue(), params[MAKEUP_PARAM].getValue(),
		params[MIX_PARAM].getValue(), params[LOOKAHEAD_PARAM].getValue(), args.sampleRate);
	threshold = settings.threshold; ratio = settings.ratio; attackTime = settings.attack; releaseTime = settings.release;
	knee = settings.knee; makeup = settings.makeup; mix = settings.mix; mixDisplay = mix * 100.0f;
	lookAhead = std::floor(settings.lookAhead);
	settings.glide();

	float fade = 1.0f / (0.005f * args.sampleRate);
	bypassAmount = bypass ? std::min(bypassAmount + fade, 1.0f) : std::max(bypassAmount - fade, 0.0f);
	float fadeDelay = 1.0f / (0.01f * args.sampleRate);
	// bypassed, the lookahead delay stays: the audio is neither skipped nor
	// repeated when the bypass goes on or off
	int delayLen = settings.delay;

	int channels = std::max(1, inputs[IN_L_INPUT].getChannels());
	bool sc = inputs[SC_L_INPUT].isConnected();
	// a sidechain with fewer channels than the audio wraps around (mono drives all)
	int scChannels = std::max(1, inputs[SC_L_INPUT].getChannels());
	float loudest = 0.0f, scLoudest = 0.0f, deepest = 0.0f;
	// the gain with no reduction (make-up and mix only): quiet channels skip the dB maths
	float restGain = barDbToGain(settings.makeupNow) * settings.mixNow + (1.0f - settings.mixNow);
	for (int c = 0; c < channels; c++) {
		float in = inputs[IN_L_INPUT].getVoltage(c);
		float scIn = inputs[SC_L_INPUT].getVoltage(c % scChannels);
		float level = hold[c].process(sc ? scIn : in, settings.fall);
		float reduction, g;
		if (level < settings.quietBelow && comp[c].reduction > -1e-4f) {
			// under the threshold and the knee, nothing to release: no reduction
			comp[c].reduction = 0.0f;
			reduction = 0.0f;
			g = restGain;
		}
		else {
			reduction = comp[c].process(barToDbFast(level), settings);
			g = barDbToGain(settings.makeupNow + reduction) * settings.mixNow + (1.0f - settings.mixNow);
		}
		deepest = std::min(deepest, reduction);
		g += (1.0f - g) * bypassAmount;
		outputs[OUT_L_OUTPUT].setVoltage(bidooSoftLimit(delay[c].process(in, delayLen, fadeDelay) * g), c);
		if (std::fabs(in) > std::fabs(loudest))
			loudest = in;
		if (std::fabs(scIn) > std::fabs(scLoudest))
			scLoudest = scIn;
	}
	outputs[OUT_L_OUTPUT].setChannels(channels);
	gaindB = settings.makeupNow + deepest;

	if (inputs[IN_L_INPUT].isConnected())
		meter.process(loudest, args.sampleRate);
	else
		meter.clear();
	if (sc)
		scMeter.process(scLoudest, args.sampleRate);
	else
		scMeter.clear();
	rms_L = meter.rmsDb; vu_L = meter.vuDb; peakL = meter.peakDb; in_L_dBFS = meter.inDb;
	SC_rms_L = scMeter.rmsDb; SC_vu_L = scMeter.vuDb; SC_peakL = scMeter.peakDb; SC_in_L_dBFS = scMeter.inDb;
}

struct MINIBARDisplay : TransparentWidget {
	MINIBAR *module;
	float height = 230.0f;
	float width = 10.0f;
	float spacer = 2.0f;
	MINIBARDisplay() {

	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
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
				if (SC_inL>SC_rmsL+3.0f)
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
		Widget::drawLayer(args, layer);
	}

};

struct LabelMICROBARWidget : TransparentWidget {
	float *value = NULL;
	const char *format = NULL;
	const char *header = "Ready";
	const char *tail = NULL;

	LabelMICROBARWidget() {

	};

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
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
		Widget::drawLayer(args, layer);
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

struct MINIBARWidget : BidooWidget {
	MINIBARWidget(MINIBAR *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/MINIBAR.svg"));

		// No screws, as before. The original hand-drawn panel: the bypass button and its light under the title, on the
		// panel's centre like the title; eight trimpots down the left, each beside its upright label (the old code had
		// them 1 px above the labels' middles); the screen; three tiny jacks in their boxes, within 0.4 px, left as
		// they were.
		if (module) {
			MINIBARDisplay *display = new MINIBARDisplay();
			display->module = module;
			display->box = theme::specificRect(34, 45, 70, 70);
			addChild(display);
		}

		LabelMICROBARWidget *display = new LabelMICROBARWidget();
		display->box.pos = theme::specific(31.5f, 287);   // where the screen writes the value being edited
		addChild(display);

		const float centre = box.size.x * 0.5f;
		const theme::Size kMiniButtonSize = {mm2px(1.8f), mm2px(1.8f)};   // MiniLEDButton
		const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};     // SmallLight
		addParam(theme::param<MiniLEDButton>(theme::sameRow(theme::specificCorner(34, 15, kMiniButtonSize), centre), module, MINIBAR::BYPASS_PARAM));
		addChild(theme::light<SmallLight<RedLight>>(theme::sameRow(theme::specificCorner(34, 15, kSmallLightSize), centre), module, MINIBAR::BYPASS_LIGHT));

		const float kTrimpotPitch = 35.f;
		const theme::Pos first = theme::specificCorner(2, 38, theme::kTrimpotSize);
		auto trimpot = [&](int i) { return first.down(kTrimpotPitch * i); };

		MicrobarTrimpotWithDisplay* tresh = theme::param<MicrobarTrimpotWithDisplay>(trimpot(0), module, MINIBAR::THRESHOLD_PARAM);
		tresh->lblDisplay = display;
		tresh->valueForDisplay = module ? &module->threshold : NULL;
		tresh->format = "%2.1f";
		tresh->header = "Thresh.";
		tresh->tail = "dB";
		addParam(tresh);

		MicrobarTrimpotWithDisplay* ratio = theme::param<MicrobarTrimpotWithDisplay>(trimpot(1), module, MINIBAR::RATIO_PARAM);
		ratio->lblDisplay = display;
		ratio->valueForDisplay = module ? &module->ratio : NULL;
		ratio->format = "%1.0f:1";
		ratio->header = "Ratio";
		ratio->tail = " ";
		addParam(ratio);

		MicrobarTrimpotWithDisplay* attack = theme::param<MicrobarTrimpotWithDisplay>(trimpot(2), module, MINIBAR::ATTACK_PARAM);
		attack->lblDisplay = display;
		attack->valueForDisplay = module ? &module->attackTime : NULL;
		attack->format = "%1.0f";
		attack->header = "Attack";
		attack->tail = "ms";
		addParam(attack);

		MicrobarTrimpotWithDisplay* release = theme::param<MicrobarTrimpotWithDisplay>(trimpot(3), module, MINIBAR::RELEASE_PARAM);
		release->lblDisplay = display;
		release->valueForDisplay = module ? &module->releaseTime : NULL;
		release->format = "%1.0f";
		release->header = "Release";
		release->tail = "ms";
		addParam(release);

		MicrobarTrimpotWithDisplay* knee = theme::param<MicrobarTrimpotWithDisplay>(trimpot(4), module, MINIBAR::KNEE_PARAM);
		knee->lblDisplay = display;
		knee->valueForDisplay = module ? &module->knee : NULL;
		knee->format = "%1.1f";
		knee->header = "Knee";
		knee->tail = "dB";
		addParam(knee);

		MicrobarTrimpotWithDisplay* makeup = theme::param<MicrobarTrimpotWithDisplay>(trimpot(5), module, MINIBAR::MAKEUP_PARAM);
		makeup->lblDisplay = display;
		makeup->valueForDisplay = module ? &module->makeup : NULL;
		makeup->format = "%1.1f";
		makeup->header = "Gain";
		makeup->tail = "dB";
		addParam(makeup);

		MicrobarTrimpotWithDisplay* mix = theme::param<MicrobarTrimpotWithDisplay>(trimpot(6), module, MINIBAR::MIX_PARAM);
		mix->lblDisplay = display;
		mix->valueForDisplay = module ? &module->mixDisplay : NULL;
		mix->format = "%1.0f";
		mix->header = "Mix";
		mix->tail = "%";
		addParam(mix);

		MicrobarTrimpotWithDisplay* looka = theme::param<MicrobarTrimpotWithDisplay>(trimpot(7), module, MINIBAR::LOOKAHEAD_PARAM);
		looka->lblDisplay = display;
		looka->valueForDisplay = module ? &module->lookAhead : NULL;
		looka->format = "%1.0f";
		looka->header = "Look.";
		looka->tail = "%";
		addParam(looka);

		const float kJackPitch = 24.f;
		const theme::Pos in = theme::specificCorner(6, 340, theme::kTinyJackSize);
		addInput(theme::input<TinyPJ301MPort>(in, module, MINIBAR::IN_L_INPUT));
		addInput(theme::input<TinyPJ301MPort>(in.right(kJackPitch), module, MINIBAR::SC_L_INPUT));
		addOutput(theme::output<TinyPJ301MPort>(in.right(2.f * kJackPitch), module, MINIBAR::OUT_L_OUTPUT));
	}
};

Model *modelMINIBAR = createModel<MINIBAR, MINIBARWidget>("mINIBar");
