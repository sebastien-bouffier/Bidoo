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
	// identity phase locking with a resampled spectrum (HCTIP); off keeps the
	// original bin mapping (REI's shimmer)
	bool phaseLock = false;
	float *gAnaPhase = nullptr;
	long *gSrc = nullptr;
	long *gNearPeak = nullptr;
	float *gWindow = nullptr;  // Hann, worked out once
	double magn, phase, tmp, window, real, imag;
	double freqPerBin, expct, invOsamp, invFftFrameSize, invFftFrameSize2, invPi;
	long fftFrameSize, osamp, i,k, qpd, index, inFifoLatency, stepSize, fftFrameSize2;

	PitchShifter() {

	}

	void init(long fftFrameSize, long osamp, float sampleRate) {
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

		gInFIFO = new float[fftFrameSize] {0.f};
		gOutFIFO =  new float[fftFrameSize] {0.f};
		gFFTworksp = (float*)pffft_aligned_malloc(fftFrameSize*sizeof(float));
		gFFTworkspOut =  (float*)pffft_aligned_malloc(fftFrameSize*sizeof(float));
		gLastPhase = new float[fftFrameSize2+1] {0.f};
		gSumPhase = new float[fftFrameSize2+1] {0.f};
		gOutputAccum = new float[2*fftFrameSize] {0.f};
		gAnaFreq = new float[fftFrameSize] {0.f};
		gAnaMagn = new float[fftFrameSize] {0.f};
		gSynFreq = new float[fftFrameSize] {0.f};
		gAnaPhase = new float[fftFrameSize] {0.f};
		gSrc = new long[fftFrameSize] {0};
		gNearPeak = new long[fftFrameSize] {0};
		gWindow = new float[fftFrameSize];
		for (k = 0; k < fftFrameSize; k++)
			gWindow[k] = -0.5 * cos(2.0 * M_PI * (double)k * invFftFrameSize) + 0.5;
		gSynMagn = new float[fftFrameSize] {0.f};
	}

	~PitchShifter() {
		pffft_destroy_setup(pffftSetup);
		delete[] gInFIFO;
		delete[] gOutFIFO;
		delete[] gLastPhase;
		delete[] gSumPhase;
		delete[] gOutputAccum;
		delete[] gAnaFreq;
		delete[] gAnaMagn;
		delete[] gSynFreq;
		delete[] gSynMagn;
		delete[] gAnaPhase;
		delete[] gSrc;
		delete[] gNearPeak;
		delete[] gWindow;
		pffft_aligned_free(gFFTworksp);
		pffft_aligned_free(gFFTworkspOut);
	}

	void process(const float pitchShift, const float *input, float *output) {
		for (long n = 0; n < fftFrameSize; n++)
			output[n] = step(pitchShift, input[n]);
	}

	// One sample in, one out: the FFT work is done every stepSize samples
	// instead of all at once every fftFrameSize.
	float step(const float pitchShift, const float in) {
				float out;
				gInFIFO[gRover] = in;

				if(gRover >= inFifoLatency)  // [bsp] 09Mar2019: this fixes the noise burst issue in REI
					 out = gOutFIFO[gRover-inFifoLatency];
				else
					 out = 0.0f;

				gRover++;

				if (gRover >= fftFrameSize) {
					gRover = inFifoLatency;

					memset(gFFTworksp, 0, fftFrameSize*sizeof(float));
					memset(gFFTworkspOut, 0, fftFrameSize*sizeof(float));

					for (k = 0; k < fftFrameSize;k++) {
						gFFTworksp[k] = gInFIFO[k] * gWindow[k];
					}

					pffft_transform_ordered(pffftSetup, gFFTworksp, gFFTworkspOut, NULL, PFFFT_FORWARD);

					for (k = 0; k < fftFrameSize2; k++) {
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
						gAnaPhase[k] = phase;
					}

					memset(gSynMagn, 0, fftFrameSize*sizeof(float));
					memset(gSynFreq, 0, fftFrameSize*sizeof(float));

					if (phaseLock) {
						// synthesis bin k takes the spectrum at analysis bin k / pitchShift
						for (k = 0; k < fftFrameSize2; k++) {
							double src = (double)k / pitchShift;
							long k0 = (long)src;
							double fr = src - (double)k0;
							if (k0 + 1 < fftFrameSize2) {
								gSynMagn[k] = gAnaMagn[k0] + (gAnaMagn[k0+1] - gAnaMagn[k0]) * fr;
								gSrc[k] = fr < 0.5 ? k0 : k0 + 1;
								gSynFreq[k] = gAnaFreq[gSrc[k]] * pitchShift;
							}
							else {
								gSynMagn[k] = 0.f;
								gSrc[k] = 0;
								gSynFreq[k] = k * freqPerBin;
							}
						}
						// peaks run their own phase; the bins around a peak are locked to it
						// (pi per bin: the frame stays centred), so a partial's bins add up
						long last = -1;
						for (k = 0; k < fftFrameSize2; k++) {
							bool peak = k > 0 && k < fftFrameSize2 - 1 && gSynMagn[k] > 0.f && gSynMagn[k] > gSynMagn[k-1] && gSynMagn[k] >= gSynMagn[k+1];
							if (peak) {
								last = k;
								tmp = gSynFreq[k] - (double)k*freqPerBin;
								tmp = 2.0 * M_PI * (tmp / freqPerBin) * invOsamp + (double)k*expct;
								gSumPhase[k] = fmod(gSumPhase[k] + tmp, 2.0 * M_PI);
							}
							gNearPeak[k] = last;
						}
						long next = -1;
						for (k = fftFrameSize2 - 1; k >= 0; k--) {
							if (gNearPeak[k] == k)
								next = k;
							long prev = gNearPeak[k];
							if (prev < 0 || (next >= 0 && next - k < k - prev))
								gNearPeak[k] = next;
						}
						for (k = 0; k < fftFrameSize2; k++) {
							long pk = gNearPeak[k];
							if (pk >= 0 && pk != k)
								gSumPhase[k] = fmod(gSumPhase[pk] + M_PI * (double)(k - pk), 2.0 * M_PI);
							else if (pk < 0) {
								tmp = gSynFreq[k] - (double)k*freqPerBin;
								tmp = 2.0 * M_PI * (tmp / freqPerBin) * invOsamp + (double)k*expct;
								gSumPhase[k] = fmod(gSumPhase[k] + tmp, 2.0 * M_PI);
							}
							magn = k == 0 ? 0 : gSynMagn[k];
							phase = gSumPhase[k];
							gFFTworksp[2*k] = magn*cos(phase);
							gFFTworksp[2*k+1] = magn*sin(phase);
						}
					}
					else {
					for (k = 0; k < fftFrameSize2; k++) {
						index = k*pitchShift;
						if (index < fftFrameSize2) {
							gSynMagn[index] += gAnaMagn[k];
							gSynFreq[index] = gAnaFreq[k] * pitchShift;
						}
					}

					memset(gFFTworksp, 0, fftFrameSize*sizeof(float));
					memset(gFFTworkspOut, 0, fftFrameSize*sizeof(float));

					for (k = 0; k < fftFrameSize2; k++) {
						magn = k==0 ? 0 : gSynMagn[k];
						tmp = gSynFreq[k];
						tmp -= (double)k*freqPerBin;
						tmp /= freqPerBin;
						tmp = 2.0f * M_PI * tmp * invOsamp;
						tmp += (double)k*expct;
						gSumPhase[k] = fmod(gSumPhase[k] + tmp, 2.0 * M_PI);
						phase = gSumPhase[k];
						gFFTworksp[2*k] = magn*cos(phase);
						gFFTworksp[2*k+1] = magn*sin(phase);
					}
					}

					pffft_transform_ordered(pffftSetup, gFFTworksp, gFFTworkspOut , NULL, PFFFT_BACKWARD);
					for(k=0; k < fftFrameSize; k++) {
						gOutputAccum[k] += 2.0f * gWindow[k] * gFFTworkspOut[k] * invFftFrameSize2 * invOsamp;
					}

					for (k = 0; k < stepSize; k++) gOutFIFO[k] = gOutputAccum[k];
					memmove(gOutputAccum, gOutputAccum+stepSize, fftFrameSize*sizeof(float));
					for (k = 0; k < inFifoLatency; k++) gInFIFO[k] = gInFIFO[k+stepSize];
				}
				return out;
	}
};
