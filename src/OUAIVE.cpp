#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "osdialog.h"
#include <vector>
#include "cmath"
#include <iomanip>
#include <sstream>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <thread>
#include "dep/waves.hpp"
#include "SampleRead.hpp"

using namespace std;

// A loaded sample, resampled to the engine rate, with a small overview for
// the display.
struct OuaiveSample {
	static constexpr int OVERVIEW = 1024;
	std::vector<dsp::Frame<2>> frames;
	sampleread::Mip<2> mip;  // band-limited copies, for playing faster than 1x
	int channels = 0;
	int count = 0;
	float sampleRate = 0.f;
	float overviewMin[2][OVERVIEW] = {};
	float overviewMax[2][OVERVIEW] = {};

	void buildOverview() {
		for (int b = 0; b < OVERVIEW; b++) {
			int i0 = (int)((long long)b * count / OVERVIEW), i1 = std::max((int)((long long)(b + 1) * count / OVERVIEW), i0 + 1);
			for (int c = 0; c < 2; c++) {
				float lo = 0.f, hi = 0.f;
				for (int i = i0; i < i1 && i < count; i++) {
					lo = std::min(lo, frames[i].samples[c]);
					hi = std::max(hi, frames[i].samples[c]);
				}
				overviewMin[c][b] = lo;
				overviewMax[c][b] = hi;
			}
		}
	}
};

struct OUAIVE : BidooModule {
	enum ParamIds {
		NB_SLICES_PARAM,
		TRIG_MODE_PARAM,
		READ_MODE_PARAM,
		SPEED_PARAM,
		CVSLICES_PARAM,
		CVSPEED_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		GATE_INPUT,
		POS_INPUT,
		NB_SLICES_INPUT,
		READ_MODE_INPUT,
		SPEED_INPUT,
		POS_RESET_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		EOC_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	bool play = false;
	double samplePos = 0.0;  // double: a float loses the pitch far into long samples
	std::string lastPath;
	int trigMode = 0; // 0 trig, 1 gate, 2 sliced
	int sliceIndex = -1;
	int sliceLength = 0;
	int nbSlices = 1;
	int readMode = 0; // 0 forward, 1 backward, 2 repeat
	float speed = 1.0f;
	bool gateWasHigh = false;
	dsp::SchmittTrigger playTrigger;
	dsp::SchmittTrigger trigModeTrigger;
	dsp::SchmittTrigger readModeTrigger;
	dsp::SchmittTrigger posResetTrigger;
	int eoc = 0;
	dsp::PulseGenerator eocPulse;
	float speedExp = 0.f;     // the speed in octaves, exp2 only when it moves
	float scrubPos = 0.f;     // gate mode with a position CV: the CV, smoothed
	sampleread::Declick<2> dc;
	float lastStep = 0.f, lastGain = 0.f;  // of the last sample played
	double lastPos = 0.0;

	// Audio thread: the sample it plays. The worker loads new ones into
	// `pending`; the audio thread takes it and moves its old one to `retired`,
	// which the worker frees.
	OuaiveSample *sample = nullptr;
	std::atomic<OuaiveSample*> pending{nullptr};
	std::atomic<OuaiveSample*> retired{nullptr};

	// Worker thread: loads the file asked for, at the rate asked for.
	std::thread worker;
	std::mutex jobMutex;
	std::condition_variable jobCv;
	std::string jobPath;
	float jobRate = 0.f;
	bool jobRequested = false;
	std::atomic<bool> quit{false};
	std::atomic<float> reloadRate{0.f};  // set by the audio thread when the engine rate changes

	// UI thread: what the display shows, copied by the worker.
	std::mutex uiMutex;
	std::shared_ptr<OuaiveSample> uiSample;
	std::string waveFileName;
	std::atomic<bool> loading{false};

	OUAIVE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(TRIG_MODE_PARAM, "Trigger mode (trig, gate, slice)");
		configButton(READ_MODE_PARAM, "Read mode (forward, backward, repeat)");
		configParam(NB_SLICES_PARAM, 1.0f, 128.0f, 1.0f, "Slices");
		paramQuantities[NB_SLICES_PARAM]->snapEnabled = true;
		configParam(CVSLICES_PARAM, -1.0f, 1.0f, 0.0f, "Slices CV amount", "%", 0.f, 100.f);
		configParam(SPEED_PARAM, -4.0f, 4.0f, 0.0f, "Speed", "x", 2.0f);
		configParam(CVSPEED_PARAM, -1.0f, 1.0f, 0.0f, "Speed CV amount (100% = 1V/oct)", "%", 0.f, 100.f);
		configInput(GATE_INPUT, "Trigger / gate");
		configInput(POS_INPUT, "Position (0-10 V = start-end, or the slice)");
		configInput(NB_SLICES_INPUT, "Slices CV");
		configInput(READ_MODE_INPUT, "Read mode (0, 5, 10 V)");
		configInput(SPEED_INPUT, "Speed CV (1V/oct)");
		configInput(POS_RESET_INPUT, "Position reset");
		configOutput(OUTL_OUTPUT, "Left");
		configOutput(OUTR_OUTPUT, "Right");
		configOutput(EOC_OUTPUT, "End of cycle");
		worker = std::thread(&OUAIVE::workerLoop, this);
	}

	~OUAIVE() {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			quit = true;
		}
		jobCv.notify_all();
		if (worker.joinable())
			worker.join();
		delete pending.load();
		delete retired.load();
		delete sample;
	}

	float engineRate() {
		return (APP && APP->engine) ? APP->engine->getSampleRate() : 48000.f;
	}

	// Any thread but the audio thread.
	void requestLoad(const std::string &path, float rate) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			jobPath = path;
			jobRate = rate;
			jobRequested = true;
		}
		loading = true;
		jobCv.notify_all();
	}

	void workerLoop() {
		std::unique_lock<std::mutex> lock(jobMutex);
		while (!quit) {
			// The audio thread cannot notify without locking: poll for its requests.
			jobCv.wait_for(lock, std::chrono::milliseconds(50));
			delete retired.exchange(nullptr, std::memory_order_acq_rel);
			float rr = reloadRate.exchange(0.f);
			if (rr > 0.f && !jobRequested && !lastPath.empty()) {
				jobPath = lastPath;
				jobRate = rr;
				jobRequested = true;
			}
			if (!jobRequested || quit)
				continue;
			std::string path = jobPath;
			float rate = jobRate;
			jobRequested = false;
			lock.unlock();

			OuaiveSample *s = new OuaiveSample();
			std::string name, ext;
			int sr = 0;
			s->frames = waves::getStereoWav(path, rate, name, ext, s->channels, sr, s->count);
			s->count = s->frames.size();
			s->sampleRate = rate;
			s->mip.build((const float *)s->frames.data(), s->count);
			s->buildOverview();
			{
				std::lock_guard<std::mutex> ui(uiMutex);
				auto shown = std::make_shared<OuaiveSample>();
				shown->frames = s->frames; // the display reads them when zoomed in
				shown->count = s->count;
				shown->channels = s->channels;
				std::copy(&s->overviewMin[0][0], &s->overviewMin[0][0] + 2 * OuaiveSample::OVERVIEW, &shown->overviewMin[0][0]);
				std::copy(&s->overviewMax[0][0], &s->overviewMax[0][0] + 2 * OuaiveSample::OVERVIEW, &shown->overviewMax[0][0]);
				uiSample = shown;
				waveFileName = name;
			}
			// Wait until the audio thread has taken the previous one.
			while (pending.load() && !quit)
				std::this_thread::sleep_for(std::chrono::milliseconds(5));
			delete pending.exchange(s, std::memory_order_acq_rel);
			loading = false;
			lock.lock();
		}
	}

	void process(const ProcessArgs &args) override;

	// Takes a newly loaded sample, once the worker has freed the previous one.
	void takePending() {
		if (pending.load(std::memory_order_relaxed) && !retired.load(std::memory_order_acquire)) {
			OuaiveSample *s = pending.exchange(nullptr, std::memory_order_acq_rel);
			if (s) {
				retired.store(sample, std::memory_order_release);
				sample = s;
				samplePos = 0.f;
				play = false;
				dc.reset();
			}
		}
	}

	// Bypassed: still takes the loaded sample, so the loader never waits.
	void processBypass(const ProcessArgs &args) override {
		takePending();
		Module::processBypass(args);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		json_object_set_new(rootJ, "trigMode", json_integer(trigMode));
		json_object_set_new(rootJ, "readMode", json_integer(readMode));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		if (json_t *j = json_object_get(rootJ, "trigMode")) trigMode = clamp((int)json_integer_value(j), 0, 2);
		if (json_t *j = json_object_get(rootJ, "readMode")) readMode = clamp((int)json_integer_value(j), 0, 2);
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ) {
			std::string p = json_string_value(lastPathJ);
			if (!p.empty())
				loadFile(p);
		}
	}

	// UI thread: load a file.
	void loadFile(const std::string &path) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			lastPath = path;
		}
		sliceIndex = -1;
		requestLoad(path, engineRate());
	}
};

void OUAIVE::process(const ProcessArgs &args) {
	takePending();
	// The engine rate changed: load the file again at the new rate.
	if (sample && sample->count > 0 && sample->sampleRate != args.sampleRate && !loading)
		reloadRate = args.sampleRate;

	const int total = sample ? sample->count : 0;
	const int fadeLen = sampleread::declickLength(args.sampleRate);
	sampleread::View<2> view;
	if (sample)
		view = {(const float *)sample->frames.data(), total, &sample->mip};

	if (trigModeTrigger.process(params[TRIG_MODE_PARAM].getValue()))
		trigMode = (trigMode + 1) % 3;
	if (inputs[READ_MODE_INPUT].isConnected())
		readMode = clamp((int)std::round(inputs[READ_MODE_INPUT].getVoltage() * 0.2f), 0, 2);
	else if (readModeTrigger.process(params[READ_MODE_PARAM].getValue()))
		readMode = (readMode + 1) % 3;
	nbSlices = clamp((int)std::round(params[NB_SLICES_PARAM].getValue() + params[CVSLICES_PARAM].getValue() * inputs[NB_SLICES_INPUT].getVoltage()), 1, 128);
	float e = params[SPEED_PARAM].getValue() + params[CVSPEED_PARAM].getValue() * inputs[SPEED_INPUT].getVoltage();
	if (e != speedExp) {
		speedExp = e;
		speed = clamp(std::exp2(e), 1.f / 16.f, 16.f);
	}
	sliceLength = std::max(total / nbSlices, 1);

	// Declicked restarts and stops: the old head fades out over 2 ms while
	// the new one fades in.
	auto restart = [&](double pos) {
		if (play)
			dc.tail(samplePos, lastStep, lastGain, fadeLen);
		samplePos = pos;
		play = true;
		dc.fadeIn(fadeLen);
	};
	auto stop = [&]() {
		if (play)
			dc.tail(samplePos, lastStep, lastGain, fadeLen);
		play = false;
	};

	// Reset stops playback (the only way to stop a repeating sound) and rewinds
	// the slices so the next trigger, even on this same sample, plays slice 1.
	if (posResetTrigger.process(inputs[POS_RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
		stop();
		sliceIndex = -1;
		samplePos = 0.0f;
	}

	float posCv = clamp(inputs[POS_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
	bool trig = playTrigger.process(inputs[GATE_INPUT].getVoltage(), 0.1f, 1.f);
	bool gate = playTrigger.isHigh();
	bool scrub = (trigMode == 1) && inputs[POS_INPUT].isConnected();
	if (trigMode == 0 && trig) {
		if (inputs[POS_INPUT].isConnected())
			restart(posCv * (total - 1.0f));
		else
			restart((readMode != 1) ? 0.0f : total - 1.0f);
	}
	else if (trigMode == 1) {
		// Gate: plays while the gate is high. With a position CV it follows the
		// CV (scrubbing, smoothed over about 5 ms); without one it plays from
		// the start.
		if (scrub) {
			float target = posCv * (total - 1.0f);
			if (gate && !play) {
				scrubPos = target;
				restart(target);
			}
			else if (!gate)
				stop();
			scrubPos += (target - scrubPos) * std::min(1.f, 200.f * args.sampleTime);
			if (play)
				samplePos = scrubPos;
		}
		else if (gate && !gateWasHigh)
			restart((readMode != 1) ? 0.0f : total - 1.0f);
		else if (!gate)
			stop();
	}
	else if (trigMode == 2 && trig) {
		if (inputs[POS_INPUT].isConnected())
			sliceIndex = clamp((int)(posCv * nbSlices), 0, nbSlices - 1);
		else
			sliceIndex = (sliceIndex + 1) % nbSlices;
		restart(clamp((float)((readMode != 1) ? sliceIndex * sliceLength : (sliceIndex + 1) * sliceLength - 1), 0.0f, total - 1.0f));
	}
	gateWasHigh = gate;

	float acc[2] = {0.f, 0.f};
	bool tailDone = false;
	if (play && total > 0 && samplePos >= 0 && samplePos < total) {
		float step = scrub ? (float)(samplePos - lastPos) : ((readMode != 1) ? speed : -speed);
		float g = dc.nextIn();
		float sliceStart = 0.f, sliceEnd = (float)total;
		if (trigMode == 2) {
			sliceStart = sliceIndex * sliceLength;
			sliceEnd = std::min((float)((sliceIndex + 1) * sliceLength), (float)total);
		}
		if (!scrub && readMode != 2) {
			// Fades out just before the end it stops at.
			float left = (float)((readMode == 0) ? (sliceEnd - samplePos) : (samplePos - sliceStart)) / speed;
			if (left < fadeLen)
				g *= std::max(left, 0.f) / fadeLen;
		}
		view.read(samplePos, std::fabs(step), acc);
		acc[0] *= g;
		acc[1] *= g;
		lastGain = g;
		lastStep = step;
		// The tail before the jumps below: one started there plays from the
		// next sample.
		dc.addTail(view, acc);
		tailDone = true;

		if (!scrub) {
			samplePos += step;
			if (trigMode == 2) {
				if (readMode == 0 && samplePos >= sliceEnd)
					play = false;
				else if (readMode == 1 && samplePos < sliceStart)
					play = false;
				else if (readMode == 2 && samplePos >= sliceEnd) {
					dc.tail(samplePos, step, g, fadeLen);
					samplePos = sliceStart;
					dc.fadeIn(fadeLen);
				}
			}
			else {
				if (readMode == 0 && samplePos >= total)
					play = false;
				else if (readMode == 1 && samplePos < 0)
					play = false;
				else if (readMode == 2 && samplePos >= total) {
					dc.tail(samplePos, step, g, fadeLen);
					samplePos = inputs[POS_INPUT].isConnected() ? posCv * (total - 1.0f) : 0.0f;
					dc.fadeIn(fadeLen);
				}
			}
		}
	}
	else if (!(trigMode == 1 && inputs[POS_INPUT].isConnected() && total > 0))
		play = false;
	lastPos = samplePos;
	if (sample && !tailDone)
		dc.addTail(view, acc);
	float l = acc[0], r = acc[1];
	if (sample && sample->channels >= 2 && !(outputs[OUTL_OUTPUT].isConnected() && outputs[OUTR_OUTPUT].isConnected()))
		l = r = 0.5f * (l + r);
	outputs[OUTL_OUTPUT].setVoltage(5.0f * l);
	outputs[OUTR_OUTPUT].setVoltage(5.0f * r);

	if (eoc == 0 && play)
		eoc = 1;
	if (eoc == 1 && !play) {
		eoc = 0;
		eocPulse.trigger(1e-3f);
	}
	outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime) ? 10.f : 0.f);
}

struct OUAIVEDisplay : OpaqueWidget {
	OUAIVE *module;
	const float width = 125.0f;
	const float height = 50.0f;
	float zoomWidth = 125.0f;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refX = 0.0f;

	OUAIVEDisplay() {

	}

	// Zoom anchors at the click point, as in cANARd.
	void onButton(const event::Button &e) override {
		refX = e.pos.x;
		OpaqueWidget::onButton(e);
	}

  void onDragStart(const event::DragStart &e) override {
		APP->window->cursorLock();
		OpaqueWidget::onDragStart(e);
	}

  void onDragMove(const event::DragMove &e) override {
		float zoom = 1.0f;
    if (e.mouseDelta.y > 0.0f) {
      zoom = 1.0f/(((APP->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f);
    }
    else if (e.mouseDelta.y < 0.0f) {
      zoom = ((APP->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f;
    }
    zoomWidth = clamp(zoomWidth*zoom,width,zoomWidth*((APP->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT) ? 2.0f : 1.1f));
    zoomLeftAnchor = clamp(refX - (refX - zoomLeftAnchor)*zoom + e.mouseDelta.x, width - zoomWidth,0.0f);
		OpaqueWidget::onDragMove(e);
	}

  void onDragEnd(const event::DragEnd &e) override {
    APP->window->cursorUnlock();
    OpaqueWidget::onDragEnd(e);
  }

	void drawWave(const DrawArgs& args, const OuaiveSample &s, int c, float top) {
		Rect b = Rect(Vec(zoomLeftAnchor, top), Vec(zoomWidth, height));
		nvgScissor(args.vg, 0, b.pos.y, width, height);
		// filled: one column per pixel, from its lowest to its highest value; from the overview when
		// a pixel covers more than one of its bins, from the frames when zoomed in
		const int total = s.count, bins = OuaiveSample::OVERVIEW;
		const double perPixel = (double)total / b.size.x, binFrames = (double)total / bins;
		nvgBeginPath(args.vg);
		for (int px = 0; px < (int)width; px++) {
			const double f0 = (px - b.pos.x) * perPixel, f1 = f0 + perPixel;
			if (f1 <= 0.0 || f0 >= total)
				continue;
			float lo = 1.f, hi = -1.f;
			if (perPixel >= binFrames || (int)s.frames.size() < total) {
				const int k0 = clamp((int)(f0 / binFrames), 0, bins - 1), k1 = clamp((int)(f1 / binFrames), k0 + 1, bins);
				for (int k = k0; k < k1; k++) {
					lo = std::min(lo, s.overviewMin[c][k]);
					hi = std::max(hi, s.overviewMax[c][k]);
				}
			}
			else {
				const int j0 = clamp((int)f0, 0, total - 1), j1 = clamp((int)std::ceil(f1), j0, total - 1);
				for (int j = j0; j <= j1; j++) {
					lo = std::min(lo, s.frames[j].samples[c]);
					hi = std::max(hi, s.frames[j].samples[c]);
				}
			}
			const float yHi = b.pos.y + b.size.y * (0.5f - 0.5f * clamp(hi, -1.f, 1.f));
			const float yLo = b.pos.y + b.size.y * (0.5f - 0.5f * clamp(lo, -1.f, 1.f));
			nvgRect(args.vg, px, yHi, 1.f, std::max(yLo - yHi, 1.f));
		}
		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgFillColor(args.vg, PINK_BIDOO);
		nvgFill(args.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && module) {
			std::shared_ptr<OuaiveSample> s;
			{
				std::lock_guard<std::mutex> lock(module->uiMutex);
				s = module->uiSample;
			}
			if (s && s->count > 0) {
				size_t nbSample = s->count;

				nvgFontSize(args.vg, 14);
				nvgFillColor(args.vg, YELLOW_BIDOO);

				std::string trigMode = "";
				std::string slices = "";
				if (module->trigMode == 0) {
					trigMode = "TRIG ";
				}
				else if (module->trigMode==1)	{
					trigMode = "GATE ";
				}
				else {
					trigMode = "SLICE ";
					slices = "|" + to_string(module->nbSlices) + "|";
				}

				nvgTextBox(args.vg, 3, -15, 40, trigMode.c_str(), NULL);
				nvgTextBox(args.vg, 59, -15, 40, slices.c_str(), NULL);

				std::string readMode = "";
				if (module->readMode == 0) {
					readMode = "►";
				}
				else if (module->readMode == 2) {
					readMode = "►►";
				}
				else {
					readMode = "◄";
				}

				nvgTextBox(args.vg, 40, -15, 40, readMode.c_str(), NULL);

				stringstream stream;
				stream << fixed << setprecision(module->speed < 1.f ? 2 : 1) << module->speed;
				std::string speed = "x" + stream.str();

				nvgTextBox(args.vg, 90, -15, 40, speed.c_str(), NULL);

				//Draw play line
				if (module->play) {
					nvgStrokeColor(args.vg, LIGHTBLUE_BIDOO);
					nvgBeginPath(args.vg);
					nvgStrokeWidth(args.vg, 2);
					float x = module->samplePos * zoomWidth / nbSample + zoomLeftAnchor;
					nvgMoveTo(args.vg, x, 0);
					nvgLineTo(args.vg, x, 2 * height+10);
					nvgClosePath(args.vg);
					nvgStroke(args.vg);
				}

				//Draw ref line
				nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
				nvgStrokeWidth(args.vg, 1);
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0, height * 0.5f);
				nvgLineTo(args.vg, width, height * 0.5f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);

				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0, 3*height * 0.5f + 10);
				nvgLineTo(args.vg, width, 3*height * 0.5f + 10);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);

				//Draw waveform
				nvgStrokeColor(args.vg, PINK_BIDOO);
				nvgSave(args.vg);
				drawWave(args, *s, 0, 0);
				drawWave(args, *s, 1, height + 10);
				nvgResetScissor(args.vg);

				//draw slices
				if (module->trigMode == 2) {
					nvgScissor(args.vg, 0, 0, width, 2*height+10);
					for (int i = 1; i < module->nbSlices; i++) {
						nvgStrokeColor(args.vg, YELLOW_BIDOO);
						nvgStrokeWidth(args.vg, 1);
						nvgBeginPath(args.vg);
						nvgMoveTo(args.vg, (int)(i * module->sliceLength * zoomWidth / nbSample + zoomLeftAnchor) , 0);
						nvgLineTo(args.vg, (int)(i * module->sliceLength * zoomWidth / nbSample + zoomLeftAnchor) , 2*height+10);
						nvgClosePath(args.vg);
						nvgStroke(args.vg);
					}
					nvgResetScissor(args.vg);
				}

				nvgRestore(args.vg);
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct OUAIVEWidget : BidooWidget {
	OUAIVEWidget(OUAIVE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/OUAIVE.svg"));

		theme::addScrews(this);

		// The original hand-drawn panel: no control sits on the grid, so each position is either specific (the old
		// top left corner, kept to the pixel) or relative to one of those. See docs/THEME.md, "Placing controls in code".
		OUAIVEDisplay *display = new OUAIVEDisplay();
		display->module = module;
		display->box = theme::specificRect(5, 70, 125, 110);
		addChild(display);

		// Top: position reset in on the left, end of cycle out on the right
		const theme::Pos reset = theme::specificCorner(10, 18, theme::kTinyJackSize);
		const theme::Pos eoc = theme::specificCorner(112, 18, theme::kTinyJackSize);
		addInput(theme::input<TinyPJ301MPort>(reset, module, OUAIVE::POS_RESET_INPUT));
		addOutput(theme::output<TinyPJ301MPort>(eoc, module, OUAIVE::EOC_OUTPUT));

		// Mode buttons, the read mode's CV jack on the right; below, one row each for slices and speed:
		// the knob, its CV amount, its CV jack in the same column as the read mode's
		const float kCvAmountRight = 24.f;   // from a knob to its CV trimpot
		const float kSpeedBelow = 25.f;      // from the slices row to the speed row
		const theme::Pos trigMode = theme::specificCorner(9, 215, theme::kCKD6Size);
		const theme::Pos readMode = trigMode.right(44.f);
		const theme::Pos readModeCv = theme::specificCorner(106, 222, theme::kTinyJackSize);
		const theme::Pos slices = theme::specificCorner(58, 250, theme::kTrimpotSize);
		const theme::Pos slicesCv = readModeCv.down(30.f);
		const theme::Pos speed = slices.down(kSpeedBelow);
		const theme::Pos speedCv = slicesCv.down(kSpeedBelow);
		addParam(theme::param<BlueCKD6>(trigMode, module, OUAIVE::TRIG_MODE_PARAM));
		addParam(theme::param<BlueCKD6>(readMode, module, OUAIVE::READ_MODE_PARAM));
		addInput(theme::input<TinyPJ301MPort>(readModeCv, module, OUAIVE::READ_MODE_INPUT));
		addParam(theme::param<BidooBlueTrimpot>(slices, module, OUAIVE::NB_SLICES_PARAM));
		addParam(theme::param<BidooBlueTrimpot>(slices.right(kCvAmountRight), module, OUAIVE::CVSLICES_PARAM));
		addInput(theme::input<TinyPJ301MPort>(slicesCv, module, OUAIVE::NB_SLICES_INPUT));
		addParam(theme::param<BidooBlueTrimpot>(speed, module, OUAIVE::SPEED_PARAM));
		addParam(theme::param<BidooBlueTrimpot>(speed.right(kCvAmountRight), module, OUAIVE::CVSPEED_PARAM));
		addInput(theme::input<TinyPJ301MPort>(speedCv, module, OUAIVE::SPEED_INPUT));

		// Bottom: gate and position in, the stereo out on the right, R under the end of cycle out
		const theme::Pos gate = theme::specificCorner(7, 330, theme::kJackSize);
		const theme::Pos outL = theme::specificCorner(90, 340, theme::kTinyJackSize);
		addInput(theme::input<PJ301MPort>(gate, module, OUAIVE::GATE_INPUT));
		addInput(theme::input<PJ301MPort>(gate.right(33.f), module, OUAIVE::POS_INPUT));
		addOutput(theme::output<TinyPJ301MPort>(outL, module, OUAIVE::OUTL_OUTPUT));
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(eoc, outL), module, OUAIVE::OUTR_OUTPUT));
	}

  struct OUAIVEItem : MenuItem {
  	OUAIVE *module;
  	void onAction(const event::Action &e) override {

  		std::string dir = module->lastPath.empty() ? asset::user("") : rack::system::getDirectory(module->lastPath);
  		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
  		if (path) {
  			module->loadFile(path);
  			free(path);
  		}
  	}
  };

  void appendContextMenu(ui::Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		OUAIVE *module = dynamic_cast<OUAIVE*>(this->module);
		if (!module)
			return;

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<OUAIVEItem>(&MenuItem::text, "Load sample", &OUAIVEItem::module, module));
	}

	void onPathDrop(const PathDropEvent& e) override {
		Widget::onPathDrop(e);
		OUAIVE *module = dynamic_cast<OUAIVE*>(this->module);
		if (module && !e.paths.empty())
			module->loadFile(e.paths[0]);
	}
};

Model *modelOUAIVE = createModel<OUAIVE, OUAIVEWidget>("OUAIve");
