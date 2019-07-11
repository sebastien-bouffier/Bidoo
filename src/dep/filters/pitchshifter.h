#include <string.h>
#include <math.h>
#include <stdio.h>
#include "../pffft/pffft.h"

using namespace std;

struct PitchShifter {

	float *gInFIFO;
	float *gOutFIFO;
	float *gFFTworksp;
	float *gFFTworkspOut;
	float *gLastPhase;
	float *gSumPhase;
	float *gOutputAccum;
	float *gAnaFreq;
	float *gAnaMagn;
	float *gSynFreq;
	float *gSynMagn;
	float sampleRate;
	PFFFT_Setup *pffftSetup;
	long gRover = false;
	double magn, phase, tmp, window, real, imag;
	double freqPerBin, expct, invOsamp, invFftFrameSize, invFftFrameSize2, invPi;
	long fftFrameSize, osamp, i,k, qpd, index, inFifoLatency, stepSize, fftFrameSize2;

	PitchShifter(long fftFrameSize, long osamp, float sampleRate) {
		this->fftFrameSize = fftFrameSize;
		this->osamp = osamp;
		this->sampleRate = sampleRate;
		pffftSetup = pffft_new_setup(fftFrameSize, PFFFT_REAL);
		fftFrameSize2 = fftFrameSize/2;
		stepSize = fftFrameSize/osamp;
		freqPerBin = sampleRate/(double)fftFrameSize;
		expct = 2.0f * M_PI * (double)stepSize/(double)fftFrameSize;
		inFifoLatency = fftFrameSize-stepSize;
		invOsamp = 1.0f/osamp;
		invFftFrameSize = 1.0f/fftFrameSize;
		invFftFrameSize2 = 1.0f/fftFrameSize2;
		invPi = 1.0f/M_PI;

		gInFIFO = (float*)calloc(fftFrameSize,sizeof(float));
		memset(gInFIFO, 0, fftFrameSize*sizeof(float));
		gOutFIFO =  (float*)calloc(fftFrameSize,sizeof(float));
		memset(gOutFIFO, 0, fftFrameSize*sizeof(float));
		gFFTworksp = (float*)pffft_aligned_malloc(fftFrameSize*sizeof(float));
		gFFTworkspOut =  (float*)pffft_aligned_malloc(fftFrameSize*sizeof(float));
		gLastPhase = (float*)calloc((fftFrameSize/2+1),sizeof(float));
		memset(gLastPhase, 0, (fftFrameSize/2+1)*sizeof(float));
		gSumPhase = (float*)calloc((fftFrameSize/2+1),sizeof(float));
		memset(gSumPhase, 0, (fftFrameSize/2+1)*sizeof(float));
		gOutputAccum = (float*)calloc(2*fftFrameSize,sizeof(float));
		memset(gOutputAccum, 0, 2*fftFrameSize*sizeof(float));
		gAnaFreq = (float*)calloc(fftFrameSize,sizeof(float));
		memset(gAnaFreq, 0, fftFrameSize*sizeof(float));
		gAnaMagn = (float*)calloc(fftFrameSize,sizeof(float));
		memset(gAnaMagn, 0, fftFrameSize*sizeof(float));
		gSynFreq = (float*)calloc(fftFrameSize,sizeof(float));
		memset(gSynFreq, 0, fftFrameSize*sizeof(float));
		gSynMagn = (float*)calloc(fftFrameSize,sizeof(float));
		memset(gSynMagn, 0, fftFrameSize*sizeof(float));
	}

	~PitchShifter() {
		pffft_destroy_setup(pffftSetup);
		free(gInFIFO);
		free(gOutFIFO);
		free(gLastPhase);
		free(gSumPhase);
		free(gOutputAccum);
		free(gAnaFreq);
		free(gAnaMagn);
		free(gSynFreq);
		free(gSynMagn);
		pffft_aligned_free(gFFTworksp);
		pffft_aligned_free(gFFTworkspOut);
	}

	void process(const float pitchShift, const float *input, float *output) {

			for (i = 0; i < fftFrameSize; i++) {
				gInFIFO[gRover] = input[i];

				if(gRover >= inFifoLatency)  // [bsp] 09Mar2019: this fixes the noise burst issue in REI
					 output[i] = gOutFIFO[gRover-inFifoLatency];
				else
					 output[i] = 0.0f;

				gRover++;

				if (gRover >= fftFrameSize) {
					gRover = inFifoLatency;

					memset(gFFTworksp, 0, fftFrameSize*sizeof(float));
					memset(gFFTworkspOut, 0, fftFrameSize*sizeof(float));

					for (k = 0; k < fftFrameSize;k++) {
						window = -0.5 * cos(2.0f * M_PI * (double)k * invFftFrameSize) + 0.5f;
						gFFTworksp[k] = gInFIFO[k] * window;
					}

					pffft_transform_ordered(pffftSetup, gFFTworksp, gFFTworkspOut, NULL, PFFFT_FORWARD);

					for (k = 0; k <= fftFrameSize2; k++) {
						real = gFFTworkspOut[2*k];
						imag = gFFTworkspOut[2*k+1];
						magn = 2.*sqrt(real*real + imag*imag);
						phase = atan2(imag,real);
						tmp = phase - gLastPhase[k];
						gLastPhase[k] = phase;
						tmp -= (double)k*expct;
						qpd = tmp * invPi;
						if (qpd >= 0) qpd += qpd&1;
						else qpd -= qpd&1;
						tmp -= M_PI*(double)qpd;
						tmp = osamp * tmp * invPi * 0.5f;
						tmp = (double)k*freqPerBin + tmp*freqPerBin;
						gAnaMagn[k] = magn;
						gAnaFreq[k] = tmp;
					}

					memset(gSynMagn, 0, fftFrameSize*sizeof(float));
					memset(gSynFreq, 0, fftFrameSize*sizeof(float));

					for (k = 0; k <= fftFrameSize2; k++) {
						index = k*pitchShift;
						if (index <= fftFrameSize2) {
							gSynMagn[index] += gAnaMagn[k];
							gSynFreq[index] = gAnaFreq[k] * pitchShift;
						}
					}

					memset(gFFTworksp, 0, fftFrameSize*sizeof(float));
					memset(gFFTworkspOut, 0, fftFrameSize*sizeof(float));

					for (k = 0; k <= fftFrameSize2; k++) {
						magn = k==0?0:gSynMagn[k];
						tmp = gSynFreq[k];
						tmp -= (double)k*freqPerBin;
						tmp /= freqPerBin;
						tmp = 2.0f * M_PI * tmp * invOsamp;
						tmp += (double)k*expct;
						gSumPhase[k] += tmp;
						phase = gSumPhase[k];
						gFFTworksp[2*k] = magn*cos(phase);
						gFFTworksp[2*k+1] = magn*sin(phase);
					}

					for (k = fftFrameSize+2; k < 2*fftFrameSize; k++) gFFTworksp[k] = 0.0f;
					pffft_transform_ordered(pffftSetup, gFFTworksp, gFFTworkspOut , NULL, PFFFT_BACKWARD);
					for(k=0; k < fftFrameSize; k++) {
						window = -0.5f * cos(2.0f * M_PI *(double)k * invFftFrameSize) + 0.5f;
						gOutputAccum[k] += 2.0f * window * gFFTworkspOut[k] * invFftFrameSize2 * invOsamp;
					}

					for (k = 0; k < stepSize; k++) gOutFIFO[k] = gOutputAccum[k];
					memmove(gOutputAccum, gOutputAccum+stepSize, fftFrameSize*sizeof(float));
					for (k = 0; k < inFifoLatency; k++) gInFIFO[k] = gInFIFO[k+stepSize];
				}
			}
	}
};
