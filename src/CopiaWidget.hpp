// cOPIA panel: the Riso look in orange (an effect). The display shows the loop and the lanes, where the
// pattern is drawn; the panel SVG holds the labels.
#pragma once
#include "RisoComponents.hpp"

namespace copia_ui {
using namespace riso;
namespace g = copia::g;

static const NVGcolor ACCENT = ORANGE;
static const NVGcolor PALE = nvgRGB(0xE4, 0xDE, 0xD0);

inline std::shared_ptr<window::Font> monoFont() {
	return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
}

// An effect setting in its own terms (Mangle's units)
inline std::string paramText(g::Fx fx, int k, float v) {
	const g::ParamDef &d = g::paramDef(fx, k);
	switch (d.kind) {
		case g::Kind::Length: return g::lengths()[(size_t)(d.lo + g::normToStep(v, g::numSteps(d)))].label;
		case g::Kind::Percent: return rack::string::f("%.0f%%", v * 100.f);
		case g::Kind::Semitones: return rack::string::f("%+d st", g::semitoneValue(d, v));
		case g::Kind::Choice: return d.choices ? d.choices[g::stepValue(d, v)] : "";
		case g::Kind::Count: return std::to_string(g::countValue(d, v));
		case g::Kind::Range: return rack::string::f("%.*f%s", d.decimals, g::rangeValue(d, v), d.unit);
		default: return "";
	}
}

// A slot's effect setting in the current scene, as a menu slider
struct SlotParamQuantity : Quantity {
	COPIA *m = nullptr;
	int slot = 0, k = 0;
	copia::SlotData &sd() {
		return m->engine.scenes[m->engine.current.load()].slot[slot];
	}
	void setValue(float v) override {
		sd().p[k] = clamp(v, 0.f, 1.f);
	}
	float getValue() override {
		return sd().p[k].load();
	}
	float getDefaultValue() override {
		return g::paramDef(sd().effect(), k).def;
	}
	std::string getLabel() override {
		return g::paramDef(sd().effect(), k).name;
	}
	std::string getDisplayValueString() override {
		return paramText(sd().effect(), k, getValue());
	}
};

// Any float of the module through two lambdas, as a menu slider
struct LambdaQuantity : Quantity {
	std::function<float()> get;
	std::function<void(float)> set;
	std::function<std::string(float)> text;
	std::string label;
	float def = 0.f, lo = 0.f, hi = 1.f;
	void setValue(float v) override {
		set(clamp(v, lo, hi));
	}
	float getValue() override {
		return get();
	}
	float getMinValue() override {
		return lo;
	}
	float getMaxValue() override {
		return hi;
	}
	float getDefaultValue() override {
		return def;
	}
	std::string getLabel() override {
		return label;
	}
	std::string getDisplayValueString() override {
		return text ? text(getValue()) : rack::string::f("%.0f%%", getValue() * 100.f);
	}
};

inline ui::Slider *slider(Quantity *q) {
	ui::Slider *s = new ui::Slider;
	s->quantity = q;
	s->box.size.x = 220.f;
	return s;
}

inline const char *const kFilterNames[] = {"None", "Low pass", "High pass", "Band pass", "Notch"};
inline const char *const kShapeNames[] = {"Hold", "Ramp up", "Ramp down", "Swell", "Steps"};

// The slot's menu (its label on the display): effect, settings, source, filter, Chance weight, lane
inline void slotMenu(COPIA *m, int s) {
	Menu *menu = createMenu();
	copia::Engine *e = &m->engine;
	copia::Scene &sc = e->scenes[e->current.load()];
	copia::SlotData &sd = sc.slot[s];
	menu->addChild(createMenuLabel(rack::string::f("Slot %d (scene %d)", s + 1, e->current.load() + 1)));
	std::vector<std::string> names;
	for (int i = 0; i < copia::kNumEffects; i++)
		names.push_back(g::kEffectNames[(int)copia::kEffects[i].fx]);
	menu->addChild(createIndexSubmenuItem("Effect", names,
		[=]() { return (size_t)clamp((int)std::round(m->params[COPIA::FX_PARAM + s].getValue()), 0, copia::kNumEffects - 1); },
		[=](size_t i) { m->params[COPIA::FX_PARAM + s].setValue((float)i); }));
	const g::Fx fx = sd.effect();
	for (int k = 0; k < g::kNumEffectParams; k++) {
		if (g::paramDef(fx, k).kind == g::Kind::Unused)
			continue;
		SlotParamQuantity *q = new SlotParamQuantity;
		q->m = m;
		q->slot = s;
		q->k = k;
		menu->addChild(slider(q));
	}
	menu->addChild(new MenuSeparator);
	menu->addChild(createIndexSubmenuItem("Capture effects read", {"LOOP: the captured bars", "LIVE: the live input"},
		[=]() { return (size_t)(e->scenes[e->current.load()].slot[s].live.load() ? 1 : 0); },
		[=](size_t i) { e->scenes[e->current.load()].slot[s].live = i == 1; }));
	menu->addChild(createIndexSubmenuItem("Filter", {kFilterNames[0], kFilterNames[1], kFilterNames[2], kFilterNames[3], kFilterNames[4]},
		[=]() { return (size_t)e->scenes[e->current.load()].slot[s].filter.load(); },
		[=](size_t i) { e->scenes[e->current.load()].slot[s].filter = (int)i; }));
	LambdaQuantity *fq = new LambdaQuantity;
	fq->label = "Filter frequency";
	fq->def = g::freqMap().convertTo0to1(1000.f);
	fq->get = [=]() { return e->scenes[e->current.load()].slot[s].freq.load(); };
	fq->set = [=](float v) { e->scenes[e->current.load()].slot[s].freq = v; };
	fq->text = [](float v) { return rack::string::f("%.0f Hz", g::freqMap().convertFrom0to1(v)); };
	menu->addChild(slider(fq));
	LambdaQuantity *wq = new LambdaQuantity;
	wq->label = "Weight (Rand, Chance lane)";
	wq->def = 1.f;
	wq->get = [=]() { return e->scenes[e->current.load()].slot[s].weight.load(); };
	wq->set = [=](float v) { e->scenes[e->current.load()].slot[s].weight = v; };
	menu->addChild(slider(wq));
	menu->addChild(new MenuSeparator);
	menu->addChild(createBoolMenuItem("Mute", "", [=]() { return m->mute[s].load(); }, [=](bool v) { m->mute[s] = v; }));
	menu->addChild(createMenuItem("Dice: a new lane", "", [=]() { m->randLane = s; }));
	menu->addChild(createMenuItem("Clear the lane", "", [=]() { e->scenes[e->current.load()].pattern.clearLane(s); }));
}

// The display: the loop (waveform, bars, play head, capture) above the lanes of the current scene.
// Lanes: drag on an empty place draws a block; drag a block sideways to move it, up or down for its
// level; double-click it for its shape, probability and ratchets; right-click erases it, a double
// right-click clears the lane. Labels: the number mutes, the name opens the slot's menu, LP / LV
// switches the source of its capture effects.
struct CopiaDisplay : OpaqueWidget {
	COPIA *module = nullptr;
	static constexpr float WAVE = 30.f, LABEL = 72.f, TOP = 34.f;

	// drag state
	int dragLane = -1, dragMode = 0;      // 1 draw, 2 move or level (decided by the first move), 3 move, 4 level
	int pressStep = 0, curStep = 0;
	int blockFirst = 0, blockLast = 0;
	g::BlockInfo dragInfo;
	float dragX = 0.f, dragY = 0.f, startLevel = 1.f;
	double lastRight = -1.0;
	int lastRightLane = -1;

	float laneH() const {
		return (box.size.y - TOP) / copia::kLanes;
	}
	copia::Scene &scene() {
		return module->engine.scenes[module->engine.current.load()];
	}
	int steps() {
		return std::max(1, scene().steps(module->beatsPerBar));
	}
	float stepW() {
		return (box.size.x - LABEL) / steps();
	}
	int laneAt(float y) const {
		if (y < TOP)
			return -1;
		return clamp((int)((y - TOP) / laneH()), 0, copia::kLanes - 1);
	}
	int stepAt(float x) {
		return clamp((int)((x - LABEL) / stepW()), 0, steps() - 1);
	}

	void onButton(const ButtonEvent &e) override {
		if (!module) {
			OpaqueWidget::onButton(e);
			return;
		}
		const int lane = laneAt(e.pos.y);
		if (e.action != GLFW_PRESS) {
			OpaqueWidget::onButton(e);
			return;
		}
		if (lane < 0) {
			// the wave strip: the module's menu items for the scene
			if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				sceneMenu();
				e.consume(this);
			}
			return;
		}
		g::Pattern &p = scene().pattern;
		if (e.pos.x < LABEL) {
			if (e.button == GLFW_MOUSE_BUTTON_LEFT || e.button == GLFW_MOUSE_BUTTON_RIGHT) {
				if (lane == copia::kChance)
					chanceMenu();
				else if (e.pos.x < 16.f)
					module->mute[lane] = !module->mute[lane].load();
				else if (e.pos.x > LABEL - 20.f && e.button == GLFW_MOUSE_BUTTON_LEFT)
					scene().slot[lane].live = !scene().slot[lane].live.load();
				else
					slotMenu(module, lane);
				e.consume(this);
			}
			return;
		}
		const int st = stepAt(e.pos.x);
		if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			const double now = system::getTime();
			if (lastRightLane == lane && now - lastRight < 0.35) {
				p.clearLane(lane);
				lastRight = -1.0;
			}
			else {
				int first, last;
				if (p.blockAt(lane, st, first, last))
					p.erase(lane, first, last);
				lastRight = now;
				lastRightLane = lane;
			}
			e.consume(this);
			return;
		}
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			dragLane = lane;
			pressStep = curStep = st;
			dragX = e.pos.x;
			dragY = e.pos.y;
			if (p.blockAt(lane, st, blockFirst, blockLast)) {
				dragMode = 2;
				dragInfo = p.infoAt(lane, blockFirst);
				startLevel = dragInfo.level * 0.01f;
			}
			else {
				dragMode = 1;
				p.setBlock(lane, st, st);
			}
			OpaqueWidget::onButton(e);
			e.consume(this);
		}
	}

	void onDoubleClick(const DoubleClickEvent &e) override {
		if (!module || dragLane < 0)
			return;
		int first, last;
		if (scene().pattern.blockAt(dragLane, pressStep, first, last)) {
			blockMenu(dragLane, first);
			e.consume(this);
		}
	}

	void onDragStart(const DragStartEvent &e) override {
		e.consume(this);
	}

	void onDragMove(const DragMoveEvent &e) override {
		if (!module || dragLane < 0)
			return;
		const float zoom = getAbsoluteZoom();
		dragX += e.mouseDelta.x / zoom;
		dragY += e.mouseDelta.y / zoom;
		g::Pattern &p = scene().pattern;
		const int st = stepAt(dragX);
		if (dragMode == 1) {
			if (st != curStep) {
				curStep = st;
				p.erase(dragLane, std::min(pressStep, st), std::max(pressStep, st));
				p.setBlock(dragLane, std::min(pressStep, st), std::max(pressStep, st));
			}
			return;
		}
		if (dragMode == 2) {
			const float dx = std::fabs(dragX - (LABEL + (pressStep + 0.5f) * stepW()));
			const float dy = std::fabs(dragY - (TOP + (dragLane + 0.5f) * laneH()));
			if (dx > 5.f)
				dragMode = 3;
			else if (dy > 5.f)
				dragMode = 4;
		}
		if (dragMode == 3 && st != curStep) {
			// move the block, as long as it stays inside the pattern
			const int len = blockLast - blockFirst;
			const int shift = st - curStep;
			const int nf = blockFirst + shift;
			if (nf >= 0 && nf + len < steps()) {
				p.erase(dragLane, blockFirst, blockLast);
				blockFirst = nf;
				blockLast = nf + len;
				p.setBlock(dragLane, blockFirst, blockLast, dragInfo);
				curStep = st;
			}
		}
		if (dragMode == 4) {
			const float lvl = clamp(startLevel - (dragY - (TOP + (dragLane + 0.5f) * laneH())) / 60.f, 0.05f, 1.f);
			dragInfo.level = (uint8_t)std::round(lvl * 100.f);
			p.setInfo(dragLane, blockFirst, dragInfo);
		}
	}

	void onDragEnd(const DragEndEvent &e) override {
		dragMode = 0;
	}

	void blockMenu(int lane, int first) {
		COPIA *m = module;
		Menu *menu = createMenu();
		auto pat = [=]() -> g::Pattern & { return m->engine.scenes[m->engine.current.load()].pattern; };
		menu->addChild(createMenuLabel(rack::string::f("Block at step %d", first + 1)));
		LambdaQuantity *lq = new LambdaQuantity;
		lq->label = "Level";
		lq->def = 1.f;
		lq->get = [=]() { return pat().levelAt(lane, first); };
		lq->set = [=](float v) { g::BlockInfo b = pat().infoAt(lane, first); b.level = (uint8_t)std::round(v * 100.f); pat().setInfo(lane, first, b); };
		menu->addChild(slider(lq));
		menu->addChild(createIndexSubmenuItem("Shape", {kShapeNames[0], kShapeNames[1], kShapeNames[2], kShapeNames[3], kShapeNames[4]},
			[=]() { return (size_t)pat().shapeAt(lane, first); },
			[=](size_t i) { g::BlockInfo b = pat().infoAt(lane, first); b.shape = (uint8_t)i; pat().setInfo(lane, first, b); }));
		LambdaQuantity *pq = new LambdaQuantity;
		pq->label = "Probability";
		pq->def = 1.f;
		pq->get = [=]() { return pat().probabilityAt(lane, first); };
		pq->set = [=](float v) { g::BlockInfo b = pat().infoAt(lane, first); b.probability = (uint8_t)std::round(v * 100.f); pat().setInfo(lane, first, b); };
		menu->addChild(slider(pq));
		menu->addChild(createIndexSubmenuItem("Ratchets", {"None", "x2", "x3", "x4", "x5", "x6", "x7", "x8"},
			[=]() { return (size_t)(pat().ratchetsAt(lane, first) - 1); },
			[=](size_t i) { g::BlockInfo b = pat().infoAt(lane, first); b.ratchets = (uint8_t)(i + 1); pat().setInfo(lane, first, b); }));
		menu->addChild(createMenuItem("Erase", "", [=]() {
			int a, z;
			if (pat().blockAt(lane, first, a, z))
				pat().erase(lane, a, z);
		}));
	}

	void chanceMenu() {
		COPIA *m = module;
		Menu *menu = createMenu();
		menu->addChild(createMenuLabel("Chance lane: each block plays one slot, picked by weight"));
		menu->addChild(createMenuItem("Dice: a new lane", "", [=]() { m->randLane = copia::kChance; }));
		menu->addChild(createMenuItem("Clear the lane", "", [=]() { m->engine.scenes[m->engine.current.load()].pattern.clearLane(copia::kChance); }));
	}

	void sceneMenu() {
		COPIA *m = module;
		Menu *menu = createMenu();
		menu->addChild(createMenuLabel(rack::string::f("Scene %d", m->engine.current.load() + 1)));
		menu->addChild(createIndexSubmenuItem("Pattern length", {"1 bar", "2 bars"},
			[=]() { return (size_t)(m->engine.scenes[m->engine.current.load()].bars.load() - 1); },
			[=](size_t i) { m->engine.scenes[m->engine.current.load()].bars = (int)i + 1; }));
		menu->addChild(createMenuItem("Rand", "", [=]() { m->uiRand = true; }));
		menu->addChild(createMenuItem("Mutate", "", [=]() { m->uiMutate = true; }));
		menu->addChild(createMenuItem("Clear the pattern", "", [=]() { m->engine.scenes[m->engine.current.load()].pattern.clear(); }));
		menu->addChild(createMenuItem("Clear the loop", "", [=]() { m->engine.clearRequest = true; m->dirty = true; }));
	}

	void drawLayer(const DrawArgs &args, int layer) override {
		if (layer == 1)
			drawContent(args);
		OpaqueWidget::drawLayer(args, layer);
	}

	void draw(const DrawArgs &args) override {
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, 3.f);
		nvgFillColor(args.vg, INK);
		nvgFill(args.vg);
		if (!module)
			drawContent(args);
	}

	void text(const DrawArgs &args, float x, float y, const std::string &s, int align, NVGcolor c, float size = 9.f) {
		nvgFontSize(args.vg, size);
		nvgTextAlign(args.vg, align);
		nvgFillColor(args.vg, c);
		nvgText(args.vg, x, y, s.c_str(), nullptr);
	}

	void drawContent(const DrawArgs &args) {
		std::shared_ptr<window::Font> f = monoFont();
		if (!f)
			return;
		nvgFontFaceId(args.vg, f->handle);
		const float W = box.size.x;
		const NVGcolor dim = nvgRGB(0x57, 0x53, 0x4A);
		if (!module) {
			text(args, W / 2, WAVE / 2, "cOPIA  live resampler", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, PAPER, 11.f);
			return;
		}
		copia::Engine &e = module->engine;
		// ---- the loop
		const int fr = e.front.load();
		const int cs = e.capState.load();
		const float ww = W - LABEL;
		if (fr >= 0) {
			const copia::Loop &lp = e.loops[fr];
			nvgBeginPath(args.vg);
			for (int i = 0; i < copia::kPeaks; i++) {
				const float x = LABEL + ww * i / copia::kPeaks;
				const float h = std::min(1.f, lp.peaks[i]) * (WAVE / 2 - 2);
				nvgRect(args.vg, x, WAVE / 2 - h, std::max(1.f, ww / copia::kPeaks), 2 * h + 0.5f);
			}
			nvgFillColor(args.vg, PALE);
			nvgFill(args.vg);
			// the bars
			const int bars = std::max(1, (int)std::round(lp.beats / std::max(1, module->beatsPerBar)));
			for (int b = 1; b < bars; b++) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, LABEL + ww * b / bars, 0, 1, WAVE);
				nvgFillColor(args.vg, dim);
				nvgFill(args.vg);
			}
			const float ph = e.loopPhase.load();
			if (ph >= 0.f) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, LABEL + ww * ph - 1, 0, 2, WAVE);
				nvgFillColor(args.vg, ACCENT);
				nvgFill(args.vg);
			}
		}
		if (cs == 2) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, LABEL, WAVE - 3, ww * e.capProgress.load(), 3);
			nvgFillColor(args.vg, ACCENT);
			nvgFill(args.vg);
		}
		// what is going on, top left
		std::string state;
		if (cs == 1)
			state = "ARMED";
		else if (cs == 2)
			state = "REC";
		else if (fr >= 0)
			state = rack::string::f("%d BARS", std::max(1, (int)std::round(e.loops[fr].beats / std::max(1, module->beatsPerBar))));
		else
			state = "NO LOOP";
		const bool blink = cs != 1 || std::fmod(system::getTime(), 0.5) < 0.25;
		if (blink)
			text(args, 4, 3, state, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, cs > 0 ? ACCENT : PAPER, 10.f);
		const float bpm = module->bpmNow.load();
		text(args, 4, 16, module->runningNow.load() ? rack::string::f("%.1f BPM", bpm) : std::string("STOPPED"), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, dim, 8.f);
		std::string right = rack::string::f("SC %d", e.current.load() + 1);
		const float morph = module->params[COPIA::MORPH_PARAM].getValue() + module->inputs[COPIA::MORPH_INPUT].getVoltage() * 0.1f;
		if (morph > 0.005f)
			right += rack::string::f(" > %d %.0f%%", module->morphB.load() + 1, clamp(morph, 0.f, 1.f) * 100.f);
		right += e.isIn.load() ? "  IN" : "  OUT";
		text(args, W - 4, 3, right, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, e.isIn.load() ? ACCENT : PAPER, 9.f);

		// ---- the lanes
		copia::Scene &sc = scene();
		const int n = steps();
		const float sw = stepW(), lh = laneH();
		const int div = sc.div();
		// beat shading
		for (int st = 0; st < n; st++) {
			if ((st / div) % 2 == 1) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, LABEL + st * sw, TOP, sw, lh * copia::kLanes);
				nvgFillColor(args.vg, nvgRGB(0x26, 0x25, 0x21));
				nvgFill(args.vg);
			}
		}
		const float play = e.playStep();
		for (int lane = 0; lane < copia::kLanes; lane++) {
			const float y = TOP + lane * lh;
			const bool chance = lane == copia::kChance;
			const bool muted = !chance && module->mute[lane].load();
			// the label
			if (chance) {
				text(args, 4, y + lh / 2, "? CHANCE", NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, PAPER, 8.f);
			}
			else {
				const bool sounding = e.sounding(lane);
				text(args, 4, y + lh / 2, std::to_string(lane + 1), NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, muted ? dim : sounding ? ACCENT : PAPER, 9.f);
				text(args, 16, y + lh / 2, copia::kEffects[copia::effectIndex(sc.slot[lane].effect())].shortName, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE, muted ? dim : PAPER, 8.f);
				text(args, LABEL - 4, y + lh / 2, sc.slot[lane].live.load() ? "LV" : "LP", NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE, sc.slot[lane].live.load() ? ACCENT : dim, 8.f);
			}
			// the blocks
			for (int st = 0; st < n;) {
				int first, last;
				if (!sc.pattern.blockAt(lane, st, first, last)) {
					st++;
					continue;
				}
				last = std::min(last, n - 1);
				const float level = sc.pattern.levelAt(lane, first);
				const float prob = sc.pattern.probabilityAt(lane, first);
				const float x0 = LABEL + first * sw + 0.5f, x1 = LABEL + (last + 1) * sw - 0.5f;
				const float h = (lh - 3) * std::max(0.2f, level);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x0, y + lh - 1.5f - h, x1 - x0, h);
				NVGcolor c = muted ? dim : chance ? PALE : ACCENT;
				if (prob < 1.f)
					c = nvgTransRGBAf(c, 0.45f);
				nvgFillColor(args.vg, c);
				nvgFill(args.vg);
				const int rat = sc.pattern.ratchetsAt(lane, first);
				for (int k = 1; k < rat; k++) {
					nvgBeginPath(args.vg);
					nvgRect(args.vg, x0 + (x1 - x0) * k / rat, y + lh - 1.5f - h, 1, h);
					nvgFillColor(args.vg, INK);
					nvgFill(args.vg);
				}
				st = last + 1;
			}
			// lane line
			nvgBeginPath(args.vg);
			nvgRect(args.vg, LABEL, y + lh - 0.5f, W - LABEL, 0.5f);
			nvgFillColor(args.vg, dim);
			nvgFill(args.vg);
		}
		if (play >= 0.f) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, LABEL + std::fmod(play, (float)n) * sw - 0.5f, TOP, 1.f, lh * copia::kLanes);
			nvgFillColor(args.vg, PAPER);
			nvgFill(args.vg);
		}
	}
};

// The eight scene pads: click plays the scene (on the scene grid); right-click: morph B, copy, paste,
// clear. The scene that plays is filled, the one waiting outlined, B marked.
struct ScenePads : OpaqueWidget {
	COPIA *module = nullptr;
	static constexpr float PAD = 18.f, STEP = 21.5f;

	int padAt(float x) const {
		return clamp((int)(x / STEP), 0, copia::kScenes - 1);
	}

	static copia::Scene &clipboard() {
		static copia::Scene *c = new copia::Scene;
		return *c;
	}
	static bool &clipboardFull() {
		static bool full = false;
		return full;
	}

	void onButton(const ButtonEvent &e) override {
		if (!module || e.action != GLFW_PRESS)
			return;
		const int i = padAt(e.pos.x);
		COPIA *m = module;
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			m->sceneTarget = i;
			e.consume(this);
		}
		else if (e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			Menu *menu = createMenu();
			menu->addChild(createMenuLabel(rack::string::f("Scene %d", i + 1)));
			menu->addChild(createCheckMenuItem("Morph B (the MORPH knob goes to it)", "", [=]() { return m->morphB.load() == i; }, [=]() { m->morphB = i; }));
			menu->addChild(createMenuItem("Copy", "", [=]() {
				clipboard().copyFrom(m->engine.scenes[i]);
				clipboardFull() = true;
			}));
			menu->addChild(createMenuItem("Paste", "", [=]() {
				if (clipboardFull())
					m->engine.scenes[i].copyFrom(clipboard());
			}));
			menu->addChild(createMenuItem("Clear (it starts again as a copy)", "", [=]() {
				if (m->engine.current.load() != i)
					m->engine.scenes[i].used = false;
			}));
			e.consume(this);
		}
	}

	void draw(const DrawArgs &args) override {
		std::shared_ptr<window::Font> f = monoFont();
		const int cur = module ? module->engine.current.load() : 0;
		const int target = module ? module->sceneTarget.load() : 0;
		const int b = module ? module->morphB.load() : 1;
		for (int i = 0; i < copia::kScenes; i++) {
			const float x = i * STEP;
			const bool used = module ? module->engine.scenes[i].used.load() : i == 0;
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x + 0.5f, 0.5f, PAD - 1, PAD - 1, 2.f);
			nvgFillColor(args.vg, i == cur ? ACCENT : used ? TRACK : PAPER);
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, i == target && target != cur ? ACCENT : INK);
			nvgStrokeWidth(args.vg, i == target && target != cur ? 2.f : 1.f);
			nvgStroke(args.vg);
			if (f) {
				nvgFontFaceId(args.vg, f->handle);
				nvgFontSize(args.vg, 9.f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFillColor(args.vg, i == cur ? PAPER : INK);
				nvgText(args.vg, x + PAD / 2, PAD / 2 + 0.5f, i == b ? "B" : std::to_string(i + 1).c_str(), nullptr);
			}
		}
	}
};

// CAPTURE: lit while armed (blinking) and while recording, a ring once a loop plays; Shift+click clears
struct CaptureButton : app::Switch {
	COPIA *m = nullptr;
	CaptureButton() {
		momentary = true;
		box.size = Vec(28, 28);
	}
	void onButton(const ButtonEvent &e) override {
		if (m && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
			m->engine.clearRequest = true;
			m->dirty = true;
			e.consume(this);
			return;
		}
		app::Switch::onButton(e);
	}
	void draw(const DrawArgs &args) override {
		const float c = box.size.x / 2;
		const int st = m ? m->engine.capState.load() : 0;
		const bool loop = m && m->engine.front.load() >= 0;
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, c - 0.5f);
		nvgFillColor(args.vg, INK);
		nvgFill(args.vg);
		bool lit = st == 2 || (st == 1 && std::fmod(system::getTime(), 0.5) < 0.25);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, c * 0.5f);
		nvgFillColor(args.vg, lit ? ACCENT : PAPER);
		nvgFill(args.vg);
		if (loop || st == 2) {
			const float a0 = -M_PI / 2;
			const float a1 = st == 2 ? a0 + 2 * M_PI * m->engine.capProgress.load() : a0 + 2 * M_PI;
			nvgBeginPath(args.vg);
			nvgArc(args.vg, c, c, c * 0.78f, a0, a1, NVG_CW);
			nvgStrokeColor(args.vg, ACCENT);
			nvgStrokeWidth(args.vg, 2.f);
			nvgStroke(args.vg);
		}
	}
};

}  // namespace copia_ui

struct COPIAWidget : BidooWidget {
	COPIAWidget(COPIA *module) {
		using namespace copia_ui;
		setModule(module);
		themable = false; // the Riso panel has no other look
		prepareThemes(asset::plugin(pluginInstance, "res/COPIA.svg"));

		CopiaDisplay *display = new CopiaDisplay;
		display->module = module;
		display->box.pos = Vec(10, 22);
		display->box.size = Vec(490, 150);
		addChild(display);

		// row 1: capture, length, grid, rand, mutate, scenes, morph, mix, in/out
		const float r1 = 208.f;
		CaptureButton *cap = createParamCentered<CaptureButton>(Vec(30, r1), module, COPIA::CAPTURE_PARAM);
		cap->m = module;
		addParam(cap);
		auto knob = [&](float x, float y, int param, bool snapped) {
			RisoKnob *k = snapped ? (RisoKnob *)createParamCentered<RisoSnapKnob>(Vec(x, y), module, param)
			                      : createParamCentered<RisoKnob>(Vec(x, y), module, param);
			k->arc = ACCENT;
			addParam(k);
		};
		auto button = [&](float x, float y, int param) {
			RisoButton *b = createParamCentered<RisoButton>(Vec(x, y), module, param);
			b->lit = ACCENT;
			addParam(b);
			return b;
		};
		knob(72, r1, COPIA::LENGTH_PARAM, true);
		knob(110, r1, COPIA::GRID_PARAM, true);
		button(146, r1, COPIA::RAND_PARAM)->momentary = true;
		button(178, r1, COPIA::MUTATE_PARAM)->momentary = true;
		ScenePads *pads = new ScenePads;
		pads->module = module;
		pads->box.pos = Vec(200, r1 - 9);
		pads->box.size = Vec(ScenePads::STEP * copia::kScenes, ScenePads::PAD);
		addChild(pads);
		knob(398, r1, COPIA::MORPH_PARAM, false);
		knob(436, r1, COPIA::MIX_PARAM, false);
		RisoButton *inout = button(478, r1, COPIA::INOUT_PARAM);
		inout->box.size = Vec(28, 28);
		inout->box.pos = Vec(478 - 14, r1 - 14);

		// row 2: each slot's effect and punch-in knobs
		const float r2 = 256.f;
		for (int s = 0; s < copia::kSlots; s++) {
			const float cx = 10.f + 61.25f * (s + 0.5f);
			knob(cx - 14.f, r2, COPIA::FX_PARAM + s, true);
			knob(cx + 14.f, r2, COPIA::PUNCH_PARAM + s, false);
		}

		// jacks: inputs on paper, outputs on the ink block
		auto jx = [](int i) { return 24.f + 36.f * i; };
		const int rowA[9] = {COPIA::INL_INPUT, COPIA::INR_INPUT, COPIA::CLOCK_INPUT, COPIA::RESET_INPUT, COPIA::CAPTURE_INPUT,
		                     COPIA::INOUT_INPUT, COPIA::SCENE_INPUT, COPIA::MORPH_INPUT, COPIA::MIX_INPUT};
		for (int i = 0; i < 9; i++)
			addInput(createInputCentered<RisoJack>(Vec(jx(i), 302.f), module, rowA[i]));
		for (int s = 0; s < copia::kSlots; s++)
			addInput(createInputCentered<RisoJack>(Vec(jx(s), 342.f), module, COPIA::PUNCH_INPUT + s));
		const int outA[4] = {COPIA::OUTL_OUTPUT, COPIA::OUTR_OUTPUT, COPIA::BAR_OUTPUT, COPIA::REC_OUTPUT};
		const int outB[3] = {COPIA::WETL_OUTPUT, COPIA::WETR_OUTPUT, COPIA::LANES_OUTPUT};
		for (int i = 0; i < 4; i++) {
			RisoJack *j = createOutputCentered<RisoJack>(Vec(362.f + 36.f * i, 302.f), module, outA[i]);
			j->onInk = true;
			addOutput(j);
		}
		for (int i = 0; i < 3; i++) {
			RisoJack *j = createOutputCentered<RisoJack>(Vec(362.f + 36.f * i, 342.f), module, outB[i]);
			j->onInk = true;
			addOutput(j);
		}
	}

	void appendContextMenu(Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		COPIA *m = dynamic_cast<COPIA *>(module);
		if (!m)
			return;
		using namespace copia_ui;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Clock"));
		static const int ppqns[8] = {1, 2, 4, 8, 12, 24, 48, 96};
		menu->addChild(createIndexSubmenuItem("Clock pulses per beat", {"1", "2", "4 (16th notes)", "8", "12", "24 (MIDI clock)", "48", "96"},
			[=]() { for (int i = 0; i < 8; i++) if (ppqns[i] == m->clockPpqn) return (size_t)i; return (size_t)2; },
			[=](size_t i) { m->clockPpqn = ppqns[i]; m->updateClockName(); }));
		static const int bpbs[4] = {3, 4, 5, 7};
		menu->addChild(createIndexSubmenuItem("Beats per bar", {"3", "4", "5", "7"},
			[=]() { for (int i = 0; i < 4; i++) if (bpbs[i] == m->beatsPerBar) return (size_t)i; return (size_t)1; },
			[=](size_t i) { m->beatsPerBar = bpbs[i]; }));
		LambdaQuantity *bq = new LambdaQuantity;
		bq->label = "Tempo without a clock";
		bq->lo = 30.f;
		bq->hi = 300.f;
		bq->def = 120.f;
		bq->get = [=]() { return m->internalBpm; };
		bq->set = [=](float v) { m->internalBpm = std::round(v * 10.f) / 10.f; };
		bq->text = [](float v) { return rack::string::f("%.1f BPM", v); };
		menu->addChild(slider(bq));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Remix"));
		static const std::vector<std::string> quants = {"At once", "Next step", "Next beat", "Next bar"};
		menu->addChild(createIndexPtrSubmenuItem("Scene changes wait for", quants, &m->sceneQuant));
		menu->addChild(createIndexPtrSubmenuItem("IN/OUT waits for", quants, &m->inQuant));
		menu->addChild(createIndexPtrSubmenuItem("Mix law", {"Equal power", "Crossfade", "The live mix stays", "The remix stays", "Remix only (replace)"}, &m->law));
		menu->addChild(createBoolPtrMenuItem("Remix comes in after capture", "", &m->autoIn));
		menu->addChild(createBoolPtrMenuItem("Capture the output instead of the input (resample the remix)", "", &m->resampleOut));
		LambdaQuantity *mq = new LambdaQuantity;
		mq->label = "Mutate amount";
		mq->def = 0.3f;
		mq->get = [=]() { return m->mutateAmount; };
		mq->set = [=](float v) { m->mutateAmount = v; };
		menu->addChild(slider(mq));
		menu->addChild(createMenuItem("Clear the loop", "Shift+click CAPTURE", [=]() { m->engine.clearRequest = true; m->dirty = true; }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Lanes: drag draws a block; drag a block to move it, up or down for its level"));
		menu->addChild(createMenuLabel("Double-click a block for its shape, probability, ratchets; right-click erases"));
		menu->addChild(createMenuLabel("Labels: number mutes, name opens the slot, LP / LV: the loop or the live input"));
	}
};
