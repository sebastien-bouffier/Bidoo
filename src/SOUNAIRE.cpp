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
#include <cstring>
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
		REC_PARAM,
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
		TRIG_INPUT,
		SLICE_INPUT,
		RESET_INPUT,
		CLOCK_INPUT,
		INL_INPUT,
		INR_INPUT,
		REC_INPUT,
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
	// `shown` is the sample as loaded (display, patch storage, slices, export);
	// the engine plays a copy at its own rate (the same object when the rates
	// match). The worker keeps a reference for each copy it hands over in
	// `owned`; the audio thread plays raw pointers and hands back each one no
	// voice uses any more, and the worker drops that reference.
	std::mutex sampleMutex;
	std::vector<std::shared_ptr<Sample>> owned;
	std::shared_ptr<Sample> shown;
	std::atomic<Sample *> pending {nullptr};
	std::atomic<const Sample *> unused {nullptr};
	std::atomic<bool> dirty {false};      // the sample in the patch storage is out of date
	std::atomic<float> pendingRoot {-1000.f};
	std::atomic<bool> clearCents {false};   // a new sample came without a detected root
	std::atomic<bool> loading {false};
	std::atomic<float> engineRate {48000.f};   // the rate the worker makes playing copies for
	float playRate = 0.f;                     // worker: the rate of the copy it handed over last
	const Sample *handed = nullptr;           // worker: that copy
	// texts the worker, the UI and the autosave share: under infoMutex
	std::mutex infoMutex;
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
	bool waveBlue = false; // the waveform's colour on the display: orange, or blue
	bool autoRoot = true;
	bool polyOut = true;
	int bendRange = 2;
	float rootCents = 0.f;         // the detected root's offset from the note
	float rootSeen = 60.f;         // ROOT as last seen: a change by hand clears rootCents
	bool xfadeEqualPower = true;   // loop crossfade law (off: linear)
	std::string sampleName;        // under infoMutex

	// ---- slices (SLICES mode)
	// the UI and the worker edit `uiSlices` under sliceMutex and publish a copy;
	// the audio thread swaps the copy in and hands the old one back
	std::mutex sliceMutex;
	Slices uiSlices;
	std::atomic<int> selectedSlice {0};
	std::atomic<Slices *> pendingSlices {nullptr};
	std::atomic<Slices *> retiredSlices {nullptr};
	std::atomic<uint32_t> sliceVersion {0};   // counts the edits: the display copies the slices only when it moves
	Slices *slices = nullptr;      // audio thread
	bool detectOnLoad = true;      // saved
	float sensitivity = 0.64f;     // 0..1 (about 8 dB of rise); saved
	int detector = 0;              // 0: drums (attacks), 1: tonal (spectral change); saved

	// ---- clock sync (CLOCK input): the sample's loop follows the tempo
	int clockPpqn = 1;             // pulses per beat; saved
	int loopBeats = 0;             // the loop's length in beats, 0: guessed; saved
	dsp::SchmittTrigger clockIn;
	int64_t clockCount = 0;        // samples since the last pulse
	double clockInterval = 0;      // samples between pulses, 0: no clock yet
	std::atomic<float> syncBpm {0.f};      // for the display, 0: not synced
	std::atomic<int> syncBeats {0};
	// worked out at each pulse, or when the sample or the beats change
	bool syncOn = false;
	double syncSeconds = -1.0;
	int syncLoopBeats = -1, syncBeatsNow = 4;
	float syncBpmNow = 0.f;

	// ---- recording (IN L, IN R, REC): into chunks the worker keeps ready
	struct RecChunk {
		static constexpr int FRAMES = 16384;
		float f[2][FRAMES];
		int used = 0;
	};
	static constexpr int REC_SPARES = 16;
	static constexpr int REC_MAX_CHUNKS = 4096;   // 5 minutes at 192 kHz
	std::atomic<RecChunk *> recSpare[REC_SPARES];
	RecChunk *recChunks[REC_MAX_CHUNKS] = {};
	int recCount = 0;
	int64_t recFrames = 0, recLimit = 0;
	bool recording = false;
	bool recStereo = false;
	float recStartRate = 48000.f;           // the rate the recording runs at
	std::atomic<int> recDone {-1};          // chunks of a finished recording, for the worker
	std::atomic<float> recRate {48000.f};
	std::atomic<bool> recStereoDone {false};
	std::atomic<float> recSeconds {0.f};    // for the display

	// ---- the patch storage folder, known once added (the worker cannot ask Rack)
	std::mutex storageMutex;
	std::string storageDir;
	std::string jobExportDir, jobExportFile;
	std::atomic<int> lastSlice {-1};   // the slice played last, for the display
	std::atomic<int> keyHeld[MAX_VOICES];  // the key each held gate plays, -1: none (for the keyboard strip)
	// a key clicked on the keyboard strip: plays while the mouse is held
	std::atomic<int> mouseKey {-1};         // a new key to start
	std::atomic<bool> mouseHeld {false};
	std::atomic<int> mouseShown {-1};       // the key it plays, for the strip
	bool mouseOn = false;
	// TRIG, SLICE and RESET: slices played in turn or picked by CV
	dsp::SchmittTrigger trigIn, resetIn;
	int seqSlice = -1;           // the slice TRIG played last
	bool trigOn = false;

	// ---- audio thread state
	float syncFactor = 1.f;        // the clock's speed factor for the loop
	bool held[MAX_VOICES] = {};
	int legatoChannel = 0;
	float outL[MAX_VOICES] = {}, outR[MAX_VOICES] = {};
	// per channel speed: exp2 worked out again only when knob + CV move
	float speedX[MAX_VOICES], speedOut[MAX_VOICES];
	// preview (Alt+click on the display)
	std::atomic<float> previewPos {-1.f};
	std::atomic<bool> previewHeld {false};
	bool previewing = false;
	// what the UI reads (keys held, recording time, tempo, lights), and the
	// hand-backs to the worker: every 64 frames
	dsp::ClockDivider uiDivider;
	// settings(): the exponentials again only when their knob moves
	float speedKnob = NAN, speedPow = 1.f, attackKnob = NAN, attackMsNow = 0.f, releaseKnob = NAN, releaseMsNow = 0.f;

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
		configSwitch(REC_PARAM, 0.f, 1.f, 0.f, "Record (IN L / IN R; replaces the sample)", {"Off", "Recording"});
		getParamQuantity(REC_PARAM)->randomizeEnabled = false;
		getParamQuantity(MODE_PARAM)->randomizeEnabled = false;
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
		configInput(TRIG_INPUT, "Slice trigger (SLICES mode): plays the slice SLICE picks, or the next one; held like a gate");
		configInput(SLICE_INPUT, "Slice select (SLICES mode, 0-10 V across the slices, read at each TRIG)");
		configInput(RESET_INPUT, "Reset (SLICES mode): the next TRIG plays slice 1");
		configInput(CLOCK_INPUT, "Clock: the sample loops in time with it (1 pulse per beat, see menu)");
		configInput(INL_INPUT, "Record left (5 V = full scale)");
		configInput(INR_INPUT, "Record right (left when unpatched: a mono recording)");
		configInput(REC_INPUT, "Record gate (records while high)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		engine.prepare(48000.f);
		uiDivider.setDivision(64);
		// the sinc tables, built here rather than on the first pitched-up note
		sounaire::playKernel();
		sounaire::longKernel();
		std::fill(speedX, speedX + MAX_VOICES, 1e9f);
		std::fill(speedOut, speedOut + MAX_VOICES, 1.f);
		slices = new Slices;
		for (auto &k : keyHeld)
			k = -1;
		for (auto &c : recSpare)
			c = nullptr;
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
		delete[] engine.oldPool;
		delete slices;
		delete pendingSlices.exchange(nullptr);
		delete retiredSlices.exchange(nullptr);
		for (auto &c : recSpare)
			delete c.exchange(nullptr);
		for (RecChunk *&c : recChunks)
			delete c;
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
		sliceVersion.fetch_add(1);
	}

	Slices getSlices() {
		std::lock_guard<std::mutex> lock(sliceMutex);
		return uiSlices;
	}

	// ---------------------------------------------------------------- texts

	std::string getSampleName() {
		std::lock_guard<std::mutex> lock(infoMutex);
		return sampleName;
	}
	void setSampleName(const std::string &n) {
		std::lock_guard<std::mutex> lock(infoMutex);
		sampleName = n;
	}
	std::string getLastError() {
		std::lock_guard<std::mutex> lock(infoMutex);
		return lastError;
	}
	void setLastError(const std::string &e) {
		std::lock_guard<std::mutex> lock(infoMutex);
		lastError = e;
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
				u.resetSettings(u.count, type);
				u.count++;
			}
			if (u.count == 0) {
				u.count = 1;
				u.start[0] = 0;
				u.resetSettings(0, type);
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
				u.copySettings(k - 1, k);
			}
			u.start[i + 1] = f;
			u.copySettings(i, i + 1);
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
				u.copySettings(k + 1, k);
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

	void setSliceChoke(int i, int group) {
		{
			std::lock_guard<std::mutex> lock(sliceMutex);
			if (i < 0 || i >= uiSlices.count)
				return;
			uiSlices.choke[i] = (uint8_t)clamp(group, 0, 4);
		}
		publishSlices();
	}

	// a slice's level (dB), tune (semitones), pan, attack or release (ms, < 0: the knobs)
	enum SliceSetting { SET_LEVEL, SET_TUNE, SET_PAN, SET_ATTACK, SET_RELEASE };
	void setSliceSetting(int i, int which, float v) {
		{
			std::lock_guard<std::mutex> lock(sliceMutex);
			if (i < 0 || i >= uiSlices.count)
				return;
			Slices &u = uiSlices;
			switch (which) {
				case SET_LEVEL: u.level[i] = clamp(v, -36.f, 12.f); break;
				case SET_TUNE: u.tune[i] = clamp(v, -24.f, 24.f); break;
				case SET_PAN: u.pan[i] = clamp(v, -1.f, 1.f); break;
				case SET_ATTACK: u.attack[i] = v < 0.f ? -1.f : std::min(v, 2000.f); break;
				default: u.release[i] = v < 0.f ? -1.f : std::min(v, 5000.f); break;
			}
		}
		publishSlices();
	}

	float sliceSetting(int i, int which) {
		std::lock_guard<std::mutex> lock(sliceMutex);
		i = clamp(i, 0, uiSlices.count - 1);
		const float *t[5] = {uiSlices.level, uiSlices.tune, uiSlices.pan, uiSlices.attack, uiSlices.release};
		return t[clamp(which, 0, 4)][i];
	}

	// the whole slice state back (undo / redo)
	void restoreSlices(const Slices &u) {
		{
			std::lock_guard<std::mutex> lock(sliceMutex);
			uiSlices = u;
		}
		selectedSlice = clamp((int)selectedSlice, 0, u.count - 1);
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

	// the cuts again, at the sensitivity, or about n slices (UI thread: quick
	// enough for loops, and an undo step can be made of it)
	void detectNow(int about = 0) {
		std::shared_ptr<Sample> s = displaySample();
		if (!s)
			return;
		std::vector<Onset> onsets = findOnsets(*s, detector == 1);
		std::vector<int64_t> cuts = about > 0 ? pickAbout(onsets, about, s->rate) : pickOnsets(onsets, riseForSensitivity(sensitivity), s->rate);
		setSlices(cuts, getSlices().type[0]);
	}

	void requestExport(const std::string &dir, const std::string &file) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			jobExportDir = dir;
			jobExportFile = file;
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
			std::string exportDir, exportFile;
			{
				std::unique_lock<std::mutex> lock(jobMutex);
				jobCv.wait_for(lock, std::chrono::milliseconds(20), [&] { return quit || !jobPath.empty() || jobAnalyse || jobPoolPreset >= 0 || jobDetect >= 0 || !jobExportDir.empty() || !jobExportFile.empty() || (shown && playRate != engineRate.load()); });
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
				std::swap(exportDir, jobExportDir);
				std::swap(exportFile, jobExportFile);
			}
			delete[] retiredPool.exchange(nullptr);
			if (poolPreset >= 0 && !pendingPool.load()) {
				Stretcher *pool = sounaire::Engine::buildPool(poolRate, poolPreset);
				pendingPoolPreset = poolPreset;
				pendingPoolRate = poolRate;
				pendingPool = pool;
			}
			// drop the reference of what the audio thread gave back
			if (const Sample *u = unused.exchange(nullptr))
				dropOwned(u);
			// recording: spare chunks ready, touched here so the audio thread never
			// takes a page fault on them
			for (auto &c : recSpare) {
				if (!c.load()) {
					RecChunk *k = new RecChunk;
					std::memset(k->f, 0, sizeof(k->f));
					c.store(k, std::memory_order_release);
				}
			}
			int done = recDone.load();
			if (done >= 0) {
				std::shared_ptr<Sample> s = std::make_shared<Sample>();
				s->channels = recStereoDone ? 2 : 1;
				s->rate = recRate;
				s->bits = 24;
				s->name = "Recording";
				int64_t n = 0;
				for (int c = 0; c < done; c++)
					n += recChunks[c]->used;
				for (int ch = 0; ch < s->channels; ch++)
					s->ch[ch].reserve((size_t)n);
				for (int c = 0; c < done; c++) {
					for (int ch = 0; ch < s->channels; ch++)
						s->ch[ch].insert(s->ch[ch].end(), recChunks[c]->f[ch], recChunks[c]->f[ch] + recChunks[c]->used);
					delete recChunks[c];
					recChunks[c] = nullptr;
				}
				s->frames = n;
				if (n > 16) {
					s->analyse();
					installSample(s, false);
				}
				// only now may the audio thread record again
				recDone = -1;
			}
			if (!path.empty()) {
				std::string error;
				std::shared_ptr<Sample> s = loadSampleFile(path, error);
				if (s) {
					installSample(s, keep);
					setLastError("");
				}
				else {
					setLastError(error);
				}
				loading = false;
			}
			// the engine's rate changed: a copy of the sample at the new one
			float rate = engineRate.load();
			if (playRate != rate) {
				std::shared_ptr<Sample> s = displaySample();
				if (s) {
					std::shared_ptr<Sample> play = sounaire::playableAt(s, rate);
					playRate = rate;
					handOver(play);
				}
			}
			if (!exportDir.empty() || !exportFile.empty())
				exportNow(exportDir, exportFile);
			if (detect >= 0) {
				std::shared_ptr<Sample> s = displaySample();
				if (s) {
					int type = getSlices().type[0];
					setSlices(pickOnsets(findOnsets(*s, detector == 1), riseForSensitivity(sensitivity), s->rate), type);
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

	// a loaded or recorded sample to play (worker thread); `keep`: from the
	// patch, so its root, markers and slices stay as saved
	void installSample(std::shared_ptr<Sample> s, bool keep) {
		if (!keep) {
			float note = autoRoot ? detectRootNote(*s, 0, s->frames) : -1.f;
			if (note > 0)
				pendingRoot = note;
			else
				clearCents = true;   // the old sample's offset means nothing here
		}
		// the copy the engine plays, at its rate, with its octave copies
		float rate = engineRate.load();
		std::shared_ptr<Sample> play = sounaire::playableAt(s, rate);
		playRate = rate;
		{
			std::lock_guard<std::mutex> lock(sampleMutex);
			shown = s;
		}
		setSampleName(s->name);
		handOver(play);
		if (!keep) {
			dirty = true;
			resetMarkers = true;
			// a new sample: cut at its transients, or one slice
			if (detectOnLoad)
				setSlices(pickOnsets(findOnsets(*s, detector == 1), riseForSensitivity(sensitivity), s->rate), PLAY_ONE_SHOT);
			else
				setSlices({0}, PLAY_ONE_SHOT);
			// kept in the patch storage now, so saving the patch has nothing to wait for
			std::string dir;
			{
				std::lock_guard<std::mutex> lock(storageMutex);
				dir = storageDir;
			}
			if (!dir.empty() && flacWriteFile(rack::system::join(dir, "sample.flac"), s->ch, s->channels, s->frames, (int)s->rate, s->bits))
				dirty = false;
		}
	}

	// a playing copy for the audio thread, never waiting for it (it does not run
	// while the module is bypassed): one it has not taken yet is replaced
	void handOver(const std::shared_ptr<Sample> &play) {
		if (play.get() == handed)
			return;   // already there (a rate change back before anything else)
		{
			std::lock_guard<std::mutex> lock(sampleMutex);
			owned.push_back(play);
		}
		handed = play.get();
		if (Sample *prev = pending.exchange(play.get()))
			dropOwned(prev);
	}

	// one reference fewer to a copy the audio thread no longer has
	void dropOwned(const Sample *u) {
		std::shared_ptr<Sample> last;   // freed outside the lock
		std::lock_guard<std::mutex> lock(sampleMutex);
		auto it = std::find_if(owned.begin(), owned.end(), [&](const std::shared_ptr<Sample> &p) { return p.get() == u; });
		if (it != owned.end()) {
			last = *it;
			owned.erase(it);
		}
	}

	// every slice as its own WAV in `dir`, or the sample as one WAV (worker thread)
	void exportNow(const std::string &dir, const std::string &file) {
		std::shared_ptr<Sample> s = displaySample();
		if (!s)
			return;
		auto write = [&](const std::string &path, int64_t a, int64_t b) {
			drwav_data_format format;
			format.container = drwav_container_riff;
			format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
			format.channels = s->channels;
			format.sampleRate = (drwav_uint32)s->rate;
			format.bitsPerSample = 32;
			drwav wav;
			if (!drwav_init_file_write(&wav, path.c_str(), &format, nullptr))
				return;
			std::vector<float> buf((size_t)(b - a) * s->channels);
			for (int64_t i = a; i < b; i++)
				for (int c = 0; c < s->channels; c++)
					buf[(size_t)(i - a) * s->channels + c] = s->ch[c][(size_t)i];
			drwav_write_pcm_frames(&wav, (drwav_uint64)(b - a), buf.data());
			drwav_uninit(&wav);
		};
		if (!file.empty())
			write(file, 0, s->frames);
		if (!dir.empty()) {
			Slices u = getSlices();
			std::string base = rack::system::getStem(s->name);
			for (int i = 0; i < u.count; i++) {
				int64_t a = std::min(u.start[i], s->frames), b = std::min(u.end(i, s->frames), s->frames);
				if (b > a)
					write(rack::system::join(dir, rack::string::f("%s_%02d.wav", base.c_str(), i + 1)), a, b);
			}
		}
	}

	std::shared_ptr<Sample> displaySample() {
		std::lock_guard<std::mutex> lock(sampleMutex);
		return shown;
	}

	// ---------------------------------------------------------------- patch

	// the folder is made on demand: Rack does not create it, and writing the
	// sample into a folder that does not exist fails (the sample is then lost
	// between sessions)
	std::string storagePath() {
		return rack::system::join(createPatchStorageDirectory(), "sample.flac");
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
		{
			std::lock_guard<std::mutex> lock(storageMutex);
			storageDir = createPatchStorageDirectory();
		}
		std::string path = rack::system::join(getPatchStorageDirectory(), "sample.flac");
		if (rack::system::isFile(path))
			requestLoad(path, true);
	}

	// (the engine is stopped meanwhile) what plays fades out; a recording ends
	// at the rate it was made at; the worker makes a copy of the sample at the
	// new rate, and a pool of stretchers when one is needed
	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		if (recording)
			finishRecording();
		engine.prepare(e.sampleRate);
		engineRate = e.sampleRate;
		jobCv.notify_all();
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "autoNormalize", json_boolean(autoNormalize));
		json_object_set_new(rootJ, "waveBlue", json_boolean(waveBlue));
		json_object_set_new(rootJ, "autoRoot", json_boolean(autoRoot));
		json_object_set_new(rootJ, "polyOut", json_boolean(polyOut));
		json_object_set_new(rootJ, "bendRange", json_integer(bendRange));
		json_object_set_new(rootJ, "rootCents", json_real(rootCents));
		json_object_set_new(rootJ, "sampleName", json_string(getSampleName().c_str()));
		json_object_set_new(rootJ, "xfadeEqualPower", json_boolean(xfadeEqualPower));
		json_object_set_new(rootJ, "detectOnLoad", json_boolean(detectOnLoad));
		json_object_set_new(rootJ, "sensitivity", json_real(sensitivity));
		json_object_set_new(rootJ, "detector", json_integer(detector));
		json_object_set_new(rootJ, "clockPpqn", json_integer(clockPpqn));
		json_object_set_new(rootJ, "loopBeats", json_integer(loopBeats));
		json_object_set_new(rootJ, "selectedSlice", json_integer(selectedSlice));
		Slices u = getSlices();
		json_t *slicesJ = json_array();
		for (int i = 0; i < u.count; i++) {
			json_t *sJ = json_array();
			json_array_append_new(sJ, json_integer(u.start[i]));
			json_array_append_new(sJ, json_integer(u.type[i]));
			json_array_append_new(sJ, json_integer(u.choke[i]));
			json_array_append_new(sJ, json_real(u.level[i]));
			json_array_append_new(sJ, json_real(u.tune[i]));
			json_array_append_new(sJ, json_real(u.pan[i]));
			json_array_append_new(sJ, json_real(u.attack[i]));
			json_array_append_new(sJ, json_real(u.release[i]));
			json_array_append_new(slicesJ, sJ);
		}
		json_object_set_new(rootJ, "slices", slicesJ);
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		if (json_t *j = json_object_get(rootJ, "autoNormalize")) autoNormalize = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "waveBlue")) waveBlue = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "autoRoot")) autoRoot = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "polyOut")) polyOut = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "bendRange")) bendRange = clamp((int)json_integer_value(j), 1, 48);
		if (json_t *j = json_object_get(rootJ, "rootCents")) rootCents = json_number_value(j);
		rootSeen = params[ROOT_PARAM].getValue();   // params come in before data: not a change by hand
		if (json_t *j = json_object_get(rootJ, "sampleName")) setSampleName(json_string_value(j) ? json_string_value(j) : "");
		if (json_t *j = json_object_get(rootJ, "xfadeEqualPower")) xfadeEqualPower = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "detectOnLoad")) detectOnLoad = json_boolean_value(j);
		if (json_t *j = json_object_get(rootJ, "sensitivity")) sensitivity = clamp((float)json_number_value(j), 0.f, 1.f);
		if (json_t *j = json_object_get(rootJ, "detector")) detector = clamp((int)json_integer_value(j), 0, 1);
		if (json_t *j = json_object_get(rootJ, "clockPpqn")) clockPpqn = clamp((int)json_integer_value(j), 1, 24);
		if (json_t *j = json_object_get(rootJ, "loopBeats")) loopBeats = clamp((int)json_integer_value(j), 0, 64);
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
				u.choke[u.count] = (uint8_t)clamp((int)json_integer_value(json_array_get(sJ, 2)), 0, 4);
				u.level[u.count] = (float)json_number_value(json_array_get(sJ, 3));
				u.tune[u.count] = (float)json_number_value(json_array_get(sJ, 4));
				u.pan[u.count] = (float)json_number_value(json_array_get(sJ, 5));
				json_t *aJ = json_array_get(sJ, 6), *rJ = json_array_get(sJ, 7);
				u.attack[u.count] = aJ ? (float)json_number_value(aJ) : -1.f;
				u.release[u.count] = rJ ? (float)json_number_value(rJ) : -1.f;
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
		p.equalPower = xfadeEqualPower;
		p.reverse = params[REVERSE_PARAM].getValue() > 0.5f;
		p.timeMode = (int)params[TIME_MODE_PARAM].getValue();
		// speed and resonance: the knobs here, each voice adds its own CV (voiceSpeed, voiceResonance)
		float k = params[SPEED_PARAM].getValue();
		if (k != speedKnob) {
			speedKnob = k;
			speedPow = std::pow(2.f, k);
		}
		p.speed = clamp(speedPow * syncFactor, 0.0625f, 8.f);
		// as the knobs show them: 0-2 s and 2 ms-5 s, exponential
		k = params[ATTACK_PARAM].getValue();
		if (k != attackKnob) {
			attackKnob = k;
			attackMsNow = std::pow(2000.f, k) - 1.f;
		}
		p.attackMs = attackMsNow;
		k = params[RELEASE_PARAM].getValue();
		if (k != releaseKnob) {
			releaseKnob = k;
			releaseMsNow = 2.f * std::pow(2500.f, k);
		}
		p.releaseMs = releaseMsNow;
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
		if (slices)
			slicePlay(slot, c, note - (int)params[ROOT_PARAM].getValue(), p, startOffset, vel);
	}

	// slice i on engine voice `slot`; a slice in a choke group first fades out
	// the other voices of its group
	void slicePlay(int slot, int c, int i, const PlaySettings &p, double startOffset, float vel) {
		const Sample *smp = engine.current;
		if (!smp || !slices || smp->frames < 4 || i < 0 || i >= slices->count)
			return;
		int group = slices->choke[i];
		if (group > 0) {
			for (int s = 0; s < sounaire::Engine::SLOTS; s++) {
				sounaire::Voice &o = engine.voices[s];
				if (s != slot && s != sounaire::Engine::PREVIEW && o.active && !o.fadingOut && o.choke == group)
					engine.fadeOut(s);
			}
		}
		int64_t F = smp->originFrames > 0 ? smp->originFrames : smp->frames;   // the slices' frames
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
		engine.noteOn(slot, slot >= MAX_VOICES ? 0 : c, semitones(c), vel, p, startOffset, cutoffNote(c), &own, oneShot);
		sounaire::Voice &v = engine.voices[slot];
		v.choke = group;
		v.slice = i;
		v.pitchOffset = slices->tune[i];
		float g = std::pow(10.f, slices->level[i] / 20.f), pan = slices->pan[i];
		v.gainL = g * std::min(1.f, 1.f - pan);
		v.gainR = g * std::min(1.f, 1.f + pan);
		v.attackMs = slices->attack[i];
		v.releaseMs = slices->release[i];
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
		float n = params[CUTOFF_PARAM].getValue() + inputs[CUTOFF_INPUT].getPolyVoltage(c) * 12.f;
		return std::isfinite(n) ? n : params[CUTOFF_PARAM].getValue();
	}

	float voiceSpeed(int c) {
		float x = params[SPEED_PARAM].getValue() + inputs[SPEED_INPUT].getPolyVoltage(c);
		if (x != speedX[c]) {
			speedX[c] = x;
			speedOut[c] = std::pow(2.f, x);
		}
		return clamp(speedOut[c] * syncFactor, 0.0625f, 8.f);
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

	// The loop's length in beats (set, or guessed: the power of two that puts
	// the loop between 70 and 180 BPM, nearest 120), and the speed that plays
	// it in time with the clock. Without a clock for 2 s the speed is 1.
	static int guessBeats(double seconds) {
		int best = 4;
		double bestD = 1e9;
		for (int b = 1; b <= 64; b *= 2) {
			double bpm = 60.0 * b / seconds;
			double d = (bpm >= 70.0 && bpm < 180.0) ? std::fabs(bpm - 120.0) : 1e6 + std::fabs(bpm - 120.0);
			if (d < bestD) {
				bestD = d;
				best = b;
			}
		}
		return best;
	}

	void syncClock(const ProcessArgs &args) {
		if (!inputs[CLOCK_INPUT].isConnected()) {
			clockInterval = 0;
			syncOn = false;
			syncFactor = 1.f;
			return;
		}
		clockCount++;
		bool pulse = clockIn.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f);
		if (pulse) {
			// a change of tempo is followed within a beat; a steady one smoothly
			if (clockInterval <= 0 || std::fabs(clockCount - clockInterval) > 0.1 * clockInterval)
				clockInterval = (double)clockCount;
			else
				clockInterval += 0.5 * (clockCount - clockInterval);
			clockCount = 0;
		}
		const Sample *s = engine.current;
		if (clockInterval <= 0 || clockCount > 2.0 * args.sampleRate + clockInterval || !s || s->frames < 16) {
			syncOn = false;
			syncFactor = 1.f;
			return;
		}
		// the tempo moves only on a pulse; the beats only with the sample or the menu
		double seconds = s->frames / (double)s->rate;
		if (pulse || !syncOn || seconds != syncSeconds || loopBeats != syncLoopBeats) {
			if (seconds != syncSeconds || loopBeats != syncLoopBeats) {
				syncSeconds = seconds;
				syncLoopBeats = loopBeats;
				syncBeatsNow = loopBeats > 0 ? loopBeats : guessBeats(seconds);
			}
			double bpm = 60.0 * args.sampleRate / (clockInterval * clockPpqn);
			syncFactor = (float)(seconds * bpm / (60.0 * syncBeatsNow));
			syncBpmNow = (float)bpm;
			syncOn = true;
		}
	}

	// REC (button, or the gate while high) records IN L / IN R; the recording
	// replaces the sample when it stops (up to 5 minutes)
	void recordInput(const ProcessArgs &args) {
		bool want = params[REC_PARAM].getValue() > 0.5f || inputs[REC_INPUT].getVoltage() >= 1.f;
		if (want && !recording && recDone.load(std::memory_order_relaxed) < 0 && inputs[INL_INPUT].isConnected()) {
			recording = true;
			recCount = 0;
			recFrames = 0;
			recStartRate = args.sampleRate;
			recLimit = (int64_t)(300.f * args.sampleRate);
			recStereo = inputs[INR_INPUT].isConnected();
		}
		if (!recording)
			return;
		if (!want) {
			finishRecording();
			return;
		}
		RecChunk *ch = recCount > 0 ? recChunks[recCount - 1] : nullptr;
		if (!ch || ch->used >= RecChunk::FRAMES) {
			ch = nullptr;
			if (recCount < REC_MAX_CHUNKS)
				for (int i = 0; i < REC_SPARES && !ch; i++)
					if (recSpare[i].load(std::memory_order_relaxed))
						ch = recSpare[i].exchange(nullptr, std::memory_order_acq_rel);
			if (ch) {
				ch->used = 0;
				recChunks[recCount++] = ch;
			}
		}
		if (!ch || recFrames >= recLimit) {
			// out of chunks (the worker fell behind) or 5 minutes reached
			finishRecording();
			return;
		}
		float l = inputs[INL_INPUT].getVoltage() * 0.2f;
		ch->f[0][ch->used] = l;
		ch->f[1][ch->used] = recStereo ? inputs[INR_INPUT].getVoltage() * 0.2f : l;
		ch->used++;
		recFrames++;
	}

	// the recording goes to the worker, stamped with the rate it was made at
	void finishRecording() {
		recording = false;
		recRate = recStartRate;
		recStereoDone = recStereo;
		recDone = recCount;
		params[REC_PARAM].setValue(0.f);
	}

	void process(const ProcessArgs &args) override {
		const bool ui = uiDivider.process();
		// a new sample (hot-swap: playing notes keep the old one); with no room
		// left for the old ones, the oldest fades out first
		if (pending.load(std::memory_order_relaxed)) {
			if (engine.canTake()) {
				if (Sample *s = pending.exchange(nullptr, std::memory_order_acquire))
					engine.setSample(s);
			}
			else {
				engine.makeRoom();
			}
		}
		if (ui) {
			// hand back what nothing plays any more
			if (!unused.load(std::memory_order_relaxed)) {
				if (const Sample *u = engine.takeUnused())
					unused.store(u, std::memory_order_release);
			}
			if (!retiredPool.load(std::memory_order_relaxed)) {
				if (Stretcher *o = engine.takeOldPool())
					retiredPool.store(o, std::memory_order_release);
			}
		}
		if (resetMarkers.load(std::memory_order_relaxed) && resetMarkers.exchange(false)) {
			params[START_PARAM].setValue(0.f);
			params[END_PARAM].setValue(1.f);
			params[LOOP_START_PARAM].setValue(0.f);
			params[LOOP_END_PARAM].setValue(1.f);
		}
		// ROOT turned by hand, or a new sample without a detected root: no offset
		if ((clearCents.load(std::memory_order_relaxed) && clearCents.exchange(false)) || params[ROOT_PARAM].getValue() != rootSeen) {
			rootSeen = params[ROOT_PARAM].getValue();
			rootCents = 0.f;
		}
		float root = pendingRoot.load(std::memory_order_relaxed) > -999.f ? pendingRoot.exchange(-1000.f) : -1000.f;
		if (root > -999.f) {
			float n = std::round(root);
			params[ROOT_PARAM].setValue(clamp(n, 0.f, 127.f));
			rootSeen = params[ROOT_PARAM].getValue();
			rootCents = (root - n) * 100.f;
		}

		// slices edited in the display or found by the worker
		if (pendingSlices.load(std::memory_order_relaxed) && !retiredSlices.load(std::memory_order_relaxed)) {
			if (Slices *n = pendingSlices.exchange(nullptr, std::memory_order_acq_rel)) {
				retiredSlices.store(slices, std::memory_order_release);
				slices = n;
			}
		}

		syncClock(args);
		recordInput(args);

		PlaySettings p = settings();
		// stretchers for the time mode, built off the audio thread; the stretched
		// voices fade out on the old pool, handed back once they are done
		if (pendingPool.load(std::memory_order_relaxed) && engine.canTakePool()) {
			if (Stretcher *pool = pendingPool.exchange(nullptr, std::memory_order_acquire)) {
				engine.setPool(pool, pendingPoolPreset.load(std::memory_order_relaxed), pendingPoolRate.load(std::memory_order_relaxed));
				poolRequested = false;
			}
		}
		if (p.timeMode != TIME_REPITCH && !engine.poolReady(p.timeMode) && !poolRequested) {
			requestPool(sounaire::Engine::presetFor(p.timeMode), args.sampleRate);
			poolRequested = true;
		}
		bool slicing = sliceMode();
		bool legato = !slicing && params[LEGATO_PARAM].getValue() > 0.5f;
		if (ui) {
			lights[REVERSE_LIGHT].setBrightness(p.reverse ? 1.f : 0.f);
			lights[LEGATO_LIGHT].setBrightness(legato ? 1.f : 0.f);
		}
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

		// TRIG plays the slice SLICE picks, or the next one; RESET goes back to
		// slice 1. The slice plays while TRIG is high (gate and loop types).
		if (slicing) {
			if (resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f))
				seqSlice = -1;
			if (trigIn.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f) && slices) {
				int n = slices->count;
				int i = inputs[SLICE_INPUT].isConnected() ? clamp((int)(inputs[SLICE_INPUT].getVoltage() * 0.1f * n), 0, n - 1)
				                                         : (seqSlice + 1) % n;
				seqSlice = i;
				slicePlay(sounaire::Engine::TRIGGER, 0, i, p, 0.0, 1.f);
				trigOn = true;
			}
		}
		if (trigOn && !trigIn.isHigh()) {
			engine.noteOff(sounaire::Engine::TRIGGER);
			trigOn = false;
		}

		// a key clicked on the keyboard strip, at full velocity
		int mk = mouseKey.load(std::memory_order_relaxed) >= 0 ? mouseKey.exchange(-1) : -1;
		if (mk >= 0) {
			if (slicing)
				sliceNoteOn(sounaire::Engine::MOUSE, 0, mk, p, 0.0, 1.f);
			else if (noteInRange(mk))
				engine.noteOn(sounaire::Engine::MOUSE, 0, semitonesFor((float)mk, 0), 1.f, p, 0.0, cutoffNote(0));
			mouseOn = true;
			mouseShown = mk;
		}
		if (mouseOn && !mouseHeld.load(std::memory_order_relaxed)) {
			engine.noteOff(sounaire::Engine::MOUSE);
			mouseOn = false;
			mouseShown = -1;
		}

		// preview from the display
		float pp = previewPos.load(std::memory_order_relaxed) >= 0.f ? previewPos.exchange(-1.f) : -1.f;
		if (pp >= 0.f) {
			engine.previewOn(pp, p);
			previewing = true;
		}
		if (previewing && !previewHeld.load(std::memory_order_relaxed)) {
			engine.previewOff();
			previewing = false;
		}

		// for the display: the keys held now, the recording time, the tempo
		if (ui) {
			for (int c = 0; c < MAX_VOICES; c++) {
				float n = held[c] ? inputs[VOCT_INPUT].getPolyVoltage(c) * 12.f + 60.f : -1.f;
				keyHeld[c].store(std::isfinite(n) ? (int)std::round(n) : -1, std::memory_order_relaxed);
			}
			recSeconds.store(recording ? (float)(recFrames / recStartRate) : 0.f, std::memory_order_relaxed);
			syncBpm.store(syncOn ? syncBpmNow : 0.f, std::memory_order_relaxed);
			syncBeats.store(syncBeatsNow, std::memory_order_relaxed);
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
