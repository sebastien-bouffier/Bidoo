#include <string.h>
#include <math.h>
#include <stdio.h>
#include "../pffft/pffft.h"

using namespace std;

// Phase vocoder pitch shifter (after Stephan Bernsee's smbPitchShift), one
// sample in and one out, the FFT work done every hop. Float throughout, with
// fast atan2 / sincos and floor-based phase wraps (no fmod).
namespace pitchshifter_detail {

constexpr float kPi = 3.14159265358979f;
constexpr float kTwoPi = 6.28318530717959f;
constexpr float kInvTwoPi = 0.159154943091895f;

// wraps x into [-pi, pi)
inline float wrapPi(float x) {
	return x - kTwoPi * floorf(x * kInvTwoPi + 0.5f);
}

// atan2 within 1e-5 rad: a degree-11 odd minimax polynomial on [0, 1] and the octant folds
inline float fastAtan2(float y, float x) {
	float ax = fabsf(x), ay = fabsf(y);
	float mx = ax > ay ? ax : ay, mn = ax > ay ? ay : ax;
	if (mx == 0.f)
		return 0.f;
	float a = mn / mx;
	float s = a * a;
	float r = ((((-0.0117212f * s + 0.05265332f) * s - 0.11643287f) * s + 0.19354346f) * s - 0.33262347f) * s * a + 0.99997726f * a;
	if (ay > ax)
		r = 1.57079637f - r;
	if (x < 0.f)
		r = kPi - r;
	return y < 0.f ? -r : r;
}

// sin and cos of x in [-pi, pi] within 2e-7: quadrant fold, then degree 7 / 8 Taylor on [-pi/4, pi/4]
inline void fastSinCos(float x, float &s, float &c) {
	float q = floorf(x * 0.636619772f + 0.5f);  // nearest multiple of pi/2
	float r = x - q * 1.57079637f;
	float r2 = r * r;
	float sr = r * (1.f + r2 * (-1.f / 6.f + r2 * (1.f / 120.f + r2 * (-1.f / 5040.f))));
	float cr = 1.f + r2 * (-0.5f + r2 * (1.f / 24.f + r2 * (-1.f / 720.f + r2 * (1.f / 40320.f))));
	switch (((int)q) & 3) {
		case 0: s = sr; c = cr; break;
		case 1: s = cr; c = -sr; break;
		case 2: s = -sr; c = -cr; break;
		default: s = -cr; c = sr; break;
	}
}

}  // namespace pitchshifter_detail

struct PitchShifter {
	float *gInFIFO = nullptr;
	float *gOutFIFO = nullptr;
	float *gFFTworksp = nullptr;
	float *gFFTworkspOut = nullptr;
	float *gLastPhase = nullptr;
	float *gSumPhase = nullptr;
	float *gOutputAccum = nullptr;
	float *gAnaFreq = nullptr;
	float *gAnaMagn = nullptr;
	float *gSynFreq = nullptr;
	float *gSynMagn = nullptr;
	float sampleRate = 48000.f;
	PFFFT_Setup *pffftSetup = nullptr;
	long gRover = 0;
	// identity phase locking with a resampled spectrum (HCTIP); off keeps the
	// original bin mapping
	bool phaseLock = false;
	float *gAnaPhase = nullptr;
	long *gSrc = nullptr;
	long *gNearPeak = nullptr;
	float *gWindow = nullptr;  // Hann, worked out once
	float freqPerBin = 0.f, expct = 0.f, invOsamp = 0.f, outScale = 0.f;
	long fftFrameSize = 0, osamp = 0, inFifoLatency = 0, stepSize = 0, fftFrameSize2 = 0;

	PitchShifter() {
	}

	void init(long fftFrameSize, long osamp, float sampleRate) {
		this->fftFrameSize = fftFrameSize;
		this->osamp = osamp;
		this->sampleRate = sampleRate;

		pffftSetup = pffft_new_setup(fftFrameSize, PFFFT_REAL);

		fftFrameSize2 = fftFrameSize / 2;
		stepSize = fftFrameSize / osamp;
		freqPerBin = sampleRate / (float)fftFrameSize;
		expct = 2.f * (float)M_PI * (float)stepSize / (float)fftFrameSize;
		inFifoLatency = fftFrameSize - stepSize;
		invOsamp = 1.f / osamp;
		// the analysis and synthesis Hann windows overlap-add to 3/8 osamp: divided
		// out here, so a sine comes out at its own level with no shift
		outScale = 2.f / (float)fftFrameSize2 * invOsamp / (0.375f * (float)osamp);

		gInFIFO = new float[fftFrameSize] {0.f};
		gOutFIFO = new float[fftFrameSize] {0.f};
		gFFTworksp = (float*)pffft_aligned_malloc(fftFrameSize * sizeof(float));
		gFFTworkspOut = (float*)pffft_aligned_malloc(fftFrameSize * sizeof(float));
		gLastPhase = new float[fftFrameSize2 + 1] {0.f};
		gSumPhase = new float[fftFrameSize2 + 1] {0.f};
		gOutputAccum = new float[2 * fftFrameSize] {0.f};
		gAnaFreq = new float[fftFrameSize] {0.f};
		gAnaMagn = new float[fftFrameSize] {0.f};
		gSynFreq = new float[fftFrameSize] {0.f};
		gAnaPhase = new float[fftFrameSize] {0.f};
		gSrc = new long[fftFrameSize] {0};
		gNearPeak = new long[fftFrameSize] {0};
		gWindow = new float[fftFrameSize];
		for (long k = 0; k < fftFrameSize; k++)
			gWindow[k] = -0.5 * cos(2.0 * M_PI * (double)k / (double)fftFrameSize) + 0.5;
		gSynMagn = new float[fftFrameSize] {0.f};
	}

	// silence in every buffer (after a pause, so nothing old plays out)
	void clear() {
		memset(gInFIFO, 0, fftFrameSize * sizeof(float));
		memset(gOutFIFO, 0, fftFrameSize * sizeof(float));
		memset(gLastPhase, 0, (fftFrameSize2 + 1) * sizeof(float));
		memset(gSumPhase, 0, (fftFrameSize2 + 1) * sizeof(float));
		memset(gOutputAccum, 0, 2 * fftFrameSize * sizeof(float));
		gRover = 0;
	}

	~PitchShifter() {
		if (pffftSetup)
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
		if (gFFTworksp)
			pffft_aligned_free(gFFTworksp);
		if (gFFTworkspOut)
			pffft_aligned_free(gFFTworkspOut);
	}

	void process(const float pitchShift, const float *input, float *output) {
		for (long n = 0; n < fftFrameSize; n++)
			output[n] = step(pitchShift, input[n]);
	}

	// One sample in, one out: the FFT work is done every stepSize samples
	// instead of all at once every fftFrameSize.
	float step(const float pitchShift, const float in) {
		using namespace pitchshifter_detail;
		float out;
		gInFIFO[gRover] = in;

		if (gRover >= inFifoLatency)  // [bsp] 09Mar2019: this fixes the noise burst issue in REI
			out = gOutFIFO[gRover - inFifoLatency];
		else
			out = 0.0f;

		gRover++;

		if (gRover >= fftFrameSize) {
			gRover = inFifoLatency;

			for (long k = 0; k < fftFrameSize; k++)
				gFFTworksp[k] = gInFIFO[k] * gWindow[k];

			pffft_transform_ordered(pffftSetup, gFFTworksp, gFFTworkspOut, NULL, PFFFT_FORWARD);

			// pffft's ordered layout keeps DC in [0] and Nyquist in [1]: neither is a
			// partial to shift, so bin 0 is left empty instead of reading both as one
			gAnaMagn[0] = 0.f;
			gAnaFreq[0] = 0.f;
			gAnaPhase[0] = 0.f;
			const float binScale = (float)osamp * 0.5f / kPi;
			for (long k = 1; k < fftFrameSize2; k++) {
				float real = gFFTworkspOut[2 * k];
				float imag = gFFTworkspOut[2 * k + 1];
				float phase = fastAtan2(imag, real);
				float tmp = phase - gLastPhase[k];
				gLastPhase[k] = phase;
				// the deviation from the bin's expected advance, wrapped into [-pi, pi)
				tmp = wrapPi(tmp - (float)k * expct);
				gAnaMagn[k] = 2.f * sqrtf(real * real + imag * imag);
				gAnaFreq[k] = ((float)k + tmp * binScale) * freqPerBin;
				gAnaPhase[k] = phase;
			}

			memset(gSynMagn, 0, fftFrameSize * sizeof(float));
			memset(gSynFreq, 0, fftFrameSize * sizeof(float));
			memset(gFFTworksp, 0, fftFrameSize * sizeof(float));

			const float invFreqPerBin = 1.f / freqPerBin;
			const float advance = kTwoPi * invOsamp;
			if (phaseLock) {
				// synthesis bin k takes the spectrum at analysis bin k / pitchShift
				const float invShift = 1.f / pitchShift;
				for (long k = 0; k < fftFrameSize2; k++) {
					float src = (float)k * invShift;
					long k0 = (long)src;
					float fr = src - (float)k0;
					if (k0 + 1 < fftFrameSize2) {
						gSynMagn[k] = gAnaMagn[k0] + (gAnaMagn[k0 + 1] - gAnaMagn[k0]) * fr;
						gSrc[k] = fr < 0.5f ? k0 : k0 + 1;
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
				for (long k = 0; k < fftFrameSize2; k++) {
					bool peak = k > 0 && k < fftFrameSize2 - 1 && gSynMagn[k] > 0.f && gSynMagn[k] > gSynMagn[k - 1] && gSynMagn[k] >= gSynMagn[k + 1];
					if (peak) {
						last = k;
						float tmp = (gSynFreq[k] - (float)k * freqPerBin) * invFreqPerBin;
						gSumPhase[k] = wrapPi(gSumPhase[k] + tmp * advance + (float)k * expct);
					}
					gNearPeak[k] = last;
				}
				long next = -1;
				for (long k = fftFrameSize2 - 1; k >= 0; k--) {
					if (gNearPeak[k] == k)
						next = k;
					long prev = gNearPeak[k];
					if (prev < 0 || (next >= 0 && next - k < k - prev))
						gNearPeak[k] = next;
				}
				for (long k = 1; k < fftFrameSize2; k++) {
					long pk = gNearPeak[k];
					if (pk >= 0 && pk != k)
						gSumPhase[k] = wrapPi(gSumPhase[pk] + kPi * (float)((k - pk) & 1));
					else if (pk < 0) {
						float tmp = (gSynFreq[k] - (float)k * freqPerBin) * invFreqPerBin;
						gSumPhase[k] = wrapPi(gSumPhase[k] + tmp * advance + (float)k * expct);
					}
					float s, c;
					fastSinCos(gSumPhase[k], s, c);
					gFFTworksp[2 * k] = gSynMagn[k] * c;
					gFFTworksp[2 * k + 1] = gSynMagn[k] * s;
				}
			}
			else {
				for (long k = 0; k < fftFrameSize2; k++) {
					long index = (long)(k * pitchShift);
					if (index < fftFrameSize2) {
						gSynMagn[index] += gAnaMagn[k];
						gSynFreq[index] = gAnaFreq[k] * pitchShift;
					}
				}
				for (long k = 1; k < fftFrameSize2; k++) {
					float tmp = (gSynFreq[k] - (float)k * freqPerBin) * invFreqPerBin;
					gSumPhase[k] = wrapPi(gSumPhase[k] + tmp * advance + (float)k * expct);
					float s, c;
					fastSinCos(gSumPhase[k], s, c);
					gFFTworksp[2 * k] = gSynMagn[k] * c;
					gFFTworksp[2 * k + 1] = gSynMagn[k] * s;
				}
			}

			pffft_transform_ordered(pffftSetup, gFFTworksp, gFFTworkspOut, NULL, PFFFT_BACKWARD);
			for (long k = 0; k < fftFrameSize; k++)
				gOutputAccum[k] += gWindow[k] * gFFTworkspOut[k] * outScale;

			for (long k = 0; k < stepSize; k++)
				gOutFIFO[k] = gOutputAccum[k];
			memmove(gOutputAccum, gOutputAccum + stepSize, fftFrameSize * sizeof(float));
			memmove(gInFIFO, gInFIFO + stepSize, inFifoLatency * sizeof(float));
		}
		return out;
	}
};
