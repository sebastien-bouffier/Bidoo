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

// latching button: ink, lit in the module's colour when on
struct RisoButton : app::Switch {
	RisoButton() {
		box.size = Vec(20, 20);
	}
	void draw(const DrawArgs &args) override {
		engine::ParamQuantity *pq = getParamQuantity();
		bool on = pq && pq->getValue() > 0.5f;
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

// The waveform: markers, loop, playheads; Shift snaps a dragged marker to a zero
// crossing, Ctrl+Shift+drag picks the region the root note is found in,
// Alt+click previews the sample from there. In SLICES mode: the cuts; a click
// selects a slice, a cut drags (Shift: zero crossing), a double-click adds a
// cut there or removes the one under the mouse.
struct SounaireDisplay : OpaqueWidget {
	SOUNAIRE *module = nullptr;
	int dragMarker = -1;          // START, END, LOOP_START, LOOP_END param
	int dragCut = -1;             // SLICES mode: the cut being dragged
	float pressX = 0.f;
	bool selecting = false;
	float selFrom = 0.f, selTo = 0.f;
	float dragX = 0.f;

	float xToPos(float x) const {
		return clamp(x / box.size.x, 0.f, 1.f);
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
			float d = std::fabs(module->params[id].getValue() * box.size.x - x);
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
		int64_t f = (int64_t)(xToPos(x) * s->frames);
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
			float d = std::fabs((float)u.start[i] / s->frames * box.size.x - x);
			if (d < bestD) {
				bestD = d;
				best = i;
			}
		}
		return best;
	}

	void onDoubleClick(const event::DoubleClick &e) override {
		if (!module || !module->sliceMode())
			return;
		e.consume(this);
		int cut = cutAt(pressX);
		if (cut > 0)
			module->removeSlice(cut);
		else
			module->addSlice(xToFrame(pressX, (APP->window->getMods() & RACK_MOD_MASK) & GLFW_MOD_SHIFT));
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
				if (dragCut < 0) {
					std::shared_ptr<sounaire::Sample> s = module->displaySample();
					if (s)
						module->selectedSlice = module->getSlices().at(xToFrame(e.pos.x, false));
				}
				return;
			}
			dragMarker = markerAt(e.pos.x);
			if (dragMarker < 0) {
				// a click away from the markers moves the nearest of start and end
				float s = module->params[SOUNAIRE::START_PARAM].getValue() * box.size.x;
				float en = module->params[SOUNAIRE::END_PARAM].getValue() * box.size.x;
				dragMarker = std::fabs(e.pos.x - s) < std::fabs(e.pos.x - en) ? SOUNAIRE::START_PARAM : SOUNAIRE::END_PARAM;
			}
			setMarker(e.pos.x, mods & GLFW_MOD_SHIFT);
		}
		else if (e.action == GLFW_RELEASE) {
			module->previewHeld = false;
			if (selecting) {
				selecting = false;
				if (std::fabs(selTo - selFrom) > 0.002f)
					module->requestRootAnalysis(selFrom, selTo);
			}
			dragMarker = -1;
			dragCut = -1;
		}
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
		if (!module)
			return;
		module->previewHeld = false;
		if (selecting) {
			selecting = false;
			if (std::fabs(selTo - selFrom) > 0.002f)
				module->requestRootAnalysis(selFrom, selTo);
		}
		dragMarker = -1;
		dragCut = -1;
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
		if (!module) {
			OpaqueWidget::drawLayer(args, layer);
			return;
		}
		std::shared_ptr<sounaire::Sample> s = module->displaySample();
		std::shared_ptr<window::Font> f = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		nvgSave(args.vg);
		nvgScissor(args.vg, 0, 0, W, H);
		// the sample on the top line, the playing information on a line under
		// the waveform, so a long name never runs into it
		float B = H - 13.f;
		float mid = (14.f + B) * 0.5f, amp = (B - 14.f) * 0.46f;
		bool slicing = module->sliceMode();
		sounaire::Slices cuts = module->getSlices();
		int sel = clamp((int)module->selectedSlice, 0, cuts.count - 1);
		if (s && !s->ovMin.empty()) {
			float st = module->params[SOUNAIRE::START_PARAM].getValue(), en = module->params[SOUNAIRE::END_PARAM].getValue();
			float a = std::min(st, en) * W, b = std::max(st, en) * W;
			if (slicing) {
				// the whole sample plays; the selected slice is shaded
				a = 0.f;
				b = W;
				float sx0 = (float)cuts.start[sel] / s->frames * W, sx1 = (float)cuts.end(sel, s->frames) / s->frames * W;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, sx0, 14, sx1 - sx0, B - 14);
				nvgFillColor(args.vg, nvgRGBA(0xF0, 0x62, 0x2F, 0x38));
				nvgFill(args.vg);
			}
			// loop region
			if (!slicing && module->params[SOUNAIRE::LOOP_MODE_PARAM].getValue() > 0.5f) {
				float ls = module->params[SOUNAIRE::LOOP_START_PARAM].getValue(), le = module->params[SOUNAIRE::LOOP_END_PARAM].getValue();
				nvgBeginPath(args.vg);
				nvgRect(args.vg, std::min(ls, le) * W, 14, std::fabs(le - ls) * W, B - 14);
				nvgFillColor(args.vg, nvgRGBA(0x2F, 0x55, 0xD4, 0x40));
				nvgFill(args.vg);
			}
			// waveform, normalized as it plays
			float norm = (module->autoNormalize && s->peak > 1e-6f) ? 0.891f / s->peak : 1.f;
			int n = (int)s->ovMin.size();
			for (int i = 0; i < (int)W; i++) {
				int k0 = i * n / (int)W, k1 = std::max(k0 + 1, (i + 1) * n / (int)W);
				float lo = 1.f, hi = -1.f;
				for (int k = k0; k < k1 && k < n; k++) {
					lo = std::min(lo, s->ovMin[k]);
					hi = std::max(hi, s->ovMax[k]);
				}
				lo = clamp(lo * norm, -1.f, 1.f);
				hi = clamp(hi * norm, -1.f, 1.f);
				bool inside = i >= a && i <= b;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, i, mid - hi * amp, 1.f, std::max(1.f, (hi - lo) * amp));
				nvgFillColor(args.vg, inside ? BLUE : nvgRGBA(0x2F, 0x55, 0xD4, 0x55));
				nvgFill(args.vg);
			}
			// SLICES mode: the cuts, each slice numbered
			if (slicing && f && f->handle >= 0) {
				nvgFontFaceId(args.vg, f->handle);
				nvgFontSize(args.vg, 8.f);
				nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
				for (int i = 0; i < cuts.count; i++) {
					float x = (float)cuts.start[i] / s->frames * W;
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, x, 14);
					nvgLineTo(args.vg, x, B);
					nvgStrokeColor(args.vg, i == sel ? ORANGE : PAPER);
					nvgStrokeWidth(args.vg, 1.f);
					nvgStroke(args.vg);
					float next = (float)cuts.end(i, s->frames) / s->frames * W;
					if (next - x > 9.f) {
						nvgFillColor(args.vg, i == sel ? ORANGE : PAPER);
						nvgText(args.vg, x + 2, 15, std::to_string(i + 1).c_str(), nullptr);
					}
				}
			}
			// start and end
			for (int id : {SOUNAIRE::START_PARAM, SOUNAIRE::END_PARAM}) {
				if (slicing)
					break;
				float x = module->params[id].getValue() * W;
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, x, 14);
				nvgLineTo(args.vg, x, B);
				nvgStrokeColor(args.vg, PAPER);
				nvgStrokeWidth(args.vg, 1.f);
				nvgStroke(args.vg);
				nvgBeginPath(args.vg);
				float dir = id == SOUNAIRE::START_PARAM ? 1.f : -1.f;
				nvgMoveTo(args.vg, x, 14);
				nvgLineTo(args.vg, x + 5 * dir, 14);
				nvgLineTo(args.vg, x, 20);
				nvgClosePath(args.vg);
				nvgFillColor(args.vg, PAPER);
				nvgFill(args.vg);
			}
			if (!slicing && module->params[SOUNAIRE::LOOP_MODE_PARAM].getValue() > 0.5f) {
				for (int id : {SOUNAIRE::LOOP_START_PARAM, SOUNAIRE::LOOP_END_PARAM}) {
					float x = module->params[id].getValue() * W;
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, x, 14);
					nvgLineTo(args.vg, x, B);
					nvgStrokeColor(args.vg, ORANGE);
					nvgStrokeWidth(args.vg, 1.f);
					nvgStroke(args.vg);
				}
			}
			// where the voices are
			for (int i = 0; i < sounaire::Engine::SLOTS; i++) {
				const sounaire::Voice &v = module->engine.voices[i];
				if (!v.active || v.sample != s.get() || s->frames <= 0)
					continue;
				double pos = v.head.v;
				bool reversed = v.ownRegion ? v.region.reverse : module->params[SOUNAIRE::REVERSE_PARAM].getValue() > 0.5f;
				if (reversed && i != sounaire::Engine::PREVIEW)
					pos = s->frames - 1 - pos;
				float x = (float)(pos / s->frames) * W;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, x, 14, 1.f, B - 14);
				nvgFillColor(args.vg, nvgRGBA(0xF2, 0xEE, 0xE4, (unsigned char)(80 + 175 * v.env)));
				nvgFill(args.vg);
			}
		}
		if (selecting) {
			nvgBeginPath(args.vg);
			nvgRect(args.vg, std::min(selFrom, selTo) * W, 14, std::fabs(selTo - selFrom) * W, B - 14);
			nvgFillColor(args.vg, nvgRGBA(0xF0, 0x62, 0x2F, 0x60));
			nvgFill(args.vg);
		}
		// header line: sample, rate, length, root
		if (f && f->handle >= 0) {
			nvgFontFaceId(args.vg, f->handle);
			nvgFontSize(args.vg, 10.f);
			nvgFillColor(args.vg, PAPER);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
			std::string line;
			if (module->loading)
				line = "LOADING...";
			else if (!module->lastError.empty())
				line = module->lastError;
			else if (s)
				line = string::f("%s  %.1f kHz  %.2f s", s->name.c_str(), s->rate / 1000.f, s->frames / s->rate);
			else
				line = "DROP A WAV, AIFF OR FLAC HERE";
			nvgText(args.vg, 4, 3, line.c_str(), nullptr);
			nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM);
			std::string root;
			if (slicing) {
				static const char *types[] = {"ONE SHOT", "GATE", "LOOP", "PING-PONG", "REVERSE", "REV LOOP"};
				int base = (int)module->params[SOUNAIRE::ROOT_PARAM].getValue();
				int play = (int)module->params[SOUNAIRE::PLAY_PARAM].getValue();
				root = string::f("SLICE %d/%d %s  KEY %s", sel + 1, cuts.count, types[clamp((int)cuts.type[sel], 0, 5)],
				                 sounaire_ui::noteName(base + sel).c_str());
				if (play > 0)
					root += string::f("  PLAY: %s", types[clamp(play - 1, 0, 5)]);
			}
			else {
				root = "ROOT " + sounaire_ui::noteName(module->params[SOUNAIRE::ROOT_PARAM].getValue());
				if (std::fabs(module->rootCents) >= 0.5f)
					root += string::f(" %+.0fc", module->rootCents);
			}
			nvgText(args.vg, W - 4, H - 2, root.c_str(), nullptr);
		}
		nvgResetScissor(args.vg);
		nvgRestore(args.vg);
		OpaqueWidget::drawLayer(args, layer);
	}
};

// the keys from C-1 to G9: the playing range in blue, the root in orange, the
// keys being played in paper; a click plays a key
struct SounaireKeys : OpaqueWidget {
	SOUNAIRE *module = nullptr;
	float dragX = 0.f;
	int clicked = -1;

	int keyAt(float x) const {
		return clamp((int)(x / box.size.x * 128.f), 0, 127);
	}

	// a click plays the key (in SLICES mode its slice) while held; dragging
	// moves to the keys passed over
	void onButton(const event::Button &e) override {
		if (!module || e.button != GLFW_MOUSE_BUTTON_LEFT)
			return;
		if (e.action == GLFW_PRESS) {
			e.consume(this);
			dragX = e.pos.x;
			clicked = keyAt(e.pos.x);
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
		int k = keyAt(dragX);
		if (k != clicked) {
			clicked = k;
			module->mouseKey = k;
		}
	}

	void onDragEnd(const event::DragEnd &e) override {
		if (module)
			module->mouseHeld = false;
		clicked = -1;
	}

	void drawLayer(const DrawArgs &args, int layer) override {
		using namespace sounaire_ui;
		if (layer == 1 && module) {
			float W = box.size.x, H = box.size.y, kw = W / 128.f;
			int lo = (int)module->params[SOUNAIRE::LOW_PARAM].getValue(), hi = (int)module->params[SOUNAIRE::HIGH_PARAM].getValue();
			int root = (int)module->params[SOUNAIRE::ROOT_PARAM].getValue();
			if (module->sliceMode()) {
				// one key per slice from BASE; the selected slice's key in orange
				lo = root;
				hi = root + module->getSlices().count - 1;
				root += module->selectedSlice;
			}
			for (int k = 0; k < 128; k++) {
				int pc = k % 12;
				bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
				bool inRange = k >= std::min(lo, hi) && k <= std::max(lo, hi);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, k * kw, black ? 0 : 0, std::max(1.f, kw - 0.4f), black ? H * 0.62f : H);
				NVGcolor c = black ? INK : (inRange ? BLUE : TRACK);
				if (black && inRange)
					c = nvgRGB(0x1F, 0x3F, 0xAF);
				if (k == root)
					c = ORANGE;
				nvgFillColor(args.vg, c);
				nvgFill(args.vg);
			}
			// the keys being played: paper, with an ink dot at the bottom
			for (int c = 0; c <= MAX_VOICES; c++) {
				int k = c < MAX_VOICES ? (int)module->keyHeld[c] : (int)module->mouseShown;
				if (k < 0 || k > 127)
					continue;
				int pc = k % 12;
				bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
				float h = black ? H * 0.62f : H;
				nvgBeginPath(args.vg);
				nvgRect(args.vg, k * kw, 0, std::max(1.f, kw - 0.4f), h);
				nvgFillColor(args.vg, PAPER);
				nvgFill(args.vg);
				nvgBeginPath(args.vg);
				nvgRect(args.vg, k * kw, h - 2.5f, std::max(1.f, kw - 0.4f), 2.5f);
				nvgFillColor(args.vg, INK);
				nvgFill(args.vg);
			}
		}
		OpaqueWidget::drawLayer(args, layer);
	}
};

struct SOUNAIREWidget : BidooWidget {
	SOUNAIREWidget(SOUNAIRE *module) {
		using namespace sounaire_ui;
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/SOUNAIRE.svg"));
		SounaireDisplay *display = new SounaireDisplay;
		display->module = module;
		display->box.pos = Vec(10, 24);
		display->box.size = Vec(400, 100);
		addChild(display);
		SounaireKeys *keys = new SounaireKeys;
		keys->module = module;
		keys->box.pos = Vec(10, 128);
		keys->box.size = Vec(400, 12);
		addChild(keys);

		// three rows of eight columns
		auto cx = [](int col) { return 37.f + 49.f * col; };
		const float rows[3] = {172.f, 228.f, 284.f};
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
		addParam(createParamCentered<RisoButton>(Vec(cx(6), rows[0]), module, SOUNAIRE::REVERSE_PARAM));
		addParam(createParamCentered<RisoButton>(Vec(cx(7), rows[0]), module, SOUNAIRE::LEGATO_PARAM));

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

		// jacks: ten inputs, then the two outputs on the ink block
		int ins[10] = {SOUNAIRE::VOCT_INPUT, SOUNAIRE::GATE_INPUT, SOUNAIRE::VEL_INPUT, SOUNAIRE::BEND_INPUT,
		               SOUNAIRE::START_INPUT, SOUNAIRE::SPEED_INPUT, SOUNAIRE::CUTOFF_INPUT, SOUNAIRE::RES_INPUT,
		               SOUNAIRE::PITCH_INPUT, SOUNAIRE::PLAY_INPUT};
		for (int i = 0; i < 10; i++)
			addInput(createInputCentered<RisoJack>(Vec(22.f + 33.f * i, 338.f), module, ins[i]));
		RisoJack *l = createOutputCentered<RisoJack>(Vec(361.f, 338.f), module, SOUNAIRE::L_OUTPUT);
		RisoJack *r = createOutputCentered<RisoJack>(Vec(395.f, 338.f), module, SOUNAIRE::R_OUTPUT);
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
		menu->addChild(createMenuItem("Find the root note again (whole sample)", "", [=]() { m->requestRootAnalysis(0.0, 1.0); }));
		menu->addChild(createBoolPtrMenuItem("Normalize the sample", "", &m->autoNormalize));
		menu->addChild(createBoolPtrMenuItem("Find the root note when loading", "", &m->autoRoot));
		menu->addChild(createBoolPtrMenuItem("Polyphonic outputs (one channel per voice)", "", &m->polyOut));
		menu->addChild(createIndexSubmenuItem("Pitch bend range", {"1 semitone", "2 semitones", "7 semitones", "12 semitones", "24 semitones"},
			[=]() { int r[] = {1, 2, 7, 12, 24}; for (int i = 0; i < 5; i++) if (r[i] == m->bendRange) return i; return 1; },
			[=](size_t i) { int r[] = {1, 2, 7, 12, 24}; m->bendRange = r[i]; }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Slices"));
		static const std::vector<std::string> types = {"One shot", "Gate", "Loop", "Ping-pong", "Reverse", "Reverse loop"};
		menu->addChild(createSubmenuItem("Find the transients again", "", [=](Menu *sub) {
			const char *names[3] = {"Fewer slices (low sensitivity)", "Medium", "More slices (high sensitivity)"};
			for (int i = 0; i < 3; i++)
				sub->addChild(createCheckMenuItem(names[i], "", [=]() { return m->sensitivity == i; }, [=]() { m->sensitivity = i; m->requestDetect(); }));
		}));
		menu->addChild(createBoolPtrMenuItem("Find the transients when loading", "", &m->detectOnLoad));
		menu->addChild(createSubmenuItem("Divide equally", "", [=](Menu *sub) {
			for (int n : {2, 4, 8, 16, 32, 64})
				sub->addChild(createMenuItem(string::f("%d slices", n), "", [=]() { m->divideSlices(n); }));
		}));
		menu->addChild(createMenuItem("One slice (clear the cuts)", "", [=]() { m->setSlices({0}, m->getSlices().type[0]); }));
		int sel = m->selectedSlice;
		menu->addChild(createIndexSubmenuItem(string::f("Slice %d plays", sel + 1), types,
			[=]() { return (size_t)m->getSlices().type[clamp(sel, 0, m->getSlices().count - 1)]; },
			[=](size_t i) { m->setSliceType(sel, (int)i); }));
		menu->addChild(createSubmenuItem("Every slice plays", "", [=](Menu *sub) {
			for (int i = 0; i < (int)types.size(); i++)
				sub->addChild(createMenuItem(types[i], "", [=]() { m->setAllSliceTypes(i); }));
		}));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Drag a marker; Shift: snap to a zero crossing"));
		menu->addChild(createMenuLabel("Ctrl+Shift+drag: region for the root note"));
		menu->addChild(createMenuLabel("Alt+click: preview from there"));
		menu->addChild(createMenuLabel("Slices: click selects, double-click adds or removes a cut"));
	}
};
