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
	// the frames, shared (never copied) by the engine's sample and the display's; freed by whichever thread drops
	// the last one, never the audio thread (it only reads `frames`, the worker deletes what it retires)
	std::shared_ptr<const std::vector<dsp::Frame<2>>> buf;
	const dsp::Frame<2> *frames = nullptr;   // buf's data
	sampleread::Mip<2> mip;  // band-limited copies, for playing faster than 1x
	int channels = 0;
	int count = 0;
	float sampleRate = 0.f;
	float overviewMin[2][OVERVIEW] = {};
	float overviewMax[2][OVERVIEW] = {};
	float overviewRms[2][OVERVIEW] = {};

	void buildOverview() {
		for (int b = 0; b < OVERVIEW; b++) {
			int i0 = (int)((long long)b * count / OVERVIEW), i1 = std::min(std::max((int)((long long)(b + 1) * count / OVERVIEW), i0 + 1), count);
			for (int c = 0; c < 2; c++)
				wavestyle::binOf(i0, i1, [&](int64_t i) { return frames[i].samples[c]; }, overviewMin[c][b], overviewMax[c][b], overviewRms[c][b]);
		}
	}
};

// One playing head. OUAIve plays one per polyphonic channel of GATE / POS / V/O: several slices or pitches at once
struct OuaiveVoice {
	bool play = false;
	double samplePos = 0.0;  // double: a float loses the pitch far into long samples
	int sliceIndex = -1;
	float speed = 1.0f;
	float speedExp = 0.f;     // the speed in octaves, exp2 only when it moves
	bool gateWasHigh = false;
	dsp::SchmittTrigger playTrigger;
	int eoc = 0;
	dsp::PulseGenerator eocPulse;
	float scrubPos = 0.f;     // gate mode with a position CV: the CV, smoothed
	sampleread::Declick<2> dc;
	sampleread::OctaveCache oct;   // log2 of its speed, for the band-limited read
	float lastStep = 0.f, lastGain = 0.f;  // of the last sample played
	sampleread::Declick<2> fadeOld[2];     // its heads on a sample being replaced, fading out
	double lastPos = 0.0;
	void silence() {
		play = false;
		dc.reset();
		eoc = 0;
		eocPulse.reset();
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

	std::string lastPath;
	int trigMode = 0; // 0 trig, 1 gate, 2 sliced
	int sliceLength = 0;
	int nbSlices = 1;
	int readMode = 0; // 0 forward, 1 backward, 2 repeat
	dsp::SchmittTrigger trigModeTrigger;
	dsp::SchmittTrigger readModeTrigger;
	dsp::SchmittTrigger posResetTrigger;
	OuaiveVoice voices[16];
	int nVoices = 1;
	// for the display: the first voice, and every playing head
	bool play = false;
	double samplePos = 0.0;
	int sliceIndex = -1;
	float speed = 1.0f;
	float heads[16] = {};
	std::atomic<int> headCount{0};
	// A sample replaced while it plays: kept until its heads (the playing ones, their tails) have faded out, then retired
	OuaiveSample *fading = nullptr;

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
		configParam(SPEED_PARAM, -4.0f, 4.0f, 0.0f, "Tune", " semitones", 0.f, 12.f);
		configParam(CVSPEED_PARAM, -1.0f / 12.0f, 1.0f / 12.0f, 0.0f, "Fine tune", " cents", 0.f, 1200.f);
		configInput(GATE_INPUT, "Trigger / gate (polyphonic: one voice per channel)");
		configInput(POS_INPUT, "Position (0-10 V = start-end, or the slice; polyphonic: one voice per channel)");
		configInput(NB_SLICES_INPUT, "Slices CV");
		configInput(READ_MODE_INPUT, "Read mode (0, 5, 10 V)");
		configInput(SPEED_INPUT, "V/O: 1 V per octave, 0 V plays the sample at its own pitch (added to the tunings, 4 octaves up or down at most; polyphonic: one voice per channel)");
		configInput(POS_RESET_INPUT, "Position reset (every voice)");
		configOutput(OUTL_OUTPUT, "Left (one channel per voice)");
		configOutput(OUTR_OUTPUT, "Right (one channel per voice)");
		configOutput(EOC_OUTPUT, "End of cycle (one channel per voice)");
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
		delete fading;
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
				loading = true;   // the audio thread stops asking while this reload runs
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
			// no path: a clear, the module gets an empty sample (silence)
			auto buf = path.empty() ? std::make_shared<std::vector<dsp::Frame<2>>>()
				: std::make_shared<std::vector<dsp::Frame<2>>>(waves::getStereoWav(path, rate, name, ext, s->channels, sr, s->count));
			s->count = buf->size();
			s->frames = buf->data();
			s->buf = buf;
			s->sampleRate = rate;
			s->mip.build((const float *)s->frames, s->count);
			s->buildOverview();
			{
				std::lock_guard<std::mutex> ui(uiMutex);
				auto shown = std::make_shared<OuaiveSample>();
				shown->buf = s->buf; // the display reads the frames when zoomed in
				shown->frames = s->frames;
				shown->count = s->count;
				shown->channels = s->channels;
				std::copy(&s->overviewMin[0][0], &s->overviewMin[0][0] + 2 * OuaiveSample::OVERVIEW, &shown->overviewMin[0][0]);
				std::copy(&s->overviewMax[0][0], &s->overviewMax[0][0] + 2 * OuaiveSample::OVERVIEW, &shown->overviewMax[0][0]);
				std::copy(&s->overviewRms[0][0], &s->overviewRms[0][0] + 2 * OuaiveSample::OVERVIEW, &shown->overviewRms[0][0]);
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
	void takePending(int fadeLen = 0) {
		if (pending.load(std::memory_order_relaxed) && !retired.load(std::memory_order_acquire) && !fading) {
			OuaiveSample *s = pending.exchange(nullptr, std::memory_order_acq_rel);
			if (s) {
				bool sounding = false;
				for (const OuaiveVoice &v : voices)
					sounding = sounding || v.play || v.dc.tailActive();
				if (fadeLen > 0 && sample && sounding) {
					for (OuaiveVoice &v : voices) {
						v.fadeOld[0] = v.dc;
						v.fadeOld[1].reset();
						if (v.play)
							v.fadeOld[1].tail(v.samplePos, v.lastStep, v.lastGain, fadeLen);
					}
					fading = sample;
				}
				else
					retired.store(sample, std::memory_order_release);
				sample = s;
				for (OuaiveVoice &v : voices) {
					v.samplePos = 0.f;
					v.sliceIndex = -1;
					v.play = false;
					v.dc.reset();
				}
			}
		}
	}

	// The old sample is retired once its heads are silent (and the worker freed the one before)
	void finishFading(bool now) {
		if (!fading || retired.load(std::memory_order_acquire))
			return;
		bool silent = true;
		for (const OuaiveVoice &v : voices)
			silent = silent && !v.fadeOld[0].tailActive() && !v.fadeOld[1].tailActive();
		if (now || silent) {
			retired.store(fading, std::memory_order_release);
			fading = nullptr;
			for (OuaiveVoice &v : voices) {
				v.fadeOld[0].reset();
				v.fadeOld[1].reset();
			}
		}
	}

	// Bypassed: still takes the loaded sample, so the loader never waits.
	void processBypass(const ProcessArgs &args) override {
		finishFading(true);
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

	void playVoice(int c, OuaiveVoice &v, const sampleread::View<2> &view, int total, int fadeLen, float sampleTime, float sampleRate, float *out);

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		if (json_t *j = json_object_get(rootJ, "trigMode")) trigMode = clamp((int)json_integer_value(j), 0, 2);
		if (json_t *j = json_object_get(rootJ, "readMode")) readMode = clamp((int)json_integer_value(j), 0, 2);
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		std::string p = json_is_string(lastPathJ) ? json_string_value(lastPathJ) : "";
		if (!p.empty())
			loadFile(p);
		else
			clearFile();   // saved with no sample: not the one loaded before
	}

	void onReset(const ResetEvent &e) override {
		BidooModule::onReset(e);
		trigMode = 0;
		readMode = 0;
	}

	std::string getPath() {
		std::lock_guard<std::mutex> lock(jobMutex);
		return lastPath;
	}

	// A file the loader can read: a RIFF / RF64 / BW64 / Wave64 WAV or an AIFF / AIFC, from its first bytes (a macOS
	// "._" companion file or a misnamed one has the extension but not the header)
	static bool loadable(const std::string &path) {
		FILE *f = std::fopen(path.c_str(), "rb");
		if (!f)
			return false;
		unsigned char h[12] = {};
		const size_t n = std::fread(h, 1, 12, f);
		std::fclose(f);
		if (n < 12)
			return false;
		auto is = [&](int at, const char *tag) { return std::memcmp(h + at, tag, 4) == 0; };
		return ((is(0, "RIFF") || is(0, "RF64") || is(0, "BW64")) && is(8, "WAVE")) || is(0, "riff")
			|| (is(0, "FORM") && (is(8, "AIFF") || is(8, "AIFC")));
	}

	// UI thread: the previous (dir -1) or next (+1) sample the loader can read in the loaded file's folder, by name,
	// round the end
	void loadSibling(int dir) {
		const std::string cur = getPath();
		if (cur.empty())
			return;
		std::vector<std::string> files;
		for (const std::string &f : rack::system::getEntries(rack::system::getDirectory(cur))) {
			const std::string ext = rack::string::lowercase(rack::system::getExtension(f));
			if ((ext == ".wav" || ext == ".aif" || ext == ".aiff") && rack::system::getFilename(f).rfind("._", 0) != 0 && rack::system::isFile(f))
				files.push_back(f);
		}
		if (files.empty())
			return;
		auto key = [](const std::string &f) { return rack::string::lowercase(rack::system::getFilename(f)); };
		std::sort(files.begin(), files.end(), [&](const std::string &a, const std::string &b) { return key(a) < key(b); });
		const std::string curKey = key(cur);
		int i = -1;
		for (int k = 0; k < (int)files.size(); k++)
			if (key(files[k]) == curKey)
				i = k;
		// the next one whose header the loader reads (the header is checked only for the files stepped over)
		const int n = (int)files.size();
		int next = i < 0 ? (dir > 0 ? 0 : n - 1) : ((i + dir) % n + n) % n;
		for (int tries = 0; tries < n && next != i; tries++) {
			if (loadable(files[next])) {
				loadFile(files[next]);
				return;
			}
			next = ((next + dir) % n + n) % n;
		}
	}

	// UI thread: load a file.
	void loadFile(const std::string &path) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			lastPath = path;
		}
		requestLoad(path, engineRate());
	}

	// UI thread: empty the module, through the loader like a load (the worker
	// frees the old sample). Nothing to do when no file was asked for.
	void clearFile() {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			if (lastPath.empty())
				return;
			lastPath.clear();
		}
		requestLoad(std::string(), engineRate());
	}
};

void OUAIVE::process(const ProcessArgs &args) {
	finishFading(false);
	takePending(sampleread::declickLength(args.sampleRate));
	// The engine rate changed: load the file again at the new rate.
	if (sample && sample->count > 0 && sample->sampleRate != args.sampleRate && !loading && !pending.load())
		reloadRate = args.sampleRate;

	const int total = sample ? sample->count : 0;
	const int fadeLen = sampleread::declickLength(args.sampleRate);
	sampleread::View<2> view;
	if (sample)
		view = {(const float *)sample->frames, total, &sample->mip};

	if (trigModeTrigger.process(params[TRIG_MODE_PARAM].getValue()))
		trigMode = (trigMode + 1) % 3;
	if (inputs[READ_MODE_INPUT].isConnected())
		readMode = clamp((int)std::round(inputs[READ_MODE_INPUT].getVoltage() * 0.2f), 0, 2);
	else if (readModeTrigger.process(params[READ_MODE_PARAM].getValue()))
		readMode = (readMode + 1) % 3;
	nbSlices = clamp((int)std::round(params[NB_SLICES_PARAM].getValue() + params[CVSLICES_PARAM].getValue() * inputs[NB_SLICES_INPUT].getVoltage()), 1, 128);
	sliceLength = std::max(total / nbSlices, 1);

	// as many voices as the GATE, POS or V/O cable has channels
	const int n = clamp(std::max(inputs[GATE_INPUT].getChannels(), std::max(inputs[POS_INPUT].getChannels(), inputs[SPEED_INPUT].getChannels())), 1, 16);
	for (int c = n; c < nVoices; c++)
		voices[c].silence();
	nVoices = n;

	// Reset stops playback (the only way to stop a repeating sound) and rewinds
	// the slices so the next trigger, even on this same sample, plays slice 1.
	if (posResetTrigger.process(inputs[POS_RESET_INPUT].getVoltage(), 0.1f, 1.f)) {
		for (OuaiveVoice &v : voices) {
			if (v.play)
				v.dc.tail(v.samplePos, v.lastStep, v.lastGain, fadeLen);
			v.play = false;
			v.sliceIndex = -1;
			v.samplePos = 0.0f;
		}
	}

	// a stereo sample on one output only is heard summed there
	const bool sum = sample && sample->channels >= 2 && !(outputs[OUTL_OUTPUT].isConnected() && outputs[OUTR_OUTPUT].isConnected());
	int nh = 0;
	for (int c = 0; c < n; c++) {
		OuaiveVoice &v = voices[c];
		float out[2];
		playVoice(c, v, view, total, fadeLen, args.sampleTime, args.sampleRate, out);
		float l = out[0], r = out[1];
		if (sum)
			l = r = 0.5f * (l + r);
		outputs[OUTL_OUTPUT].setVoltage(5.0f * l, c);
		outputs[OUTR_OUTPUT].setVoltage(5.0f * r, c);
		if (v.eoc == 0 && v.play)
			v.eoc = 1;
		if (v.eoc == 1 && !v.play) {
			v.eoc = 0;
			v.eocPulse.trigger(1e-3f);
		}
		outputs[EOC_OUTPUT].setVoltage(v.eocPulse.process(args.sampleTime) ? 10.f : 0.f, c);
		if (v.play)
			heads[nh++] = (float)v.samplePos;
	}
	outputs[OUTL_OUTPUT].setChannels(n);
	outputs[OUTR_OUTPUT].setChannels(n);
	outputs[EOC_OUTPUT].setChannels(n);
	headCount.store(nh, std::memory_order_relaxed);
	play = voices[0].play;
	samplePos = voices[0].samplePos;
	sliceIndex = voices[0].sliceIndex;
	speed = voices[0].speed;
}

// One voice, one sample: out gets its stereo frame (5 V = 1)
void OUAIVE::playVoice(int c, OuaiveVoice &v, const sampleread::View<2> &view, int total, int fadeLen, float sampleTime, float sampleRate, float *out) {
	// V/O: exactly 1 V per octave on top of the two tunings; 16x either way is the most the band-limited reading plays
	// cleanly
	float e = clamp(params[SPEED_PARAM].getValue() + params[CVSPEED_PARAM].getValue() + inputs[SPEED_INPUT].getPolyVoltage(c), -4.f, 4.f);
	if (e != v.speedExp) {
		v.speedExp = e;
		// within 6e-6 of exp2 (0.01 cent), exact on whole octaves
		v.speed = dsp::exp2_taylor5(e);
	}
	const float speed = v.speed;

	// Declicked restarts and stops: the old head fades out over 2 ms while
	// the new one fades in.
	auto restart = [&](double pos) {
		if (v.play)
			v.dc.tail(v.samplePos, v.lastStep, v.lastGain, fadeLen);
		v.samplePos = pos;
		v.play = true;
		v.dc.fadeIn(fadeLen);
	};
	auto stop = [&]() {
		if (v.play)
			v.dc.tail(v.samplePos, v.lastStep, v.lastGain, fadeLen);
		v.play = false;
	};

	const bool posOn = inputs[POS_INPUT].isConnected();
	float posCv = clamp(inputs[POS_INPUT].getPolyVoltage(c) * 0.1f, 0.f, 1.f);
	bool trig = v.playTrigger.process(inputs[GATE_INPUT].getPolyVoltage(c), 0.1f, 1.f);
	bool gate = v.playTrigger.isHigh();
	bool scrub = (trigMode == 1) && posOn;
	if (trigMode == 0 && trig) {
		if (posOn)
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
			if (gate && !v.play) {
				v.scrubPos = target;
				restart(target);
			}
			else if (!gate)
				stop();
			if (v.play && std::fabs(target - v.scrubPos) > 0.02f * sampleRate && v.dc.in >= 1.f) {
				// More than 20 ms of the sample away: the smoothing would sweep across it (a harsh scratch); the head
				// jumps there instead, the old one fading out
				v.dc.tail(v.samplePos, v.lastStep, v.lastGain, fadeLen);
				v.scrubPos = target;
				v.dc.fadeIn(fadeLen);
			}
			v.scrubPos += (target - v.scrubPos) * std::min(1.f, 200.f * sampleTime);
			if (v.play)
				v.samplePos = v.scrubPos;
		}
		else if (gate && !v.gateWasHigh)
			restart((readMode != 1) ? 0.0f : total - 1.0f);
		else if (!gate)
			stop();
	}
	else if (trigMode == 2 && trig) {
		if (posOn)
			v.sliceIndex = clamp((int)(posCv * nbSlices), 0, nbSlices - 1);
		else
			v.sliceIndex = (v.sliceIndex + 1) % nbSlices;
		restart(clamp((float)((readMode != 1) ? v.sliceIndex * sliceLength : (v.sliceIndex + 1) * sliceLength - 1), 0.0f, total - 1.0f));
	}
	v.gateWasHigh = gate;

	float acc[2] = {0.f, 0.f};
	bool tailDone = false;
	// The slice count changed under a playing slice: its bounds moved. A head past the new end stops (or wraps, in
	// repeat), one left before the new start jumps to it; both fade over 2 ms instead of cutting
	if (v.play && trigMode == 2 && total > 0 && v.sliceIndex >= 0) {
		const float s0 = v.sliceIndex * sliceLength, s1 = std::min((float)((v.sliceIndex + 1) * sliceLength), (float)total);
		const bool back = readMode == 1;
		const bool past = back ? v.samplePos < s0 - 0.5 : v.samplePos > s1 + 0.5;
		const bool before = back ? v.samplePos > s1 + 0.5 : v.samplePos < s0 - 0.5;
		if (past && readMode == 0) {
			v.dc.tail(v.samplePos, v.lastStep, v.lastGain, fadeLen);
			v.play = false;
		}
		else if (past || before) {
			v.dc.tail(v.samplePos, v.lastStep, v.lastGain, fadeLen);
			v.samplePos = back ? s1 - 1.0 : s0;
			v.dc.fadeIn(fadeLen);
		}
	}
	if (v.play && total > 0 && v.samplePos >= 0 && v.samplePos < total) {
		float step = scrub ? (float)(v.samplePos - v.lastPos) : ((readMode != 1) ? speed : -speed);
		float g = v.dc.nextIn();
		float sliceStart = 0.f, sliceEnd = (float)total;
		if (trigMode == 2) {
			sliceStart = v.sliceIndex * sliceLength;
			sliceEnd = std::min((float)((v.sliceIndex + 1) * sliceLength), (float)total);
		}
		if (!scrub && readMode != 2) {
			// Fades out just before the end it stops at.
			float left = (float)((readMode == 0) ? (sliceEnd - v.samplePos) : (v.samplePos - sliceStart)) / speed;
			if (left < fadeLen)
				g *= std::max(left, 0.f) / fadeLen;
		}
		view.read(v.samplePos, std::fabs(step), acc, v.oct);
		acc[0] *= g;
		acc[1] *= g;
		v.lastGain = g;
		v.lastStep = step;
		// The tail before the jumps below: one started there plays from the
		// next sample.
		v.dc.addTail(view, acc);
		tailDone = true;

		if (!scrub) {
			v.samplePos += step;
			if (trigMode == 2) {
				if ((readMode == 0 && v.samplePos >= sliceEnd) || (readMode == 1 && v.samplePos < sliceStart)) {
					// At its end the gain is already 0 and this does nothing
					v.dc.tail(v.samplePos, step, g, fadeLen);
					v.play = false;
				}
				else if (readMode == 2 && v.samplePos >= sliceEnd) {
					v.dc.tail(v.samplePos, step, g, fadeLen);
					v.samplePos = sliceStart;
					v.dc.fadeIn(fadeLen);
				}
			}
			else {
				if ((readMode == 0 && v.samplePos >= total) || (readMode == 1 && v.samplePos < 0)) {
					v.dc.tail(v.samplePos, step, g, fadeLen);
					v.play = false;
				}
				else if (readMode == 2 && v.samplePos >= total) {
					v.dc.tail(v.samplePos, step, g, fadeLen);
					v.samplePos = posOn ? posCv * (total - 1.0f) : 0.0f;
					v.dc.fadeIn(fadeLen);
				}
			}
		}
	}
	else if (!(trigMode == 1 && posOn && total > 0))
		v.play = false;
	v.lastPos = v.samplePos;
	if (sample && !tailDone)
		v.dc.addTail(view, acc);
	if (fading && fading->count > 1) {
		const sampleread::View<2> old{(const float *)fading->frames, fading->count, &fading->mip};
		v.fadeOld[0].addTail(old, acc);
		v.fadeOld[1].addTail(old, acc);
	}
	out[0] = acc[0];
	out[1] = acc[1];
}

struct OUAIVEDisplay : OpaqueWidget {
	OUAIVE *module;
	// the two waves, 10 px apart, fill the box (setArea)
	float width = 125.0f;
	float height = 50.0f;
	float zoomWidth = 125.0f;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refX = 0.0f;

	void setArea(Rect r) {
		box = r;
		width = zoomWidth = r.size.x;
		height = (r.size.y - 10.f) * 0.5f;
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

	void drawWave(const DrawArgs& args, const OuaiveSample &s, int c, float top, float activeX0, float activeX1) {
		Rect b = Rect(Vec(zoomLeftAnchor, top), Vec(zoomWidth, height));
		nvgScissor(args.vg, 0, b.pos.y, width, height);
		// peaks and RMS core, one column per screen pixel; the part that plays in hot pink, the rest dimmed
		// (src/WaveStyle.hpp)
		const double perUnit = (double)s.count / b.size.x;
		wavestyle::wave(args.vg, width, b.pos.y + b.size.y * 0.5f, b.size.y * 0.5f, activeX0, activeX1, [&](float x0, float x1) {
			return wavestyle::colOfSample((x0 - b.pos.x) * perUnit, (x1 - b.pos.x) * perUnit, s.count, s.overviewMin[c], s.overviewMax[c],
				s.overviewRms[c], OuaiveSample::OVERVIEW, s.frames != nullptr, [&](int64_t j) { return s.frames[j].samples[c]; });
		});
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

				nvgFontSize(args.vg, 12);
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

				nvgTextBox(args.vg, 3, -4, 40, trigMode.c_str(), NULL);
				nvgTextBox(args.vg, 59, -4, 40, slices.c_str(), NULL);

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

				nvgTextBox(args.vg, 40, -4, 40, readMode.c_str(), NULL);

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

				auto px = [&](double frame) { return (float)(frame * zoomWidth / nbSample + zoomLeftAnchor); };
				// what plays: in SLICE mode the slice (shaded as a loop when it repeats), else the whole sample
				const bool sliced = module->trigMode == 2 && module->sliceIndex >= 0;
				const float playX0 = sliced ? px((double)module->sliceIndex * module->sliceLength) : -1.f;
				const float playX1 = sliced ? px(std::min((double)(module->sliceIndex + 1) * module->sliceLength, (double)nbSample)) : width + 1.f;
				nvgSave(args.vg);
				nvgScissor(args.vg, 0, 0, width, 2*height+10);
				if (sliced && module->readMode == 2)
					wavestyle::band(args.vg, playX0, playX1, 0, 2*height+10);
				nvgResetScissor(args.vg);

				//Draw waveform
				drawWave(args, *s, 0, 0, playX0, playX1);
				drawWave(args, *s, 1, height + 10, playX0, playX1);
				nvgResetScissor(args.vg);

				// the slices' cuts, and the play head on top (it lights up the pink under it)
				nvgScissor(args.vg, 0, 0, width, 2*height+10);
				if (module->trigMode == 2)
					for (int i = 1; i < module->nbSlices; i++)
						wavestyle::bar(args.vg, (int)px((double)i * module->sliceLength), 0, 2*height+10);
				// one head per playing voice
				const int nh = module->headCount.load(std::memory_order_relaxed);
				for (int h = 0; h < nh; h++)
					wavestyle::head(args.vg, px(module->heads[h]), 0, 2*height+10);
				nvgResetScissor(args.vg);

				nvgRestore(args.vg);
			}
		}
		Widget::drawLayer(args, layer);
	}

};

// The loaded file's name on the screen, between two arrows that load the previous and next sample of its folder
struct OUAIVEFileBar : OpaqueWidget {
	OUAIVE *module = nullptr;
	static constexpr float kArrow = 13.f;  // the arrows' click zones, at each end

	void onButton(const event::Button &e) override {
		if (module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
			if (e.pos.x < kArrow)
				module->loadSibling(-1);
			else if (e.pos.x > box.size.x - kArrow)
				module->loadSibling(1);
			e.consume(this);
			return;
		}
		OpaqueWidget::onButton(e);
	}

	void drawLayer(const DrawArgs &args, int layer) override {
		if (layer == 1 && module) {
			std::string name;
			{
				std::lock_guard<std::mutex> lock(module->uiMutex);
				name = module->uiSample ? module->waveFileName : "";
			}
			const bool any = !module->getPath().empty();
			const float mid = box.size.y * 0.5f;
			const float fontSize = 11.f;
			nvgFontSize(args.vg, fontSize);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE);
			if (any) {
				// triangles centred on the same line as the name's capitals
				const float h = 3.5f, w = 4.f;
				for (int side = 0; side < 2; side++) {
					const float x = side == 0 ? kArrow * 0.5f : box.size.x - kArrow * 0.5f;
					const float tip = side == 0 ? -w * 0.5f : w * 0.5f;
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, x + tip, mid);
					nvgLineTo(args.vg, x - tip, mid - h);
					nvgLineTo(args.vg, x - tip, mid + h);
					nvgClosePath(args.vg);
					nvgFill(args.vg);
				}
			}
			// the capitals' height is about 0.7 of the font size: a baseline that puts their middle on mid
			const float baseline = mid + 0.35f * fontSize;
			// the name, shortened with an ellipsis to fit between the arrows
			const float room = box.size.x - 2.f * kArrow - 4.f;
			std::string shown = name.empty() ? (any ? "loading..." : "drop a sample here") : name;
			float bounds[4];
			if (nvgTextBounds(args.vg, 0, 0, shown.c_str(), NULL, bounds) > room) {
				while (shown.size() > 1) {
					// drop whole UTF-8 characters
					do
						shown.pop_back();
					while (!shown.empty() && ((unsigned char)shown.back() & 0xC0) == 0x80);
					if (shown.empty())
						break;
					shown.pop_back();
					if (nvgTextBounds(args.vg, 0, 0, (shown + "...").c_str(), NULL, bounds) <= room)
						break;
				}
				shown += "...";
			}
			nvgText(args.vg, box.size.x * 0.5f, baseline, shown.c_str(), NULL);
			nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
		}
		Widget::drawLayer(args, layer);
	}
};

struct OUAIVEWidget : BidooWidget {
	OUAIVEWidget(OUAIVE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/OUAIVE.svg"));

		theme::addScrews(this);

		// The panel of tools/panels/ouaive.py: the old arrangement. The controls keep their old places (specific: the old
		// top left corners, or relative to them); the screen follows the theme, and the bottom jacks go on the grid.
		const float w = box.size.x;

		// Top: position reset in on the left, end of cycle out on the right, beside the title as before. Tiny and
		// without boxes: there is no room for a box between the screws, the title and the screen
		const theme::Pos reset = theme::specificCorner(10, 18, theme::kTinyJackSize);
		const theme::Pos eoc = theme::specificCorner(112, 18, theme::kTinyJackSize);
		addInput(theme::input<TinyPJ301MPort>(reset, module, OUAIVE::POS_RESET_INPUT));
		addOutput(theme::output<TinyPJ301MPort>(eoc, module, OUAIVE::EOC_OUTPUT));

		// Mode buttons, the read mode's CV jack on the right; below, one row each for slices and speed: the trimpot,
		// its CV amount, its CV jack in the read mode CV's column. The CV jacks share a box (25 above the first, 17
		// under the last), over OUT R's column
		const float kCvAmountRight = 24.f;   // from a trimpot to its CV amount
		const float kSpeedBelow = 25.f;      // from the slices row to the speed row, the same in tools/panels/ouaive.py
		const int n = 4;   // the bottom row's columns, N in tools/panels/ouaive.py
		// the CV jacks over OUT R, in its column's box (1.9 px left of their old corner 106), at their old rows
		const theme::Pos readModeCv = theme::colRow(theme::jackAt(n - 1, n, 0, w), theme::specificCorner(106, 222, theme::kTinyJackSize));
		// the slices trimpot 8 px left of its old corner, so its CV amount clears the CV box by 5 px and PLAY MODE above
		// it stays clear of TYPE's label; speed and PLAY MODE follow it (TRIM_LEFT in tools/panels/ouaive.py)
		const float kTrimLeft = 8.f;
		const theme::Pos slices = theme::specificCorner(58.f - kTrimLeft, 250, theme::kTrimpotSize);
		const theme::Pos speed = slices.down(kSpeedBelow);
		// TYPE over the TRIG jack (0.375 px right of the old CKD6's centre), PLAY MODE over the trimpots:
		// BidooRoundButtons, momentary as before, on the read mode CV's row (0.5 px lower)
		const theme::Pos trigMode = theme::colRow(theme::jackAt(0, n, 0, w), readModeCv);
		const theme::Pos readMode = theme::colRow(slices, readModeCv);
		for (const auto &b : {std::make_pair(trigMode, (int)OUAIVE::TRIG_MODE_PARAM), std::make_pair(readMode, (int)OUAIVE::READ_MODE_PARAM)}) {
			BidooRoundButton *button = theme::param<BidooRoundButton>(b.first, module, b.second);
			button->momentary = true;
			addParam(button);
		}
		addInput(theme::input<TinyPJ301MPort>(readModeCv, module, OUAIVE::READ_MODE_INPUT));
		addParam(theme::param<BidooBlueTrimpot>(slices, module, OUAIVE::NB_SLICES_PARAM));
		addParam(theme::param<BidooBlueTrimpot>(slices.right(kCvAmountRight), module, OUAIVE::CVSLICES_PARAM));
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(readModeCv, slices), module, OUAIVE::NB_SLICES_INPUT));   // on the trimpots' row (0.1 px)
		addParam(theme::param<BidooBlueTrimpot>(speed, module, OUAIVE::SPEED_PARAM));
		addParam(theme::param<BidooBlueTrimpot>(speed.right(kCvAmountRight), module, OUAIVE::CVSPEED_PARAM));
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(readModeCv, speed), module, OUAIVE::SPEED_INPUT));

		// The screen: the theme's top and margins, down to 4 px above the CV box (SCREEN_BOTTOM in tools/panels/ouaive.py).
		// Inside it, as before: the file bar at the top, the mode line, then the two waves
		const float kScreenBottom = readModeCv.y - theme::kBoxAboveJack - theme::kBoxGap;
		const theme::Area screen = theme::specificRect(theme::kSideMargin, theme::kScreenTop, w - 2.f * theme::kSideMargin, kScreenBottom - theme::kScreenTop);
		const float kInset = 1.f;         // inside the screen's frame
		const float kFileBarTop = 3.f;    // from the screen's top
		const float kFileBarH = 14.f;
		const float kModeLineH = 15.f;    // the mode line, drawn by the display above its box
		const float kBottomRoom = 8.f;
		OUAIVEFileBar *fileBar = new OUAIVEFileBar();
		fileBar->module = module;
		fileBar->box = theme::specificRect(screen.x, screen.y + kFileBarTop, screen.w, kFileBarH);
		addChild(fileBar);
		OUAIVEDisplay *display = new OUAIVEDisplay();
		display->module = module;
		const float waveTop = screen.y + kFileBarTop + kFileBarH + kModeLineH;
		display->setArea(theme::specificRect(screen.x + kInset, waveTop, screen.w - 2.f * kInset, screen.y + screen.h - kBottomRoom - waveTop));
		addChild(display);

		// Bottom row, on the grid of four columns: gate and position in, the stereo out over the last two (full jacks:
		// they fit in its box)
		addInput(theme::input<PJ301MPort>(theme::jackAt(0, n, 0, w), module, OUAIVE::GATE_INPUT));
		addInput(theme::input<PJ301MPort>(theme::jackAt(1, n, 0, w), module, OUAIVE::POS_INPUT));
		addOutput(theme::output<PJ301MPort>(theme::jackAt(2, n, 0, w), module, OUAIVE::OUTL_OUTPUT));
		addOutput(theme::output<PJ301MPort>(theme::jackAt(3, n, 0, w), module, OUAIVE::OUTR_OUTPUT));
	}

  struct OUAIVEItem : MenuItem {
  	OUAIVE *module;
  	void onAction(const event::Action &e) override {

  		// the last folder a sample came from, kept between openings and sessions (plugin.hpp)
  		std::string dir = sampleDialogDir(module->getPath());
  		char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
  		if (path) {
  			rememberSampleFolder(path);
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
		if (module && !e.paths.empty()) {
			rememberSampleFolder(e.paths[0]);
			module->loadFile(e.paths[0]);
		}
	}
};

Model *modelOUAIVE = createModel<OUAIVE, OUAIVEWidget>("OUAIve");
