// sOUNAiRe: a sampler. One sample over a key range, 16 voices (poly V/oct and
// gate), loops with crossfades, reverse, legato, multimode filter, Signalsmith
// time-stretch, and the sample kept in the patch as FLAC. In SLICES mode the
// sample is cut at its transients and each slice plays on its own key.
#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"
#include "SounaireEngine.hpp"
#include "SounaireFlac.hpp"
#include "dep/dr_wav/dr_wav.h"
#define DR_FLAC_IMPLEMENTATION
#include "dep/dr_flac/dr_flac.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <osdialog.h>

using namespace sounaire;

namespace {

// WAV, AIFF or FLAC into a Sample (worker thread)
std::shared_ptr<Sample> loadSampleFile(const std::string &path, std::string &error) {
	unsigned int channels = 0, rate = 0;
	uint64_t count = 0;
	float *frames = nullptr;
	int bits = 24;
	std::string ext = rack::string::lowercase(rack::system::getExtension(path));
	if (ext == ".flac") {
		drflac *f = drflac_open_file(path.c_str(), nullptr);
		if (f) {
			bits = f->bitsPerSample;
			drflac_close(f);
		}
		drflac_uint64 n = 0;
		frames = drflac_open_file_and_read_pcm_frames_f32(path.c_str(), &channels, &rate, &n, nullptr);
		count = n;
		if (frames && bits)
			bits = bits <= 16 ? 16 : 24;
	}
	else {
		drwav wav;
		if (drwav_init_file(&wav, path.c_str(), nullptr)) {
			bits = (wav.translatedFormatTag == DR_WAVE_FORMAT_PCM && wav.bitsPerSample <= 16) ? 16 : 24;
			drwav_uninit(&wav);
		}
		drwav_uint64 n = 0;
		frames = drwav_open_file_and_read_pcm_frames_f32(path.c_str(), &channels, &rate, &n, nullptr);
		count = n;
	}
	if (!frames || count == 0 || channels == 0) {
		error = "not a WAV, AIFF or FLAC file";
		if (frames)
			free(frames);
		return nullptr;
	}
	auto s = std::make_shared<Sample>();
	s->channels = channels >= 2 ? 2 : 1;
	s->frames = (int64_t)count;
	s->rate = (float)rate;
	s->bits = bits;
	s->name = rack::system::getFilename(path);
	for (int c = 0; c < s->channels; c++) {
		s->ch[c].resize(count);
		for (uint64_t i = 0; i < count; i++)
			s->ch[c][i] = frames[i * channels + c];
	}
	free(frames);
	s->analyse();
	return s;
}

std::string noteName(float n) {
	static const char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	int i = (int)std::round(n);
	return std::string(names[((i % 12) + 12) % 12]) + std::to_string((int)std::floor(i / 12.f) - 1);
}

// note names in the tooltips of ROOT, LOW, HIGH and CUTOFF
struct NoteQuantity : engine::ParamQuantity {
	std::string getDisplayValueString() override {
		return noteName(getValue());
	}
};

}  // namespace

struct SOUNAIRE : BidooModule {
	enum ParamIds {
		START_PARAM,
		END_PARAM,
		LOOP_START_PARAM,
		LOOP_END_PARAM,
		LOOP_MODE_PARAM,
		XFADE_PARAM,
		REVERSE_PARAM,
		LEGATO_PARAM,
		ROOT_PARAM,
		LOW_PARAM,
		HIGH_PARAM,
		TUNE_PARAM,
		FINE_PARAM,
		SPEED_PARAM,
		TIME_MODE_PARAM,
		ATTACK_PARAM,
		RELEASE_PARAM,
		FILTER_MODE_PARAM,
		SLOPE_PARAM,
		CUTOFF_PARAM,
		RESONANCE_PARAM,
		LEVEL_PARAM,
		MODE_PARAM,
		PLAY_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		VOCT_INPUT,
		GATE_INPUT,
		VEL_INPUT,
		BEND_INPUT,
		START_INPUT,
		SPEED_INPUT,
		CUTOFF_INPUT,
		RES_INPUT,
		PITCH_INPUT,
		PLAY_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		L_OUTPUT,
		R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		REVERSE_LIGHT,
		LEGATO_LIGHT,
		NUM_LIGHTS
	};

	sounaire::Engine engine;

	// ---- the sample, shared by the threads
	// the worker owns every loaded sample; the audio thread plays raw pointers to
	// them and hands back the ones no voice uses any more; the display reads `shown`
	std::mutex sampleMutex;
	std::vector<std::shared_ptr<Sample>> owned;
	std::shared_ptr<Sample> shown;
	std::atomic<Sample *> pending {nullptr};
	std::atomic<const Sample *> unused {nullptr};
	std::atomic<bool> dirty {false};      // the sample in the patch storage is out of date
	std::atomic<float> pendingRoot {-1000.f};
	std::atomic<bool> clearCents {false};   // a new sample came without a detected root
	std::atomic<bool> loading {false};
	std::string lastError;

	// ---- worker
	std::thread worker;
	std::mutex jobMutex;
	std::condition_variable jobCv;
	bool quit = false;
	std::string jobPath;
	bool jobKeep = false;          // keep markers and root (a patch being loaded)
	bool jobAnalyse = false;
	double jobFrom = 0, jobTo = 1;
	int jobDetect = -1;            // transients to find, at this sensitivity
	// stretchers: built and warmed up by the worker, swapped in by the audio thread
	int jobPoolPreset = -1;
	float jobPoolRate = 0.f;
	std::atomic<Stretcher *> pendingPool {nullptr};
	std::atomic<int> pendingPoolPreset {-1};
	std::atomic<float> pendingPoolRate {0.f};
	std::atomic<Stretcher *> retiredPool {nullptr};
	bool poolRequested = false;

	// ---- options (context menu), saved
	bool autoNormalize = true;
	bool autoRoot = true;
	bool polyOut = true;
	int bendRange = 2;
	float rootCents = 0.f;         // the detected root's offset from the note
	float rootSeen = 60.f;         // ROOT as last seen: a change by hand clears rootCents
	std::string sampleName;

	// ---- slices (SLICES mode)
	// the UI and the worker edit `uiSlices` under sliceMutex and publish a copy;
	// the audio thread swaps the copy in and hands the old one back
	std::mutex sliceMutex;
	Slices uiSlices;
	std::atomic<int> selectedSlice {0};
	std::atomic<Slices *> pendingSlices {nullptr};
	std::atomic<Slices *> retiredSlices {nullptr};
	Slices *slices = nullptr;      // audio thread
	bool detectOnLoad = true;      // saved
	int sensitivity = 1;           // 0 low, 1 medium, 2 high; saved
	std::atomic<int> lastSlice {-1};   // the slice played last, for the display
	std::atomic<int> keyHeld[MAX_VOICES];  // the key each held gate plays, -1: none (for the keyboard strip)
	// a key clicked on the keyboard strip: plays while the mouse is held
	std::atomic<int> mouseKey {-1};         // a new key to start
	std::atomic<bool> mouseHeld {false};
	std::atomic<int> mouseShown {-1};       // the key it plays, for the strip
	bool mouseOn = false;

	// ---- audio thread state
	bool held[MAX_VOICES] = {};
	int legatoChannel = 0;
	float outL[MAX_VOICES] = {}, outR[MAX_VOICES] = {};
	// per channel speed: exp2 worked out again only when knob + CV move
	float speedX[MAX_VOICES], speedOut[MAX_VOICES];
	// preview (Alt+click on the display)
	std::atomic<float> previewPos {-1.f};
	std::atomic<bool> previewHeld {false};
	bool previewing = false;

	SOUNAIRE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(START_PARAM, 0.f, 1.f, 0.f, "Start", "%", 0.f, 100.f);
		configParam(END_PARAM, 0.f, 1.f, 1.f, "End", "%", 0.f, 100.f);
		configParam(LOOP_START_PARAM, 0.f, 1.f, 0.f, "Loop start", "%", 0.f, 100.f);
		configParam(LOOP_END_PARAM, 0.f, 1.f, 1.f, "Loop end", "%", 0.f, 100.f);
		configSwitch(LOOP_MODE_PARAM, 0.f, 3.f, 0.f, "Loop", {"Off", "Forward", "Ping-pong", "Reverse"});
		configParam(XFADE_PARAM, 0.f, 500.f, 10.f, "Loop crossfade", " ms");
		configSwitch(REVERSE_PARAM, 0.f, 1.f, 0.f, "Reverse", {"Off", "On"});
		configSwitch(LEGATO_PARAM, 0.f, 1.f, 0.f, "Legato (one voice)", {"Off", "On"});
		configParam<NoteQuantity>(ROOT_PARAM, 0.f, 127.f, 60.f, "Root note");
		configParam<NoteQuantity>(LOW_PARAM, 0.f, 127.f, 0.f, "Lowest note");
		configParam<NoteQuantity>(HIGH_PARAM, 0.f, 127.f, 127.f, "Highest note");
		for (int p : {ROOT_PARAM, LOW_PARAM, HIGH_PARAM})
			paramQuantities[p]->snapEnabled = true;
		configParam(TUNE_PARAM, -24.f, 24.f, 0.f, "Tune", " semitones");
		paramQuantities[TUNE_PARAM]->snapEnabled = true;
		configParam(FINE_PARAM, -100.f, 100.f, 0.f, "Fine tune", " cents");
		configParam(SPEED_PARAM, -2.f, 2.f, 0.f, "Speed", "x", 2.f);
		configSwitch(TIME_MODE_PARAM, 0.f, 3.f, 0.f, "Time", {"Repitch (speed changes the pitch)", "Stretch (Signalsmith default)", "Stretch (Signalsmith cheaper)", "Stretch, keep formants"});
		configParam(ATTACK_PARAM, 0.f, 1.f, 0.f, "Attack", " ms", 2000.f, 1.f, -1.f);
		configParam(RELEASE_PARAM, 0.f, 1.f, 0.4f, "Release", " ms", 2500.f, 2.f);
		configSwitch(FILTER_MODE_PARAM, 0.f, 4.f, 0.f, "Filter", {"Off", "Low-pass", "High-pass", "Band-pass", "Notch"});
		configSwitch(SLOPE_PARAM, 0.f, 3.f, 1.f, "Filter slope", {"12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct"});
		configParam<NoteQuantity>(CUTOFF_PARAM, 0.f, 135.f, 135.f, "Cutoff (note)");
		configParam(RESONANCE_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
		configParam(LEVEL_PARAM, 0.f, 1.f, 0.8f, "Level", "%", 0.f, 100.f);
		configSwitch(MODE_PARAM, 0.f, 1.f, 0.f, "Mode", {"Keys (one sample over a key range)", "Slices (one slice per key)"});
		configSwitch(PLAY_PARAM, 0.f, (float)NUM_PLAY_TYPES, 0.f, "Slice play type", {"As set per slice", "One shot", "Gate", "Loop", "Ping-pong", "Reverse", "Reverse loop"});
		configInput(VOCT_INPUT, "V/oct (polyphonic)");
		configInput(GATE_INPUT, "Gate (polyphonic)");
		configInput(VEL_INPUT, "Velocity (0-10 V, polyphonic)");
		configInput(BEND_INPUT, "Pitch bend (-5 to 5 V: the bend range)");
		configInput(START_INPUT, "Start offset (0-10 V: through the region, polyphonic)");
		configInput(SPEED_INPUT, "Speed CV (1 V doubles it, polyphonic)");
		configInput(CUTOFF_INPUT, "Cutoff CV (1V/oct, polyphonic)");
		configInput(RES_INPUT, "Resonance CV (0-10 V, polyphonic)");
		configInput(PITCH_INPUT, "Pitch (1V/oct, polyphonic): transposes every note");
		configInput(PLAY_INPUT, "Slice play type CV (1 V per step, added to PLAY, polyphonic)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		engine.prepare(48000.f);
		std::fill(speedX, speedX + MAX_VOICES, 1e9f);
		std::fill(speedOut, speedOut + MAX_VOICES, 1.f);
		slices = new Slices;
		for (auto &k : keyHeld)
			k = -1;
		worker = std::thread([this] { work(); });
	}

	~SOUNAIRE() {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			quit = true;
		}
		jobCv.notify_all();
		if (worker.joinable())
			worker.join();
		delete pending.exchange(nullptr);
		delete[] pendingPool.exchange(nullptr);
		delete[] retiredPool.exchange(nullptr);
		delete[] engine.pool;
		delete slices;
		delete pendingSlices.exchange(nullptr);
		delete retiredSlices.exchange(nullptr);
	}

	// ---------------------------------------------------------------- slices

	// a copy of uiSlices for the audio thread (UI or worker thread)
	void publishSlices() {
		Slices *n = new Slices;
		{
			std::lock_guard<std::mutex> lock(sliceMutex);
			*n = uiSlices;
		}
		delete retiredSlices.exchange(nullptr);
		delete pendingSlices.exchange(n);
	}

	Slices getSlices() {
		std::lock_guard<std::mutex> lock(sliceMutex);
		return uiSlices;
	}

	// new cut points; every slice gets `type`
	void setSlices(const std::vector<int64_t> &starts, int type) {
		{
			std::lock_guard<std::mutex> lock(sliceMutex);
			Slices &u = uiSlices;
			u.count = 0;
			for (int64_t p : starts) {
				if (u.count >= MAX_SLICES)
					break;
				if (u.count > 0 && p <= u.start[u.count - 1])
					continue;
				u.start[u.count] = u.count == 0 ? 0 : p;
				u.type[u.count] = (uint8_t)type;
				u.count++;
			}
			if (u.count == 0) {
				u.count = 1;
				u.start[0] = 0;
				u.type[0] = (uint8_t)type;
			}
		}
		selectedSlice = 0;
		publishSlices();
	}

	// a cut at frame f; the new slice plays like the one it was cut from
	void addSlice(int64_t f) {
		{
			std::lock_guard<std::mutex> lock(sliceMutex);
			Slices &u = uiSlices;
			if (u.count >= MAX_SLICES || f <= 0)
				return;
			int i = u.at(f);
			if (f - u.start[i] < 64 || (i + 1 < u.count && u.start[i + 1] - f < 64))
				return;
			for (int k = u.count; k > i + 1; k--) {
				u.start[k] = u.start[k - 1];
				u.type[k] = u.type[k - 1];
			}
			u.start[i + 1] = f;
			u.type[i + 1] = u.type[i];
			u.count++;
			selectedSlice = i + 1;
		}
		publishSlices();
	}

	// removes cut i (> 0): slice i joins the one before
	void removeSlice(int i) {
		{
			std::lock_guard<std::mutex> lock(sliceMutex);
			Slices &u = uiSlices;
			if (i <= 0 || i >= u.count)
				return;
			for (int k = i; k + 1 < u.count; k++) {
				u.start[k] = u.start[k + 1];
				u.type[k] = u.type[k + 1];
			}
			u.count--;
			selectedSlice = i - 1;
		}
		publishSlices();
	}

	// cut i (> 0) moved to f, between its neighbours
	void moveSlice(int i, int64_t f) {
		{
			std::lock_guard<std::mutex> lock(sliceMutex);
			Slices &u = uiSlices;
			if (i <= 0 || i >= u.count)
				return;
			int64_t lo = u.start[i - 1] + 64;
			int64_t hi = (i + 1 < u.count ? u.start[i + 1] : INT64_MAX) - 64;
			if (hi < lo)
				return;
			u.start[i] = std::max(lo, std::min(hi, f));
		}
		publishSlices();
	}

	void setSliceType(int i, int type) {
		{
			std::lock_guard<std::mutex> lock(sliceMutex);
			if (i < 0 || i >= uiSlices.count)
				return;
			uiSlices.type[i] = (uint8_t)type;
		}
		publishSlices();
	}

	void setAllSliceTypes(int type) {
		{
			std::lock_guard<std::mutex> lock(sliceMutex);
			for (int i = 0; i < uiSlices.count; i++)
				uiSlices.type[i] = (uint8_t)type;
		}
		publishSlices();
	}

	void divideSlices(int n) {
		std::shared_ptr<Sample> sm = displaySample();
		if (!sm)
			return;
		std::vector<int64_t> starts;
		for (int i = 0; i < n; i++)
			starts.push_back(sm->frames * i / n);
		setSlices(starts, getSlices().type[0]);
	}

	static float riseDb(int sens) {
		const float db[3] = {12.f, 8.f, 5.f};
		return db[clamp(sens, 0, 2)];
	}

	void requestDetect() {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			jobDetect = sensitivity;
		}
		jobCv.notify_all();
	}

	void requestPool(int preset, float rate) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			jobPoolPreset = preset;
			jobPoolRate = rate;
		}
		jobCv.notify_all();
	}

	// ---------------------------------------------------------------- worker

	void requestLoad(const std::string &path, bool keepSettings) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			jobPath = path;
			jobKeep = keepSettings;
		}
		loading = true;
		jobCv.notify_all();
	}

	void requestRootAnalysis(double from, double to) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			jobAnalyse = true;
			jobFrom = std::min(from, to);
			jobTo = std::max(from, to);
		}
		jobCv.notify_all();
	}

	void work() {
		while (true) {
			std::string path;
			bool keep = false, analyse = false;
			double from = 0, to = 1;
			int poolPreset = -1;
			float poolRate = 0.f;
			int detect = -1;
			{
				std::unique_lock<std::mutex> lock(jobMutex);
				jobCv.wait_for(lock, std::chrono::milliseconds(50), [&] { return quit || !jobPath.empty() || jobAnalyse || jobPoolPreset >= 0 || jobDetect >= 0; });
				if (quit)
					return;
				std::swap(path, jobPath);
				keep = jobKeep;
				analyse = jobAnalyse;
				jobAnalyse = false;
				from = jobFrom;
				to = jobTo;
				poolPreset = jobPoolPreset;
				poolRate = jobPoolRate;
				jobPoolPreset = -1;
				detect = jobDetect;
				jobDetect = -1;
			}
			delete[] retiredPool.exchange(nullptr);
			if (poolPreset >= 0 && !pendingPool.load()) {
				Stretcher *pool = sounaire::Engine::buildPool(poolRate, poolPreset);
				pendingPoolPreset = poolPreset;
				pendingPoolRate = poolRate;
				pendingPool = pool;
			}
			// free what the audio thread gave back
			if (const Sample *u = unused.exchange(nullptr)) {
				std::lock_guard<std::mutex> lock(sampleMutex);
				owned.erase(std::remove_if(owned.begin(), owned.end(), [&](const std::shared_ptr<Sample> &p) { return p.get() == u; }), owned.end());
			}
			if (!path.empty()) {
				std::string error;
				std::shared_ptr<Sample> s = loadSampleFile(path, error);
				if (s) {
					if (!keep) {
						float note = autoRoot ? detectRootNote(*s, 0, s->frames) : -1.f;
						if (note > 0)
							pendingRoot = note;
						else
							clearCents = true;   // the old sample's offset means nothing here
					}
					{
						std::lock_guard<std::mutex> lock(sampleMutex);
						owned.push_back(s);
						shown = s;
						sampleName = s->name;
					}
					// wait until the audio thread took the previous one
					while (pending.load() && !quit)
						std::this_thread::sleep_for(std::chrono::milliseconds(5));
					pending = s.get();
					if (!keep) {
						dirty = true;
						resetMarkers = true;
						// a new sample: cut at its transients, or one slice
						if (detectOnLoad)
							setSlices(detectTransients(*s, riseDb(sensitivity)), PLAY_ONE_SHOT);
						else
							setSlices({0}, PLAY_ONE_SHOT);
					}
					lastError.clear();
				}
				else {
					lastError = error;
				}
				loading = false;
			}
			if (detect >= 0) {
				std::shared_ptr<Sample> s = displaySample();
				if (s) {
					int type = getSlices().type[0];
					setSlices(detectTransients(*s, riseDb(detect)), type);
				}
			}
			if (analyse) {
				std::shared_ptr<Sample> s;
				{
					std::lock_guard<std::mutex> lock(sampleMutex);
					s = shown;
				}
				if (s) {
					float note = detectRootNote(*s, (int64_t)(from * s->frames), (int64_t)(to * s->frames));
					if (note > 0)
						pendingRoot = note;
				}
			}
		}
	}

	std::atomic<bool> resetMarkers {false};

	std::shared_ptr<Sample> displaySample() {
		std::lock_guard<std::mutex> lock(sampleMutex);
		return shown;
	}

	// ---------------------------------------------------------------- patch

	std::string storagePath() {
		return rack::system::join(getPatchStorageDirectory(), "sample.flac");
	}

	void onSave(const SaveEvent &e) override {
		std::shared_ptr<Sample> s = displaySample();
		if (!s)
			return;
		std::string path = storagePath();
		if (!dirty && rack::system::isFile(path))
			return;
		if (flacWriteFile(path, s->ch, s->channels, s->frames, (int)s->rate, s->bits))
			dirty = false;
	}

	void onAdd(const AddEvent &e) override {
		std::string path = rack::system::join(getPatchStorageDirectory(), "sample.flac");
		if (rack::system::isFile(path))
			requestLoad(path, true);
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		engine.prepare(e.sampleRate);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "autoNormalize", json_boolean(autoNormalize));
		json_object_set_new(rootJ, "autoRoot", json_boolean(autoRoot));
		json_object_set_new(rootJ, "polyOut", json_boolean(polyOut));
		json_object_set_new(rootJ, "bendRange", json_integer(bendRange));
		json_object_set_new(rootJ, "rootCents", json_real(rootCents));
		json_object_set_new(rootJ, "sampleName", json_string(sampleName.c_str()));
		json_object_set_new(rootJ, "detectOnLoad", json_boolean(detectOnLoad));
		json_object_set_new(rootJ, "sensitivity", json_integer(sensitivity));
		json_object_set_new(rootJ, "selectedSlice", json_integer(selectedSlice));
		Slices u = getSlices();
		json_t *slicesJ = json_array();
		for (int i = 0; i < u.count; i++) {
			json_t *sJ = json_array();
			json_array_append_new(sJ, json_integer(u.start[i]));
			json_array_append_new(sJ, json_integer(u.type[i]));
			json_array_append_new(slicesJ, sJ);
		}
		json_object_set_new(rootJ, "slices", slicesJ);
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		if (json_t *j = json_object_get(rootJ, "autoNormalize")) autoNormalize = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "autoRoot")) autoRoot = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "polyOut")) polyOut = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "bendRange")) bendRange = clamp((int)json_integer_value(j), 1, 48);
		if (json_t *j = json_object_get(rootJ, "rootCents")) rootCents = json_number_value(j);
		rootSeen = params[ROOT_PARAM].getValue();   // params come in before data: not a change by hand
		if (json_t *j = json_object_get(rootJ, "sampleName")) sampleName = json_string_value(j);
		if (json_t *j = json_object_get(rootJ, "detectOnLoad")) detectOnLoad = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "sensitivity")) sensitivity = clamp((int)json_integer_value(j), 0, 2);
		if (json_t *slicesJ = json_object_get(rootJ, "slices")) {
			std::lock_guard<std::mutex> lock(sliceMutex);
			Slices &u = uiSlices;
			u.count = 0;
			size_t i;
			json_t *sJ;
			json_array_foreach(slicesJ, i, sJ) {
				if (u.count >= MAX_SLICES)
					break;
				u.start[u.count] = u.count == 0 ? 0 : (int64_t)json_integer_value(json_array_get(sJ, 0));
				u.type[u.count] = (uint8_t)clamp((int)json_integer_value(json_array_get(sJ, 1)), 0, NUM_PLAY_TYPES - 1);
				u.count++;
			}
			if (u.count == 0)
				u.count = 1;
		}
		publishSlices();
		if (json_t *j = json_object_get(rootJ, "selectedSlice")) selectedSlice = clamp((int)json_integer_value(j), 0, MAX_SLICES - 1);
	}

	// ---------------------------------------------------------------- audio

	PlaySettings settings() {
		PlaySettings p;
		p.start = params[START_PARAM].getValue();
		p.end = params[END_PARAM].getValue();
		p.loopStart = params[LOOP_START_PARAM].getValue();
		p.loopEnd = params[LOOP_END_PARAM].getValue();
		p.loopMode = (int)params[LOOP_MODE_PARAM].getValue();
		p.xfadeMs = params[XFADE_PARAM].getValue();
		p.reverse = params[REVERSE_PARAM].getValue() > 0.5f;
		p.timeMode = (int)params[TIME_MODE_PARAM].getValue();
		// speed and resonance: the knobs here, each voice adds its own CV (voiceSpeed, voiceResonance)
		p.speed = clamp(std::pow(2.f, params[SPEED_PARAM].getValue()), 0.0625f, 8.f);
		// as the knobs show them: 0-2 s and 2 ms-5 s, exponential
		p.attackMs = std::pow(2000.f, params[ATTACK_PARAM].getValue()) - 1.f;
		p.releaseMs = 2.f * std::pow(2500.f, params[RELEASE_PARAM].getValue());
		p.filter.mode = (int)params[FILTER_MODE_PARAM].getValue();
		p.filter.stages = (int)params[SLOPE_PARAM].getValue() + 1;
		p.filter.resonance = params[RESONANCE_PARAM].getValue();
		const Sample *s = engine.current;
		float norm = (autoNormalize && s && s->peak > 1e-6f) ? 0.891f / s->peak : 1.f;
		p.gain = norm * params[LEVEL_PARAM].getValue() * 5.f;
		return p;
	}

	bool sliceMode() {
		return params[MODE_PARAM].getValue() > 0.5f;
	}

	// semitones from the root for channel c; in SLICES mode the key picks the
	// slice, so only PITCH, tune and bend move it
	float semitones(int c) {
		return semitonesFor(inputs[VOCT_INPUT].getPolyVoltage(c) * 12.f + 60.f, c);
	}

	// the same for a given note, with channel c's PITCH, bend and tune
	float semitonesFor(float note, int c) {
		float bend = clamp(inputs[BEND_INPUT].getPolyVoltage(c), -5.f, 5.f) / 5.f * bendRange;
		float shift = inputs[PITCH_INPUT].getPolyVoltage(c) * 12.f + params[TUNE_PARAM].getValue() + params[FINE_PARAM].getValue() * 0.01f + bend;
		if (sliceMode())
			return shift;
		return note - (params[ROOT_PARAM].getValue() + rootCents * 0.01f) + shift;
	}

	// the play type PLAY and its CV force, or -1: each slice's own
	int playOverride(int c) {
		int k = (int)std::round(params[PLAY_PARAM].getValue() + inputs[PLAY_INPUT].getPolyVoltage(c));
		return clamp(k, 0, (int)NUM_PLAY_TYPES) - 1;
	}

	// SLICES mode: the key from BASE (the ROOT knob) picks the slice; START and
	// END trim inside it
	// slot: the engine voice; c: the channel whose CVs it follows
	void sliceNoteOn(int slot, int c, int note, const PlaySettings &p, double startOffset, float vel) {
		const Sample *smp = engine.current;
		if (!smp || !slices || smp->frames < 4)
			return;
		int i = note - (int)params[ROOT_PARAM].getValue();
		if (i < 0 || i >= slices->count)
			return;
		int64_t F = smp->frames;
		int64_t a = clamp(slices->start[i], (int64_t)0, F - 2);
		int64_t b = clamp(slices->end(i, F), a + 2, F);
		double t0 = params[START_PARAM].getValue(), t1 = params[END_PARAM].getValue();
		double sa = a + std::min(t0, t1) * (b - a), sb = a + std::max(t0, t1) * (b - a);
		int type = playOverride(c);
		if (type < 0)
			type = slices->type[i];
		PlaySettings own = p;
		own.start = own.loopStart = sa / F;
		own.end = own.loopEnd = sb / F;
		own.loopMode = (type == PLAY_LOOP || type == PLAY_REVERSE_LOOP) ? LOOP_FORWARD : (type == PLAY_PINGPONG ? LOOP_PINGPONG : LOOP_OFF);
		own.reverse = type == PLAY_REVERSE || type == PLAY_REVERSE_LOOP;
		bool oneShot = type == PLAY_ONE_SHOT || type == PLAY_REVERSE;
		engine.noteOn(slot, slot == sounaire::Engine::MOUSE ? 0 : c, semitones(c), vel, p, startOffset, cutoffNote(c), &own, oneShot);
		lastSlice = i;
	}

	// LOW above HIGH plays the same range the keyboard strip shows
	bool inRange(int c) {
		return noteInRange((int)std::round(inputs[VOCT_INPUT].getPolyVoltage(c) * 12.f + 60.f));
	}

	bool noteInRange(int note) {
		int lo = (int)params[LOW_PARAM].getValue(), hi = (int)params[HIGH_PARAM].getValue();
		return note >= std::min(lo, hi) && note <= std::max(lo, hi);
	}

	float cutoffNote(int c) {
		return params[CUTOFF_PARAM].getValue() + inputs[CUTOFF_INPUT].getPolyVoltage(c) * 12.f;
	}

	float voiceSpeed(int c) {
		float x = params[SPEED_PARAM].getValue() + inputs[SPEED_INPUT].getPolyVoltage(c);
		if (x != speedX[c]) {
			speedX[c] = x;
			speedOut[c] = clamp(std::pow(2.f, x), 0.0625f, 8.f);
		}
		return speedOut[c];
	}

	float voiceResonance(int c) {
		return clamp(params[RESONANCE_PARAM].getValue() + inputs[RES_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f);
	}

	// what follows the channel while the voice plays
	void track(Voice &v, int c) {
		v.pitchSemis = semitones(c);
		v.cutoffNote = cutoffNote(c);
		v.speed = voiceSpeed(c);
		v.resonance = voiceResonance(c);
	}

	float velocity(int c) {
		return inputs[VEL_INPUT].isConnected() ? clamp(inputs[VEL_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f) : 1.f;
	}

	void process(const ProcessArgs &args) override {
		// a new sample (hot-swap: playing notes keep the old one)
		if (Sample *s = pending.exchange(nullptr))
			engine.setSample(s);
		if (!unused.load()) {
			if (const Sample *u = engine.takeUnused())
				unused = u;
		}
		if (resetMarkers.exchange(false)) {
			params[START_PARAM].setValue(0.f);
			params[END_PARAM].setValue(1.f);
			params[LOOP_START_PARAM].setValue(0.f);
			params[LOOP_END_PARAM].setValue(1.f);
		}
		// ROOT turned by hand, or a new sample without a detected root: no offset
		if (clearCents.exchange(false) || params[ROOT_PARAM].getValue() != rootSeen) {
			rootSeen = params[ROOT_PARAM].getValue();
			rootCents = 0.f;
		}
		float root = pendingRoot.exchange(-1000.f);
		if (root > -999.f) {
			float n = std::round(root);
			params[ROOT_PARAM].setValue(clamp(n, 0.f, 127.f));
			rootSeen = params[ROOT_PARAM].getValue();
			rootCents = (root - n) * 100.f;
		}

		// slices edited in the display or found by the worker
		if (!retiredSlices.load()) {
			if (Slices *n = pendingSlices.exchange(nullptr)) {
				retiredSlices = slices;
				slices = n;
			}
		}

		PlaySettings p = settings();
		// stretchers for the time mode, built off the audio thread
		if (Stretcher *pool = pendingPool.exchange(nullptr)) {
			Stretcher *old = engine.setPool(pool, pendingPoolPreset, pendingPoolRate);
			if (old && !retiredPool.load())
				retiredPool = old;
			else
				delete[] old;  // never happens: the worker frees each one before building the next
			poolRequested = false;
		}
		if (p.timeMode != TIME_REPITCH && !engine.poolReady(p.timeMode) && !poolRequested) {
			requestPool(sounaire::Engine::presetFor(p.timeMode), args.sampleRate);
			poolRequested = true;
		}
		bool slicing = sliceMode();
		bool legato = !slicing && params[LEGATO_PARAM].getValue() > 0.5f;
		lights[REVERSE_LIGHT].setBrightness(p.reverse ? 1.f : 0.f);
		lights[LEGATO_LIGHT].setBrightness(legato ? 1.f : 0.f);
		int channels = std::max(1, std::max(inputs[GATE_INPUT].getChannels(), inputs[VOCT_INPUT].getChannels()));
		auto startOffset = [&](int c) { return (double)clamp(inputs[START_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f); };

		if (legato) {
			// one voice: the latest note sets the pitch; a new note while another
			// is held moves the pitch there at once, without starting the sample again
			bool anyHeld = false;
			for (int c = 0; c < channels; c++) {
				bool was = held[c];
				held[c] = inputs[GATE_INPUT].getPolyVoltage(c) >= 1.f;
				if (held[c] && !was && inRange(c)) {
					bool other = false;
					for (int o = 0; o < channels; o++)
						other |= o != c && held[o];
					legatoChannel = c;
					if (!(other && engine.voices[0].active && engine.voices[0].gate))
						engine.noteOn(0, 0, semitones(c), velocity(c), p, startOffset(c), cutoffNote(c));
				}
				anyHeld |= held[c];
			}
			if (!anyHeld && engine.voices[0].gate)
				engine.noteOff(0);
			if (!held[legatoChannel]) {
				for (int c = channels - 1; c >= 0; c--)
					if (held[c]) {
						legatoChannel = c;
						break;
					}
			}
			Voice &v = engine.voices[0];
			if (v.active && !v.fadingOut)
				track(v, legatoChannel);
			for (int c = 1; c < MAX_VOICES; c++)
				if (engine.voices[c].gate)
					engine.noteOff(c);
		}
		else {
			for (int c = 0; c < MAX_VOICES; c++) {
				bool g = c < channels && inputs[GATE_INPUT].getPolyVoltage(c) >= 1.f;
				if (g && !held[c] && slicing)
					sliceNoteOn(c, c, (int)std::round(inputs[VOCT_INPUT].getPolyVoltage(c) * 12.f + 60.f), p, startOffset(c), velocity(c));
				else if (g && !held[c] && inRange(c))
					engine.noteOn(c, c, semitones(c), velocity(c), p, startOffset(c), cutoffNote(c));
				else if (!g && held[c])
					engine.noteOff(c);
				held[c] = g;
				Voice &v = engine.voices[c];
				if (v.active && !v.fadingOut && c < channels)
					track(v, c);
			}
		}

		// a key clicked on the keyboard strip, at full velocity
		int mk = mouseKey.exchange(-1);
		if (mk >= 0) {
			if (slicing)
				sliceNoteOn(sounaire::Engine::MOUSE, 0, mk, p, 0.0, 1.f);
			else if (noteInRange(mk))
				engine.noteOn(sounaire::Engine::MOUSE, 0, semitonesFor((float)mk, 0), 1.f, p, 0.0, cutoffNote(0));
			mouseOn = true;
			mouseShown = mk;
		}
		if (mouseOn && !mouseHeld) {
			engine.noteOff(sounaire::Engine::MOUSE);
			mouseOn = false;
			mouseShown = -1;
		}

		// preview from the display
		float pp = previewPos.exchange(-1.f);
		if (pp >= 0.f) {
			engine.previewOn(pp, p);
			previewing = true;
		}
		if (previewing && !previewHeld) {
			engine.previewOff();
			previewing = false;
		}

		// the keys held now, for the keyboard strip
		for (int c = 0; c < MAX_VOICES; c++)
			keyHeld[c] = held[c] ? (int)std::round(inputs[VOCT_INPUT].getPolyVoltage(c) * 12.f + 60.f) : -1;

		std::fill(outL, outL + MAX_VOICES, 0.f);
		std::fill(outR, outR + MAX_VOICES, 0.f);
		engine.process(p, outL, outR);

		int outChannels = (polyOut && !legato) ? channels : 1;
		if (outChannels == 1) {
			float l = 0.f, r = 0.f;
			for (int c = 0; c < MAX_VOICES; c++) {
				l += outL[c];
				r += outR[c];
			}
			outputs[L_OUTPUT].setVoltage(bidooSoftLimit(l), 0);
			outputs[R_OUTPUT].setVoltage(bidooSoftLimit(r), 0);
		}
		else {
			for (int c = 0; c < outChannels; c++) {
				outputs[L_OUTPUT].setVoltage(bidooSoftLimit(outL[c]), c);
				outputs[R_OUTPUT].setVoltage(bidooSoftLimit(outR[c]), c);
			}
		}
		outputs[L_OUTPUT].setChannels(outChannels);
		outputs[R_OUTPUT].setChannels(outChannels);
	}
};

#include "SounaireWidget.hpp"

Model *modelSOUNAIRE = createModel<SOUNAIRE, SOUNAIREWidget>("SOUNAIRE");
