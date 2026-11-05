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
};

struct EDSAROS : BidooModule {
	enum ParamIds {
		SAMPLESTART_PARAM,
		SAMPLEEND_PARAM,
		LOOPSTART_PARAM,
		LOOPEND_PARAM,
		LOOPCROSSFADE_PARAM,
		RELEASESTART_PARAM,
		RELEASECROSSFADE_PARAM,
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
		LOOPCROSSFADE_INPUT,
		RELEASESTART_INPUT,
		RELEASECROSSFADE_INPUT,
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
		configSwitch(RELEASEMODE_PARAM, 0.0f, 3.0f, 0.0f, "Release mode", {"Cut", "Play to the end", "Forward loop", "Ping-pong loop"});
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

	// The release goes from `from`, the level when the gate fell, to 0.
	float getEnv(const float t, const bool r, const float from) {
		if (attack>0 && t>=0.0f && t<=attack && !r) {
			return getYBez(t/attack,init,attackSlope*(peak-init)+init,peak);
		}
		else if (decay>0 && t>attack && t<=(attack+decay) && !r) {
			return getYBez((t-attack)/decay,peak,decaySlope*(sustain-peak)+peak,sustain);
		}
		else if (r && t<=release) {
			return getYBez(t/release,from,releaseSlope*from,0.0f);
		}
		else if (r && t>release) {
			return 0.0f;
		}
		return sustain;
	}

	float getEnv(const float t, const bool r) {
		return getEnv(t, r, sustain);
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
	// Take a newly loaded sample, once the worker has freed the previous one.
	if (pending.load(std::memory_order_relaxed) && !retired.load(std::memory_order_acquire)) {
		EdsarosSample *s = pending.exchange(nullptr, std::memory_order_acq_rel);
		if (s) {
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
				play[i] = rel[i] = false;
				gain[i] = 0.f;
				audio[i].clear();
				direction[i] = 1;
			}
		}
	}
	// The engine rate changed: load the file again at the new rate.
	if (smp && smp->sampleRate != args.sampleRate && !loading)
		reloadRate = args.sampleRate;

	if (zeroCrossingTrigger.process(params[ZEROCROSSING_PARAM].getValue()))
		zeroCrossing=!zeroCrossing;
	lights[ZEROCROSSING_LIGHT].setBrightness(zeroCrossing?1:0);

	attack = clamp(params[ATTACK_PARAM].getValue()+inputs[ATTACK_INPUT].getVoltage()*2.0f,0.0f,20.0f);
	decay = clamp(params[DECAY_PARAM].getValue()+inputs[DECAY_INPUT].getVoltage()*6.0f,0.0f,60.0f);
	release = clamp(params[RELEASE_PARAM].getValue()+inputs[RELEASE_INPUT].getVoltage()*6.0f,0.0f,60.0f);
	init = clamp(params[INIT_PARAM].getValue()+inputs[INIT_INPUT].getVoltage()*0.1f,0.0f,1.0f);
	peak = clamp(params[PEAK_PARAM].getValue()+inputs[PEAK_INPUT].getVoltage()*0.1f,0.0f,1.0f);
	sustain = clamp(params[SUSTAIN_PARAM].getValue()+inputs[SUSTAIN_INPUT].getVoltage()*0.1f,0.0f,1.0f);
	attackSlope = clamp(params[ATTACKSLOPE_PARAM].getValue()+inputs[ATTACKSLOPE_INPUT].getVoltage()*0.1f,-1.0f,1.0f);
	decaySlope = clamp(params[DECAYSLOPE_PARAM].getValue()+inputs[DECAYSLOPE_INPUT].getVoltage()*0.1f,-1.0f,1.0f);
	releaseSlope = clamp(params[RELEASESLOPE_PARAM].getValue()+inputs[RELEASESLOPE_INPUT].getVoltage()*0.1f,-1.0f,1.0f);

	updatePoints();

	// One voice per channel of the pitch or gate input, whichever has more.
	int channels = std::max(std::max(inputs[PITCH_INPUT].getChannels(), inputs[TRIG_INPUT].getChannels()), 1);
	for (int i = channels; i < lastChannels; i++) {
		play[i] = rel[i] = false;
		gain[i] = 0.f;
		audio[i].clear();
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
			}
			gateHigh = oneShot[i] && (!play[i] || voiceTime[i] < attack + decay);
			if (!gateHigh)
				oneShot[i] = false;
		}
		if (gateHigh) {
			if (!play[i]) {
				voiceTime[i]=0.0f;
				rel[i]=false;
				setPos(i, sampleStart, revIndex(sampleStart));
				direction[i]=1;
				audio[i].clear();
			}
			play[i]=true;
		}
		else {
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

		gain[i] = (play[i] || rel[i]) ? getEnv(voiceTime[i], rel[i], releaseFrom[i]) : 0.0f;

		if (audio[i].size()==0) { feed[i] = true;}

		internalIntegerPosition[i] = voices[i].get_playback_pos() >> 32;
		internalIntegerRevPosition[i] = rev_voices[i].get_playback_pos() >> 32;

		// A voice going backward when the mode no longer has a backward leg
		// (the loop or release mode was changed) turns forward where it is.
		if (direction[i] == -1 && !((play[i] && loopMode == 2) || (rel[i] && releaseMode == 3))) {
			int at = clamp(revIndex(internalIntegerRevPosition[i]), 0, totalSampleCount - 1);
			voices[i].set_playback_pos(static_cast <rspl::Int64> (at) << 32);
			internalIntegerPosition[i] = at;
			direction[i] = 1;
		}

		if ((play[i] || rel[i]) && feed[i]) {
			if (loopMode==0 && internalIntegerPosition[i]>=sampleEnd) {
				rel[i]=false;
				setPos(i, sampleEnd, revIndex(sampleEnd));
			}
			else if (loopMode==1 && internalIntegerPosition[i]>=loopEnd && play[i]) {
				voices[i].set_playback_pos(static_cast <rspl::Int64> (loopStart) << 32);
			}
			else if (loopMode==2 && direction[i]==1 && internalIntegerPosition[i]>=loopEnd && play[i]) {
				setPos(i, loopStart, revIndex(loopEnd));
				direction[i]=-1;
			}
			else if (loopMode==2 && direction[i]==-1 && internalIntegerRevPosition[i]>=revIndex(loopStart) && play[i]) {
				setPos(i, loopStart, revIndex(loopEnd));
				direction[i]=1;
			}

			if (releaseMode==0 && !play[i]) {
				voices[i].set_playback_pos(static_cast <rspl::Int64> (sampleStart) << 32);
				rel[i]=false;
			}
			else if (releaseMode==1 && internalIntegerPosition[i]>=sampleEnd) {
				voices[i].set_playback_pos(static_cast <rspl::Int64> (sampleStart) << 32);
				rel[i]=false;
			}
			else if (releaseMode==2 && internalIntegerPosition[i]>=sampleEnd && rel[i]) {
				voices[i].set_playback_pos(static_cast <rspl::Int64> (releaseStart) << 32);
			}
			else if (releaseMode==2 && internalIntegerPosition[i]>=sampleEnd && !rel[i]) {
				voices[i].set_playback_pos(static_cast <rspl::Int64> (sampleStart) << 32);
			}
			else if (releaseMode==3 && direction[i]==1 && internalIntegerPosition[i]>=sampleEnd && rel[i]) {
				setPos(i, releaseStart, revIndex(sampleEnd));
				direction[i]=-1;
			}
			else if (releaseMode==3 && direction[i]==-1 && internalIntegerRevPosition[i]>=revIndex(releaseStart) && rel[i]) {
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
		if (audio[i].size()>=1) {
			bool silent = ((loopStart==loopEnd) && (internalIntegerPosition[i]>=loopStart)) || ((releaseStart==sampleEnd) && (internalIntegerPosition[i]>=releaseStart));
			if (!silent)
				out = *audio[i].startData()*5.0f*gain[i]*sampleGain;
			audio[i].startIncr(1);
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
    if (e.mouseDelta.y < 0.0f) {
      zoom = 1.0f/(((APP->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 3.0f : 2.0f);
    }
    else if (e.mouseDelta.y > 0.0f) {
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

	// A point marker: a line with a small flag, pointing right or left.
	void drawMarker(const DrawArgs &args, float x, float top, bool right) {
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, x, top);
		nvgLineTo(args.vg, x, height);
		nvgClosePath(args.vg);
		nvgStroke(args.vg);
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, x, top);
		nvgLineTo(args.vg, x + (right ? 5.0f : -5.0f), top + 3);
		nvgLineTo(args.vg, x, top + 6);
		nvgLineTo(args.vg, x, top);
		nvgClosePath(args.vg);
		nvgStroke(args.vg);
		nvgFill(args.vg);
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

		nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 255));
		nvgBeginPath(args.vg);
		nvgMoveTo(args.vg, 0, height/2.0f);
		nvgLineTo(args.vg, width, height/2.0f);
		nvgClosePath(args.vg);
		nvgStroke(args.vg);

		// Waveform, from the overview built at load time.
		nvgStrokeColor(args.vg, nvgRGBA(164, 3, 111, 200));
		nvgBeginPath(args.vg);
		for (int i = 0; i < EdsarosSample::OVERVIEW; i++) {
			float x = b.pos.x + b.size.x * (i + 0.5f) / EdsarosSample::OVERVIEW;
			float y = b.pos.y + b.size.y * (0.5f - 0.5f * clamp(o->overviewMax[i] * g, -1.f, 1.f));
			if (i == 0)
				nvgMoveTo(args.vg, x, y);
			else
				nvgLineTo(args.vg, x, y);
		}
		for (int i = EdsarosSample::OVERVIEW - 1; i >= 0; i--) {
			float x = b.pos.x + b.size.x * (i + 0.5f) / EdsarosSample::OVERVIEW;
			nvgLineTo(args.vg, x, b.pos.y + b.size.y * (0.5f - 0.5f * clamp(o->overviewMin[i] * g, -1.f, 1.f)));
		}
		nvgLineCap(args.vg, NVG_MITER);
		nvgStrokeWidth(args.vg, 1);
		nvgStroke(args.vg);

		// Playhead of the first voice.
		nvgStrokeColor(args.vg, LIGHTBLUE_BIDOO);
		nvgBeginPath(args.vg);
		nvgStrokeWidth(args.vg, 1);
		float head = px(module->direction[0] == 1 ? module->internalIntegerPosition[0] : module->revIndex(module->internalIntegerRevPosition[0]));
		nvgMoveTo(args.vg, head, 0);
		nvgLineTo(args.vg, head, height);
		nvgClosePath(args.vg);
		nvgStroke(args.vg);

		// Points.
		nvgStrokeColor(args.vg, YELLOW_BIDOO);
		nvgFillColor(args.vg, YELLOW_BIDOO);
		nvgStrokeWidth(args.vg, 1);
		drawMarker(args, px(module->sampleStart), 10, true);
		drawMarker(args, px(module->loopStart), 0, true);
		drawMarker(args, px(module->loopEnd), 0, false);
		drawMarker(args, px(module->releaseStart), 10, true);
		drawMarker(args, px(module->sampleEnd), 10, false);

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

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			EDSAROSDisplay *display = new EDSAROSDisplay();
			display->module = module;
			display->box.pos = Vec(5, 25);
			display->box.size = Vec(125, 60);
			addChild(display);
		}

		const float controlsXAnchor = 3.5f;
		const float controlsXOffset = 26;
		const float controlsYAnchor = 97;
		const float controlsYOffset = 28;
		const float inputsXOffset = 4.5f;

		EDSAROSBidooSmallBlueKnob *btnSampleStartParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor, controlsYAnchor), module, EDSAROS::SAMPLESTART_PARAM);
		btnSampleStartParam->showSample = true;
		addParam(btnSampleStartParam);
		EDSAROSBidooSmallBlueKnob *btnSampleEndParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor+controlsXOffset, controlsYAnchor), module, EDSAROS::SAMPLEEND_PARAM);
		btnSampleEndParam->showSample = true;
		addParam(btnSampleEndParam);
		EDSAROSBidooSmallBlueKnob *btnLoopStartParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor+2*controlsXOffset, controlsYAnchor), module, EDSAROS::LOOPSTART_PARAM);
		btnLoopStartParam->showSample = true;
		addParam(btnLoopStartParam);
		EDSAROSBidooSmallBlueKnob *btnLoopEndParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor+3*controlsXOffset, controlsYAnchor), module, EDSAROS::LOOPEND_PARAM);
		btnLoopEndParam->showSample = true;
		addParam(btnLoopEndParam);
		EDSAROSBidooSmallBlueKnob *btnReleaseStartParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor+4*controlsXOffset, controlsYAnchor), module, EDSAROS::RELEASESTART_PARAM);
		btnReleaseStartParam->showSample = true;
		addParam(btnReleaseStartParam);

		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+inputsXOffset, controlsYAnchor+controlsYOffset), module, EDSAROS::SAMPLESTART_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+controlsXOffset+inputsXOffset, controlsYAnchor+controlsYOffset), module, EDSAROS::SAMPLEEND_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+2*controlsXOffset+inputsXOffset, controlsYAnchor+controlsYOffset), module, EDSAROS::LOOPSTART_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+3*controlsXOffset+inputsXOffset, controlsYAnchor+controlsYOffset), module, EDSAROS::LOOPEND_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+4*controlsXOffset+inputsXOffset, controlsYAnchor+controlsYOffset), module, EDSAROS::RELEASESTART_INPUT));

		EDSAROSBidooSmallBlueKnob *btnAttackParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor, controlsYAnchor+2*controlsYOffset), module, EDSAROS::ATTACK_PARAM);
		btnAttackParam->showSample = false;
		addParam(btnAttackParam);
		EDSAROSBidooSmallBlueKnob *btnDecayParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor+controlsXOffset, controlsYAnchor+2*controlsYOffset), module, EDSAROS::DECAY_PARAM);
		btnDecayParam->showSample = false;
		addParam(btnDecayParam);
		EDSAROSBidooSmallBlueKnob *btnReleaseParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor+2*controlsXOffset, controlsYAnchor+2*controlsYOffset), module, EDSAROS::RELEASE_PARAM);
		btnReleaseParam->showSample = false;
		addParam(btnReleaseParam);

		EDSAROSBidooSmallSnapBlueKnob *btnLoopModeParam = createParam<EDSAROSBidooSmallSnapBlueKnob>(Vec(controlsXAnchor+4*controlsXOffset, controlsYAnchor+2*controlsYOffset), module, EDSAROS::LOOPMODE_PARAM);
		btnLoopModeParam->showSample = true;
		addParam(btnLoopModeParam);

		{
			EDSAROSLoopDisplay *displayLoop = new EDSAROSLoopDisplay();
			displayLoop->module = module;
			displayLoop->box.pos = Vec(controlsXAnchor+4*controlsXOffset+12, controlsYAnchor+3*controlsYOffset+10.5f);
			displayLoop->box.size = Vec(20.0f, 10.0f);
			addChild(displayLoop);
		}

		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+inputsXOffset, controlsYAnchor+3*controlsYOffset), module, EDSAROS::ATTACK_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+controlsXOffset+inputsXOffset, controlsYAnchor+3*controlsYOffset), module, EDSAROS::DECAY_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+2*controlsXOffset+inputsXOffset, controlsYAnchor+3*controlsYOffset), module, EDSAROS::RELEASE_INPUT));

		EDSAROSBidooSmallBlueKnob *btnAttackSlopeParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor, controlsYAnchor+4*controlsYOffset), module, EDSAROS::ATTACKSLOPE_PARAM);
		btnAttackSlopeParam->showSample = false;
		addParam(btnAttackSlopeParam);
		EDSAROSBidooSmallBlueKnob *btnDecaySlopeParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor+controlsXOffset, controlsYAnchor+4*controlsYOffset), module, EDSAROS::DECAYSLOPE_PARAM);
		btnDecaySlopeParam->showSample = false;
		addParam(btnDecaySlopeParam);
		EDSAROSBidooSmallBlueKnob *btnReleaseSlopeParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor+2*controlsXOffset, controlsYAnchor+4*controlsYOffset), module, EDSAROS::RELEASESLOPE_PARAM);
		btnReleaseSlopeParam->showSample = false;
		addParam(btnReleaseSlopeParam);
		EDSAROSBidooSmallSnapBlueKnob *btnReleaseModeParam = createParam<EDSAROSBidooSmallSnapBlueKnob>(Vec(controlsXAnchor+4*controlsXOffset, controlsYAnchor+4*controlsYOffset), module, EDSAROS::RELEASEMODE_PARAM);
		btnReleaseModeParam->showSample = true;
		addParam(btnReleaseModeParam);

		{
			EDSAROSReleaseDisplay *displayRelease = new EDSAROSReleaseDisplay();
			displayRelease->module = module;
			displayRelease->box.pos = Vec(controlsXAnchor+4*controlsXOffset+12, controlsYAnchor+5*controlsYOffset+10.5f);
			displayRelease->box.size = Vec(20.0f, 10.0f);
			addChild(displayRelease);
		}

		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+inputsXOffset, controlsYAnchor+5*controlsYOffset), module, EDSAROS::ATTACKSLOPE_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+controlsXOffset+inputsXOffset, controlsYAnchor+5*controlsYOffset), module, EDSAROS::DECAYSLOPE_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+2*controlsXOffset+inputsXOffset, controlsYAnchor+5*controlsYOffset), module, EDSAROS::RELEASESLOPE_INPUT));

		EDSAROSBidooSmallBlueKnob *btnInitParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor, controlsYAnchor+6*controlsYOffset), module, EDSAROS::INIT_PARAM);
		btnInitParam->showSample = false;
		addParam(btnInitParam);
		EDSAROSBidooSmallBlueKnob *btnPeakParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor+controlsXOffset, controlsYAnchor+6*controlsYOffset), module, EDSAROS::PEAK_PARAM);
		btnPeakParam->showSample = false;
		addParam(btnPeakParam);
		EDSAROSBidooSmallBlueKnob *btnSustainParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor+2*controlsXOffset, controlsYAnchor+6*controlsYOffset), module, EDSAROS::SUSTAIN_PARAM);
		btnSustainParam->showSample = false;
		addParam(btnSustainParam);
    EDSAROSBidooSmallBlueKnob *btnGainParam = createParam<EDSAROSBidooSmallBlueKnob>(Vec(controlsXAnchor+3*controlsXOffset, controlsYAnchor+6*controlsYOffset), module, EDSAROS::GAIN_PARAM);
		btnGainParam->showSample = true;
		addParam(btnGainParam);
		EDSAROSBidooSmallSnapBlueKnob *btnLinkModeParam = createParam<EDSAROSBidooSmallSnapBlueKnob>(Vec(controlsXAnchor+4*controlsXOffset, controlsYAnchor+6*controlsYOffset), module, EDSAROS::LINKTYPE_PARAM);
		btnLinkModeParam->showSample = true;
		addParam(btnLinkModeParam);

		{
			EDSAROSLinkDisplay *displayLink = new EDSAROSLinkDisplay();
			displayLink->module = module;
			displayLink->box.pos = Vec(controlsXAnchor+4*controlsXOffset+12, controlsYAnchor+7*controlsYOffset+10.5f);
			displayLink->box.size = Vec(20.0f, 10.0f);
			addChild(displayLink);
		}

		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+inputsXOffset, controlsYAnchor+7*controlsYOffset), module, EDSAROS::INIT_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+controlsXOffset+inputsXOffset, controlsYAnchor+7*controlsYOffset), module, EDSAROS::PEAK_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+2*controlsXOffset+inputsXOffset, controlsYAnchor+7*controlsYOffset), module, EDSAROS::SUSTAIN_INPUT));

    addParam(createParam<LEDButton>(Vec(75.0f, 333.f), module, EDSAROS::ZEROCROSSING_PARAM));
  	addChild(createLight<SmallLight<GreenLight>>(Vec(75.0f+6.0f, 333.f+6.0f), module, EDSAROS::ZEROCROSSING_LIGHT));

		addInput(createInput<PJ301MPort>(Vec(7, 330), module, EDSAROS::TRIG_INPUT));
		addInput(createInput<PJ301MPort>(Vec(40, 330), module, EDSAROS::PITCH_INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(103, 330), module, EDSAROS::OUT));
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
