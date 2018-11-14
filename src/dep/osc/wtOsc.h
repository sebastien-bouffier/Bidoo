#include "dsp/resampler.hpp"
#include "dsp/fir.hpp"
#include "../pffft/pffft.h"
#include <algorithm>
#include <iostream>
#include <fstream>

#define FS 2048
#define AC 2.0*M_PI/FS
#define FS2 1024

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
  std::fill(magnitude.begin(), magnitude.end(), 0);
  std::fill(phase.begin(), phase.end(), 0);
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

void wtFrame::normalize() {
  float amp = maxAmp();
  float g= amp>0?1.0f/amp:0.0f;
	gain(g);
}

void wtFrame::smooth() {
  for(size_t i=0; i<16;i++) {
    float avg=(sample[i]+sample[FS-i-1])/2.0f;
    sample[i]= ((16-i)*avg+i*sample[i])/16.0f;
    sample[FS-i-1]= ((16-i)*avg+i*sample[FS-i-1])/16.0f;
  }
}

void wtFrame::window() {
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
  std::fill(sample.begin(), sample.end(), 0);
  if (interpolate) {
    for(size_t i=0;i<FS;i++) {
      size_t index = i*((float)max(sCount-1,0)/(float)FS);
      float pos = (float)i*((float)max(sCount-1,0)/(float)FS);
      sample[i]=rescale(pos,index,index+1,*(wav+index),*(wav+index+1));
    }
  }
  else {
    for(size_t i=0;i<sCount;i++) {
      sample[i]=*(wav+i);
    }
  }
}

float wtFrame::maxAmp() {
  float amp = 0.0f;
	for(size_t i = 0 ; i < FS; i++) {
		amp = max(amp,abs(sample[i]));
	}
	return amp;
}

void wtFrame::gain(float g) {
	for(size_t i = 0 ; i < FS; i++) {
		sample[i]*=g;
	}
}

struct wtTable {
  std::vector<wtFrame> frames;
  mutex mtx;

  void loadSample(size_t sCount, size_t frameSize, bool interpolate, float *sample);
  void normalize();
  void normalizeFrame(size_t index);
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
  void reset();
  void deleteMorphing();
  void init();
};

void wtTable::loadSample(size_t sCount, size_t frameSize, bool interpolate, float *sample) {
  reset();

  mtx.lock();
  size_t sUsed=0;
  while ((sUsed != sCount) && (frames.size()<256)) {
    size_t lenFrame = min(frameSize,sCount-sUsed);
    wtFrame frame;
    frame.loadSample(lenFrame,interpolate,sample+sUsed);
    frames.push_back(frame);
    sUsed+=lenFrame;
  }
  mtx.unlock();
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

void wtTable::normalizeFrame(size_t index) {
  frames[index].normalize();
}

void wtTable::smooth() {
  for(size_t i=0; i<frames.size();i++) {
    frames[i].smooth();
  }
}

void wtTable::smoothFrame(size_t index) {
  frames[index].smooth();
}

void wtTable::window() {
  for(size_t i=0; i<frames.size();i++) {
    frames[i].window();
  }
}

void wtTable::windowFrame(size_t index) {
  frames[index].window();
}

void wtTable::removeDCOffset() {
  for(size_t i=0; i<frames.size();i++) {
    frames[i].removeDCOffset();
  }
}

void wtTable::removeFrameDCOffset(size_t index) {
  frames[index].removeDCOffset();
}

void wtTable::addFrame(size_t index) {
  wtFrame nFrame;
  frames.insert(frames.begin()+index,nFrame);
}

void wtTable::removeFrame(size_t index) {
  frames.erase(frames.begin()+index);
}

void wtTable::morphFrames() {
  deleteMorphing();

  mtx.lock();
  if (frames.size()>1) {
    size_t fCount = 256 - frames.size();
    size_t fIndex = 0;
    size_t pCount = frames.size()-1;
    size_t cIndex = 0;
    while (fCount>0) {
      wtFrame frame;
      frame.morphed=true;
      for(size_t i=0; i<FS; i++) {
        frame.sample[i]=(frames[fIndex].sample[i] + frames[fIndex+1].sample[i])*0.5f;
      }
      frames.insert(frames.begin()+fIndex+1, frame);
      fIndex += 2;
      cIndex++;
      if (cIndex>=pCount) {
        fIndex=0;
        pCount+=cIndex-1;
        cIndex=0;
      }
      fCount--;
    }
  }
  mtx.unlock();
}

void wtTable::morphSpectrum() {
  deleteMorphing();

  mtx.lock();
  if (frames.size()>1) {
    for(size_t i=0; i<frames.size(); i++) {
      frames[i].calcFFT();
    }
    size_t fCount = 256 - frames.size();
    size_t fIndex = 0;
    size_t pCount = frames.size()-1;
    size_t cIndex = 0;
    while (fCount>0) {
      wtFrame frame;
      frame.morphed=true;
      for(size_t i=0; i<FS2; i++) {
        frame.magnitude[i]=(frames[fIndex].magnitude[i] + frames[fIndex+1].magnitude[i])*0.5f;
        frame.phase[i]=(frames[fIndex].phase[i] + frames[fIndex+1].phase[i])*0.5f;
      }
      frame.calcIFFT();
      frames.insert(frames.begin()+fIndex+1, frame);
      fIndex += 2;
      cIndex++;
      if (cIndex>=pCount) {
        fIndex=0;
        pCount+=cIndex-1;
        cIndex=0;
      }
      fCount--;
    }
  }
  mtx.unlock();
}

void wtTable::reset() {
  mtx.lock();
  frames.clear();
  mtx.unlock();
}

void wtTable::init() {
  reset();
  mtx.lock();
  for(size_t i=0; i<256; i++) {
    wtFrame frame;
    frames.push_back(frame);
  }
  mtx.unlock();
}

void wtTable::deleteMorphing() {
  mtx.lock();
  frames.erase(std::remove_if(
    frames.begin(), frames.end(),
    [](const wtFrame& x) {
        return x.morphed;
    }), frames.end());
  mtx.unlock();
}

struct wtOscillator {
  wtTable table;

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

  wtOscillator() { }

	void setPitch(float pitchKnob, float pitchCv) {
		pitch = pitchKnob;
		pitch = roundf(pitch);
		pitch += pitchCv;
		freq = 261.626f * powf(2.0f, pitch / 12.0f);
	}

	void process(float deltaTime, float syncValue, size_t index) {
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

      if (index<table.frames.size()) {
        table.mtx.lock();
			  wavBuffer[i] = 1.66f * interpolateLinear(table.frames[index].sample.data(), phase * 2047.f);
        table.mtx.unlock();
      }
      else {
        wavBuffer[i] =  0.0f;
      }

			pIndex = index;
			phase += deltaPhase / 16;
			phase = eucmod(phase, 1.0f);
		}

	}

	float out() {
		return wavDecimator.process(wavBuffer);
	}

};
