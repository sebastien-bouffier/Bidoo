// sOUNAiRe panel: Riso look (paper, ink, blue for a sound source). The panel SVG
// holds the labels; knobs, buttons, jacks and the display draw themselves.
#pragma once

namespace sounaire_ui {

static const NVGcolor PAPER = nvgRGB(0xF2, 0xEE, 0xE4);
static const NVGcolor INK = nvgRGB(0x1B, 0x1A, 0x17);
static const NVGcolor TRACK = nvgRGB(0xD3, 0xCC, 0xBC);
static const NVGcolor BLUE = nvgRGB(0x2F, 0x55, 0xD4);
static const NVGcolor ORANGE = nvgRGB(0xF0, 0x62, 0x2F);
static const NVGcolor GRAPHITE = nvgRGB(0x5E, 0x5A, 0x52);

inline std::string noteName(float n) {
	return ::noteName(n);
}

// a knob drawn in ink with a value arc in the module's colour
struct RisoKnob : app::Knob {
	NVGcolor arc = BLUE;
	RisoKnob() {
		box.size = Vec(26, 26);
		minAngle = -0.75f * M_PI;
		maxAngle = 0.75f * M_PI;
	}
	void draw(const DrawArgs &args) override {
		engine::ParamQuantity *pq = getParamQuantity();
		float v = pq ? pq->getScaledValue() : 0.f;
		float cx = box.size.x / 2, cy = box.size.y / 2, r = box.size.x / 2 - 1.5f;
		float a0 = 0.75f * M_PI, a1 = a0 + 1.5f * M_PI;
		float av = a0 + v * 1.5f * M_PI;
		nvgLineCap(args.vg, NVG_ROUND);
		nvgStrokeWidth(args.vg, 2.2f);
		nvgBeginPath(args.vg);
		nvgArc(args.vg, cx, cy, r, a0, a1, NVG_CW);
		nvgStrokeColor(args.vg, TRACK);
		nvgStroke(args.vg);
		if (v > 0.005f) {
			nvgBeginPath(args.vg);
			nvgArc(args.vg, cx, cy, r, a0, av, NVG_CW);
			nvgStrokeColor(args.vg, arc);
			nvgStroke(args.vg);
		}
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, cx, cy, r * 0.74f);
		nvgFillColor(args.vg, INK);
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, cx, cy);
		nvgLineTo(args.vg, cx + std::cos(av) * r * 0.55f, cy + std::sin(av) * r * 0.55f);
		nvgStrokeColor(args.vg, PAPER);
		nvgStrokeWidth(args.vg, 2.f);
		nvgStroke(args.vg);
	}
};

struct RisoSnapKnob : RisoKnob {
	RisoSnapKnob() {
		snap = true;
	}
};

// latching button: ink, lit in the module's colour when on; faded while
// `dimmed` says it has no effect
struct RisoButton : app::Switch {
	std::function<bool()> dimmed;
	RisoButton() {
		box.size = Vec(20, 20);
	}
	void draw(const DrawArgs &args) override {
		engine::ParamQuantity *pq = getParamQuantity();
		bool on = pq && pq->getValue() > 0.5f;
		if (dimmed && dimmed())
			nvgGlobalAlpha(args.vg, 0.3f);
		float c = box.size.x / 2;
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, c - 0.5f);
		nvgFillColor(args.vg, INK);
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, c * 0.42f);
		nvgFillColor(args.vg, on ? BLUE : PAPER);
		nvgFill(args.vg);
	}
};

// round jack: on paper for inputs, on the ink block for outputs
struct RisoJack : app::PortWidget {
	bool onInk = false;
	RisoJack() {
		box.size = Vec(24, 24);
	}
	void draw(const DrawArgs &args) override {
		float c = box.size.x / 2, r = c - 1.f;
		// a round nut
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, r);
		nvgFillColor(args.vg, onInk ? INK : PAPER);
		nvgFill(args.vg);
		nvgStrokeColor(args.vg, onInk ? PAPER : INK);
		nvgStrokeWidth(args.vg, 1.2f);
		nvgStroke(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, r * 0.55f);
		nvgFillColor(args.vg, onInk ? nvgRGB(0x57, 0x53, 0x4A) : nvgRGB(0xCF, 0xC8, 0xB8));
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, r * 0.3f);
		nvgFillColor(args.vg, INK);
		nvgFill(args.vg);
	}
};

}  // namespace sounaire_ui

// An undo step for the slices: the state before and after an edit (cuts,
// play types, choke groups). Found again by module id, so it survives the
// module being deleted and brought back.
struct SounaireSliceAction : history::ModuleAction {
	sounaire::Slices before, after;
	void apply(const sounaire::Slices &u) {
		SOUNAIRE *m = dynamic_cast<SOUNAIRE *>(APP->engine->getModule(moduleId));
		if (m)
			m->restoreSlices(u);
	}
	void undo() override {
		apply(before);
	}
	void redo() override {
		apply(after);
	}
};

inline bool sameSlices(const sounaire::Slices &a, const sounaire::Slices &b) {
	if (a.count != b.count)
		return false;
	for (int i = 0; i < a.count; i++)
		if (a.start[i] != b.start[i] || a.type[i] != b.type[i] || a.choke[i] != b.choke[i])
			return false;
	return true;
}

// an edit done by `edit`, as one undo step named `name`
template <typename F>
inline void sliceEdit(SOUNAIRE *m, const std::string &name, F edit) {
	sounaire::Slices before = m->getSlices();
	edit();
	sounaire::Slices after = m->getSlices();
	if (sameSlices(before, after))
		return;
	SounaireSliceAction *a = new SounaireSliceAction;
	a->moduleId = m->id;
	a->name = name;
	a->before = before;
	a->after = after;
	APP->history->push(a);
}

// The slices as the module last published them, copied again only when its
// edit counter moves (not every frame under the mutex)
struct SounaireSliceCache {
	uint32_t version = UINT32_MAX;
	sounaire::Slices slices;
	const sounaire::Slices &get(SOUNAIRE *m) {
		uint32_t v = m->sliceVersion.load();
		if (v != version) {
			slices = m->getSlices();
			version = v;
		}
		return slices;
	}
};

// The waveform: markers, loop, playheads; Shift snaps a dragged marker to a zero
// crossing, Ctrl+Shift+drag picks the region the root note is found in,
// Alt+click previews the sample from there. The wheel zooms around the mouse,
// Shift+wheel scrolls. In SLICES mode: the cuts; a click selects a slice, a cut
// drags (Shift: zero crossing), a double-click adds a cut there or removes the
// one under the mouse.
struct SounaireDisplay : OpaqueWidget {
	SOUNAIRE *module = nullptr;
	int dragMarker = -1;          // START, END, LOOP_START, LOOP_END param
	int dragCut = -1;             // SLICES mode: the cut being dragged
	sounaire::Slices dragBefore;  // the slices when that drag began (undo)
	float pressX = 0.f;
	bool selecting = false;
	float selFrom = 0.f, selTo = 0.f;
	float dragX = 0.f;
	double v0 = 0.0, v1 = 1.0;    // the part of the sample shown (0..1)

	float xToPos(float x) const {
		return (float)(v0 + clamp(x / box.size.x, 0.f, 1.f) * (v1 - v0));
	}
	float posToX(double pos) const {
		return (float)((pos - v0) / (v1 - v0) * box.size.x);
	}

	int markerAt(float x) const {
		if (!module)
			return -1;
		int ids[4] = {SOUNAIRE::START_PARAM, SOUNAIRE::END_PARAM, SOUNAIRE::LOOP_START_PARAM, SOUNAIRE::LOOP_END_PARAM};
		int best = -1;
		float bestD = 6.f;
		for (int id : ids) {
			if ((id == SOUNAIRE::LOOP_START_PARAM || id == SOUNAIRE::LOOP_END_PARAM) && module->params[SOUNAIRE::LOOP_MODE_PARAM].getValue() < 0.5f)
				continue;
			float d = std::fabs(posToX(module->params[id].getValue()) - x);
			if (d < bestD) {
				bestD = d;
				best = id;
			}
		}
		return best;
	}

	void setMarker(float x, bool snap) {
		if (!module || dragMarker < 0)
			return;
		float pos = xToPos(x);
		if (snap) {
			std::shared_ptr<sounaire::Sample> s = module->displaySample();
			if (s && s->frames > 1)
				pos = (float)((double)s->zeroCrossing((int64_t)(pos * s->frames)) / s->frames);
		}
		module->paramQuantities[dragMarker]->setValue(pos);
	}

	// SLICES mode: frame under x, the cut near x (within 5 px), -1 if none
	int64_t xToFrame(float x, bool snap) const {
		std::shared_ptr<sounaire::Sample> s = module->displaySample();
		if (!s || s->frames < 2)
			return 0;
		int64_t f = (int64_t)((double)xToPos(x) * s->frames);
		return snap ? s->zeroCrossing(f) : f;
	}

	int cutAt(float x) const {
		std::shared_ptr<sounaire::Sample> s = module->displaySample();
		if (!s || s->frames < 2)
			return -1;
		sounaire::Slices u = module->getSlices();
		int best = -1;
		float bestD = 5.f;
		for (int i = 1; i < u.count; i++) {
			float d = std::fabs(posToX((double)u.start[i] / s->frames) - x);
			if (d < bestD) {
				bestD = d;
				best = i;
			}
		}
		return best;
	}

	void onHoverScroll(const event::HoverScroll &e) override {
		if (!module)
			return;
		e.consume(this);
		double span = v1 - v0;
		int mods = APP->window->getMods() & RACK_MOD_MASK;
		float dy = e.scrollDelta.y, dx = e.scrollDelta.x;
		if ((mods & GLFW_MOD_SHIFT) || dx != 0.f) {
			// scroll: a tenth of the view per notch
			double d = -(dx != 0.f ? dx : dy) / 50.0 * span * 0.1;
			d = std::max(-v0, std::min(1.0 - v1, d));
			v0 += d;
			v1 += d;
			return;
		}
		// zoom around the mouse, down to about 64 frames across
		std::shared_ptr<sounaire::Sample> s = module->displaySample();
		double minSpan = s && s->frames > 64 ? 64.0 / s->frames : 0.01;
		double at = v0 + clamp(e.pos.x / box.size.x, 0.f, 1.f) * span;
		double k = dy > 0 ? 1.0 / 1.25 : 1.25;
		double ns = clamp(span * k, minSpan, 1.0);
		double t = (at - v0) / span;
		v0 = at - t * ns;
		v1 = v0 + ns;
		if (v0 < 0.0) {
			v1 -= v0;
			v0 = 0.0;
		}
		if (v1 > 1.0) {
			v0 -= v1 - 1.0;
			v1 = 1.0;
		}
		v0 = std::max(0.0, v0);
	}

	void onDoubleClick(const event::DoubleClick &e) override {
		if (!module || !module->sliceMode())
			return;
		e.consume(this);
		int cut = cutAt(pressX);
		bool snap = (APP->window->getMods() & RACK_MOD_MASK) & GLFW_MOD_SHIFT;
		if (cut > 0)
			sliceEdit(module, "remove a sOUNAiRe cut", [&]() { module->removeSlice(cut); });
		else
			sliceEdit(module, "add a sOUNAiRe cut", [&]() { module->addSlice(xToFrame(pressX, snap)); });
	}

	void onButton(const event::Button &e) override {
		if (!module || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		int mods = e.mods & RACK_MOD_MASK;
		if (e.action == GLFW_PRESS) {
			e.consume(this);
			dragX = e.pos.x;
			pressX = e.pos.x;
			if (mods & GLFW_MOD_ALT) {
				module->previewPos = xToPos(e.pos.x);
				module->previewHeld = true;
				return;
			}
			if ((mods & RACK_MOD_CTRL) && (mods & GLFW_MOD_SHIFT)) {
				selecting = true;
				selFrom = selTo = xToPos(e.pos.x);
				return;
			}
			if (module->sliceMode()) {
				dragCut = cutAt(e.pos.x);
				if (dragCut > 0)
					dragBefore = module->getSlices();
				else
					module->selectedSlice = module->getSlices().at(xToFrame(e.pos.x, false));
				return;
			}
			dragMarker = markerAt(e.pos.x);
			if (dragMarker < 0) {
				// a click away from the markers moves the nearest of start and end
				float s = posToX(module->params[SOUNAIRE::START_PARAM].getValue());
				float en = posToX(module->params[SOUNAIRE::END_PARAM].getValue());
				dragMarker = std::fabs(e.pos.x - s) < std::fabs(e.pos.x - en) ? SOUNAIRE::START_PARAM : SOUNAIRE::END_PARAM;
			}
			setMarker(e.pos.x, mods & GLFW_MOD_SHIFT);
		}
		else if (e.action == GLFW_RELEASE) {
			endDrag();
		}
	}

	void endDrag() {
		module->previewHeld = false;
		if (selecting) {
			selecting = false;
			if (std::fabs(selTo - selFrom) > 0.002f)
				module->requestRootAnalysis(selFrom, selTo);
		}
		if (dragCut > 0) {
			// the whole drag as one undo step
			sounaire::Slices after = module->getSlices();
			if (!sameSlices(dragBefore, after)) {
				SounaireSliceAction *a = new SounaireSliceAction;
				a->moduleId = module->id;
				a->name = "move a sOUNAiRe cut";
				a->before = dragBefore;
				a->after = after;
				APP->history->push(a);
			}
		}
		dragMarker = -1;
		dragCut = -1;
	}

	void onDragMove(const event::DragMove &e) override {
		if (!module)
			return;
		dragX += e.mouseDelta.x / getAbsoluteZoom();
		int mods = APP->window->getMods() & RACK_MOD_MASK;
		if (selecting)
			selTo = xToPos(dragX);
		else if (dragCut > 0)
			module->moveSlice(dragCut, xToFrame(dragX, mods & GLFW_MOD_SHIFT));
		else if (dragMarker >= 0)
			setMarker(dragX, mods & GLFW_MOD_SHIFT);
	}

	void onDragEnd(const event::DragEnd &e) override {
		if (module)
			endDrag();
	}

	// ---- drawing. The waveform, regions, markers and cuts change only with the
	// sample, the view or an edit: drawn into a framebuffer then. The playheads
	// and the text lines go on top of it every frame.
	struct Wave : Widget {
		SounaireDisplay *d = nullptr;
		void draw(const DrawArgs &args) override {
			d->drawWave(args);
		}
	};
	widget::FramebufferWidget *fb = nullptr;
	SounaireSliceCache sliceCache;
	double waveKey[16] = {};

	void step() override {
		if (!fb) {
			fb = new widget::FramebufferWidget;
			Wave *w = new Wave;
			w->d = this;
			fb->addChild(w);
			addChild(fb);
		}
		fb->box.pos = Vec(0, 0);
		fb->box.size = box.size;
		fb->children.front()->box.size = box.size;
		if (module) {
			// what the framebuffer shows: drawn again when any of it moves
			std::shared_ptr<sounaire::Sample> s = module->displaySample();
			double k[16] = {(double)(uintptr_t)s.get(), v0, v1,
			                module->params[SOUNAIRE::START_PARAM].getValue(), module->params[SOUNAIRE::END_PARAM].getValue(),
			                module->params[SOUNAIRE::LOOP_START_PARAM].getValue(), module->params[SOUNAIRE::LOOP_END_PARAM].getValue(),
			                module->params[SOUNAIRE::LOOP_MODE_PARAM].getValue() > 0.5f ? 1.0 : 0.0, module->sliceMode() ? 1.0 : 0.0,
			                (double)module->sliceVersion.load(), (double)module->selectedSlice.load(), module->autoNormalize ? 1.0 : 0.0,
			                box.size.x, box.size.y, 0.0, 0.0};
			if (std::memcmp(k, waveKey, sizeof(k)) != 0) {
				std::memcpy(waveKey, k, sizeof(k));
				fb->setDirty();
			}
		}
		OpaqueWidget::step();
	}

	// layer 0: nothing (the framebuffer is drawn on the light layer, with the rest)
	void draw(const DrawArgs &args) override {
	}

	// the static part, into the framebuffer
	void drawWave(const DrawArgs &args) {
		using namespace sounaire_ui;
		if (!module)
			return;
		std::shared_ptr<sounaire::Sample> s = module->displaySample();
		if (!s || s->ovMin.empty() || s->frames <= 1)
			return;
		std::shared_ptr<window::Font> f = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		float W = box.size.x, H = box.size.y;
		float B = H - 13.f;
		float mid = (14.f + B) * 0.5f, amp = (B - 14.f) * 0.46f;
		bool slicing = module->sliceMode();
		const sounaire::Slices &cuts = sliceCache.get(module);
		int sel = clamp((int)module->selectedSlice, 0, cuts.count - 1);
		double F = (double)s->frames;
		float st = module->params[SOUNAIRE::START_PARAM].getValue(), en = module->params[SOUNAIRE::END_PARAM].getValue();
		float a = posToX(std::min(st, en)), b = posToX(std::max(st, en));
		if (slicing) {
			// the whole sample plays; the selected slice is shaded
			a = -1.f;
			b = W + 1.f;
			float sx0 = posToX(cuts.start[sel] / F), sx1 = posToX(cuts.end(sel, s->frames) / F);
			nvgBeginPath(args.vg);
			nvgRect(args.vg, sx0, 14, sx1 - sx0, B - 14);
			nvgFillColor(args.vg, nvgRGBA(0xF0, 0x62, 0x2F, 0x38));
			nvgFill(args.vg);
		}
		// loop region
		bool looping = !slicing && module->params[SOUNAIRE::LOOP_MODE_PARAM].getValue() > 0.5f;
		if (looping) {
			float ls = module->params[SOUNAIRE::LOOP_START_PARAM].getValue(), le = module->params[SOUNAIRE::LOOP_END_PARAM].getValue();
			float x0 = posToX(std::min(ls, le)), x1 = posToX(std::max(ls, le));
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x0, 14, x1 - x0, B - 14);
			nvgFillColor(args.vg, nvgRGBA(0x2F, 0x55, 0xD4, 0x40));
			nvgFill(args.vg);
		}
		// waveform, normalized as it plays: from the overview when a pixel
		// covers more than an overview bin, from the frames when zoomed in; one
		// path for the part that plays, one for the rest
		float norm = (module->autoNormalize && s->peak > 1e-6f) ? 0.891f / s->peak : 1.f;
		int n = (int)s->ovMin.size();
		double bin = F / n;
		int cols = std::max(0, (int)W);
		std::vector<float> los((size_t)cols), his((size_t)cols);
		for (int i = 0; i < cols; i++) {
			double f0 = (v0 + (v1 - v0) * i / W) * F, f1 = (v0 + (v1 - v0) * (i + 1) / W) * F;
			float lo = 1.f, hi = -1.f;
			if (f1 - f0 >= bin) {
				int k0 = (int)(f0 / bin), k1 = std::max(k0 + 1, (int)(f1 / bin));
				for (int k = k0; k < k1 && k < n; k++) {
					lo = std::min(lo, s->ovMin[k]);
					hi = std::max(hi, s->ovMax[k]);
				}
			}
			else {
				int64_t j0 = (int64_t)f0, j1 = std::max(j0 + 1, (int64_t)std::ceil(f1));
				for (int64_t j = j0; j <= j1 && j < s->frames; j++) {
					float x = s->mono(j);
					lo = std::min(lo, x);
					hi = std::max(hi, x);
				}
			}
			los[(size_t)i] = lo;
			his[(size_t)i] = hi;
		}
		for (int pass = 0; pass < 2; pass++) {
			nvgBeginPath(args.vg);
			for (int i = 0; i < cols; i++) {
				float lo = los[(size_t)i], hi = his[(size_t)i];
				if (hi < lo || (i >= a && i <= b) != (pass == 0))
					continue;
				lo = clamp(lo * norm, -1.f, 1.f);
				hi = clamp(hi * norm, -1.f, 1.f);
				nvgRect(args.vg, i, mid - hi * amp, 1.f, std::max(1.f, (hi - lo) * amp));
			}
			nvgFillColor(args.vg, pass == 0 ? BLUE : nvgRGBA(0x2F, 0x55, 0xD4, 0x55));
			nvgFill(args.vg);
		}
		// SLICES mode: the cuts, each slice numbered
		if (slicing) {
			nvgBeginPath(args.vg);
			for (int i = 0; i < cuts.count; i++) {
				float x = posToX(cuts.start[i] / F);
				float next = posToX(cuts.end(i, s->frames) / F);
				if (next < 0.f || x > W || i == sel)
					continue;
				nvgMoveTo(args.vg, x, 14);
				nvgLineTo(args.vg, x, B);
			}
			nvgStrokeColor(args.vg, PAPER);
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);
			float xs = posToX(cuts.start[sel] / F);
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, xs, 14);
			nvgLineTo(args.vg, xs, B);
			nvgStrokeColor(args.vg, ORANGE);
			nvgStroke(args.vg);
			if (f && f->handle >= 0) {
				nvgFontFaceId(args.vg, f->handle);
				nvgFontSize(args.vg, 8.f);
				nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
				for (int i = 0; i < cuts.count; i++) {
					float x = posToX(cuts.start[i] / F);
					float next = posToX(cuts.end(i, s->frames) / F);
					if (next < 0.f || x > W || next - x <= 9.f)
						continue;
					nvgFillColor(args.vg, i == sel ? ORANGE : PAPER);
					nvgText(args.vg, std::max(x, 0.f) + 2, 15, std::to_string(i + 1).c_str(), nullptr);
				}
			}
		}
		else {
			// start and end
			nvgBeginPath(args.vg);
			for (int id : {SOUNAIRE::START_PARAM, SOUNAIRE::END_PARAM}) {
				float x = posToX(module->params[id].getValue());
				nvgMoveTo(args.vg, x, 14);
				nvgLineTo(args.vg, x, B);
			}
			nvgStrokeColor(args.vg, PAPER);
			nvgStrokeWidth(args.vg, 1.f);
			nvgStroke(args.vg);
			nvgBeginPath(args.vg);
			for (int id : {SOUNAIRE::START_PARAM, SOUNAIRE::END_PARAM}) {
				float x = posToX(module->params[id].getValue());
				float dir = id == SOUNAIRE::START_PARAM ? 1.f : -1.f;
				nvgMoveTo(args.vg, x, 14);
				nvgLineTo(args.vg, x + 5 * dir, 14);
				nvgLineTo(args.vg, x, 20);
				nvgClosePath(args.vg);
			}
			nvgFillColor(args.vg, PAPER);
			nvgFill(args.vg);
			if (looping) {
				nvgBeginPath(args.vg);
				for (int id : {SOUNAIRE::LOOP_START_PARAM, SOUNAIRE::LOOP_END_PARAM}) {
					float x = posToX(module->params[id].getValue());
					nvgMoveTo(args.vg, x, 14);
					nvgLineTo(args.vg, x, B);
				}
				nvgStrokeColor(args.vg, ORANGE);
				nvgStrokeWidth(args.vg, 1.f);
				nvgStroke(args.vg);
			}
		}
		// zoomed: where the view is in the sample
		if (v1 - v0 < 0.999) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, (float)v0 * W, B - 2, std::max(2.f, (float)(v1 - v0) * W), 2);
			nvgFillColor(args.vg, ORANGE);
			nvgFill(args.vg);
		}
	}

	void drawLayer(const DrawArgs &args, int layer) override {
		using namespace sounaire_ui;
		if (layer != 1) {
			OpaqueWidget::drawLayer(args, layer);
			return;
		}
		float W = box.size.x, H = box.size.y;
		nvgBeginPath(args.vg);
		nvgRoundedRect(args.vg, 0, 0, W, H, 3);
		nvgFillColor(args.vg, INK);
		nvgFill(args.vg);
		if (!module || !fb) {
			OpaqueWidget::drawLayer(args, layer);
			return;
		}
		std::shared_ptr<sounaire::Sample> s = module->displaySample();
		std::shared_ptr<window::Font> f = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		nvgSave(args.vg);
		nvgScissor(args.vg, 0, 0, W, H);
		drawChild(fb, args);
		// the sample on the top line, the playing information on a line under
		// the waveform, so a long name never runs into it
		float B = H - 13.f;
		bool slicing = module->sliceMode();
		const sounaire::Slices &cuts = sliceCache.get(module);
		int sel = clamp((int)module->selectedSlice, 0, cuts.count - 1);
		if (s && s->frames > 1) {
			// where the voices are (a voice may play a copy at the engine's rate:
			// its position goes through its own length)
			for (int i = 0; i < sounaire::Engine::SLOTS; i++) {
				const sounaire::Voice &v = module->engine.voices[i];
				if (!v.active || v.origin != s.get() || v.frames <= 1)
					continue;
				double pos = v.head.v;
				bool reversed = v.ownRegion ? v.region.reverse : module->params[SOUNAIRE::REVERSE_PARAM].getValue() > 0.5f;
				if (reversed && i != sounaire::Engine::PREVIEW)
					pos = v.frames - 1 - pos;
				float x = posToX(pos / v.frames);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x, 14, 1.f, B - 14);
				nvgFillColor(args.vg, nvgRGBA(0xF2, 0xEE, 0xE4, (unsigned char)(80 + 175 * clamp(v.env, 0.f, 1.f))));
				nvgFill(args.vg);
			}
		}
		if (selecting) {
			float x0 = posToX(std::min(selFrom, selTo)), x1 = posToX(std::max(selFrom, selTo));
			nvgBeginPath(args.vg);
			nvgRect(args.vg, x0, 14, x1 - x0, B - 14);
			nvgFillColor(args.vg, nvgRGBA(0xF0, 0x62, 0x2F, 0x60));
			nvgFill(args.vg);
		}
		// header line: sample, rate, length; under the waveform: what plays
		if (f && f->handle >= 0) {
			nvgFontFaceId(args.vg, f->handle);
			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, PAPER);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			std::string line;
			float rec = module->recSeconds;
			std::string error = module->getLastError();
			if (rec > 0.f)
				line = rack::string::f("RECORDING  %.1f s", rec);
			else if (module->loading)
				line = "LOADING...";
			else if (!error.empty())
				line = error;
			else if (s)
				line = rack::string::f("%s  %.1f kHz  %.2f s", s->name.c_str(), s->rate / 1000.f, s->frames / s->rate);
			else
				line = "DROP A WAV, AIFF OR FLAC HERE";
			if (rec > 0.f)
				nvgFillColor(args.vg, ORANGE);
			nvgText(args.vg, 4, 3, line.c_str(), nullptr);
			nvgFillColor(args.vg, PAPER);
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
			std::string root;
			if (slicing) {
				static const char *types[] = {"ONE SHOT", "GATE", "LOOP", "PING-PONG", "REVERSE", "REV LOOP"};
				int base = (int)module->params[SOUNAIRE::ROOT_PARAM].getValue();
				int play = (int)module->params[SOUNAIRE::PLAY_PARAM].getValue();
				root = rack::string::f("SLICE %d/%d %s  KEY %s", sel + 1, cuts.count, types[clamp((int)cuts.type[sel], 0, 5)],
				                 sounaire_ui::noteName(base + sel).c_str());
				if (cuts.choke[sel] > 0)
					root += rack::string::f("  CHOKE %d", cuts.choke[sel]);
				if (play > 0)
					root += rack::string::f("  PLAY: %s", types[clamp(play - 1, 0, 5)]);
			}
			else {
				root = "ROOT " + sounaire_ui::noteName(module->params[SOUNAIRE::ROOT_PARAM].getValue());
				if (std::fabs(module->rootCents) >= 0.5f)
					root += rack::string::f(" %+.0fc", module->rootCents);
			}
			nvgText(args.vg, W - 4, H - 2, root.c_str(), nullptr);
			float bpm = module->syncBpm;
			if (bpm > 0.f) {
				nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM);
				nvgText(args.vg, 4, H - 2, rack::string::f("SYNC %.1f BPM  %d BEATS", bpm, (int)module->syncBeats).c_str(), nullptr);
			}
		}
		nvgResetScissor(args.vg);
		nvgRestore(args.vg);
		OpaqueWidget::drawLayer(args, layer);
	}
};

// Under the display. KEYS mode: a piano of three octaves around the playing
// range (or the root when the range is wider), with octave names; the range in
// blue, the root in orange, the keys being played in paper; the wheel shifts
// it by an octave. SLICES mode: one pad per slice, lined up with its part of
// the waveform (and the display's zoom), with its number and key; a pad lights
// while its slice plays. A click plays the key or the pad's slice while held;
// dragging moves to the keys or pads passed over.
struct SounaireKeys : OpaqueWidget {
	SOUNAIRE *module = nullptr;
	SounaireDisplay *display = nullptr;
	SounaireSliceCache sliceCache;
	float dragX = 0.f, dragY = 0.f;
	int clicked = -1;
	int octaveShift = 0;

	static bool isBlack(int k) {
		int pc = ((k % 12) + 12) % 12;
		return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
	}

	// KEYS mode: the first key shown (a C), for 36 keys
	int firstKey() const {
		int lo = (int)module->params[SOUNAIRE::LOW_PARAM].getValue(), hi = (int)module->params[SOUNAIRE::HIGH_PARAM].getValue();
		int a = std::min(lo, hi), b = std::max(lo, hi);
		int root = (int)module->params[SOUNAIRE::ROOT_PARAM].getValue();
		int from = b - a < 36 ? (a / 12) * 12 : ((root - 12) / 12) * 12;
		if (b - a < 36 && b >= from + 36)
			from += 12;
		return clamp(from + 12 * octaveShift, 0, 128 - 36);
	}

	// KEYS mode: the key under (x, y): a black key on the top 60%, else the white
	int pianoKeyAt(float x, float y) const {
		int from = firstKey();
		float ww = box.size.x / 21.f;   // 21 white keys in three octaves
		int wi = clamp((int)(x / ww), 0, 20);
		int white = from;
		for (int n = 0, k = from; k < from + 36; k++)
			if (!isBlack(k) && n++ == wi) {
				white = k;
				break;
			}
		if (y < box.size.y * 0.6f) {
			float fx = x / ww - wi;
			if (fx < 0.3f && white - 1 >= from && isBlack(white - 1))
				return white - 1;
			if (fx > 0.7f && white + 1 < from + 36 && isBlack(white + 1))
				return white + 1;
		}
		return white;
	}

	// SLICES mode: the slice under x, in the display's view
	int sliceAt(float x) const {
		std::shared_ptr<sounaire::Sample> s = module->displaySample();
		if (!s || s->frames < 2 || !display)
			return -1;
		return module->getSlices().at((int64_t)((double)display->xToPos(x) * s->frames));
	}

	int keyAt(float x, float y) const {
		if (module->sliceMode()) {
			int i = sliceAt(x);
			return i < 0 ? -1 : (int)module->params[SOUNAIRE::ROOT_PARAM].getValue() + i;
		}
		return pianoKeyAt(x, y);
	}

	void onHoverScroll(const event::HoverScroll &e) override {
		if (!module || module->sliceMode())
			return;
		e.consume(this);
		octaveShift += e.scrollDelta.y > 0 ? 1 : -1;
		octaveShift = clamp(octaveShift, -10, 10);
	}

	void onButton(const event::Button &e) override {
		if (!module || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		if (e.action == GLFW_PRESS) {
			e.consume(this);
			dragX = e.pos.x;
			dragY = e.pos.y;
			clicked = keyAt(e.pos.x, e.pos.y);
			if (clicked < 0)
				return;
			if (module->sliceMode())
				module->selectedSlice = clicked - (int)module->params[SOUNAIRE::ROOT_PARAM].getValue();
			module->mouseHeld = true;
			module->mouseKey = clicked;
		}
		else if (e.action == GLFW_RELEASE) {
			module->mouseHeld = false;
			clicked = -1;
		}
	}

	void onDragMove(const event::DragMove &e) override {
		if (!module || clicked < 0)
			return;
		dragX += e.mouseDelta.x / getAbsoluteZoom();
		dragY += e.mouseDelta.y / getAbsoluteZoom();
		int k = keyAt(dragX, dragY);
		if (k >= 0 && k != clicked) {
			clicked = k;
			module->mouseKey = k;
		}
	}

	void onDragEnd(const event::DragEnd &e) override {
		if (module)
			module->mouseHeld = false;
		clicked = -1;
	}

	bool keyPlayed(int k) const {
		for (int c = 0; c < MAX_VOICES; c++)
			if (module->keyHeld[c] == k)
				return true;
		return module->mouseShown == k;
	}

	void drawPiano(const DrawArgs &args, std::shared_ptr<window::Font> f) {
		using namespace sounaire_ui;
		float W = box.size.x, H = box.size.y, ww = W / 21.f;
		int from = firstKey();
		int lo = (int)module->params[SOUNAIRE::LOW_PARAM].getValue(), hi = (int)module->params[SOUNAIRE::HIGH_PARAM].getValue();
		int a = std::min(lo, hi), b = std::max(lo, hi);
		int root = (int)module->params[SOUNAIRE::ROOT_PARAM].getValue();
		int wi = 0;
		for (int k = from; k < from + 36; k++) {
			if (isBlack(k))
				continue;
			bool in = k >= a && k <= b, played = keyPlayed(k);
			NVGcolor c = played ? PAPER : (k == root ? ORANGE : (in ? BLUE : TRACK));
			nvgBeginPath(args.vg);
			nvgRect(args.vg, wi * ww + 0.5f, 0, ww - 1.f, H);
			nvgFillColor(args.vg, c);
			nvgFill(args.vg);
			if (played) {
				nvgBeginPath(args.vg);
				nvgRect(args.vg, wi * ww + 0.5f, H - 3, ww - 1.f, 3);
				nvgFillColor(args.vg, INK);
				nvgFill(args.vg);
			}
			if (k % 12 == 0 && f && f->handle >= 0) {
				nvgFontFaceId(args.vg, f->handle);
				nvgFontSize(args.vg, 8.f);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
				nvgFillColor(args.vg, (in && !played && k != root) ? PAPER : INK);
				nvgText(args.vg, wi * ww + ww / 2, H - 3, sounaire_ui::noteName((float)k).c_str(), nullptr);
			}
			wi++;
		}
		wi = 0;
		for (int k = from; k < from + 36; k++) {
			if (!isBlack(k)) {
				wi++;
				continue;
			}
			bool in = k >= a && k <= b, played = keyPlayed(k);
			NVGcolor c = played ? PAPER : (k == root ? ORANGE : (in ? nvgRGB(0x1F, 0x3F, 0xAF) : INK));
			nvgBeginPath(args.vg);
			nvgRect(args.vg, wi * ww - ww * 0.3f, 0, ww * 0.6f, H * 0.6f);
			nvgFillColor(args.vg, c);
			nvgFill(args.vg);
			nvgStrokeColor(args.vg, INK);
			nvgStrokeWidth(args.vg, 0.6f);
			nvgStroke(args.vg);
		}
	}

	void drawPads(const DrawArgs &args, std::shared_ptr<window::Font> f) {
		using namespace sounaire_ui;
		std::shared_ptr<sounaire::Sample> s = module->displaySample();
		float W = box.size.x, H = box.size.y;
		if (!s || s->frames < 2 || !display) {
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 0, 0, W, H, 3);
			nvgFillColor(args.vg, TRACK);
			nvgFill(args.vg);
			return;
		}
		const sounaire::Slices &u = sliceCache.get(module);
		int sel = clamp((int)module->selectedSlice, 0, u.count - 1);
		int base = (int)module->params[SOUNAIRE::ROOT_PARAM].getValue();
		// how loud each slice sounds now
		float lit[sounaire::MAX_SLICES] = {};
		for (int i = 0; i < sounaire::Engine::SLOTS; i++) {
			const sounaire::Voice &v = module->engine.voices[i];
			if (v.active && v.slice >= 0 && v.slice < u.count && v.origin == s.get())
				lit[v.slice] = std::max(lit[v.slice], v.env * v.fadeGain);
		}
		double F = (double)s->frames;
		nvgSave(args.vg);
		nvgScissor(args.vg, 0, 0, W, H);
		for (int i = 0; i < u.count; i++) {
			float x0 = display->posToX(u.start[i] / F) + 0.5f, x1 = display->posToX(u.end(i, s->frames) / F) - 0.5f;
			if (x1 < 0.f || x0 > W || x1 - x0 < 0.5f)
				continue;
			NVGcolor c = i == sel ? ORANGE : BLUE;
			if (lit[i] > 0.f)
				c = nvgLerpRGBA(c, PAPER, std::min(1.f, 0.3f + lit[i]));
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, x0, 0, x1 - x0, H, 2);
			nvgFillColor(args.vg, c);
			nvgFill(args.vg);
			if (lit[i] > 0.f) {
				nvgStrokeColor(args.vg, INK);
				nvgStrokeWidth(args.vg, 1.f);
				nvgStroke(args.vg);
			}
			if (f && f->handle >= 0 && x1 - x0 > 12.f) {
				float cx = (std::max(x0, 0.f) + std::min(x1, W)) / 2;
				nvgFontFaceId(args.vg, f->handle);
				nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
				nvgFillColor(args.vg, lit[i] > 0.f ? INK : PAPER);
				nvgFontSize(args.vg, 10.f);
				bool both = x1 - x0 > 30.f;
				nvgText(args.vg, cx, both ? H * 0.32f : H * 0.5f, std::to_string(i + 1).c_str(), nullptr);
				if (both) {
					nvgFontSize(args.vg, 8.f);
					nvgText(args.vg, cx, H * 0.72f, sounaire_ui::noteName((float)(base + i)).c_str(), nullptr);
				}
			}
		}
		nvgResetScissor(args.vg);
		nvgRestore(args.vg);
	}

	void drawLayer(const DrawArgs &args, int layer) override {
		if (layer == 1 && module) {
			std::shared_ptr<window::Font> f = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
			if (module->sliceMode())
				drawPads(args, f);
			else
				drawPiano(args, f);
		}
		OpaqueWidget::drawLayer(args, layer);
	}
};

// a slice's level, tune, pan, attack or release, as a menu slider
struct SliceSettingQuantity : Quantity {
	SOUNAIRE *m = nullptr;
	int slice = 0, which = 0;
	float lo = 0.f, hi = 1.f, def = 0.f;
	std::string label, unit;
	void setValue(float v) override {
		m->setSliceSetting(slice, which, clamp(v, lo, hi));
	}
	float getValue() override {
		return m->sliceSetting(slice, which);
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
	std::string getUnit() override {
		return unit;
	}
	std::string getDisplayValueString() override {
		float v = getValue();
		if ((which == SOUNAIRE::SET_ATTACK || which == SOUNAIRE::SET_RELEASE) && v < 0.f)
			return "the knob";
		if (which == SOUNAIRE::SET_PAN)
			return std::fabs(v) < 0.005f ? "centre" : rack::string::f("%.0f%% %s", std::fabs(v) * 100.f, v < 0 ? "left" : "right");
		return rack::string::f(which == SOUNAIRE::SET_TUNE ? "%+.2f" : "%.1f", v);
	}
};

// the sensitivity of the transient detection, as a menu slider
struct SensitivityQuantity : Quantity {
	SOUNAIRE *m = nullptr;
	void setValue(float v) override {
		m->sensitivity = clamp(v, 0.f, 1.f);
	}
	float getValue() override {
		return m->sensitivity;
	}
	float getDefaultValue() override {
		return 0.64f;
	}
	std::string getLabel() override {
		return "Sensitivity";
	}
	std::string getDisplayValueString() override {
		return rack::string::f("%.0f%% (%.1f dB of rise)", m->sensitivity * 100.f, sounaire::riseForSensitivity(m->sensitivity));
	}
};

struct SOUNAIREWidget : BidooWidget {
	SOUNAIREWidget(SOUNAIRE *module) {
		using namespace sounaire_ui;
		setModule(module);
		themable = false; // the Riso panel has no other look
		prepareThemes(asset::plugin(pluginInstance, "res/SOUNAIRE.svg"));
		SounaireDisplay *display = new SounaireDisplay;
		display->module = module;
		display->box.pos = Vec(10, 24);
		display->box.size = Vec(460, 76);
		addChild(display);
		SounaireKeys *keys = new SounaireKeys;
		keys->module = module;
		keys->display = display;
		keys->box.pos = Vec(10, 104);
		keys->box.size = Vec(460, 28);
		addChild(keys);

		// three rows of eight columns
		auto cx = [](int col) { return 40.f + 57.f * col; };
		const float rows[3] = {164.f, 212.f, 260.f};
		auto knob = [&](int col, int row, int param, bool snapped) {
			app::Knob *k = snapped ? (app::Knob *)createParamCentered<RisoSnapKnob>(Vec(cx(col), rows[row]), module, param)
			                       : (app::Knob *)createParamCentered<RisoKnob>(Vec(cx(col), rows[row]), module, param);
			addParam(k);
		};
		knob(0, 0, SOUNAIRE::START_PARAM, false);
		knob(1, 0, SOUNAIRE::END_PARAM, false);
		knob(2, 0, SOUNAIRE::LOOP_START_PARAM, false);
		knob(3, 0, SOUNAIRE::LOOP_END_PARAM, false);
		knob(4, 0, SOUNAIRE::XFADE_PARAM, false);
		knob(5, 0, SOUNAIRE::LOOP_MODE_PARAM, true);
		// REVERSE and LEGATO do nothing in SLICES mode: faded there
		for (int i = 0; i < 2; i++) {
			RisoButton *b = createParamCentered<RisoButton>(Vec(cx(6 + i), rows[0]), module, i == 0 ? SOUNAIRE::REVERSE_PARAM : SOUNAIRE::LEGATO_PARAM);
			b->dimmed = [=]() { return module && module->sliceMode(); };
			addParam(b);
		}

		knob(0, 1, SOUNAIRE::ROOT_PARAM, true);
		knob(1, 1, SOUNAIRE::LOW_PARAM, true);
		knob(2, 1, SOUNAIRE::HIGH_PARAM, true);
		knob(3, 1, SOUNAIRE::TUNE_PARAM, true);
		knob(4, 1, SOUNAIRE::FINE_PARAM, false);
		knob(5, 1, SOUNAIRE::SPEED_PARAM, false);
		knob(6, 1, SOUNAIRE::TIME_MODE_PARAM, true);
		knob(7, 1, SOUNAIRE::LEVEL_PARAM, false);

		knob(0, 2, SOUNAIRE::FILTER_MODE_PARAM, true);
		knob(1, 2, SOUNAIRE::SLOPE_PARAM, true);
		knob(2, 2, SOUNAIRE::CUTOFF_PARAM, false);
		knob(3, 2, SOUNAIRE::RESONANCE_PARAM, false);
		knob(4, 2, SOUNAIRE::ATTACK_PARAM, false);
		knob(5, 2, SOUNAIRE::RELEASE_PARAM, false);
		knob(6, 2, SOUNAIRE::PLAY_PARAM, true);
		addParam(createParamCentered<RisoButton>(Vec(cx(7), rows[2]), module, SOUNAIRE::MODE_PARAM));

		// two rows of jacks (centres 24 + 38 i): playing, then slices, clock and
		// recording; the outputs on the ink block, one on each row
		auto jx = [](int i) { return 24.f + 38.f * i; };
		int row1[10] = {SOUNAIRE::VOCT_INPUT, SOUNAIRE::GATE_INPUT, SOUNAIRE::VEL_INPUT, SOUNAIRE::BEND_INPUT,
		                SOUNAIRE::START_INPUT, SOUNAIRE::SPEED_INPUT, SOUNAIRE::CUTOFF_INPUT, SOUNAIRE::RES_INPUT,
		                SOUNAIRE::PITCH_INPUT, SOUNAIRE::PLAY_INPUT};
		for (int i = 0; i < 10; i++)
			addInput(createInputCentered<RisoJack>(Vec(jx(i), 306.f), module, row1[i]));
		const int row2Col[6] = {0, 1, 2, 3, 5, 6};
		int row2[6] = {SOUNAIRE::TRIG_INPUT, SOUNAIRE::SLICE_INPUT, SOUNAIRE::RESET_INPUT, SOUNAIRE::CLOCK_INPUT,
		               SOUNAIRE::INL_INPUT, SOUNAIRE::INR_INPUT};
		for (int i = 0; i < 6; i++)
			addInput(createInputCentered<RisoJack>(Vec(jx(row2Col[i]), 348.f), module, row2[i]));
		addParam(createParamCentered<RisoButton>(Vec(jx(7), 348.f), module, SOUNAIRE::REC_PARAM));
		addInput(createInputCentered<RisoJack>(Vec(jx(8), 348.f), module, SOUNAIRE::REC_INPUT));
		RisoJack *l = createOutputCentered<RisoJack>(Vec(452.f, 306.f), module, SOUNAIRE::L_OUTPUT);
		RisoJack *r = createOutputCentered<RisoJack>(Vec(452.f, 348.f), module, SOUNAIRE::R_OUTPUT);
		l->onInk = r->onInk = true;
		addOutput(l);
		addOutput(r);
	}

	// the knobs that mean something else in SLICES mode say so in their tooltips
	void step() override {
		BidooWidget::step();
		SOUNAIRE *m = dynamic_cast<SOUNAIRE *>(module);
		if (!m)
			return;
		bool sl = m->sliceMode();
		m->paramQuantities[SOUNAIRE::ROOT_PARAM]->name = sl ? "Base key (slice 1)" : "Root note";
		m->paramQuantities[SOUNAIRE::START_PARAM]->name = sl ? "Start inside each slice" : "Start";
		m->paramQuantities[SOUNAIRE::END_PARAM]->name = sl ? "End inside each slice" : "End";
	}

	void onPathDrop(const PathDropEvent &e) override {
		SOUNAIRE *m = dynamic_cast<SOUNAIRE *>(module);
		if (m && !e.paths.empty())
			m->requestLoad(e.paths[0], false);
	}

	static ui::Slider *slider(Quantity *q) {
		ui::Slider *s = new ui::Slider;
		s->quantity = q;
		s->box.size.x = 220.f;
		return s;
	}

	void appendContextMenu(Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		SOUNAIRE *m = dynamic_cast<SOUNAIRE *>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuItem("Load sample...", "", [=]() {
			osdialog_filters *filters = osdialog_filters_parse("Audio:wav,WAV,aif,AIF,aiff,AIFF,flac,FLAC");
			char *path = osdialog_file(OSDIALOG_OPEN, nullptr, nullptr, filters);
			osdialog_filters_free(filters);
			if (path) {
				m->requestLoad(path, false);
				std::free(path);
			}
		}));
		menu->addChild(createMenuItem("Export the sample as WAV...", "", [=]() {
			osdialog_filters *filters = osdialog_filters_parse("WAV:wav");
			std::string name = m->getSampleName();
			std::string stem = rack::system::getStem(name.empty() ? std::string("sample") : name);
			char *path = osdialog_file(OSDIALOG_SAVE, nullptr, (stem + ".wav").c_str(), filters);
			osdialog_filters_free(filters);
			if (path) {
				std::string p = path;
				if (rack::system::getExtension(p).empty())
					p += ".wav";
				m->requestExport("", p);
				std::free(path);
			}
		}));
		menu->addChild(createMenuItem("Find the root note again (whole sample)", "", [=]() { m->requestRootAnalysis(0.0, 1.0); }));
		menu->addChild(createBoolPtrMenuItem("Normalize the sample", "", &m->autoNormalize));
		menu->addChild(createBoolPtrMenuItem("Find the root note when loading", "", &m->autoRoot));
		menu->addChild(createBoolPtrMenuItem("Polyphonic outputs (one channel per voice)", "", &m->polyOut));
		menu->addChild(createBoolPtrMenuItem("Equal-power loop crossfade (off: linear)", "", &m->xfadeEqualPower));
		menu->addChild(createIndexSubmenuItem("Pitch bend range", {"1 semitone", "2 semitones", "7 semitones", "12 semitones", "24 semitones"},
			[=]() { int r[] = {1, 2, 7, 12, 24}; for (int i = 0; i < 5; i++) if (r[i] == m->bendRange) return i; return 1; },
			[=](size_t i) { int r[] = {1, 2, 7, 12, 24}; m->bendRange = r[i]; }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Clock (CLOCK input)"));
		menu->addChild(createIndexSubmenuItem("Clock pulses per beat", {"1", "2", "4 (16th notes)", "12", "24 (MIDI clock)"},
			[=]() { int r[] = {1, 2, 4, 12, 24}; for (int i = 0; i < 5; i++) if (r[i] == m->clockPpqn) return i; return 0; },
			[=](size_t i) { int r[] = {1, 2, 4, 12, 24}; m->clockPpqn = r[i]; }));
		std::shared_ptr<sounaire::Sample> s = m->displaySample();
		int guess = s ? SOUNAIRE::guessBeats(s->frames / (double)s->rate) : 4;
		menu->addChild(createIndexSubmenuItem("The sample lasts", {rack::string::f("Guess (%d beats)", guess), "1 beat", "2 beats", "4 beats (1 bar)", "8 beats", "16 beats", "32 beats", "64 beats"},
			[=]() { if (m->loopBeats <= 0) return 0; int i = 1; for (int b = 1; b < m->loopBeats; b *= 2) i++; return i; },
			[=](size_t i) { m->loopBeats = i == 0 ? 0 : 1 << (i - 1); }));

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Slices"));
		static const std::vector<std::string> types = {"One shot", "Gate", "Loop", "Ping-pong", "Reverse", "Reverse loop"};
		menu->addChild(createIndexSubmenuItem("Find the transients of", {"Drums (attacks)", "Tonal material (spectral changes)"},
			[=]() { return (size_t)m->detector; }, [=](size_t i) { m->detector = (int)i; }));
		SensitivityQuantity *sq = new SensitivityQuantity;
		sq->m = m;
		menu->addChild(slider(sq));
		menu->addChild(createMenuItem("Find the transients again", "", [=]() {
			sliceEdit(m, "find the sOUNAiRe transients", [=]() { m->detectNow(); });
		}));
		menu->addChild(createSubmenuItem("Find about", "", [=](Menu *sub) {
			for (int n : {4, 8, 12, 16, 24, 32, 48, 64})
				sub->addChild(createMenuItem(rack::string::f("%d slices", n), "", [=]() {
					sliceEdit(m, "find sOUNAiRe slices", [=]() { m->detectNow(n); });
				}));
		}));
		menu->addChild(createBoolPtrMenuItem("Find the transients when loading", "", &m->detectOnLoad));
		menu->addChild(createSubmenuItem("Divide equally", "", [=](Menu *sub) {
			for (int n : {2, 4, 8, 16, 32, 64})
				sub->addChild(createMenuItem(rack::string::f("%d slices", n), "", [=]() {
					sliceEdit(m, "divide the sOUNAiRe sample", [=]() { m->divideSlices(n); });
				}));
		}));
		menu->addChild(createMenuItem("One slice (clear the cuts)", "", [=]() {
			sliceEdit(m, "clear the sOUNAiRe cuts", [=]() { m->setSlices({0}, m->getSlices().type[0]); });
		}));
		menu->addChild(createSubmenuItem("Every slice plays", "", [=](Menu *sub) {
			for (int i = 0; i < (int)types.size(); i++)
				sub->addChild(createMenuItem(types[i], "", [=]() {
					sliceEdit(m, "set the sOUNAiRe play types", [=]() { m->setAllSliceTypes(i); });
				}));
		}));
		menu->addChild(createMenuItem("Export the slices as WAV files...", "", [=]() {
			char *dir = osdialog_file(OSDIALOG_OPEN_DIR, nullptr, nullptr, nullptr);
			if (dir) {
				m->requestExport(dir, "");
				std::free(dir);
			}
		}));

		// the selected slice
		int sel = clamp((int)m->selectedSlice, 0, m->getSlices().count - 1);
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel(rack::string::f("Slice %d (click a slice in the display to pick another)", sel + 1)));
		menu->addChild(createIndexSubmenuItem("Plays", types,
			[=]() { return (size_t)m->getSlices().type[clamp(sel, 0, m->getSlices().count - 1)]; },
			[=](size_t i) { sliceEdit(m, "set a sOUNAiRe play type", [=]() { m->setSliceType(sel, (int)i); }); }));
		menu->addChild(createIndexSubmenuItem("Choke group", {"None", "1", "2", "3", "4"},
			[=]() { return (size_t)m->getSlices().choke[clamp(sel, 0, m->getSlices().count - 1)]; },
			[=](size_t i) { sliceEdit(m, "set a sOUNAiRe choke group", [=]() { m->setSliceChoke(sel, (int)i); }); }));
		struct Setting {
			int which;
			const char *label, *unit;
			float lo, hi, def;
		};
		const Setting settings[5] = {
			{SOUNAIRE::SET_LEVEL, "Level", " dB", -36.f, 12.f, 0.f},
			{SOUNAIRE::SET_TUNE, "Tune", " semitones", -24.f, 24.f, 0.f},
			{SOUNAIRE::SET_PAN, "Pan", "", -1.f, 1.f, 0.f},
			{SOUNAIRE::SET_ATTACK, "Attack", " ms", -1.f, 2000.f, -1.f},
			{SOUNAIRE::SET_RELEASE, "Release", " ms", -1.f, 5000.f, -1.f},
		};
		for (const Setting &st : settings) {
			SliceSettingQuantity *q = new SliceSettingQuantity;
			q->m = m;
			q->slice = sel;
			q->which = st.which;
			q->label = st.label;
			q->unit = st.unit;
			q->lo = st.lo;
			q->hi = st.hi;
			q->def = st.def;
			menu->addChild(slider(q));
		}

		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Drag a marker; Shift: snap to a zero crossing"));
		menu->addChild(createMenuLabel("Wheel: zoom; Shift+wheel: scroll; on the keyboard: octave"));
		menu->addChild(createMenuLabel("Ctrl+Shift+drag: region for the root note"));
		menu->addChild(createMenuLabel("Alt+click: preview from there"));
		menu->addChild(createMenuLabel("Slices: click selects, double-click adds or removes a cut (Ctrl+Z undoes)"));
	}
};
