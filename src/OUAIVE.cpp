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

using namespace std;

// A loaded sample, resampled to the engine rate, with a small overview for
// the display.
struct OuaiveSample {
	static constexpr int OVERVIEW = 1024;
	std::vector<dsp::Frame<2>> frames;
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
	float samplePos = 0.0f;
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
			s->buildOverview();
			{
				std::lock_guard<std::mutex> ui(uiMutex);
				auto shown = std::make_shared<OuaiveSample>();
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
	// Take a newly loaded sample, once the worker has freed the previous one.
	if (pending.load(std::memory_order_relaxed) && !retired.load(std::memory_order_acquire)) {
		OuaiveSample *s = pending.exchange(nullptr, std::memory_order_acq_rel);
		if (s) {
			retired.store(sample, std::memory_order_release);
			sample = s;
			samplePos = 0.f;
			play = false;
		}
	}
	// The engine rate changed: load the file again at the new rate.
	if (sample && sample->count > 0 && sample->sampleRate != args.sampleRate && !loading)
		reloadRate = args.sampleRate;

	const int total = sample ? sample->count : 0;

	if (trigModeTrigger.process(params[TRIG_MODE_PARAM].getValue()))
		trigMode = (trigMode + 1) % 3;
	if (inputs[READ_MODE_INPUT].isConnected())
		readMode = clamp((int)std::round(inputs[READ_MODE_INPUT].getVoltage() * 0.2f), 0, 2);
	else if (readModeTrigger.process(params[READ_MODE_PARAM].getValue()))
		readMode = (readMode + 1) % 3;
	nbSlices = clamp((int)std::round(params[NB_SLICES_PARAM].getValue() + params[CVSLICES_PARAM].getValue() * inputs[NB_SLICES_INPUT].getVoltage()), 1, 128);
	speed = clamp(std::exp2(params[SPEED_PARAM].getValue() + params[CVSPEED_PARAM].getValue() * inputs[SPEED_INPUT].getVoltage()), 1.f / 16.f, 16.f);
	sliceLength = std::max(total / nbSlices, 1);

	// Reset stops playback (the only way to stop a repeating sound) and rewinds
	// the slices so the next trigger, even on this same sample, plays slice 1.
	if (posResetTrigger.process(inputs[POS_RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
		play = false;
		sliceIndex = -1;
		samplePos = 0.0f;
	}

	float posCv = clamp(inputs[POS_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
	bool trig = playTrigger.process(inputs[GATE_INPUT].getVoltage(), 0.1f, 1.f);
	bool gate = playTrigger.isHigh();
	if (trigMode == 0 && trig) {
		play = true;
		if (inputs[POS_INPUT].isConnected())
			samplePos = posCv * (total - 1.0f);
		else
			samplePos = (readMode != 1) ? 0.0f : total - 1.0f;
	}
	else if (trigMode == 1) {
		// Gate: plays while the gate is high. With a position CV it follows the
		// CV (scrubbing); without one it plays from the start.
		if (inputs[POS_INPUT].isConnected()) {
			samplePos = posCv * (total - 1.0f);
			play = gate;
		}
		else if (gate && !gateWasHigh) {
			samplePos = (readMode != 1) ? 0.0f : total - 1.0f;
			play = true;
		}
		else if (!gate)
			play = false;
	}
	else if (trigMode == 2 && trig) {
		play = true;
		if (inputs[POS_INPUT].isConnected())
			sliceIndex = clamp((int)(posCv * nbSlices), 0, nbSlices - 1);
		else
			sliceIndex = (sliceIndex + 1) % nbSlices;
		samplePos = clamp((float)((readMode != 1) ? sliceIndex * sliceLength : (sliceIndex + 1) * sliceLength - 1), 0.0f, total - 1.0f);
	}
	gateWasHigh = gate;

	float outL = 0.f, outR = 0.f;
	if (play && total > 0 && samplePos >= 0 && samplePos < total) {
		const std::vector<dsp::Frame<2>> &buf = sample->frames;
		int xi = (int)samplePos;
		float xf = samplePos - xi;
		int xn = std::min(xi + 1, total - 1);
		float l = crossfade(buf[xi].samples[0], buf[xn].samples[0], xf);
		float r = crossfade(buf[xi].samples[1], buf[xn].samples[1], xf);
		if (sample->channels >= 2 && !(outputs[OUTL_OUTPUT].isConnected() && outputs[OUTR_OUTPUT].isConnected()))
			l = r = 0.5f * (l + r);
		outL = 5.0f * l;
		outR = 5.0f * r;

		bool scrub = (trigMode == 1) && inputs[POS_INPUT].isConnected();
		if (!scrub) {
			samplePos += (readMode != 1) ? speed : -speed;
			if (trigMode == 2) {
				float start = sliceIndex * sliceLength, end = std::min((float)((sliceIndex + 1) * sliceLength), (float)total);
				if (readMode == 0 && samplePos >= end)
					play = false;
				else if (readMode == 1 && samplePos < start)
					play = false;
				else if (readMode == 2 && samplePos >= end)
					samplePos = start;
			}
			else {
				if (readMode == 0 && samplePos >= total)
					play = false;
				else if (readMode == 1 && samplePos < 0)
					play = false;
				else if (readMode == 2 && samplePos >= total)
					samplePos = inputs[POS_INPUT].isConnected() ? posCv * (total - 1.0f) : 0.0f;
			}
		}
	}
	else if (!(trigMode == 1 && inputs[POS_INPUT].isConnected() && total > 0))
		play = false;
	outputs[OUTL_OUTPUT].setVoltage(outL);
	outputs[OUTR_OUTPUT].setVoltage(outR);

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
		nvgBeginPath(args.vg);
		for (int i = 0; i < OuaiveSample::OVERVIEW; i++) {
			float x = b.pos.x + b.size.x * (i + 0.5f) / OuaiveSample::OVERVIEW;
			float y = b.pos.y + b.size.y * (0.5f - 0.5f * s.overviewMax[c][i]);
			if (i == 0)
				nvgMoveTo(args.vg, x, y);
			else
				nvgLineTo(args.vg, x, y);
		}
		for (int i = OuaiveSample::OVERVIEW - 1; i >= 0; i--) {
			float x = b.pos.x + b.size.x * (i + 0.5f) / OuaiveSample::OVERVIEW;
			nvgLineTo(args.vg, x, b.pos.y + b.size.y * (0.5f - 0.5f * s.overviewMin[c][i]));
		}
		nvgLineCap(args.vg, NVG_MITER);
		nvgStrokeWidth(args.vg, 1);
		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgStroke(args.vg);
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

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			OUAIVEDisplay *display = new OUAIVEDisplay();
			display->module = module;
			display->box.pos = Vec(5, 70);
			display->box.size = Vec(125, 110);
			addChild(display);
		}

		static const float portX0[4] = {34, 67, 101};

		addInput(createInput<TinyPJ301MPort>(Vec(10, 18), module, OUAIVE::POS_RESET_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(112, 18), module, OUAIVE::EOC_OUTPUT));

		addParam(createParam<BlueCKD6>(Vec(portX0[0]-25, 215), module, OUAIVE::TRIG_MODE_PARAM));

		addParam(createParam<BlueCKD6>(Vec(portX0[1]-14, 215), module, OUAIVE::READ_MODE_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(portX0[2]+5, 222), module, OUAIVE::READ_MODE_INPUT));

		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]-9, 250), module, OUAIVE::NB_SLICES_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]+15, 250), module, OUAIVE::CVSLICES_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(portX0[2]+5, 252), module, OUAIVE::NB_SLICES_INPUT));

		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]-9, 275), module, OUAIVE::SPEED_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(portX0[1]+15, 275), module, OUAIVE::CVSPEED_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(portX0[2]+5, 277), module, OUAIVE::SPEED_INPUT));

		addInput(createInput<PJ301MPort>(Vec(7, 330), module, OUAIVE::GATE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(40, 330), module, OUAIVE::POS_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(portX0[2]-11, 340), module, OUAIVE::OUTL_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(portX0[2]+11, 340), module, OUAIVE::OUTR_OUTPUT));
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
