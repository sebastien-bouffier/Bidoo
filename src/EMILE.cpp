#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "osdialog.h"
#include <vector>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include "dep/lodepng/lodepng.h"

// An image played as sound: the row under the position is a spectrum (x is
// frequency, the selected channels the level), resynthesized by inverse FFT
// with 4 overlapping frames. Each bin keeps its phase from frame to frame, so
// partials are steady; the bins start at random phases so a bright band is
// a sound, not a click per frame.

const int FS = 4096;
const int N = 4;
const int FS2 = FS / 2;
const int FS4 = FS2 / 2;
const float IFS = 1.0f / FS;
const int STS = FS/N;
const float IFS2 = 1.0f/FS2;


using namespace std;

struct EmileImage {
	std::vector<unsigned char> rgba;  // 16 bits per channel, big endian, as lodepng gives it
	unsigned width = 0;
	unsigned height = 0;
	// Per pixel column: where it lands on the frequency curve (0 to 1), for
	// the curve it was computed at. Sized by the worker, filled by the audio
	// thread when the curve moves.
	std::vector<float> shape;
	float shapeCurve = -1.f;
};

// tanh, close enough for a soft clip (Pade, exact at 0 and at +-3).
inline float emileSoftClip(float x) {
	x = clamp(x, -3.f, 3.f);
	float x2 = x * x;
	return x * (27.f + x2) / (27.f + 9.f * x2);
}

struct EMILE : BidooModule {
	enum ParamIds {
		CURVE_PARAM,
		GAIN_PARAM,
		TUNE_PARAM,
		POS_PARAM,
		R_PARAM,
		G_PARAM,
		B_PARAM,
		A_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		POS_INPUT,
		CURVE_INPUT,
		TUNE_INPUT,
		R_INPUT,
		G_INPUT,
		B_INPUT,
		A_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		R_LIGTH,
		G_LIGTH,
		B_LIGTH,
		A_LIGTH,
		NUM_LIGHTS
	};

	// Audio thread: the image it plays. The worker loads new ones into
	// `pending`; the audio thread takes it and moves the old one to `retired`,
	// which the worker frees.
	EmileImage *img = nullptr;
	std::atomic<EmileImage*> pending{nullptr};
	std::atomic<EmileImage*> retired{nullptr};
	std::atomic<bool> pendingSet{false};

	std::thread worker;
	std::mutex jobMutex;
	std::condition_variable jobCv;
	std::string jobPath;
	bool jobRequested = false;
	std::atomic<bool> quit{false};

	// UI thread: what the display shows.
	std::mutex uiMutex;
	std::string lastPath;      // under uiMutex
	std::atomic<unsigned> shownWidth{0}, shownHeight{0};

	std::atomic<unsigned> samplePos{0};
	float *magn;
	float *out;
	float *acc;
	float phase[FS2];
	float binFreq[FS2];   // weighted true frequency (in bins) of what feeds each bin
	float binWeight[FS2];
	int rIdx = STS;
	PFFFT_Setup *pffftSetup;
	float *fftIn;
	float *fftOut;
	// All four colours on, so a new EMILE plays the image straight away.
	bool r = true;
	bool g = true;
	bool b = true;
	bool a = true;
	dsp::SchmittTrigger rTrigger, gTrigger, bTrigger, aTrigger;
	float curve = 0.0f;
	float window[FS];          // Hann, computed once
	dsp::ClockDivider lightDivider;

	EMILE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(GAIN_PARAM, 0.1f, 10.0f, 1.0f, "Gain", "x");
		configParam(POS_PARAM, 0.0f, 1.0f, 0.0f, "Position (image row)", "%", 0.f, 100.f);
		configParam(TUNE_PARAM, -4.0f, 6.0f, 0.0f, "Tune", " oct");
		configParam(CURVE_PARAM, 0.01f, 0.1f, 0.05f, "Frequency curve");
		configButton(R_PARAM, "Red");
		configButton(G_PARAM, "Green");
		configButton(B_PARAM, "Blue");
		configButton(A_PARAM, "Alpha");
		configInput(POS_INPUT, "Position (10 V = last row)");
		configInput(CURVE_INPUT, "Frequency curve (10 V = +0.09)");
		configInput(TUNE_INPUT, "Tune (1V/oct)");
		configInput(R_INPUT, "Red toggle");
		configInput(G_INPUT, "Green toggle");
		configInput(B_INPUT, "Blue toggle");
		configInput(A_INPUT, "Alpha toggle");
		configOutput(OUT, "Audio");

		magn = (float*) pffft_aligned_malloc(FS2*sizeof(float));
		out = (float*) pffft_aligned_malloc(STS*sizeof(float));
		acc = (float*) pffft_aligned_malloc(2*FS*sizeof(float));
		memset(acc, 0, 2*FS*sizeof(float));
		memset(out, 0, STS*sizeof(float));
		pffftSetup = pffft_new_setup(FS, PFFFT_REAL);
		fftIn = (float*)pffft_aligned_malloc(FS*sizeof(float));
		fftOut =  (float*)pffft_aligned_malloc(FS*sizeof(float));
		for (int i = 0; i < FS; i++)
			window[i] = -0.5f * std::cos(2.0f * float(M_PI) * i * IFS) + 0.5f;
		lightDivider.setDivision(64);
		// Fixed random starting phases (the same every time).
		uint32_t seed = 12345;
		for (int i = 0; i < FS2; i++) {
			seed = seed * 1664525u + 1013904223u;
			phase[i] = (seed >> 8) * (2.0f * float(M_PI) / 16777216.0f);
		}
		worker = std::thread(&EMILE::workerLoop, this);
	}

	~EMILE() override {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			quit = true;
		}
		jobCv.notify_all();
		if (worker.joinable())
			worker.join();
		delete pending.load();
		delete retired.load();
		delete img;
		pffft_aligned_free(magn);
		pffft_aligned_free(out);
		pffft_aligned_free(acc);
		pffft_aligned_free(fftIn);
		pffft_aligned_free(fftOut);
		pffft_destroy_setup(pffftSetup);
	}

	// UI thread: load a PNG.
	void loadImage(const std::string &path) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			jobPath = path;
			jobRequested = true;
		}
		jobCv.notify_all();
	}

	std::string getPath() {
		std::lock_guard<std::mutex> lock(uiMutex);
		return lastPath;
	}

	void workerLoop() {
		std::unique_lock<std::mutex> lock(jobMutex);
		while (!quit) {
			jobCv.wait_for(lock, std::chrono::milliseconds(50));
			delete retired.exchange(nullptr, std::memory_order_acq_rel);
			if (!jobRequested || quit)
				continue;
			std::string path = jobPath;
			jobRequested = false;
			lock.unlock();

			EmileImage *im = new EmileImage();
			unsigned error = lodepng::decode(im->rgba, im->width, im->height, path, LCT_RGBA, 16);
			if (error != 0 || im->width == 0 || im->height == 0) {
				delete im;
				im = nullptr;
				path.clear();
			}
			if (im)
				im->shape.assign(im->width, 0.f);
			{
				std::lock_guard<std::mutex> ui(uiMutex);
				lastPath = path;
			}
			shownWidth = im ? im->width : 0;
			shownHeight = im ? im->height : 0;
			while (pendingSet.load() && !quit) {
				delete retired.exchange(nullptr, std::memory_order_acq_rel);
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			delete pending.exchange(im, std::memory_order_acq_rel);
			pendingSet = true;
			lock.lock();
		}
	}

	void process(const ProcessArgs &args) override;

	// Takes a newly loaded image, once the worker has freed the previous one.
	void takePending() {
		if (pendingSet.load(std::memory_order_acquire) && !retired.load(std::memory_order_acquire)) {
			EmileImage *im = pending.exchange(nullptr, std::memory_order_acq_rel);
			retired.store(img, std::memory_order_release);
			img = im;
			pendingSet = false;
		}
	}

	// Bypassed: still takes the loaded image, so the loader never waits.
	void processBypass(const ProcessArgs &args) override {
		takePending();
		Module::processBypass(args);
	}

	void onReset() override {
		r = g = b = a = true;
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "lastPath", json_string(getPath().c_str()));
		json_object_set_new(rootJ, "r", json_boolean(r));
		json_object_set_new(rootJ, "g", json_boolean(g));
		json_object_set_new(rootJ, "b", json_boolean(b));
		json_object_set_new(rootJ, "a", json_boolean(a));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ && json_string_value(lastPathJ)[0])
			loadImage(json_string_value(lastPathJ));
		json_t *rJ = json_object_get(rootJ, "r");
		if (rJ) r = json_is_true(rJ);
		json_t *gJ = json_object_get(rootJ, "g");
		if (gJ) g = json_is_true(gJ);
		json_t *bJ = json_object_get(rootJ, "b");
		if (bJ) b = json_is_true(bJ);
		json_t *aJ = json_object_get(rootJ, "a");
		if (aJ) a = json_is_true(aJ);
	}

};

void EMILE::process(const ProcessArgs &args) {
	takePending();

	if (rTrigger.process(params[R_PARAM].getValue()+inputs[R_INPUT].getVoltage(), 0.1f, 1.f))
		r=!r;
	if (gTrigger.process(params[G_PARAM].getValue()+inputs[G_INPUT].getVoltage(), 0.1f, 1.f))
		g=!g;
	if (bTrigger.process(params[B_PARAM].getValue()+inputs[B_INPUT].getVoltage(), 0.1f, 1.f))
		b=!b;
	if (aTrigger.process(params[A_PARAM].getValue()+inputs[A_INPUT].getVoltage(), 0.1f, 1.f))
		a=!a;
	if (lightDivider.process()) {
		lights[R_LIGTH].setBrightness(r?1:0);
		lights[G_LIGTH].setBrightness(g?1:0);
		lights[B_LIGTH].setBrightness(b?1:0);
		lights[A_LIGTH].setBrightness(a?1:0);
	}

	curve = clamp(params[CURVE_PARAM].getValue() + clamp(inputs[CURVE_INPUT].getVoltage(), 0.0f, 10.0f) * 0.009f, 0.01f, 0.2f);

	// Nothing to play, or nobody listening: no resynthesis.
	if (!img || !outputs[OUT].isConnected()) {
		outputs[OUT].setVoltage(0.f);
		return;
	}
	const unsigned width = img->width, height = img->height;
	unsigned pos = clamp(params[POS_PARAM].getValue() + clamp(inputs[POS_INPUT].getVoltage(), 0.0f, 10.0f) * 0.1f, 0.0f, 1.0f) * (height - 1);
	samplePos = pos;

	if (rIdx >= STS) {
		memset(fftIn, 0, FS*sizeof(float));
		memset(magn, 0, FS2*sizeof(float));
		memset(binFreq, 0, sizeof(binFreq));
		memset(binWeight, 0, sizeof(binWeight));

		// 1V/oct: 0 V is the original mapping, each volt doubles the frequencies.
		// The bins are scaled to 48 kHz, so the pitch is the same at any rate.
		float scale = 5.0f * dsp::exp2_taylor5(clamp(params[TUNE_PARAM].getValue() + inputs[TUNE_INPUT].getVoltage(), -10.f, 10.f)) * FS2 * (48000.f / args.sampleRate);
		int channels = r + g + b + a;
		const unsigned char *row = img->rgba.data() + (size_t)pos * 8 * width;
		if (img->shapeCurve != curve && img->shape.size() == width) {
			float iWidth = 1.0f/width;
			for (unsigned x = 0; x < width; x++)
				img->shape[x] = 1.0f - std::pow(1.0f - x * iWidth, curve);
			img->shapeCurve = curve;
		}
		for (unsigned x = 0; x < width; x++) {
			const unsigned char *p = row + x * 8;
			unsigned sum = (r ? 256 * p[0] + p[1] : 0) + (g ? 256 * p[2] + p[3] : 0) + (b ? 256 * p[4] + p[5] : 0) + (a ? 256 * p[6] + p[7] : 0);
			if (sum == 0)
				continue;
			float mix = 1e-7f * sum / std::max(1, channels);
			float index = scale * img->shape[x] + 3;
			// Bins above the spectrum are dropped.
			if (index < 1.0f || index >= FS2 - 2)
				continue;
			int i0 = (int)index;
			float frac = index - i0;
			magn[i0] += mix * (1.0f - frac);
			magn[i0 + 1] += mix * frac;
			binFreq[i0] += mix * (1.0f - frac) * index;
			binWeight[i0] += mix * (1.0f - frac);
			binFreq[i0 + 1] += mix * frac * index;
			binWeight[i0 + 1] += mix * frac;
		}

		// Each bin advances by the true frequency of the partials feeding it over
		// one hop, so partials continue, even between two bins.
		for (int i = 1; i < FS2; i++) {
			float f = binWeight[i] > 0.f ? binFreq[i] / binWeight[i] : (float)i;
			phase[i] += 2.0f * float(M_PI) * f * STS * IFS;
			phase[i] -= 2.0f * float(M_PI) * std::floor(phase[i] / (2.0f * float(M_PI)));
			if (magn[i] != 0.f) {
				fftIn[2*i] = magn[i] * std::cos(phase[i]);
				fftIn[2*i+1] = magn[i] * std::sin(phase[i]);
			}
		}

		pffft_transform_ordered(pffftSetup, fftIn, fftOut, NULL, PFFFT_BACKWARD);

		for (int i = 0; i < FS; i++)
			acc[i] += 2.0f*fftOut[i]*window[i];
		for (int i = 0; i < STS; i++)
			out[i] = acc[i];
		memmove(acc, acc+STS, FS*sizeof(float));
		rIdx = 0;
	}

	outputs[OUT].setVoltage(emileSoftClip(params[GAIN_PARAM].getValue() * out[rIdx]) * 5.0f);
	rIdx++;
}

struct EMILEDisplay : OpaqueWidget {
	EMILE *module;
	const float width = 125.0f;
	const float height = 130.0f;
	std::string path = "";
	int img = 0;

	EMILEDisplay() {

	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && module) {
			std::string p = module->getPath();
			if (p != path) {
				if (img)
					nvgDeleteImage(args.vg, img);
				img = p.empty() ? 0 : nvgCreateImage(args.vg, p.c_str(), 0);
				path = p;
			}
			float w = module->shownWidth, h = module->shownHeight;
			if (img && w > 0 && h > 0) {
				nvgSave(args.vg);
				nvgScale(args.vg, width / w, height / h);
				nvgBeginPath(args.vg);
				NVGpaint imgPaint = nvgImagePattern(args.vg, 0, 0, w, h, 0, img, 1.0f);
				nvgRect(args.vg, 0, 0, w, h);
				nvgFillPaint(args.vg, imgPaint);
				nvgFill(args.vg);
				nvgClosePath(args.vg);

				nvgStrokeColor(args.vg, LIGHTBLUE_BIDOO);
				nvgBeginPath(args.vg);
				nvgStrokeWidth(args.vg, 5);
				nvgMoveTo(args.vg, 0, (float)module->samplePos);
				nvgLineTo(args.vg, w, (float)module->samplePos);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgRestore(args.vg);
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct EMILEWidget : BidooWidget {
	EMILEWidget(EMILE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/EMILE.svg"));

		theme::addScrews(this);

		// The original hand-drawn panel: the picture screen; four colour buttons, each joined by a line to its CV jack;
		// four knobs under their labels; four boxed jacks. Each button and its jack sit on the line the panel draws
		// (the old code had the button 0.5 px right of its jack), each knob under its label (it was up to 0.7 px off);
		// the jacks keep their old corners (within 0.2 px of their boxes).
		EMILEDisplay *display = new EMILEDisplay();
		display->module = module;
		display->box = theme::specificRect(5, 30, 125, 130);
		addChild(display);

		const theme::Size kButtonSize = {17.9996f, 18.f};                 // LEDButton
		const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};     // SmallLight
		const float kLightIn = 6.f;   // from a button's corner to its light's
		const theme::Pos buttonRow = theme::specificCorner(8, 170, kButtonSize);
		const theme::Pos cvRow = theme::specificCorner(9, 195, theme::kTinyJackSize);
		const theme::Pos line[4] = {theme::specific(16.625f, 0), theme::specific(50.125f, 0), theme::specific(83.688f, 0), theme::specific(117.188f, 0)};
		const int buttonIds[4] = {EMILE::R_PARAM, EMILE::G_PARAM, EMILE::B_PARAM, EMILE::A_PARAM};
		const int cvIds[4] = {EMILE::R_INPUT, EMILE::G_INPUT, EMILE::B_INPUT, EMILE::A_INPUT};
		for (int i = 0; i < 4; i++) {
			const theme::Pos button = theme::colRow(line[i], buttonRow);
			const theme::Pos light = theme::specificCorner(button.x - kButtonSize.w * 0.5f + kLightIn, button.top + kLightIn, kSmallLightSize);
			addParam(theme::param<LEDButton>(button, module, buttonIds[i]));
			switch (i) {
				case 0: addChild(theme::light<SmallLight<RedLight>>(light, module, EMILE::R_LIGTH)); break;
				case 1: addChild(theme::light<SmallLight<GreenLight>>(light, module, EMILE::G_LIGTH)); break;
				case 2: addChild(theme::light<SmallLight<BlueLight>>(light, module, EMILE::B_LIGTH)); break;
				default: addChild(theme::light<SmallLight<WhiteLight>>(light, module, EMILE::A_LIGTH)); break;
			}
		}
		for (int i = 0; i < 4; i++)
			addInput(theme::input<TinyPJ301MPort>(theme::colRow(line[i], cvRow), module, cvIds[i]));

		const theme::Pos knobRow = theme::specificCorner(5, 280, theme::kBlueKnobSize);
		const theme::Pos label[4] = {theme::specific(19.062f, 0), theme::specific(50.625f, 0), theme::specific(82.188f, 0), theme::specific(113.781f, 0)};
		const int knobIds[4] = {EMILE::TUNE_PARAM, EMILE::POS_PARAM, EMILE::CURVE_PARAM, EMILE::GAIN_PARAM};
		for (int i = 0; i < 4; i++)
			addParam(theme::param<BidooBlueKnob>(theme::colRow(label[i], knobRow), module, knobIds[i]));

		const float kJackPitch = 32.2f;
		const theme::Pos tuneIn = theme::specificCorner(7.5f, 330, theme::kJackSize);
		addInput(theme::input<PJ301MPort>(tuneIn, module, EMILE::TUNE_INPUT));
		addInput(theme::input<PJ301MPort>(tuneIn.right(kJackPitch), module, EMILE::POS_INPUT));
		addInput(theme::input<PJ301MPort>(tuneIn.right(2.f * kJackPitch), module, EMILE::CURVE_INPUT));
		addOutput(theme::output<PJ301MPort>(tuneIn.right(3.f * kJackPitch), module, EMILE::OUT));
	}

  void onPathDrop(const PathDropEvent& e) override {
    Widget::onPathDrop(e);
    EMILE *module = dynamic_cast<EMILE*>(this->module);
    if (module && !e.paths.empty())
      module->loadImage(e.paths[0]);
  }

  struct EMILEItem : MenuItem {
  	EMILE *module;
  	void onAction(const event::Action &e) override {
  		std::string last = module->getPath();
  		std::string dir = last.empty() ?  asset::user("") : rack::system::getDirectory(last);
  		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
  		if (path) {
  			module->loadImage(path);
  			free(path);
  		}
  	}
  };

  void appendContextMenu(ui::Menu *menu) override {
    BidooWidget::appendContextMenu(menu);
		EMILE *module = dynamic_cast<EMILE*>(this->module);
		if (!module)
			return;
    menu->addChild(new MenuSeparator());
		menu->addChild(construct<EMILEItem>(&MenuItem::text, "Load image (png)", &EMILEItem::module, module));
	}
};

Model *modelEMILE = createModel<EMILE, EMILEWidget>("EMILE");
