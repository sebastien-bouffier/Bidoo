// dOSE: a polyphonic VCA whose envelope goes through a curve, from short exponential through linear to long
// exponential. The sound is in DoseCore.hpp; this is the ports, the panel and the screen.
#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "DoseCore.hpp"

struct DOSE : BidooModule {
	enum ParamIds {
		CURVE_PARAM,
		CURVE_CV_PARAM,
		LEVEL_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_INPUT,
		ENV_INPUT,
		CURVE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_OUTPUT,
		MIX_OUTPUT,
		ENV_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	dose::Engine engine;
	dose::Engine::Io io;

	DOSE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CURVE_PARAM, -1.f, 1.f, 0.f, "Curve", "%", 0.f, 100.f)->description =
			"0 %: the envelope as it is (linear). Left, to -100 %: exponential, shorter and snappier. "
			"Right, to +100 %: logarithmic, longer.";
		configParam(CURVE_CV_PARAM, -1.f, 1.f, 0.f, "Curve CV amount", "%", 0.f, 100.f);
		configParam(LEVEL_PARAM, 0.f, 2.f, 1.f, "Level", " dB", -10.f, 20.f)->description =
			"Output gain: 0 dB at 12 o'clock, +6 dB fully right";
		configInput(IN_INPUT, "Audio (polyphonic)");
		configInput(ENV_INPUT, "Envelope, 0..10 V (mono drives every channel, polyphonic maps channel to channel; unplugged: gain 1)");
		configInput(CURVE_INPUT, "Curve CV (+-5 V spans the knob's range)");
		configOutput(OUT_OUTPUT, "Audio (polyphonic, IN's channels)");
		configOutput(MIX_OUTPUT, "Mix of all channels");
		configOutput(ENV_OUTPUT, "Shaped envelope, 0..10 V (polyphonic)");
		configBypass(IN_INPUT, OUT_OUTPUT);
		engine.setSampleRate(APP->engine->getSampleRate());
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		engine.setSampleRate(e.sampleRate);
	}

	void onReset(const ResetEvent &e) override {
		BidooModule::onReset(e);
		engine.reset();
	}

	void process(const ProcessArgs &args) override {
		io.in = inputs[IN_INPUT].getVoltages();
		io.inChannels = inputs[IN_INPUT].getChannels();
		io.env = inputs[ENV_INPUT].getVoltages();
		io.envChannels = inputs[ENV_INPUT].getChannels();
		io.cv = inputs[CURVE_INPUT].getVoltages();
		io.cvChannels = inputs[CURVE_INPUT].getChannels();
		io.curveKnob = params[CURVE_PARAM].getValue();
		io.cvAmount = params[CURVE_CV_PARAM].getValue();
		io.levelKnob = params[LEVEL_PARAM].getValue();
		engine.process(io);
		outputs[OUT_OUTPUT].setChannels(io.outChannels);
		outputs[OUT_OUTPUT].writeVoltages(io.out);
		outputs[ENV_OUTPUT].setChannels(io.envOutChannels);
		outputs[ENV_OUTPUT].writeVoltages(io.envOut);
		outputs[MIX_OUTPUT].setVoltage(io.mix);
	}
};

// The screen: the curve in pink, and where each channel's envelope sits on it in yellow.
struct DoseScreen : TransparentWidget {
	DOSE *module = nullptr;

	void drawLayer(const DrawArgs &args, int layer) override {
		if (layer == 1)
			drawScreen(args.vg);
		TransparentWidget::drawLayer(args, layer);
	}

	static float curveY(float x, float curve) {
		return dose::shape(dose::float_4(x), dose::float_4(-dose::kMax * curve))[0];
	}

	void drawCurve(NVGcontext *vg, float curve, float l, float t, float s, NVGcolor colour, float width) {
		nvgBeginPath(vg);
		const int steps = 48;
		for (int i = 0; i <= steps; i++) {
			const float x = (float)i / steps;
			const float px = l + x * s, py = t + s - curveY(x, curve) * s;
			if (i == 0)
				nvgMoveTo(vg, px, py);
			else
				nvgLineTo(vg, px, py);
		}
		nvgStrokeColor(vg, colour);
		nvgStrokeWidth(vg, width);
		nvgStroke(vg);
	}

	void drawScreen(NVGcontext *vg) {
		const float w = box.size.x, h = box.size.y;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, w, h, 3.f);
		nvgFillColor(vg, theme::kScreenBlack);
		nvgFill(vg);

		// a square plot, centred: gain up, envelope across
		const float s = std::min(w, h) - 12.f;
		const float l = (w - s) * 0.5f, t = (h - s) * 0.5f;
		nvgBeginPath(vg);
		nvgRect(vg, l, t, s, s);
		nvgStrokeColor(vg, theme::kKeyOff);
		nvgStrokeWidth(vg, 0.7f);
		nvgStroke(vg);

		if (!module) {
			// in the module browser: the family, from short to long
			for (float c : {-1.f, -0.5f, 0.5f, 1.f})
				drawCurve(vg, c, l, t, s, nvgTransRGBAf(theme::kWavePink, 0.45f), 1.f);
			drawCurve(vg, 0.f, l, t, s, theme::kWavePink, 1.5f);
			return;
		}
		drawCurve(vg, module->engine.shownCurve, l, t, s, theme::kWavePink, 1.5f);
		if (!module->inputs[DOSE::ENV_INPUT].isConnected())
			return;
		const int n = clamp(module->engine.shownChannels, 0, dose::Engine::kMaxChannels);
		nvgBeginPath(vg);
		for (int c = 0; c < n; c++) {
			const float x = clamp(module->engine.shownX[c], 0.f, 1.f), y = clamp(module->engine.shownY[c], 0.f, 1.f);
			nvgCircle(vg, l + x * s, t + s - y * s, 1.8f);
		}
		nvgFillColor(vg, theme::kScreenYellow);
		nvgFill(vg);
	}
};

struct DOSEWidget : BidooWidget {
	DOSEWidget(DOSE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/DOSE.svg"));

		// 6 HP: the usual screws (theme::addScrews, 15 px in) would sit on the title and the logo, so they go in the
		// corners, as on Rack's narrow modules
		for (float top : {0.f, theme::kScrewBottomY}) {
			addChild(theme::atCorner(createWidget<ScrewSilver>(Vec()), 0.f, top));
			addChild(theme::atCorner(createWidget<ScrewSilver>(Vec()), box.size.x - 15.f, top));   // a screw is 15 px
		}

		// On the grid, two columns (tools/panels/dose.py). The knobs sit over the outer jacks (THEME.md, "Spacing"),
		// so every control of a column shares the jack's x: jackAt for all of them.
		const int n = 2;
		const float w = box.size.x;
		auto at = [&](float i, float k) { return theme::jackAt(i, n, k, w); };

		DoseScreen *screen = new DoseScreen;
		screen->module = module;
		// from the title down to above row 4's knob labels, between the side margins
		screen->box = theme::specificRect(theme::kSideMargin, theme::kScreenTop, w - 2.f * theme::kSideMargin,
			theme::screenAboveKnobs(4) - theme::kScreenTop);
		addChild(screen);

		addParam(theme::param<BidooBlueKnob>(at(0, 4), module, DOSE::CURVE_PARAM));
		addParam(theme::param<BidooBlueKnob>(at(1, 4), module, DOSE::LEVEL_PARAM));
		addParam(theme::param<BidooBlueTrimpot>(at(0, 3), module, DOSE::CURVE_CV_PARAM));
		addInput(theme::input<PJ301MPort>(at(1, 3), module, DOSE::CURVE_INPUT));
		addInput(theme::input<PJ301MPort>(at(0, 2), module, DOSE::IN_INPUT));
		addInput(theme::input<PJ301MPort>(at(1, 2), module, DOSE::ENV_INPUT));
		addOutput(theme::output<PJ301MPort>(at(0, 1), module, DOSE::OUT_OUTPUT));
		addOutput(theme::output<PJ301MPort>(at(1, 1), module, DOSE::MIX_OUTPUT));
		addOutput(theme::output<PJ301MPort>(at(0.5f, 0), module, DOSE::ENV_OUTPUT));
	}
};

Model *modelDOSE = createModel<DOSE, DOSEWidget>("DOSE");
