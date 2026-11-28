// PRiSME: an additive voice, 64 harmonics per note, up to 16 notes. Four spectra drawn on the screen (A to D, morphed
// in a chain), an attack and a decay time per harmonic (with a loop mode), tilt, odd/even, stretch and a drawn formant
// filter. The sound is in PrismeEngine.hpp, the
// drawn pages in PrismeBars.hpp; this is the ports, the panel, the screen and its tools.
#include "plugin.hpp"
#include "Svf.hpp"   // bidooSoftLimit
#include "BidooComponents.hpp"
#include "PrismeEngine.hpp"
#include "PrismeBars.hpp"

using namespace std;

struct PRISME : BidooModule {
	enum ParamIds {
		MORPH_PARAM,
		SWEEP_PARAM,
		BRIGHT_PARAM,
		ODDEVEN_PARAM,
		STRETCH_PARAM,
		FORMANT_PARAM,
		ATTACK_PARAM,
		DECAY_PARAM,
		SUSTAIN_PARAM,
		RELEASE_PARAM,
		DAMP_PARAM,
		DEPTH_PARAM,
		TUNE_PARAM,
		SPREAD_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		VOCT_INPUT,
		GATE_INPUT,
		VEL_INPUT,
		MORPH_INPUT,
		BRIGHT_INPUT,
		STRETCH_INPUT,
		FORMANT_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		L_OUTPUT,
		R_OUTPUT,
		POLY_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	enum Tool { TOOL_BRUSH, TOOL_LINE };

	// Knob laws: value 0..1 to seconds or Hz, the same as their configParam displays
	static float sweepSeconds(float v) { return v <= 0.f ? 0.f : 0.005f * std::pow(4000.f, v); }
	static float attackSeconds(float v) { return 0.001f * std::pow(10000.f, v); }
	static float decaySeconds(float v) { return 0.005f * std::pow(4000.f, v); }
	static constexpr float kFormantRange = 3.f;   // FORMANT at +-1 moves the drawn curve +-3 octaves

	// SWEEP shows its time, or off at 0 (its law has no form configParam can display)
	struct SweepQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			const float s = sweepSeconds(getValue());
			if (s <= 0.f)
				return "off";
			return s < 1.f ? rack::string::f("%.0f ms", s * 1000.f) : rack::string::f("%.2f s", s);
		}
		std::string getUnit() override { return ""; }
	};

	prisme::Engine engine;
	prisme::Engine::Io io;
	prisme::Bars bars;           // written by the screen (UI thread), read by the engine at each control step
	// the screen's state, saved with the patch
	int page = 0;
	int tool = TOOL_BRUSH;
	float brushWidth = 1.f;      // in bars
	bool spreadPhases = false;
	// the menu's settings, saved with the patch
	int spectra = 4;             // how many spectra MORPH goes through: A-B, A-C or A-D
	bool loop = false;
	float velLevel = 1.f;        // velocity to level: 0 or 1
	float velMorph = 0.f;        // velocity to MORPH: 0, 0.25, 0.5, 1
	float formantEnv = 0.f;      // octaves the formant moves with the note's envelope: -2..2
	uint32_t randomSeed = 1;

	PRISME() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MORPH_PARAM, 0.f, 1.f, 0.f, "Morph", "%", 0.f, 100.f)->description =
			"Along the spectra: A at 0 %, then B, C and D (as many as right-click > Spectra in the morph says). With SWEEP, where each note starts";
		configParam<SweepQuantity>(SWEEP_PARAM, 0.f, 1.f, 0.f, "Sweep")->description =
			"Fully left: off. Otherwise each note travels from MORPH's position to the last spectrum in this time (5 ms to 20 s)";
		configParam(BRIGHT_PARAM, -1.f, 1.f, 0.f, "Brightness", " dB/oct", 0.f, 6.f)->description =
			"Tilts the spectrum: high harmonics up (right) or down (left), up to 6 dB per octave";
		configParam(ODDEVEN_PARAM, -1.f, 1.f, 0.f, "Odd / even", "%", 0.f, 100.f)->description =
			"Left: even harmonics fade out (odd only at -100 %, hollow). Right: odd harmonics fade out";
		configParam(STRETCH_PARAM, -1.f, 1.f, 0.f, "Stretch", "%", 0.f, 100.f)->description =
			"Moves the harmonics off their exact multiples: right spreads them (bells, struck metal), left squeezes them";
		configParam(FORMANT_PARAM, -1.f, 1.f, 0.f, "Formant shift", " oct", 0.f, kFormantRange)->description =
			"Moves the formant filter drawn on the F page up or down; it stays put as the note moves (DEPTH sets how much)";
		configParam(ATTACK_PARAM, 0.f, 1.f, 0.15f, "Attack", " ms", 10000.f, 1.f)->description =
			"Time for the harmonics to rise, times each one's bar on the attack page (At)";
		configParam(DECAY_PARAM, 0.f, 1.f, 0.6f, "Decay", " s", 4000.f, 0.005f)->description =
			"Time for the harmonics to fall 60 dB towards SUSTAIN, times each one's bar on the decay page (Dt); DAMP shortens high ones";
		configParam(SUSTAIN_PARAM, 0.f, 1.f, 0.7f, "Sustain", "%", 0.f, 100.f);
		configParam(RELEASE_PARAM, 0.f, 1.f, 0.45f, "Release", " s", 4000.f, 0.005f)->description =
			"Time to fall 60 dB after the gate, times each harmonic's bar on the decay page (Dt); DAMP shortens high ones";
		configParam(DAMP_PARAM, 0.f, 1.f, 0.3f, "Damp", "%", 0.f, 100.f)->description =
			"High harmonics decay faster: at 100 % harmonic k decays k times faster than the fundamental, as a plucked string";
		configParam(DEPTH_PARAM, 0.f, 1.f, 0.f, "Formant depth", "%", 0.f, 100.f);
		configParam(TUNE_PARAM, -2.f, 2.f, 0.f, "Tune", " semitones", 0.f, 12.f);
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.f, "Stereo spread", "%", 0.f, 100.f)->description =
			"Odd harmonics to the left, even ones to the right; the fundamental stays in the middle";
		configInput(VOCT_INPUT, "V/O (polyphonic, C4 at 0 V)");
		configInput(GATE_INPUT, "Gate (polyphonic; unplugged: the voices are held)");
		configInput(VEL_INPUT, "Velocity, 0..10 V: the level, and MORPH if the menu says (polyphonic; unplugged: full level)");
		configInput(MORPH_INPUT, "Morph CV (10 V spans the knob's range)");
		configInput(BRIGHT_INPUT, "Brightness CV (+-5 V spans the knob's range)");
		configInput(STRETCH_INPUT, "Stretch CV (+-5 V spans the knob's range)");
		configInput(FORMANT_INPUT, "Formant shift CV, 1 V/octave");
		configOutput(L_OUTPUT, "Left: every voice, over the square root of the channel count (mono when R is unplugged)");
		configOutput(R_OUTPUT, "Right (every voice)");
		configOutput(POLY_OUTPUT, "Each voice (polyphonic, mono per voice)");
		bars.setDefaults();
		engine.setSampleRate(APP->engine->getSampleRate());
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		engine.setSampleRate(e.sampleRate);
	}

	void onReset(const ResetEvent &e) override {
		BidooModule::onReset(e);
		bars.setDefaults();
		page = 0;
		tool = TOOL_BRUSH;
		brushWidth = 1.f;
		spreadPhases = false;
		spectra = 4;
		loop = false;
		velLevel = 1.f;
		velMorph = 0.f;
		formantEnv = 0.f;
		engine.reset();
	}

	void process(const ProcessArgs &args) override {
		auto port = [&](int id, const float *&v, int &n) {
			v = inputs[id].getVoltages();
			n = inputs[id].isConnected() ? inputs[id].getChannels() : 0;
		};
		port(VOCT_INPUT, io.voct, io.voctChannels);
		port(GATE_INPUT, io.gate, io.gateChannels);
		port(VEL_INPUT, io.vel, io.velChannels);
		port(MORPH_INPUT, io.morphCv, io.morphChannels);
		port(BRIGHT_INPUT, io.brightCv, io.brightChannels);
		port(STRETCH_INPUT, io.stretchCv, io.stretchChannels);
		port(FORMANT_INPUT, io.formantCv, io.formantChannels);
		// the knobs: read every sample, cheap; the laws with pow only at the engine's control rate
		if (engine.countdown <= 1) {
			io.sweep = sweepSeconds(params[SWEEP_PARAM].getValue());
			io.attack = attackSeconds(params[ATTACK_PARAM].getValue());
			io.decay = decaySeconds(params[DECAY_PARAM].getValue());
			io.release = decaySeconds(params[RELEASE_PARAM].getValue());
		}
		io.morph = params[MORPH_PARAM].getValue();
		io.bright = params[BRIGHT_PARAM].getValue();
		io.oddEven = params[ODDEVEN_PARAM].getValue();
		io.stretch = params[STRETCH_PARAM].getValue();
		io.depth = params[DEPTH_PARAM].getValue();
		io.formantShift = kFormantRange * params[FORMANT_PARAM].getValue();
		io.formantEnv = formantEnv;
		io.spectra = spectra;
		io.loop = loop;
		io.velLevel = velLevel;
		io.velMorph = velMorph;
		io.sustain = params[SUSTAIN_PARAM].getValue();
		io.damp = params[DAMP_PARAM].getValue();
		io.tune = params[TUNE_PARAM].getValue();
		io.spread = params[SPREAD_PARAM].getValue();
		io.spreadPhases = spreadPhases;
		io.bars = &bars.v[0][0];
		engine.process(io);

		outputs[POLY_OUTPUT].setChannels(io.channels);
		for (int c = 0; c < io.channels; c++)
			outputs[POLY_OUTPUT].setVoltage(bidooSoftLimit(io.poly[c]), c);
		// the sums (over the square root of the channel count, in the engine): kept under 10 V, linear up to 8 V
		if (outputs[R_OUTPUT].isConnected()) {
			outputs[L_OUTPUT].setVoltage(bidooSoftLimit(io.left));
			outputs[R_OUTPUT].setVoltage(bidooSoftLimit(io.right));
		}
		else
			outputs[L_OUTPUT].setVoltage(bidooSoftLimit(0.5f * (io.left + io.right)));
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_t *pagesJ = json_array();
		for (int p = 0; p < prisme::kBarPages; p++) {
			json_t *pageJ = json_array();
			for (int i = 0; i < prisme::kBars; i++)
				json_array_append_new(pageJ, json_real(bars.v[p][i]));
			json_array_append_new(pagesJ, pageJ);
		}
		json_object_set_new(rootJ, "bars", pagesJ);
		json_object_set_new(rootJ, "page", json_integer(page));
		json_object_set_new(rootJ, "tool", json_integer(tool));
		json_object_set_new(rootJ, "brushWidth", json_real(brushWidth));
		json_object_set_new(rootJ, "spreadPhases", json_boolean(spreadPhases));
		json_object_set_new(rootJ, "spectra", json_integer(spectra));
		json_object_set_new(rootJ, "loop", json_boolean(loop));
		json_object_set_new(rootJ, "velLevel", json_real(velLevel));
		json_object_set_new(rootJ, "velMorph", json_real(velMorph));
		json_object_set_new(rootJ, "formantEnv", json_real(formantEnv));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		// a damaged patch: values of the wrong type or out of range are left at their default, never trusted
		prisme::Bars b;
		b.setDefaults();
		json_t *pagesJ = json_object_get(rootJ, "bars");
		// the first PRiSME saved three pages (A, B, and the decay times): they go to A, B and Dt, and MORPH spans A-B
		const bool firstLayout = json_is_array(pagesJ) && json_array_size(pagesJ) == 3;
		static const int firstPages[3] = {prisme::PAGE_A, prisme::PAGE_B, prisme::PAGE_DECAY};
		if (json_is_array(pagesJ))
			for (int n = 0; n < prisme::kBarPages && n < (int)json_array_size(pagesJ); n++) {
				json_t *pageJ = json_array_get(pagesJ, n);
				if (!json_is_array(pageJ))
					continue;
				const int p = firstLayout ? firstPages[n] : n;
				for (int i = 0; i < prisme::kBars && i < (int)json_array_size(pageJ); i++) {
					json_t *vJ = json_array_get(pageJ, i);
					if (json_is_number(vJ))
						b.v[p][i] = prisme::cleanBar((float)json_number_value(vJ), p);
				}
			}
		bars = b;
		spectra = firstLayout ? 2 : 4;
		json_t *j = json_object_get(rootJ, "page");
		if (json_is_integer(j))
			page = clamp((int)json_integer_value(j), 0, prisme::kBarPages - 1);
		j = json_object_get(rootJ, "tool");
		if (json_is_integer(j))
			tool = clamp((int)json_integer_value(j), 0, (int)TOOL_LINE);
		j = json_object_get(rootJ, "brushWidth");
		if (json_is_number(j) && std::isfinite(json_number_value(j)))
			brushWidth = clamp((float)json_number_value(j), 1.f, 16.f);
		j = json_object_get(rootJ, "spreadPhases");
		if (json_is_boolean(j))
			spreadPhases = json_boolean_value(j);
		j = json_object_get(rootJ, "spectra");
		if (json_is_integer(j))
			spectra = clamp((int)json_integer_value(j), 2, prisme::kSpectra);
		j = json_object_get(rootJ, "loop");
		if (json_is_boolean(j))
			loop = json_boolean_value(j);
		auto number = [&](const char *key, float &v, float lo, float hi) {
			json_t *n = json_object_get(rootJ, key);
			if (json_is_number(n) && std::isfinite(json_number_value(n)))
				v = clamp((float)json_number_value(n), lo, hi);
		};
		number("velLevel", velLevel, 0.f, 1.f);
		number("velMorph", velMorph, 0.f, 1.f);
		number("formantEnv", formantEnv, -2.f, 2.f);
	}

	// ---- drawing (UI thread)
	prisme::Bars before;         // the pages when the stroke began, for the history
	bool stroking = false;
	bool lining = false;         // the line tool (or Shift with the brush): drawn on release from lineFrom
	bool erasing = false;
	float lastX = 0.f, lastY = 0.f, lineFromX = 0.f, lineFromY = 0.f;

	void beginStroke(float x, float y, bool erase, bool line) {
		before = bars;
		stroking = true;
		erasing = erase;
		lining = line && !erase;
		lastX = lineFromX = x;
		lastY = lineFromY = y;
		if (!lining)
			moveStroke(x, y);
	}

	void moveStroke(float x, float y) {
		if (!stroking)
			return;
		if (lining) {
			// the line from where it began, drawn again over the pages as they were
			std::memcpy(bars.v[page], before.v[page], sizeof(bars.v[page]));
			prisme::lineBars(bars.v[page], lineFromX, lineFromY, x, y);
		}
		else {
			const float ty = erasing ? prisme::eraseValue(page) : y;
			prisme::strokeBars(bars.v[page], lastX, erasing ? ty : lastY, x, ty, brushWidth);
		}
		lastX = x;
		lastY = y;
	}

	void endStroke() {
		if (!stroking)
			return;
		stroking = false;
		recordEdit(erasing ? "erase PRiSME bars" : (lining ? "draw a line on PRiSME" : "draw on PRiSME"));
	}

	// one change of the pages into Rack's history, from `before` to now
	void recordEdit(const char *name);

	void editPage(const char *name, std::function<void(float *)> f) {
		before = bars;
		f(bars.v[page]);
		recordEdit(name);
	}

	void applyShape(int shape) {
		randomSeed = randomSeed * 747796405u + 2891336453u;
		const uint32_t seed = randomSeed;
		const int pg = page;
		editPage("PRiSME shape", [=](float *p) { prisme::writeShape(p, shape, pg, seed); });
	}

	// the page shown copied to another page of the same kind (a spectrum to a spectrum, a time page to the other)
	void copyTo(int to) {
		if (to == page || to < 0 || to >= prisme::kBarPages)
			return;
		before = bars;
		std::memcpy(bars.v[to], bars.v[page], sizeof(bars.v[page]));
		recordEdit("copy a PRiSME page");
	}

	void clearPage() {
		const int pg = page;
		editPage("clear a PRiSME page", [=](float *p) {
			for (int i = 0; i < prisme::kBars; i++)
				p[i] = prisme::eraseValue(pg);
		});
	}
};

struct PrismeEditAction : history::ModuleAction {
	prisme::Bars before, after;
	void apply(const prisme::Bars &b) {
		if (PRISME *m = dynamic_cast<PRISME*>(APP->engine->getModule(moduleId)))
			m->bars = b;
	}
	void undo() override { apply(before); }
	void redo() override { apply(after); }
};

void PRISME::recordEdit(const char *name) {
	if (bars == before || !APP || !APP->history)
		return;
	PrismeEditAction *a = new PrismeEditAction;
	a->moduleId = id;
	a->name = name;
	a->before = before;
	a->after = bars;
	APP->history->push(a);
}

// Layout, the same in tools/panels/prisme.py
static constexpr int kCols = 6;
static constexpr float kStripH = 14.f;           // the tool strip under the screen
static constexpr float kStripGap = 3.f;          // between the screen and the strip
static constexpr int kKnobTopRow = 3, kKnobLowRow = 2;
static constexpr float kKnobNameAbove = 18.f;    // the knobs' names over their centre (KNOB_NAME_ABOVE): 3.2 px over the knob

// The screen: the bars of the page shown (spectra pink, times and the formant yellow), a neighbouring spectrum as an
// outline, and what the newest voice plays now as yellow ticks, so the envelopes are seen moving; on the formant page,
// the curve where that note hears it and its harmonics. Left draws, right erases, the wheel sets
// the brush's width, Shift draws a line.
struct PrismeScreen : OpaqueWidget {
	PRISME *module = nullptr;
	bool hovering = false;
	Vec mouse;
	static constexpr float kPad = 4.f, kTop = 12.f, kBottom = 4.f;   // the bars' area inside the screen; the top for the page's name

	float barW() const { return (box.size.x - 2.f * kPad) / prisme::kBars; }
	float plotH() const { return box.size.y - kTop - kBottom; }
	// the screen's point as a bar index (fractional, bar i centred on i) and a value
	void toBars(Vec p, float &x, float &y) const {
		x = clamp((p.x - kPad) / barW() - 0.5f, 0.f, (float)(prisme::kBars - 1));
		y = clamp(1.f - (p.y - kTop) / plotH(), 0.f, 1.f);
	}
	Vec mouseHere() {
		return APP->scene->getMousePos().minus(getAbsoluteOffset(Vec(0, 0))).div(getAbsoluteZoom());
	}

	void onButton(const ButtonEvent &e) override {
		if (module && e.action == GLFW_PRESS && (e.button == GLFW_MOUSE_BUTTON_LEFT || e.button == GLFW_MOUSE_BUTTON_RIGHT)) {
			const bool shift = (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT;
			float x, y;
			toBars(e.pos, x, y);
			module->beginStroke(x, y, e.button == GLFW_MOUSE_BUTTON_RIGHT, module->tool == PRISME::TOOL_LINE || shift);
			mouse = e.pos;
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
	void onDragMove(const DragMoveEvent &e) override {
		if (module && module->stroking) {
			mouse = mouseHere();
			float x, y;
			toBars(mouse, x, y);
			module->moveStroke(x, y);
			return;
		}
		OpaqueWidget::onDragMove(e);
	}
	void onDragEnd(const DragEndEvent &e) override {
		if (module && module->stroking) {
			module->endStroke();
			return;
		}
		OpaqueWidget::onDragEnd(e);
	}
	void onHover(const HoverEvent &e) override {
		OpaqueWidget::onHover(e);
		hovering = true;
		mouse = e.pos;
	}
	void onLeave(const LeaveEvent &e) override {
		hovering = false;
		OpaqueWidget::onLeave(e);
	}
	void onHoverScroll(const HoverScrollEvent &e) override {
		if (module && e.scrollDelta.y != 0.f) {
			static const float widths[5] = {1.f, 2.f, 4.f, 8.f, 16.f};
			int i = 0;
			while (i < 4 && widths[i] < module->brushWidth)
				i++;
			module->brushWidth = widths[clamp(i + (e.scrollDelta.y > 0.f ? 1 : -1), 0, 4)];
			e.consume(this);
			return;
		}
		OpaqueWidget::onHoverScroll(e);
	}

	void drawLayer(const DrawArgs &args, int layer) override {
		if (layer == 1)
			drawScreen(args.vg);
		OpaqueWidget::drawLayer(args, layer);
	}

	void drawScreen(NVGcontext *vg) {
		const float w = box.size.x, h = box.size.y;
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, w, h);
		nvgFillColor(vg, theme::kScreenBlack);
		nvgFill(vg);

		static prisme::Bars shownDefaults;   // the browser's preview: the default pages
		static bool init = false;
		if (!init) {
			shownDefaults.setDefaults();
			init = true;
		}
		const prisme::Bars &b = module ? module->bars : shownDefaults;
		const int page = module ? module->page : 0;
		const float bw = barW(), ph = plotH();
		auto bx = [&](int i) { return kPad + i * bw; };
		auto by = [&](float v) { return kTop + (1.f - v) * ph; };

		const bool spectrum = prisme::isSpectrum(page), timePage = prisme::isTimePage(page), formant = page == prisme::PAGE_FORMANT;
		// where a page's bars start from: the bottom on a spectrum and on the formant page (a mountain of gain, the
		// 0 dB line drawn across it), the middle line (the knob's time) on a time page
		const float base = timePage ? 0.5f : 0.f;

		// the guides: -20 and -40 dB on a spectrum; x1/4, x1 and x4 on a time page; 0 and -24 dB, and 100 Hz, 1 kHz and
		// 10 kHz, on the formant page
		nvgBeginPath(vg);
		std::vector<float> levels;
		if (spectrum)
			levels = {1.f / 3.f, 2.f / 3.f};
		else if (timePage)
			levels = {1.f / 6.f, 0.5f, 5.f / 6.f};
		else
			levels = {prisme::formantDbToBar(-24.f), prisme::kFormantUnity};
		for (float v : levels) {
			nvgMoveTo(vg, kPad, by(v));
			nvgLineTo(vg, w - kPad, by(v));
		}
		auto hzX = [&](float hz) {
			return kPad + (std::log2(hz / prisme::kFormantLow) / prisme::kFormantOctaves * (prisme::kBars - 1) + 0.5f) * bw;
		};
		if (formant)
			for (float hz : {100.f, 1000.f, 10000.f}) {
				nvgMoveTo(vg, hzX(hz), kTop);
				nvgLineTo(vg, hzX(hz), kTop + ph);
			}
		nvgStrokeColor(vg, theme::kKeyOff);
		nvgStrokeWidth(vg, 0.6f);
		nvgStroke(vg);

		// the bars
		nvgBeginPath(vg);
		for (int i = 0; i < prisme::kBars; i++) {
			const float v = b.v[page][i];
			if (!timePage) {
				if (v > 0.f)
					nvgRect(vg, bx(i) + 0.5f, by(v), bw - 1.f, v * ph);
			}
			else {
				const float y0 = by(base), y1 = by(v);
				nvgRect(vg, bx(i) + 0.5f, std::min(y0, y1), bw - 1.f, std::max(std::fabs(y1 - y0), 0.8f));
			}
		}
		nvgFillColor(vg, spectrum ? theme::kWavePink : theme::kScreenYellow);
		nvgFill(vg);
		if (formant) {
			// the 0 dB line over the bars, and the frequencies of the guides
			nvgBeginPath(vg);
			nvgMoveTo(vg, kPad, by(prisme::kFormantUnity));
			nvgLineTo(vg, w - kPad, by(prisme::kFormantUnity));
			nvgStrokeColor(vg, nvgRGBA(0, 0, 0, 160));
			nvgStrokeWidth(vg, 0.8f);
			nvgStroke(vg);
			std::shared_ptr<window::Font> small = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
			if (small && small->handle >= 0) {
				nvgFontFaceId(vg, small->handle);
				nvgFontSize(vg, 7.f);
				nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
				nvgFillColor(vg, nvgRGB(150, 150, 150));
				const char *labels[3] = {"100 Hz", "1 kHz", "10 kHz"};
				const float hzs[3] = {100.f, 1000.f, 10000.f};
				for (int k = 0; k < 3; k++) {
					// the last one left of its line, inside the screen
					nvgTextAlign(vg, (k == 2 ? NVG_ALIGN_RIGHT : NVG_ALIGN_LEFT) | NVG_ALIGN_TOP);
					nvgText(vg, hzX(hzs[k]) + (k == 2 ? -2.f : 2.f), kTop, labels[k], NULL);
				}
			}
		}

		const prisme::Engine::Voice *voice = nullptr;
		if (module) {
			const prisme::Engine::Voice &v = module->engine.voices[clamp(module->engine.shownVoice, 0, prisme::kMaxVoices - 1)];
			if (v.stage != prisme::Engine::IDLE)
				voice = &v;
		}
		if (spectrum) {
			// the neighbour along the chain as an outline (B for A, the one before for the others)
			const int other = page == 0 ? 1 : page - 1;
			nvgBeginPath(vg);
			for (int i = 0; i < prisme::kBars; i++) {
				const float y = by(b.v[other][i]);
				if (i == 0)
					nvgMoveTo(vg, bx(i), y);
				else
					nvgLineTo(vg, bx(i), y);
				nvgLineTo(vg, bx(i + 1), y);
			}
			nvgStrokeColor(vg, nvgRGBA(235, 235, 235, 190));
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);

			// what the newest voice plays, in the bars' dB scale (a harmonic alone at full level is a full bar)
			if (voice) {
				nvgBeginPath(vg);
				for (int i = 0; i < prisme::kBars; i++) {
					const float a = voice->shown[i / 4][i % 4] / prisme::Engine::kVolts;
					if (!(a > 1e-3f))
						continue;
					const float y = by(clamp(1.f + 20.f * std::log10(a) / prisme::kBarDb, 0.f, 1.f));
					nvgMoveTo(vg, bx(i) + 0.5f, y);
					nvgLineTo(vg, bx(i + 1) - 0.5f, y);
				}
				nvgStrokeColor(vg, theme::kScreenYellow);
				nvgStrokeWidth(vg, 1.4f);
				nvgStroke(vg);
			}
		}
		if (formant && voice) {
			// the curve where the newest note hears it (moved by FORMANT, its CV and the envelope), and that note's
			// harmonics as pink ticks at their frequencies
			const float shiftBars = voice->shift / prisme::kFormantOctaves * (prisme::kBars - 1);
			nvgBeginPath(vg);
			bool first = true;
			for (int i = 0; i < prisme::kBars; i++) {
				const float x = kPad + (i + shiftBars + 0.5f) * bw;
				if (x < kPad || x > w - kPad)
					continue;
				if (first)
					nvgMoveTo(vg, x, by(b.v[page][i]));
				else
					nvgLineTo(vg, x, by(b.v[page][i]));
				first = false;
			}
			nvgStrokeColor(vg, nvgRGBA(235, 235, 235, 190));
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
			const float sr = module->engine.sampleRate;
			nvgBeginPath(vg);
			for (int i = 0; i < prisme::kBars; i++) {
				if (!(voice->shown[i / 4][i % 4] > 1e-4f))
					continue;
				const float hz = voice->inc[i / 4][i % 4] * sr;
				if (hz < prisme::kFormantLow)
					continue;
				const float x = hzX(hz);
				if (x > w - kPad)
					break;
				nvgMoveTo(vg, x, kTop + ph);
				nvgLineTo(vg, x, kTop + ph - 6.f);
			}
			nvgStrokeColor(vg, theme::kWavePink);
			nvgStrokeWidth(vg, 1.2f);
			nvgStroke(vg);
		}

		// the line being drawn
		if (module && module->stroking && module->lining) {
			nvgBeginPath(vg);
			nvgMoveTo(vg, kPad + (module->lineFromX + 0.5f) * bw, by(module->lineFromY));
			nvgLineTo(vg, kPad + (module->lastX + 0.5f) * bw, by(module->lastY));
			nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 200));
			nvgStrokeWidth(vg, 1.f);
			nvgStroke(vg);
		}
		// the brush under the mouse: its width in bars
		if (module && (hovering || module->stroking) && !(module->stroking && module->lining)) {
			const float half = 0.5f * module->brushWidth * bw;
			nvgBeginPath(vg);
			nvgRect(vg, clamp(mouse.x, kPad, w - kPad) - half, kTop, 2.f * half, ph);
			nvgStrokeColor(vg, nvgRGBA(255, 255, 255, 90));
			nvgStrokeWidth(vg, 0.8f);
			nvgStroke(vg);
		}

		// the page's name, top left
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		if (font && font->handle >= 0) {
			static const char *names[prisme::kBarPages] = {"A", "B", "C", "D", "At  attack times", "Dt  decay times",
				"F  formant filter"};
			nvgFontFaceId(vg, font->handle);
			nvgFontSize(vg, 8.f);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, theme::kScreenYellow);
			nvgText(vg, kPad, 2.f, names[page], NULL);
			if (spectrum && module && page >= module->spectra) {
				nvgFillColor(vg, nvgRGB(150, 150, 150));
				nvgText(vg, kPad + 10.f, 2.f, "(not in the morph: right-click > Spectra in the morph)", NULL);
				nvgFillColor(vg, theme::kScreenYellow);
			}
			if (module && module->engine.shownChannels > 0) {
				nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
				nvgText(vg, w - kPad, 2.f, rack::string::f("%d voice%s", module->engine.shownChannels,
					module->engine.shownChannels > 1 ? "s" : "").c_str(), NULL);
			}
		}
	}
};

// The pages a page can be copied to: another spectrum, the other time page; none for the formant
static std::vector<int> copyTargets(int page) {
	std::vector<int> out;
	if (prisme::isSpectrum(page)) {
		for (int p = 0; p < prisme::kSpectra; p++)
			if (p != page)
				out.push_back(p);
	}
	else if (prisme::isTimePage(page))
		out.push_back(page == prisme::PAGE_ATTACK ? prisme::PAGE_DECAY : prisme::PAGE_ATTACK);
	return out;
}

static const char *pageLongName(int page) {
	static const char *names[prisme::kBarPages] = {"A: first spectrum", "B: second spectrum", "C: third spectrum",
		"D: fourth spectrum", "At: attack times", "Dt: decay times", "F: formant filter"};
	return names[clamp(page, 0, prisme::kBarPages - 1)];
}

static void appendShapes(Menu *menu, PRISME *module) {
	for (int s = 0; s < prisme::NUM_SHAPES; s++)
		menu->addChild(createMenuItem(prisme::shapeName(s, module->page), "", [=]() { module->applyShape(s); }));
}

static void appendCopies(Menu *menu, PRISME *module) {
	for (int to : copyTargets(module->page))
		menu->addChild(createMenuItem(std::string("To ") + pageLongName(to), "", [=]() { module->copyTo(to); }));
}

// Under the screen, always there: the seven pages (the four spectra, the attack and decay times, the formant), the
// brush and the line, shapes (waveforms, or vowels on the formant page), copy and clear. Every change goes into Rack's
// history. Black in every theme, as the screen.
struct PrismeStrip : OpaqueWidget {
	PRISME *module = nullptr;
	enum Cell { C_A, C_B, C_C, C_D, C_ATTACK, C_DECAY, C_FORMANT, C_BRUSH, C_LINE, C_SHAPE, C_COPY, C_CLEAR, NUM_CELLS };

	float cell() const { return box.size.x / NUM_CELLS; }

	void onButton(const ButtonEvent &e) override {
		if (!module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) {
			OpaqueWidget::onButton(e);
			return;
		}
		const int i = clamp((int)(e.pos.x / cell()), 0, NUM_CELLS - 1);
		if (i <= C_FORMANT)
			module->page = i - C_A;
		else if (i == C_BRUSH)
			module->tool = PRISME::TOOL_BRUSH;
		else if (i == C_LINE)
			module->tool = PRISME::TOOL_LINE;
		else if (i == C_SHAPE) {
			Menu *menu = createMenu();
			menu->addChild(createMenuLabel(std::string("Shape on ") + pageLongName(module->page)));
			appendShapes(menu, module);
		}
		else if (i == C_COPY) {
			if (!copyTargets(module->page).empty()) {
				Menu *menu = createMenu();
				menu->addChild(createMenuLabel(std::string("Copy ") + pageLongName(module->page)));
				appendCopies(menu, module);
			}
		}
		else if (i == C_CLEAR)
			module->clearPage();
		e.consume(this);
	}

	void drawLayer(const DrawArgs &args, int layer) override {
		if (layer == 1)
			drawStrip(args.vg);
		OpaqueWidget::drawLayer(args, layer);
	}

	void drawStrip(NVGcontext *vg) {
		nvgBeginPath(vg);
		nvgRect(vg, 0, 0, box.size.x, box.size.y);
		nvgFillColor(vg, theme::kScreenBlack);
		nvgFill(vg);
		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/DejaVuSans.ttf"));
		const float c = cell(), hgt = box.size.y;
		const int page = module ? module->page : 0, tool = module ? module->tool : 0;
		const int spectra = module ? module->spectra : prisme::kSpectra;
		const NVGcolor dim = nvgRGB(150, 150, 150), ink = theme::kScreenYellow;
		for (int i = 0; i < NUM_CELLS; i++) {
			const float x = i * c, cx = x + 0.5f * c, cy = 0.5f * hgt;
			bool selected = false;
			if (i <= C_FORMANT) {
				selected = page == i - C_A;
				static const char *names[prisme::kBarPages] = {"A", "B", "C", "D", "At", "Dt", "F"};
				// a spectrum outside the morph is darker
				const bool unused = i <= C_D && i - C_A >= spectra;
				if (font && font->handle >= 0) {
					nvgFontFaceId(vg, font->handle);
					nvgFontSize(vg, 9.f);
					nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
					nvgFillColor(vg, selected ? ink : (unused ? theme::kKeyOff : dim));
					nvgText(vg, cx, cy + 0.5f, names[i - C_A], NULL);
				}
				// a thin line between the spectra and the other pages
				if (i == C_ATTACK) {
					nvgBeginPath(vg);
					nvgMoveTo(vg, x, 3.f);
					nvgLineTo(vg, x, hgt - 3.f);
					nvgStrokeColor(vg, theme::kKeyOff);
					nvgStrokeWidth(vg, 0.8f);
					nvgStroke(vg);
				}
			}
			else if (i == C_BRUSH) {
				// a pencil
				selected = tool == PRISME::TOOL_BRUSH;
				const float s = hgt - 5.f, l = cx - 0.5f * s, t = cy - 0.5f * s;
				nvgBeginPath(vg);
				nvgMoveTo(vg, l + s * 0.15f, t + s * 0.85f);
				nvgLineTo(vg, l + s * 0.3f, t + s * 0.55f);
				nvgLineTo(vg, l + s * 0.75f, t + s * 0.1f);
				nvgLineTo(vg, l + s * 0.9f, t + s * 0.25f);
				nvgLineTo(vg, l + s * 0.45f, t + s * 0.7f);
				nvgClosePath(vg);
				nvgFillColor(vg, selected ? ink : dim);
				nvgFill(vg);
			}
			else if (i == C_LINE) {
				selected = tool == PRISME::TOOL_LINE;
				const float s = hgt - 6.f;
				nvgBeginPath(vg);
				nvgMoveTo(vg, cx - 0.5f * s, cy + 0.5f * s);
				nvgLineTo(vg, cx + 0.5f * s, cy - 0.5f * s);
				nvgStrokeColor(vg, selected ? ink : dim);
				nvgStrokeWidth(vg, 1.3f);
				nvgStroke(vg);
			}
			else if (i == C_SHAPE) {
				// a falling row of tiny bars: the shapes' menu
				const float bw = (c - 8.f) / 6.f, base = hgt - 2.5f, top = 2.5f;
				nvgBeginPath(vg);
				for (int k = 0; k < 6; k++) {
					const float v = prisme::powerBar(k + 1, 1.f);
					nvgRect(vg, x + 4.f + k * bw, base - v * (base - top), std::max(bw - 0.6f, 0.6f), v * (base - top));
				}
				nvgFillColor(vg, theme::kWavePink);
				nvgFill(vg);
			}
			else if (i == C_COPY) {
				// two overlapping squares, dimmed where the page has nowhere to go
				const float s = hgt - 7.f;
				nvgBeginPath(vg);
				nvgRect(vg, cx - 0.5f * s - 1.5f, cy - 0.5f * s - 1.5f, s, s);
				nvgRect(vg, cx - 0.5f * s + 1.5f, cy - 0.5f * s + 1.5f, s, s);
				nvgStrokeColor(vg, copyTargets(page).empty() ? theme::kKeyOff : ink);
				nvgStrokeWidth(vg, 1.f);
				nvgStroke(vg);
			}
			else if (i == C_CLEAR) {
				const float s = hgt - 6.f;
				nvgBeginPath(vg);
				nvgMoveTo(vg, cx - 0.5f * s, cy - 0.5f * s);
				nvgLineTo(vg, cx + 0.5f * s, cy + 0.5f * s);
				nvgMoveTo(vg, cx + 0.5f * s, cy - 0.5f * s);
				nvgLineTo(vg, cx - 0.5f * s, cy + 0.5f * s);
				nvgStrokeColor(vg, ink);
				nvgStrokeWidth(vg, 1.3f);
				nvgStroke(vg);
			}
			if (selected) {
				nvgBeginPath(vg);
				nvgRect(vg, x + 1.5f, 1.f, c - 3.f, hgt - 2.f);
				nvgStrokeColor(vg, ink);
				nvgStrokeWidth(vg, 0.8f);
				nvgStroke(vg);
			}
		}
	}
};

struct PRISMEWidget : BidooWidget {
	PRISMEWidget(PRISME *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/PRISME.svg"));
		theme::addScrews(this);

		// On the grid, six columns (tools/panels/prisme.py): the screen and its strip down to above row 3's knob names,
		// two rows of knobs, then the jacks. Every control of a column shares its jack's x (jackAt), as dOSE.
		const float w = box.size.x;
		auto at = [&](float i, float k) { return theme::jackAt(i, kCols, k, w); };
		const float stripTop = theme::row(kKnobTopRow) - kKnobNameAbove - theme::kLabelRoom - kStripH;

		PrismeScreen *screen = new PrismeScreen;
		screen->module = module;
		screen->box = theme::specificRect(theme::kSideMargin, theme::kScreenTop, w - 2.f * theme::kSideMargin,
			stripTop - kStripGap - theme::kScreenTop);
		addChild(screen);
		PrismeStrip *strip = new PrismeStrip;
		strip->module = module;
		strip->box = theme::specificRect(theme::kSideMargin, stripTop, w - 2.f * theme::kSideMargin, kStripH);
		addChild(strip);

		// ODD/EVEN, the widest name, between the two narrowest; BRIGHT and STRETCH over their CV jacks
		const int top[kCols] = {PRISME::MORPH_PARAM, PRISME::SWEEP_PARAM, PRISME::ODDEVEN_PARAM, PRISME::BRIGHT_PARAM,
			PRISME::STRETCH_PARAM, PRISME::FORMANT_PARAM};
		const int low[kCols] = {PRISME::ATTACK_PARAM, PRISME::DECAY_PARAM, PRISME::SUSTAIN_PARAM, PRISME::RELEASE_PARAM,
			PRISME::DAMP_PARAM, PRISME::DEPTH_PARAM};
		const int ins[kCols] = {PRISME::VOCT_INPUT, PRISME::GATE_INPUT, PRISME::VEL_INPUT, PRISME::BRIGHT_INPUT,
			PRISME::STRETCH_INPUT, PRISME::MORPH_INPUT};
		for (int i = 0; i < kCols; i++) {
			addParam(theme::param<BidooBlueKnob>(at(i, kKnobTopRow), module, top[i]));
			addParam(theme::param<BidooBlueKnob>(at(i, kKnobLowRow), module, low[i]));
			addInput(theme::input<PJ301MPort>(at(i, 1), module, ins[i]));
		}
		addInput(theme::input<PJ301MPort>(at(0, 0), module, PRISME::FORMANT_INPUT));
		addParam(theme::param<BidooSmallBlueKnob>(at(1, 0), module, PRISME::TUNE_PARAM));
		addParam(theme::param<BidooSmallBlueKnob>(at(2, 0), module, PRISME::SPREAD_PARAM));
		addOutput(theme::output<PJ301MPort>(at(3, 0), module, PRISME::L_OUTPUT));
		addOutput(theme::output<PJ301MPort>(at(4, 0), module, PRISME::R_OUTPUT));
		addOutput(theme::output<PJ301MPort>(at(5, 0), module, PRISME::POLY_OUTPUT));
	}

	void appendContextMenu(ui::Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		PRISME *module = dynamic_cast<PRISME*>(this->module);
		if (!module)
			return;
		menu->addChild(new MenuSeparator());
		menu->addChild(createIndexSubmenuItem("Spectra in the morph", {"A to B", "A to C", "A to D"},
			[=]() { return (size_t)(module->spectra - 2); },
			[=](size_t i) { module->spectra = (int)i + 2; }));
		menu->addChild(createBoolPtrMenuItem("Loop the harmonics' envelopes", "", &module->loop));
		menu->addChild(createBoolMenuItem("Velocity sets the level", "",
			[=]() { return module->velLevel > 0.5f; },
			[=](bool on) { module->velLevel = on ? 1.f : 0.f; }));
		static const float morphs[] = {0.f, 0.25f, 0.5f, 1.f};
		menu->addChild(createIndexSubmenuItem("Velocity moves MORPH", {"No", "A quarter of the way", "Half the way", "All the way"},
			[=]() {
				for (size_t i = 0; i < 4; i++)
					if (module->velMorph == morphs[i])
						return i;
				return (size_t)0;
			},
			[=](size_t i) { module->velMorph = morphs[i]; }));
		static const float envs[] = {-2.f, -1.f, 0.f, 1.f, 2.f};
		menu->addChild(createIndexSubmenuItem("Formant follows the envelope",
			{"Down 2 octaves", "Down 1 octave", "No", "Up 1 octave", "Up 2 octaves"},
			[=]() {
				for (size_t i = 0; i < 5; i++)
					if (module->formantEnv == envs[i])
						return i;
				return (size_t)2;
			},
			[=](size_t i) { module->formantEnv = envs[i]; }));
		menu->addChild(createIndexSubmenuItem("Phases at note-on",
			{"Aligned: the classic waveforms", "Spread: the same sound, lower peaks"},
			[=]() { return (size_t)module->spreadPhases; },
			[=](size_t i) { module->spreadPhases = i == 1; }));

		menu->addChild(new MenuSeparator());
		menu->addChild(createSubmenuItem("Page", pageLongName(module->page), [=](Menu *sub) {
			for (int p = 0; p < prisme::kBarPages; p++)
				sub->addChild(createCheckMenuItem(pageLongName(p), "", [=]() { return module->page == p; },
					[=]() { module->page = p; }));
		}));
		menu->addChild(createIndexSubmenuItem("Tool", {"Brush", "Line"},
			[=]() { return (size_t)module->tool; },
			[=](size_t i) { module->tool = (int)i; }));
		static const float widths[] = {1.f, 2.f, 4.f, 8.f, 16.f};
		menu->addChild(createIndexSubmenuItem("Brush width", {"1 bar", "2 bars", "4 bars", "8 bars", "16 bars"},
			[=]() {
				for (size_t i = 0; i < 5; i++)
					if (module->brushWidth == widths[i])
						return i;
				return (size_t)0;
			},
			[=](size_t i) { module->brushWidth = widths[i]; }));
		menu->addChild(createSubmenuItem("Shape on this page", "", [=](Menu *sub) { appendShapes(sub, module); }));
		menu->addChild(createSubmenuItem("Copy this page", "", [=](Menu *sub) { appendCopies(sub, module); },
			copyTargets(module->page).empty()));
		menu->addChild(createMenuItem("Clear this page", "", [=]() { module->clearPage(); }));
		menu->addChild(createMenuLabel("Left draws, right erases, Shift draws a line, the wheel sets the width"));
		menu->addChild(createMenuLabel("Undo, redo: " RACK_MOD_CTRL_NAME "+Z, " RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+Z"));
	}
};

Model *modelPRISME = createModel<PRISME, PRISMEWidget>("PRISME");
