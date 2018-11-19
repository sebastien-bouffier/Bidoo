#include "dsp/resampler.hpp"
#include "dsp/fir.hpp"
#include "../pffft/pffft.h"
#include <algorithm>
#include <iostream>
#include <fstream>
#include <atomic>

#define FS 2048
#define AC 2.0*M_PI/FS
#define FS2 1024
#define NF 256
#define IFS 1.0f/FS
#define IFS2 1.0f/FS2
#define IM_PI 1.0f/M_PI

using namespace std;

struct wtFrame {
  vector<float> sample;
  vector<float> magnitude;
  vector<float> phase;
  bool morphed=false;

  wtFrame() {
    sample.resize(FS,0);
    magnitude.resize(FS2,0);
    phase.resize(FS2,0);
  }

  void calcFFT();
  void calcIFFT();
  void calcWav();
  void normalize();
  void smooth();
  void window();
  void removeDCOffset();
  void loadSample(size_t sCount, bool interpolate, float *wav);
  void loadMagnitude(size_t sCount, bool interpolate, float *magn);
  float maxAmp();
  void gain(float g);
};

void wtFrame::calcFFT() {
  PFFFT_Setup *pffftSetup;
	pffftSetup = pffft_new_setup(FS, PFFFT_REAL);
	float *fftIn;
	float *fftOut;
	fftIn = (float*)pffft_aligned_malloc(FS*sizeof(float));
	fftOut = (float*)pffft_aligned_malloc(FS*sizeof(float));
	memset(fftIn, 0, FS*sizeof(float));
	memset(fftOut, 0, FS*sizeof(float));

	for (size_t k = 0; k < FS; k++) {
		fftIn[k] = sample[k];
	}

	pffft_transform_ordered(pffftSetup, fftIn, fftOut, 0, PFFFT_FORWARD);

	for (size_t k = 0; k < FS2; k++) {
		if ((abs(fftOut[2*k])>1e-2f) || (abs(fftOut[2*k+1])>1e-2f)) {
			float real = fftOut[2*k];
			float imag = fftOut[2*k+1];
			phase[k] = atan2(imag,real);
			magnitude[k] = 2.0f*sqrt(real*real+imag*imag)/FS;
		}
    else {
      phase[k] = 0.0f;
			magnitude[k] = 0.0f;
    }
	}

	pffft_destroy_setup(pffftSetup);
	pffft_aligned_free(fftIn);
	pffft_aligned_free(fftOut);
}

void wtFrame::calcIFFT() {
  PFFFT_Setup *pffftSetup;
	pffftSetup = pffft_new_setup(FS, PFFFT_REAL);
	float *fftIn;
	float *fftOut;
	fftIn = (float*)pffft_aligned_malloc(FS*sizeof(float));
	fftOut =  (float*)pffft_aligned_malloc(FS*sizeof(float));
	memset(fftIn, 0, FS*sizeof(float));
	memset(fftOut, 0, FS*sizeof(float));

	for (size_t i = 0; i < FS2; i++) {
		fftIn[2*i] = magnitude[i]*cos(phase[i]);
		fftIn[2*i+1] = magnitude[i]*sin(phase[i]);
	}

	pffft_transform_ordered(pffftSetup, fftIn, fftOut, 0, PFFFT_BACKWARD);

	for (size_t i = 0; i < FS; i++) {
		sample[i]=fftOut[i]*0.5f;
	}

	pffft_destroy_setup(pffftSetup);
	pffft_aligned_free(fftIn);
	pffft_aligned_free(fftOut);
}

void wtFrame::calcWav() {
  for(size_t i = 0 ; i < FS; i++) {
    sample[i] = 0;
		for(size_t j = 0; j < FS2; j++) {
			if (magnitude[j]>0) {
        sample[i] += magnitude[j] * cos(i*j*AC + phase[j]);
      }
		}
  }
}

inline void wtFrame::normalize() {
  float amp = maxAmp();
  float g= amp>0?1.0f/amp:0.0f;
	gain(g);
}

inline void wtFrame::smooth() {
  for(size_t i=0; i<16;i++) {
    float avg=(sample[i]+sample[FS-i-1])/2.0f;
    sample[i]= ((16-i)*avg+i*sample[i])/16.0f;
    sample[FS-i-1]= ((16-i)*avg+i*sample[FS-i-1])/16.0f;
  }
}

inline void wtFrame::window() {
  for (size_t i = 0; i < FS;i++) {
		float window = min(-10.0f * cos(2.0f * M_PI * (double)i / FS) + 10.0f, 1.0f);
		sample[i] *= window;
	}
}

void wtFrame::removeDCOffset() {
  calcFFT();
  magnitude[0]=0.0f;
  calcIFFT();
}

void wtFrame::loadSample(size_t sCount, bool interpolate, float *wav) {
  if (interpolate) {
    for(size_t i=0;i<FS;i++) {
      size_t index = i*((float)max(sCount-1,0)/(float)FS);
      float pos = (float)i*((float)max(sCount-1,0)/(float)FS);
      sample[i]=rescale(pos,index,index+1,*(wav+index),*(wav+index+1));
    }
  }
  else {
    for(size_t i=0;i<FS;i++) {
      if (i<sCount) {
        sample[i]=*(wav+i);
      }
      else {
        sample[i]=0.0f;
      }
    }
  }
}

inline float wtFrame::maxAmp() {
  float amp = 0.0f;
	for(size_t i = 0 ; i < FS; i++) {
		amp = max(amp,abs(sample[i]));
	}
	return amp;
}

inline void wtFrame::gain(float g) {
	for(size_t i = 0 ; i < FS; i++) {
		sample[i]*=g;
	}
}

struct wtTable {
  std::vector<wtFrame> frames;

  wtTable() {

  }

  void loadSample(size_t sCount, size_t frameSize, bool interpolate, float *sample);
  void loadMagnitude(size_t sCount, size_t frameSize, bool interpolate, float *magn);
  void normalize();
  void normalizeFrame(size_t index);
  void normalizeAllFrames();
  void smooth();
  void smoothFrame(size_t index);
  void window();
  void windowFrame(size_t index);
  void removeDCOffset();
  void removeFrameDCOffset(size_t index);
  void addFrame(size_t index);
  void removeFrame(size_t index);
  void morphFrames();
  void morphSpectrum();
  void morphSpectrumConstantPhase();
  void reset();
  void deleteMorphing();
  void init();
};

void wtTable::loadSample(size_t sCount, size_t frameSize, bool interpolate, float *sample) {
  frames.clear();
  size_t sUsed=0;
  while ((sUsed != sCount) && (frames.size()<NF)) {
    size_t lenFrame = min(frameSize,sCount-sUsed);
    wtFrame frame;
    frame.loadSample(lenFrame,interpolate,sample+sUsed);
    frames.push_back(frame);
    sUsed+=lenFrame;
  }
}

void wtTable::normalize() {
  float amp = 0.0f;
  for(size_t i=0; i<frames.size(); i++) {
    amp = max(amp,frames[i].maxAmp());
  }
  float g=amp>0?1.0f/amp:0.0f;
  for(size_t i=0; i<frames.size(); i++) {
    frames[i].gain(g);
  }
}

inline void wtTable::normalizeFrame(size_t index) {
  frames[index].normalize();
}

inline void wtTable::normalizeAllFrames() {
  for(size_t i=0; i<frames.size(); i++) {
    frames[i].normalize();
  }
}

inline void wtTable::smooth() {
  for(size_t i=0; i<frames.size();i++) {
    frames[i].smooth();
  }
}

inline void wtTable::smoothFrame(size_t index) {
  frames[index].smooth();
}

inline void wtTable::window() {
  for(size_t i=0; i<frames.size();i++) {
    frames[i].window();
  }
}

inline void wtTable::windowFrame(size_t index) {
  frames[index].window();
}

inline void wtTable::removeDCOffset() {
  for(size_t i=0; i<frames.size();i++) {
    frames[i].removeDCOffset();
  }
}

inline void wtTable::removeFrameDCOffset(size_t index) {
  frames[index].removeDCOffset();
}

inline void wtTable::addFrame(size_t index) {
  if (frames.size()<NF) {
    wtFrame nFrame;
    if ((frames.size()>1) && (index<frames.size()-1)) {
      frames.insert(frames.begin()+index+1,nFrame);
    }
    else {
      frames.push_back(nFrame);
    }
  }
}

inline void wtTable::removeFrame(size_t index) {
  if ((frames.size()>0) && (index<frames.size())) {
    frames.erase(frames.begin()+index);
  }
}

void wtTable::morphFrames() {
  deleteMorphing();
  if (frames.size()>1) {
    size_t fs = frames.size();
    size_t fCount = (NF - fs)/(fs-1);
    for (size_t i=0; i<(fs-1); i++) {
      for (size_t j=0; j<fCount; j++) {
        wtFrame frame;
        frame.morphed=true;
        for(size_t k=0; k<FS; k++) {
          frame.sample[k]=rescale(j+1,0,fCount+1,frames[i*(fCount+1)].sample[k],frames[i*(fCount+1)+j+1].sample[k]);
        }
        frames.insert(frames.begin()+i*(fCount+1)+j+1, frame);
      }
    }
  }
}

void wtTable::morphSpectrum() {
  deleteMorphing();
  if (frames.size()>1) {
    for(size_t i=0; i<frames.size(); i++) {
      frames[i].calcFFT();
    }
    size_t fs = frames.size();
    size_t fCount = (NF - fs)/(fs-1);
    for (size_t i=0; i<(fs-1); i++) {
      for (size_t j=0; j<fCount; j++) {
        wtFrame frame;
        frame.morphed=true;
        for(size_t k=0; k<FS2; k++) {
          frame.magnitude[k]=rescale(j+1,0,fCount+1,frames[i*(fCount+1)].magnitude[k],frames[i*(fCount+1)+j+1].magnitude[k]);
          frame.phase[k]=rescale(j+1,0,fCount+1,frames[i*(fCount+1)].phase[k],frames[i*(fCount+1)+j+1].phase[k]);
        }
        frame.calcIFFT();
        frames.insert(frames.begin()+i*(fCount+1)+j+1, frame);
      }
    }
  }
}

void wtTable::morphSpectrumConstantPhase() {
  if (frames.size()>1) {
    for(size_t i=0; i<frames.size(); i++) {
      frames[i].calcFFT();
      if (i>0) {
        for(size_t k=0; k<FS2; k++) {
          frames[i].phase[k]=frames[0].phase[k];
        }
        frames[i].calcIFFT();
      }
    }
    size_t fs = frames.size();
    size_t fCount = (NF - fs)/(fs-1);
    for (size_t i=0; i<(fs-1); i++) {
      for (size_t j=0; j<fCount; j++) {
        wtFrame frame;
        frame.morphed=true;
        for(size_t k=0; k<FS2; k++) {
          frame.magnitude[k]=rescale(j+1,0,fCount+1,frames[i*(fCount+1)].magnitude[k],frames[i*(fCount+1)+j+1].magnitude[k]);
          frame.phase[k]=frames[0].phase[k];
        }
        frame.calcIFFT();
        frames.insert(frames.begin()+i*(fCount+1)+j+1, frame);
      }
    }
  }
}

inline void wtTable::reset() {
  frames.clear();
}

inline void wtTable::init() {
  reset();
  for(size_t i=0; i<NF; i++) {
    wtFrame frame;
    frames.push_back(frame);
  }
}

inline void wtTable::deleteMorphing() {
  frames.erase(std::remove_if(
    frames.begin(), frames.end(),
    [](const wtFrame& x) {
        return x.morphed;
    }), frames.end());
}

struct wtOscillator {
  wtTable table;

  wtOscillator() {

  }

	bool soft = false;
	float lastSyncValue = 0.0f;
	float phase = 0.0f;
	float freq;
	float pitch;
	bool syncEnabled = false;
	bool syncDirection = false;
	Decimator<16, 16> wavDecimator;
	float pitchSlew = 0.0f;
	int pitchSlewIndex = 0;
	size_t pIndex=0;
	float wavBuffer[16] = {};
  float pValue = 0.0f;

	void setPitch(float pitchKnob, float pitchCv) {
		pitch = pitchKnob;
		pitch = roundf(pitch);
		pitch += pitchCv;
		freq = 261.626f * powf(2.0f, pitch / 12.0f);
	}

	void process(float deltaTime, float syncValue, float index, std::mutex &mtx) {
		// Advance phase
		float deltaPhase = clamp(freq * deltaTime, 1e-6, 0.5f);

		// Detect sync
		int syncIndex = -1; // Index in the oversample loop where sync occurs [0, OVERSAMPLE)
		float syncCrossing = 0.0f; // Offset that sync occurs [0.0f, 1.0f)
		if (syncEnabled) {
			syncValue -= 0.01f;
			if (syncValue > 0.0f && lastSyncValue <= 0.0f) {
				float deltaSync = syncValue - lastSyncValue;
				syncCrossing = 1.0f - syncValue / deltaSync;
				syncCrossing *= 16;
				syncIndex = (int)syncCrossing;
				syncCrossing -= syncIndex;
			}
			lastSyncValue = syncValue;
		}

		if (syncDirection)
			deltaPhase *= -1.0f;

		for (int i = 0; i < 16; i++) {
			if (syncIndex == i) {
				if (soft) {
					syncDirection = !syncDirection;
					deltaPhase *= -1.0f;
				}
				else {
					phase = 0.0f;
				}
			}

      {
        std::lock_guard<std::mutex> lck {mtx};
        size_t idx = index*(table.frames.size()-1);
        wavBuffer[i] = idx<table.frames.size() ? 1.66f * interpolateLinear(table.frames[idx].sample.data(), phase * 2047.f) : pValue;
        pValue = wavBuffer[i];
      }

			phase += deltaPhase / 16;
			phase = eucmod(phase, 1.0f);
		}

	}

	float out() {
		return wavDecimator.process(wavBuffer);
	}
};
