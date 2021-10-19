#include <string.h>
#include <math.h>
#include <stdio.h>
#include "../pffft/pffft.h"
#include <vector>
#include <algorithm>
#include <mutex>

using namespace std;

struct FfftAnalysis {

	float *gInFIFO;
	float *gFFTworksp;
	float *gFFTworkspOut;
	float *gAnaMagn;
	float gSum;
	float sampleRate;
	PFFFT_Setup *pffftSetup;
	long gRover = false;
	double magn, window, real, imag;
	double invFftFrameSize;
	long fftFrameSize, osamp, i,k, qpd, index, inFifoLatency, stepSize, fftFrameSize2;
	long depth;

	FfftAnalysis(long fftFrameSize, long depth, long osamp, float sampleRate) {
		this->fftFrameSize = fftFrameSize;
		this->depth = depth;
		this->osamp = osamp;
		this->sampleRate = sampleRate;
		pffftSetup = pffft_new_setup(fftFrameSize, PFFFT_REAL);
		fftFrameSize2 = fftFrameSize/2;
		stepSize = fftFrameSize/osamp;
		inFifoLatency = fftFrameSize-stepSize;
		invFftFrameSize = 1.0f/fftFrameSize;

		gInFIFO = (float*)calloc(fftFrameSize,sizeof(float));
		gFFTworksp = (float*)pffft_aligned_malloc(fftFrameSize*sizeof(float));
		gFFTworkspOut =  (float*)pffft_aligned_malloc(fftFrameSize*sizeof(float));
		gAnaMagn = (float*)calloc(fftFrameSize,sizeof(float));
	}

	~FfftAnalysis() {
		pffft_destroy_setup(pffftSetup);
		free(gInFIFO);
		free(gAnaMagn);
		pffft_aligned_free(gFFTworksp);
		pffft_aligned_free(gFFTworkspOut);
	}

	void process(const float *input, vector<vector<float>> *result, vector<float> *sum, int min, int max) {

			for (i = 0; i < fftFrameSize; i++) {
				gInFIFO[gRover] = input[i];
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
					gSum = 0;

					for (k = 0; k <= fftFrameSize2; k++) {
						real = gFFTworkspOut[2*k];
						imag = gFFTworkspOut[2*k+1];
						magn = 2.*sqrt(real*real + imag*imag);
						gAnaMagn[k] = magn;
						if ((k>=min) && (k<=max)) gSum += magn;
					}

					std::vector<float> v(gAnaMagn, gAnaMagn + fftFrameSize2);

					if (result->size() == 0) {
						result->push_back(v);
						sum->push_back(gSum);
					}
					else if (long(result->size()) >= depth) {
						std::rotate(result->rbegin(), result->rbegin() + 1, result->rend());
						vector<vector<float>>& resultRef = *result;
						resultRef[0] = v;

						std::rotate(sum->rbegin(), sum->rbegin() + 1, sum->rend());
						vector<float>& sumRef = *sum;
						sumRef[0] = gSum;
					}
					else {
						result->push_back(v);
						std::rotate(result->rbegin(), result->rbegin() + 1, result->rend());

						sum->push_back(gSum);
						std::rotate(sum->rbegin(), sum->rbegin() + 1, sum->rend());
					}

					/* move input FIFO */
					for (k = 0; k < inFifoLatency; k++) gInFIFO[k] = gInFIFO[k+stepSize];
				}
			}
	}
};
