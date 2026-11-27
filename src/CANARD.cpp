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
#include "SampleRead.hpp"

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
// builds a new one on the worker. The display and the patch storage share it
// (shared_ptr), so saving needs no copy and no wait.
struct CanardSample {
	static constexpr int OVERVIEW = 1024;
	std::vector<dsp::Frame<2>> frames;
	// Band-limited copies for playing faster than 1x; shared by the versions
	// that keep the same audio (a marker edit), built again when it changes.
	std::shared_ptr<const sampleread::Mip<2>> mip;
	std::vector<int> slices{0};
	float sampleRate = 0.f;
	float overviewMin[2][OVERVIEW] = {};
	float overviewMax[2][OVERVIEW] = {};
	float overviewRms[2][OVERVIEW] = {};

	int size() const { return (int)frames.size(); }

	// The audio changed: the mip copies are built again in finish().
	void audioChanged() {
		mip.reset();
	}

	void finish() {
		if (!mip && size() > 0) {
			auto m = std::make_shared<sampleread::Mip<2>>();
			m->build((const float *)frames.data(), size());
			mip = m;
		}
		std::sort(slices.begin(), slices.end());
		slices.erase(std::unique(slices.begin(), slices.end()), slices.end());
		slices.erase(std::remove_if(slices.begin(), slices.end(), [&](int s) { return s < 0 || (s > 0 && s >= size()); }), slices.end());
		if (slices.empty() || slices[0] != 0)
			slices.insert(slices.begin(), 0);
		int count = size();
		for (int b = 0; b < OVERVIEW; b++) {
			int i0 = (int)((long long)b * count / OVERVIEW), i1 = std::min(std::max((int)((long long)(b + 1) * count / OVERVIEW), i0 + 1), count);
			for (int c = 0; c < 2; c++)
				wavestyle::binOf(i0, i1, [&](int64_t i) { return frames[i].samples[c]; }, overviewMin[c][b], overviewMax[c][b], overviewRms[c][b]);
		}
	}
};

// Recording goes into chunks the worker allocates in advance.
struct CanardChunk {
	static constexpr int FRAMES = 16384;
	dsp::Frame<2> f[FRAMES];
	int used = 0;
};

// One playing head. cANARd plays one per polyphonic channel of TRIG / GATE / V/O: the same sample at several
// pitches or places at once; each channel has its own slice, start, length and fade CVs.
struct CanardVoice {
	bool play = false;
	double samplePos = 0.0;  // double: a float loses the pitch far into long samples
	float sampleStart = 0.0f, loopLength = 0.0f, fadeLenght = 0.0f;
	float direction = 1.0f;  // ping-pong flips it
	float speed = 1.0f;
	float speedOct = 0.f;    // speed = 2^speedOct, exp2 only when it moves
	sampleread::Declick<2> dc;
	sampleread::OctaveCache oct;   // log2 of its speed, for the band-limited read
	sampleread::Declick<2> dc2;  // a tail still fading out when another jump comes (two jumps within 2 ms)
	float lastStep = 0.f, lastGain = 0.f;  // of the last sample played
	double lastPos = 0.0;
	// The fade at the edges of the played region, slewed: a FADE or START change (CV or knob) glides over the declick
	// length instead of stepping the gain
	float regionGain = 1.f;
	bool regionJump = true;  // a new head: the gain starts where it should be
	sampleread::Declick<2> fadeOld[3];  // its heads on a sample being replaced, fading out
	dsp::SchmittTrigger trigTrigger;
	dsp::SchmittTrigger gateTrigger;
	dsp::PulseGenerator eocPulse;

	// The head jumps: it fades out from where it was, without cutting a tail already fading out
	void jumpTail(double pos, float step, float gain, int len) {
		if (gain == 0.f)
			return;
		if (dc.tailActive())
			dc2 = dc;
		dc.tail(pos, step, gain, len);
	}
	bool fadingOld() const { return fadeOld[0].tailActive() || fadeOld[1].tailActive() || fadeOld[2].tailActive(); }
	void silence() {
		play = false;
		dc.reset();
		dc2.reset();
		eocPulse.reset();
	}
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
	bool record = false;
	int totalSampleCount = 0;
	bool reverse = false;        // plays the slices backward
	CanardVoice voices[16];
	int nVoices = 1;
	// for the display: the first voice's region, and every playing head
	double samplePos = 0.0;
	float sampleStart = 0.0f, loopLength = 0.0f, fadeLenght = 0.0f;
	float heads[16] = {};
	std::atomic<int> headCount{0};
	// A sample replaced while it plays: the old one stays alive until its heads have faded out (then it is retired)
	CanardSample *fading = nullptr;
	dsp::SchmittTrigger recordTrigger;
	dsp::SchmittTrigger clearTrigger;

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
	bool loadStored = false;               // set by dataFromJson, used by onAdd
	std::vector<int> storedSlices;
	int storedRate = 0;

	// Worker: loads, saves, slice edits, recordings.
	enum JobType { LOAD, SAVE, CLEAR, DELETE_SLICE, ADD_MARKER, DELETE_MARKER, TRANSIENTS, LOAD_STORED, RESAMPLE };
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
	// Worker: owns every sample it published (the audio thread only borrows
	// them); one is let go once the audio thread retired it.
	std::vector<std::shared_ptr<CanardSample>> owned;

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
		configParam(SPEED_PARAM, -4.0f, 4.0f, 0.0f, "Tune", " semitones", 0.f, 12.f);
		configParam(FADE_PARAM, 0.0f, 10.0f, 0.0f, "Fade", "%", 0.f, 5.f);
		configParam(SLICE_PARAM, 0.0f, 10.0f, 0.0f, "Slice");
		configButton(CLEAR_PARAM, "Clear the sample");
		configParam(THRESHOLD_PARAM, 0.01f, 10.0f, 1.0f, "Transient threshold (Shift-click: detect)");
		configSwitch(MODE_PARAM, 0, 1, 0, "Slice mode", {"Off", "On"});
		configInput(INL_INPUT, "Left in");
		configInput(INR_INPUT, "Right in");
		configInput(TRIG_INPUT, "Trigger (polyphonic: one voice per channel)");
		configInput(GATE_INPUT, "Gate (polyphonic: one voice per channel)");
		configInput(SAMPLE_START_INPUT, "Start CV (polyphonic: per voice)");
		configInput(LOOP_LENGTH_INPUT, "Length CV (polyphonic: per voice)");
		configInput(READ_MODE_INPUT, "Read mode CV");
		configInput(SPEED_INPUT, "V/O: 1 V per octave, 0 V plays the sample at its own pitch (added to TUNE, 4 octaves up or down at most; polyphonic: one voice per channel)");
		configInput(RECORD_INPUT, "Record trigger");
		configInput(FADE_INPUT, "Fade CV (polyphonic: per voice)");
		configInput(SLICE_INPUT, "Slice CV (polyphonic: per voice)");
		configInput(CLEAR_INPUT, "Clear trigger");
		configOutput(OUTL_OUTPUT, "Left (one channel per voice)");
		configOutput(OUTR_OUTPUT, "Right (one channel per voice)");
		configOutput(EOC_OUTPUT, "End of cycle (one channel per voice)");
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
		// pending, retired and smp are all in `owned`.
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

	// Worker: lets go of a sample the audio thread no longer uses.
	void disown(CanardSample *p) {
		if (p)
			owned.erase(std::remove_if(owned.begin(), owned.end(), [p](const std::shared_ptr<CanardSample> &o) { return o.get() == p; }), owned.end());
	}

	void releaseRetired() {
		disown(retired.exchange(nullptr, std::memory_order_acq_rel));
	}

	// Worker: hands a new sample (or none) to the audio thread. The audio
	// thread takes it even while the module is bypassed (processBypass).
	void publish(CanardSample *s) {
		std::shared_ptr<CanardSample> sp;
		if (s) {
			s->finish();
			sp.reset(s);
			owned.push_back(sp);
		}
		{
			std::lock_guard<std::mutex> ui(uiMutex);
			uiSample = sp;
		}
		while (pendingSet.load() && !quit) {
			// The audio thread takes the new one only once the old one is freed.
			releaseRetired();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		disown(pending.exchange(s, std::memory_order_acq_rel));
		pendingSet = true;
	}

	CanardSample *copyLive() {
		// An edit starts from the newest sample: wait until the audio thread took it.
		while (pendingSet.load() && !quit) {
			releaseRetired();
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		CanardSample *l = live.load(std::memory_order_acquire);
		return l ? new CanardSample(*l) : nullptr;
	}

	// A slice where the energy of both channels jumps, cut at the zero
	// crossing (a sign change of the mid signal) just before its block, so
	// the onset stays whole.
	void detectTransients(CanardSample *s, float threshold) {
		s->slices.clear();
		s->slices.push_back(0);
		const int size = 256;
		auto mid = [&](int i) { return s->frames[i].samples[0] + s->frames[i].samples[1]; };
		float prevNrgy = 0.0f;
		for (int i = 0; i + size < s->size(); i += size) {
			float nrgy = 0.0f;
			for (int k = 0; k < size; k++) {
				float l = s->frames[i + k].samples[0], r = s->frames[i + k].samples[1];
				nrgy += 50 * (l * l + r * r) / size;
			}
			if ((nrgy > threshold) && (nrgy > 10 * prevNrgy)) {
				int at = i;
				for (int k = i; k > std::max(i - size, 0); k--) {
					float a = mid(k - 1), b = mid(k);
					if (b == 0.f || (a < 0.f) != (b < 0.f)) {
						at = (std::fabs(a) < std::fabs(b)) ? k - 1 : k;
						break;
					}
				}
				s->slices.push_back(at);
			}
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
			case RESAMPLE: {
				CanardSample *s = copyLive();
				if (!s)
					break;
				double ratio = job.rate / s->sampleRate;
				s->frames = waves::resampleStereo(s->frames, s->sampleRate, job.rate);
				s->audioChanged();
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
					s->audioChanged();
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
			releaseRetired();
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
				s->audioChanged();
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

	// Takes a new sample from the worker, once it has freed the previous one.
	void takePending(int fadeLen = 0) {
		if (pendingSet.load(std::memory_order_acquire) && !retired.load(std::memory_order_acquire) && !fading) {
			CanardSample *s = pending.exchange(nullptr, std::memory_order_acq_rel);
			bool sounding = false;
			for (const CanardVoice &v : voices)
				sounding = sounding || v.play || v.dc.tailActive() || v.dc2.tailActive();
			if (fadeLen > 0 && smp && sounding) {
				// Playing: the old sample's heads (the playing ones and their tails) fade out from it while the new one fades in
				for (CanardVoice &v : voices) {
					v.fadeOld[0] = v.dc;
					v.fadeOld[1] = v.dc2;
					v.fadeOld[2].reset();
					if (v.play)
						v.fadeOld[2].tail(v.samplePos + v.lastStep, v.lastStep, v.lastGain, fadeLen);
				}
				fading = smp;
			}
			else
				retired.store(smp, std::memory_order_release);
			smp = s;
			live.store(s, std::memory_order_release);
			pendingSet = false;
			totalSampleCount = smp ? smp->size() : 0;
			for (CanardVoice &v : voices) {
				v.dc.reset();  // their tails read the old sample: fadeOld took them
				v.dc2.reset();
				if (v.samplePos >= totalSampleCount)
					v.play = false;
				else if (v.play) {
					v.dc.fadeIn(fadeLen);
					v.regionJump = true;
				}
			}
		}
	}

	// The old sample is retired once its heads are silent (and the worker took the one before)
	void finishFading(bool now) {
		if (!fading || retired.load(std::memory_order_acquire))
			return;
		bool silent = true;
		for (const CanardVoice &v : voices)
			silent = silent && !v.fadingOld();
		if (now || silent) {
			retired.store(fading, std::memory_order_release);
			fading = nullptr;
			for (CanardVoice &v : voices)
				for (auto &f : v.fadeOld)
					f.reset();
		}
	}

	// Bypassed: still takes the worker's samples, so it never waits.
	void processBypass(const ProcessArgs &args) override {
		finishFading(true);
		takePending();
		Module::processBypass(args);
	}

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
		// The newest sample, shared with the display: written from here, with
		// no wait on the worker (busy or not, bypassed or not).
		std::shared_ptr<CanardSample> s;
		{
			std::lock_guard<std::mutex> lock(uiMutex);
			s = uiSample;
		}
		if (s && s->size() > 0 && waves::saveWave(s->frames, s->sampleRate, path))
			storeDirty = false;
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

	// The played region of channel c: the slice, start, length and fade CVs are per channel
	void calcLoop(int c, CanardVoice &v) {
		int sliceStart = 0;
		int sliceEnd = totalSampleCount > 0 ? totalSampleCount - 1 : 0;
		if (smp && params[MODE_PARAM].getValue() == 1 && smp->slices.size() > 0) {
			int n = smp->slices.size();
			int slice = clamp((int)std::round(clamp(params[SLICE_PARAM].getValue() + inputs[SLICE_INPUT].getPolyVoltage(c), 0.0f, 10.0f) * (n - 1) / 10), 0, n - 1);
			sliceStart = smp->slices[slice];
			sliceEnd = (slice < n - 1) ? (smp->slices[slice + 1] - 1) : (totalSampleCount - 1);
		}
		if (totalSampleCount > 0) {
			v.sampleStart = rescale(clamp(inputs[SAMPLE_START_INPUT].getPolyVoltage(c) + params[SAMPLE_START_PARAM].getValue(), 0.0f, 10.0f), 0.0f, 10.0f, sliceStart, sliceEnd);
			v.loopLength = clamp(rescale(clamp(inputs[LOOP_LENGTH_INPUT].getPolyVoltage(c) + params[LOOP_LENGTH_PARAM].getValue(), 0.0f, 10.0f), 0.0f, 10.0f, 0.0f, sliceEnd - sliceStart + 1), 1.0f, std::max(sliceEnd - v.sampleStart, 1.0f));
			v.fadeLenght = rescale(clamp(inputs[FADE_INPUT].getPolyVoltage(c) + params[FADE_PARAM].getValue(), 0.0f, 10.0f), 0.0f, 10.0f, 0.0f, std::floor(v.loopLength / 2));
		}
		else {
			v.loopLength = 0;
			v.sampleStart = 0;
			v.fadeLenght = 0;
		}
	}

	void initPos(CanardVoice &v) {
		v.direction = 1.0f;
		v.samplePos = reverse ? v.sampleStart + v.loopLength : v.sampleStart;
	}

	void playVoice(int c, CanardVoice &v, int trigMode, int readMode, int fadeLen, float *out);
};

void CANARD::process(const ProcessArgs &args) {
	const int fadeLen = sampleread::declickLength(args.sampleRate);
	finishFading(false);
	takePending(fadeLen);
	if (smp && smp->sampleRate != args.sampleRate && busy.load() == 0 && recDone.load() < 0 && reloadRate.load() == 0.f)
		reloadRate = args.sampleRate;

	if (clearTrigger.process(inputs[CLEAR_INPUT].getVoltage() + params[CLEAR_PARAM].getValue(), 0.1f, 1.f)) {
		// Through the worker, like any change of the sample.
		clearRequested = true;
		for (CanardVoice &v : voices) {
			if (v.play)
				v.jumpTail(v.samplePos + v.lastStep, v.lastStep, v.lastGain, fadeLen);
			v.play = false;
		}
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
		// (none yet, right after the module was added: the recording waits for the worker's first chunk)
		else if (recCount > 0) {
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
	// as many voices as the TRIG (or GATE) or V/O cable has channels
	const int trigChannels = trigMode == 1 ? inputs[TRIG_INPUT].getChannels() : trigMode == 2 ? inputs[GATE_INPUT].getChannels() : 1;
	const int n = clamp(std::max(trigChannels, inputs[SPEED_INPUT].getChannels()), 1, 16);
	for (int c = n; c < nVoices; c++)
		voices[c].silence();
	nVoices = n;

	int heads_ = 0;
	for (int c = 0; c < n; c++) {
		float out[2];
		playVoice(c, voices[c], trigMode, readMode, fadeLen, out);
		outputs[OUTL_OUTPUT].setVoltage(canardSoftLimit(out[0] * 5.0f), c);
		outputs[OUTR_OUTPUT].setVoltage(canardSoftLimit(out[1] * 5.0f), c);
		outputs[EOC_OUTPUT].setVoltage(voices[c].eocPulse.process(args.sampleTime) ? 10.0f : 0.0f, c);
		if (voices[c].play)
			heads[heads_++] = (float)voices[c].samplePos;
	}
	outputs[OUTL_OUTPUT].setChannels(n);
	outputs[OUTR_OUTPUT].setChannels(n);
	outputs[EOC_OUTPUT].setChannels(n);
	headCount.store(heads_, std::memory_order_relaxed);
	// the display shows the first voice's region
	samplePos = voices[0].samplePos;
	sampleStart = voices[0].sampleStart;
	loopLength = voices[0].loopLength;
	fadeLenght = voices[0].fadeLenght;
}

// One voice, one sample: out gets its stereo frame (5 V = 1)
void CANARD::playVoice(int c, CanardVoice &v, int trigMode, int readMode, int fadeLen, float *out) {
	// Declicked restarts and stops: the old head fades out over 2 ms while
	// the new one fades in.
	// (samplePos is where the last sample played: the old head goes on from
	// one step further.)
	auto restart = [&]() {
		if (v.play)
			v.jumpTail(v.samplePos + v.lastStep, v.lastStep, v.lastGain, fadeLen);
		v.dc.fadeIn(fadeLen);
		v.regionJump = true;
	};
	auto stop = [&]() {
		if (v.play)
			v.jumpTail(v.samplePos + v.lastStep, v.lastStep, v.lastGain, fadeLen);
		v.play = false;
	};
	// V/O: exactly 1 V per octave on top of TUNE; 16x either way is the most the band-limited reading plays cleanly
	float oct = clamp(params[SPEED_PARAM].getValue() + inputs[SPEED_INPUT].getPolyVoltage(c), -4.f, 4.f);
	if (oct != v.speedOct) {
		v.speedOct = oct;
		v.speed = std::exp2(oct);
	}
	calcLoop(c, v);
	const float sliceCv = clamp(params[SLICE_PARAM].getValue() + inputs[SLICE_INPUT].getPolyVoltage(c), 0.0f, 10.0f);

	bool trig = v.trigTrigger.process(inputs[TRIG_INPUT].getPolyVoltage(c), 0.1f, 1.f);
	bool gateRise = v.gateTrigger.process(inputs[GATE_INPUT].getPolyVoltage(c), 0.1f, 1.f);
	bool gateHigh = v.gateTrigger.isHigh();
	float end = v.sampleStart + v.loopLength;
	bool advance = false;

	bool scrub = false;
	if (trigMode == 1) {
		if (trig) {
			restart();
			initPos(v);
			if (smp && smp->slices.size() == 1 && inputs[SLICE_INPUT].isConnected())
				v.samplePos = v.sampleStart + v.loopLength * sliceCv * 0.1f;
			v.play = totalSampleCount > 0;
		}
		else
			advance = v.play;
	}
	else if (trigMode == 2) {
		if (gateHigh) {
			if (inputs[SLICE_INPUT].isConnected()) {
				// Scrubbing: the slice CV sets the position.
				scrub = true;
				if (!v.play)
					v.dc.fadeIn(fadeLen);
				v.play = totalSampleCount > 0;
				v.samplePos = v.sampleStart + sliceCv * 0.1f * v.loopLength;
			}
			else if (gateRise) {
				restart();
				initPos(v);
				v.play = totalSampleCount > 0;
			}
			else
				advance = v.play;
		}
		else
			stop();
	}
	else
		stop();

	float step = 0.f;
	// A real jump (more than a few frames, and not while the last one still fades in) is declicked; a region that
	// glides past the head (an LFO on START) carries it along, which is continuous
	const bool forward = (reverse ? -1.f : 1.f) * v.direction > 0.f;
	const double behind = forward ? v.sampleStart - v.samplePos : v.samplePos - end;
	if (advance && behind > 8.0 && v.dc.in >= 1.f) {
		v.jumpTail(v.samplePos + v.lastStep, v.lastStep, v.lastGain, fadeLen);
		v.samplePos = std::min(std::max(v.samplePos, (double)v.sampleStart), (double)end);
		v.dc.fadeIn(fadeLen);
		v.regionJump = true;
	}
	if (advance) {
		// Forward, or backward with reverse; ping-pong flips the direction.
		float dir = (reverse ? -1.f : 1.f) * v.direction;
		step = dir * v.speed;
		v.samplePos += step;
		bool pastEnd = dir > 0 ? v.samplePos >= end : v.samplePos <= v.sampleStart;
		if (pastEnd) {
			if (readMode == 0) {
				v.play = false;
				v.eocPulse.trigger(1e-3f);
			}
			else if (readMode == 1) {
				// The loop jump: the old head goes on, fading out.
				v.jumpTail(v.samplePos, step, v.lastGain, fadeLen);
				initPos(v);
				v.dc.fadeIn(fadeLen);
				v.regionJump = true;
			}
			else {
				v.direction = -v.direction;
				v.samplePos = std::min(std::max(v.samplePos, (double)v.sampleStart), (double)end);
			}
		}
		v.samplePos = std::min(std::max(v.samplePos, (double)v.sampleStart), (double)end);
	}
	else if (scrub)
		step = (float)(v.samplePos - v.lastPos);

	float acc[2] = {0.f, 0.f};
	if (v.play && smp && totalSampleCount > 1) {
		float fade = v.dc.nextIn();
		float rg = 1.f;
		if (v.fadeLenght >= 1.0f) {
			if ((v.samplePos - v.sampleStart) < v.fadeLenght)
				rg = (float)(v.samplePos - v.sampleStart) / v.fadeLenght;
			else if ((end - v.samplePos) < v.fadeLenght)
				rg = (float)(end - v.samplePos) / v.fadeLenght;
		}
		if (v.regionJump)
			v.regionGain = rg;
		else
			v.regionGain += clamp(rg - v.regionGain, -1.f / fadeLen, 1.f / fadeLen);
		v.regionJump = false;
		fade *= v.regionGain;
		if (readMode == 0 && !scrub) {
			// Played once: at least 2 ms of fade before the end it stops at.
			float dir = (reverse ? -1.f : 1.f) * v.direction;
			float left = (float)(dir > 0 ? end - v.samplePos : v.samplePos - v.sampleStart) / v.speed;
			if (left < fadeLen)
				fade *= std::max(left, 0.f) / fadeLen;
		}
		double p = std::min(std::max(v.samplePos, 0.0), totalSampleCount - 1.001);
		const sampleread::View<2> view{(const float *)smp->frames.data(), totalSampleCount, smp->mip.get()};
		view.read(p, std::fabs(step == 0.f ? v.speed : step), acc, v.oct);
		acc[0] *= fade;
		acc[1] *= fade;
		v.lastGain = fade;
		v.lastStep = step;
	}
	v.lastPos = v.samplePos;
	if (smp && totalSampleCount > 1) {
		const sampleread::View<2> view{(const float *)smp->frames.data(), totalSampleCount, smp->mip.get()};
		v.dc.addTail(view, acc);
		v.dc2.addTail(view, acc);
	}
	if (fading && fading->size() > 1) {
		const sampleread::View<2> old{(const float *)fading->frames.data(), fading->size(), fading->mip.get()};
		for (auto &f : v.fadeOld)
			f.addTail(old, acc);
	}
	out[0] = acc[0];
	out[1] = acc[1];
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

	void drawWave(const DrawArgs& args, const CanardSample &s, int c, float top, float activeX0, float activeX1) {
		Rect b = Rect(Vec(zoomLeftAnchor, top), Vec(zoomWidth, height));
		nvgScissor(args.vg, 0, b.pos.y, width, height);
		// peaks and RMS core, one column per screen pixel; the part that plays in hot pink, the rest dimmed
		// (src/WaveStyle.hpp)
		const int total = s.size();
		const double perUnit = (double)total / b.size.x;
		wavestyle::wave(args.vg, width, b.pos.y + b.size.y * 0.5f, b.size.y * 0.5f, activeX0, activeX1, [&](float x0, float x1) {
			return wavestyle::colOfSample((x0 - b.pos.x) * perUnit, (x1 - b.pos.x) * perUnit, total, s.overviewMin[c], s.overviewMax[c],
				s.overviewRms[c], CanardSample::OVERVIEW, (int)s.frames.size() >= total, [&](int64_t j) { return s.frames[j].samples[c]; });
		});
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1 && module) {
			std::shared_ptr<CanardSample> s = shown();
			float nbSample = module->totalSampleCount;
			if (s && nbSample > 0) {
				auto px = [&](float idx) { return idx * zoomWidth / nbSample + zoomLeftAnchor; };
				nvgScissor(args.vg, 0, 0, width, 2*height+10);

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

				// The loop, its fades as slopes, in the samplers' loop pink
				nvgFillColor(args.vg, wavestyle::kLoop);
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

				// Waveform: hot pink over the part that plays
				const float playX0 = px(module->sampleStart), playX1 = px(module->sampleStart + module->loopLength);
				nvgSave(args.vg);
				drawWave(args, *s, 0, 0, playX0, playX1);
				drawWave(args, *s, 1, height + 10, playX0, playX1);
				nvgRestore(args.vg);
				// START and END of the part that plays
				wavestyle::bar(args.vg, playX0, 0, 2*height+10, 1);
				wavestyle::bar(args.vg, playX1, 0, 2*height+10, -1);

				// Slices
				if (sliceMode)
					for (int x : s->slices)
						wavestyle::bar(args.vg, px(x), 0, 2*height+10, 0, x != module->deleteSliceMarker ? wavestyle::kBar : RED_BIDOO);
				// the play heads, on top: they light up the pink under them (one per playing voice; the first
				// voice's place when none plays)
				const int nh = module->headCount.load(std::memory_order_relaxed);
				if (nh == 0)
					wavestyle::head(args.vg, px(module->samplePos), 0, 2*height+10);
				for (int h = 0; h < nh; h++)
					wavestyle::head(args.vg, px(module->heads[h]), 0, 2*height+10);
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

		theme::addScrews(this);

		// The original hand-drawn panel: the screen; five columns, each a control over its CV jack joined by a line,
		// in two rows, with the EOC box and the threshold trimpot in the last column; the mode switch; the stereo in
		// and out. Every column is now on the line and label the panel draws (the old code had its controls 0.14 to
		// 1.15 px off them); each stereo jack under its L or R label; rows keep their old tops.
		CANARDDisplay *display = new CANARDDisplay();
		display->module = module;
		display->box = theme::specificRect(10, 35, 175, 110);
		addChild(display);

		const theme::Pos col[5] = {theme::specific(24.f, 0), theme::specific(60.938f, 0), theme::specific(98.f, 0), theme::specific(133.625f, 0), theme::specific(168.719f, 0)};
		const theme::Pos knobRow = theme::specificCorner(0, 170, theme::kBlueKnobSize);
		const theme::Pos buttonRow = theme::specificCorner(0, 170, theme::kCKD6Size);
		const theme::Pos jackRow = theme::specificCorner(0, 202, theme::kJackSize);
		const float kLowerBelow = 75.f;   // from the upper row of controls and jacks to the lower one
		const theme::Size kSmallLightSize = {mm2px(2.f), mm2px(2.f)};   // SmallLight
		const theme::Size kCKSSSize = {14.f, 20.64111f};                // CKSS
		auto at = [&](int c, theme::Pos row) { return theme::colRow(col[c], row); };

		addChild(theme::light<SmallLight<RedLight>>(theme::specificCorner(6, 167, kSmallLightSize), module, CANARD::REC_LIGHT));   // beside REC, as drawn
		addParam(theme::param<BlueCKD6>(at(0, buttonRow), module, CANARD::RECORD_PARAM));
		addParam(theme::param<BidooBlueKnob>(at(2, knobRow), module, CANARD::SAMPLE_START_PARAM));
		addParam(theme::param<BidooBlueKnob>(at(3, knobRow), module, CANARD::LOOP_LENGTH_PARAM));
		addParam(theme::param<BidooBlueSnapKnob>(at(4, knobRow), module, CANARD::READ_MODE_PARAM));
		addInput(theme::input<PJ301MPort>(at(0, jackRow), module, CANARD::RECORD_INPUT));
		addInput(theme::input<PJ301MPort>(at(1, jackRow), module, CANARD::TRIG_INPUT));
		addInput(theme::input<PJ301MPort>(at(1, theme::specificCorner(0, 172, theme::kJackSize)), module, CANARD::GATE_INPUT));
		addInput(theme::input<PJ301MPort>(at(2, jackRow), module, CANARD::SAMPLE_START_INPUT));
		addInput(theme::input<PJ301MPort>(at(3, jackRow), module, CANARD::LOOP_LENGTH_INPUT));
		addInput(theme::input<PJ301MPort>(at(4, jackRow), module, CANARD::READ_MODE_INPUT));

		addParam(theme::param<BidooBlueKnob>(at(0, knobRow.down(kLowerBelow)), module, CANARD::SPEED_PARAM));
		addParam(theme::param<BidooBlueKnob>(at(1, knobRow.down(kLowerBelow)), module, CANARD::FADE_PARAM));
		addParam(theme::param<BidooBlueKnob>(at(2, knobRow.down(kLowerBelow)), module, CANARD::SLICE_PARAM));
		addParam(theme::param<BlueCKD6>(at(3, buttonRow.down(kLowerBelow)), module, CANARD::CLEAR_PARAM));
		addOutput(theme::output<PJ301MPort>(at(4, theme::specificCorner(0, 247, theme::kJackSize)), module, CANARD::EOC_OUTPUT));   // in the EOC box
		addInput(theme::input<PJ301MPort>(at(0, jackRow.down(kLowerBelow)), module, CANARD::SPEED_INPUT));
		addInput(theme::input<PJ301MPort>(at(1, jackRow.down(kLowerBelow)), module, CANARD::FADE_INPUT));
		addInput(theme::input<PJ301MPort>(at(2, jackRow.down(kLowerBelow)), module, CANARD::SLICE_INPUT));
		addInput(theme::input<PJ301MPort>(at(3, jackRow.down(kLowerBelow)), module, CANARD::CLEAR_INPUT));
		BidooTransientsBlueTrimpot *trim = theme::param<BidooTransientsBlueTrimpot>(at(4, theme::specificCorner(0, 280, theme::kTrimpotSize)), module, CANARD::THRESHOLD_PARAM);
		trim->module=module;
		addParam(trim);
		addParam(theme::param<CKSS>(theme::specificCorner(89, 325, kCKSSSize), module, CANARD::MODE_PARAM));

		const theme::Pos stereoRow = theme::specificCorner(8, 340, theme::kTinyJackSize);
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specific(14.594f, 0), stereoRow), module, CANARD::INL_INPUT));
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specific(38.125f, 0), stereoRow), module, CANARD::INR_INPUT));
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(theme::specific(157.281f, 0), stereoRow), module, CANARD::OUTL_OUTPUT));
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(theme::specific(178.812f, 0), stereoRow), module, CANARD::OUTR_OUTPUT));
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
