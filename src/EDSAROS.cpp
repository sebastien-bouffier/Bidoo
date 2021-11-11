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
#include	"dep/resampler/StopWatch.h"

using namespace std;

#define SIZE 16

template <typename T>
typename std::enable_if<std::is_signed<T>::value, int>::type
inline constexpr signum(T const x) {
    return (T(0) < x) - (x < T(0));
}

struct EDSAROS : Module {
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
	std::string waveExtension;
	vector<dsp::Frame<1>> loadingBuffer;
	int channels=0;
  int sampleRate=0;
  int totalSampleCount=0;
	rspl::InterpPack interp_pack;
	rspl::MipMapFlt	mip_map;
	rspl::MipMapFlt	rev_mip_map;
	rspl::ResamplerFlt voices[16];
	rspl::ResamplerFlt rev_voices[16];
	float *sample;
	float *rev_sample;
	bool loading = false;
	std::mutex mylock;
	int pos = 0;
	dsp::DoubleRingBuffer<float,SIZE> audio[16];
	bool play[16] = {false};
	bool feed[16] = {false};
	int internalIntegerPosition[16] = {0};
	int internalFloatingPosition[16] = {0};
	int internalIntegerRevPosition[16] = {0};
	int internalFloatingRevPosition[16] = {0};
	const long depth = 1L << rspl::ResamplerFlt::NBR_BITS_PER_OCT;
	int sampleStart=0;
	int sampleEnd=0;
	int loopStart=0;
	int loopEnd=0;
	int releaseStart=0;
	int loopMode=0;
	int releaseMode=0;
	bool snap = true;
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
	int direction[16] = {1};
  bool zeroCrossing = false;
  dsp::SchmittTrigger zeroCrossingTrigger;

	EDSAROS() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SAMPLESTART_PARAM, 0.0f, 10.0f, 0.0f, "Sample start");
		configParam(LOOPSTART_PARAM, 0.0f, 10.0f, 0.0f, "Loop start");
		configParam(LOOPEND_PARAM, 0.0f, 10.0f, 10.0f, "Loop end");
		configParam(SAMPLEEND_PARAM, 0.0f, 10.0f, 10.0f, "Sample end");
		configParam(RELEASESTART_PARAM, 0.0f, 10.0f, 10.0f, "Release start");

		configParam(LOOPMODE_PARAM, 0.0f, 2.0f, 0.0f, "Loop mode");
		configParam(RELEASEMODE_PARAM, 0.0f, 3.0f, 0.0f, "Release mode");
		configParam(LINKTYPE_PARAM, 0.0f, 3.0f, 0.0f, "Link points mode");

		configParam(ATTACK_PARAM, 0.0f, 20.0f, 0.0f, "Attack time");
		configParam(DECAY_PARAM, 0.0f, 60.0f, 0.2f, "Decay time");
		configParam(RELEASE_PARAM, 0.0f, 60.0f, 0.5f, "Release time");
		configParam(INIT_PARAM, 0.0f, 1.0f, 0.0f, "Init gain");
		configParam(PEAK_PARAM, 0.0f, 1.0f, 1.0f, "Peak gain");
		configParam(SUSTAIN_PARAM, 0.0f, 1.0f, 1.0f, "Sustain gain");
		configParam(ATTACKSLOPE_PARAM, 0.0f, 1.0f, 0.5f, "Attack slope");
		configParam(DECAYSLOPE_PARAM, 0.0f, 1.0f, 0.5f, "Decay slope");
		configParam(RELEASESLOPE_PARAM, 0.0f, 1.0f, 0.5f, "Release slope");

    configParam(GAIN_PARAM, 1.0f, 10.0f, 1.0f, "Sample gain");
    configParam(ZEROCROSSING_PARAM, 0.0f, 1.0f, 0.0f, "Zero crossing");
	}

	~EDSAROS() {
		free(sample);
		free(rev_sample);
	}

	void process(const ProcessArgs &args) override;

	void loadSample();

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
    json_object_set_new(rootJ, "zeroCrossing", json_boolean(zeroCrossing));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ) {
			lastPath = json_string_value(lastPathJ);
			if (!lastPath.empty()) loadSample();
		}
    json_t *zeroCrossingJ = json_object_get(rootJ, "zeroCrossing");
		if (zeroCrossingJ) zeroCrossing = json_is_true(zeroCrossingJ);
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

	float getEnv(const float t,const bool r) {
		if (attack>0 && t>=0.0f && t<=attack && !r) {
			return getYBez(t/attack,init,attackSlope*(peak-init)+init,peak);
		}
		else if (decay>0 && t>attack && t<=(attack+decay) && !r) {
			return getYBez((t-attack)/decay,peak,decaySlope*(sustain-peak)+peak,sustain);
		}
		else if (r && t<=release) {
			return getYBez(t/release,sustain,releaseSlope*sustain,0.0f);
		}
		else if (r && t>release) {
			return 0.0f;
		}
		return sustain;
	}

	void onSampleRateChange() override {
		if (!lastPath.empty()) loadSample();
	}

	int getSnappedIndex(float p, bool forward, bool zercoCross) {
		int idx = p*(totalSampleCount-1)*0.1f;
    if (!zeroCrossing) return idx;
		if (forward) {
			while ((sample[idx]*sample[idx+1])>0 && (idx<totalSampleCount-1)) {
				idx=idx+1;
			}
		}
		else {
			while ((sample[idx]*sample[idx+1])>0 && (idx>=0)) {
				idx=idx-1;
			}
		}
		return idx;
	}

	int revIndex(const int i) {
		return totalSampleCount-i-1;
	}

	void updatePoints() {
    if (totalSampleCount>0) {
        sampleStart = getSnappedIndex(clamp(params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage(),0.0f,10.0f), true, zeroCrossing);
        if (params[LINKTYPE_PARAM].getValue()==0.0f) {
          sampleEnd = std::max(getSnappedIndex(clamp(params[SAMPLEEND_PARAM].getValue()+inputs[SAMPLEEND_INPUT].getVoltage(),0.0f,10.0f), false, zeroCrossing), sampleStart);
          loopStart = std::min(std::max(getSnappedIndex(clamp(params[LOOPSTART_PARAM].getValue()+inputs[LOOPSTART_INPUT].getVoltage(),0.0f,10.0f), true, zeroCrossing), sampleStart), sampleEnd);
          loopEnd = std::min(std::max(getSnappedIndex(clamp(params[LOOPEND_PARAM].getValue()+inputs[LOOPEND_INPUT].getVoltage(),0.0f,10.0f), false, zeroCrossing), loopStart), sampleEnd);
          releaseStart = std::min(std::max(getSnappedIndex(clamp(params[RELEASESTART_PARAM].getValue()+inputs[RELEASESTART_INPUT].getVoltage(),0.0f,10.0f), true, zeroCrossing), sampleStart), sampleEnd);
        }
        else if (params[LINKTYPE_PARAM].getValue()==1.0f) {
          sampleEnd = std::max(getSnappedIndex(clamp(params[SAMPLEEND_PARAM].getValue()+inputs[SAMPLEEND_INPUT].getVoltage()+params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage(),0.0f,10.0f), false, zeroCrossing), sampleStart);
          loopStart = std::min(std::max(getSnappedIndex(clamp(params[LOOPSTART_PARAM].getValue()+inputs[LOOPSTART_INPUT].getVoltage()+params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage(),0.0f,10.0f), true, zeroCrossing), sampleStart), sampleEnd);
          loopEnd = std::min(std::max(getSnappedIndex(clamp(params[LOOPEND_PARAM].getValue()+inputs[LOOPEND_INPUT].getVoltage()+params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage(),0.0f,10.0f), false, zeroCrossing), loopStart), sampleEnd);
          releaseStart = std::min(std::max(getSnappedIndex(clamp(params[RELEASESTART_PARAM].getValue()+inputs[RELEASESTART_INPUT].getVoltage()+params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage(),0.0f,10.0f), true, zeroCrossing), sampleStart), sampleEnd);
        }
        else if (params[LINKTYPE_PARAM].getValue()==2.0f) {
          float coef = 1+(float)sampleStart/(float)totalSampleCount;
          sampleEnd = std::max(getSnappedIndex(clamp(params[SAMPLEEND_PARAM].getValue()+inputs[SAMPLEEND_INPUT].getVoltage()+(params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage())*coef,0.0f,10.0f), false, zeroCrossing), sampleStart);
          loopStart = std::min(std::max(getSnappedIndex(clamp(params[LOOPSTART_PARAM].getValue()+inputs[LOOPSTART_INPUT].getVoltage()+(params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage())*coef,0.0f,10.0f), true, zeroCrossing), sampleStart), sampleEnd);
          loopEnd = std::min(std::max(getSnappedIndex(clamp(params[LOOPEND_PARAM].getValue()+inputs[LOOPEND_INPUT].getVoltage()+(params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage())*coef,0.0f,10.0f), false, zeroCrossing), loopStart), sampleEnd);
          releaseStart = std::min(std::max(getSnappedIndex(clamp(params[RELEASESTART_PARAM].getValue()+inputs[RELEASESTART_INPUT].getVoltage()+(params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage())*coef,0.0f,10.0f), true, zeroCrossing), sampleStart), sampleEnd);
        }
        else {
          float coef = 1-(float)sampleStart/(float)totalSampleCount;
          sampleEnd = std::max(getSnappedIndex(clamp(params[SAMPLEEND_PARAM].getValue()+inputs[SAMPLEEND_INPUT].getVoltage()+(params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage())*coef,0.0f,10.0f), false, zeroCrossing), sampleStart);
          loopStart = std::min(std::max(getSnappedIndex(clamp(params[LOOPSTART_PARAM].getValue()+inputs[LOOPSTART_INPUT].getVoltage()+(params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage())*coef,0.0f,10.0f), true, zeroCrossing), sampleStart), sampleEnd);
          loopEnd = std::min(std::max(getSnappedIndex(clamp(params[LOOPEND_PARAM].getValue()+inputs[LOOPEND_INPUT].getVoltage()+(params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage())*coef,0.0f,10.0f), false, zeroCrossing), loopStart), sampleEnd);
          releaseStart = std::min(std::max(getSnappedIndex(clamp(params[RELEASESTART_PARAM].getValue()+inputs[RELEASESTART_INPUT].getVoltage()+(params[SAMPLESTART_PARAM].getValue()+inputs[SAMPLESTART_INPUT].getVoltage())*coef,0.0f,10.0f), true, zeroCrossing), sampleStart), sampleEnd);
        }
      }
    }

};

void EDSAROS::loadSample() {
	APP->engine->yieldWorkers();
	mylock.lock();
	loadingBuffer = waves::getMonoWav(lastPath, APP->engine->getSampleRate(), waveFileName, waveExtension, channels, sampleRate, totalSampleCount);
	if (loadingBuffer.size()>0) {
		sample = new float[2*totalSampleCount];
		rev_sample = new float[2*totalSampleCount];

		for (int i=0; i<totalSampleCount; i++) {
			sample[i]=loadingBuffer[i].samples[0];
			sample[i+totalSampleCount]=loadingBuffer[i].samples[0];
			rev_sample[i]=loadingBuffer[totalSampleCount-i-1].samples[0];
			rev_sample[i+totalSampleCount]=loadingBuffer[totalSampleCount-i-1].samples[0];
		}

		mip_map.init_sample (
			2*totalSampleCount,
			rspl::InterpPack::get_len_pre (),
			rspl::InterpPack::get_len_post (),
			12,
			rspl::ResamplerFlt::_fir_mip_map_coef_arr,
			rspl::ResamplerFlt::MIP_MAP_FIR_LEN
		);

		mip_map.fill_sample (&sample[0], 2*totalSampleCount);

		rev_mip_map.init_sample (
			2*totalSampleCount,
			rspl::InterpPack::get_len_pre (),
			rspl::InterpPack::get_len_post (),
			12,
			rspl::ResamplerFlt::_fir_mip_map_coef_arr,
			rspl::ResamplerFlt::MIP_MAP_FIR_LEN
		);

		rev_mip_map.fill_sample (&rev_sample[0], 2*totalSampleCount);

		for (int i=0; i<16; i++) {
			voices[i].set_sample (mip_map);
			voices[i].set_interp (interp_pack);
			voices[i].clear_buffers ();
			rev_voices[i].set_sample (rev_mip_map);
			rev_voices[i].set_interp (interp_pack);
			rev_voices[i].clear_buffers ();
		}

	}

	mylock.unlock();
	loading = false;
}

void EDSAROS::process(const ProcessArgs &args) {
	if (loading) {
		loadSample();
	}

  if (zeroCrossingTrigger.process(params[ZEROCROSSING_PARAM].getValue())) {
    zeroCrossing=!zeroCrossing;
  }

  lights[ZEROCROSSING_LIGHT].setBrightness(zeroCrossing?1:0);

	attack = clamp(params[ATTACK_PARAM].getValue()+rescale(inputs[ATTACK_INPUT].getVoltage(),0.0f,10.0f,0.0f,20.0f),0.0f,20.0f);
	decay = clamp(params[DECAY_PARAM].getValue()+rescale(inputs[DECAY_INPUT].getVoltage(),0.0f,10.0f,0.0f,60.0f),0.0f,60.0f);
	release = clamp(params[RELEASE_PARAM].getValue()+rescale(inputs[RELEASE_INPUT].getVoltage(),0.0f,10.0f,0.0f,60.0f),0.0f,60.0f);
	init = clamp(params[INIT_PARAM].getValue()+rescale(inputs[INIT_INPUT].getVoltage(),0.0f,10.0f,0.0f,1.0f),0.0f,1.0f);
	peak = clamp(params[PEAK_PARAM].getValue()+rescale(inputs[PEAK_INPUT].getVoltage(),0.0f,10.0f,0.0f,1.0f),0.0f,1.0f);
	sustain = clamp(params[SUSTAIN_PARAM].getValue()+rescale(inputs[SUSTAIN_INPUT].getVoltage(),0.0f,10.0f,0.0f,1.0f),0.0f,1.0f);
	attackSlope = clamp(params[ATTACKSLOPE_PARAM].getValue()+rescale(inputs[ATTACKSLOPE_INPUT].getVoltage(),-10.0f,10.0f,-1.0f,1.0f),-1.0f,1.0f);
	decaySlope = clamp(params[DECAYSLOPE_PARAM].getValue()+rescale(inputs[DECAYSLOPE_INPUT].getVoltage(),-10.0f,10.0f,-1.0f,1.0f),-1.0f,1.0f);
	releaseSlope = clamp(params[RELEASESLOPE_PARAM].getValue()+rescale(inputs[RELEASESLOPE_INPUT].getVoltage(),-10.0f,10.0f,-1.0f,1.0f),-1.0f,1.0f);

	updatePoints();

	if (totalSampleCount>0) {
		for (int i=0; i<inputs[PITCH_INPUT].getChannels(); i++) {
			if (inputs[TRIG_INPUT].getVoltage(i)>0.5f) {
				if (!play[i]) {
					voiceTime[i]=0.0f;
					rel[i]=false;
					voices[i].set_playback_pos(static_cast <rspl::Int64> (sampleStart) << 32);
					rev_voices[i].set_playback_pos(static_cast <rspl::Int64> (revIndex(sampleStart)) << 32);
					direction[i]=1;
				}
				play[i]=true;
			}
			else {
				if (play[i]) {
					voiceTime[i]=0.0f;
					rel[i]=true;
					direction[i]=1;
				}
				play[i]= false;
				if (gain[i]==0.0f) {
					rel[i]=false;
					voices[i].set_playback_pos(static_cast <rspl::Int64> (sampleStart) << 32);
					rev_voices[i].set_playback_pos(static_cast <rspl::Int64> (revIndex(sampleStart)) << 32);
					direction[i]=1;
				}
			}

			gain[i] = getEnv(voiceTime[i],rel[i]);

			if (audio[i].size()==0) { feed[i] = true;}

			internalIntegerPosition[i] = voices[i].get_playback_pos() >> 32;
			internalFloatingPosition[i] = voices[i].get_playback_pos() << 32;
			internalIntegerRevPosition[i] = rev_voices[i].get_playback_pos() >> 32;
			internalFloatingRevPosition[i] = rev_voices[i].get_playback_pos() << 32;

			if ((play[i] || rel[i]) && feed[i]) {
				if (params[LOOPMODE_PARAM].getValue()==0.0f && internalIntegerPosition[i]>=sampleEnd) {
					rel[i]=false;
					voices[i].set_playback_pos(static_cast <rspl::Int64> (sampleEnd) << 32);
					rev_voices[i].set_playback_pos(static_cast <rspl::Int64> (revIndex(sampleEnd)) << 32);
				}
				else if (params[LOOPMODE_PARAM].getValue()==1.0f && internalIntegerPosition[i]>=loopEnd && play[i]) {
					voices[i].set_playback_pos(static_cast <rspl::Int64> (loopStart) << 32);
				}
				else if (params[LOOPMODE_PARAM].getValue()==2.0f && direction[i]==1 && internalIntegerPosition[i]>=loopEnd && play[i]) {
					voices[i].set_playback_pos(static_cast <rspl::Int64> (loopStart) << 32);
					rev_voices[i].set_playback_pos(static_cast <rspl::Int64> (revIndex(loopEnd)) << 32);
					direction[i]=-1;
				}
				else if (params[LOOPMODE_PARAM].getValue()==2.0f && direction[i]==-1 && internalIntegerRevPosition[i]>=revIndex(loopStart) && play[i]) {
					voices[i].set_playback_pos(static_cast <rspl::Int64> (loopStart) << 32);
					rev_voices[i].set_playback_pos(static_cast <rspl::Int64> (revIndex(loopEnd)) << 32);
					direction[i]=1;
				}

				if (params[RELEASEMODE_PARAM].getValue()==0.0f && !play[i]) {
					voices[i].set_playback_pos(static_cast <rspl::Int64> (sampleStart) << 32);
					rel[i]=false;
				}
				else if (params[RELEASEMODE_PARAM].getValue()==1.0f && internalIntegerPosition[i]>=sampleEnd) {
					voices[i].set_playback_pos(static_cast <rspl::Int64> (sampleStart) << 32);
					rel[i]=false;
				}
				else if (params[RELEASEMODE_PARAM].getValue()==2.0f && internalIntegerPosition[i]>=sampleEnd && rel[i]) {
					voices[i].set_playback_pos(static_cast <rspl::Int64> (releaseStart) << 32);
				}
				else if (params[RELEASEMODE_PARAM].getValue()==2.0f && internalIntegerPosition[i]>=sampleEnd && !rel[i]) {
					voices[i].set_playback_pos(static_cast <rspl::Int64> (sampleStart) << 32);
				}
				else if (params[RELEASEMODE_PARAM].getValue()==3.0f  && direction[i]==1 && internalIntegerPosition[i]>=sampleEnd && rel[i]) {
					voices[i].set_playback_pos(static_cast <rspl::Int64> (releaseStart) << 32);
					rev_voices[i].set_playback_pos(static_cast <rspl::Int64> (revIndex(sampleEnd)) << 32);
					direction[i]=-1;
				}
				else if (params[RELEASEMODE_PARAM].getValue()==3.0f && direction[i]==-1 && internalIntegerRevPosition[i]>=revIndex(releaseStart) && rel[i]) {
					voices[i].set_playback_pos(static_cast <rspl::Int64> (releaseStart) << 32);
					rev_voices[i].set_playback_pos(static_cast <rspl::Int64> (revIndex(sampleEnd)) << 32);
					direction[i]=1;
				}

				if (play[i] || rel[i]) {
					const long pitch = inputs[PITCH_INPUT].getVoltage(i) * depth;
					voices[i].set_pitch(pitch);
					rev_voices[i].set_pitch(pitch);

					if (direction[i]==1) {
            long nbr_spl;
            if (play[i] && params[LOOPMODE_PARAM].getValue()==0.0f) {
              nbr_spl = rspl::min (SIZE, sampleEnd - internalIntegerPosition[i]);
            }
            else if (play[i] && params[LOOPMODE_PARAM].getValue()>0.0f) {
              nbr_spl = rspl::min (SIZE, loopEnd - internalIntegerPosition[i]);
            }
            else if (rel[i] && params[RELEASEMODE_PARAM].getValue()>0.0f) {
              nbr_spl = rspl::min (SIZE, sampleEnd - internalIntegerPosition[i]);
            }
            else {
              nbr_spl = SIZE;
            }
            if (nbr_spl>0) {
              float *buff = new float[nbr_spl];
  						voices[i].interpolate_block(buff, nbr_spl);
              for (int j=0; j<nbr_spl; j++) {
    						*(audio[i].endData()+j) = buff[j];
    					}
    					audio[i].endIncr(nbr_spl);
            }
					}
					else {
            long nbr_spl;
            if (play[i] && params[LOOPMODE_PARAM].getValue()==2.0f) {
              nbr_spl = rspl::min (SIZE, revIndex(loopStart) - internalIntegerRevPosition[i]);
            }
            else if (rel[i] && params[RELEASEMODE_PARAM].getValue()>0.0f) {
              nbr_spl = rspl::min (SIZE, revIndex(releaseStart) - internalIntegerRevPosition[i]);
            }
            else {
              nbr_spl = SIZE;
            }
            if (nbr_spl>0) {
              float *buff = new float[nbr_spl];
              rev_voices[i].interpolate_block(buff, nbr_spl);
              for (int j=0; j<nbr_spl; j++) {
                *(audio[i].endData()+j) = buff[j];
              }
              audio[i].endIncr(nbr_spl);
            }
					}

					feed[i] = false;
				}
			}
			else if (feed[i]) {
				for (int j=0; j<SIZE; j++) {
					*(audio[i].endData()+j) = 0.0f;
				}
				audio[i].endIncr(SIZE);
				feed[i] = false;
			}

			voiceTime[i]+= play[i] || rel[i] ? args.sampleTime : 0.0f;

			if (audio[i].size()>=1) {
				if (((loopStart==loopEnd) && (internalIntegerPosition[i]>=loopStart)) || ((releaseStart==sampleEnd) && (internalIntegerPosition[i]>=releaseStart))) {
					outputs[OUT].setVoltage(0.0f);
					audio[i].startIncr(1);
				}
				else {
					outputs[OUT].setVoltage(*audio[i].startData()*5.0f*gain[i]*params[GAIN_PARAM].getValue(),i);
					audio[i].startIncr(1);
				}
			}
			else {
				outputs[OUT].setVoltage(0.0f,i);
			}
		}

		outputs[OUT].setChannels(inputs[PITCH_INPUT].getChannels());
	}
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
			int releaseType = (int)module->params[EDSAROS::LINKTYPE_PARAM].getValue();
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

	void drawSample(const DrawArgs &args) {
		if (module->loadingBuffer.size()>0) {
			module->mylock.lock();
  		std::vector<float> vL;
			for (int i=0;i<module->totalSampleCount;i++) {
				vL.push_back(module->loadingBuffer[i].samples[0]*module->params[EDSAROS::GAIN_PARAM].getValue());
			}
  		module->mylock.unlock();
  		size_t nbSample = vL.size();

  		if (nbSample>0) {
				nvgSave(args.vg);
				Rect b = Rect(Vec(zoomLeftAnchor, 0), Vec(zoomWidth, height));
				size_t inc = std::max(vL.size()/zoomWidth/4,1.f);
  			nvgScissor(args.vg, -0.5f, b.pos.y-0.5f, width+1.0f, height+1.0f);

				nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 255));
				nvgBeginPath(args.vg);
				nvgMoveTo(args.vg, 0, height/2.0f);
				nvgLineTo(args.vg, width, height/2.0f);
				nvgClosePath(args.vg);
				nvgStroke(args.vg);

  			nvgStrokeColor(args.vg, nvgRGBA(164, 3, 111, 200));

  			nvgBeginPath(args.vg);
  			for (size_t i = 0; i < vL.size(); i+=inc) {
  				float x, y;
  				x = (float)i/vL.size();
  				y = (-1.f)*vL[i] / 2.0f + 0.5f;
  				Vec p;
  				p.x = b.pos.x + b.size.x * x;
  				p.y = b.pos.y + b.size.y * (1.0f - y);
  				if (i == 0) {
  					nvgMoveTo(args.vg, p.x, p.y);
  				}
  				else {
  					nvgLineTo(args.vg, p.x, p.y);
  				}
  			}
  			nvgLineCap(args.vg, NVG_MITER);
  			nvgStrokeWidth(args.vg, 1);
  			nvgStroke(args.vg);

	  		if (nbSample>0) {
	  			nvgStrokeColor(args.vg, LIGHTBLUE_BIDOO);
	  			{
	  				nvgBeginPath(args.vg);
	  				nvgStrokeWidth(args.vg, 1);
  					nvgMoveTo(args.vg, (module->direction[0] == 1 ? module->internalIntegerPosition[0] : module->revIndex(module->internalIntegerRevPosition[0])) * zoomWidth / nbSample + zoomLeftAnchor, 0);
  					nvgLineTo(args.vg, (module->direction[0] == 1 ? module->internalIntegerPosition[0] : module->revIndex(module->internalIntegerRevPosition[0])) * zoomWidth / nbSample + zoomLeftAnchor, height);
	  				nvgClosePath(args.vg);
	  			}
	  			nvgStroke(args.vg);
	  		}

	  		if (nbSample>0) {
					nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
					nvgFontSize(args.vg, 4.0f);
  				nvgStrokeColor(args.vg, YELLOW_BIDOO);
					nvgFillColor(args.vg, YELLOW_BIDOO);
					nvgStrokeWidth(args.vg, 1);

					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, module->sampleStart * zoomWidth / nbSample + zoomLeftAnchor, 10);
					nvgLineTo(args.vg, module->sampleStart * zoomWidth / nbSample + zoomLeftAnchor, height);
					nvgClosePath(args.vg);
					nvgStroke(args.vg);
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, module->sampleStart * zoomWidth / nbSample + zoomLeftAnchor, 10);
					nvgLineTo(args.vg, module->sampleStart * zoomWidth / nbSample + zoomLeftAnchor+5, 13);
					nvgLineTo(args.vg, module->sampleStart * zoomWidth / nbSample + zoomLeftAnchor, 16);
					nvgLineTo(args.vg, module->sampleStart * zoomWidth / nbSample + zoomLeftAnchor, 10);
					nvgClosePath(args.vg);
					nvgStroke(args.vg);
					nvgFill(args.vg);

					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, module->loopStart * zoomWidth / nbSample + zoomLeftAnchor, 0);
					nvgLineTo(args.vg, module->loopStart * zoomWidth / nbSample + zoomLeftAnchor, height);
					nvgClosePath(args.vg);
					nvgStroke(args.vg);
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, module->loopStart * zoomWidth / nbSample + zoomLeftAnchor, 0);
					nvgLineTo(args.vg, module->loopStart * zoomWidth / nbSample + zoomLeftAnchor+5, 3);
					nvgLineTo(args.vg, module->loopStart * zoomWidth / nbSample + zoomLeftAnchor, 6);
					nvgLineTo(args.vg, module->loopStart * zoomWidth / nbSample + zoomLeftAnchor, 0);
					nvgClosePath(args.vg);
					nvgStroke(args.vg);
					nvgFill(args.vg);

					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, module->loopEnd * zoomWidth / nbSample + zoomLeftAnchor, 0);
					nvgLineTo(args.vg, module->loopEnd * zoomWidth / nbSample + zoomLeftAnchor, height);
					nvgClosePath(args.vg);
					nvgStroke(args.vg);
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, module->loopEnd * zoomWidth / nbSample + zoomLeftAnchor, 0);
					nvgLineTo(args.vg, module->loopEnd * zoomWidth / nbSample + zoomLeftAnchor-5, 3);
					nvgLineTo(args.vg, module->loopEnd * zoomWidth / nbSample + zoomLeftAnchor, 6);
					nvgLineTo(args.vg, module->loopEnd * zoomWidth / nbSample + zoomLeftAnchor, 0);
					nvgClosePath(args.vg);
					nvgStroke(args.vg);
					nvgFill(args.vg);

					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, module->releaseStart * zoomWidth / nbSample + zoomLeftAnchor, 10);
					nvgLineTo(args.vg, module->releaseStart * zoomWidth / nbSample + zoomLeftAnchor, height);
					nvgClosePath(args.vg);
					nvgStroke(args.vg);
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, module->releaseStart * zoomWidth / nbSample + zoomLeftAnchor, 10);
					nvgLineTo(args.vg, module->releaseStart * zoomWidth / nbSample + zoomLeftAnchor+5, 13);
					nvgLineTo(args.vg, module->releaseStart * zoomWidth / nbSample + zoomLeftAnchor, 16);
					nvgLineTo(args.vg, module->releaseStart * zoomWidth / nbSample + zoomLeftAnchor, 10);
					nvgClosePath(args.vg);
					nvgStroke(args.vg);
					nvgFill(args.vg);

					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, module->sampleEnd * zoomWidth / nbSample + zoomLeftAnchor, 10);
					nvgLineTo(args.vg, module->sampleEnd * zoomWidth / nbSample + zoomLeftAnchor, height);
					nvgClosePath(args.vg);
					nvgStroke(args.vg);
					nvgBeginPath(args.vg);
					nvgMoveTo(args.vg, module->sampleEnd * zoomWidth / nbSample + zoomLeftAnchor, 10);
					nvgLineTo(args.vg, module->sampleEnd * zoomWidth / nbSample + zoomLeftAnchor-5, 13);
					nvgLineTo(args.vg, module->sampleEnd * zoomWidth / nbSample + zoomLeftAnchor, 16);
					nvgLineTo(args.vg, module->sampleEnd * zoomWidth / nbSample + zoomLeftAnchor, 10);
					nvgClosePath(args.vg);
					nvgStroke(args.vg);
					nvgFill(args.vg);
  			}

				nvgResetScissor(args.vg);
				nvgRestore(args.vg);
  		}
    }
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
		EDSAROS *module = dynamic_cast<EDSAROS*>(this->getParamQuantity()->module);
		module->showSample = showSample;
	}
};

struct EDSAROSBidooSmallSnapBlueKnob : BidooSmallSnapBlueKnob {
	bool showSample = false;

	void onButton(const ButtonEvent& e) override {
		BidooSmallSnapBlueKnob::onButton(e);
		EDSAROS *module = dynamic_cast<EDSAROS*>(this->getParamQuantity()->module);
		module->showSample = showSample;
	}
};


struct EDSAROSWidget : ModuleWidget {
	EDSAROSWidget(EDSAROS *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/EDSAROS.svg")));

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
		addInput(createInput<TinyPJ301MPort>(Vec(controlsXAnchor+2*controlsXOffset+inputsXOffset, controlsYAnchor+5*controlsYOffset), module, EDSAROS::RELEASESLOPE_PARAM));

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
  			module->lastPath = path;
				module->loading=true;
  			free(path);
  		}
  	}
  };

  void appendContextMenu(ui::Menu *menu) override {
		EDSAROS *module = dynamic_cast<EDSAROS*>(this->module);
		assert(module);
		menu->addChild(construct<MenuLabel>());
		menu->addChild(construct<EDSAROSItem>(&MenuItem::text, "Load sample", &EDSAROSItem::module, module));
	}

	void onPathDrop(const PathDropEvent& e) override {
		Widget::onPathDrop(e);
		EDSAROS *module = dynamic_cast<EDSAROS*>(this->module);
		module->lastPath = e.paths[0];
		module->loading=true;
	}
};

Model *modelEDSAROS = createModel<EDSAROS, EDSAROSWidget>("eDsaroS");
