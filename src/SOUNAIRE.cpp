// sOUNAiRe: a sampler. One sample over a key range, 16 voices (poly V/oct and
// gate), loops with crossfades, reverse, legato, multimode filter, Signalsmith
// time-stretch, and the sample kept in the patch as FLAC.
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
	std::string sampleName;

	// ---- audio thread state
	dsp::SchmittTrigger gates[MAX_VOICES];
	bool held[MAX_VOICES] = {};
	int legatoChannel = 0;
	float outL[MAX_VOICES] = {}, outR[MAX_VOICES] = {};
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
		configInput(VOCT_INPUT, "V/oct (polyphonic)");
		configInput(GATE_INPUT, "Gate (polyphonic)");
		configInput(VEL_INPUT, "Velocity (0-10 V, polyphonic)");
		configInput(BEND_INPUT, "Pitch bend (-5 to 5 V: the bend range)");
		configInput(START_INPUT, "Start offset (0-10 V: through the region, polyphonic)");
		configInput(SPEED_INPUT, "Speed CV (1 V doubles it)");
		configInput(CUTOFF_INPUT, "Cutoff CV (1V/oct, polyphonic)");
		configInput(RES_INPUT, "Resonance CV (0-10 V)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		engine.prepare(48000.f);
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
			{
				std::unique_lock<std::mutex> lock(jobMutex);
				jobCv.wait_for(lock, std::chrono::milliseconds(50), [&] { return quit || !jobPath.empty() || jobAnalyse || jobPoolPreset >= 0; });
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
					if (autoRoot && !keep) {
						float note = detectRootNote(*s, 0, s->frames);
						if (note > 0)
							pendingRoot = note;
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
					}
					lastError.clear();
				}
				else {
					lastError = error;
				}
				loading = false;
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
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		if (json_t *j = json_object_get(rootJ, "autoNormalize")) autoNormalize = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "autoRoot")) autoRoot = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "polyOut")) polyOut = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "bendRange")) bendRange = clamp((int)json_integer_value(j), 1, 48);
		if (json_t *j = json_object_get(rootJ, "rootCents")) rootCents = json_number_value(j);
		if (json_t *j = json_object_get(rootJ, "sampleName")) sampleName = json_string_value(j);
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
		p.speed = clamp(std::pow(2.f, params[SPEED_PARAM].getValue() + inputs[SPEED_INPUT].getVoltage()), 0.0625f, 8.f);
		// as the knobs show them: 0-2 s and 2 ms-5 s, exponential
		p.attackMs = std::pow(2000.f, params[ATTACK_PARAM].getValue()) - 1.f;
		p.releaseMs = 2.f * std::pow(2500.f, params[RELEASE_PARAM].getValue());
		p.filter.mode = (int)params[FILTER_MODE_PARAM].getValue();
		p.filter.stages = (int)params[SLOPE_PARAM].getValue() + 1;
		p.filter.resonance = clamp(params[RESONANCE_PARAM].getValue() + inputs[RES_INPUT].getVoltage() * 0.1f, 0.f, 1.f);
		const Sample *s = engine.current;
		float norm = (autoNormalize && s && s->peak > 1e-6f) ? 0.891f / s->peak : 1.f;
		p.gain = norm * params[LEVEL_PARAM].getValue() * 5.f;
		return p;
	}

	// semitones from the root for channel c
	float semitones(int c) {
		float note = inputs[VOCT_INPUT].getPolyVoltage(c) * 12.f + 60.f;
		float bend = clamp(inputs[BEND_INPUT].getPolyVoltage(c), -5.f, 5.f) / 5.f * bendRange;
		return note - (params[ROOT_PARAM].getValue() + rootCents * 0.01f) + params[TUNE_PARAM].getValue() + params[FINE_PARAM].getValue() * 0.01f + bend;
	}

	bool inRange(int c) {
		int note = (int)std::round(inputs[VOCT_INPUT].getPolyVoltage(c) * 12.f + 60.f);
		return note >= (int)params[LOW_PARAM].getValue() && note <= (int)params[HIGH_PARAM].getValue();
	}

	float cutoffNote(int c) {
		return params[CUTOFF_PARAM].getValue() + inputs[CUTOFF_INPUT].getPolyVoltage(c) * 12.f;
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
		float root = pendingRoot.exchange(-1000.f);
		if (root > -999.f) {
			float n = std::round(root);
			params[ROOT_PARAM].setValue(clamp(n, 0.f, 127.f));
			rootCents = (root - n) * 100.f;
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
		bool legato = params[LEGATO_PARAM].getValue() > 0.5f;
		lights[REVERSE_LIGHT].setBrightness(p.reverse ? 1.f : 0.f);
		lights[LEGATO_LIGHT].setBrightness(legato ? 1.f : 0.f);
		int channels = std::max(1, std::max(inputs[GATE_INPUT].getChannels(), inputs[VOCT_INPUT].getChannels()));
		auto startOffset = [&](int c) { return (double)clamp(inputs[START_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f); };

		if (legato) {
			// one voice: the latest note sets the pitch; a new note while another
			// is held glides there without starting the sample again
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
			if (v.active && !v.fadingOut) {
				v.pitchSemis = semitones(legatoChannel);
				v.cutoffNote = cutoffNote(legatoChannel);
			}
			for (int c = 1; c < MAX_VOICES; c++)
				if (engine.voices[c].gate)
					engine.noteOff(c);
		}
		else {
			for (int c = 0; c < MAX_VOICES; c++) {
				bool g = c < channels && inputs[GATE_INPUT].getPolyVoltage(c) >= 1.f;
				if (g && !held[c] && inRange(c))
					engine.noteOn(c, c, semitones(c), velocity(c), p, startOffset(c), cutoffNote(c));
				else if (!g && held[c])
					engine.noteOff(c);
				held[c] = g;
				Voice &v = engine.voices[c];
				if (v.active && !v.fadingOut && c < channels) {
					v.pitchSemis = semitones(c);
					v.cutoffNote = cutoffNote(c);
				}
			}
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
