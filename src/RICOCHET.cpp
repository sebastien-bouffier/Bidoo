// rICOcHEt: a stereo delay with six characters: Digital, Tape, BBD, Lo-fi, Diffuse, Reverse. The sound is all in
// bidoo-dsp (delay/CharacterDelay.h, copied unchanged in src/dep/bidoo-dsp); this is the panel, the jacks and the clock.
#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"
#include <bidoo/dsp/delay/CharacterDelay.h>
#include <bidoo/dsp/time/PulseClock.h>

namespace ricochet {
static const char *const kTypes[] = {"DIGITAL", "TAPE", "BBD", "LO-FI", "DIFFUSE", "REVERSE"};
// With a clock, TIME picks a note value: a ratio of the beat (a quarter note), the beat being the clock's pulses per
// beat (context menu) times its period; names as notes, T for triplets and . for dotted ones
static const double kRatios[] = {1.0 / 8.0, 1.0 / 6.0, 1.0 / 4.0, 1.0 / 3.0, 3.0 / 8.0, 1.0 / 2.0, 2.0 / 3.0, 3.0 / 4.0, 1.0, 4.0 / 3.0, 3.0 / 2.0, 2.0, 3.0, 4.0, 6.0, 8.0, 12.0, 16.0};
static const char *const kRatioNames[] = {"1/32", "1/16T", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.", "1/4", "1/2T", "1/4.", "1/2", "1/2.", "1/1", "1/1.", "2/1", "3/1", "4/1"};
// the ratios of the beat as they were named before 2.7.9 (still typed into the TIME field)
static const char *const kOldRatioNames[] = {"1/8", "1/6", "1/4", "1/3", "3/8", "1/2", "2/3", "3/4", "1", "4/3", "3/2", "2", "3", "4", "6", "8", "12", "16"};
static const int kPpqns[] = {1, 2, 4, 8, 12, 16, 24, 32, 48, 96};
static constexpr int kNumPpqns = 10;
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
				return ricochet::kRatioNames[ricochet::ratioOfKnob(getValue())];
			return ricochet::timeText(ricochet::secondsOfKnob(getValue()));
		}
		// a ratio name when clocked ("3/4", "x3/4"); else a time, "ms" or "s", a bare number in the unit shown
		void setDisplayValueString(std::string s) override {
			RICOCHET *m = dynamic_cast<RICOCHET *>(module);
			std::string t;
			for (char c : s)
				if (c != ' ' && c != 'x' && c != 'X')
					t += (char)std::tolower((unsigned char)c);
			if (m && m->clocked) {
				if (t.size() > 5 && t.compare(t.size() - 5, 5, "clock") == 0)
					t.resize(t.size() - 5);
				for (int i = 0; i < ricochet::kNumRatios; i++) {
					std::string note = ricochet::kRatioNames[i];
					for (char &c : note)
						c = (char)std::tolower((unsigned char)c);
					if (t == note || t == ricochet::kOldRatioNames[i])
						setValue((i + 0.5f) / ricochet::kNumRatios);
				}
				return;
			}
			const double v = std::atof(t.c_str());
			if (!(v > 0.0))
				return;
			bool seconds;
			if (t.find("ms") != std::string::npos)
				seconds = false;
			else if (t.find('s') != std::string::npos)
				seconds = true;
			else
				seconds = ricochet::secondsOfKnob(getValue()) >= 1.0;
			const double sec = seconds ? v : v * 0.001;
			setValue(clamp((float)(std::log(sec / 0.002) / std::log(4000.0)), 0.f, 1.f));
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
	double clockedSamples = -1.0;   // the clocked time held, in whole samples (see control())
	int clockPpqn = 4;              // the clock's pulses per beat (a patch from before 2.7.9 loads as 1: its pulse was the beat)

	// the screen: a rolling peak and RMS of the echoes, and the input's peak, one column per slice of time
	float peaks[2][ricochet::kColumns] = {};
	float rms[2][ricochet::kColumns] = {};
	float inPeaks[2][ricochet::kColumns] = {};
	float peakNow[2] = {}, inNow[2] = {};
	double sqNow[2] = {};
	int peakWrite = 0, peakCount = 0, peakLength = 1000;
	double shownSpan = 1.2;   // the seconds the screen shows

	RICOCHET() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configSwitch(TYPE_PARAM, 0.f, 5.f, 0.f, "Type", {"Digital", "Tape", "BBD", "Lo-fi", "Diffuse", "Reverse"});
		configParam<TimeQuantity>(TIME_PARAM, 0.f, 1.f, 0.62f, "Time");
		configParam(FEEDBACK_PARAM, 0.f, 1.f, 0.45f, "Feedback", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 0.35f, "Mix", "%", 0.f, 100.f);
		configParam(TONE_PARAM, -1.f, 1.f, 0.f, "Tone (dark, centre flat, thin)", "%", 0.f, 100.f);
		configParam(CHARACTER_PARAM, 0.f, 1.f, 0.f, "Character", "%", 0.f, 100.f);
		configParam(SPREAD_PARAM, -1.f, 1.f, 0.f, "Spread (one side up to 1.5 x longer)", "%", 0.f, 100.f);
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Stereo", {"Stereo", "Ping-pong"});
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"});
		configInput(TIME_INPUT, "Time CV, 1 V/oct up is half the time; with a clock, 10 V = the whole list of ratios");
		configInput(FEEDBACK_INPUT, "Feedback CV (10 V = full range)");
		configInput(TONE_INPUT, "Tone CV (5 V = full range each way)");
		configInput(MIX_INPUT, "Mix CV (10 V = full range)");
		configInput(CHARACTER_INPUT, "Character CV (10 V = full range)");
		configInput(TYPE_INPUT, "Type CV (10 V = all six types)");
		configInput(CLOCK_INPUT, "Clock");
		updateClockName();
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

	void updateClockName() {
		inputInfos[CLOCK_INPUT]->name = rack::string::f("Clock (%d pulses per beat, right-click to change): TIME then picks a note value", clockPpqn);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "clockPpqn", json_integer(clockPpqn));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		// before 2.7.9 TIME was a ratio of the clock's pulse: those patches play as they did with 1 pulse per beat
		json_t *j = json_object_get(rootJ, "clockPpqn");
		clockPpqn = j ? clamp((int)json_integer_value(j), 1, 96) : 1;
		clockedSamples = -1.0;
		updateClockName();
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
			// held on a whole sample, moved only past 2 samples or 0.2 %: a clock whose pulses fall a sample apart
			// (120 BPM at 44.1 kHz: 5512.5 samples a 16th) moved the time at every pulse, and Digital crossfaded two
			// heads a sample apart over and over, a low pass; a real tempo change still moves it
			const double wanted = clock.samplesPerPulse() * clockPpqn * ricochet::kRatios[shownRatio];
			if (clockedSamples < 0.0 || std::fabs(wanted - clockedSamples) > std::max(2.0, 0.002 * clockedSamples))
				clockedSamples = std::round(wanted);
			seconds = clockedSamples / sampleRate;
		}
		else {
			shownRatio = -1;
			clockedSamples = -1.0;
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
		shownSpan = (double)peakLength * ricochet::kColumns / sampleRate;
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

		inNow[0] = std::max(inNow[0], std::fabs(l));
		inNow[1] = std::max(inNow[1], std::fabs(r));
		for (int c = 0; c < 2; c++) {
			const float w = delay.getWet(c);
			peakNow[c] = std::max(peakNow[c], std::fabs(w));
			sqNow[c] += (double)w * w;
		}
		if (++peakCount >= peakLength) {
			for (int c = 0; c < 2; c++) {
				peaks[c][peakWrite] = peakNow[c];
				rms[c][peakWrite] = (float)std::sqrt(sqNow[c] / peakCount);
				inPeaks[c][peakWrite] = inNow[c];
				peakNow[c] = inNow[c] = 0.f;
				sqNow[c] = 0.0;
			}
			peakCount = 0;
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

		// left channel up and right down, newest on the right, in dB over kRangeDb, so every repeat of a decaying
		// train shows, not only the first: the input in blue behind (its outline on top), the echoes in pink, peaks
		// with their RMS as a brighter core (src/WaveStyle.hpp)
		const float top = 19.f, mid = (top + h) * 0.5f, reach = (h - top) * 0.5f - 2.f;
		const float colW = w / ricochet::kColumns;
		static constexpr float kRangeDb = 48.f;
		auto level = [](float a) { return a > 0.f ? clamp(1.f + 20.f * std::log10(a) / kRangeDb, 0.f, 1.f) : 0.f; };
		// the dB lines: -12, -24 and -36 dB
		nvgBeginPath(vg);
		for (float db : {12.f, 24.f, 36.f}) {
			const float y = (1.f - db / kRangeDb) * reach;
			nvgRect(vg, 0, mid - y - 0.25f, w, 0.5f);
			nvgRect(vg, 0, mid + y - 0.25f, w, 0.5f);
		}
		nvgFillColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x14));
		nvgFill(vg);
		// a mark at every repeat of the delay time back from now: the echoes of a sound fall on them
		const double span = module ? module->shownSpan : 1.2, sec = module ? module->shownSeconds : 0.291;
		if (sec > 0.0 && span / sec < 64.0) {
			nvgBeginPath(vg);
			for (int k = 1; k * sec < span; k++) {
				const float x = w * (float)(1.0 - k * sec / span);
				for (float y = top; y < h; y += 4.f)
					nvgRect(vg, x - 0.25f, y, 0.5f, 2.f);
			}
			nvgFillColor(vg, nvgTransRGBA(theme::kScreenYellow, 0x50));
			nvgFill(vg);
		}
		// the input's level of column c (0..1 of reach), left and right; in the browser, one hit at the first mark
		auto inLevels = [&](int c, float &a, float &b) {
			if (module) {
				const int i = (module->peakWrite + c) % ricochet::kColumns;
				a = level(module->inPeaks[0][i]);
				b = level(module->inPeaks[1][i]);
			}
			else {
				const double rep = sec / span, first = 1.0 - std::floor(span / sec) * rep;
				const float at = (float)(((double)c / ricochet::kColumns - first) / rep);
				a = b = (at < 0.f || at >= 1.f) ? 0.f : level(std::exp(-at * 9.f));
			}
		};
		nvgBeginPath(vg);
		for (int c = 0; c < ricochet::kColumns; c++) {
			float a, b;
			inLevels(c, a, b);
			if (a > 0.f)
				nvgRect(vg, c * colW, mid - a * reach, colW + 0.2f, a * reach);
			if (b > 0.f)
				nvgRect(vg, c * colW, mid, colW + 0.2f, b * reach);
		}
		nvgFillColor(vg, nvgTransRGBA(wavestyle::kHead, 0x55));
		nvgFill(vg);
		for (int layer = 0; layer < 2; layer++) {
			nvgBeginPath(vg);
			for (int c = 0; c < ricochet::kColumns; c++) {
				float a, b;
				if (module) {
					const int i = (module->peakWrite + c) % ricochet::kColumns;
					a = layer == 0 ? module->peaks[0][i] : std::min(module->rms[0][i], module->peaks[0][i]);
					b = layer == 0 ? module->peaks[1][i] : std::min(module->rms[1][i], module->peaks[1][i]);
				}
				else {
					// in the module browser: a train of echoes, each on its mark
					const double rep = sec / span, first = 1.0 - std::floor(span / sec) * rep;
					const float at = (float)(((double)c / ricochet::kColumns - first) / rep);
					a = b = at < 1.f ? 0.f : std::pow(0.6f, std::floor(at)) * std::exp(-(at - std::floor(at)) * 7.f) * (layer == 0 ? 1.f : 0.5f);
				}
				const float x = c * colW, la = level(a) * reach, lb = level(b) * reach;
				if (la > 0.f)
					nvgRect(vg, x, mid - la, colW + 0.2f, la);
				if (lb > 0.f)
					nvgRect(vg, x, mid, colW + 0.2f, lb);
			}
			nvgFillColor(vg, layer == 0 ? wavestyle::kActivePeak : wavestyle::kActiveCore);
			nvgFill(vg);
		}
		// the input's outline over the echoes, so a sound and its repeats read side by side
		for (int side = 0; side < 2; side++) {
			// only where there is input: silence draws nothing across the echoes
			nvgBeginPath(vg);
			bool drawing = false;
			for (int c = 0; c < ricochet::kColumns; c++) {
				float a, b;
				inLevels(c, a, b);
				const float v = side == 0 ? a : b;
				if (v <= 0.f) {
					if (drawing)
						nvgLineTo(vg, c * colW, mid);
					drawing = false;
					continue;
				}
				const float y = side == 0 ? mid - v * reach : mid + v * reach;
				if (!drawing)
					nvgMoveTo(vg, c * colW, mid);
				nvgLineTo(vg, c * colW, y);
				nvgLineTo(vg, (c + 1) * colW, y);
				drawing = true;
			}
			nvgStrokeColor(vg, wavestyle::kHead);
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
		}

		nvgFontSize(vg, 13.f);
		nvgFillColor(vg, theme::kScreenYellow);
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
		const int type = module ? module->shownType : 0;
		nvgText(vg, 5.f, 3.f, ricochet::kTypes[clamp(type, 0, 5)], nullptr);
		nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
		std::string time = "291 ms";
		if (module) {
			time = module->shownRatio >= 0 ? std::string(ricochet::kRatioNames[module->shownRatio]) : ricochet::timeText(module->shownSeconds);
		}
		nvgText(vg, w - 5.f, 3.f, time.c_str(), nullptr);
		if (module && module->frozen) {
			nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_TOP);
			nvgText(vg, w * 0.5f, 3.f, "FREEZE", nullptr);
		}
		else {
			// the legend: the input in blue, the echoes in pink
			nvgFontSize(vg, 10.f);
			nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
			nvgFillColor(vg, wavestyle::kHead);
			nvgText(vg, w * 0.5f - 2.f, 5.f, "IN", nullptr);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, wavestyle::kActiveCore);
			nvgText(vg, w * 0.5f + 2.f, 5.f, "ECHOES", nullptr);
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

	void appendContextMenu(Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		RICOCHET *m = dynamic_cast<RICOCHET *>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		// the beat the note values count in: e.g. 4 for a 16th-note clock, 16 for tOCAnTe's 1/64, 24 for a MIDI clock
		menu->addChild(createIndexSubmenuItem("Clock pulses per beat",
			{"1 (quarter notes)", "2 (8th notes)", "4 (16th notes)", "8 (32nd notes)", "12", "16 (64th notes)", "24 (MIDI clock)", "32", "48", "96"},
			[=]() { for (int i = 0; i < ricochet::kNumPpqns; i++) if (ricochet::kPpqns[i] == m->clockPpqn) return (size_t)i; return (size_t)0; },
			[=](size_t i) { m->clockPpqn = ricochet::kPpqns[i]; m->clockedSamples = -1.0; m->updateClockName(); }));
	}
};

Model *modelRICOCHET = createModel<RICOCHET, RICOCHETWidget>("RICOCHET");
