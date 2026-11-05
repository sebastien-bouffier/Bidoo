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
};

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
	if (pendingSet.load(std::memory_order_acquire) && !retired.load(std::memory_order_acquire)) {
		EmileImage *im = pending.exchange(nullptr, std::memory_order_acq_rel);
		retired.store(img, std::memory_order_release);
		img = im;
		pendingSet = false;
	}

	if (rTrigger.process(params[R_PARAM].getValue()+inputs[R_INPUT].getVoltage(), 0.1f, 1.f))
		r=!r;
	lights[R_LIGTH].setBrightness(r?1:0);
	if (gTrigger.process(params[G_PARAM].getValue()+inputs[G_INPUT].getVoltage(), 0.1f, 1.f))
		g=!g;
	lights[G_LIGTH].setBrightness(g?1:0);
	if (bTrigger.process(params[B_PARAM].getValue()+inputs[B_INPUT].getVoltage(), 0.1f, 1.f))
		b=!b;
	lights[B_LIGTH].setBrightness(b?1:0);
	if (aTrigger.process(params[A_PARAM].getValue()+inputs[A_INPUT].getVoltage(), 0.1f, 1.f))
		a=!a;
	lights[A_LIGTH].setBrightness(a?1:0);

	curve = clamp(params[CURVE_PARAM].getValue() + clamp(inputs[CURVE_INPUT].getVoltage(), 0.0f, 10.0f) * 0.009f, 0.01f, 0.2f);

	if (!img) {
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
		float scale = 5.0f * dsp::exp2_taylor5(clamp(params[TUNE_PARAM].getValue() + inputs[TUNE_INPUT].getVoltage(), -10.f, 10.f)) * FS2;
		int channels = r + g + b + a;
		const unsigned char *row = img->rgba.data() + (size_t)pos * 8 * width;
		float iWidth = 1.0f/width;
		for (unsigned x = 0; x < width; x++) {
			const unsigned char *p = row + x * 8;
			unsigned sum = (r ? 256 * p[0] + p[1] : 0) + (g ? 256 * p[2] + p[3] : 0) + (b ? 256 * p[4] + p[5] : 0) + (a ? 256 * p[6] + p[7] : 0);
			if (sum == 0)
				continue;
			float mix = 1e-7f * sum / std::max(1, channels);
			float index = scale * (1.0f - std::pow(1.0f - x * iWidth, curve)) + 3;
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

		for (int i = 0; i < FS; i++) {
			float window = -0.5f * std::cos(2.0f * float(M_PI) * i * IFS) + 0.5f;
			acc[i] += 2.0f*fftOut[i]*window;
		}
		for (int i = 0; i < STS; i++)
			out[i] = acc[i];
		memmove(acc, acc+STS, FS*sizeof(float));
		rIdx = 0;
	}

	outputs[OUT].setVoltage(std::tanh(params[GAIN_PARAM].getValue() * out[rIdx]) * 5.0f);
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

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			EMILEDisplay *display = new EMILEDisplay();
			display->module = module;
			display->box.pos = Vec(5, 30);
			display->box.size = Vec(125, 130);
			addChild(display);
		}

    const int xlightsAnchor = 8;
    const int ylightsAnchor = 170;
    const float xlightsOffset = 33.5;
    const int olights = 6;
    const int inputYOffset = 25;

    addParam(createParam<LEDButton>(Vec(xlightsAnchor, ylightsAnchor), module, EMILE::R_PARAM));
		addChild(createLight<SmallLight<RedLight>>(Vec(xlightsAnchor+olights, ylightsAnchor+olights), module, EMILE::R_LIGTH));
    addParam(createParam<LEDButton>(Vec(xlightsAnchor+xlightsOffset, ylightsAnchor), module, EMILE::G_PARAM));
		addChild(createLight<SmallLight<GreenLight>>(Vec(xlightsAnchor+xlightsOffset+olights, ylightsAnchor+olights), module, EMILE::G_LIGTH));
    addParam(createParam<LEDButton>(Vec(xlightsAnchor+2*xlightsOffset, ylightsAnchor), module, EMILE::B_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(xlightsAnchor+2*xlightsOffset+olights, ylightsAnchor+olights), module, EMILE::B_LIGTH));
    addParam(createParam<LEDButton>(Vec(xlightsAnchor+3*xlightsOffset, ylightsAnchor), module, EMILE::A_PARAM));
		addChild(createLight<SmallLight<WhiteLight>>(Vec(xlightsAnchor+3*xlightsOffset+olights, ylightsAnchor+olights), module, EMILE::A_LIGTH));

    addInput(createInput<TinyPJ301MPort>(Vec(xlightsAnchor+1, ylightsAnchor+inputYOffset), module, EMILE::R_INPUT));
    addInput(createInput<TinyPJ301MPort>(Vec(xlightsAnchor+xlightsOffset+1, ylightsAnchor+inputYOffset), module, EMILE::G_INPUT));
    addInput(createInput<TinyPJ301MPort>(Vec(xlightsAnchor+2*xlightsOffset+1, ylightsAnchor+inputYOffset), module, EMILE::B_INPUT));
    addInput(createInput<TinyPJ301MPort>(Vec(xlightsAnchor+3*xlightsOffset+1, ylightsAnchor+inputYOffset), module, EMILE::A_INPUT));

    const float xPort = 7.5f;
    const float xPortOffset = 32.2f;
    const float yPort = 330.0f;

    const float xControl = 5.0f;
    const float xControlOffset = 31.3f;
    const float yControl = 280.0f;

    addParam(createParam<BidooBlueKnob>(Vec(xControl, yControl), module, EMILE::TUNE_PARAM));
    addParam(createParam<BidooBlueKnob>(Vec(xControl+xControlOffset, yControl), module, EMILE::POS_PARAM));
    addParam(createParam<BidooBlueKnob>(Vec(xControl+2*xControlOffset, yControl), module, EMILE::CURVE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(xControl+3*xControlOffset, yControl), module, EMILE::GAIN_PARAM));

    addInput(createInput<PJ301MPort>(Vec(xPort, yPort), module, EMILE::TUNE_INPUT));
    addInput(createInput<PJ301MPort>(Vec(xPort+xPortOffset, yPort), module, EMILE::POS_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPort+2*xPortOffset, yPort), module, EMILE::CURVE_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(xPort+3*xPortOffset, yPort), module, EMILE::OUT));
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
