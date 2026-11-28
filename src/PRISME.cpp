// PRiSME: an additive voice, 64 harmonics per note, up to 16 notes. Two spectra drawn on the screen (A and B, morphed),
// a decay time per harmonic (the T page), tilt, odd/even, stretch and a formant. The sound is in PrismeEngine.hpp, the
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
	static float formantHz(float v) { return 100.f * std::pow(80.f, v); }

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
	uint32_t randomSeed = 1;

	PRISME() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(MORPH_PARAM, 0.f, 1.f, 0.f, "Morph", "%", 0.f, 100.f)->description =
			"From spectrum A (0 %) to spectrum B (100 %). With SWEEP, where each note glides to from A";
		configParam<SweepQuantity>(SWEEP_PARAM, 0.f, 1.f, 0.f, "Sweep")->description =
			"Fully left: off. Otherwise each note starts on A and glides to MORPH's position in this time (5 ms to 20 s)";
		configParam(BRIGHT_PARAM, -1.f, 1.f, 0.f, "Brightness", " dB/oct", 0.f, 6.f)->description =
			"Tilts the spectrum: high harmonics up (right) or down (left), up to 6 dB per octave";
		configParam(ODDEVEN_PARAM, -1.f, 1.f, 0.f, "Odd / even", "%", 0.f, 100.f)->description =
			"Left: even harmonics fade out (odd only at -100 %, hollow). Right: odd harmonics fade out";
		configParam(STRETCH_PARAM, -1.f, 1.f, 0.f, "Stretch", "%", 0.f, 100.f)->description =
			"Moves the harmonics off their exact multiples: right spreads them (bells, struck metal), left squeezes them";
		configParam(FORMANT_PARAM, 0.f, 1.f, 0.5f, "Formant", " Hz", 80.f, 100.f)->description =
			"Centre of the formant: a bump at a fixed frequency that stays put as the note moves (DEPTH sets how much)";
		configParam(ATTACK_PARAM, 0.f, 1.f, 0.15f, "Attack", " ms", 10000.f, 1.f);
		configParam(DECAY_PARAM, 0.f, 1.f, 0.6f, "Decay", " s", 4000.f, 0.005f)->description =
			"Time for the harmonics to fall 60 dB towards SUSTAIN, times each one's bar on the T page; DAMP shortens high ones";
		configParam(SUSTAIN_PARAM, 0.f, 1.f, 0.7f, "Sustain", "%", 0.f, 100.f);
		configParam(RELEASE_PARAM, 0.f, 1.f, 0.45f, "Release", " s", 4000.f, 0.005f)->description =
			"Time to fall 60 dB after the gate, times each harmonic's bar on the T page; DAMP shortens high ones";
		configParam(DAMP_PARAM, 0.f, 1.f, 0.3f, "Damp", "%", 0.f, 100.f)->description =
			"High harmonics decay faster: at 100 % harmonic k decays k times faster than the fundamental, as a plucked string";
		configParam(DEPTH_PARAM, 0.f, 1.f, 0.f, "Formant depth", "%", 0.f, 100.f);
		configParam(TUNE_PARAM, -2.f, 2.f, 0.f, "Tune", " semitones", 0.f, 12.f);
		configParam(SPREAD_PARAM, 0.f, 1.f, 0.f, "Stereo spread", "%", 0.f, 100.f)->description =
			"Odd harmonics to the left, even ones to the right; the fundamental stays in the middle";
		configInput(VOCT_INPUT, "V/Oct (polyphonic, C4 at 0 V)");
		configInput(GATE_INPUT, "Gate (polyphonic; unplugged: the voices are held)");
		configInput(VEL_INPUT, "Velocity, 0..10 V (polyphonic; unplugged: full level)");
		configInput(MORPH_INPUT, "Morph CV (10 V spans the knob's range)");
		configInput(BRIGHT_INPUT, "Brightness CV (+-5 V spans the knob's range)");
		configInput(STRETCH_INPUT, "Stretch CV (+-5 V spans the knob's range)");
		configInput(FORMANT_INPUT, "Formant CV, 1 V/octave");
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
			io.formantHz = formantHz(params[FORMANT_PARAM].getValue());
			io.attack = attackSeconds(params[ATTACK_PARAM].getValue());
			io.decay = decaySeconds(params[DECAY_PARAM].getValue());
			io.release = decaySeconds(params[RELEASE_PARAM].getValue());
		}
		io.morph = params[MORPH_PARAM].getValue();
		io.bright = params[BRIGHT_PARAM].getValue();
		io.oddEven = params[ODDEVEN_PARAM].getValue();
		io.stretch = params[STRETCH_PARAM].getValue();
		io.depth = params[DEPTH_PARAM].getValue();
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
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		// a damaged patch: values of the wrong type or out of range are left at their default, never trusted
		prisme::Bars b;
		b.setDefaults();
		json_t *pagesJ = json_object_get(rootJ, "bars");
		if (json_is_array(pagesJ))
			for (int p = 0; p < prisme::kBarPages && p < (int)json_array_size(pagesJ); p++) {
				json_t *pageJ = json_array_get(pagesJ, p);
				if (!json_is_array(pageJ))
					continue;
				for (int i = 0; i < prisme::kBars && i < (int)json_array_size(pageJ); i++) {
					json_t *vJ = json_array_get(pageJ, i);
					if (json_is_number(vJ))
						b.v[p][i] = prisme::cleanBar((float)json_number_value(vJ), p);
				}
			}
		bars = b;
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
		editPage("PRiSME shape", [=](float *p) { prisme::writeShape(p, shape, page == 2, seed); });
	}

	void copyToOther() {
		if (page == 2)
			return;
		before = bars;
		std::memcpy(bars.v[1 - page], bars.v[page], sizeof(bars.v[page]));
		recordEdit("copy a PRiSME spectrum");
	}

	void clearPage() {
		editPage("clear a PRiSME page", [=](float *p) {
			for (int i = 0; i < prisme::kBars; i++)
				p[i] = prisme::eraseValue(page);
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

// The screen: the bars of the page shown (A and B pink, T yellow), the other spectrum as a grey outline, and what the
// newest voice plays now as yellow ticks, so the envelopes are seen moving. Left draws, right erases, the wheel sets
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

		// the guides: -20 and -40 dB on A and B; on T, x1/4, x1 and x4 of DECAY
		nvgBeginPath(vg);
		for (float v : page == 2 ? std::initializer_list<float>{1.f / 6.f, 0.5f, 5.f / 6.f} : std::initializer_list<float>{1.f / 3.f, 2.f / 3.f}) {
			nvgMoveTo(vg, kPad, by(v));
			nvgLineTo(vg, w - kPad, by(v));
		}
		nvgStrokeColor(vg, theme::kKeyOff);
		nvgStrokeWidth(vg, 0.6f);
		nvgStroke(vg);

		// the bars
		nvgBeginPath(vg);
		for (int i = 0; i < prisme::kBars; i++) {
			const float v = b.v[page][i];
			if (page == 2) {
				// times: from the middle line (DECAY's own time) up or down
				const float y0 = by(0.5f), y1 = by(v);
				nvgRect(vg, bx(i) + 0.5f, std::min(y0, y1), bw - 1.f, std::max(std::fabs(y1 - y0), 0.8f));
			}
			else if (v > 0.f)
				nvgRect(vg, bx(i) + 0.5f, by(v), bw - 1.f, v * ph);
		}
		nvgFillColor(vg, page == 2 ? theme::kScreenYellow : theme::kWavePink);
		nvgFill(vg);

		if (page != 2) {
			// the other spectrum's outline
			const int other = 1 - page;
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
			if (module) {
				const prisme::Engine::Voice &v = module->engine.voices[clamp(module->engine.shownVoice, 0, prisme::kMaxVoices - 1)];
				if (v.stage != prisme::Engine::IDLE) {
					nvgBeginPath(vg);
					for (int i = 0; i < prisme::kBars; i++) {
						const float a = v.shown[i / 4][i % 4] / prisme::Engine::kVolts;
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
			static const char *names[3] = {"A", "B", "T  decay times"};
			nvgFontFaceId(vg, font->handle);
			nvgFontSize(vg, 8.f);
			nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			nvgFillColor(vg, theme::kScreenYellow);
			nvgText(vg, kPad, 2.f, names[page], NULL);
			if (module && module->engine.shownChannels > 0) {
				nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP);
				nvgText(vg, w - kPad, 2.f, rack::string::f("%d voice%s", module->engine.shownChannels,
					module->engine.shownChannels > 1 ? "s" : "").c_str(), NULL);
			}
		}
	}
};

// Under the screen, always there: the pages (A, B, T), the brush and the line, five shapes, copy (A to B or B to A) and
// clear. Every change goes into Rack's history. Black in every theme, as the screen.
struct PrismeStrip : OpaqueWidget {
	PRISME *module = nullptr;
	enum Cell { C_A, C_B, C_T, C_BRUSH, C_LINE, C_SAW, C_SQUARE, C_TRIANGLE, C_ORGAN, C_RANDOM, C_COPY, C_CLEAR, NUM_CELLS };

	float cell() const { return box.size.x / NUM_CELLS; }

	void onButton(const ButtonEvent &e) override {
		if (!module || e.action != GLFW_PRESS || e.button != GLFW_MOUSE_BUTTON_LEFT) {
			OpaqueWidget::onButton(e);
			return;
		}
		const int i = clamp((int)(e.pos.x / cell()), 0, NUM_CELLS - 1);
		switch (i) {
			case C_A: case C_B: case C_T: module->page = i - C_A; break;
			case C_BRUSH: module->tool = PRISME::TOOL_BRUSH; break;
			case C_LINE: module->tool = PRISME::TOOL_LINE; break;
			case C_COPY: module->copyToOther(); break;
			case C_CLEAR: module->clearPage(); break;
			default: module->applyShape(prisme::SHAPE_SAW + (i - C_SAW)); break;
		}
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
		const NVGcolor dim = nvgRGB(150, 150, 150);
		for (int i = 0; i < NUM_CELLS; i++) {
			const float x = i * c, cx = x + 0.5f * c, cy = 0.5f * hgt;
			bool selected = false;
			const NVGcolor ink = theme::kScreenYellow;
			if (i <= C_T) {
				selected = page == i - C_A;
				static const char *names[3] = {"A", "B", "T"};
				if (font && font->handle >= 0) {
					nvgFontFaceId(vg, font->handle);
					nvgFontSize(vg, 9.f);
					nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
					nvgFillColor(vg, selected ? ink : dim);
					nvgText(vg, cx, cy + 0.5f, names[i - C_A], NULL);
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
			else if (i >= C_SAW && i <= C_RANDOM) {
				// the shape as eight tiny bars
				float p[prisme::kBars];
				prisme::writeShape(p, i - C_SAW, false, 7u);
				const float bw = (c - 6.f) / 8.f, base = hgt - 2.5f, top = 2.5f;
				nvgBeginPath(vg);
				for (int k = 0; k < 8; k++) {
					const float v = p[k];
					if (v > 0.f)
						nvgRect(vg, x + 3.f + k * bw, base - v * (base - top), std::max(bw - 0.6f, 0.6f), v * (base - top));
				}
				nvgFillColor(vg, theme::kWavePink);
				nvgFill(vg);
			}
			else if (i == C_COPY) {
				// two overlapping squares, dimmed on T
				const float s = hgt - 7.f;
				nvgBeginPath(vg);
				nvgRect(vg, cx - 0.5f * s - 1.5f, cy - 0.5f * s - 1.5f, s, s);
				nvgRect(vg, cx - 0.5f * s + 1.5f, cy - 0.5f * s + 1.5f, s, s);
				nvgStrokeColor(vg, page == 2 ? theme::kKeyOff : ink);
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
		menu->addChild(createIndexSubmenuItem("Phases at note-on",
			{"Aligned: the classic waveforms", "Spread: the same sound, lower peaks"},
			[=]() { return (size_t)module->spreadPhases; },
			[=](size_t i) { module->spreadPhases = i == 1; }));
		menu->addChild(createIndexSubmenuItem("Page", {"A: first spectrum", "B: second spectrum", "T: decay times"},
			[=]() { return (size_t)module->page; },
			[=](size_t i) { module->page = (int)i; }));
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
		menu->addChild(createSubmenuItem("Shape on this page", "", [=](Menu *sub) {
			const char *names[] = {"Sawtooth", "Square", "Triangle", "Organ", "Random"};
			for (int s = 0; s < prisme::NUM_SHAPES; s++)
				sub->addChild(createMenuItem(names[s], "", [=]() { module->applyShape(s); }));
		}));
		menu->addChild(createMenuItem("Copy this spectrum to the other", "", [=]() { module->copyToOther(); }, module->page == 2));
		menu->addChild(createMenuItem("Clear this page", "", [=]() { module->clearPage(); }));
		menu->addChild(createMenuLabel("Left draws, right erases, Shift draws a line, the wheel sets the width"));
		menu->addChild(createMenuLabel("Undo, redo: " RACK_MOD_CTRL_NAME "+Z, " RACK_MOD_CTRL_NAME "+" RACK_MOD_SHIFT_NAME "+Z"));
	}
};

Model *modelPRISME = createModel<PRISME, PRISMEWidget>("PRISME");
