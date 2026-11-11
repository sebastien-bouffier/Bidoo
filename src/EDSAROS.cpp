/*
==============================================================================
Based on Laurent de Soras resampler for pitch shifting
==============================================================================
*/


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

#include	"dep/resampler/def.h"
#include	"dep/resampler/Downsampler2Flt.h"
#include	"dep/resampler/fnc.h"
#include	"dep/resampler/Fixed3232.h"
#include	"dep/resampler/Int16.h"
#include	"dep/resampler/Int64.h"
#include	"dep/resampler/InterpFlt.h"
#include	"dep/resampler/InterpPack.h"
#include	"dep/resampler/MipMapFlt.h"
#include	"dep/resampler/ResamplerFlt.h"
#include "SampleRead.hpp"

using namespace std;

static const int EDS_BLOCK = 16;

// Linear up to 8 V, then bends to stay under 10 V (9.95 V at most).
inline float edsSoftLimit(float x) {
	float a = std::fabs(x);
	if (a <= 8.0f)
		return x;
	return std::copysign(8.0f + 1.95f * std::tanh((a - 8.0f) * 0.5f), x);
}

// A loaded sample: forward and reversed mipmaps for the resampler, the zero
// crossings for snapping, and an overview for the display. Built by the
// worker thread, then owned by the audio thread.
struct EdsarosSample {
	static constexpr int OVERVIEW = 1024;
	int count = 0;
	float sampleRate = 0.f;
	std::vector<float> data;
	std::vector<int> nextZero, prevZero;  // the nearest zero crossing at or after / at or before each index
	rspl::MipMapFlt mip, revMip;
	float overviewMin[OVERVIEW] = {};
	float overviewMax[OVERVIEW] = {};

	bool build(const std::vector<dsp::Frame<1>> &frames) {
		count = frames.size();
		if (count < 2)
			return false;
		data.resize(count);
		for (int i = 0; i < count; i++)
			data[i] = frames[i].samples[0];
		// Doubled, as before, so the interpolator can read past the end.
		std::vector<float> fwd(2 * count), rev(2 * count);
		for (int i = 0; i < count; i++) {
			fwd[i] = fwd[i + count] = data[i];
			rev[i] = rev[i + count] = data[count - i - 1];
		}
		mip.init_sample(2 * count, rspl::InterpPack::get_len_pre(), rspl::InterpPack::get_len_post(), 12, rspl::ResamplerFlt::_fir_mip_map_coef_arr, rspl::ResamplerFlt::MIP_MAP_FIR_LEN);
		mip.fill_sample(fwd.data(), 2 * count);
		revMip.init_sample(2 * count, rspl::InterpPack::get_len_pre(), rspl::InterpPack::get_len_post(), 12, rspl::ResamplerFlt::_fir_mip_map_coef_arr, rspl::ResamplerFlt::MIP_MAP_FIR_LEN);
		revMip.fill_sample(rev.data(), 2 * count);

		nextZero.resize(count);
		prevZero.resize(count);
		auto crossing = [&](int i) { return i >= count - 1 || data[i] * data[i + 1] <= 0.f; };
		int next = count - 1;
		for (int i = count - 1; i >= 0; i--) {
			if (crossing(i))
				next = i;
			nextZero[i] = next;
		}
		int prev = 0;
		for (int i = 0; i < count; i++) {
			if (crossing(i))
				prev = i;
			prevZero[i] = prev;
		}

		for (int b = 0; b < OVERVIEW; b++) {
			int i0 = (int)((long long)b * count / OVERVIEW), i1 = std::max((int)((long long)(b + 1) * count / OVERVIEW), i0 + 1);
			float lo = 0.f, hi = 0.f;
			for (int i = i0; i < i1 && i < count; i++) {
				lo = std::min(lo, data[i]);
				hi = std::max(hi, data[i]);
			}
			overviewMin[b] = lo;
			overviewMax[b] = hi;
		}
		return true;
	}
};

// What the display draws, copied by the worker.
struct EdsarosOverview {
	int count = 0;
	float overviewMin[EdsarosSample::OVERVIEW] = {};
	float overviewMax[EdsarosSample::OVERVIEW] = {};
	std::vector<float> data;   // the sample itself, for the display zoomed in
};

struct EDSAROS : BidooModule {
	enum ParamIds {
		SAMPLESTART_PARAM,
		SAMPLEEND_PARAM,
		LOOPSTART_PARAM,
		LOOPEND_PARAM,
		LOOPCROSSFADE_PARAM, // unused (no crossfade control): kept so saved patches keep the ids after it
		RELEASESTART_PARAM,
		RELEASECROSSFADE_PARAM, // unused, kept for the ids
		LOOPMODE_PARAM,
		RELEASEMODE_PARAM,
		ATTACK_PARAM,
		DECAY_PARAM,
		RELEASE_PARAM,
		INIT_PARAM,
		PEAK_PARAM,
		SUSTAIN_PARAM,
		ATTACKSLOPE_PARAM,
		DECAYSLOPE_PARAM,
		RELEASESLOPE_PARAM,
		LINKTYPE_PARAM,
		GAIN_PARAM,
		ZEROCROSSING_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		TRIG_INPUT,
		PITCH_INPUT,
		SAMPLESTART_INPUT,
		SAMPLEEND_INPUT,
		LOOPSTART_INPUT,
		LOOPEND_INPUT,
		LOOPCROSSFADE_INPUT, // unused (no jack): kept so saved cables keep their ports
		RELEASESTART_INPUT,
		RELEASECROSSFADE_INPUT, // unused, kept for the ids
		ATTACK_INPUT,
		DECAY_INPUT,
		RELEASE_INPUT,
		INIT_INPUT,
		PEAK_INPUT,
		SUSTAIN_INPUT,
		ATTACKSLOPE_INPUT,
		DECAYSLOPE_INPUT,
		RELEASESLOPE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ZEROCROSSING_LIGHT,
		NUM_LIGHTS
	};

	std::string lastPath;
	std::string waveFileName;
	rspl::InterpPack interp_pack;
	rspl::ResamplerFlt voices[16];
	rspl::ResamplerFlt rev_voices[16];

	// Audio thread: the sample it plays. The worker loads new ones into
	// `pending`; the audio thread takes it and moves its old one to `retired`,
	// which the worker frees.
	EdsarosSample *smp = nullptr;
	std::atomic<EdsarosSample*> pending{nullptr};
	std::atomic<EdsarosSample*> retired{nullptr};
	int totalSampleCount = 0;

	// Worker thread: loads the file asked for, at the rate asked for.
	std::thread worker;
	std::mutex jobMutex;
	std::condition_variable jobCv;
	std::string jobPath;
	float jobRate = 0.f;
	bool jobRequested = false;
	std::atomic<bool> quit{false};
	std::atomic<float> reloadRate{0.f};
	std::atomic<bool> loading{false};

	// UI thread: the overview of the sample, copied by the worker.
	std::mutex uiMutex;
	std::shared_ptr<EdsarosOverview> uiOverview;

	dsp::DoubleRingBuffer<float,EDS_BLOCK> audio[16];
	bool play[16] = {false};
	bool feed[16] = {false};
	int internalIntegerPosition[16] = {0};
	int internalIntegerRevPosition[16] = {0};
	const long depth = 1L << rspl::ResamplerFlt::NBR_BITS_PER_OCT;
	int sampleStart=0;
	int sampleEnd=0;
	int loopStart=0;
	int loopEnd=0;
	int releaseStart=0;
	bool showSample = true;
	float attack = 0.0f;
	float decay = 0.0f;
	float release = 0.0f;
	float sustain = 0.0f;
	float attackSlope = 0.0f;
	float decaySlope = 0.0f;
	float releaseSlope = 0.0f;
	float init = 0.0f;
	float peak = 0.0f;
	float voiceTime[16] = {0.0f};
	bool rel[16] = {false};
	float gain[16] = {0.0f};
	float releaseFrom[16] = {0.0f};
	int direction[16] = {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1};
	bool zeroCrossing = false;
	// Trigger mode: a rising edge plays the attack and the decay whatever the
	// gate length, then the release. Off: the gate holds the sustain.
	bool triggerMode = false;
	bool oneShot[16] = {false};
	// The voice reached S END with no loop: silent until the gate falls or a
	// new trigger, rather than starting again.
	bool ended[16] = {false};
	// Declicking: each jump of a play head (loop, ping-pong turn, release
	// loop, restart, Cut) and each sudden stop lets the old head go on for
	// 2 ms, fading out, while the new one fades in.
	sampleread::Declick<1> dc[16];
	float lastLevel[16] = {0.f};    // the gain of the head in the last output sample
	float voiceRatio[16] = {1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f, 1.f};
	float attackFrom[16] = {0.f};   // where the attack starts: init, or the level a retrigger caught
	int fadeLen = 96;
	// A sample replaced while voices sound: kept until each voice's old heads (its tail, its playing head) have faded
	// out from it, then retired
	EdsarosSample *fading = nullptr;
	sampleread::Declick<1> fadeOld[16][2];
	dsp::SchmittTrigger zeroCrossingTrigger;
	dsp::SchmittTrigger trigTriggers[16];
	int lastChannels = 0;

	EDSAROS() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SAMPLESTART_PARAM, 0.0f, 10.0f, 0.0f, "Sample start", "%", 0.f, 10.f);
		configParam(LOOPSTART_PARAM, 0.0f, 10.0f, 0.0f, "Loop start", "%", 0.f, 10.f);
		configParam(LOOPEND_PARAM, 0.0f, 10.0f, 10.0f, "Loop end", "%", 0.f, 10.f);
		configParam(SAMPLEEND_PARAM, 0.0f, 10.0f, 10.0f, "Sample end", "%", 0.f, 10.f);
		configParam(RELEASESTART_PARAM, 0.0f, 10.0f, 10.0f, "Release start", "%", 0.f, 10.f);

		configSwitch(LOOPMODE_PARAM, 0.0f, 2.0f, 0.0f, "Loop mode", {"None", "Forward", "Ping-pong"});
		configSwitch(RELEASEMODE_PARAM, 0.0f, 3.0f, 1.0f, "Release mode", {"Cut", "Play to the end", "Forward loop", "Ping-pong loop"});
		configSwitch(LINKTYPE_PARAM, 0.0f, 3.0f, 0.0f, "Link points mode", {"None", "Offset", "Offset, widening", "Offset, narrowing"});

		configParam(ATTACK_PARAM, 0.0f, 20.0f, 0.0f, "Attack time", " s");
		configParam(DECAY_PARAM, 0.0f, 60.0f, 0.2f, "Decay time", " s");
		configParam(RELEASE_PARAM, 0.0f, 60.0f, 0.5f, "Release time", " s");
		configParam(INIT_PARAM, 0.0f, 1.0f, 0.0f, "Init gain", "%", 0.f, 100.f);
		configParam(PEAK_PARAM, 0.0f, 1.0f, 1.0f, "Peak gain", "%", 0.f, 100.f);
		configParam(SUSTAIN_PARAM, 0.0f, 1.0f, 1.0f, "Sustain gain", "%", 0.f, 100.f);
		configParam(ATTACKSLOPE_PARAM, 0.0f, 1.0f, 0.5f, "Attack slope", "%", 0.f, 100.f);
		configParam(DECAYSLOPE_PARAM, 0.0f, 1.0f, 0.5f, "Decay slope", "%", 0.f, 100.f);
		configParam(RELEASESLOPE_PARAM, 0.0f, 1.0f, 0.5f, "Release slope", "%", 0.f, 100.f);

		configParam(GAIN_PARAM, 1.0f, 10.0f, 1.0f, "Sample gain", "x");
		configButton(ZEROCROSSING_PARAM, "Snap the points to zero crossings");

		configInput(TRIG_INPUT, "Gate (polyphonic)");
		configInput(PITCH_INPUT, "Pitch (1V/oct, 0 V = original, polyphonic)");
		configInput(SAMPLESTART_INPUT, "Sample start CV");
		configInput(SAMPLEEND_INPUT, "Sample end CV");
		configInput(LOOPSTART_INPUT, "Loop start CV");
		configInput(LOOPEND_INPUT, "Loop end CV");
		configInput(RELEASESTART_INPUT, "Release start CV");
		configInput(ATTACK_INPUT, "Attack CV (10 V = 20 s)");
		configInput(DECAY_INPUT, "Decay CV (10 V = 60 s)");
		configInput(RELEASE_INPUT, "Release CV (10 V = 60 s)");
		configInput(INIT_INPUT, "Init gain CV (10 V = 100%)");
		configInput(PEAK_INPUT, "Peak gain CV (10 V = 100%)");
		configInput(SUSTAIN_INPUT, "Sustain gain CV (10 V = 100%)");
		configInput(ATTACKSLOPE_INPUT, "Attack slope CV");
		configInput(DECAYSLOPE_INPUT, "Decay slope CV");
		configInput(RELEASESLOPE_INPUT, "Release slope CV");
		configOutput(OUT, "Audio (polyphonic)");

		worker = std::thread(&EDSAROS::workerLoop, this);
	}

	~EDSAROS() {
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
		delete smp;
	}

	float engineRate() {
		return (APP && APP->engine) ? APP->engine->getSampleRate() : 48000.f;
	}

	// UI thread: load a file.
	void loadFile(const std::string &path) {
		{
			std::lock_guard<std::mutex> lock(jobMutex);
			lastPath = path;
			jobPath = path;
			jobRate = engineRate();
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

			std::string name, ext;
			int ch = 0, sr = 0, cnt = 0;
			std::vector<dsp::Frame<1>> frames = waves::getMonoWav(path, rate, name, ext, ch, sr, cnt);
			EdsarosSample *s = new EdsarosSample();
			s->sampleRate = rate;
			if (s->build(frames)) {
				auto o = std::make_shared<EdsarosOverview>();
				o->count = s->count;
				o->data = s->data;
				std::copy(s->overviewMin, s->overviewMin + EdsarosSample::OVERVIEW, o->overviewMin);
				std::copy(s->overviewMax, s->overviewMax + EdsarosSample::OVERVIEW, o->overviewMax);
				{
					std::lock_guard<std::mutex> ui(uiMutex);
					uiOverview = o;
					waveFileName = name;
				}
				// Wait until the audio thread has taken the previous one.
				while (pending.load() && !quit)
					std::this_thread::sleep_for(std::chrono::milliseconds(5));
				delete pending.exchange(s, std::memory_order_acq_rel);
			}
			else
				delete s;
			loading = false;
			lock.lock();
		}
	}

	void process(const ProcessArgs &args) override;

	// Takes a newly loaded sample, once the worker has freed the previous one.
	void takePending(bool fade) {
		if (pending.load(std::memory_order_relaxed) && !retired.load(std::memory_order_acquire) && !fading) {
			EdsarosSample *s = pending.exchange(nullptr, std::memory_order_acq_rel);
			if (s) {
				// The voices that sound fade out from the old sample (from where their heads are, read before the
				// resamplers move to the new one) instead of stopping dead
				bool any = false;
				for (int i = 0; fade && smp && i < lastChannels; i++) {   // the voices the output loop runs (the others are silent)
					fadeOld[i][0].reset();
					fadeOld[i][1].reset();
					if (dc[i].tailActive()) {
						fadeOld[i][0] = dc[i];
						any = true;
					}
					if (lastLevel[i] > 0.f) {
						double pos;
						float step;
						headNow(i, pos, step);
						fadeOld[i][1].tail(pos, step, lastLevel[i], fadeLen);
						any = true;
					}
				}
				if (any)
					fading = smp;
				else
					retired.store(smp, std::memory_order_release);
				smp = s;
				totalSampleCount = s->count;
				for (int i=0; i<16; i++) {
					voices[i].set_sample (smp->mip);
					voices[i].set_interp (interp_pack);
					voices[i].clear_buffers ();
					rev_voices[i].set_sample (smp->revMip);
					rev_voices[i].set_interp (interp_pack);
					rev_voices[i].clear_buffers ();
					play[i] = rel[i] = ended[i] = false;
					gain[i] = 0.f;
					audio[i].clear();
					direction[i] = 1;
					dc[i].reset();
					lastLevel[i] = 0.f;
				}
			}
		}
	}

	// The old sample is retired once every voice's old heads are silent (and the worker freed the one before)
	void finishFading(bool now) {
		if (!fading || retired.load(std::memory_order_acquire))
			return;
		if (!now)
			for (int i = 0; i < 16; i++)
				if (fadeOld[i][0].tailActive() || fadeOld[i][1].tailActive())
					return;
		retired.store(fading, std::memory_order_release);
		fading = nullptr;
		for (int i = 0; i < 16; i++) {
			fadeOld[i][0].reset();
			fadeOld[i][1].reset();
		}
	}

	// Bypassed: still takes the loaded sample, so the loader never waits.
	void processBypass(const ProcessArgs &args) override {
		finishFading(true);
		takePending(false);
		Module::processBypass(args);
	}

	// Where the next output sample of voice i reads (in frames of the sample)
	// and how far it moves per sample: the resampler's head, less what is
	// still queued.
	void headNow(int i, double &pos, float &step) {
		const double scale = 1.0 / 4294967296.0;
		// The resampler's output lags its head by about 1.1 output samples
		// (measured at -1, 0, +0.5 and +1 octave).
		const double lag = 1.1;
		double queued = ((double)audio[i].size() + lag) * voiceRatio[i];
		if (direction[i] == 1) {
			pos = (double)voices[i].get_playback_pos() * scale - queued;
			step = voiceRatio[i];
		}
		else {
			pos = (double)(totalSampleCount - 1) - ((double)rev_voices[i].get_playback_pos() * scale - queued);
			step = -voiceRatio[i];
		}
		// The tables hold the sample twice: past the end is the start again.
		if (totalSampleCount > 0) {
			pos = std::fmod(pos, (double)totalSampleCount);
			if (pos < 0.0)
				pos += totalSampleCount;
		}
	}

	// The head of voice i jumps or stops now: its old place goes on as a
	// fading tail.
	void tailFrom(int i) {
		double pos;
		float step;
		headNow(i, pos, step);
		dc[i].tail(pos, step, lastLevel[i], fadeLen);
	}

	// A jump: the old place fades out, the new one fades in.
	void jump(int i) {
		tailFrom(i);
		dc[i].fadeIn(fadeLen);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		json_object_set_new(rootJ, "zeroCrossing", json_boolean(zeroCrossing));
		json_object_set_new(rootJ, "triggerMode", json_boolean(triggerMode));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *zeroCrossingJ = json_object_get(rootJ, "zeroCrossing");
		if (zeroCrossingJ) zeroCrossing = json_is_true(zeroCrossingJ);
		json_t *triggerModeJ = json_object_get(rootJ, "triggerMode");
		triggerMode = triggerModeJ && json_is_true(triggerModeJ);
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ) {
			std::string p = json_string_value(lastPathJ);
			if (!p.empty())
				loadFile(p);
		}
	}

	float getXBez(const float t, const float x1, const float x2, const float x3) {
		float t2 = t * t;
		float t3 = t * t * t;

		float A = (3*t2 - 3*t3);
		float B = (3*t3 - 6*t2 + 3*t);
		float C = (3*t2 - t3 - 3*t + 1);

		return t3*x3 + (A+B)*x2 + C*x1;
	}

	float getYBez(const float t, const float y1, const float y2, const float y3) {
		float t2 = t * t;
		float t3 = t * t * t;

		float A = (3*t2 - 3*t3);
		float B = (3*t3 - 6*t2 + 3*t);
		float C = (3*t2 - t3 - 3*t + 1);

		return t3*y3 + (A+B)*y2 + C*y1;
	}

	// The release goes from `from`, the level when the gate fell, to 0. The
	// attack goes from `start` (init, or the level a retrigger caught) to peak.
	float getEnv(const float t, const bool r, const float from, const float start) {
		if (attack>0 && t>=0.0f && t<=attack && !r) {
			return getYBez(t/attack,start,attackSlope*(peak-start)+start,peak);
		}
		else if (decay>0 && t>attack && t<=(attack+decay) && !r) {
			return getYBez((t-attack)/decay,peak,decaySlope*(sustain-peak)+peak,sustain);
		}
		else if (r && release>0 && t<=release) {
			return getYBez(t/release,from,releaseSlope*from,0.0f);
		}
		else if (r) {
			// Past the release, or no release (0 s): silent (the declicker
			// fades the last 2 ms).
			return 0.0f;
		}
		return sustain;
	}

	float getEnv(const float t, const bool r) {
		return getEnv(t, r, sustain, init);
	}

	int getSnappedIndex(float p, bool forward) {
		int idx = clamp((int)(p*(totalSampleCount-1)*0.1f), 0, std::max(totalSampleCount-1, 0));
		if (!zeroCrossing || !smp)
			return idx;
		return forward ? smp->nextZero[idx] : smp->prevZero[idx];
	}

	int revIndex(const int i) {
		return totalSampleCount-i-1;
	}

	void updatePoints() {
		if (totalSampleCount <= 0)
			return;
		float start = params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage();
		float pEnd = params[SAMPLEEND_PARAM].getValue()+inputs[SAMPLEEND_INPUT].getVoltage();
		float pLoopStart = params[LOOPSTART_PARAM].getValue()+inputs[LOOPSTART_INPUT].getVoltage();
		float pLoopEnd = params[LOOPEND_PARAM].getValue()+inputs[LOOPEND_INPUT].getVoltage();
		float pRelease = params[RELEASESTART_PARAM].getValue()+inputs[RELEASESTART_INPUT].getVoltage();
		sampleStart = getSnappedIndex(clamp(start,0.0f,10.0f), true);
		// Link modes: the other points follow the start. Offset moves them by
		// the start value; widening moves them further (start x (1 + position)),
		// so the region grows as the start moves later; narrowing moves them
		// less (start x (1 - position)), so the region shrinks.
		float offset = 0.f;
		int link = (int)params[LINKTYPE_PARAM].getValue();
		float position = (float)sampleStart/(float)totalSampleCount;
		if (link == 1)
			offset = start;
		else if (link == 2)
			offset = start * (1+position);
		else if (link == 3)
			offset = start * (1-position);
		sampleEnd = std::max(getSnappedIndex(clamp(pEnd+offset,0.0f,10.0f), false), sampleStart);
		loopStart = std::min(std::max(getSnappedIndex(clamp(pLoopStart+offset,0.0f,10.0f), true), sampleStart), sampleEnd);
		loopEnd = std::min(std::max(getSnappedIndex(clamp(pLoopEnd+offset,0.0f,10.0f), false), loopStart), sampleEnd);
		releaseStart = std::min(std::max(getSnappedIndex(clamp(pRelease+offset,0.0f,10.0f), true), sampleStart), sampleEnd);
	}

	void setPos(int i, int fwdPos, int revPos) {
		voices[i].set_playback_pos(static_cast <rspl::Int64> (fwdPos) << 32);
		rev_voices[i].set_playback_pos(static_cast <rspl::Int64> (revPos) << 32);
	}
};

void EDSAROS::process(const ProcessArgs &args) {
	fadeLen = sampleread::declickLength(args.sampleRate);
	finishFading(false);
	takePending(true);
	// The engine rate changed: load the file again at the new rate.
	if (smp && smp->sampleRate != args.sampleRate && !loading)
		reloadRate = args.sampleRate;

	if (zeroCrossingTrigger.process(params[ZEROCROSSING_PARAM].getValue()))
		zeroCrossing=!zeroCrossing;
	lights[ZEROCROSSING_LIGHT].setBrightness(zeroCrossing?1:0);

	attack = clamp(params[ATTACK_PARAM].getValue()+inputs[ATTACK_INPUT].getVoltage()*2.0f,0.0f,20.0f);
	decay = clamp(params[DECAY_PARAM].getValue()+inputs[DECAY_INPUT].getVoltage()*6.0f,0.0f,60.0f);
	release = clamp(params[RELEASE_PARAM].getValue()+inputs[RELEASE_INPUT].getVoltage()*6.0f,0.0f,60.0f);
	// The levels glide: a jump of INIT, PEAK or SUSTAIN (knob or CV) takes 2 ms instead of stepping the voices' gain
	const float levelStep = 1.f / (float)fadeLen;
	auto glide = [&](float &v, float target) { v += clamp(target - v, -levelStep, levelStep); };
	glide(init, clamp(params[INIT_PARAM].getValue()+inputs[INIT_INPUT].getVoltage()*0.1f,0.0f,1.0f));
	glide(peak, clamp(params[PEAK_PARAM].getValue()+inputs[PEAK_INPUT].getVoltage()*0.1f,0.0f,1.0f));
	glide(sustain, clamp(params[SUSTAIN_PARAM].getValue()+inputs[SUSTAIN_INPUT].getVoltage()*0.1f,0.0f,1.0f));
	attackSlope = clamp(params[ATTACKSLOPE_PARAM].getValue()+inputs[ATTACKSLOPE_INPUT].getVoltage()*0.1f,-1.0f,1.0f);
	decaySlope = clamp(params[DECAYSLOPE_PARAM].getValue()+inputs[DECAYSLOPE_INPUT].getVoltage()*0.1f,-1.0f,1.0f);
	releaseSlope = clamp(params[RELEASESLOPE_PARAM].getValue()+inputs[RELEASESLOPE_INPUT].getVoltage()*0.1f,-1.0f,1.0f);

	updatePoints();

	// One voice per channel of the pitch or gate input, whichever has more.
	int channels = std::max(std::max(inputs[PITCH_INPUT].getChannels(), inputs[TRIG_INPUT].getChannels()), 1);
	for (int i = channels; i < lastChannels; i++) {
		play[i] = rel[i] = ended[i] = false;
		gain[i] = 0.f;
		audio[i].clear();
		dc[i].reset();
		lastLevel[i] = 0.f;
	}
	lastChannels = channels;
	const int loopMode = (int)params[LOOPMODE_PARAM].getValue();
	const int releaseMode = (int)params[RELEASEMODE_PARAM].getValue();
	const float sampleGain = params[GAIN_PARAM].getValue();

	for (int i=0; i<channels; i++) {
		if (!smp) {
			outputs[OUT].setVoltage(0.0f, i);
			continue;
		}
		bool rise = trigTriggers[i].process(inputs[TRIG_INPUT].getPolyVoltage(i), 0.1f, 1.f);
		bool gateHigh = trigTriggers[i].isHigh();
		if (triggerMode) {
			// Each rising edge starts the voice again from the top.
			if (rise) {
				play[i] = false;
				oneShot[i] = true;
				ended[i] = false;
			}
			gateHigh = oneShot[i] && !ended[i] && (!play[i] || voiceTime[i] < attack + decay);
			if (!gateHigh)
				oneShot[i] = false;
		}
		if (gateHigh) {
			if (!ended[i]) {
				if (!play[i]) {
					// A retrigger of a sounding voice (in its release, or in
					// trigger mode) starts the attack from
					// where its level is, and crossfades from the old place.
					attackFrom[i] = (lastLevel[i] > 0.f) ? gain[i] : init;
					if (lastLevel[i] > 0.f)
						tailFrom(i);
					dc[i].fadeIn(fadeLen);
					voiceTime[i]=0.0f;
					rel[i]=false;
					setPos(i, sampleStart, revIndex(sampleStart));
					direction[i]=1;
					audio[i].clear();
				}
				play[i]=true;
			}
		}
		else {
			ended[i] = false;
			if (play[i]) {
				voiceTime[i]=0.0f;
				rel[i]=true;
				releaseFrom[i]=gain[i];
				direction[i]=1;
			}
			play[i]= false;
			if (gain[i]==0.0f) {
				rel[i]=false;
				setPos(i, sampleStart, revIndex(sampleStart));
				direction[i]=1;
			}
		}

		gain[i] = (play[i] || rel[i]) ? getEnv(voiceTime[i], rel[i], releaseFrom[i], attackFrom[i]) : 0.0f;
		bool tailed = false;  // a tail started this sample

		if (audio[i].size()==0) { feed[i] = true;}

		internalIntegerPosition[i] = voices[i].get_playback_pos() >> 32;
		internalIntegerRevPosition[i] = rev_voices[i].get_playback_pos() >> 32;

		// A voice going backward when the mode no longer has a backward leg
		// (the loop or release mode was changed) turns forward where it is.
		if (direction[i] == -1 && !((play[i] && loopMode == 2) || (rel[i] && releaseMode == 3))) {
			int at = clamp(revIndex(internalIntegerRevPosition[i]), 0, totalSampleCount - 1);
			if (feed[i]) {
				jump(i);
				tailed = true;
			}
			voices[i].set_playback_pos(static_cast <rspl::Int64> (at) << 32);
			internalIntegerPosition[i] = at;
			direction[i] = 1;
		}

		if ((play[i] || rel[i]) && feed[i]) {
			// The loop mode drives the held phase, the release mode the release.
			// Every jump below is crossfaded over 2 ms (jump / tailFrom); a
			// stop (end, Cut) fades its last 2 ms.
			if (loopMode==0 && internalIntegerPosition[i]>=sampleEnd && play[i]) {
				tailFrom(i);
				tailed = true;
				play[i]=false;
				ended[i]=true;
				setPos(i, sampleStart, revIndex(sampleStart));
			}
			else if (loopMode==1 && internalIntegerPosition[i]>=loopEnd && play[i]) {
				jump(i);
				tailed = true;
				voices[i].set_playback_pos(static_cast <rspl::Int64> (loopStart) << 32);
			}
			else if (loopMode==2 && direction[i]==1 && internalIntegerPosition[i]>=loopEnd && play[i]) {
				jump(i);
				tailed = true;
				setPos(i, loopStart, revIndex(loopEnd));
				direction[i]=-1;
			}
			else if (loopMode==2 && direction[i]==-1 && internalIntegerRevPosition[i]>=revIndex(loopStart) && play[i]) {
				jump(i);
				tailed = true;
				setPos(i, loopStart, revIndex(loopEnd));
				direction[i]=1;
			}

			if (releaseMode==0 && !play[i]) {
				// Cut: the head stops, its last 2 ms fade.
				if (rel[i] && !tailed) {
					tailFrom(i);
					tailed = true;
				}
				voices[i].set_playback_pos(static_cast <rspl::Int64> (sampleStart) << 32);
				rel[i]=false;
			}
			else if (releaseMode==1 && internalIntegerPosition[i]>=sampleEnd && rel[i]) {
				tailFrom(i);
				tailed = true;
				voices[i].set_playback_pos(static_cast <rspl::Int64> (sampleStart) << 32);
				rel[i]=false;
			}
			else if (releaseMode==2 && internalIntegerPosition[i]>=sampleEnd && rel[i]) {
				jump(i);
				tailed = true;
				voices[i].set_playback_pos(static_cast <rspl::Int64> (releaseStart) << 32);
			}
			else if (releaseMode==3 && direction[i]==1 && internalIntegerPosition[i]>=sampleEnd && rel[i]) {
				jump(i);
				tailed = true;
				setPos(i, releaseStart, revIndex(sampleEnd));
				direction[i]=-1;
			}
			else if (releaseMode==3 && direction[i]==-1 && internalIntegerRevPosition[i]>=revIndex(releaseStart) && rel[i]) {
				jump(i);
				tailed = true;
				setPos(i, releaseStart, revIndex(sampleEnd));
				direction[i]=1;
			}

			if (play[i] || rel[i]) {
				const float pitchV = clamp(inputs[PITCH_INPUT].getPolyVoltage(i), -10.f, 10.f);
				const long pitch = pitchV * depth;
				voices[i].set_pitch(pitch);
				rev_voices[i].set_pitch(pitch);

				// Output samples until the voice reaches `dist` input samples
				// further, at this pitch.
				const double ratio = std::exp2((double)pitchV);
				voiceRatio[i] = (float)ratio;
				auto outLen = [&](long dist) -> long {
					if (dist <= 0)
						return 0;
					return std::min((long)EDS_BLOCK, std::max(1L, (long)std::ceil(dist / ratio)));
				};
				long nbr_spl;
				if (direction[i]==1) {
					if (play[i] && loopMode==0)
						nbr_spl = outLen(sampleEnd - internalIntegerPosition[i]);
					else if (play[i] && loopMode>0)
						nbr_spl = outLen(loopEnd - internalIntegerPosition[i]);
					else if (rel[i] && releaseMode>0)
						nbr_spl = outLen(sampleEnd - internalIntegerPosition[i]);
					else
						nbr_spl = EDS_BLOCK;
				}
				else {
					if (play[i] && loopMode==2)
						nbr_spl = outLen(revIndex(loopStart) - internalIntegerRevPosition[i]);
					else if (rel[i] && releaseMode>0)
						nbr_spl = outLen(revIndex(releaseStart) - internalIntegerRevPosition[i]);
					else
						nbr_spl = EDS_BLOCK;
				}
				// Never read past the tables (the sample is stored twice).
				{
					long pos = (direction[i]==1) ? internalIntegerPosition[i] : internalIntegerRevPosition[i];
					long room = 2L * totalSampleCount - 4 - pos;
					long maxOut = room > 0 ? (long)std::floor((room - 1) / ratio) : 0;
					if (maxOut < nbr_spl)
						nbr_spl = std::max(maxOut, 0L);
					if (nbr_spl <= 0 && pos >= 0) {
						ended[i] = play[i];
						play[i] = false;
						rel[i] = false;
					}
				}
				if (nbr_spl>0) {
					float buff[EDS_BLOCK];
					if (direction[i]==1)
						voices[i].interpolate_block(buff, nbr_spl);
					else
						rev_voices[i].interpolate_block(buff, nbr_spl);
					for (int j=0; j<nbr_spl; j++)
						*(audio[i].endData()+j) = buff[j];
					audio[i].endIncr(nbr_spl);
				}
				feed[i] = false;
			}
		}
		else if (feed[i]) {
			for (int j=0; j<EDS_BLOCK; j++)
				*(audio[i].endData()+j) = 0.0f;
			audio[i].endIncr(EDS_BLOCK);
			feed[i] = false;
		}

		voiceTime[i]+= play[i] || rel[i] ? args.sampleTime : 0.0f;

		float out = 0.f;
		float level = 0.f;
		bool queued = audio[i].size()>=1;
		// An empty loop or release loop mutes only while it is in use.
		bool silent = (play[i] && loopMode>0 && (loopStart==loopEnd) && (internalIntegerPosition[i]>=loopStart))
			|| (rel[i] && releaseMode>=2 && (releaseStart==sampleEnd) && (internalIntegerPosition[i]>=releaseStart));
		if (queued && !silent && (play[i] || rel[i]))
			level = gain[i] * sampleGain;
		// A sudden drop to silence (no release, the end of the data, an empty
		// loop): the head goes on for 2 ms, fading.
		if (!tailed && level == 0.f && lastLevel[i] > 0.f)
			tailFrom(i);
		if (queued) {
			if (level > 0.f) {
				level *= dc[i].nextIn();
				out = *audio[i].startData()*5.0f*level;
			}
			audio[i].startIncr(1);
		}
		lastLevel[i] = level;
		if (dc[i].tailActive()) {
			const sampleread::View<1> view{smp->data.data(), totalSampleCount, nullptr};
			float t = 0.f;
			dc[i].addTail(view, &t);
			out += 5.0f * t;
		}
		if (fading) {
			const sampleread::View<1> old{fading->data.data(), fading->count, nullptr};
			float t = 0.f;
			fadeOld[i][0].addTail(old, &t);
			fadeOld[i][1].addTail(old, &t);
			out += 5.0f * t;
		}
		outputs[OUT].setVoltage(edsSoftLimit(out), i);
	}

	outputs[OUT].setChannels(channels);
}

struct EDSAROSLoopDisplay : TransparentWidget {
	EDSAROS *module;

	EDSAROSLoopDisplay() {

	}

	void drawLoop(const DrawArgs &args, Vec pos) {
		if (module) {
			int loopType = (int)module->params[EDSAROS::LOOPMODE_PARAM].getValue();
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgFontSize(args.vg, 12.0f);
			nvgTextLetterSpacing(args.vg, -2.0f);
			if (loopType == 0) {
				std::string s = "⇥";
				nvgText(args.vg, 0, 0, s.c_str(), NULL);
			}
			else if (loopType == 1) {
				std::string s = "⇉";
				nvgText(args.vg, 0, 0, s.c_str(), NULL);
			}
			else if (loopType == 2) {
				std::string s = "⇄";
				nvgText(args.vg, 0, 0, s.c_str(), NULL);
			}
		}
	}

  void drawLayer(const DrawArgs& args, int layer) override {
  	if (layer == 1) {
  		drawLoop(args, Vec(0.0f, 0.0f));
  	}
  	Widget::drawLayer(args, layer);
  }

};

struct EDSAROSReleaseDisplay : TransparentWidget {
	EDSAROS *module;

	EDSAROSReleaseDisplay() {

	}

	void drawRelease(const DrawArgs &args, Vec pos) {
		if (module) {
			int releaseType = (int)module->params[EDSAROS::RELEASEMODE_PARAM].getValue();
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgFontSize(args.vg, 12.0f);
			nvgTextLetterSpacing(args.vg, -2.0f);
			if (releaseType == 0) {
				std::string s = "-";
				nvgText(args.vg, 0, 0, s.c_str(), NULL);
			}
			else if (releaseType == 1) {
				std::string s = "⇥";
				nvgText(args.vg, 0, 0, s.c_str(), NULL);
			}
			else if (releaseType == 2) {
				std::string s = "⇉";
				nvgText(args.vg, 0, 0, s.c_str(), NULL);
			}
			else if (releaseType == 3) {
				std::string s = "⇄";
				nvgText(args.vg, 0, 0, s.c_str(), NULL);
			}
		}
	}

  void drawLayer(const DrawArgs& args, int layer) override {
    if (layer == 1) {
      drawRelease(args, Vec(0.0f, 0.0f));
    }
    Widget::drawLayer(args, layer);
  }

};

struct EDSAROSLinkDisplay : TransparentWidget {
	EDSAROS *module;

	EDSAROSLinkDisplay() {

	}

	void drawLink(const DrawArgs &args, Vec pos) {
		if (module) {
			int linkType = (int)module->params[EDSAROS::LINKTYPE_PARAM].getValue();
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			nvgFontSize(args.vg, 12.0f);
			nvgTextLetterSpacing(args.vg, -2.0f);
			if (linkType == 0) {
				std::string s = "-";
				nvgText(args.vg, 0, 0, s.c_str(), NULL);
			}
			else if (linkType == 1) {
				std::string s = "⇥";
				nvgText(args.vg, 0, 0, s.c_str(), NULL);
			}
			else if (linkType == 2) {
				std::string s = "⇉";
				nvgText(args.vg, 0, 0, s.c_str(), NULL);
			}
			else if (linkType == 3) {
				std::string s = "⇄";
				nvgText(args.vg, 0, 0, s.c_str(), NULL);
			}
		}
	}

  void drawLayer(const DrawArgs& args, int layer) override {
    if (layer == 1) {
      drawLink(args, Vec(0.0f, 0.0f));
    }
    Widget::drawLayer(args, layer);
  }

};

struct EDSAROSDisplay : OpaqueWidget {
	EDSAROS *module;
	const float width = 125.0f;
	const float height = 60.0f;
	float zoomWidth = 125.0f;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refX = 0.0f;

	EDSAROSDisplay() {

	}

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
		// As in cANARd: drag up zooms in, drag down out, the same step both ways.
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

	void drawSample(const DrawArgs &args) {
		std::shared_ptr<EdsarosOverview> o;
		{
			std::lock_guard<std::mutex> lock(module->uiMutex);
			o = module->uiOverview;
		}
		if (!o || o->count <= 0)
			return;
		float nbSample = o->count;
		float g = module->params[EDSAROS::GAIN_PARAM].getValue();
		auto px = [&](float idx) { return idx * zoomWidth / nbSample + zoomLeftAnchor; };

		nvgSave(args.vg);
		Rect b = Rect(Vec(zoomLeftAnchor, 0), Vec(zoomWidth, height));
		nvgScissor(args.vg, -0.5f, b.pos.y-0.5f, width+1.0f, height+1.0f);

		// The centre line, as in cANARd and sOUNAiRe
		nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
		nvgStrokeWidth(args.vg, 1);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0, height/2.0f);
		nvgLineTo(args.vg, width, height/2.0f);
		nvgStroke(args.vg);

		// The loop, when one is on, shaded behind the waveform (src/WaveStyle.hpp)
		const float playX0 = px(module->sampleStart), playX1 = px(module->sampleEnd);
		if ((int)module->params[EDSAROS::LOOPMODE_PARAM].getValue() > 0)
			wavestyle::band(args.vg, px(module->loopStart), px(module->loopEnd), 0, height);

		// The waveform, filled as in cANARd: one column per pixel, from its lowest to its highest value; from the
		// overview when a pixel covers more than one of its bins, from the sample when zoomed in. Hot pink between
		// S STRT and S END (what plays), dimmed elsewhere
		for (int pass = 0; pass < 2; pass++) {
			const int total = o->count, bins = EdsarosSample::OVERVIEW;
			const double perPixel = (double)total / b.size.x, binFrames = (double)total / bins;
			nvgBeginPath(args.vg);
			for (int x = 0; x < (int)width; x++) {
				if ((x >= playX0 && x <= playX1) != (pass == 0))
					continue;
				const double f0 = (x - b.pos.x) * perPixel, f1 = f0 + perPixel;
				if (f1 <= 0.0 || f0 >= total)
					continue;
				float lo = 1.f, hi = -1.f;
				if (perPixel >= binFrames || (int)o->data.size() < total) {
					const int k0 = clamp((int)(f0 / binFrames), 0, bins - 1), k1 = clamp((int)(f1 / binFrames), k0 + 1, bins);
					for (int k = k0; k < k1; k++) {
						lo = std::min(lo, o->overviewMin[k]);
						hi = std::max(hi, o->overviewMax[k]);
					}
				}
				else {
					const int j0 = clamp((int)f0, 0, total - 1), j1 = clamp((int)std::ceil(f1), j0, total - 1);
					for (int j = j0; j <= j1; j++) {
						lo = std::min(lo, o->data[j]);
						hi = std::max(hi, o->data[j]);
					}
				}
				wavestyle::column(args.vg, x, lo * g, hi * g, b.pos.y + b.size.y * 0.5f, b.size.y * 0.5f);
			}
			wavestyle::fill(args.vg, pass == 0);
		}

		// The points as yellow bars: the loop's flags at the top, the sample's and the release's lower
		wavestyle::bar(args.vg, px(module->sampleStart), 10, height, 1);
		wavestyle::bar(args.vg, px(module->loopStart), 0, height, 1);
		wavestyle::bar(args.vg, px(module->loopEnd), 0, height, -1);
		wavestyle::bar(args.vg, px(module->releaseStart), 10, height, 1);
		wavestyle::bar(args.vg, px(module->sampleEnd), 10, height, -1);

		// A play head for each voice that sounds (polyphonic: one per channel), on top: it lights up the pink
		const int voices = clamp(module->lastChannels, 0, 16);
		for (int i = 0; i < voices; i++) {
			if (!(module->play[i] || module->rel[i]) || module->lastLevel[i] <= 0.f)
				continue;
			wavestyle::head(args.vg, px(module->direction[i] == 1 ? module->internalIntegerPosition[i] : module->revIndex(module->internalIntegerRevPosition[i])), 0, height,
				0.3f + 0.7f * clamp(module->lastLevel[i], 0.f, 1.f));
		}

		nvgResetScissor(args.vg);
		nvgRestore(args.vg);
	}

	void drawEnv(const DrawArgs &args) {
		nvgSave(args.vg);
		nvgStrokeWidth(args.vg, 1);
		nvgBeginPath(args.vg);
		nvgStrokeColor(args.vg, YELLOW_BIDOO);
		bool first = true;
		for (int i=0; i<width; i++) {
			float x=i*160/width;
			float y=height*(1.0f - module->getEnv(x < 160-module->release ? x : x-160+module->release, x > 160-module->release));
			if (first) {
				nvgMoveTo(args.vg,i,y);
				first=false;
			}
			else {
				nvgLineTo(args.vg, i, y);
			}
		}

		nvgStroke(args.vg);
		nvgRestore(args.vg);
	}

  void drawLayer(const DrawArgs& args, int layer) override {
    if (layer == 1) {
      if (module) {
  			if (module->showSample) {
  				drawSample(args);
  			}
  			else {
  				drawEnv(args);
  			}
  		}
    }
    Widget::drawLayer(args, layer);
  }

};

struct EDSAROSBidooSmallBlueKnob : BidooSmallBlueKnob {
	bool showSample = false;

	void onButton(const ButtonEvent& e) override {
		BidooSmallBlueKnob::onButton(e);
		ParamQuantity *pq = this->getParamQuantity();
		EDSAROS *module = pq ? dynamic_cast<EDSAROS*>(pq->module) : NULL;
		if (module)
			module->showSample = showSample;
	}
};

struct EDSAROSBidooSmallSnapBlueKnob : BidooSmallSnapBlueKnob {
	bool showSample = false;

	void onButton(const ButtonEvent& e) override {
		BidooSmallSnapBlueKnob::onButton(e);
		ParamQuantity *pq = this->getParamQuantity();
		EDSAROS *module = pq ? dynamic_cast<EDSAROS*>(pq->module) : NULL;
		if (module)
			module->showSample = showSample;
	}
};


struct EDSAROSWidget : BidooWidget {
	EDSAROSWidget(EDSAROS *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/EDSAROS.svg"));

		theme::addScrews(this);

		// The original hand-drawn panel: the screen; a grid of small knobs, five across, 28 px down, each with its tiny
		// CV jack below and the mode screens on the right; GATE, VO, the zero-crossing button and OUT. Old corners kept
		// (every knob and jack within 0.25 px of its label), except the three bottom jacks, now on their boxes' centres
		// (the old code had VO 0.7 px left of its box).
		EDSAROSDisplay *display = new EDSAROSDisplay();
		display->module = module;
		display->box = theme::specificRect(5, 25, 125, 60);
		addChild(display);

		const float controlsXOffset = 26;   // across
		const float controlsYOffset = 28;   // down
		const float inputsXOffset = 4.5f;   // a tiny jack's corner right of its knob's
		const theme::Pos firstKnob = theme::specificCorner(3.5f, 97, theme::Size{mm2px(8.f), mm2px(8.f)});   // BidooSmallBlueKnob (8 mm)
		const theme::Pos firstCv = theme::specificCorner(3.5f + inputsXOffset, 97 + controlsYOffset, theme::kTinyJackSize);
		auto knob = [&](int c, int r) { return firstKnob.offset(controlsXOffset * c, controlsYOffset * r); };
		auto cv = [&](int c, int r) { return firstCv.offset(controlsXOffset * c, controlsYOffset * r); };
		auto modeScreen = [&](int r) { return theme::specificRect(3.5f + 4 * controlsXOffset + 12, 97 + r * controlsYOffset + 10.5f, 20, 10); };

		EDSAROSBidooSmallBlueKnob *btnSampleStartParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(0, 0), module, EDSAROS::SAMPLESTART_PARAM);
		btnSampleStartParam->showSample = true;
		addParam(btnSampleStartParam);
		EDSAROSBidooSmallBlueKnob *btnSampleEndParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(1, 0), module, EDSAROS::SAMPLEEND_PARAM);
		btnSampleEndParam->showSample = true;
		addParam(btnSampleEndParam);
		EDSAROSBidooSmallBlueKnob *btnLoopStartParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(2, 0), module, EDSAROS::LOOPSTART_PARAM);
		btnLoopStartParam->showSample = true;
		addParam(btnLoopStartParam);
		EDSAROSBidooSmallBlueKnob *btnLoopEndParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(3, 0), module, EDSAROS::LOOPEND_PARAM);
		btnLoopEndParam->showSample = true;
		addParam(btnLoopEndParam);
		EDSAROSBidooSmallBlueKnob *btnReleaseStartParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(4, 0), module, EDSAROS::RELEASESTART_PARAM);
		btnReleaseStartParam->showSample = true;
		addParam(btnReleaseStartParam);

		addInput(theme::input<TinyPJ301MPort>(cv(0, 0), module, EDSAROS::SAMPLESTART_INPUT));
		addInput(theme::input<TinyPJ301MPort>(cv(1, 0), module, EDSAROS::SAMPLEEND_INPUT));
		addInput(theme::input<TinyPJ301MPort>(cv(2, 0), module, EDSAROS::LOOPSTART_INPUT));
		addInput(theme::input<TinyPJ301MPort>(cv(3, 0), module, EDSAROS::LOOPEND_INPUT));
		addInput(theme::input<TinyPJ301MPort>(cv(4, 0), module, EDSAROS::RELEASESTART_INPUT));

		EDSAROSBidooSmallBlueKnob *btnAttackParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(0, 2), module, EDSAROS::ATTACK_PARAM);
		btnAttackParam->showSample = false;
		addParam(btnAttackParam);
		EDSAROSBidooSmallBlueKnob *btnDecayParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(1, 2), module, EDSAROS::DECAY_PARAM);
		btnDecayParam->showSample = false;
		addParam(btnDecayParam);
		EDSAROSBidooSmallBlueKnob *btnReleaseParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(2, 2), module, EDSAROS::RELEASE_PARAM);
		btnReleaseParam->showSample = false;
		addParam(btnReleaseParam);

		EDSAROSBidooSmallSnapBlueKnob *btnLoopModeParam = theme::param<EDSAROSBidooSmallSnapBlueKnob>(knob(4, 2), module, EDSAROS::LOOPMODE_PARAM);
		btnLoopModeParam->showSample = true;
		addParam(btnLoopModeParam);

		{
			EDSAROSLoopDisplay *displayLoop = new EDSAROSLoopDisplay();
			displayLoop->module = module;
			displayLoop->box = modeScreen(3);
			addChild(displayLoop);
		}

		addInput(theme::input<TinyPJ301MPort>(cv(0, 2), module, EDSAROS::ATTACK_INPUT));
		addInput(theme::input<TinyPJ301MPort>(cv(1, 2), module, EDSAROS::DECAY_INPUT));
		addInput(theme::input<TinyPJ301MPort>(cv(2, 2), module, EDSAROS::RELEASE_INPUT));

		EDSAROSBidooSmallBlueKnob *btnAttackSlopeParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(0, 4), module, EDSAROS::ATTACKSLOPE_PARAM);
		btnAttackSlopeParam->showSample = false;
		addParam(btnAttackSlopeParam);
		EDSAROSBidooSmallBlueKnob *btnDecaySlopeParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(1, 4), module, EDSAROS::DECAYSLOPE_PARAM);
		btnDecaySlopeParam->showSample = false;
		addParam(btnDecaySlopeParam);
		EDSAROSBidooSmallBlueKnob *btnReleaseSlopeParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(2, 4), module, EDSAROS::RELEASESLOPE_PARAM);
		btnReleaseSlopeParam->showSample = false;
		addParam(btnReleaseSlopeParam);
		EDSAROSBidooSmallSnapBlueKnob *btnReleaseModeParam = theme::param<EDSAROSBidooSmallSnapBlueKnob>(knob(4, 4), module, EDSAROS::RELEASEMODE_PARAM);
		btnReleaseModeParam->showSample = true;
		addParam(btnReleaseModeParam);

		{
			EDSAROSReleaseDisplay *displayRelease = new EDSAROSReleaseDisplay();
			displayRelease->module = module;
			displayRelease->box = modeScreen(5);
			addChild(displayRelease);
		}

		addInput(theme::input<TinyPJ301MPort>(cv(0, 4), module, EDSAROS::ATTACKSLOPE_INPUT));
		addInput(theme::input<TinyPJ301MPort>(cv(1, 4), module, EDSAROS::DECAYSLOPE_INPUT));
		addInput(theme::input<TinyPJ301MPort>(cv(2, 4), module, EDSAROS::RELEASESLOPE_INPUT));

		EDSAROSBidooSmallBlueKnob *btnInitParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(0, 6), module, EDSAROS::INIT_PARAM);
		btnInitParam->showSample = false;
		addParam(btnInitParam);
		EDSAROSBidooSmallBlueKnob *btnPeakParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(1, 6), module, EDSAROS::PEAK_PARAM);
		btnPeakParam->showSample = false;
		addParam(btnPeakParam);
		EDSAROSBidooSmallBlueKnob *btnSustainParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(2, 6), module, EDSAROS::SUSTAIN_PARAM);
		btnSustainParam->showSample = false;
		addParam(btnSustainParam);
    EDSAROSBidooSmallBlueKnob *btnGainParam = theme::param<EDSAROSBidooSmallBlueKnob>(knob(3, 6), module, EDSAROS::GAIN_PARAM);
		btnGainParam->showSample = true;
		addParam(btnGainParam);
		EDSAROSBidooSmallSnapBlueKnob *btnLinkModeParam = theme::param<EDSAROSBidooSmallSnapBlueKnob>(knob(4, 6), module, EDSAROS::LINKTYPE_PARAM);
		btnLinkModeParam->showSample = true;
		addParam(btnLinkModeParam);

		{
			EDSAROSLinkDisplay *displayLink = new EDSAROSLinkDisplay();
			displayLink->module = module;
			displayLink->box = modeScreen(7);
			addChild(displayLink);
		}

		addInput(theme::input<TinyPJ301MPort>(cv(0, 6), module, EDSAROS::INIT_INPUT));
		addInput(theme::input<TinyPJ301MPort>(cv(1, 6), module, EDSAROS::PEAK_INPUT));
		addInput(theme::input<TinyPJ301MPort>(cv(2, 6), module, EDSAROS::SUSTAIN_INPUT));

		addParam(theme::param<LEDButton>(theme::specificCorner(75, 333, theme::Size{17.9996f, 18.f}), module, EDSAROS::ZEROCROSSING_PARAM));   // LEDButton
		addChild(theme::light<SmallLight<GreenLight>>(theme::specificCorner(75 + 6, 333 + 6, theme::Size{mm2px(2.f), mm2px(2.f)}), module, EDSAROS::ZEROCROSSING_LIGHT));   // SmallLight, 6 px into the button
		const theme::Pos jackRow = theme::specificCorner(7, 330, theme::kJackSize);
		addInput(theme::input<PJ301MPort>(theme::colRow(theme::specific(19.156f, 0), jackRow), module, EDSAROS::TRIG_INPUT));    // the boxes' centres
		addInput(theme::input<PJ301MPort>(theme::colRow(theme::specific(52.531f, 0), jackRow), module, EDSAROS::PITCH_INPUT));
		addOutput(theme::output<PJ301MPort>(theme::colRow(theme::specific(115.125f, 0), jackRow), module, EDSAROS::OUT));
	}

  struct EDSAROSItem : MenuItem {
  	EDSAROS *module;
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
		EDSAROS *module = dynamic_cast<EDSAROS*>(this->module);
		if (!module)
			return;
		menu->addChild(new MenuSeparator());
		menu->addChild(construct<EDSAROSItem>(&MenuItem::text, "Load sample", &EDSAROSItem::module, module));
		menu->addChild(createBoolPtrMenuItem("Trigger mode (a trigger plays attack and decay, then the release)", "", &module->triggerMode));
	}

	void onPathDrop(const PathDropEvent& e) override {
		Widget::onPathDrop(e);
		EDSAROS *module = dynamic_cast<EDSAROS*>(this->module);
		if (module && !e.paths.empty())
			module->loadFile(e.paths[0]);
	}
};

Model *modelEDSAROS = createModel<EDSAROS, EDSAROSWidget>("eDsaroS");
