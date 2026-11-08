// FiLTRe's panel: the Riso look in orange (an effect). The screen draws the spectrum of the signal and the curve of the
// selected filter, cached in a framebuffer and redrawn only when they change; the panel SVG holds the labels.
#pragma once
#include "RisoComponents.hpp"
#if defined(__SSE2__)
#include <xmmintrin.h>
#endif

namespace filtre_ui {
using namespace riso;
using filtre::kPoints;

// The curve's measures run on their own thread: a reverb or a long comb takes a few milliseconds, and the UI must not
// feel it. They wait three times what the last one took, so a moving CV never takes more than a quarter of a core.
struct CurveWorker {
	std::thread thread;
	std::mutex mutex;
	std::condition_variable wake;
	bool stop = false, pending = false, fresh = false;
	filtre::CurveSettings request;
	double requestFs = 48000.0;
	filtre::Curve result;

	void start() {
		thread = std::thread([this] { run(); });
	}
	~CurveWorker() {
		if (thread.joinable()) {
			{
				std::lock_guard<std::mutex> lock(mutex);
				stop = true;
			}
			wake.notify_all();
			thread.join();
		}
	}
	void ask(const filtre::CurveSettings &s, double fs) {
		{
			std::lock_guard<std::mutex> lock(mutex);
			request = s;
			requestFs = fs;
			pending = true;
		}
		wake.notify_all();
	}
	bool take(filtre::Curve &out) {
		std::lock_guard<std::mutex> lock(mutex);
		if (!fresh)
			return false;
		fresh = false;
		out = result;
		return true;
	}
	void run() {
#if defined(__SSE2__)
		_mm_setcsr(_mm_getcsr() | 0x8040);  // no denormals in the tails, as on Rack's engine threads
#endif
		auto meter = std::unique_ptr<filtre::CurveMeter>(new filtre::CurveMeter);
		auto curve = std::unique_ptr<filtre::Curve>(new filtre::Curve);
		double fs = 0.0;
		std::unique_lock<std::mutex> lock(mutex);
		while (true) {
			wake.wait(lock, [this] { return stop || pending; });
			if (stop)
				return;
			pending = false;
			const filtre::CurveSettings s = request;
			const double askedFs = requestFs;
			lock.unlock();
			const auto t0 = std::chrono::steady_clock::now();
			if (askedFs != fs) {
				fs = askedFs;
				meter->prepare(fs);
			}
			meter->measure(s, *curve);
			const auto took = std::chrono::steady_clock::now() - t0;
			lock.lock();
			result = *curve;
			fresh = true;
			const auto pause = std::max<std::chrono::steady_clock::duration>(std::chrono::milliseconds(33), 3 * took);
			wake.wait_for(lock, pause, [this] { return stop; });
			if (stop)
				return;
		}
	}
};

inline std::string hzText(float hz) {
	return hz >= 1000.f ? rack::string::f("%.2f kHz", hz / 1000.f) : rack::string::f("%.0f Hz", hz);
}

// What the screen draws. Both axes share one scale, 1 dB is the same height for the curve and the spectra, so the output
// is the input moved by the curve; the 0 dB of the curve sits 24 dB under the top, full scale (5 V) of the spectra at the top.
struct Screen : TransparentWidget {
	static constexpr float RANGE = 72.f, CURVE_TOP = 24.f, AXIS = 13.f;
	filtre::Curve curve;
	float in[kPoints], out[kPoints];
	double fs = 48000.0;
	filtre::CurveSettings shown;

	Screen() {
		std::fill(in, in + kPoints, filtre::SpectrumMeter::kFloor);
		std::fill(out, out + kPoints, filtre::SpectrumMeter::kFloor);
	}
	float plotH() const {
		return box.size.y - AXIS;
	}
	float xOfPoint(int p) const {
		return box.size.x * (float)p / (kPoints - 1);
	}
	float xOfHz(double hz) const {
		return box.size.x * (float)(std::log(hz / 20.0) / std::log(filtre::topHz(fs) / 20.0));
	}
	float yOfCurve(float db) const {
		return clamp(plotH() * (CURVE_TOP - db) / RANGE, 0.f, plotH());
	}
	float yOfSpectrum(float db) const {
		return clamp(plotH() * -db / RANGE, 0.f, plotH());
	}
	void line(NVGcontext *vg, const float *db, bool isCurve) const {
		for (int p = 0; p < kPoints; p++) {
			const float x = xOfPoint(p), y = isCurve ? yOfCurve(db[p]) : yOfSpectrum(db[p]);
			if (p == 0)
				nvgMoveTo(vg, x, y);
			else
				nvgLineTo(vg, x, y);
		}
	}
	void label(NVGcontext *vg, float x, float y, const std::string &s, int align, NVGcolor c, float size = 8.5f) const {
		nvgFontSize(vg, size);
		nvgTextAlign(vg, align);
		nvgFillColor(vg, c);
		nvgText(vg, x, y, s.c_str(), nullptr);
	}

	void draw(const DrawArgs &args) override {
		NVGcontext *vg = args.vg;
		const float w = box.size.x, h = box.size.y, ph = plotH();
		const NVGcolor dimPaper = nvgRGBA(0xF2, 0xEE, 0xE4, 110), gridC = nvgRGBA(0xF2, 0xEE, 0xE4, 26), strongGrid = nvgRGBA(0xF2, 0xEE, 0xE4, 80);
		nvgBeginPath(vg);
		nvgRoundedRect(vg, 0, 0, w, h, 3.f);
		nvgFillColor(vg, INK);
		nvgFill(vg);

		std::shared_ptr<window::Font> font = APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
		if (font)
			nvgFontFaceId(vg, font->handle);

		nvgScissor(vg, 0, 0, w, h);
		// frequency grid
		static const float freqs[] = {50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
		static const char *const freqNames[] = {"50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"};
		for (int i = 0; i < 9; i++) {
			if (freqs[i] >= filtre::topHz(fs))
				continue;
			const float x = xOfHz(freqs[i]);
			nvgBeginPath(vg);
			nvgMoveTo(vg, x, 0);
			nvgLineTo(vg, x, ph);
			nvgStrokeColor(vg, gridC);
			nvgStrokeWidth(vg, 0.7f);
			nvgStroke(vg);
			label(vg, x, h - 3.f, freqNames[i], NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM, dimPaper);
		}
		// level grid every 12 dB: on the left the curve's gain, on the right the spectra's level
		for (int db = -48; db <= 24; db += 12) {
			const float y = yOfCurve((float)db);
			nvgBeginPath(vg);
			nvgMoveTo(vg, 0, y);
			nvgLineTo(vg, w, y);
			nvgStrokeColor(vg, db == 0 ? strongGrid : gridC);
			nvgStrokeWidth(vg, 0.7f);
			nvgStroke(vg);
			if (db > -48 && db < 24) {
				label(vg, 3.f, y - 1.f, db ? rack::string::f("%+d", db) : "0", NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, dimPaper);
				label(vg, w - 3.f, y - 1.f, std::to_string(db - 24), NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, nvgRGBA(0xF0, 0x62, 0x2F, 170));
			}
		}

		// the output: bright and filled; the input: a dim line behind it
		nvgBeginPath(vg);
		nvgMoveTo(vg, 0, ph);
		for (int p = 0; p < kPoints; p++)
			nvgLineTo(vg, xOfPoint(p), yOfSpectrum(out[p]));
		nvgLineTo(vg, w, ph);
		nvgClosePath(vg);
		nvgFillColor(vg, nvgRGBA(0xF0, 0x62, 0x2F, 150));
		nvgFill(vg);
		nvgBeginPath(vg);
		line(vg, out, false);
		nvgStrokeColor(vg, ORANGE);
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);
		nvgBeginPath(vg);
		line(vg, in, false);
		nvgStrokeColor(vg, nvgRGBA(0xF2, 0xEE, 0xE4, 90));
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);

		// the cutoff
		const float cx = xOfHz(std::max(shown.cutoff, 20.f));
		nvgBeginPath(vg);
		nvgMoveTo(vg, cx, ph - 7.f);
		nvgLineTo(vg, cx, ph);
		nvgStrokeColor(vg, PAPER);
		nvgStrokeWidth(vg, 1.5f);
		nvgStroke(vg);

		// the curve: what the bins do inside each point as a band behind it, the mean power as the line
		nvgBeginPath(vg);
		for (int p = 0; p < kPoints; p++) {
			const float x = xOfPoint(p), y = yOfCurve(curve.high[p]);
			if (p == 0)
				nvgMoveTo(vg, x, y);
			else
				nvgLineTo(vg, x, y);
		}
		for (int p = kPoints - 1; p >= 0; p--)
			nvgLineTo(vg, xOfPoint(p), yOfCurve(curve.low[p]));
		nvgClosePath(vg);
		nvgFillColor(vg, nvgRGBA(0xF2, 0xEE, 0xE4, 56));
		nvgFill(vg);
		nvgBeginPath(vg);
		line(vg, curve.mean, true);
		nvgStrokeColor(vg, PAPER);
		nvgStrokeWidth(vg, 1.6f);
		nvgLineJoin(vg, NVG_ROUND);
		nvgStroke(vg);

		// the readout
		const filtre::TypeInfo &info = filtre::typeAt(filtre::positionOfId(shown.id));
		label(vg, 24.f, 3.f, info.name, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, PAPER, 15.f);
		if (info.morph)
			label(vg, 24.f, 18.f, rack::string::f("MORPH %s  %.0f%%", info.morph, shown.morph * 100.f), NVG_ALIGN_LEFT | NVG_ALIGN_TOP, PAPER);
		else
			label(vg, 24.f, 18.f, "no MORPH", NVG_ALIGN_LEFT | NVG_ALIGN_TOP, dimPaper);
		label(vg, w - 24.f, 3.f, hzText(shown.cutoff), NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, PAPER, 15.f);
		label(vg, w - 24.f, 18.f, rack::string::f("RES %.0f%%  DRIVE %.0f%%  MIX %.0f%%", shown.res * 100.f, shown.drive * 100.f, shown.mix * 100.f),
		      NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, PAPER);
		std::string note;
		if (curve.kind == filtre::CurveKind::Noise)
			note = "curve: white noise through it, this type moves frequencies";
		else if (curve.kind == filtre::CurveKind::SmallSignal || shown.drive > 0.001f)
			note = "curve: quiet signal, loud ones bend it";
		if (!note.empty()) {
			// on a plate: the spectrum is often bright down there
			nvgFontSize(vg, 8.5f);
			const float noteW = nvgTextBounds(vg, 0.f, 0.f, note.c_str(), nullptr, nullptr);
			nvgBeginPath(vg);
			nvgRect(vg, w - 24.f - noteW - 3.f, ph - 13.f, noteW + 6.f, 11.f);
			nvgFillColor(vg, nvgRGBA(0x1B, 0x1A, 0x17, 190));
			nvgFill(vg);
			label(vg, w - 24.f, ph - 3.f, note, NVG_ALIGN_RIGHT | NVG_ALIGN_BOTTOM, PAPER);
		}
		nvgResetScissor(vg);
	}
};

// Lit in a dark room, as fLAME's screen
struct ScreenBuffer : FramebufferWidget {
	void draw(const DrawArgs &args) override {
	}
	void drawLayer(const DrawArgs &args, int layer) override {
		if (layer == 1)
			FramebufferWidget::draw(args);
	}
};

inline void appendTypeMenu(Menu *menu, FILTRE *m) {
	const int current = (int)m->params[FILTRE::TYPE_PARAM].getValue();
	for (int g = 0; g < filtre::groupCount(); g++) {
		const bool here = filtre::groupOfPosition(current) == g;
		menu->addChild(createSubmenuItem(filtre::groupName(g), here ? filtre::typeAt(current).name : "", [=](Menu *sub) {
			for (int i = 0; i < filtre::kNumTypes; i++)
				if (filtre::groupOfPosition(i) == g)
					sub->addChild(createCheckMenuItem(filtre::typeAt(i).name, filtre::typeAt(i).morph ? filtre::typeAt(i).morph : "",
						[=]() { return (int)m->params[FILTRE::TYPE_PARAM].getValue() == i; },
						[=]() { m->params[FILTRE::TYPE_PARAM].setValue((float)i); }));
		}));
	}
}

struct Display : OpaqueWidget {
	FILTRE *module = nullptr;
	ScreenBuffer *buffer = nullptr;
	Screen *screen = nullptr;
	CurveWorker worker;
	filtre::SpectrumMeter meterIn, meterOut;
	std::vector<float> window;
	filtre::CurveSettings asked;
	bool askedOnce = false;
	double fs = 0.0;
	double lastFrame = 0.0;
	uint32_t lastWritten = 0;
	bool settled = true;

	void setup() {
		buffer = new ScreenBuffer;
		buffer->box.size = box.size;
		addChild(buffer);
		screen = new Screen;
		screen->box.size = box.size;
		buffer->addChild(screen);
		if (module)
			worker.start();
		else
			preview();
	}

	// In the module browser (and the wiki's picture): real numbers, pink noise through the default filter
	void preview() {
		const double rate = 48000.0;
		screen->fs = rate;
		filtre::CurveSettings s;
		s.id = filtre::typeAt(filtre::positionOfId(1)).id;
		s.cutoff = 1200.f;
		s.res = 0.55f;
		screen->shown = s;
		filtre::CurveMeter curveMeter;
		curveMeter.prepare(rate);
		curveMeter.measure(s, screen->curve);
		filtre::FilterCore core;
		core.prepare(rate);
		core.setup(s.id, s.cutoff, s.res, s.drive, s.morph);
		meterIn.prepare(rate);
		meterOut.prepare(rate);
		const int n = meterIn.windowSize();
		std::vector<float> a((size_t)n), b((size_t)n);
		uint32_t seed = 12345;
		float b0 = 0.f, b1 = 0.f, b2 = 0.f;
		for (int pass = 0; pass < 8; pass++) {
			for (int i = 0; i < n; i++) {
				seed = seed * 1664525u + 1013904223u;
				const float white = (float)(int32_t)seed / 2147483648.f;
				b0 = 0.99765f * b0 + white * 0.0990460f;
				b1 = 0.96300f * b1 + white * 0.2965164f;
				b2 = 0.57000f * b2 + white * 1.0526913f;
				float l = 0.25f * (b0 + b1 + b2 + white * 0.1848f), r = l;
				a[(size_t)i] = l;
				core.process(l, r);
				b[(size_t)i] = l;
			}
			meterIn.update(a.data(), screen->in);
			meterOut.update(b.data(), screen->out);
		}
	}

	void step() override {
		OpaqueWidget::step();
		if (!module || !buffer)
			return;
		const double rate = module->sampleRate.load();
		if (rate != fs) {
			fs = rate;
			screen->fs = fs;
			meterIn.prepare(fs);
			meterOut.prepare(fs);
			window.resize((size_t)meterIn.windowSize());
			lastWritten = module->ringOut.written();
			askedOnce = false;
			settled = false;
		}
		filtre::CurveSettings s;
		s.id = module->shownId.load();
		s.cutoff = module->shownCut.load();
		s.res = module->shownRes.load();
		s.drive = module->shownDrive.load();
		s.morph = module->shownMorph.load();
		s.mix = module->shownMix.load();
		if (!askedOnce || !s.same(asked)) {
			asked = s;
			askedOnce = true;
			worker.ask(s, fs);
		}
		bool dirty = worker.take(screen->curve);
		const double now = system::getTime();
		if (now - lastFrame < 1.0 / 30.0) {
			if (dirty)
				buffer->setDirty();
			return;
		}
		lastFrame = now;
		if (s.id != screen->shown.id || s.cutoff != screen->shown.cutoff || s.res != screen->shown.res || s.drive != screen->shown.drive
		    || s.morph != screen->shown.morph || s.mix != screen->shown.mix) {
			screen->shown = s;
			dirty = true;
		}
		// the spectra: from the rings while the audio runs, then falling to the floor on silence
		const uint32_t written = module->ringOut.written();
		const bool running = written != lastWritten;
		lastWritten = written;
		if (running || !settled) {
			const int n = meterIn.windowSize();
			if (running)
				module->ringIn.latest(window.data(), n);
			else
				std::fill(window.begin(), window.end(), 0.f);
			meterIn.update(window.data(), screen->in);
			if (running)
				module->ringOut.latest(window.data(), n);
			meterOut.update(window.data(), screen->out);
			float top = filtre::SpectrumMeter::kFloor;
			for (int p = 0; p < kPoints; p++)
				top = std::max({top, screen->in[p], screen->out[p]});
			settled = !running && top < -100.f;
			dirty = true;
		}
		if (dirty)
			buffer->setDirty();
	}

	void onButton(const ButtonEvent &e) override {
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
			Menu *menu = createMenu();
			menu->addChild(createMenuLabel("Type"));
			appendTypeMenu(menu, module);
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}
};

// The TYPE knob: right-click for the types by family
struct TypeKnob : RisoSnapKnob {
	void appendContextMenu(Menu *menu) override {
		FILTRE *m = dynamic_cast<FILTRE *>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		appendTypeMenu(menu, m);
	}
};

// Faded when the type does not use it
struct MorphKnob : RisoKnob {
	void draw(const DrawArgs &args) override {
		FILTRE *m = dynamic_cast<FILTRE *>(module);
		if (m && !filtre::typeAt(filtre::positionOfId(m->shownId.load())).morph)
			nvgGlobalAlpha(args.vg, 0.3f);
		RisoKnob::draw(args);
	}
};

// An attenuverter: small, its arc from the middle
struct Attenuverter : app::Knob {
	Attenuverter() {
		box.size = Vec(18, 18);
		minAngle = -0.75f * M_PI;
		maxAngle = 0.75f * M_PI;
	}
	void draw(const DrawArgs &args) override {
		engine::ParamQuantity *pq = getParamQuantity();
		const float v = pq ? pq->getScaledValue() : 0.5f;
		const float c = box.size.x / 2, r = c - 1.2f;
		const float a0 = 0.75f * M_PI, av = a0 + v * 1.5f * M_PI, mid = a0 + 0.75f * M_PI;
		nvgLineCap(args.vg, NVG_ROUND);
		nvgStrokeWidth(args.vg, 1.6f);
		nvgBeginPath(args.vg);
		nvgArc(args.vg, c, c, r, a0, a0 + 1.5f * M_PI, NVG_CW);
		nvgStrokeColor(args.vg, TRACK);
		nvgStroke(args.vg);
		if (std::fabs(av - mid) > 0.02f) {
			nvgBeginPath(args.vg);
			nvgArc(args.vg, c, c, r, std::min(av, mid), std::max(av, mid), NVG_CW);
			nvgStrokeColor(args.vg, ORANGE);
			nvgStroke(args.vg);
		}
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, c, c, r * 0.68f);
		nvgFillColor(args.vg, INK);
		nvgFill(args.vg);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, c, c);
		nvgLineTo(args.vg, c + std::cos(av) * r * 0.5f, c + std::sin(av) * r * 0.5f);
		nvgStrokeColor(args.vg, PAPER);
		nvgStrokeWidth(args.vg, 1.5f);
		nvgStroke(args.vg);
	}
};

}  // namespace filtre_ui

struct FILTREWidget : BidooWidget {
	FILTREWidget(FILTRE *module) {
		using namespace filtre_ui;
		setModule(module);
		themable = false; // the Riso panel has no other look
		prepareThemes(asset::plugin(pluginInstance, "res/FILTRE.svg"));

		Display *display = new Display;
		display->module = module;
		display->box.pos = Vec(10, 21);
		display->box.size = Vec(400, 190);
		addChild(display);
		display->setup();

		// a column per control: knob, attenuverter, CV jack
		static const int knobs[6] = {FILTRE::TYPE_PARAM, FILTRE::CUTOFF_PARAM, FILTRE::RES_PARAM, FILTRE::DRIVE_PARAM, FILTRE::MORPH_PARAM, FILTRE::MIX_PARAM};
		static const int amounts[6] = {FILTRE::TYPE_CV_PARAM, FILTRE::CUTOFF_CV_PARAM, FILTRE::RES_CV_PARAM, FILTRE::DRIVE_CV_PARAM, FILTRE::MORPH_CV_PARAM, FILTRE::MIX_CV_PARAM};
		static const int cvs[6] = {FILTRE::TYPE_INPUT, FILTRE::CUTOFF_INPUT, FILTRE::RES_INPUT, FILTRE::DRIVE_INPUT, FILTRE::MORPH_INPUT, FILTRE::MIX_INPUT};
		for (int i = 0; i < 6; i++) {
			const float x = 82.f + 51.f * i;
			RisoKnob *k;
			if (i == 0)
				k = createParamCentered<TypeKnob>(Vec(x, 234.f), module, knobs[i]);
			else if (i == 4)
				k = createParamCentered<MorphKnob>(Vec(x, 234.f), module, knobs[i]);
			else
				k = createParamCentered<RisoKnob>(Vec(x, 234.f), module, knobs[i]);
			k->arc = ORANGE;
			addParam(k);
			addParam(createParamCentered<Attenuverter>(Vec(x, 276.f), module, amounts[i]));
			addInput(createInputCentered<RisoJack>(Vec(x, 312.f), module, cvs[i]));
		}
		addInput(createInputCentered<RisoJack>(Vec(30.f, 252.f), module, FILTRE::INL_INPUT));
		addInput(createInputCentered<RisoJack>(Vec(30.f, 300.f), module, FILTRE::INR_INPUT));
		RisoJack *ol = createOutputCentered<RisoJack>(Vec(390.f, 252.f), module, FILTRE::OUTL_OUTPUT);
		ol->onInk = true;
		addOutput(ol);
		RisoJack *orj = createOutputCentered<RisoJack>(Vec(390.f, 300.f), module, FILTRE::OUTR_OUTPUT);
		orj->onInk = true;
		addOutput(orj);
	}

	void appendContextMenu(Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		FILTRE *m = dynamic_cast<FILTRE *>(module);
		if (!m)
			return;
		using namespace filtre_ui;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Type"));
		appendTypeMenu(menu, m);
		menu->addChild(new MenuSeparator);
		menu->addChild(createIndexPtrSubmenuItem("Controls and CV are read", {"Every 16 samples (as Potion)", "Every 4 samples", "Every sample (audio-rate FM, more CPU)"}, &m->rate));
	}
};
