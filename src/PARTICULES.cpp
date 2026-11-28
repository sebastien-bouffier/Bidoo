// pARtiCuLeS: grains from a buffer (the live input recorded without end, or a file), free running or on a clock, each
// with its envelope, pitch (V/O in turn), direction and pan, then a Fdn16 reverb with a shimmer. The engine is
// src/ParticulesEngine.hpp (Rack-free, tools/tests/particules_engine_test.cpp); this is the panel, the jacks, the clock,
// the file and the patch storage.
#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"
#include "ParticulesEngine.hpp"
#include "SounaireFlac.hpp"
#include "dep/dr_flac/dr_flac.h"
#include "dep/waves.hpp"
#include "osdialog.h"
#include <bidoo/dsp/time/PulseClock.h>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

using namespace std;

struct PARTICULES : BidooModule {
	enum ParamIds {
		POSITION_PARAM,
		SPRAY_PARAM,
		SCAN_PARAM,
		SIZE_PARAM,
		RATE_PARAM,
		CHAOS_PARAM,
		COUNT_PARAM,
		CLOCK_PARAM,
		SHAPE_PARAM,
		SOFT_PARAM,
		TUNE_PARAM,
		DETUNE_PARAM,
		WIDTH_PARAM,
		DIRECTION_PARAM,
		REC_PARAM,
		FREEZE_PARAM,
		DECAY_PARAM,
		ROOM_PARAM,
		TONE_PARAM,
		SHIMMER_PARAM,
		INTERVAL_PARAM,
		MIX_PARAM,
		LEVEL_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		POSITION_INPUT,
		SPRAY_INPUT,
		SIZE_INPUT,
		RATE_INPUT,
		SHAPE_INPUT,
		DETUNE_INPUT,
		MIX_INPUT,
		TRIG_INPUT,
		REC_INPUT,
		FREEZE_INPUT,
		L_INPUT,
		R_INPUT,
		VOCT_INPUT,
		CLOCK_INPUT,
		RESET_INPUT,
		SCAN_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		GRAIN_OUTPUT,
		ENV_OUTPUT,
		L_OUTPUT,
		R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	static constexpr float kVolts = 5.f;
	static constexpr int B = particules::kBlock;

	particules::Engine engine;
	bidoo::dsp::PulseClock clock;
	dsp::SchmittTrigger clockIn, resetIn, trigIn;
	dsp::PulseGenerator grainPulse;
	int clockPpqn = 4;
	int detuneQuant = 0;
	std::string fileName;   // the file loaded (shown), empty for the live buffer; under jobMutex
	std::atomic<bool> dirty {false};   // the buffer differs from the patch storage
	// Rack's rate, for the worker: APP (Rack's context) is per thread and null on it
	std::atomic<double> rackRate {48000.0};

	// Worker thread: decodes a file (or the patch storage) into a Buffer and hands it to the engine through
	// `pending`; it also frees `retired`. A newer request (or a clear) bumps loadGen: the older load is dropped.
	struct Job {
		std::string path;
		bool storage = false;   // the patch's buffer.flac
		double rate = 48000.0;  // Rack's rate when asked
		uint32_t gen = 0;
	};
	std::thread worker;
	std::mutex jobMutex;
	std::condition_variable jobCv;
	Job job;
	bool jobRequested = false;
	std::atomic<bool> quit {false};
	std::atomic<uint32_t> loadGen {0};
	std::atomic<bool> loading {false};

	// one block in, one block out (the output is a block late)
	float inL[B] = {}, inR[B] = {}, outL[B] = {}, outR[B] = {}, env[B] = {};
	double ppqs[B] = {};
	bool trigs[B] = {}, started[B] = {};
	int pos = 0;
	bool blockRunning = false;
	double blockBpm = 120.0;
	// for the screen
	std::atomic<float> bpmNow {0.f};
	std::atomic<bool> runningNow {false};

	// the RATE knob: grains a second, or a division of the clock
	struct RateQuantity : ParamQuantity {
		std::string getDisplayValueString() override {
			PARTICULES *m = dynamic_cast<PARTICULES *>(module);
			const float v = getValue();
			if (m && m->params[CLOCK_PARAM].getValue() > 0.5f)
				return particules::kDivisions[divisionOf(v)].name;
			return v < 0.005f ? "TRIG only" : rack::string::f("%.2f / s", rateOf(v));
		}
		std::string getLabel() override {
			PARTICULES *m = dynamic_cast<PARTICULES *>(module);
			return m && m->params[CLOCK_PARAM].getValue() > 0.5f ? "Division" : "Rate";
		}
	};
	struct ChaosQuantity : ParamQuantity {
		std::string getLabel() override {
			PARTICULES *m = dynamic_cast<PARTICULES *>(module);
			return m && m->params[CLOCK_PARAM].getValue() > 0.5f ? "Chaos (divisions skipped)" : "Chaos (0 regular, 100 random)";
		}
	};
	struct SecondsQuantity : ParamQuantity {
		float lo = 0.001f, hi = 2.f;
		std::string getDisplayValueString() override {
			const float s = lo * std::pow(hi / lo, getValue());
			return s < 1.f ? rack::string::f("%.1f ms", s * 1000.f) : rack::string::f("%.2f s", s);
		}
	};

	static float rateOf(float v) {
		return 0.5f * std::pow(400.f, v);           // 0.5 .. 200 a second
	}
	static int divisionOf(float v) {
		return clamp((int)std::round(v * (particules::kNumDivisions - 1)), 0, particules::kNumDivisions - 1);
	}
	static float sizeOf(float v) {
		return 0.001f * std::pow(2000.f, v);        // 1 ms .. 2 s
	}
	static float decayOf(float v) {
		return 0.3f * std::pow(100.f, v);           // 0.3 .. 30 s
	}

	PARTICULES() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(POSITION_PARAM, 0.f, 1.f, 0.5f, "Position (oldest to newest)", "%", 0.f, 100.f);
		configParam(SPRAY_PARAM, 0.f, 1.f, 0.1f, "Spray", "%", 0.f, 100.f);
		configParam(SCAN_PARAM, -1.f, 1.f, 0.f, "Scan (100% = the speed it was recorded at)", "%", 0.f, 100.f);
		SecondsQuantity *sq = configParam<SecondsQuantity>(SIZE_PARAM, 0.f, 1.f, 0.606f, "Size");
		sq->lo = 0.001f;
		sq->hi = 2.f;
		configParam<RateQuantity>(RATE_PARAM, 0.f, 1.f, 0.5f, "Rate");
		configParam<ChaosQuantity>(CHAOS_PARAM, 0.f, 1.f, 0.3f, "Chaos", "%", 0.f, 100.f);
		configParam(COUNT_PARAM, 1.f, 8.f, 1.f, "Grains started together");
		getParamQuantity(COUNT_PARAM)->snapEnabled = true;
		configSwitch(CLOCK_PARAM, 0.f, 1.f, 0.f, "Mode", {"Free running", "On the clock"});
		configParam(SHAPE_PARAM, 0.f, 1.f, 0.5f, "Envelope shape (percussive, bell, reversed)", "%", 0.f, 100.f);
		configParam(SOFT_PARAM, 0.f, 1.f, 0.7f, "Envelope softness", "%", 0.f, 100.f);
		configParam(TUNE_PARAM, -24.f, 24.f, 0.f, "Tune", " semitones");
		configParam(DETUNE_PARAM, 0.f, 1.f, 0.f, "Detune (random per grain)", "%", 0.f, 100.f);
		configParam(WIDTH_PARAM, 0.f, 1.f, 0.5f, "Stereo width", "%", 0.f, 100.f);
		configSwitch(DIRECTION_PARAM, 0.f, 3.f, 0.f, "Direction", {"Forward", "Reverse", "Ping-pong", "Random"});
		configSwitch(REC_PARAM, 0.f, 1.f, 1.f, "Record the input", {"Off", "On"});
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze (the buffer and the reverb's tail)", {"Off", "On"});
		SecondsQuantity *dq = configParam<SecondsQuantity>(DECAY_PARAM, 0.f, 1.f, 0.5f, "Reverb decay");
		dq->lo = 0.3f;
		dq->hi = 30.f;
		configParam(ROOM_PARAM, 0.f, 1.f, 0.6f, "Reverb room size", "%", 0.f, 100.f);
		configParam(TONE_PARAM, 0.f, 1.f, 0.6f, "Reverb tone (dark to bright)", "%", 0.f, 100.f);
		configParam(SHIMMER_PARAM, 0.f, 1.f, 0.f, "Shimmer", "%", 0.f, 100.f);
		configSwitch(INTERVAL_PARAM, 0.f, particules::kNumIntervals - 1, 2.f, "Shimmer interval",
			std::vector<std::string>(particules::kIntervalNames, particules::kIntervalNames + particules::kNumIntervals));
		configParam(MIX_PARAM, 0.f, 1.f, 0.3f, "Reverb mix", "%", 0.f, 100.f);
		configParam(LEVEL_PARAM, 0.f, 2.f, 1.f, "Level", " dB", -10.f, 20.f);
		configInput(POSITION_INPUT, "Position CV (10 V = the whole buffer)");
		configInput(SPRAY_INPUT, "Spray CV (10 V = full range)");
		configInput(SIZE_INPUT, "Size CV (10 V = full range)");
		configInput(RATE_INPUT, "Rate or division CV (10 V = full range)");
		configInput(SHAPE_INPUT, "Envelope shape CV (10 V = full range)");
		configInput(DETUNE_INPUT, "Detune CV (10 V = full range)");
		configInput(MIX_INPUT, "Reverb mix CV (10 V = full range)");
		configInput(TRIG_INPUT, "Trigger: starts COUNT grains");
		configInput(REC_INPUT, "Record gate (replaces the button)");
		configInput(FREEZE_INPUT, "Freeze gate");
		configInput(L_INPUT, "Left (polyphonic: the channels are summed)");
		configInput(R_INPUT, "Right (the left when not connected; polyphonic: the channels are summed)");
		configInput(VOCT_INPUT, "V/O (polyphonic: the grains take the channels in turn)");
		configInput(CLOCK_INPUT, "Clock");
		configInput(RESET_INPUT, "Reset");
		configInput(SCAN_INPUT, "Scan CV (5 V = full range)");
		configOutput(GRAIN_OUTPUT, "Trigger on each grain");
		configOutput(ENV_OUTPUT, "The newest grain's envelope (0 to 10 V)");
		configOutput(L_OUTPUT, "Left");
		configOutput(R_OUTPUT, "Right");
		configBypass(L_INPUT, L_OUTPUT);
		configBypass(R_INPUT, R_OUTPUT);
		updateClockName();
		rackRate = APP->engine->getSampleRate();
		engine.prepare(rackRate);
		worker = std::thread(&PARTICULES::workerLoop, this);
	}

	// deleted mid-load: the FLAC loop stops at its next chunk, a WAV/AIFF finishes first
	~PARTICULES() {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			quit = true;
		}
		jobCv.notify_all();
		if (worker.joinable())
			worker.join();
	}

	void updateClockName() {
		inputInfos[CLOCK_INPUT]->name = rack::string::f("Clock (%d PPQN)", clockPpqn);
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		rackRate = e.sampleRate;
		engine.prepare(e.sampleRate);
		clock.clear();
	}

	float cv(int input, float scale) {
		const float v = inputs[input].getVoltage();
		return std::isfinite(v) ? v * scale : 0.f;
	}

	void process(const ProcessArgs &args) override {
		// the clock, RESET, TRIG
		if (resetIn.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f))
			clock.reset();
		clock.setPpqn(clockPpqn);
		clock.tick(clockIn.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f), args.sampleRate);
		const bool running = inputs[CLOCK_INPUT].isConnected() && clock.running();
		trigs[pos] = trigIn.process(inputs[TRIG_INPUT].getVoltage(), 0.1f, 1.f);
		ppqs[pos] = clock.position();
		if (pos == 0) {
			blockRunning = running;
			blockBpm = running ? clock.bpm() : 120.0;
		}
		blockRunning = blockRunning && running;

		const float l = inputs[L_INPUT].getVoltageSum() / kVolts;
		inL[pos] = std::isfinite(l) ? l : 0.f;
		const float r = inputs[R_INPUT].isConnected() ? inputs[R_INPUT].getVoltageSum() / kVolts : inL[pos];
		inR[pos] = std::isfinite(r) ? r : 0.f;

		// what the block before gave
		outputs[L_OUTPUT].setVoltage(bidooSoftLimit(outL[pos] * kVolts));
		outputs[R_OUTPUT].setVoltage(bidooSoftLimit(outR[pos] * kVolts));
		if (started[pos])
			grainPulse.trigger(1e-3f);
		outputs[GRAIN_OUTPUT].setVoltage(grainPulse.process(args.sampleTime) ? 10.f : 0.f);
		outputs[ENV_OUTPUT].setVoltage(10.f * clamp(env[pos], 0.f, 1.f));

		if (++pos < B)
			return;
		pos = 0;
		runBlock();
	}

	void runBlock() {
		particules::Controls c;
		c.position = clamp(params[POSITION_PARAM].getValue() + cv(POSITION_INPUT, 0.1f), 0.f, 1.f);
		c.spray = clamp(params[SPRAY_PARAM].getValue() + cv(SPRAY_INPUT, 0.1f), 0.f, 1.f);
		c.scan = clamp(params[SCAN_PARAM].getValue() + cv(SCAN_INPUT, 0.2f), -1.f, 1.f);
		c.size = sizeOf(clamp(params[SIZE_PARAM].getValue() + cv(SIZE_INPUT, 0.1f), 0.f, 1.f));
		c.clockMode = params[CLOCK_PARAM].getValue() > 0.5f;
		const float rateV = clamp(params[RATE_PARAM].getValue() + cv(RATE_INPUT, 0.1f), 0.f, 1.f);
		c.rate = rateV < 0.005f ? 0.f : rateOf(rateV);
		c.division = divisionOf(rateV);
		const float chaos = params[CHAOS_PARAM].getValue();
		c.chaos = chaos;
		c.chance = 1.f - chaos;
		c.count = clamp((int)std::round(params[COUNT_PARAM].getValue()), 1, 8);
		c.shape = clamp(params[SHAPE_PARAM].getValue() + cv(SHAPE_INPUT, 0.1f), 0.f, 1.f);
		c.soft = params[SOFT_PARAM].getValue();
		c.tune = params[TUNE_PARAM].getValue();
		c.detune = clamp(params[DETUNE_PARAM].getValue() + cv(DETUNE_INPUT, 0.1f), 0.f, 1.f);
		c.detuneQuant = detuneQuant;
		c.direction = clamp((int)std::round(params[DIRECTION_PARAM].getValue()), 0, 3);
		c.width = params[WIDTH_PARAM].getValue();
		const bool freeze = params[FREEZE_PARAM].getValue() > 0.5f || inputs[FREEZE_INPUT].getVoltage() >= 1.f;
		const bool rec = inputs[REC_INPUT].isConnected() ? inputs[REC_INPUT].getVoltage() >= 1.f : params[REC_PARAM].getValue() > 0.5f;
		c.record = rec && !freeze;
		c.freeze = freeze;
		c.decay = decayOf(params[DECAY_PARAM].getValue());
		c.room = params[ROOM_PARAM].getValue();
		c.tone = params[TONE_PARAM].getValue();
		c.shimmer = params[SHIMMER_PARAM].getValue();
		c.interval = clamp((int)std::round(params[INTERVAL_PARAM].getValue()), 0, particules::kNumIntervals - 1);
		c.mix = clamp(params[MIX_PARAM].getValue() + cv(MIX_INPUT, 0.1f), 0.f, 1.f);
		c.level = params[LEVEL_PARAM].getValue();
		if (c.record)
			dirty = true;

		float voct[16];
		const int nv = inputs[VOCT_INPUT].isConnected() ? inputs[VOCT_INPUT].getChannels() : 0;
		for (int i = 0; i < nv; i++) {
			const float v = inputs[VOCT_INPUT].getVoltage(i);
			voct[i] = std::isfinite(v) ? clamp(v, -5.f, 5.f) : 0.f;
		}
		engine.process(inL, inR, ppqs, blockRunning, blockBpm, trigs, voct, nv, c, outL, outR, started, env);
		bpmNow = (float)blockBpm;
		runningNow = blockRunning;
	}

	// ---------------------------------------------------------------- the buffer: a file, the patch

	// a new buffer for the audio thread (the one waiting, if any, is dropped); under jobMutex
	void install(particules::Buffer *b) {
		delete engine.pending.exchange(b);
		dirty = true;
	}

	std::string shownName() {
		std::lock_guard<std::mutex> lock(jobMutex);
		return fileName;
	}

	// any thread but the audio thread: the newest request wins
	void requestLoad(const std::string &path, bool storage) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			job.path = path;
			job.storage = storage;
			job.rate = APP->engine->getSampleRate();
			job.gen = ++loadGen;
			jobRequested = true;
		}
		loading = true;
		jobCv.notify_all();
	}

	void loadFile(const std::string &path) {
		requestLoad(path, false);
	}

	void workerLoop() {
		std::unique_lock<std::mutex> lock(jobMutex);
		while (!quit) {
			// the audio thread can't notify without locking: poll for `retired`
			jobCv.wait_for(lock, std::chrono::milliseconds(50));
			engine.freeRetired();
			if (engine.rateMismatch.exchange(false) && !jobRequested && !loading && !quit)
				resampleView();
			if (!jobRequested || quit)
				continue;
			Job j = job;
			jobRequested = false;
			lock.unlock();
			particules::Buffer *b = j.storage ? decodeStorage(j) : decodeFile(j);
			lock.lock();
			if (b && j.gen == loadGen && !quit) {
				install(b);
				if (j.storage) {
					dirty = false;
				}
				else {
					fileName = rack::system::getFilename(j.path);
					params[REC_PARAM].setValue(0.f);   // the file stays as it is until REC goes on again
				}
			}
			else {
				delete b;
			}
			if (j.gen == loadGen)
				loading = false;
		}
	}

	// worker, under jobMutex: REC is on and the buffer is at another rate. The worker is the only one that frees
	// buffers, so it may read `view` without a ViewLock
	void resampleView() {
		const particules::Buffer *src = engine.view.load();
		const double rate = rackRate.load();
		if (!src || src->rate == rate || engine.pending.load())
			return;
		particules::Buffer *b = new particules::Buffer;
		b->resampleFrom(*src, rate);
		install(b);
	}

	bool stale(uint32_t gen) {
		return quit || loadGen != gen;
	}

	// FLAC, chunk by chunk up to maxSeconds (0: all), at its own rate, times `gain`; false if unread or dropped
	bool readFlac(const std::string &path, float gain, double maxSeconds, uint32_t gen, std::vector<float> &l, std::vector<float> &r, double &rate) {
		drflac *f = drflac_open_file(path.c_str(), nullptr);
		if (!f)
			return false;
		const unsigned ch = f->channels;
		rate = f->sampleRate;
		const size_t cap = maxSeconds > 0.0 ? (size_t)(maxSeconds * rate) : SIZE_MAX;
		if (f->totalPCMFrameCount > 0) {
			l.reserve(std::min(cap, (size_t)f->totalPCMFrameCount));
			r.reserve(std::min(cap, (size_t)f->totalPCMFrameCount));
		}
		const size_t chunk = 4096;
		std::vector<float> buf(chunk * std::max(1u, ch));
		bool ok = ch > 0;
		while (ok && l.size() < cap) {
			if (stale(gen)) {
				ok = false;
				break;
			}
			const drflac_uint64 got = drflac_read_pcm_frames_f32(f, std::min(chunk, cap - l.size()), buf.data());
			if (got == 0)
				break;
			for (drflac_uint64 i = 0; i < got; i++) {
				l.push_back(gain * buf[i * ch]);
				r.push_back(gain * buf[i * ch + (ch > 1 ? 1 : 0)]);
			}
		}
		drflac_close(f);
		return ok;
	}

	// worker: WAV and AIFF (resampled to Rack's rate), FLAC (kept at its own rate: the grains read it at the right pitch)
	particules::Buffer *decodeFile(const Job &j) {
		std::vector<float> l, r;
		double rate = j.rate;
		if (rack::string::lowercase(rack::system::getExtension(j.path)) == ".flac") {
			if (!readFlac(j.path, 1.f, 120.0, j.gen, l, r, rate))   // two minutes at most
				return nullptr;
		}
		else {
			std::string name, ext;
			int ch = 0, sr = 0, cnt = 0;
			std::vector<dsp::Frame<2>> frames = waves::getStereoWav(j.path, (float)rate, name, ext, ch, sr, cnt, 120.0);
			const size_t n = std::min(frames.size(), (size_t)(120.0 * rate));   // two minutes at most
			l.resize(n);
			r.resize(n);
			for (size_t i = 0; i < n; i++) {
				l[i] = frames[i].samples[0];
				r[i] = frames[i].samples[1];
			}
		}
		if (l.size() < 64 || stale(j.gen))
			return nullptr;
		particules::Buffer *b = new particules::Buffer;
		b->fill(l.data(), r.data(), (int64_t)l.size(), rate);
		return b;
	}

	// worker: the patch's buffer (saved at half level), with room to record on: at least the live buffer's length
	particules::Buffer *decodeStorage(const Job &j) {
		std::vector<float> l, r;
		double rate = 0.0;
		if (!readFlac(j.path, 2.f, 0.0, j.gen, l, r, rate))
			return nullptr;
		const int64_t frames = (int64_t)l.size();
		particules::Buffer *b = new particules::Buffer;
		const double live = particules::kLiveSeconds * std::max(48000.0, rate);
		b->allocate(std::max<int64_t>(frames, (int64_t)live), rate);
		for (int64_t i = 0; i < frames; i++)
			b->record(l[(size_t)i], r[(size_t)i], 1.f);
		return b;
	}

	// a clear also drops a load on its way
	void clearBuffer() {
		particules::Buffer *b = new particules::Buffer;
		const double rate = APP->engine->getSampleRate();
		b->allocate((int64_t)(particules::kLiveSeconds * std::max(48000.0, rate)), rate);
		std::lock_guard<std::mutex> lock(jobMutex);
		++loadGen;
		jobRequested = false;
		loading = false;
		install(b);
		fileName.clear();
	}

	std::string storagePath() {
		return rack::system::join(createPatchStorageDirectory(), "buffer.flac");
	}

	// the buffer, oldest to newest, while it doesn't record (a recording one would change while it's written)
	void onSave(const SaveEvent &e) override {
		// while a load runs, the storage keeps what it has (it may be the file being read)
		particules::Engine::ViewLock view(engine);
		const particules::Buffer *b = view.b;
		if (!b || engine.recording.load() || !dirty || loading)
			return;
		std::string path = storagePath();
		if (b->length < 64) {
			if (rack::system::isFile(path))
				rack::system::remove(path);
			dirty = false;
			return;
		}
		std::vector<float> ch[2];
		ch[0].resize((size_t)b->length);
		ch[1].resize((size_t)b->length);
		const int64_t old = b->oldest();
		for (int64_t i = 0; i < b->length; i++) {
			const size_t j = (size_t)((old + i) % b->cap);
			// at half level, so that up to 10 V fits in FLAC's -1..1 (doubled back when read)
			ch[0][(size_t)i] = clamp(0.5f * b->l[j], -1.f, 1.f);
			ch[1][(size_t)i] = clamp(0.5f * b->r[j], -1.f, 1.f);
		}
		if (sounaire::flacWriteFile(path, ch, 2, b->length, (int)b->rate, 24))
			dirty = false;
	}

	// the patch's buffer, decoded by the worker
	void onAdd(const AddEvent &e) override {
		std::string path = rack::system::join(getPatchStorageDirectory(), "buffer.flac");
		if (rack::system::isFile(path))
			requestLoad(path, true);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "clockPpqn", json_integer(clockPpqn));
		json_object_set_new(rootJ, "detuneQuant", json_integer(detuneQuant));
		json_object_set_new(rootJ, "fileName", json_string(shownName().c_str()));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		if (json_t *j = json_object_get(rootJ, "clockPpqn"))
			clockPpqn = clamp((int)json_integer_value(j), 1, 96);
		if (json_t *j = json_object_get(rootJ, "detuneQuant"))
			detuneQuant = clamp((int)json_integer_value(j), 0, 2);
		if (json_t *j = json_object_get(rootJ, "fileName")) {
			std::lock_guard<std::mutex> lock(jobMutex);
			const char *s = json_string_value(j);
			fileName = s ? s : "";
		}
		updateClockName();
	}
};

// ---------------------------------------------------------------- the screen

// The buffer's waveform (oldest left, newest right) in pink, bright inside the window the grains are taken from
// (POSITION and SPRAY, shaded), each grain a blue speck at its place, brighter as its envelope opens, the write head
// orange while it records; the mode, the rate or division, the size, the grains playing, and the envelope's shape.
struct ParticulesScreen : TransparentWidget {
	PARTICULES *module = nullptr;

	static std::shared_ptr<window::Font> font() {
		return APP->window->loadFont(asset::system("res/fonts/ShareTechMono-Regular.ttf"));
	}

	void drawLayer(const DrawArgs &args, int layer) override {
		if (layer == 1)
			drawScreen(args.vg);
		TransparentWidget::drawLayer(args, layer);
	}

	void text(NVGcontext *vg, float x, float y, const std::string &s, int align, NVGcolor c, float size = 9.f) {
		nvgFontSize(vg, size);
		nvgTextAlign(vg, align);
		nvgFillColor(vg, c);
		nvgText(vg, x, y, s.c_str(), nullptr);
	}

	void drawScreen(NVGcontext *vg) {
		const float w = box.size.x, h = box.size.y;
		std::shared_ptr<window::Font> f = font();
		if (!f)
			return;
		nvgFontFaceId(vg, f->handle);
		const NVGcolor dim = nvgRGB(0x57, 0x53, 0x4A);
		if (!module) {
			text(vg, w / 2, h / 2, "pARtiCuLeS  grains from a buffer", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, theme::kScreenYellow, 11.f);
			return;
		}
		particules::Engine &e = module->engine;
		particules::Engine::ViewLock view(e);
		const particules::Buffer *b = view.b;
		const float top = 15.f, bottom = h - 15.f, mid = 0.5f * (top + bottom), amp = 0.5f * (bottom - top) - 1.f;
		// the window the grains come from, wrapping round
		const float lo = e.windowLo.load(), hi = e.windowHi.load();
		auto inWindow = [&](float p) {
			for (float o : {-1.f, 0.f, 1.f})
				if (p >= lo + o && p <= hi + o)
					return true;
			return false;
		};
		if (b && b->length > 0) {
			// peaks and RMS core, one column per screen pixel; the grains' window bright (src/WaveStyle.hpp)
			const int64_t old = b->oldest();
			const float step = wavestyle::pixelStep(vg);
			const int n = (int)std::ceil(w / step);
			for (int pass = 0; pass < 2; pass++)
				for (int layer = 0; layer < 2; layer++) {
					nvgBeginPath(vg);
					for (int i = 0; i < n; i++) {
						const float x = i * step, p = x / w;
						if (inWindow(p) != (pass == 1))
							continue;
						// the bins the column covers (the ring wraps): their highest peak and their RMS
						const int k0 = b->bin((old + (int64_t)(p * (double)b->length)) % b->cap);
						const int k1 = b->bin((old + (int64_t)(std::min(1.f, (x + step) / w) * (double)b->length)) % b->cap);
						const int count = std::max(1, (k1 - k0 + particules::kPeaks) % particules::kPeaks);
						float pk = 0.f;
						double sq = 0.0;
						for (int j = 0; j < count; j++) {
							const int k = (k0 + j) % particules::kPeaks;
							pk = std::max(pk, b->peaks[k]);
							sq += (double)b->rms[k] * b->rms[k];
						}
						const float v = clamp(layer == 0 ? pk : std::min((float)std::sqrt(sq / count), pk), 0.f, 1.f);
						if (layer == 0 || v > 0.f)
							nvgRect(vg, x, mid - v * amp, step, std::max(layer == 0 ? step : 0.f, 2.f * v * amp));
					}
					nvgFillColor(vg, layer == 0 ? (pass == 1 ? wavestyle::kActivePeak : wavestyle::kIdlePeak) : (pass == 1 ? wavestyle::kActiveCore : wavestyle::kIdleCore));
					nvgFill(vg);
				}
			for (float o : {-1.f, 0.f, 1.f})
				if (hi + o > 0.f && lo + o < 1.f)
					wavestyle::band(vg, std::max(0.f, lo + o) * w, std::min(1.f, hi + o) * w, top, bottom);
			const float centre = 0.5f * (lo + hi);
			wavestyle::bar(vg, (centre - std::floor(centre)) * w, top, bottom);   // POSITION (with the scan)
		}
		else {
			text(vg, w / 2, mid, "the buffer is empty: patch IN and press REC", NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE, dim, 9.f);
		}
		// the grains: specks, each at its own height
		nvgSave(vg);
		nvgGlobalCompositeOperation(vg, NVG_LIGHTER);
		for (int k = 0; k < particules::kGrains; k++) {
			const float p = e.grainPos[k];
			if (p < 0.f)
				continue;
			const float en = clamp(e.grainEnv[k], 0.f, 1.f);
			const float y = top + 4.f + (float)((k * 37) % 64) / 63.f * (bottom - top - 8.f);
			nvgBeginPath(vg);
			nvgCircle(vg, p * w, y, 1.2f + 2.3f * en);
			nvgFillColor(vg, nvgRGBf(wavestyle::kHead.r * (0.3f + 0.7f * en), wavestyle::kHead.g * (0.3f + 0.7f * en), wavestyle::kHead.b * (0.3f + 0.7f * en)));
			nvgFill(vg);
		}
		nvgRestore(vg);
		if (e.recording.load())
			wavestyle::head(vg, w - 2.f, top, bottom, 1.f, theme::kLitOrange);

		// top: the buffer, the mode
		const std::string name = module->shownName();
		std::string left = e.recording.load() ? "REC" : name.empty() ? "FROZEN" : name;
		if (module->loading.load())
			left = "LOADING...";
		if (b && b->length > 0)
			left += rack::string::f("  %.1f s", (double)b->length / b->rate);
		text(vg, 4, 2, left, NVG_ALIGN_LEFT | NVG_ALIGN_TOP, e.recording.load() ? theme::kLitOrange : theme::kScreenYellow, 9.f);
		const bool clocked = module->params[PARTICULES::CLOCK_PARAM].getValue() > 0.5f;
		const float rateV = module->params[PARTICULES::RATE_PARAM].getValue();
		std::string right;
		if (clocked)
			right = module->runningNow.load() ? rack::string::f("CLOCK %s  %.0f BPM", particules::kDivisions[PARTICULES::divisionOf(rateV)].name, module->bpmNow.load()) : "CLOCK  waiting";
		else
			right = rateV < 0.005f ? "TRIG only" : rack::string::f("FREE %.1f / s", PARTICULES::rateOf(rateV));
		text(vg, w - 4, 2, right, NVG_ALIGN_RIGHT | NVG_ALIGN_TOP, theme::kScreenYellow, 9.f);
		// bottom: size, grains, direction; the envelope's shape at the right
		const float size = PARTICULES::sizeOf(module->params[PARTICULES::SIZE_PARAM].getValue());
		const int dir = clamp((int)std::round(module->params[PARTICULES::DIRECTION_PARAM].getValue()), 0, 3);
		const std::string bl = rack::string::f("%s  x%d  %s  %d grains", size < 1.f ? rack::string::f("%.0f ms", size * 1000.f).c_str() : rack::string::f("%.2f s", size).c_str(),
			(int)std::round(module->params[PARTICULES::COUNT_PARAM].getValue()), particules::kDirectionNames[dir], e.active.load());
		text(vg, 4, h - 2, bl, NVG_ALIGN_LEFT | NVG_ALIGN_BOTTOM, dim, 8.f);
		const float ex = w - 64.f, ew = 60.f, eb = h - 3.f, eh = 10.f;
		const float shape = module->params[PARTICULES::SHAPE_PARAM].getValue(), soft = module->params[PARTICULES::SOFT_PARAM].getValue();
		const float minA = std::min(0.5f, 0.0003f / std::max(0.001f, size));
		const float attack = minA + shape * (1.f - 2.f * minA);
		nvgBeginPath(vg);
		for (int i = 0; i <= 60; i++) {
			const float u = i / 60.f;
			const float y = eb - eh * e.envelope(u, attack, soft);
			if (i == 0)
				nvgMoveTo(vg, ex + ew * u, y);
			else
				nvgLineTo(vg, ex + ew * u, y);
		}
		nvgStrokeColor(vg, theme::kScreenYellow);
		nvgStrokeWidth(vg, 1.f);
		nvgStroke(vg);
	}
};

// ---------------------------------------------------------------- the panel

struct PARTICULESWidget : BidooWidget {
	PARTICULESWidget(PARTICULES *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/PARTICULES.svg"));

		theme::addScrews(this);

		// On the grid, as tools/panels/particules.py draws it: the screen down to row 4's knob labels, three knob rows
		// of 8 columns (rows 4, 3, 2), the jacks in 10 columns on rows 1 and 0.
		const float W = box.size.x;
		ParticulesScreen *screen = new ParticulesScreen;
		screen->module = module;
		screen->box = theme::specificRect(theme::kSideMargin, theme::kScreenTop, W - 2.f * theme::kSideMargin, theme::screenAboveKnobs(4) - theme::kScreenTop);
		addChild(screen);

		auto knob = [&](int i, int k) { return theme::at(i, 8, k, W); };
		addParam(theme::param<BidooBlueKnob>(knob(0, 4), module, PARTICULES::POSITION_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(1, 4), module, PARTICULES::SPRAY_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(2, 4), module, PARTICULES::SCAN_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(3, 4), module, PARTICULES::SIZE_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(4, 4), module, PARTICULES::RATE_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(5, 4), module, PARTICULES::CHAOS_PARAM));
		addParam(theme::param<BidooBlueSnapKnob>(knob(6, 4), module, PARTICULES::COUNT_PARAM));
		addParam(theme::param<BidooRoundButton>(knob(7, 4), module, PARTICULES::CLOCK_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(0, 3), module, PARTICULES::SHAPE_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(1, 3), module, PARTICULES::SOFT_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(2, 3), module, PARTICULES::TUNE_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(3, 3), module, PARTICULES::DETUNE_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(4, 3), module, PARTICULES::WIDTH_PARAM));
		addParam(theme::param<BidooBlueSnapKnob>(knob(5, 3), module, PARTICULES::DIRECTION_PARAM));
		addParam(theme::param<BidooRoundButton>(knob(6, 3), module, PARTICULES::REC_PARAM));
		addParam(theme::param<BidooRoundButton>(knob(7, 3), module, PARTICULES::FREEZE_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(0, 2), module, PARTICULES::DECAY_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(1, 2), module, PARTICULES::ROOM_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(2, 2), module, PARTICULES::TONE_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(3, 2), module, PARTICULES::SHIMMER_PARAM));
		addParam(theme::param<BidooBlueSnapKnob>(knob(4, 2), module, PARTICULES::INTERVAL_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(5, 2), module, PARTICULES::MIX_PARAM));
		addParam(theme::param<BidooBlueKnob>(knob(6, 2), module, PARTICULES::LEVEL_PARAM));

		const int n = 10;
		auto jack = [&](int i, int k) { return theme::jackAt(i, n, k, W); };
		static const int row1[10] = {PARTICULES::POSITION_INPUT, PARTICULES::SPRAY_INPUT, PARTICULES::SIZE_INPUT, PARTICULES::RATE_INPUT,
			PARTICULES::SHAPE_INPUT, PARTICULES::DETUNE_INPUT, PARTICULES::MIX_INPUT, PARTICULES::TRIG_INPUT, PARTICULES::REC_INPUT, PARTICULES::FREEZE_INPUT};
		for (int i = 0; i < 10; i++)
			addInput(theme::input<PJ301MPort>(jack(i, 1), module, row1[i]));
		static const int row0[6] = {PARTICULES::L_INPUT, PARTICULES::R_INPUT, PARTICULES::VOCT_INPUT, PARTICULES::CLOCK_INPUT, PARTICULES::RESET_INPUT, PARTICULES::SCAN_INPUT};
		for (int i = 0; i < 6; i++)
			addInput(theme::input<PJ301MPort>(jack(i, 0), module, row0[i]));
		static const int outs[4] = {PARTICULES::GRAIN_OUTPUT, PARTICULES::ENV_OUTPUT, PARTICULES::L_OUTPUT, PARTICULES::R_OUTPUT};
		for (int i = 0; i < 4; i++)
			addOutput(theme::output<PJ301MPort>(jack(6 + i, 0), module, outs[i]));
	}

	void onPathDrop(const PathDropEvent &e) override {
		PARTICULES *m = dynamic_cast<PARTICULES *>(module);
		if (m && !e.paths.empty()) {
			rememberSampleFolder(e.paths[0]);
			m->loadFile(e.paths[0]);
		}
	}

	void appendContextMenu(Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		PARTICULES *m = dynamic_cast<PARTICULES *>(module);
		if (!m)
			return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Buffer"));
		menu->addChild(createMenuItem("Load a sample (or drop a file on the module)", "", [=]() {
			// the last folder a sample came from, kept between openings and sessions (plugin.hpp)
			char *path = osdialog_file(OSDIALOG_OPEN, sampleDialogDir().c_str(), nullptr, nullptr);
			if (path) {
				rememberSampleFolder(path);
				m->loadFile(path);
				std::free(path);
			}
		}));
		menu->addChild(createMenuItem("Clear the buffer (back to the live input)", "", [=]() { m->clearBuffer(); }));
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Grains"));
		menu->addChild(createIndexPtrSubmenuItem("Detune steps", {particules::kDetuneQuantNames[0], particules::kDetuneQuantNames[1], particules::kDetuneQuantNames[2]}, &m->detuneQuant));
		static const int ppqns[8] = {1, 2, 4, 8, 12, 24, 48, 96};
		menu->addChild(createIndexSubmenuItem("Clock pulses per beat", {"1", "2", "4 (16th notes)", "8", "12", "24 (MIDI clock)", "48", "96"},
			[=]() { for (int i = 0; i < 8; i++) if (ppqns[i] == m->clockPpqn) return (size_t)i; return (size_t)2; },
			[=](size_t i) { m->clockPpqn = ppqns[i]; m->updateClockName(); }));
	}
};

Model *modelPARTICULES = createModel<PARTICULES, PARTICULESWidget>("PARTICULES");
