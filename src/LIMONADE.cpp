#include "Bidoo.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "window.hpp"
#include <thread>
#include "dsp/resampler.hpp"
#include "dsp/fir.hpp"
#include "osdialog.h"
#include "dep/dr_wav/dr_wav.h"
#include "../pffft/pffft.h"
#include <iostream>
#include <fstream>

using namespace std;

#define frameSize 2048
#define aCoeff 2.0*M_PI/frameSize
#define frameSize2 1024

template <int OVERSAMPLE, int QUALITY>
struct WavOscillator {
	bool soft = false;
	float lastSyncValue = 0.0f;
	float phase = 0.0f;
	float freq;
	float pitch;
	bool syncEnabled = false;
	bool syncDirection = false;
	Decimator<OVERSAMPLE, QUALITY> wavDecimator;
	float pitchSlew = 0.0f;
	int pitchSlewIndex = 0;
	size_t pIndex=0;
	float wavBuffer[OVERSAMPLE] = {};

	void setPitch(float pitchKnob, float pitchCv) {
		// Compute frequency
		pitch = pitchKnob;
		pitch = roundf(pitch);
		pitch += pitchCv;
		// Note C4
		freq = 261.626f * powf(2.0f, pitch / 12.0f);
	}

	void process(float deltaTime, float syncValue, float **wt, size_t index) {
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
				syncCrossing *= OVERSAMPLE;
				syncIndex = (int)syncCrossing;
				syncCrossing -= syncIndex;
			}
			lastSyncValue = syncValue;
		}

		if (syncDirection)
			deltaPhase *= -1.0f;

		for (int i = 0; i < OVERSAMPLE; i++) {
			if (syncIndex == i) {
				if (soft) {
					syncDirection = !syncDirection;
					deltaPhase *= -1.0f;
				}
				else {
					// phase = syncCrossing * deltaPhase / OVERSAMPLE;
					phase = 0.0f;
				}
			}

			wavBuffer[i] = 1.66f * interpolateLinear(wt[index], phase * 2047.f);
			pIndex = index;
			phase += deltaPhase / OVERSAMPLE;
			phase = eucmod(phase, 1.0f);
		}
	}

	float wav() {
		return wavDecimator.process(wavBuffer);
	}
};

inline void doWindowing(float *in, float *out) {
	for (size_t k = 0; k < frameSize;k++) {
		float window = min(-10.0f * cos(2.0f * M_PI * (double)k / frameSize) + 10.0f, 1.0f);
		out[k] = in[k] * window;
	}
}

void doFFT(float *frame, float *magn, float *phase) {
	PFFFT_Setup *pffftSetup;
	pffftSetup = pffft_new_setup(frameSize, PFFFT_REAL);
	float *fftIn;
	float *fftOut;
	fftIn = (float*)pffft_aligned_malloc(frameSize*sizeof(float));
	fftOut = (float*)pffft_aligned_malloc(frameSize*sizeof(float));

	memset(magn, 0, frameSize2*sizeof(float));
	memset(phase, 0, frameSize2*sizeof(float));
	memset(fftIn, 0, frameSize*sizeof(float));
	memset(fftOut, 0, frameSize*sizeof(float));

	// for (size_t k = 0; k < frameSize; k++) {
	// 	fftIn[k] = frame[k];
	// }

	pffft_transform_ordered(pffftSetup, frame, fftOut, 0, PFFFT_FORWARD);

	for (size_t k = 0; k < frameSize2; k++) {
		if ((abs(fftOut[2*k])>1e-2f) || (abs(fftOut[2*k+1])>1e-2f)) {
			float real = fftOut[2*k];
			float imag = fftOut[2*k+1];
			phase[k] = atan2(imag,real);
			magn[k] = 2.0f*sqrt(real*real+imag*imag)/frameSize;
		}
	}

  // ofstream myfile;
  // myfile.open ("C:/Temp/fft.csv");
	// for (size_t k = 0; k < frameSize; k++) {
	// 	myfile << fftOut[k] << "\n";
	// }
  // myfile.close();

	pffft_destroy_setup(pffftSetup);
	pffft_aligned_free(fftIn);
	pffft_aligned_free(fftOut);
}

void doIFFT(float *frame, float *magn, float *phase) {
	PFFFT_Setup *pffftSetup;
	pffftSetup = pffft_new_setup(frameSize, PFFFT_REAL);
	float *fftIn;
	float *fftOut;
	fftIn = (float*)pffft_aligned_malloc(frameSize*sizeof(float));
	fftOut =  (float*)pffft_aligned_malloc(frameSize*sizeof(float));

	memset(frame, 0, frameSize*sizeof(float));
	memset(fftIn, 0, frameSize*sizeof(float));
	memset(fftOut, 0, frameSize*sizeof(float));

	for (size_t k = 0; k < frameSize2; k++) {
		fftIn[2*k] = magn[k]*cos(phase[k]);
		fftIn[2*k+1] = magn[k]*sin(phase[k]);
	}

	pffft_transform_ordered(pffftSetup, fftIn, fftOut, 0, PFFFT_BACKWARD);

	for (size_t k = 0; k < frameSize; k++) {
		frame[k]=fftOut[k]*0.5f;
	}

	pffft_destroy_setup(pffftSetup);
	pffft_aligned_free(fftIn);
	pffft_aligned_free(fftOut);
}

void normalize(float *wav) {
	float amp = 0.0f;
	for(size_t i = 0 ; i < frameSize; i++) {
		amp = max(amp,abs(wav[i]));
	}
	for(size_t i=0 ; i<frameSize; i++) {
		wav[i] /= amp;
	}
}

void calcWavFromBins(float *magn, float *phase, float *wav) {
	float *tWav;
	tWav = (float*)calloc(frameSize,sizeof(float));
	for(size_t i = 0 ; i < frameSize; i++) {
		for(size_t j = 0; j < frameSize2; j++) {
			if (magn[j]>0) tWav[i] += magn[j] * cos(i*j*aCoeff + phase[j]);
		}
	}
	memcpy(wav,tWav,frameSize*sizeof(float));
	free(tWav);
}

struct threadData {
	float **magn;
	float **phas;
	float **wav;
	int index;
	float *sample;
	size_t sr;
	size_t sc;
	size_t *nFrames;
	mutex *sLock;
};

void * threadSynthTask(threadData data) {
	if (data.index>=0)
		calcWavFromBins(*(data.magn+data.index),*(data.phas+data.index),*(data.wav+data.index));
	else {
		for (size_t i=0; i<*(data.nFrames);i++) {
			calcWavFromBins(*(data.magn+i),*(data.phas+i),*(data.wav+i));
		}
	}
  return 0;
}

void * threadAnalysisTask(threadData data) {
	size_t sCount = 0;
	size_t fCount = 0;

	while ((sCount<(data.sc-frameSize)) && (fCount<*(data.nFrames))) {
		doFFT(data.sample+sCount, data.magn[fCount], data.phas[fCount]);
		sCount += frameSize;
		fCount++;
	}

	for(size_t i=0; i<*(data.nFrames);i++) {
		calcWavFromBins(*(data.magn+i),*(data.phas+i),*(data.wav+i));
	}

  return 0;
}

void * threadChopSampleTask(threadData data) {
	for(size_t i=0;(i<data.sc) && (i/frameSize<256);i++) {
		data.wav[i/frameSize][i%frameSize] = data.sample[i];
	}
	*(data.nFrames)=min(data.sc/frameSize,256);
  return 0;
}

void * threadWindowSampleTask(threadData data) {
	for (size_t i=0; i<*(data.nFrames); i++) {
		doWindowing(*(data.wav+i),*(data.wav+i));
	}
  return 0;
}

void * threadNormalizeFrame(threadData data) {
	normalize(data.wav[data.index]);
  return 0;
}

void * threadNormalizeWt(threadData data) {
	float amp=0.0f;
	for (size_t i=0; i<*(data.nFrames); i++) {
		for (size_t j = 0; j < frameSize; j++) {
			amp = max(amp,abs(data.wav[i][j]));
		}
	}
	for (size_t i=0; i<*(data.nFrames); i++) {
		for (size_t j = 0; j < frameSize; j++) {
			data.wav[i][j] /= amp;
		}
	}
  return 0;
}

void * threadFFTSampleTask(threadData data) {
	doFFT(*(data.wav+data.index),*(data.magn+data.index),*(data.phas+data.index));
  return 0;
}

void * threadIFFTSampleTask(threadData data) {
	doIFFT(*(data.wav+data.index),*(data.magn+data.index),*(data.phas+data.index));
  return 0;
}

void * threadMorphSampleTask(threadData data) {
	size_t inF = (float)(256 - (*(data.nFrames)))/(float)(*(data.nFrames)-1);
	size_t tF = inF * (*(data.nFrames)-1) + (*(data.nFrames));

	float **tMagn = (float **)calloc(*(data.nFrames), sizeof(float*));
	float **tPhas = (float **)calloc(*(data.nFrames), sizeof(float*));
	float **tWav = (float **)calloc(*(data.nFrames), sizeof(float*));

	for (size_t i=0; i<*(data.nFrames); i++) {
		*(tMagn+i) = (float*)calloc(frameSize2,sizeof(float));
		memcpy(*(tMagn+i),*(data.magn+i),frameSize2*sizeof(float));
		*(tPhas+i) = (float*)calloc(frameSize2,sizeof(float));
		memcpy(*(tPhas+i),*(data.phas+i),frameSize2*sizeof(float));
		*(tWav+i) = (float*)calloc(frameSize, sizeof(float));
		memcpy(*(tWav+i),*(data.wav+i),frameSize*sizeof(float));
	}

	size_t wPos=0;
	for (size_t i=0; i<*(data.nFrames)-1; i++) {
		memcpy(*(data.magn+wPos),*(tMagn+i),frameSize2*sizeof(float));
		memcpy(*(data.phas+wPos),*(tPhas+i),frameSize2*sizeof(float));
		memcpy(*(data.wav+wPos),*(tWav+i),frameSize*sizeof(float));
		wPos++;
		for (size_t j=0; j<inF; j++) {
			for (size_t k=0; k<frameSize; k++) {
				data.wav[wPos][k] = rescale(j,0,inF,tWav[i][k],tWav[i+1][k]);
			}
			wPos++;
		}
	}
	memcpy(*(data.magn+wPos),*(tMagn+(*(data.nFrames)-1)),frameSize2*sizeof(float));
	memcpy(*(data.phas+wPos),*(tPhas+(*(data.nFrames)-1)),frameSize2*sizeof(float));
	memcpy(*(data.wav+wPos),*(tWav+(*(data.nFrames)-1)),frameSize*sizeof(float));

	for (size_t i=0; i<*(data.nFrames)-1; i++) {
		delete(*(tMagn+i));
		delete(*(tPhas+i));
		delete(*(tWav+i));
	}

	delete(tMagn);
	delete(tPhas);
	delete(tWav);

	*(data.nFrames) = tF;

  return 0;
}

void * threadMorphSpectrumTask(threadData data) {
	size_t inF = (float)(256 - (*(data.nFrames)))/(float)(*(data.nFrames)-1);
	size_t tF = inF * (*(data.nFrames)-1) + (*(data.nFrames));

	float **tMagn = (float **)calloc(*(data.nFrames), sizeof(float*));
	float **tPhas = (float **)calloc(*(data.nFrames), sizeof(float*));
	float **tWav = (float **)calloc(*(data.nFrames), sizeof(float*));

	for (size_t i=0; i<*(data.nFrames); i++) {
		doFFT(*(data.wav+i),*(data.magn+i),*(data.phas+i));
		*(tMagn+i) = (float*)calloc(frameSize2,sizeof(float));
		memcpy(*(tMagn+i),*(data.magn+i),frameSize2*sizeof(float));
		*(tPhas+i) = (float*)calloc(frameSize2,sizeof(float));
		memcpy(*(tPhas+i),*(data.phas+i),frameSize2*sizeof(float));
		*(tWav+i) = (float*)calloc(frameSize, sizeof(float));
		memcpy(*(tWav+i),*(data.wav+i),frameSize*sizeof(float));
	}

	size_t wPos=0;
	for (size_t i=0; i<*(data.nFrames)-1; i++) {
		memcpy(*(data.magn+wPos),*(tMagn+i),frameSize2*sizeof(float));
		memcpy(*(data.phas+wPos),*(tPhas+i),frameSize2*sizeof(float));
		memcpy(*(data.wav+wPos),*(tWav+i),frameSize*sizeof(float));
		wPos++;
		for (size_t j=0; j<inF; j++) {
			for (size_t k=0; k<frameSize2; k++) {
				if ((tMagn[i][k] != 0) && (tMagn[i+1][k] !=0)) {
					data.magn[wPos][k] = rescale(j,0,inF,tMagn[i][k],tMagn[i+1][k]);
					data.phas[wPos][k] = rescale(j,0,inF,tPhas[i][k],tPhas[i+1][k]);
				}
			}
			calcWavFromBins(*(data.magn+wPos),*(data.phas+wPos),*(data.wav+wPos));
			//doIFFT(*(data.wav+wPos),*(data.magn+wPos),*(data.phas+wPos));
			wPos++;
		}
	}
	memcpy(*(data.magn+wPos),*(tMagn+(*(data.nFrames)-1)),frameSize2*sizeof(float));
	memcpy(*(data.phas+wPos),*(tPhas+(*(data.nFrames)-1)),frameSize2*sizeof(float));
	memcpy(*(data.wav+wPos),*(tWav+(*(data.nFrames)-1)),frameSize*sizeof(float));

	for (size_t i=0; i<*(data.nFrames)-1; i++) {
		delete(*(tMagn+i));
		delete(*(tPhas+i));
		delete(*(tWav+i));
	}

	delete(tMagn);
	delete(tPhas);
	delete(tWav);

	*(data.nFrames) = tF;

	return 0;
}

struct LIMONADE : Module {
	enum ParamIds {
		RESET_PARAM,
		SYNC_PARAM,
		FREQ_PARAM,
		FINE_PARAM,
		FM_PARAM,
		INDEX_PARAM,
		PWINDEX_PARAM,
		PWINDEXATT_PARAM,
		COPYPASTE_PARAM,
		ADDFRAME_PARAM,
		DELETEFRAME_PARAM,
		DISPLAYMODE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
		FM_INPUT = PITCH_INPUT+4,
		SYNC_INPUT,
		SYNCMODE_INPUT,
		PWINDEX_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS = OUT+4
	};
	enum LightIds {
		COPYPASTE_LIGHT,
		NUM_LIGHTS
	};

	size_t nFrames = 0;
  float **magn;
  float **phas;
  float **wav;
	thread sThread;
	threadData sData;
	mutex sLock;
	SchmittTrigger resetTrigger;
	SchmittTrigger copyTrigger;
	SchmittTrigger addFrameTrigger;
	SchmittTrigger deleteFrameTrigger;
	SchmittTrigger displayModeTrigger;
	WavOscillator<16, 16> oscillator;
	size_t index = 0;
	size_t pWIndex = 0;
	int cpyPstIndex = -1;
	string lastPath;
	int displayMode = 0;

	LIMONADE() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
		magn = (float **)calloc(256, sizeof(float*));
		phas = (float **)calloc(256, sizeof(float*));
		wav = (float **)calloc(256, sizeof(float*));

		for (size_t i=0; i<256; i++) {
			*(magn+i) = (float*)calloc(frameSize2,sizeof(float));
			*(phas+i) = (float*)calloc(frameSize2,sizeof(float));
			*(wav+i) = (float*)calloc(frameSize, sizeof(float));
		}
		nFrames = 1;
		syncThreadData();
	}

  ~LIMONADE() {
		for (size_t i=0; i<256; i++) {
			delete(*(magn+i));
			delete(*(phas+i));
			delete(*(wav+i));
		}

    delete(magn);
		delete(phas);
		delete(wav);
	}

	void step() override;
	void syncThreadData();
	void loadSample(std::string path);
	void windowSample();
	void fftSample(size_t i);
	void ifftSample(size_t i);
	void morphSample();
	void morphSpectrum();
	void updateWaveTable(size_t i);
	void addFrame(size_t i);
	void deleteFrame(size_t i);
	void normalizeFrame(size_t i);
	void normalizeWt();

	json_t *toJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "lastPath", json_string(lastPath.c_str()));
		json_object_set_new(rootJ, "nFrames", json_integer(nFrames));

		json_t *magnSJ = json_array();
		json_t *phasSJ = json_array();
		//json_t *wavSJ = json_array();

		for (size_t i=0; i<nFrames; i++) {
			json_t *magnI = json_array();
			json_t *phasI = json_array();
			//json_t *wavI = json_array();
			for (size_t j=0; j<frameSize2; j++) {
				json_t *magnJ = json_real(magn[i][j]);
				json_array_append_new(magnI, magnJ);
				json_t *phasJ = json_real(phas[i][j]);
				json_array_append_new(phasI, phasJ);
				// json_t *wavJ = json_real(wav[i][j]);
				// json_array_append_new(wavI, wavJ);
			}
			json_array_append_new(magnSJ, magnI);
			json_array_append_new(phasSJ, phasI);
			//json_array_append_new(wavSJ, wavI);
		}

		json_object_set_new(rootJ, "magnS", magnSJ);
		json_object_set_new(rootJ, "phasS", phasSJ);
		//json_object_set_new(rootJ, "wavS", wavSJ);
		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		json_t *lastPathJ = json_object_get(rootJ, "lastPath");
		if (lastPathJ) {
			lastPath = json_string_value(lastPathJ);
			loadSample(lastPath);
		}
		else {
			json_t *nFramesJ = json_object_get(rootJ, "nFrames");
			if (nFramesJ) {
				nFrames = json_integer_value(nFramesJ);
			}

			json_t *magnSJ = json_object_get(rootJ, "magnS");
			json_t *phasSJ = json_object_get(rootJ, "phasS");
			//json_t *wavSJ = json_object_get(rootJ, "wavS");
			if (magnSJ && phasSJ) {
				for (size_t i=0; i<nFrames; i++) {
					json_t *magnI = json_array_get(magnSJ, i);
					json_t *phasI = json_array_get(phasSJ, i);
					//json_t *wavI = json_array_get(wavSJ, i);
					if (magnI && phasI) {
						for (size_t j=0; j<frameSize2; j++) {
							magn[i][j] = json_number_value(json_array_get(magnI, j));
							phas[i][j] = json_number_value(json_array_get(phasI, j));
							//wav[i][j] = json_number_value(json_array_get(wavI, j));
						}
					}
				}
			}
		}
	}

	void randomize() override {
		for (size_t i=0; i<frameSize; i++) {
			magn[index][i]=randomUniform()*100.0f;
			phas[index][i]=randomUniform()*M_PI;
		}
		updateWaveTable(index);
	}

	void reset() override {
		for (size_t i=0; i<256; i++) {
			memset(magn[i], 0, frameSize2*sizeof(float));
			memset(phas[i], 0, frameSize2*sizeof(float));
			memset(wav[i], 0, frameSize*sizeof(float));
		}
		lastPath = "";
		nFrames = 1;
	}
};

inline void LIMONADE::syncThreadData() {
	sData.magn = magn;
	sData.phas = phas;
	sData.wav = wav;
	sData.nFrames = &nFrames;
	sData.sLock = &sLock;
}

inline void LIMONADE::updateWaveTable(size_t i) {
	syncThreadData();
	sData.index = i;
	sThread = thread(threadSynthTask, std::ref(sData));
	sThread.detach();
}

inline void LIMONADE::fftSample(size_t i) {
	syncThreadData();
	sData.index = i;
	sThread = thread(threadFFTSampleTask, std::ref(sData));
	sThread.detach();
}

inline void LIMONADE::ifftSample(size_t i) {
	syncThreadData();
	sData.index = i;
	sThread = thread(threadIFFTSampleTask, std::ref(sData));
	sThread.detach();
}

inline void LIMONADE::morphSample() {
	syncThreadData();
	sThread = thread(threadMorphSampleTask, std::ref(sData));
	sThread.detach();
}

inline void LIMONADE::morphSpectrum() {
	syncThreadData();
	sThread = thread(threadMorphSpectrumTask, std::ref(sData));
	sThread.detach();
}

void LIMONADE::addFrame(size_t i) {
	if (nFrames<256) {
		for (size_t j=255; (j>=i) && (j>0); j--) {
			memcpy(*(magn+j),*(magn+j-1),frameSize2*sizeof(float));
			memcpy(*(phas+j),*(phas+j-1),frameSize2*sizeof(float));
			memcpy(*(wav+j),*(wav+j-1),frameSize*sizeof(float));
		}
		memset(phas[i], 0, frameSize2*sizeof(float));
	  memset(magn[i], 0, frameSize2*sizeof(float));
		memset(wav[i], 0, frameSize*sizeof(float));
		nFrames++;
		index++;
	}
}

void LIMONADE::deleteFrame(size_t i) {
	if (nFrames>1) {
		for (size_t j=i; j<255; j++) {
			memcpy(*(magn+j),*(magn+j+1),frameSize2*sizeof(float));
			memcpy(*(phas+j),*(phas+j+1),frameSize2*sizeof(float));
			memcpy(*(wav+j),*(wav+j+1),frameSize*sizeof(float));
		}
		nFrames--;
		index--;
	}
}

void LIMONADE::loadSample(std::string path) {
	unsigned int c;
  unsigned int sr;
  drwav_uint64 sc;
	float* pSampleData;
  pSampleData = drwav_open_and_read_file_f32(path.c_str(), &c, &sr, &sc);
  if (pSampleData != NULL)  {
		sData.sr = sr;

		for (size_t i=0; i<256; i++) {
			memset(magn[i], 0, frameSize2*sizeof(float));
			memset(phas[i], 0, frameSize2*sizeof(float));
			memset(wav[i], 0, frameSize*sizeof(float));
		}
		if (sData.sample != NULL) {
			delete(sData.sample);
		}
		if (c == 1) {
			sData.sample = (float*)calloc(sc,sizeof(float));
			for (unsigned int i=0; i < sc; i++) {
				sData.sample[i] = pSampleData[i];
			}
			sData.sc = sc;
			sThread = thread(threadChopSampleTask, std::ref(sData));
			sThread.detach();
		}
		else if (c == 2) {
			sData.sample = (float*)calloc(sc/2,sizeof(float));
			for (unsigned int i=0; i<sc/2; i++) {
				sData.sample[i] = 0.5f*(pSampleData[2*i]+pSampleData[2*i+1]);
			}
			sData.sc = sc/2;
			sThread = thread(threadChopSampleTask, std::ref(sData));
			sThread.detach();
		}
		drwav_free(pSampleData);
	}
}

void LIMONADE::windowSample() {
	sThread = thread(threadWindowSampleTask, std::ref(sData));
	sThread.detach();
}

void LIMONADE::normalizeFrame(size_t i) {
	sData.index = i;
	sThread = thread(threadNormalizeFrame, std::ref(sData));
	sThread.detach();
}

void LIMONADE::normalizeWt() {
	sThread = thread(threadNormalizeWt, std::ref(sData));
	sThread.detach();
}

void LIMONADE::step() {
	if (displayModeTrigger.process(params[DISPLAYMODE_PARAM].value))
	{
		displayMode = (displayMode+1)%3;
	}

	if (addFrameTrigger.process(params[ADDFRAME_PARAM].value))
	{
		addFrame(index);
	}

	if (deleteFrameTrigger.process(params[DELETEFRAME_PARAM].value))
	{
		deleteFrame(index);
	}

	if (resetTrigger.process(params[RESET_PARAM].value))
	{
		memset(phas[index], 0, frameSize2*sizeof(float));
	  memset(magn[index], 0, frameSize2*sizeof(float));
		updateWaveTable(index);
	}

	if (copyTrigger.process(params[COPYPASTE_PARAM].value))
	{
		if (cpyPstIndex<0) {
			cpyPstIndex = index;
			lights[COPYPASTE_LIGHT].value=10.0f;
		}
		else {
			memcpy(*(magn+index),*(magn+cpyPstIndex),frameSize2*sizeof(float));
			memcpy(*(phas+index),*(phas+cpyPstIndex),frameSize2*sizeof(float));
			updateWaveTable(index);
			cpyPstIndex = -1;
			lights[COPYPASTE_LIGHT].value=0.0f;
		}
	}

	index = max((int)rescale(params[INDEX_PARAM].value,0.0f,10.0f,0.0f,nFrames-1),0);
	pWIndex = max((int)(rescale(params[PWINDEX_PARAM].value,0.0f,10.0f,0.0f,nFrames-1) + rescale(inputs[PWINDEX_INPUT].value*params[PWINDEXATT_PARAM].value,0.0f,10.0f,0.0f,nFrames-1)),0);

	oscillator.soft = (params[SYNC_PARAM].value + inputs[SYNCMODE_INPUT].value) <= 0.0f;
	float pitchFine = 3.0f * quadraticBipolar(params[FINE_PARAM].value);
	float pitchCv = 12.0f * inputs[PITCH_INPUT].value;
	if (inputs[FM_INPUT].active) {
		pitchCv += quadraticBipolar(params[FM_PARAM].value) * 12.0f * inputs[FM_INPUT].value;
	}
	oscillator.setPitch(params[FREQ_PARAM].value, pitchFine + pitchCv);
	oscillator.syncEnabled = inputs[SYNC_INPUT].active;

	oscillator.process(engineGetSampleTime(), inputs[SYNC_INPUT].value, wav, pWIndex);
	if (outputs[OUT].active) 	outputs[OUT].value = 5.0f * oscillator.wav();

}

struct LIMONADEWidget : ModuleWidget {
	LIMONADEWidget(LIMONADE *module);
	Menu *createContextMenu() override;
};

struct LIMONADEMagnDisplay : OpaqueWidget {
	LIMONADE *module;
	shared_ptr<Font> font;
	const float width = 400.0f;
	const float heightMagn = 70.0f;
	const float heightPhas = 50.0f;
	const float graphGap = 30.0f;
	float zoomWidth = width * 28.0f;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refY = 0.0f;
	float refX = 0.0f;
	bool write = false;

	LIMONADEMagnDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void onMouseDown(EventMouseDown &e) override {
		refX = e.pos.x;
		refY = e.pos.y;
		refIdx = ((e.pos.x - zoomLeftAnchor)/zoomWidth)*(float)frameSize2;
		OpaqueWidget::onMouseDown(e);
	}

	void onDragStart(EventDragStart &e) override {
		windowCursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(EventDragMove &e) override {
		if (!windowIsShiftPressed() && (module->nFrames>0)) {
			if (refY<=heightMagn) {
				if (windowIsModPressed()) {
					module->magn[module->index][refIdx] = 0.0f;
				}
				else {
					module->magn[module->index][refIdx] -= e.mouseRel.y/500.0f;
					module->magn[module->index][refIdx] = clamp(module->magn[module->index][refIdx],0.0f, 1.0f);
				}
			}
			else if (refY>=heightMagn+graphGap) {
				if (windowIsModPressed()) {
					module->phas[module->index][refIdx] = 0.0f;
				}
				else {
					module->phas[module->index][refIdx] -= e.mouseRel.y/500.0f;
					module->phas[module->index][refIdx] = clamp(module->phas[module->index][refIdx],-1.0f*M_PI, M_PI);
				}
			}
			module->updateWaveTable(module->index);
		}
		else {
			zoomLeftAnchor = clamp(refX - (refX - zoomLeftAnchor) + e.mouseRel.x, width - zoomWidth,0.0f);
		}
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(EventDragEnd &e) override {
		windowCursorUnlock();
		OpaqueWidget::onDragEnd(e);
	}

	void draw(NVGcontext *vg) override {
		nvgSave(vg);
		Rect b = Rect(Vec(zoomLeftAnchor, 0), Vec(zoomWidth, heightMagn + graphGap + heightPhas));
		nvgScissor(vg, 0, b.pos.y, width, heightMagn + graphGap + heightPhas);
		float invframeSize = 1.0f/frameSize2;
		size_t tag=1;

		nvgFillColor(vg, YELLOW_BIDOO);
		nvgFontSize(vg, 16.0f);
		nvgFontFaceId(vg, font->handle);
		nvgTextLetterSpacing(vg, -2.0f);
		nvgFillColor(vg, YELLOW_BIDOO);
		nvgText(vg, 0.0f, heightMagn + graphGap/2+4, ("Frame " + to_string(module->index+1) + " / " + to_string(module->nFrames)).c_str(), NULL);
		nvgText(vg, 130.0f, heightMagn + graphGap/2+4, "▲ Magnitude ▼ Phase", NULL);

		for (size_t i = 0; i < frameSize2; i++) {
			float x, y;
			x = (float)i * invframeSize;
			y = module->magn[module->index][i];
			Vec p;
			p.x = b.pos.x + b.size.x * x;
			p.y = heightMagn * y;

			if (i==tag){
				nvgBeginPath(vg);
				nvgFillColor(vg, nvgRGBA(45, 114, 143, 100));
				nvgRect(vg, p.x, 0, b.size.x * invframeSize, heightMagn);
				nvgRect(vg, p.x, heightMagn + graphGap, b.size.x * invframeSize, heightPhas);
				nvgClosePath(vg);
				nvgLineCap(vg, NVG_MITER);
				nvgStrokeWidth(vg, 0);
				//nvgStroke(vg);
				nvgFill(vg);
				tag *=2;
			}

			if (p.x < width) {
				nvgStrokeColor(vg, YELLOW_BIDOO);
				nvgFillColor(vg, YELLOW_BIDOO);
				nvgLineCap(vg, NVG_MITER);
				nvgStrokeWidth(vg, 1);
				nvgBeginPath(vg);
				nvgRect(vg, p.x, heightMagn - p.y, b.size.x * invframeSize, p.y);
				y = module->phas[module->index][i]/M_PI;
				p.y = heightPhas * 0.5f * y;
				nvgRect(vg, p.x, heightMagn + graphGap + heightPhas * 0.5f - p.y, b.size.x * invframeSize, p.y);
				nvgClosePath(vg);
				nvgStroke(vg);
				nvgFill(vg);
			}
		}
		nvgResetScissor(vg);
		nvgRestore(vg);
	}
};

struct LIMONADEWavDisplay : OpaqueWidget {
	LIMONADE *module;
	shared_ptr<Font> font;
	const float width = 150.0f;
	const float height = 100.0f;
	float zoomWidth = width;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refX = 0.0f;

	LIMONADEWavDisplay() {
		font = Font::load(assetPlugin(plugin, "res/DejaVuSansMono.ttf"));
	}

	void onMouseDown(EventMouseDown &e) override {
			refX = e.pos.x;
			OpaqueWidget::onMouseDown(e);
	}

	void onDragStart(EventDragStart &e) override {
		windowCursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(EventDragMove &e) override {
		float zoom = 1.0f;
		if (e.mouseRel.y > 0.0f) {
			zoom = 1.0f/(windowIsShiftPressed() ? 2.0f : 1.1f);
		}
		else if (e.mouseRel.y < 0.0f) {
			zoom = windowIsShiftPressed() ? 2.0f : 1.1f;
		}
		zoomWidth = clamp(zoomWidth*zoom,width,zoomWidth*(windowIsShiftPressed() ? 2.0f : 1.1f));
		zoomLeftAnchor = clamp(refX - (refX - zoomLeftAnchor)*zoom + e.mouseRel.x, width - zoomWidth,0.0f);
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(EventDragEnd &e) override {
		windowCursorUnlock();
		OpaqueWidget::onDragEnd(e);
	}

	void draw(NVGcontext *vg) override {
		nvgSave(vg);
		Rect b = Rect(Vec(zoomLeftAnchor, 0), Vec(zoomWidth, height));
		nvgScissor(vg, 0, b.pos.y, width, height);
		float invNbSample = 1.0f/frameSize;
		float invNbBins = 1.0f/256;
		float dx = -1.0f;
    float dy = 1.0f;
    float dz = 1.0f;
		if (module->displayMode==0) {
			for (size_t n=0; n<(module->nFrames); n++) {
				nvgBeginPath(vg);
				for (size_t i=0; i<frameSize; i++) {
					float x, y, z;
					z = 50.0f * (float)(module->nFrames-n)/(float)module->nFrames;
					x = 0.5f * (float)i * invNbSample;
					y = 0.5f * (module->wav[module->nFrames-1-n][i] * 0.48f + 0.5f);
					Vec p;
					p.x = b.pos.x + b.size.x * x - dx * z / dz;
					p.y = b.pos.y + b.size.y * (1.0f - y) - dy * z / dz;
					if (i == 0) {
						nvgMoveTo(vg, p.x, p.y);
					}
					else {
						nvgLineTo(vg, p.x, p.y);
					}
				}
				nvgLineCap(vg, NVG_MITER);
				if ((module->nFrames-1-n) == module->pWIndex)	{
					nvgStrokeColor(vg, RED_BIDOO);
					nvgStrokeWidth(vg, 1);
				}
				else if ((module->nFrames-1-n) == (module->index))	{
					nvgStrokeColor(vg, YELLOW_BIDOO);
					nvgStrokeWidth(vg, 1);
				}
				else {
					nvgStrokeColor(vg, YELLOW_BIDOO_LIGHT);
					nvgStrokeWidth(vg, 1);
				}
				nvgStroke(vg);
			}
		}
		else {
			for (size_t n=0; n<(module->nFrames); n++) {
				nvgBeginPath(vg);
				for (size_t i=0; i<256; i++) {
					float x, y, z;
					z = 50.0f * (float)(module->nFrames-n)/(float)module->nFrames;
					x = 0.5f * (float)i * invNbBins;
					if (module->displayMode==1) {
						y = module->magn[module->nFrames-1-n][i] * 0.48f;
					}
					else {
						y = 0.5f * (module->phas[module->nFrames-1-n][i] * 0.48f / M_PI + 0.5f);
					}
					Vec p;
					p.x = b.pos.x + b.size.x * x - dx * z / dz;
					p.y = b.pos.y + b.size.y * (1.0f - y) - dy * z / dz;
					if (i == 0) {
						nvgMoveTo(vg, p.x, p.y);
					}
					else {
						nvgLineTo(vg, p.x, p.y);
					}
				}
				nvgLineCap(vg, NVG_MITER);
				if ((module->nFrames-1-n) == module->pWIndex)	{
					nvgStrokeColor(vg, RED_BIDOO);
					nvgStrokeWidth(vg, 1);
				}
				else if ((module->nFrames-1-n) == (module->index))	{
					nvgStrokeColor(vg, YELLOW_BIDOO);
					nvgStrokeWidth(vg, 1);
				}
				else {
					nvgStrokeColor(vg, YELLOW_BIDOO_LIGHT);
					nvgStrokeWidth(vg, 1);
				}
				nvgStroke(vg);
			}
		}
		nvgResetScissor(vg);
		nvgRestore(vg);
	}
};

LIMONADEWidget::LIMONADEWidget(LIMONADE *module) : ModuleWidget(module) {
	setPanel(SVG::load(assetPlugin(plugin, "res/LIMONADE.svg")));

	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
	addChild(Widget::create<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
	addChild(Widget::create<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

	{
		LIMONADEMagnDisplay *display = new LIMONADEMagnDisplay();
		display->module = module;
		display->box.pos = Vec(24, 50);
		display->box.size = Vec(500, 160);
		addChild(display);
	}

	{
		LIMONADEWavDisplay *display = new LIMONADEWavDisplay();
		display->module = module;
		display->box.pos = Vec(23, 220);
		display->box.size = Vec(150, 110);
		addChild(display);
	}

	addParam(ParamWidget::create<BlueCKD6>(Vec(205, 235), module, LIMONADE::DISPLAYMODE_PARAM, 0.0f, 1.0f, 1.0f));

	addParam(ParamWidget::create<CKSS>(Vec(255, 240), module, LIMONADE::SYNC_PARAM, 0.0f, 1.0f, 1.0f));
	addParam(ParamWidget::create<BidooBlueKnob>(Vec(287, 235), module, LIMONADE::FREQ_PARAM, -54.0f, 54.0f, 0.0f));
	addParam(ParamWidget::create<BidooBlueKnob>(Vec(327, 235), module, LIMONADE::FINE_PARAM, -1.0f, 1.0f, 0.0f));
	addParam(ParamWidget::create<BidooBlueKnob>(Vec(367, 235), module, LIMONADE::FM_PARAM, 0.0f, 1.0f, 0.0f));


	addParam(ParamWidget::create<BidooBlueKnob>(Vec(445.0f, 42), module, LIMONADE::INDEX_PARAM, 0.0f, 10.0f, 0.0f));
	addParam(ParamWidget::create<BlueCKD6>(Vec(446.0f, 90), module, LIMONADE::COPYPASTE_PARAM, 0.0f, 1.0f, 0.0f));
	addChild(ModuleLightWidget::create<SmallLight<BlueLight>>(Vec(470.0f, 88.0f), module, LIMONADE::COPYPASTE_LIGHT));
	addParam(ParamWidget::create<BlueCKD6>(Vec(446.0f, 125), module, LIMONADE::ADDFRAME_PARAM, 0.0f, 1.0f, 0.0f));
	addParam(ParamWidget::create<BlueCKD6>(Vec(446.0f, 160), module, LIMONADE::DELETEFRAME_PARAM, 0.0f, 1.0f, 0.0f));
	addParam(ParamWidget::create<BlueCKD6>(Vec(446.0f, 195), module, LIMONADE::RESET_PARAM, 0.0f, 1.0f, 0.0f));

	addParam(ParamWidget::create<BidooBlueKnob>(Vec(407.0f, 235), module, LIMONADE::PWINDEX_PARAM, 0.0f, 10.0f, 0.0f));
	addParam(ParamWidget::create<BidooBlueTrimpot>(Vec(412.0f, 275), module, LIMONADE::PWINDEXATT_PARAM, -1.0f, 1, 0.0f));
	addInput(Port::create<PJ301MPort>(Vec(410, 300), Port::INPUT, module, LIMONADE::PWINDEX_INPUT));

	addInput(Port::create<PJ301MPort>(Vec(250, 300), Port::INPUT, module, LIMONADE::SYNCMODE_INPUT));
	addInput(Port::create<PJ301MPort>(Vec(290, 300), Port::INPUT, module, LIMONADE::PITCH_INPUT));
	addInput(Port::create<PJ301MPort>(Vec(330, 300), Port::INPUT, module, LIMONADE::SYNC_INPUT));
	addInput(Port::create<PJ301MPort>(Vec(370, 300), Port::INPUT, module, LIMONADE::FM_INPUT));

	addOutput(Port::create<PJ301MPort>(Vec(447, 300), Port::OUTPUT, module, LIMONADE::OUT));
}

struct LIMONADELoadSample : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
		char *path = osdialog_file(OSDIALOG_OPEN, "", NULL, NULL);
		if (path) {
			limonadeModule->lastPath=path;
			limonadeModule->loadSample(path);
			free(path);
		}
	}
};

struct LIMONADEWindowSample : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->windowSample();
	}
};

struct LIMONADEFFTSample : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->fftSample(limonadeModule->index);
	}
};

struct LIMONADEIFFTSample : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->ifftSample(limonadeModule->index);
	}
};

struct LIMONADEMorphSample : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->morphSample();
	}
};

struct LIMONADEMorphSpectrum : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->morphSpectrum();
	}
};

struct LIMONADENormalizeFrame : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->normalizeFrame(limonadeModule->index);
	}
};

struct LIMONADENormalizeWt : MenuItem {
	LIMONADE *limonadeModule;
	void onAction(EventAction &e) override {
	limonadeModule->normalizeWt();
	}
};

Menu *LIMONADEWidget::createContextMenu() {
	LIMONADE *limonadeModule = dynamic_cast<LIMONADE*>(module);
	assert(limonadeModule);
	Menu *menu = ModuleWidget::createContextMenu();

	LIMONADELoadSample *loadItem = new LIMONADELoadSample();
	loadItem->text = "Load sample";
	loadItem->limonadeModule = limonadeModule;
	menu->addChild(loadItem);

	LIMONADEWindowSample *windowItem = new LIMONADEWindowSample();
	windowItem->text = "Window sample";
	windowItem->limonadeModule = limonadeModule;
	menu->addChild(windowItem);

	LIMONADEFFTSample *fftItem = new LIMONADEFFTSample();
	fftItem->text = "FFT sample";
	fftItem->limonadeModule = limonadeModule;
	menu->addChild(fftItem);

	LIMONADEIFFTSample *ifftItem = new LIMONADEIFFTSample();
	ifftItem->text = "IFFT sample";
	ifftItem->limonadeModule = limonadeModule;
	menu->addChild(ifftItem);

	LIMONADEMorphSample *morphSItem = new LIMONADEMorphSample();
	morphSItem->text = "Morph sample";
	morphSItem->limonadeModule = limonadeModule;
	menu->addChild(morphSItem);

	LIMONADEMorphSpectrum *morphSpItem = new LIMONADEMorphSpectrum();
	morphSpItem->text = "Morph spectrum";
	morphSpItem->limonadeModule = limonadeModule;
	menu->addChild(morphSpItem);

	LIMONADENormalizeFrame *normFItem = new LIMONADENormalizeFrame();
	normFItem->text = "Normalize frame";
	normFItem->limonadeModule = limonadeModule;
	menu->addChild(normFItem);

	LIMONADENormalizeWt *normWtItem = new LIMONADENormalizeWt();
	normWtItem->text = "Normalize wavetable";
	normWtItem->limonadeModule = limonadeModule;
	menu->addChild(normWtItem);

	return menu;
}

Model *modelLIMONADE = Model::create<LIMONADE, LIMONADEWidget>("Bidoo","LIMONADE", "LIMONADE additive osc", OSCILLATOR_TAG);
