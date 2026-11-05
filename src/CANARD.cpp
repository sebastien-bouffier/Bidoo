#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "osdialog.h"
#include <vector>
#include "cmath"
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include "dep/waves.hpp"

using namespace std;

// Linear up to 8 V, then bends to stay under 10 V (9.95 V at most).
inline float canardSoftLimit(float x) {
	float a = std::fabs(x);
	if (a <= 8.0f)
		return x;
	return std::copysign(8.0f + 1.95f * std::tanh((a - 8.0f) * 0.5f), x);
}

// A sample and its slice markers (the first is always 0), with an overview
// for the display. Never changed once handed to the audio thread: an edit
// builds a new one on the worker.
struct CanardSample {
	static constexpr int OVERVIEW = 1024;
	std::vector<dsp::Frame<2>> frames;
	std::vector<int> slices{0};
	float sampleRate = 0.f;
	float overviewMin[2][OVERVIEW] = {};
	float overviewMax[2][OVERVIEW] = {};

	int size() const { return (int)frames.size(); }

	void finish() {
		std::sort(slices.begin(), slices.end());
		slices.erase(std::unique(slices.begin(), slices.end()), slices.end());
		slices.erase(std::remove_if(slices.begin(), slices.end(), [&](int s) { return s < 0 || (s > 0 && s >= size()); }), slices.end());
		if (slices.empty() || slices[0] != 0)
			slices.insert(slices.begin(), 0);
		int count = size();
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

// Recording goes into chunks the worker allocates in advance.
struct CanardChunk {
	static constexpr int FRAMES = 16384;
	dsp::Frame<2> f[FRAMES];
	int used = 0;
};

struct CANARD : BidooModule {
	enum ParamIds {
		RECORD_PARAM,
		SAMPLE_START_PARAM,
		LOOP_LENGTH_PARAM,
		READ_MODE_PARAM,
		SPEED_PARAM,
		FADE_PARAM,
		MODE_PARAM,
		SLICE_PARAM,
		CLEAR_PARAM,
		THRESHOLD_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		INL_INPUT,
		INR_INPUT,
		TRIG_INPUT,
		GATE_INPUT,
		SAMPLE_START_INPUT,
		LOOP_LENGTH_INPUT,
		READ_MODE_INPUT,
		SPEED_INPUT,
		RECORD_INPUT,
		FADE_INPUT,
		SLICE_INPUT,
		CLEAR_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		EOC_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		REC_LIGHT,
		NUM_LIGHTS
	};

	// Audio thread: the sample it plays and the playback state.
	CanardSample *smp = nullptr;
	bool play = false;
	bool record = false;
	float samplePos = 0.0f, sampleStart = 0.0f, loopLength = 0.0f, fadeLenght = 0.0f;
	float direction = 1.0f;      // ping-pong flips it
	int playedSlice = 0;
	int totalSampleCount = 0;
	float speed = 1.0f;
	bool reverse = false;        // plays the slices backward
	dsp::SchmittTrigger trigTrigger;
	dsp::SchmittTrigger gateTrigger;
	dsp::SchmittTrigger recordTrigger;
	dsp::SchmittTrigger clearTrigger;
	dsp::PulseGenerator eocPulse;

	// Handoff between the worker and the audio thread.
	std::atomic<CanardSample*> pending{nullptr};
	std::atomic<CanardSample*> retired{nullptr};
	std::atomic<bool> pendingSet{false};   // a pending change (it may be "no sample")
	std::atomic<CanardSample*> live{nullptr};  // what the audio thread plays, read by the worker

	// Recording: chunks the worker keeps ready, filled by the audio thread.
	static constexpr int SPARES = 16;      // about 5 s ahead at 48 kHz
	static constexpr int MAX_CHUNKS = 16384;  // about 90 minutes at 48 kHz
	std::atomic<CanardChunk*> spare[SPARES];
	CanardChunk *recChunks[MAX_CHUNKS] = {};
	int recCount = 0;
	std::atomic<int> recDone{-1};             // chunks of a finished recording, for the worker
	std::atomic<bool> recAppend{false};
	std::atomic<float> recRate{48000.f};
	std::atomic<bool> clearRequested{false};

	// A sample that matches no file (a recording, or a file with a recording
	// added or a slice deleted) is stored in the patch, as sample.wav.
	std::atomic<bool> inPatch{false};
	std::atomic<bool> storeDirty{false};   // sample.wav is out of date
	std::atomic<int> storesDone{0};
	bool loadStored = false;               // set by dataFromJson, used by onAdd
	std::vector<int> storedSlices;
	int storedRate = 0;

	// Worker: loads, saves, slice edits, recordings.
	enum JobType { LOAD, SAVE, CLEAR, DELETE_SLICE, ADD_MARKER, DELETE_MARKER, TRANSIENTS, STORE, LOAD_STORED, RESAMPLE };
	struct Job { JobType type; std::string path; int value; std::vector<int> slices; float rate; };
	std::thread worker;
	std::mutex jobMutex;
	std::condition_variable jobCv;
	std::deque<Job> jobs;
	std::atomic<bool> quit{false};
	std::atomic<float> reloadRate{0.f};
	std::atomic<int> busy{0};
	std::string lastPath;      // under jobMutex
	std::string waveFileName;  // under jobMutex

	// UI thread: what the display shows.
	std::mutex uiMutex;
	std::shared_ptr<CanardSample> uiSample;
	int selected = -1;
	int addSliceMarker = -1;
	int deleteSliceMarker = -1;

	CANARD() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configButton(RECORD_PARAM, "Record (start / stop)");
		configParam(SAMPLE_START_PARAM, 0.0f, 10.0f, 0.0f, "Start", "%", 0.f, 10.f);
		configParam(LOOP_LENGTH_PARAM, 0.0f, 10.0f, 10.0f, "Length", "%", 0.f, 10.f);
		configSwitch(READ_MODE_PARAM, 0.0f, 2.0f, 0.0f, "Read mode", {"Once", "Loop", "Ping-pong"});
		configParam(SPEED_PARAM, -4.0f, 4.0f, 0.0f, "Speed", "x", 2.f);
		configParam(FADE_PARAM, 0.0f, 10.0f, 0.0f, "Fade", "%", 0.f, 5.f);
		configParam(SLICE_PARAM, 0.0f, 10.0f, 0.0f, "Slice");
		configButton(CLEAR_PARAM, "Clear the sample");
		configParam(THRESHOLD_PARAM, 0.01f, 10.0f, 1.0f, "Transient threshold (Shift-click: detect)");
		configSwitch(MODE_PARAM, 0, 1, 0, "Slice mode", {"Off", "On"});
		configInput(INL_INPUT, "Left in");
		configInput(INR_INPUT, "Right in");
		configInput(TRIG_INPUT, "Trigger");
		configInput(GATE_INPUT, "Gate");
		configInput(SAMPLE_START_INPUT, "Start CV");
		configInput(LOOP_LENGTH_INPUT, "Length CV");
		configInput(READ_MODE_INPUT, "Read mode CV");
		configInput(SPEED_INPUT, "Speed (1V/oct)");
		configInput(RECORD_INPUT, "Record trigger");
		configInput(FADE_INPUT, "Fade CV");
		configInput(SLICE_INPUT, "Slice CV");
		configInput(CLEAR_INPUT, "Clear trigger");
		configOutput(OUTL_OUTPUT, "Left");
		configOutput(OUTR_OUTPUT, "Right");
		configOutput(EOC_OUTPUT, "End of cycle");
		for (int i = 0; i < SPARES; i++)
			spare[i] = nullptr;
		worker = std::thread(&CANARD::workerLoop, this);
	}

	~CANARD() {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			quit = true;
		}
		jobCv.notify_all();
		if (worker.joinable())
			worker.join();
		delete pending.load();
		delete retired.load();
		delete smp;
		for (int i = 0; i < SPARES; i++)
			delete spare[i].load();
		for (int i = 0; i < recCount; i++)
			delete recChunks[i];
	}

	static float engineRate() {
		return (APP && APP->engine) ? APP->engine->getSampleRate() : 48000.f;
	}

	// Any thread but the audio thread.
	void post(Job job) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			if (job.type == LOAD) {
				lastPath = job.path;
				waveFileName = rack::system::getFilename(job.path);
			}
			if (job.type == SAVE) {
				lastPath = job.path;
				waveFileName = rack::system::getFilename(job.path);
			}
			if (job.type == CLEAR) {
				lastPath.clear();
				waveFileName.clear();
			}
			jobs.push_back(job);
			busy++;
		}
		jobCv.notify_all();
	}

	void loadFile(const std::string &path, std::vector<int> slices = {}) {
		post({LOAD, path, 0, slices, engineRate()});
	}

	std::string getPath() {
		std::lock_guard<std::mutex> lock(jobMutex);
		return lastPath;
	}

	std::string getFileName() {
		std::lock_guard<std::mutex> lock(jobMutex);
		return waveFileName;
	}

	// Worker: hands a new sample (or none) to the audio thread.
	void publish(CanardSample *s) {
		if (s)
			s->finish();
		auto shown = s ? std::make_shared<CanardSample>(*s) : nullptr;
		if (shown)
			shown->frames.clear();  // the display needs the overview and the slices only
		{
			std::lock_guard<std::mutex> ui(uiMutex);
			uiSample = shown;
		}
		while (pendingSet.load() && !quit) {
			// The audio thread takes the new one only once the old one is freed.
			delete retired.exchange(nullptr, std::memory_order_acq_rel);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		delete pending.exchange(s, std::memory_order_acq_rel);
		pendingSet = true;
	}

	CanardSample *copyLive() {
		// An edit starts from the newest sample: wait until the audio thread took it.
		while (pendingSet.load() && !quit) {
			delete retired.exchange(nullptr, std::memory_order_acq_rel);
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		CanardSample *l = live.load(std::memory_order_acquire);
		return l ? new CanardSample(*l) : nullptr;
	}

	void detectTransients(CanardSample *s, float threshold) {
		s->slices.clear();
		s->slices.push_back(0);
		const int size = 256;
		float prevNrgy = 0.0f;
		for (int i = 0; i + size < s->size(); i += size) {
			float nrgy = 0.0f;
			int zcIdx = 0;
			bool firstZero = true;
			for (int k = 0; k < size; k++) {
				float v = s->frames[i + k].samples[0];
				nrgy += 100 * v * v / size;
				if (v == 0.0f && firstZero) {
					zcIdx = k;
					firstZero = false;
				}
			}
			if ((nrgy > threshold) && (nrgy > 10 * prevNrgy))
				s->slices.push_back(i + zcIdx);
			prevNrgy = nrgy;
		}
	}

	void runJob(const Job &job) {
		switch (job.type) {
			case LOAD:
			case LOAD_STORED: {
				std::string name, ext;
				int ch = 0, sr = 0, cnt = 0;
				CanardSample *s = new CanardSample();
				s->frames = waves::getStereoWav(job.path, job.rate, name, ext, ch, sr, cnt);
				s->sampleRate = job.rate;
				inPatch = job.type == LOAD_STORED;
				storeDirty = false;
				if (s->frames.empty()) {
					delete s;
					publish(nullptr);
					break;
				}
				// Slices from a patch: job.value is the rate they were saved at.
				s->slices = job.slices;
				if (job.value > 0 && job.value != (int)job.rate)
					for (int &x : s->slices)
						x = (int)std::round(x * (double)job.rate / job.value);
				publish(s);
				break;
			}
			case SAVE: {
				CanardSample *l = live.load(std::memory_order_acquire);
				if (l && l->size() > 0 && waves::saveWave(l->frames, l->sampleRate, job.path))
					inPatch = false;  // the file now holds this sample
				break;
			}
			case STORE: {
				CanardSample *l = live.load(std::memory_order_acquire);
				if (l && l->size() > 0 && waves::saveWave(l->frames, l->sampleRate, job.path))
					storeDirty = false;
				storesDone++;
				break;
			}
			case RESAMPLE: {
				CanardSample *s = copyLive();
				if (!s)
					break;
				double ratio = job.rate / s->sampleRate;
				s->frames = waves::resampleStereo(s->frames, s->sampleRate, job.rate);
				for (int &x : s->slices)
					x = (int)std::round(x * ratio);
				s->sampleRate = job.rate;
				publish(s);
				break;
			}
			case CLEAR:
				inPatch = false;
				publish(nullptr);
				break;
			case DELETE_SLICE: {
				CanardSample *s = copyLive();
				if (!s)
					break;
				int sel = job.value;
				if (sel >= 0 && sel < (int)s->slices.size()) {
					int from = s->slices[sel];
					int to = (sel + 1 < (int)s->slices.size()) ? s->slices[sel + 1] : s->size();
					s->frames.erase(s->frames.begin() + from, s->frames.begin() + to);
					s->slices.erase(s->slices.begin() + sel);
					for (size_t i = sel; i < s->slices.size(); i++)
						s->slices[i] -= (to - from);
					// The audio no longer matches the file.
					inPatch = true;
					storeDirty = true;
				}
				if (s->frames.empty()) {
					delete s;
					s = nullptr;
				}
				publish(s);
				break;
			}
			case ADD_MARKER: {
				CanardSample *s = copyLive();
				if (!s)
					break;
				s->slices.push_back(job.value);
				publish(s);
				break;
			}
			case DELETE_MARKER: {
				CanardSample *s = copyLive();
				if (!s)
					break;
				if (job.value > 0)
					s->slices.erase(std::remove(s->slices.begin(), s->slices.end(), job.value), s->slices.end());
				publish(s);
				break;
			}
			case TRANSIENTS: {
				CanardSample *s = copyLive();
				if (!s)
					break;
				detectTransients(s, (float)job.value / 1000.f);
				publish(s);
				break;
			}
		}
	}

	void workerLoop() {
		std::unique_lock<std::mutex> lock(jobMutex);
		while (!quit) {
			// The audio thread cannot notify without locking: poll often for it.
			if (jobs.empty())
				jobCv.wait_for(lock, std::chrono::milliseconds(10));
			if (quit)
				break;
			lock.unlock();
			delete retired.exchange(nullptr, std::memory_order_acq_rel);
			// Keep spare recording chunks ready.
			for (int i = 0; i < SPARES; i++)
				if (!spare[i].load()) {
					// Touched here, so the audio thread never takes a page fault on it.
					CanardChunk *c = new CanardChunk();
					std::memset(c->f, 0, sizeof(c->f));
					spare[i].store(c, std::memory_order_release);
				}
			// A finished recording: a new sample, or appended as a new slice.
			if (clearRequested.exchange(false)) {
				{
					std::lock_guard<std::mutex> l2(jobMutex);
					lastPath.clear();
					waveFileName.clear();
				}
				inPatch = false;
				publish(nullptr);
			}
			int done = recDone.load();
			if (done >= 0) {
				CanardSample *s = recAppend ? copyLive() : nullptr;
				if (!s) {
					s = new CanardSample();
					s->sampleRate = recRate;
				}
				int at = s->size();
				for (int c = 0; c < done; c++) {
					s->frames.insert(s->frames.end(), recChunks[c]->f, recChunks[c]->f + recChunks[c]->used);
					delete recChunks[c];
					recChunks[c] = nullptr;
				}
				if (at > 0)
					s->slices.push_back(at);
				{
					std::lock_guard<std::mutex> l2(jobMutex);
					if (!recAppend) {
						lastPath.clear();
						waveFileName.clear();
					}
				}
				if (s->frames.empty()) {
					delete s;
					s = nullptr;
				}
				inPatch = s != nullptr;
				storeDirty = s != nullptr;
				publish(s);
				// Only now may the audio thread start another recording.
				recDone.store(-1);
			}
			float rr = reloadRate.exchange(0.f);
			lock.lock();
			if (rr > 0.f) {
				CanardSample *l = live.load(std::memory_order_acquire);
				if (!inPatch && !lastPath.empty() && l) {
					// Back from the file at the new rate; the slices move with it.
					jobs.push_back({LOAD, lastPath, (int)l->sampleRate, l->slices, rr});
					busy++;
				}
				else if (l) {
					// No file holds it: converted in memory.
					jobs.push_back({RESAMPLE, "", 0, {}, rr});
					busy++;
				}
			}
			if (jobs.empty())
				continue;
			Job job = jobs.front();
			jobs.pop_front();
			lock.unlock();
			runJob(job);
			lock.lock();
			busy--;
		}
	}

	void process(const ProcessArgs &args) override;

	// the folder is made on demand: Rack does not create it, and writing the
	// sample into a folder that does not exist fails (the sample is then lost
	// between sessions)
	std::string storagePath() {
		return rack::system::join(createPatchStorageDirectory(), "sample.wav");
	}

	void onSave(const SaveEvent &e) override {
		std::string path = storagePath();
		if (!inPatch) {
			if (rack::system::isFile(path))
				rack::system::remove(path);
			return;
		}
		if (!storeDirty && rack::system::isFile(path))
			return;
		// Written by the worker, which owns the sample; wait for it (it only
		// runs short jobs, but give up rather than hang the save).
		int before = storesDone.load();
		post({STORE, path, 0, {}, 0.f});
		for (int i = 0; i < 10000 && storesDone.load() == before; i++)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}

	void onAdd(const AddEvent &e) override {
		if (!loadStored)
			return;
		loadStored = false;
		std::string path = storagePath();
		if (rack::system::isFile(path))
			post({LOAD_STORED, path, storedRate, storedSlices, engineRate()});
		else
			inPatch = false;
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		bool stored = inPatch;
		json_object_set_new(rootJ, "lastPath", json_string(stored ? "" : getPath().c_str()));
		json_object_set_new(rootJ, "inPatch", json_boolean(stored));
		json_object_set_new(rootJ, "fileName", json_string(getFileName().c_str()));
		json_object_set_new(rootJ, "reverse", json_boolean(reverse));
		json_t *slicesJ = json_array();
		std::shared_ptr<CanardSample> s;
		{
			std::lock_guard<std::mutex> lock(uiMutex);
			s = uiSample;
		}
		if (s) {
			for (int x : s->slices)
				json_array_append_new(slicesJ, json_integer(x));
			json_object_set_new(rootJ, "sliceRate", json_integer((int)s->sampleRate));
		}
		json_object_set_new(rootJ, "slices", slicesJ);
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *reverseJ = json_object_get(rootJ, "reverse");
		reverse = reverseJ && json_is_true(reverseJ);
		std::vector<int> sl;
		json_t *slicesJ = json_object_get(rootJ, "slices");
		size_t i;
		json_t *sliceJ;
		if (slicesJ)
			json_array_foreach(slicesJ, i, sliceJ)
				sl.push_back(json_integer_value(sliceJ));
		json_t *rateJ = json_object_get(rootJ, "sliceRate");
		int rate = rateJ ? (int)json_integer_value(rateJ) : 0;
		json_t *inPatchJ = json_object_get(rootJ, "inPatch");
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (inPatchJ && json_is_true(inPatchJ)) {
			// sample.wav, from the patch storage, once the module is added (onAdd).
			loadStored = true;
			inPatch = true;  // so a save before the load ends keeps sample.wav
			storedSlices = sl;
			storedRate = rate;
			json_t *nameJ = json_object_get(rootJ, "fileName");
			std::lock_guard<std::mutex> lock(jobMutex);
			lastPath.clear();
			waveFileName = nameJ ? json_string_value(nameJ) : "";
		}
		else if (lastPathJ && json_string_value(lastPathJ)[0])
			post({LOAD, json_string_value(lastPathJ), rate, sl, engineRate()});
	}

	void calcLoop() {
		int sliceStart = 0;
		int sliceEnd = totalSampleCount > 0 ? totalSampleCount - 1 : 0;
		playedSlice = 0;
		if (smp && params[MODE_PARAM].getValue() == 1 && smp->slices.size() > 0) {
			int n = smp->slices.size();
			playedSlice = clamp((int)std::round(clamp(params[SLICE_PARAM].getValue() + inputs[SLICE_INPUT].getVoltage(), 0.0f, 10.0f) * (n - 1) / 10), 0, n - 1);
			sliceStart = smp->slices[playedSlice];
			sliceEnd = (playedSlice < n - 1) ? (smp->slices[playedSlice + 1] - 1) : (totalSampleCount - 1);
		}
		if (totalSampleCount > 0) {
			sampleStart = rescale(clamp(inputs[SAMPLE_START_INPUT].getVoltage() + params[SAMPLE_START_PARAM].getValue(), 0.0f, 10.0f), 0.0f, 10.0f, sliceStart, sliceEnd);
			loopLength = clamp(rescale(clamp(inputs[LOOP_LENGTH_INPUT].getVoltage() + params[LOOP_LENGTH_PARAM].getValue(), 0.0f, 10.0f), 0.0f, 10.0f, 0.0f, sliceEnd - sliceStart + 1), 1.0f, std::max(sliceEnd - sampleStart, 1.0f));
			fadeLenght = rescale(clamp(inputs[FADE_INPUT].getVoltage() + params[FADE_PARAM].getValue(), 0.0f, 10.0f), 0.0f, 10.0f, 0.0f, std::floor(loopLength / 2));
		}
		else {
			loopLength = 0;
			sampleStart = 0;
			fadeLenght = 0;
		}
	}

	void initPos() {
		direction = 1.0f;
		samplePos = reverse ? sampleStart + loopLength : sampleStart;
	}
};

void CANARD::process(const ProcessArgs &args) {
	// Take a new sample from the worker, once it has freed the previous one.
	if (pendingSet.load(std::memory_order_acquire) && !retired.load(std::memory_order_acquire)) {
		CanardSample *s = pending.exchange(nullptr, std::memory_order_acq_rel);
		retired.store(smp, std::memory_order_release);
		smp = s;
		live.store(s, std::memory_order_release);
		pendingSet = false;
		totalSampleCount = smp ? smp->size() : 0;
		if (samplePos >= totalSampleCount)
			play = false;
	}
	if (smp && smp->sampleRate != args.sampleRate && busy.load() == 0 && recDone.load() < 0 && reloadRate.load() == 0.f)
		reloadRate = args.sampleRate;

	if (clearTrigger.process(inputs[CLEAR_INPUT].getVoltage() + params[CLEAR_PARAM].getValue(), 0.1f, 1.f)) {
		// Through the worker, like any change of the sample.
		clearRequested = true;
		play = false;
	}

	// Recording, into chunks the worker keeps ready.
	if (recordTrigger.process(inputs[RECORD_INPUT].getVoltage() + params[RECORD_PARAM].getValue(), 0.1f, 1.f)) {
		if (record) {
			recAppend = params[MODE_PARAM].getValue() == 1;
			recRate = args.sampleRate;
			recDone = recCount;
			record = false;
		}
		else if (recDone.load() < 0) {
			recCount = 0;
			record = true;
		}
	}
	if (record) {
		CanardChunk *ch = recCount > 0 ? recChunks[recCount - 1] : nullptr;
		if (!ch || ch->used >= CanardChunk::FRAMES) {
			ch = nullptr;
			if (recCount < MAX_CHUNKS) {
				for (int i = 0; i < SPARES && !ch; i++)
					ch = spare[i].exchange(nullptr, std::memory_order_acq_rel);
			}
			if (ch) {
				ch->used = 0;
				recChunks[recCount++] = ch;
			}
		}
		if (ch) {
			// Stored like a sample file: 5 V = 1, played back at 5 V.
			ch->f[ch->used].samples[0] = inputs[INL_INPUT].getVoltage() * 0.2f;
			ch->f[ch->used].samples[1] = (inputs[INR_INPUT].isConnected() ? inputs[INR_INPUT].getVoltage() : inputs[INL_INPUT].getVoltage()) * 0.2f;
			ch->used++;
		}
		else {
			// Out of chunks (the worker fell behind or 90 minutes were reached).
			recAppend = params[MODE_PARAM].getValue() == 1;
			recRate = args.sampleRate;
			recDone = recCount;
			record = false;
		}
	}
	lights[REC_LIGHT].setBrightness(record ? 1.0f : 0.0f);

	int trigMode = inputs[TRIG_INPUT].isConnected() ? 1 : (inputs[GATE_INPUT].isConnected() ? 2 : 0);
	int readMode = clamp((int)std::round(inputs[READ_MODE_INPUT].getVoltage() + params[READ_MODE_PARAM].getValue()), 0, 2);
	speed = dsp::exp2_taylor5(clamp(params[SPEED_PARAM].getValue() + inputs[SPEED_INPUT].getVoltage(), -4.f, 4.f));
	calcLoop();

	bool trig = trigTrigger.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f);
	bool gateRise = gateTrigger.process(inputs[GATE_INPUT].getVoltage(), 0.1f, 1.f);
	bool gateHigh = gateTrigger.isHigh();
	float end = sampleStart + loopLength;
	bool advance = false;

	if (trigMode == 1) {
		if (trig) {
			initPos();
			if (smp && smp->slices.size() == 1 && inputs[SLICE_INPUT].isConnected())
				samplePos = sampleStart + loopLength * clamp(params[SLICE_PARAM].getValue() + inputs[SLICE_INPUT].getVoltage(), 0.0f, 10.0f) * 0.1f;
			play = totalSampleCount > 0;
		}
		else
			advance = play;
	}
	else if (trigMode == 2) {
		if (gateHigh) {
			if (inputs[SLICE_INPUT].isConnected()) {
				// Scrubbing: the slice CV sets the position.
				play = totalSampleCount > 0;
				samplePos = sampleStart + clamp(params[SLICE_PARAM].getValue() + inputs[SLICE_INPUT].getVoltage(), 0.0f, 10.0f) * 0.1f * loopLength;
			}
			else if (gateRise) {
				initPos();
				play = totalSampleCount > 0;
			}
			else
				advance = play;
		}
		else
			play = false;
	}
	else
		play = false;

	if (advance) {
		// Forward, or backward with reverse; ping-pong flips the direction.
		float dir = (reverse ? -1.f : 1.f) * direction;
		samplePos += dir * speed;
		bool pastEnd = dir > 0 ? samplePos >= end : samplePos <= sampleStart;
		if (pastEnd) {
			if (readMode == 0) {
				play = false;
				eocPulse.trigger(1e-3f);
			}
			else if (readMode == 1)
				initPos();
			else {
				direction = -direction;
				samplePos = clamp(samplePos, sampleStart, end);
			}
		}
		samplePos = clamp(samplePos, sampleStart, end);
	}

	float outL = 0.f, outR = 0.f;
	if (play && smp && totalSampleCount > 1) {
		float fade = 1.0f;
		if (fadeLenght >= 1.0f) {
			if ((samplePos - sampleStart) < fadeLenght)
				fade = (samplePos - sampleStart) / fadeLenght;
			else if ((end - samplePos) < fadeLenght)
				fade = (end - samplePos) / fadeLenght;
		}
		float p = clamp(samplePos, 0.f, totalSampleCount - 1.001f);
		int xi = (int)p;
		float xf = p - xi;
		const dsp::Frame<2> &a = smp->frames[xi];
		const dsp::Frame<2> &b = smp->frames[std::min(xi + 1, totalSampleCount - 1)];
		outL = crossfade(a.samples[0], b.samples[0], xf) * fade * 5.0f;
		outR = crossfade(a.samples[1], b.samples[1], xf) * fade * 5.0f;
	}
	outputs[OUTL_OUTPUT].setVoltage(canardSoftLimit(outL));
	outputs[OUTR_OUTPUT].setVoltage(canardSoftLimit(outR));
	outputs[EOC_OUTPUT].setVoltage(eocPulse.process(args.sampleTime) ? 10.0f : 0.0f);
}

struct BidooTransientsBlueTrimpot : BidooBlueTrimpot {
	CANARD *module;

	void onButton(const event::Button &e) override {
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT))
			module->post({CANARD::TRANSIENTS, "", (int)(module->params[CANARD::THRESHOLD_PARAM].getValue() * 1000.f), {}, 0.f});
		BidooBlueTrimpot::onButton(e);
	}
};

struct CANARDDisplay : OpaqueWidget {
	CANARD *module;
	const float width = 175.0f;
	const float height = 50.0f;
	float zoomWidth = 175.0f;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refX = 0.0f;

	std::shared_ptr<CanardSample> shown() {
		std::lock_guard<std::mutex> lock(module->uiMutex);
		return module->uiSample;
	}

	void onButton(const event::Button &e) override {
		if (!module)
			return;
		std::shared_ptr<CanardSample> s = shown();
		if (s && !s->slices.empty()) {
			refX = e.pos.x;
			int count = 0;
			for (int b = 0; b < 1; b++)
				count = std::max(count, module->totalSampleCount);
			refIdx = clamp((int)(((e.pos.x - zoomLeftAnchor) / zoomWidth) * (float)count), 0, std::max(count - 1, 0));
			module->addSliceMarker = refIdx;
			// The slice under the click (the first marker is always 0).
			auto upper = std::upper_bound(s->slices.begin(), s->slices.end(), refIdx);
			int sel = std::max((int)std::distance(s->slices.begin(), upper) - 1, 0);
			module->selected = sel;
			module->deleteSliceMarker = s->slices[sel];
		}
		if (e.button == 0)
			OpaqueWidget::onButton(e);
		else if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) {
			ModuleWidget *pWidget = dynamic_cast<ModuleWidget*>(this->parent);
			if (pWidget)
				pWidget->createContextMenu();
			e.consume(this);
		}
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

	void drawWave(const DrawArgs& args, const CanardSample &s, int c, float top) {
		Rect b = Rect(Vec(zoomLeftAnchor, top), Vec(zoomWidth, height));
		nvgBeginPath(args.vg);
		for (int i = 0; i < CanardSample::OVERVIEW; i++) {
			float x = b.pos.x + b.size.x * (i + 0.5f) / CanardSample::OVERVIEW;
			float y = b.pos.y + b.size.y * (0.5f - 0.5f * clamp(s.overviewMax[c][i], -1.f, 1.f));
			if (i == 0)
				nvgMoveTo(args.vg, x, y);
			else
				nvgLineTo(args.vg, x, y);
		}
		for (int i = CanardSample::OVERVIEW - 1; i >= 0; i--) {
			float x = b.pos.x + b.size.x * (i + 0.5f) / CanardSample::OVERVIEW;
			nvgLineTo(args.vg, x, b.pos.y + b.size.y * (0.5f - 0.5f * clamp(s.overviewMin[c][i], -1.f, 1.f)));
		}
		nvgLineCap(args.vg, NVG_MITER);
		nvgStrokeWidth(args.vg, 1);
		nvgGlobalCompositeOperation(args.vg, NVG_LIGHTER);
		nvgStroke(args.vg);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && module) {
			std::shared_ptr<CanardSample> s = shown();
			float nbSample = module->totalSampleCount;
			if (s && nbSample > 0) {
				auto px = [&](float idx) { return idx * zoomWidth / nbSample + zoomLeftAnchor; };
				nvgScissor(args.vg, 0, 0, width, 2*height+10);

				// Play line
				nvgStrokeColor(args.vg, LIGHTBLUE_BIDOO);
				nvgBeginPath(args.vg);
				nvgStrokeWidth(args.vg, 2);
				nvgMoveTo(args.vg, px(module->samplePos), 0);
				nvgLineTo(args.vg, px(module->samplePos), 2*height+10);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);

				// Ref lines
				nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
				nvgStrokeWidth(args.vg, 1);
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0, height/2);
				nvgLineTo(args.vg, width, height/2);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0, 3*height*0.5f+10);
				nvgLineTo(args.vg, width, 3*height*0.5f+10);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);

				// Loop
				nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 60));
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, px(module->sampleStart + module->fadeLenght), 0);
				nvgLineTo(args.vg, px(module->sampleStart), 2*height+10);
				nvgLineTo(args.vg, px(module->sampleStart + module->loopLength), 2*height+10);
				nvgLineTo(args.vg, px(module->sampleStart + module->loopLength - module->fadeLenght), 0);
				nvgLineTo(args.vg, px(module->sampleStart + module->fadeLenght), 0);
				nvgClosePath(args.vg);
				nvgFill(args.vg);

				bool sliceMode = module->params[CANARD::MODE_PARAM].getValue() == 1;
				// Selected slice
				int sel = module->selected;
				if (sel >= 0 && sel < (int)s->slices.size() && sliceMode) {
					nvgStrokeColor(args.vg, RED_BIDOO);
					nvgBeginPath(args.vg);
					nvgStrokeWidth(args.vg, 4);
					nvgMoveTo(args.vg, px(s->slices[sel]), 2*height+9);
					nvgLineTo(args.vg, sel + 1 < (int)s->slices.size() ? px(s->slices[sel+1]) : zoomWidth + zoomLeftAnchor, 2*height+9);
					nvgClosePath(args.vg);
					nvgStroke(args.vg);
				}

				// Waveform
				nvgSave(args.vg);
				nvgStrokeColor(args.vg, PINK_BIDOO);
				drawWave(args, *s, 0, 0);
				drawWave(args, *s, 1, height + 10);
				nvgRestore(args.vg);

				// Slices
				if (sliceMode) {
					for (int x : s->slices) {
						nvgStrokeColor(args.vg, x != module->deleteSliceMarker ? YELLOW_BIDOO : RED_BIDOO);
						nvgStrokeWidth(args.vg, 1);
						nvgBeginPath(args.vg);
						nvgMoveTo(args.vg, px(x), 0);
						nvgLineTo(args.vg, px(x), 2*height+10);
						nvgClosePath(args.vg);
						nvgStroke(args.vg);
					}
				}
				nvgResetScissor(args.vg);
			}
		}
		Widget::drawLayer(args, layer);
	}

};

struct CANARDWidget : BidooWidget {

	CANARDWidget(CANARD *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/CANARD.svg"));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			CANARDDisplay *display = new CANARDDisplay();
			display->module = module;
			display->box.pos = Vec(10, 35);
			display->box.size = Vec(175, 110);
			addChild(display);
		}

		static const float portX0[5] = {16, 53, 90, 126, 161};

		addChild(createLight<SmallLight<RedLight>>(Vec(portX0[0]-10, 167), module, CANARD::REC_LIGHT));
		addParam(createParam<BlueCKD6>(Vec(portX0[0]-6, 170), module, CANARD::RECORD_PARAM));

		addParam(createParam<BidooBlueKnob>(Vec(portX0[2]-7, 170), module, CANARD::SAMPLE_START_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[3]-7, 170), module, CANARD::LOOP_LENGTH_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(portX0[4]-6, 170), module, CANARD::READ_MODE_PARAM));

		addInput(createInput<PJ301MPort>(Vec(portX0[0]-4.5f, 202), module, CANARD::RECORD_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1]-4.5f, 202), module, CANARD::TRIG_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1]-4.5f, 172), module, CANARD::GATE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[2]-5.0f, 202), module, CANARD::SAMPLE_START_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[3]-5.0f, 202), module, CANARD::LOOP_LENGTH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[4]-4.5f, 202), module, CANARD::READ_MODE_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(portX0[0]-7, 245), module, CANARD::SPEED_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[1]-7, 245), module, CANARD::FADE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[2]-7, 245), module, CANARD::SLICE_PARAM));
		addParam(createParam<BlueCKD6>(Vec(portX0[3]-6, 245), module, CANARD::CLEAR_PARAM));
		addOutput(createOutput<PJ301MPort>(Vec(portX0[4]-4.5f, 247), module, CANARD::EOC_OUTPUT));

		addInput(createInput<PJ301MPort>(Vec(portX0[0]-4.5f, 277), module, CANARD::SPEED_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[1]-4.5f, 277), module, CANARD::FADE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[2]-4.5f, 277), module, CANARD::SLICE_INPUT));
		addInput(createInput<PJ301MPort>(Vec(portX0[3]-5.0f, 277), module, CANARD::CLEAR_INPUT));

		BidooTransientsBlueTrimpot *trim = createParam<BidooTransientsBlueTrimpot>(Vec(portX0[4]-1, 280), module, CANARD::THRESHOLD_PARAM);
		trim->module=module;
		addParam(trim);

		addParam(createParam<CKSS>(Vec(89, 325), module, CANARD::MODE_PARAM));

		addInput(createInput<TinyPJ301MPort>(Vec(8, 340), module, CANARD::INL_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(8+22, 340), module, CANARD::INR_INPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(150, 340), module, CANARD::OUTL_OUTPUT));
		addOutput(createOutput<TinyPJ301MPort>(Vec(150+22, 340), module, CANARD::OUTR_OUTPUT));
	}

	struct CANARDDeleteSlice : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			module->post({CANARD::DELETE_SLICE, "", module->selected, {}, 0.f});
		}
	};

	struct CANARDDeleteSliceMarker : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			module->post({CANARD::DELETE_MARKER, "", module->deleteSliceMarker, {}, 0.f});
		}
	};

	struct CANARDAddSliceMarker : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			module->post({CANARD::ADD_MARKER, "", module->addSliceMarker, {}, 0.f});
		}
	};

	struct CANARDTransientDetect : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			module->post({CANARD::TRANSIENTS, "", (int)(module->params[CANARD::THRESHOLD_PARAM].getValue() * 1000.f), {}, 0.f});
		}
	};

	struct CANARDLoadSample : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			std::string last = module->getPath();
			std::string dir = last.empty() ? asset::user("") : rack::system::getDirectory(last);
			char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
			if (path) {
				module->loadFile(path);
				free(path);
			}
		}
	};

	void onPathDrop(const PathDropEvent& e) override {
		Widget::onPathDrop(e);
		CANARD *module = dynamic_cast<CANARD*>(this->module);
		if (module && !e.paths.empty())
			module->loadFile(e.paths[0]);
	}

	struct CANARDSaveSample : MenuItem {
		CANARD *module;
		void onAction(const event::Action &e) override {
			std::string last = module->getPath();
			std::string dir = last.empty() ? asset::user("") : rack::system::getDirectory(last);
			std::string name = module->getFileName();
			std::string fileName = name.empty() ? "temp.wav" : name;
			char *path = osdialog_file(OSDIALOG_SAVE, dir.c_str(), fileName.c_str(), NULL);
			if (path) {
				module->post({CANARD::SAVE, path, 0, {}, 0.f});
				free(path);
			}
		}
	};


	void appendContextMenu(ui::Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		CANARD *module = dynamic_cast<CANARD*>(this->module);
		if (!module)
			return;

		menu->addChild(new MenuSeparator());
		menu->addChild(construct<CANARDDeleteSlice>(&MenuItem::text, "Delete slice", &CANARDDeleteSlice::module, module));
		menu->addChild(construct<CANARDDeleteSliceMarker>(&MenuItem::text, "Delete slice marker", &CANARDDeleteSliceMarker::module, module));
		menu->addChild(construct<CANARDAddSliceMarker>(&MenuItem::text, "Add slice marker", &CANARDAddSliceMarker::module, module));
		menu->addChild(construct<CANARDTransientDetect>(&MenuItem::text, "Detect transients", &CANARDTransientDetect::module, module));
		menu->addChild(construct<CANARDLoadSample>(&MenuItem::text, "Load sample", &CANARDLoadSample::module, module));
		menu->addChild(construct<CANARDSaveSample>(&MenuItem::text, "Save sample", &CANARDSaveSample::module, module));
		menu->addChild(createBoolPtrMenuItem("Reverse", "", &module->reverse));
	}
};

Model *modelCANARD = createModel<CANARD, CANARDWidget>("cANARd");
