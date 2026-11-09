// rICOcHEt: a stereo delay with six characters: Digital, Tape, BBD, Lo-fi, Diffuse, Reverse. The sound is all in
// bidoo-dsp (delay/CharacterDelay.h, copied unchanged in src/dep/bidoo-dsp); this is the panel, the jacks and the clock.
#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"
#include <bidoo/dsp/delay/CharacterDelay.h>
#include <bidoo/dsp/time/PulseClock.h>

namespace ricochet {
static const char *const kTypes[] = {"DIGITAL", "TAPE", "BBD", "LO-FI", "DIFFUSE", "REVERSE"};
// With a clock, TIME picks a ratio of the clock's period
static const double kRatios[] = {1.0 / 8.0, 1.0 / 6.0, 1.0 / 4.0, 1.0 / 3.0, 3.0 / 8.0, 1.0 / 2.0, 2.0 / 3.0, 3.0 / 4.0, 1.0, 4.0 / 3.0, 3.0 / 2.0, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0};
static const char *const kRatioNames[] = {"1/8", "1/6", "1/4", "1/3", "3/8", "1/2", "2/3", "3/4", "1", "4/3", "3/2", "2", "3", "4", "6", "8", "12", "16"};
// How many pulses a whole number of slices takes: the numerator of the ratio
static const int kRatioPulses[] = {1, 1, 1, 1, 3, 1, 2, 3, 1, 4, 3, 2, 3, 4, 6, 8, 12, 16};
static constexpr int kNumRatios = 18;
static constexpr int kColumns = 198; // one per pixel of the screen

static inline double secondsOfKnob(float x) {
	return 0.002 * std::pow(4000.0, (double)x); // 2 ms .. 8 s
}
static inline int ratioOfKnob(float x) {
	return clamp((int)(x * kNumRatios), 0, kNumRatios - 1);
}
static inline std::string timeText(double seconds) {
	return seconds >= 1.0 ? rack::string::f("%.2f s", seconds) : rack::string::f("%.1f ms", seconds * 1000.0);
}
}

struct RICOCHET : BidooModule {
	enum ParamIds {
		TYPE_PARAM,
		TIME_PARAM,
		FEEDBACK_PARAM,
		MIX_PARAM,
		TONE_PARAM,
		CHARACTER_PARAM,
		SPREAD_PARAM,
		MODE_PARAM,
		FREEZE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		TIME_INPUT,
		FEEDBACK_INPUT,
		TONE_INPUT,
		MIX_INPUT,
		CHARACTER_INPUT,
		TYPE_INPUT,
		CLOCK_INPUT,
		FREEZE_INPUT,
		L_INPUT,
		R_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		L_OUTPUT,
		R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	static constexpr float kVolts = 5.f;
	static constexpr int kControl = 16;

	struct TimeQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			RICOCHET *m = dynamic_cast<RICOCHET *>(module);
			if (m && m->clocked)
				return std::string("x") + ricochet::kRatioNames[ricochet::ratioOfKnob(getValue())] + " clock";
			return ricochet::timeText(ricochet::secondsOfKnob(getValue()));
		}
		void setDisplayValueString(std::string s) override {
			const double ms = std::atof(s.c_str());
			if (ms > 0.0)
				setValue(clamp((float)(std::log(ms * 0.001 / 0.002) / std::log(4000.0)), 0.f, 1.f));
		}
		std::string getUnit() override {
			return "";
		}
	};

	bidoo::dsp::CharacterDelay delay;
	bidoo::dsp::PulseClock clock;
	dsp::SchmittTrigger clockIn, freezeIn;
	int tick = 0;
	bool clocked = false, frozen = false;
	long pulses = 0;
	int shownType = 0;
	double shownSeconds = 0.3;
	int shownRatio = -1;

	// the screen: a rolling peak of the echoes, one column per slice of time
	float peaks[2][ricochet::kColumns] = {};
	float peakNow[2] = {};
	int peakWrite = 0, peakCount = 0, peakLength = 1000;

	RICOCHET() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(TYPE_PARAM, 0.f, 5.f, 1.f, "Type", {"Digital", "Tape", "BBD", "Lo-fi", "Diffuse", "Reverse"});
		configParam<TimeQuantity>(TIME_PARAM, 0.f, 1.f, 0.62f, "Time");
		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.45f, "Feedback", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 0.35f, "Mix", "%", 0.f, 100.f);
		configParam(TONE_PARAM, -1.f, 1.f, 0.f, "Tone (dark, centre flat, thin)", "%", 0.f, 100.f);
		configParam(CHARACTER_PARAM, 0.f, 1.f, 0.4f, "Character", "%", 0.f, 100.f);
		configParam(SPREAD_PARAM, -1.f, 1.f, 0.f, "Spread (one side up to 1.5 x longer)", "%", 0.f, 100.f);
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Stereo", {"Stereo", "Ping-pong"});
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"});
		configInput(TIME_INPUT, "Time CV, 1 V/oct up is half the time; with a clock, 10 V = the whole list of ratios");
		configInput(FEEDBACK_INPUT, "Feedback CV (10 V = full range)");
		configInput(TONE_INPUT, "Tone CV (5 V = full range each way)");
		configInput(MIX_INPUT, "Mix CV (10 V = full range)");
		configInput(CHARACTER_INPUT, "Character CV (10 V = full range)");
		configInput(TYPE_INPUT, "Type CV (10 V = all six types)");
		configInput(CLOCK_INPUT, "Clock: TIME then picks a ratio of its period");
		configInput(FREEZE_INPUT, "Freeze gate");
		configInput(L_INPUT, "Left");
		configInput(R_INPUT, "Right (the left when not connected)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configBypass(L_INPUT, L_OUTPUT);
		configBypass(R_INPUT, R_OUTPUT);
		prepare(APP->engine->getSampleRate());
	}

	void prepare(float fs) {
		delay.prepare(fs);
		clock.clear();
		tick = 0;
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		prepare(e.sampleRate);
	}

	void onReset(const ResetEvent &e) override {
		BidooModule::onReset(e);
		delay.reset();
		clock.clear();
		pulses = 0;
	}

	float cv(int input) {
		const float v = inputs[input].getVoltage();
		return std::isfinite(v) ? v : 0.f;
	}

	void control(double sampleRate) {
		bidoo::dsp::CharacterDelay::Settings s;
		const float timeKnob = params[TIME_PARAM].getValue();
		clocked = inputs[CLOCK_INPUT].isConnected() && clock.samplesPerPulse() > 0.0;
		double seconds;
		if (clocked) {
			shownRatio = ricochet::ratioOfKnob(clamp(timeKnob + cv(TIME_INPUT) * 0.1f, 0.f, 1.f));
			seconds = clock.samplesPerPulse() * ricochet::kRatios[shownRatio] / sampleRate;
		}
		else {
			shownRatio = -1;
			seconds = ricochet::secondsOfKnob(timeKnob) * std::pow(2.0, -(double)clamp(cv(TIME_INPUT), -10.f, 10.f));
			// a free time on a whole sample: Digital then reads with no interpolation at all
			seconds = std::round(seconds * sampleRate) / sampleRate;
		}
		seconds = std::min(seconds, bidoo::dsp::CharacterDelay::kMaxSeconds);
		const float spread = params[SPREAD_PARAM].getValue();
		const double longer = 1.0 + 0.5 * (double)std::fabs(spread);
		s.timeL = spread < 0.f ? seconds * longer : seconds;
		s.timeR = spread > 0.f ? seconds * longer : seconds;
		s.feedback = clamp(params[FEEDBACK_PARAM].getValue() + cv(FEEDBACK_INPUT) * 0.1f, 0.f, 1.f);
		s.tone = clamp(params[TONE_PARAM].getValue() + cv(TONE_INPUT) * 0.2f, -1.f, 1.f);
		s.mix = clamp(params[MIX_PARAM].getValue() + cv(MIX_INPUT) * 0.1f, 0.f, 1.f);
		s.character = clamp(params[CHARACTER_PARAM].getValue() + cv(CHARACTER_INPUT) * 0.1f, 0.f, 1.f);
		s.pingPong = params[MODE_PARAM].getValue() > 0.5f;
		frozen = params[FREEZE_PARAM].getValue() > 0.5f || freezeIn.isHigh();
		s.freeze = frozen;
		s.type = clamp((int)std::lround(params[TYPE_PARAM].getValue() + cv(TYPE_INPUT) * 0.5f), 0, 5);
		delay.setSettings(s);
		shownType = s.type;
		shownSeconds = seconds;
		// the screen shows about four repeats, half a second at least
		const double span = clamp(seconds * 4.0, 0.5, 16.0);
		peakLength = std::max(16, (int)(span * sampleRate / ricochet::kColumns));
	}

	void process(const ProcessArgs &args) override {
		const bool pulse = clockIn.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		clock.tick(pulse, args.sampleRate);
		freezeIn.process(inputs[FREEZE_INPUT].getVoltage(), 0.1f, 1.f);
		if (pulse) {
			// Reverse: the slices fall on the clock
			if (shownRatio >= 0 && ++pulses % ricochet::kRatioPulses[shownRatio] == 0)
				delay.restartSlices();
		}
		if (--tick <= 0) {
			tick = kControl;
			control(args.sampleRate);
		}
		const float l = inputs[L_INPUT].getVoltageSum() / kVolts;
		const float r = inputs[R_INPUT].isConnected() ? inputs[R_INPUT].getVoltageSum() / kVolts : l;
		float outL = 0.f, outR = 0.f;
		delay.process(l, r, outL, outR);
		outputs[L_OUTPUT].setVoltage(bidooSoftLimit(outL * kVolts));
		outputs[R_OUTPUT].setVoltage(bidooSoftLimit(outR * kVolts));

		peakNow[0] = std::max(peakNow[0], std::fabs(delay.getWet(0)));
		peakNow[1] = std::max(peakNow[1], std::fabs(delay.getWet(1)));
		if (++peakCount >= peakLength) {
			peakCount = 0;
			peaks[0][peakWrite] = peakNow[0];
			peaks[1][peakWrite] = peakNow[1];
			peakNow[0] = peakNow[1] = 0.f;
			peakWrite = (peakWrite + 1) % ricochet::kColumns;
		}
	}
};

// The screen: the type and the time in yellow, the echoes in pink, the left above the middle and the right below it.
struct RicochetScreen : TransparentWidget {
	RICOCHET *module = nullptr;

	void drawLayer(const DrawArgs &args, int layer) override {
		if (layer == 1)
			drawScreen(args.vg);
		TransparentWidget::drawLayer(args, layer);
	}

	void drawScreen(NVGcontext *vg) {
		const float w = box.size.x, h = box.size.y;
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, w, h, 3.f);
		nvgFillColor(vg, theme::kScreenBlack);
		nvgFill(vg);

		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (font)
			nvgFontFaceId(vg, font->handle);
		nvgScissor(vg, 0, 0, w, h);

		// the echoes
		const float top = 19.f, mid = (top + h) * 0.5f, reach = (h - top) * 0.5f - 2.f;
		const float colW = w / ricochet::kColumns;
		nvgBeginPath(vg);
		for (int c = 0; c < ricochet::kColumns; c++) {
			float a, b;
			if (module) {
				const int i = (module->peakWrite + c) % ricochet::kColumns;
				a = module->peaks[0][i];
				b = module->peaks[1][i];
			}
			else {
				// in the module browser: a train of echoes
				const float at = (float)c / ricochet::kColumns * 5.f;
				a = b = std::pow(0.6f, std::floor(at)) * std::exp(-(at - std::floor(at)) * 7.f);
			}
			const float x = c * colW;
			nvgRect(vg, x, mid - clamp(a, 0.f, 1.f) * reach, colW + 0.2f, clamp(a, 0.f, 1.f) * reach);
			nvgRect(vg, x, mid, colW + 0.2f, clamp(b, 0.f, 1.f) * reach);
		}
		nvgFillColor(vg, theme::kWavePink);
		nvgFill(vg);

		nvgFontSize(vg, 13.f);
		nvgFillColor(vg, theme::kScreenYellow);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		const int type = module ? module->shownType : 1;
		nvgText(vg, 5.f, 3.f, ricochet::kTypes[clamp(type, 0, 5)], nullptr);
		nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		std::string time = "291 ms";
		if (module) {
			time = module->shownRatio >= 0 ? std::string("x") + ricochet::kRatioNames[module->shownRatio] : ricochet::timeText(module->shownSeconds);
		}
		nvgText(vg, w - 5.f, 3.f, time.c_str(), nullptr);
		if (module && module->frozen) {
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgText(vg, w * 0.5f, 3.f, "FREEZE", nullptr);
		}
		nvgResetScissor(vg);
	}
};

struct RICOCHETWidget : BidooWidget {
	RICOCHETWidget(RICOCHET *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/RICOCHET.svg"));

		theme::addScrews(this);

		// Placed as tools/panels/ricochet.py draws them: the screen is the widget's own (margin 6, top 36), the knobs on
		// its KX/KY, the mode switch and freeze button at x 165 and 191 under their labels, the jacks on its JX columns
		// (six equal columns across the screen, off the theme grid's) and on the grid's two jack rows.
		RicochetScreen *screen = new RicochetScreen;
		screen->module = module;
		screen->box = theme::specificRect(6, 36, 198, 96);
		addChild(screen);

		static const float kx[4] = {30.f, 80.f, 130.f, 180.f};   // ricochet.py KX
		static const float ky[2] = {166.f, 228.f};               // ricochet.py KY
		auto knob = [&](int c, int r) { return theme::specific(kx[c], ky[r]); };
		addParam(theme::param<BidooBlueSnapKnob>(knob(0, 0), module, RICOCHET::TYPE_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(1, 0), module, RICOCHET::TIME_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(2, 0), module, RICOCHET::FEEDBACK_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(3, 0), module, RICOCHET::MIX_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(0, 1), module, RICOCHET::TONE_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(1, 1), module, RICOCHET::CHARACTER_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(2, 1), module, RICOCHET::SPREAD_PARAM));
		addParam(theme::param<CKSS>(theme::sameRow(knob(0, 1), 165.f), module, RICOCHET::MODE_PARAM));          // under PING / STEREO
		addParam(theme::param<BidooRoundButton>(theme::sameRow(knob(0, 1), 191.f), module, RICOCHET::FREEZE_PARAM));   // under FREEZE

		static const float jx[6] = {20.9f, 54.54f, 88.18f, 121.82f, 155.46f, 189.1f};   // ricochet.py JX
		auto jack = [&](int c, float k) { return theme::specific(jx[c], theme::row(k)); };
		static const int top[6] = {RICOCHET::TIME_INPUT, RICOCHET::FEEDBACK_INPUT, RICOCHET::TONE_INPUT, RICOCHET::MIX_INPUT, RICOCHET::CHARACTER_INPUT, RICOCHET::TYPE_INPUT};
		for (int i = 0; i < 6; i++)
			addInput(theme::input<PJ301MPort>(jack(i, 1), module, top[i]));
		addInput(theme::input<PJ301MPort>(jack(0, 0), module, RICOCHET::CLOCK_INPUT));
		addInput(theme::input<PJ301MPort>(jack(1, 0), module, RICOCHET::FREEZE_INPUT));
		addInput(theme::input<PJ301MPort>(jack(2, 0), module, RICOCHET::L_INPUT));
		addInput(theme::input<PJ301MPort>(jack(3, 0), module, RICOCHET::R_INPUT));
		addOutput(theme::output<PJ301MPort>(jack(4, 0), module, RICOCHET::L_OUTPUT));
		addOutput(theme::output<PJ301MPort>(jack(5, 0), module, RICOCHET::R_OUTPUT));
	}
};

Model *modelRICOCHET = createModel<RICOCHET, RICOCHETWidget>("RICOCHET");
