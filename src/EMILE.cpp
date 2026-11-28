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
#include "EmileCanvas.hpp"

// An image played as sound, read in one of two ways (right-click > Image read as):
// - Harmonics (a new EMILE): the row under the position is one cycle of a waveform. Its width, cut in bands, gives the
//   amplitudes of the harmonics of the note set by V/O and TUNE (C4 at 0 V); CURVE sets how many harmonics (16 to 256).
//   The cycle is rebuilt every HOP samples by an inverse FFT, without the harmonics above Nyquist, and played by a
//   phase at the note's frequency, crossfaded from the previous cycle.
// - Spectrum (patches made before): the row is a spectrum (x is frequency on CURVE's curve, the selected channels the
//   level), resynthesized by inverse FFT with 4 overlapping frames. Each bin keeps its phase from frame to frame, so
//   partials are steady; the bins start at random phases so a bright band is a sound, not a click per frame. V/O
//   scales every frequency, so it moves the timbre rather than a pitch.

const int FS = 4096;
const int N = 4;
const int FS2 = FS / 2;
const int FS4 = FS2 / 2;
const float IFS = 1.0f / FS;
const int STS = FS/N;
const float IFS2 = 1.0f/FS2;
const int HT = 2048;            // the harmonics mode's cycle, in samples
const int HOP = 256;            // the cycle rebuilt this often (5 ms at 48 kHz)
const int MAX_HARMONICS = 256;


using namespace std;

// EmileImage (the played image) and the drawing canvas are in EmileCanvas.hpp.

// What the display shows: the image shrunk by the worker to 512 px at most (8-bit RGBA), so the UI thread neither
// decodes the PNG nor uploads a 25 MP texture.
struct EmileThumb {
	int width = 0, height = 0;
	std::vector<unsigned char> rgba;

	static constexpr unsigned kMax = 512;
	// box average of the 16-bit image (big endian: the high byte first)
	static std::shared_ptr<EmileThumb> of(const EmileImage &im) {
		auto t = std::make_shared<EmileThumb>();
		const unsigned big = std::max(im.width, im.height);
		const unsigned step = std::max(1u, (big + kMax - 1) / kMax);
		t->width = (int)((im.width + step - 1) / step);
		t->height = (int)((im.height + step - 1) / step);
		t->rgba.resize((size_t)t->width * t->height * 4);
		for (int ty = 0; ty < t->height; ty++)
			for (int tx = 0; tx < t->width; tx++) {
				unsigned sum[4] = {0, 0, 0, 0}, n = 0;
				for (unsigned y = ty * step; y < std::min(im.height, (ty + 1) * step); y++)
					for (unsigned x = tx * step; x < std::min(im.width, (tx + 1) * step); x++) {
						const unsigned char *p = &im.rgba[((size_t)y * im.width + x) * 8];
						for (int c = 0; c < 4; c++)
							sum[c] += p[2 * c];
						n++;
					}
				unsigned char *q = &t->rgba[((size_t)ty * t->width + tx) * 4];
				for (int c = 0; c < 4; c++)
					q[c] = (unsigned char)(sum[c] / std::max(1u, n));
			}
		return t;
	}
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
		R_LIGHT,
		G_LIGHT,
		B_LIGHT,
		A_LIGHT,
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
	// the worker's next job: a file to load, or the canvas to play (a copy, so the UI can draw on while it converts);
	// a new request replaces one not yet started, so only the latest drawing is converted
	enum JobKind { JOB_FILE, JOB_CANVAS };
	JobKind jobKind = JOB_FILE;
	std::vector<unsigned char> jobCanvas;
	int jobCanvasW = 0, jobCanvasH = 0;
	bool jobRequested = false;
	std::atomic<bool> quit{false};

	// UI thread only: the drawing. It is played through the worker (sendCanvas), never read by the audio thread.
	EmileCanvas canvas;        // invalid until drawing starts (or after a file is loaded)
	EmileUndo undo;
	unsigned canvasRev = 0;    // bumped with each change, so the display knows to upload it
	bool drawMode = false;
	enum Tool { TOOL_BRUSH, TOOL_ERASER, TOOL_LINE };
	int tool = TOOL_BRUSH;
	float brushSize = 8.f;
	int colour = 0;            // white, red, green, blue
	float intensity = 1.f;
	bool stroking = false, strokeLine = false;
	EmileBrush strokeBrush;
	float strokeX0 = 0.f, strokeY0 = 0.f, strokeX = 0.f, strokeY = 0.f;
	double lastSend = 0.0;
	bool drawingDirty = false; // changed since drawing.png was written
	std::atomic<bool> drawn{false};   // the played image is the drawing (saved in the patch storage)

	// UI thread: what the display shows.
	std::mutex uiMutex;
	std::string lastPath;      // under uiMutex: the file asked for (saved)
	std::string shownPath;     // under uiMutex: the file loaded, empty if none (displayed)
	std::atomic<unsigned> shownWidth{0}, shownHeight{0};
	std::shared_ptr<EmileThumb> shownThumb;   // under uiMutex: null if none
	std::atomic<unsigned> thumbGen{0};        // bumped with each new thumb, so the display knows to upload it

	// How the row is read: a new EMILE plays harmonics; a patch saved without "mode" (made before) the spectrum
	enum Mode { MODE_SPECTRUM = 0, MODE_HARMONICS = 1 };
	std::atomic<int> mode{MODE_HARMONICS};
	// harmonics mode: the cycle being left and the one being played, crossfaded over a hop
	PFFFT_Setup *tableSetup;
	float *tableIn;
	float *tableOut;
	float table[2][HT + 1] = {};   // +1: the first sample again, for the interpolation
	int tableCur = 0;
	int hIdx = HOP;
	double hPhase = 0.0;
	float harm[MAX_HARMONICS + 1] = {};

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
		configInput(POS_INPUT, "Position (bipolar: +-5 V sweeps the image around the knob)");
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
		tableSetup = pffft_new_setup(HT, PFFFT_REAL);
		tableIn = (float*)pffft_aligned_malloc(HT*sizeof(float));
		tableOut = (float*)pffft_aligned_malloc(HT*sizeof(float));
		setMode(MODE_HARMONICS);
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
		pffft_aligned_free(tableIn);
		pffft_aligned_free(tableOut);
		pffft_destroy_setup(tableSetup);
	}

	// CURVE's name follows the mode (the panel says CURVE)
	void setMode(int m) {
		mode = m;
		paramQuantities[CURVE_PARAM]->name = m == MODE_HARMONICS ? "Harmonics (16 to 256)" : "Frequency curve";
		inputInfos[CURVE_INPUT]->name = m == MODE_HARMONICS ? "Harmonics (10 V = the most)" : "Frequency curve (10 V = +0.09)";
	}

	// Harmonics mode: the cycle of the row at `pos` for the note f0, into the table not being played
	void buildCycle(unsigned pos, float f0, float sampleRate) {
		const unsigned width = img->width;
		const float t = clamp((curve - 0.01f) / 0.09f, 0.f, 1.f);
		const int bands = (int)std::round(16.f * std::pow(16.f, t));                   // 16 at CURVE's left, 256 right
		const int below = std::max(1, (int)(0.45f * sampleRate / std::max(f0, 1.f)));   // the harmonics under Nyquist
		const int kept = std::min({bands, below, MAX_HARMONICS, HT / 2 - 1});
		const int channels = std::max(1, r + g + b + a);
		const unsigned char *row = img->rgba.data() + (size_t)pos * 8 * width;
		float level = 0.f;
		for (int k = 1; k <= bands && k <= MAX_HARMONICS; k++) {
			// the band of columns for harmonic k (one column at least)
			unsigned x0 = (unsigned)((k - 1) * (double)width / bands), x1 = (unsigned)(k * (double)width / bands);
			x1 = std::min(std::max(x1, x0 + 1), width);
			double sum = 0.0;
			for (unsigned x = x0; x < x1; x++) {
				const unsigned char *p = row + x * 8;
				sum += (r ? 256 * p[0] + p[1] : 0) + (g ? 256 * p[2] + p[3] : 0) + (b ? 256 * p[4] + p[5] : 0) + (a ? 256 * p[6] + p[7] : 0);
			}
			harm[k] = x1 > x0 ? (float)(sum / (65535.0 * channels * (x1 - x0))) : 0.f;
			if (k <= kept)
				level = std::max(level, harm[k]);
		}
		// sines: harmonic k as an imaginary part in pffft's ordered real layout (DC and Nyquist in 0 and 1)
		std::memset(tableIn, 0, HT * sizeof(float));
		for (int k = 1; k <= kept; k++)
			tableIn[2 * k + 1] = -harm[k];
		pffft_transform_ordered(tableSetup, tableIn, tableOut, NULL, PFFFT_BACKWARD);
		// the peak at the brightest harmonic's level: a black row is silent, a white one full scale
		float peak = 0.f;
		for (int i = 0; i < HT; i++)
			peak = std::max(peak, std::fabs(tableOut[i]));
		const float scale = peak > 0.f ? level / peak : 0.f;
		const int next = 1 - tableCur;
		for (int i = 0; i < HT; i++)
			table[next][i] = tableOut[i] * scale;
		table[next][HT] = table[next][0];
		tableCur = next;
	}

	static float readCycle(const float *t, double phase) {
		const double x = phase * HT;
		const int i = std::min((int)x, HT - 1);
		const float frac = (float)(x - i);
		return t[i] + (t[i + 1] - t[i]) * frac;
	}

	// UI thread: load a PNG. A file asked for ends the drawing: it plays as before, and the canvas starts again from it.
	void loadImage(const std::string &path) {
		{
			// kept even if the file fails to load, so a moved file can be found again
			std::lock_guard<std::mutex> ui(uiMutex);
			lastPath = path;
		}
		drawn = false;
		dropCanvas();
		requestLoad(path);
	}

	// the file job alone, without touching what the patch remembers (the saved drawing comes back this way)
	void requestLoad(const std::string &path) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			jobPath = path;
			jobKind = JOB_FILE;
			jobRequested = true;
		}
		jobCv.notify_all();
	}

	std::string getPath() {
		std::lock_guard<std::mutex> lock(uiMutex);
		return lastPath;
	}

	std::string getShownPath() {
		std::lock_guard<std::mutex> lock(uiMutex);
		return shownPath;
	}

	std::shared_ptr<EmileThumb> getThumb() {
		std::lock_guard<std::mutex> lock(uiMutex);
		return shownThumb;
	}

	// ---- Drawing (UI thread) ----

	void dropCanvas() {
		canvas = EmileCanvas();
		undo.clear();
		stroking = false;
		canvasRev++;
	}

	// The canvas drawing starts on: the image shown, at the thumbnail's resolution (512 px at most), or a black one.
	// Once drawn on, the canvas is what plays, so a big image is replaced by its 512 px version.
	void ensureCanvas() {
		if (canvas.valid())
			return;
		std::shared_ptr<EmileThumb> t = getThumb();
		if (t && t->width > 0 && t->height > 0 && t->rgba.size() == (size_t)t->width * t->height * 4) {
			// the image's own alpha is kept, so it sounds the same until it's drawn on
			canvas.width = t->width;
			canvas.height = t->height;
			canvas.rgba = t->rgba;
		}
		else
			canvas.reset(256, 256);
		canvasRev++;
	}

	// The canvas to the worker, which makes it the played image. A copy under jobMutex: the audio thread never sees it.
	void sendCanvas() {
		if (!canvas.valid())
			return;
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			jobCanvas = canvas.rgba;
			jobCanvasW = canvas.width;
			jobCanvasH = canvas.height;
			jobKind = JOB_CANVAS;
			jobRequested = true;
		}
		jobCv.notify_all();
		lastSend = system::getTime();
	}

	// what a change of the canvas leaves to do: show it, play it, save it
	void canvasChanged() {
		canvasRev++;
		drawn = true;
		drawingDirty = true;
		sendCanvas();
	}

	void beginStroke(float x, float y, bool erase, bool line) {
		ensureCanvas();
		undo.push(canvas);
		strokeBrush.size = brushSize;
		for (int c = 0; c < 3; c++)
			strokeBrush.channel[c] = colour == 0 || colour == c + 1;
		strokeBrush.intensity = intensity;
		strokeBrush.erase = erase;
		strokeLine = line;
		strokeX0 = strokeX = x;
		strokeY0 = strokeY = y;
		stroking = true;
		emileStrokeSegment(canvas, x, y, x, y, strokeBrush);
		canvasRev++;
		drawn = true;
		drawingDirty = true;
		sendCanvas();
	}

	void moveStroke(float x, float y) {
		if (!stroking)
			return;
		if (strokeLine) {
			// the line drawn again from the canvas as the stroke found it, so it follows the mouse
			const EmileUndo::Snap *s = undo.last();
			if (s && s->width == canvas.width && s->height == canvas.height)
				canvas.rgba = s->rgba;
			emileStrokeSegment(canvas, strokeX0, strokeY0, x, y, strokeBrush);
		}
		else
			emileStrokeSegment(canvas, strokeX, strokeY, x, y, strokeBrush);
		strokeX = x;
		strokeY = y;
		canvasRev++;
		// heard while drawing, 30 times a second at most
		if (system::getTime() - lastSend >= 1.0 / 30.0)
			sendCanvas();
	}

	void endStroke() {
		if (!stroking)
			return;
		stroking = false;
		canvasChanged();
	}

	void undoStroke() {
		if (stroking || !undo.pop(canvas))
			return;
		canvasChanged();
	}

	void newCanvas(int size) {
		if (stroking)
			return;
		undo.push(canvas);
		canvas.reset(size, size);
		canvasChanged();
	}

	void clearCanvas() {
		if (stroking)
			return;
		ensureCanvas();
		undo.push(canvas);
		canvas.reset(canvas.width, canvas.height);
		canvasChanged();
	}

	// the canvas as an 8-bit RGBA PNG
	bool writeCanvasPng(const std::string &path) {
		ensureCanvas();
		std::vector<unsigned char> png;
		if (lodepng::encode(png, canvas.rgba, (unsigned)canvas.width, (unsigned)canvas.height, LCT_RGBA, 8) != 0)
			return false;
		return lodepng::save_file(png, path) == 0;
	}

	// The saved drawing back from the patch storage: 1 queued, 0 not there, -1 not added to the engine yet (onAdd will).
	int requestDrawing() {
		std::string dir;
		try {
			dir = getPatchStorageDirectory();
		}
		catch (Exception &) {
			return -1;
		}
		std::string path = rack::system::join(dir, "drawing.png");
		if (!rack::system::isFile(path))
			return 0;
		requestLoad(path);
		return 1;
	}

	// the drawing lost (a module copied without its storage): the file it was drawn over, if any
	void drawingMissing() {
		drawn = false;
		std::string last = getPath();
		if (!last.empty())
			requestLoad(last);
	}

	void onAdd(const AddEvent &e) override {
		if (drawn && requestDrawing() == 0)
			drawingMissing();
	}

	void onSave(const SaveEvent &e) override {
		if (!drawn) {
			std::string path = rack::system::join(getPatchStorageDirectory(), "drawing.png");
			if (rack::system::isFile(path))
				rack::system::remove(path);
			return;
		}
		if (!canvas.valid())
			return;   // not drawn on since the patch was loaded: the storage keeps its drawing
		std::string path = rack::system::join(createPatchStorageDirectory(), "drawing.png");
		if (drawingDirty || !rack::system::isFile(path))
			if (writeCanvasPng(path))
				drawingDirty = false;
	}

	// Worker: the new image to the audio thread through `pending`, its thumb to the display.
	void publish(EmileImage *im, std::shared_ptr<EmileThumb> thumb, const std::string &path) {
		{
			std::lock_guard<std::mutex> ui(uiMutex);
			shownPath = im ? path : "";
			shownThumb = thumb;
		}
		thumbGen++;
		shownWidth = im ? im->width : 0;
		shownHeight = im ? im->height : 0;
		while (pendingSet.load() && !quit) {
			delete retired.exchange(nullptr, std::memory_order_acq_rel);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		delete pending.exchange(im, std::memory_order_acq_rel);
		pendingSet = true;
	}

	void workerLoop() {
		std::unique_lock<std::mutex> lock(jobMutex);
		while (!quit) {
			jobCv.wait_for(lock, std::chrono::milliseconds(50));
			delete retired.exchange(nullptr, std::memory_order_acq_rel);
			if (!jobRequested || quit)
				continue;
			const JobKind kind = jobKind;
			std::string path = jobPath;
			std::vector<unsigned char> px;
			px.swap(jobCanvas);
			const int cw = jobCanvasW, ch = jobCanvasH;
			jobRequested = false;
			lock.unlock();

			if (kind == JOB_CANVAS) {
				// the drawing: 8 bits to 16, the column tables as a file load makes them; the thumb is the canvas itself
				EmileImage *im = nullptr;
				std::shared_ptr<EmileThumb> thumb;
				if (cw > 0 && ch > 0 && px.size() == (size_t)cw * ch * 4) {
					im = new EmileImage();
					emileCanvasToImage(px.data(), cw, ch, *im);
					thumb = std::make_shared<EmileThumb>();
					thumb->width = cw;
					thumb->height = ch;
					thumb->rgba.swap(px);
				}
				if (im)
					publish(im, thumb, "");
				lock.lock();
				continue;
			}

			EmileImage *im = new EmileImage();
			// 25 megapixels at most: 8 bytes a pixel once decoded
			std::vector<unsigned char> png;
			unsigned w = 0, h = 0;
			bool ok = lodepng::load_file(png, path) == 0 && !png.empty();
			if (ok) {
				lodepng::State st;
				ok = lodepng_inspect(&w, &h, &st, png.data(), png.size()) == 0 && (uint64_t)w * h <= 25000000ull;
			}
			unsigned error = ok ? lodepng::decode(im->rgba, im->width, im->height, png, LCT_RGBA, 16) : 1;
			if (error != 0 || im->width == 0 || im->height == 0) {
				delete im;
				im = nullptr;
			}
			if (im)
				emileFinishImage(*im);
			std::shared_ptr<EmileThumb> thumb = im ? EmileThumb::of(*im) : nullptr;
			publish(im, thumb, path);
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

	void onReset(const ResetEvent &e) override {
		// the params back to their defaults first, then the colour buttons
		BidooModule::onReset(e);
		r = g = b = a = true;
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "lastPath", json_string(getPath().c_str()));
		json_object_set_new(rootJ, "r", json_boolean(r));
		json_object_set_new(rootJ, "g", json_boolean(g));
		json_object_set_new(rootJ, "b", json_boolean(b));
		json_object_set_new(rootJ, "a", json_boolean(a));
		json_object_set_new(rootJ, "mode", json_integer(mode));
		// the drawing itself is drawing.png in the patch storage (onSave)
		json_object_set_new(rootJ, "drawn", json_boolean(drawn));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		// no "drawn" (made before) or not a boolean: not drawn
		const bool wasDrawn = json_is_true(json_object_get(rootJ, "drawn"));
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		const char *last = json_is_string(lastPathJ) ? json_string_value(lastPathJ) : nullptr;
		if (wasDrawn) {
			// the file drawn over is remembered; the drawing plays, from the storage once the module is in the engine
			{
				std::lock_guard<std::mutex> ui(uiMutex);
				lastPath = last ? last : "";
			}
			dropCanvas();
			drawn = true;
			if (requestDrawing() == 0)
				drawingMissing();
		}
		else if (last && last[0])
			loadImage(last);
		else {
			drawn = false;
			dropCanvas();
		}
		json_t *rJ = json_object_get(rootJ, "r");
		if (rJ) r = json_is_true(rJ);
		json_t *gJ = json_object_get(rootJ, "g");
		if (gJ) g = json_is_true(gJ);
		json_t *bJ = json_object_get(rootJ, "b");
		if (bJ) b = json_is_true(bJ);
		json_t *aJ = json_object_get(rootJ, "a");
		if (aJ) a = json_is_true(aJ);
		// saved before the harmonics mode: the spectrum, as those patches sounded
		json_t *modeJ = json_object_get(rootJ, "mode");
		setMode(json_is_integer(modeJ) ? clamp((int)json_integer_value(modeJ), 0, 1) : MODE_SPECTRUM);
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
		lights[R_LIGHT].setBrightness(r?1:0);
		lights[G_LIGHT].setBrightness(g?1:0);
		lights[B_LIGHT].setBrightness(b?1:0);
		lights[A_LIGHT].setBrightness(a?1:0);
	}

	curve = clamp(params[CURVE_PARAM].getValue() + clamp(inputs[CURVE_INPUT].getVoltage(), 0.0f, 10.0f) * 0.009f, 0.01f, 0.2f);

	if (!img) {
		samplePos = 0;
		outputs[OUT].setVoltage(0.f);
		return;
	}
	// POS's CV is bipolar around the knob: +-5 V sweeps the whole image from the middle. Worked out even when nothing
	// listens, so the display's line follows POS; always one of this image's rows.
	const unsigned width = img->width, height = img->height;
	float posCv = inputs[POS_INPUT].getVoltage();
	if (!std::isfinite(posCv))
		posCv = 0.f;
	const unsigned pos = std::min(height - 1, (unsigned)(clamp(params[POS_PARAM].getValue() + posCv * 0.1f, 0.f, 1.f) * (height - 1)));
	samplePos = pos;

	// Nobody listening: no resynthesis.
	if (!outputs[OUT].isConnected()) {
		outputs[OUT].setVoltage(0.f);
		return;
	}

	if (mode.load(std::memory_order_relaxed) == MODE_HARMONICS) {
		const float f0 = dsp::FREQ_C4 * dsp::exp2_taylor5(clamp(params[TUNE_PARAM].getValue() + inputs[TUNE_INPUT].getVoltage(), -10.f, 10.f));
		if (hIdx >= HOP) {
			buildCycle(pos, f0, args.sampleRate);
			hIdx = 0;
		}
		const float fade = (float)hIdx / HOP;
		const float now = readCycle(table[tableCur], hPhase), before = readCycle(table[1 - tableCur], hPhase);
		const float v = before + (now - before) * fade;
		hPhase += f0 * args.sampleTime;
		hPhase -= std::floor(hPhase);
		hIdx++;
		outputs[OUT].setVoltage(emileSoftClip(params[GAIN_PARAM].getValue() * v) * 5.0f);
		return;
	}

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
		if (img->shapeCurve != curve && img->shape.size() == width && img->logBase.size() == width) {
			// (1 - x / width)^curve as exp(curve * log(...)), the log kept from the load
			for (unsigned x = 0; x < width; x++)
				img->shape[x] = 1.0f - std::exp(curve * img->logBase[x]);
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
	unsigned gen = ~0u;   // the thumb generation uploaded, ~0: none yet
	int img = 0;
	// the canvas while drawing: a texture of its size, updated in place
	int canvasImg = 0, canvasW = 0, canvasH = 0;
	unsigned canvasRev = ~0u;
	bool hovering = false;
	Vec mouse;            // where the brush is, in the display's coordinates

	EMILEDisplay() {

	}

	// The textures are the window's: freed with the widget, or the GPU keeps
	// them for each module deleted.
	~EMILEDisplay() {
		if (APP && APP->window && APP->window->vg) {
			if (img)
				nvgDeleteImage(APP->window->vg, img);
			if (canvasImg)
				nvgDeleteImage(APP->window->vg, canvasImg);
		}
	}

	// The context goes away (window recreated): its textures with it, so the
	// image is loaded again on the next draw.
	void onContextDestroy(const ContextDestroyEvent &e) override {
		if (img)
			nvgDeleteImage(e.vg, img);
		if (canvasImg)
			nvgDeleteImage(e.vg, canvasImg);
		img = canvasImg = 0;
		gen = canvasRev = ~0u;
		OpaqueWidget::onContextDestroy(e);
	}

	bool drawing() const {
		return module && module->drawMode;
	}

	// the display's point on the canvas (stretched to the box, as it is shown)
	void toCanvas(Vec p, float &x, float &y) const {
		x = p.x / width * module->canvas.width;
		y = p.y / height * module->canvas.height;
	}

	// the mouse in the display's coordinates, also while it drags outside
	Vec mouseHere() {
		return APP->scene->getMousePos().minus(getAbsoluteOffset(Vec(0, 0))).div(getAbsoluteZoom());
	}

	void onButton(const ButtonEvent &e) override {
		if (!drawing()) {
			OpaqueWidget::onButton(e);
			return;
		}
		// left paints with the tool, right erases (so the menu opens from the rest of the panel in draw mode)
		if (e.action == GLFW_PRESS && (e.button == GLFW_MOUSE_BUTTON_LEFT || e.button == GLFW_MOUSE_BUTTON_RIGHT)) {
			const bool right = e.button == GLFW_MOUSE_BUTTON_RIGHT;
			const bool shift = (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT;
			const int tool = module->tool;
			module->ensureCanvas();
			float x, y;
			toCanvas(e.pos, x, y);
			module->beginStroke(x, y, right || tool == EMILE::TOOL_ERASER,
				tool == EMILE::TOOL_LINE || (shift && tool == EMILE::TOOL_BRUSH));
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
			toCanvas(mouse, x, y);
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

	// Ctrl+Z over the display in draw mode undoes a stroke (elsewhere it stays Rack's undo)
	void onHoverKey(const HoverKeyEvent &e) override {
		if (drawing() && (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) && e.isKeyCommand(GLFW_KEY_Z, RACK_MOD_CTRL)) {
			module->undoStroke();
			e.consume(this);
			return;
		}
		OpaqueWidget::onHoverKey(e);
	}

	// an image of w x h pixels stretched to the box, and the line at the played row (inside the box)
	void drawImage(NVGcontext *vg, int tex, float w, float h) {
		nvgSave(vg);
		nvgScale(vg, width / w, height / h);
		nvgBeginPath(vg);
		NVGpaint imgPaint = nvgImagePattern(vg, 0, 0, w, h, 0, tex, 1.0f);
		nvgRect(vg, 0, 0, w, h);
		nvgFillPaint(vg, imgPaint);
		nvgFill(vg);
		nvgRestore(vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && module) {
			nvgSave(args.vg);
			nvgScissor(args.vg, 0, 0, width, height);
			bool shown = false;
			if (drawing() && module->canvas.valid()) {
				// drawing: the canvas itself, so a stroke shows at once
				const EmileCanvas &c = module->canvas;
				if (!canvasImg || canvasW != c.width || canvasH != c.height) {
					if (canvasImg)
						nvgDeleteImage(args.vg, canvasImg);
					canvasImg = nvgCreateImageRGBA(args.vg, c.width, c.height, 0, c.rgba.data());
					canvasW = c.width;
					canvasH = c.height;
					canvasRev = module->canvasRev;
				}
				else if (canvasRev != module->canvasRev) {
					nvgUpdateImage(args.vg, canvasImg, c.rgba.data());
					canvasRev = module->canvasRev;
				}
				if (canvasImg) {
					drawImage(args.vg, canvasImg, c.width, c.height);
					shown = true;
				}
			}
			else {
				// the worker's thumbnail: only an upload here, no file read or decode on the UI thread
				const unsigned g = module->thumbGen.load();
				if (g != gen) {
					if (img)
						nvgDeleteImage(args.vg, img);
					std::shared_ptr<EmileThumb> t = module->getThumb();
					img = t ? nvgCreateImageRGBA(args.vg, t->width, t->height, 0, t->rgba.data()) : 0;
					gen = g;
				}
				float w = module->shownWidth, h = module->shownHeight;
				if (img && w > 0 && h > 0) {
					drawImage(args.vg, img, w, h);
					shown = true;
				}
			}
			if (shown) {
				// the played row as a fraction of the image shown: right after an image of another height arrives,
				// the row may still be the old image's, so the fraction is held inside the screen
				const float rows = std::max(1.f, (float)module->shownHeight - 1.f);
				const float y = clamp((float)module->samplePos / rows, 0.f, 1.f) * height;
				nvgStrokeColor(args.vg, LIGHTBLUE_BIDOO);
				nvgBeginPath(args.vg);
				nvgStrokeWidth(args.vg, 1.5f);
				nvgMoveTo(args.vg, 0, y);
				nvgLineTo(args.vg, width, y);
				nvgStroke(args.vg);
			}
			// the brush under the mouse (its size on the canvas, stretched as the canvas is)
			if (drawing() && (hovering || module->stroking)) {
				// before the first stroke: the size the canvas will have (the thumb's, or 256 x 256)
				float cw = 256.f, ch = 256.f;
				if (module->canvas.valid()) {
					cw = module->canvas.width;
					ch = module->canvas.height;
				}
				else if (std::shared_ptr<EmileThumb> t = module->getThumb()) {
					cw = std::max(1, t->width);
					ch = std::max(1, t->height);
				}
				const float r = std::max(0.5f, 0.5f * module->brushSize);
				nvgBeginPath(args.vg);
				nvgEllipse(args.vg, mouse.x, mouse.y, std::max(1.f, r * width / cw), std::max(1.f, r * height / ch));
				nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 200));
				nvgStrokeWidth(args.vg, 1.f);
				nvgStroke(args.vg);
			}
			nvgRestore(args.vg);
		}
		Widget::drawLayer(args, layer);
	}

};

struct EMILEWidget : BidooWidget {
	EMILEWidget(EMILE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/EMILE.svg"));

		theme::addScrews(this);

		// The panel (tools/panels/emile.py): the picture screen; four colour buttons, each joined by a line to its CV jack;
		// four knobs under their labels; four boxed jacks, all in the same four columns: one box per jack between the
		// screen's edges, the theme's gap between them (kBoxLeft, kBoxRight: BOX_LEFT, BOX_RIGHT in emile.py). The
		// buttons are BidooRoundButtons lit by their colour's state, with its name above: they sit 8 px lower than the old
		// LEDButtons to leave the name room under the screen, the CV jacks are full jacks 10.5 px lower to clear them.
		EMILEDisplay *display = new EMILEDisplay();
		display->module = module;
		display->box = theme::specificRect(5, 30, 125, 130);
		addChild(display);

		const float w = box.size.x;
		const float kBoxLeft = 5.f, kBoxRight = w - 5.f;   // the display's edges
		const int n = 4;
		auto column = [&](int i) {
			const float bw = (kBoxRight - kBoxLeft - (n - 1) * theme::kBoxGap) / n;
			return kBoxLeft + 0.5f * bw + i * (bw + theme::kBoxGap);
		};
		const float kButtonY = 187.f, kCvY = 213.f;   // BUTTON_Y, CV_Y in emile.py
		const int buttonIds[4] = {EMILE::R_PARAM, EMILE::G_PARAM, EMILE::B_PARAM, EMILE::A_PARAM};
		const int lightIds[4] = {EMILE::R_LIGHT, EMILE::G_LIGHT, EMILE::B_LIGHT, EMILE::A_LIGHT};
		const int cvIds[4] = {EMILE::R_INPUT, EMILE::G_INPUT, EMILE::B_INPUT, EMILE::A_INPUT};
		const int knobIds[4] = {EMILE::TUNE_PARAM, EMILE::POS_PARAM, EMILE::CURVE_PARAM, EMILE::GAIN_PARAM};
		for (int i = 0; i < n; i++) {
			BidooRoundButton *b = theme::param<BidooRoundButton>(theme::specific(column(i), kButtonY), module, buttonIds[i]);
			b->momentary = true;
			const int lightId = lightIds[i];
			b->litBy = [=]() { return module && module->lights[lightId].getBrightness() > 0.5f; };
			addParam(b);
			addInput(theme::input<PJ301MPort>(theme::specific(column(i), kCvY), module, cvIds[i]));
			addParam(theme::param<BidooBlueKnob>(theme::specific(column(i), theme::row(1)), module, knobIds[i]));
		}
		addInput(theme::input<PJ301MPort>(theme::specific(column(0), theme::row(0)), module, EMILE::TUNE_INPUT));
		addInput(theme::input<PJ301MPort>(theme::specific(column(1), theme::row(0)), module, EMILE::POS_INPUT));
		addInput(theme::input<PJ301MPort>(theme::specific(column(2), theme::row(0)), module, EMILE::CURVE_INPUT));
		addOutput(theme::output<PJ301MPort>(theme::specific(column(3), theme::row(0)), module, EMILE::OUT));
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
		menu->addChild(createMenuItem("Save drawing as PNG...", "", [=]() {
			std::string last = module->getPath();
			std::string dir = last.empty() ? asset::user("") : rack::system::getDirectory(last);
			char *path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), "drawing.png", NULL);
			if (path) {
				std::string p = path;
				free(path);
				if (rack::system::getExtension(p).empty())
					p += ".png";
				module->writeCanvasPng(p);
			}
		}, !module->canvas.valid() && !module->getThumb()));
		menu->addChild(createIndexSubmenuItem("Image read as",
			{"Spectrum: each column a fixed frequency", "Harmonics of the V/O note: a waveform"},
			[=]() { return (size_t)module->mode.load(); },
			[=](size_t m) { module->setMode((int)m); }));

		// drawing on the screen: left paints with the tool, right erases, Shift+drag a line with the brush, Ctrl+Z undoes
		menu->addChild(new MenuSeparator());
		menu->addChild(createBoolPtrMenuItem("Draw on the screen", "", &module->drawMode));
		menu->addChild(createIndexSubmenuItem("Tool", {"Brush", "Eraser", "Line"},
			[=]() { return (size_t)module->tool; },
			[=](size_t t) { module->tool = (int)t; }));
		static const float sizes[] = {1.f, 2.f, 4.f, 8.f, 16.f, 32.f};
		menu->addChild(createIndexSubmenuItem("Brush size", {"1 px", "2 px", "4 px", "8 px", "16 px", "32 px"},
			[=]() {
				for (size_t i = 0; i < 6; i++)
					if (module->brushSize == sizes[i])
						return i;
				return (size_t)3;
			},
			[=](size_t i) { module->brushSize = sizes[i]; }));
		menu->addChild(createIndexSubmenuItem("Colour", {"White", "Red", "Green", "Blue"},
			[=]() { return (size_t)module->colour; },
			[=](size_t c) { module->colour = (int)c; }));
		static const float levels[] = {0.25f, 0.5f, 0.75f, 1.f};
		menu->addChild(createIndexSubmenuItem("Intensity", {"25 %", "50 %", "75 %", "100 %"},
			[=]() {
				for (size_t i = 0; i < 4; i++)
					if (module->intensity == levels[i])
						return i;
				return (size_t)3;
			},
			[=](size_t i) { module->intensity = levels[i]; }));
		menu->addChild(createSubmenuItem("New canvas", "", [=](Menu *sub) {
			for (int n : {64, 128, 256, 512})
				sub->addChild(createMenuItem(rack::string::f("%d x %d", n, n), "", [=]() { module->newCanvas(n); }));
		}));
		menu->addChild(createMenuItem("Clear canvas", "", [=]() { module->clearCanvas(); }));
		menu->addChild(createMenuItem("Undo stroke", RACK_MOD_CTRL_NAME "+Z", [=]() { module->undoStroke(); }, module->undo.empty()));
	}
};

Model *modelEMILE = createModel<EMILE, EMILEWidget>("EMILE");
