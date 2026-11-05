#pragma once
// Wavetable data and oscillator for LIMONADE.
//
// wtFrame / wtTable: the editable wavetable (time samples plus magnitude/phase
// bins for the spectrum editor). Only touched by non-audio threads, under the
// module's table mutex.
//
// wtPlayTable: an immutable, band-limited copy of the table built on a worker
// thread and handed to the audio thread. Each frame is stored as a set of
// mipmap levels; level k keeps at most 1024 >> k harmonics, so the oscillator
// can pick a level per voice that does not alias at its pitch.
//
// wtOscillator: 4 voices per SIMD vector. Reads two neighbouring frames and
// crossfades them, so scanning the table is smooth. minBLEP is only used for
// the real discontinuity of hard sync.

#include "dsp/minblep.hpp"
#include "../pffft/pffft.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

#define FS 2048
#define FS2 1024
#define NF 256
#define IFS (1.0f/FS)
#define IFS2 (1.0f/FS2)
#define IM_PI (1.0f/M_PI)

using simd::float_4;

// One pffft setup and work buffers per size and per thread, created on first use.
struct wtFFT {
  int n;
  PFFFT_Setup *setup;
  float *in;
  float *out;

  explicit wtFFT(int n) : n(n) {
    setup = pffft_new_setup(n, PFFFT_REAL);
    in = (float*)pffft_aligned_malloc(n * sizeof(float));
    out = (float*)pffft_aligned_malloc(n * sizeof(float));
  }
  ~wtFFT() {
    pffft_destroy_setup(setup);
    pffft_aligned_free(in);
    pffft_aligned_free(out);
  }
  void forward() { pffft_transform_ordered(setup, in, out, NULL, PFFFT_FORWARD); }
  void backward() { pffft_transform_ordered(setup, in, out, NULL, PFFFT_BACKWARD); }

  // n must be a power of two between 64 and FS.
  static wtFFT &get(int n) {
    thread_local std::unique_ptr<wtFFT> cache[12];
    int k = 0;
    while ((1 << k) < n) k++;
    if (!cache[k])
      cache[k].reset(new wtFFT(n));
    return *cache[k];
  }
};

struct wtFrame {
  std::vector<float> sample;
  // Bin 0 is DC (magnitude = |mean|, phase 0 or pi), bins 1..FS2-1 are harmonics
  // (magnitude = peak amplitude). The Nyquist bin is not edited.
  std::vector<float> magnitude;
  std::vector<float> phase;
  bool morphed = false;
  bool used = false;

  wtFrame() {
    sample.resize(FS, 0.f);
    magnitude.resize(FS2, 0.f);
    phase.resize(FS2, 0.f);
  }

  void calcFFT() {
    wtFFT &fft = wtFFT::get(FS);
    std::copy(sample.begin(), sample.end(), fft.in);
    fft.forward();
    // pffft ordered layout: out[0] = DC, out[1] = Nyquist, then (re, im) pairs.
    magnitude[0] = std::fabs(fft.out[0]) / FS;
    phase[0] = fft.out[0] < 0.f ? M_PI : 0.f;
    for (size_t k = 1; k < FS2; k++) {
      float re = fft.out[2 * k];
      float im = fft.out[2 * k + 1];
      if (std::fabs(re) > 1e-2f || std::fabs(im) > 1e-2f) {
        magnitude[k] = 2.f * std::sqrt(re * re + im * im) / FS;
        phase[k] = std::atan2(im, re);
      }
      else {
        magnitude[k] = 0.f;
        phase[k] = 0.f;
      }
    }
  }

  void calcIFFT() {
    wtFFT &fft = wtFFT::get(FS);
    fft.in[0] = magnitude[0] * std::cos(phase[0]);
    fft.in[1] = 0.f;
    for (size_t k = 1; k < FS2; k++) {
      fft.in[2 * k] = 0.5f * magnitude[k] * std::cos(phase[k]);
      fft.in[2 * k + 1] = 0.5f * magnitude[k] * std::sin(phase[k]);
    }
    fft.backward();
    std::copy(fft.out, fft.out + FS, sample.begin());
  }

  float maxAmp() const {
    float amp = 0.f;
    for (float s : sample) amp = std::max(amp, std::fabs(s));
    return amp;
  }

  void gain(float g) {
    for (float &s : sample) s *= g;
  }

  void normalize() {
    float amp = maxAmp();
    gain(amp > 0.f ? 1.f / amp : 0.f);
  }

  // Crossfades the first and last 16 samples towards their average, so the
  // frame loops without a jump.
  void smooth() {
    for (size_t i = 0; i < 16; i++) {
      float avg = (sample[i] + sample[FS - i - 1]) * 0.5f;
      sample[i] = ((16 - i) * avg + i * sample[i]) / 16.f;
      sample[FS - i - 1] = ((16 - i) * avg + i * sample[FS - i - 1]) / 16.f;
    }
  }

  // Short fade in and out at the frame edges.
  void window() {
    for (size_t i = 0; i < FS; i++) {
      float w = std::min((float)(-10.f * std::cos(2.f * M_PI * (float)i / FS) + 10.f), 1.f);
      sample[i] *= w;
    }
  }

  void removeDCOffset() {
    float mean = 0.f;
    for (float s : sample) mean += s;
    mean /= FS;
    for (float &s : sample) s -= mean;
  }

  // Fills the frame from sCount samples, stretched to FS if interpolate is set.
  void loadSample(size_t sCount, bool interpolate, const float *wav) {
    if (sCount == 0) {
      std::fill(sample.begin(), sample.end(), 0.f);
      return;
    }
    if (interpolate) {
      if (sCount == 1) {
        std::fill(sample.begin(), sample.end(), wav[0]);
        return;
      }
      // One cycle: the last sample leads back to the first, and FS samples copy as is.
      double step = (double)sCount / FS;
      for (size_t i = 0; i < FS; i++) {
        double pos = i * step;
        size_t index = std::min((size_t)pos, sCount - 1);
        float t = (float)(pos - index);
        float next = wav[(index + 1) % sCount];
        sample[i] = wav[index] + t * (next - wav[index]);
      }
    }
    else {
      for (size_t i = 0; i < FS; i++)
        sample[i] = i < sCount ? wav[i] : 0.f;
    }
  }

  void reset() {
    std::fill(sample.begin(), sample.end(), 0.f);
    std::fill(magnitude.begin(), magnitude.end(), 0.f);
    std::fill(phase.begin(), phase.end(), 0.f);
    used = false;
    morphed = false;
  }
};

struct wtTable {
  std::vector<wtFrame> frames;
  size_t nFrames = 0;

  wtTable() {
    frames.resize(NF);
  }

  // Frame under a 0..1 position, as shown by the editor. Only valid if nFrames > 0.
  size_t frameAt(float pos) const {
    if (nFrames == 0)
      return 0;
    return std::min((size_t)(clamp(pos, 0.f, 1.f) * (nFrames - 1)), nFrames - 1);
  }

  void reset() {
    for (auto &frame : frames) frame.reset();
    nFrames = 0;
  }

  void copyFrame(size_t from, size_t to) {
    frames[to].sample = frames[from].sample;
    frames[to].magnitude = frames[from].magnitude;
    frames[to].phase = frames[from].phase;
  }

  // Slices sCount samples into frames of frameSize samples.
  void loadSample(size_t sCount, size_t frameSize, bool interpolate, const float *sample) {
    reset();
    frameSize = std::max(frameSize, (size_t)1);
    size_t sUsed = 0;
    while (sUsed < sCount && nFrames < NF) {
      size_t lenFrame = std::min(frameSize, sCount - sUsed);
      frames[nFrames].loadSample(lenFrame, interpolate, sample + sUsed);
      frames[nFrames].used = true;
      sUsed += lenFrame;
      nFrames++;
    }
  }

  void calcFFT() {
    for (size_t i = 0; i < nFrames; i++) frames[i].calcFFT();
  }

  void normalize() {
    float amp = 0.f;
    for (size_t i = 0; i < nFrames; i++) amp = std::max(amp, frames[i].maxAmp());
    float g = amp > 0.f ? 1.f / amp : 0.f;
    for (size_t i = 0; i < nFrames; i++) frames[i].gain(g);
  }

  void normalizeAllFrames() {
    for (size_t i = 0; i < nFrames; i++) frames[i].normalize();
  }

  void smooth() {
    for (size_t i = 0; i < nFrames; i++) frames[i].smooth();
  }

  void window() {
    for (size_t i = 0; i < nFrames; i++) frames[i].window();
  }

  void removeDCOffset() {
    for (size_t i = 0; i < nFrames; i++) frames[i].removeDCOffset();
  }

  // Inserts an empty frame after index (or the first frame of an empty table).
  void addFrame(size_t index) {
    if (nFrames >= NF)
      return;
    size_t at = nFrames == 0 ? 0 : std::min(index, nFrames - 1) + 1;
    for (size_t i = nFrames; i > at; i--) {
      copyFrame(i - 1, i);
      frames[i].used = frames[i - 1].used;
      frames[i].morphed = frames[i - 1].morphed;
    }
    frames[at].reset();
    frames[at].used = true;
    nFrames++;
  }

  void removeFrame(size_t index) {
    if (index >= nFrames)
      return;
    for (size_t i = index; i + 1 < nFrames; i++) {
      copyFrame(i + 1, i);
      frames[i].used = frames[i + 1].used;
      frames[i].morphed = frames[i + 1].morphed;
    }
    nFrames--;
    frames[nFrames].reset();
  }

  // Removes generated frames, keeping the original ones in order.
  void deleteMorphing() {
    size_t cu = 0;
    for (size_t i = 0; i < nFrames; i++) {
      if (frames[i].morphed)
        continue;
      if (i != cu)
        copyFrame(i, cu);
      frames[cu].used = true;
      frames[cu].morphed = false;
      cu++;
    }
    for (size_t i = cu; i < nFrames; i++) frames[i].reset();
    nFrames = cu;
  }

  enum MorphMode { MORPH_WAVE, MORPH_SPECTRUM, MORPH_SPECTRUM_CONSTANT_PHASE };

  // Spreads the original frames over the whole table and fills the gaps with
  // interpolated frames.
  void morph(MorphMode mode) {
    deleteMorphing();
    if (nFrames < 2)
      return;
    size_t fs = nFrames;
    size_t fCount = (NF - fs) / (fs - 1);
    size_t step = fCount + 1;

    if (mode != MORPH_WAVE) {
      for (size_t i = 0; i < fs; i++) frames[i].calcFFT();
      if (mode == MORPH_SPECTRUM_CONSTANT_PHASE) {
        for (size_t i = 1; i < fs; i++) {
          frames[i].phase = frames[0].phase;
          frames[i].calcIFFT();
        }
      }
    }

    // Move originals to their final slots, last first so nothing is overwritten.
    for (size_t i = fs - 1; i > 0; i--) {
      copyFrame(i, i * step);
      frames[i * step].morphed = false;
      frames[i * step].used = true;
    }

    for (size_t i = 0; i + 1 < fs; i++) {
      const wtFrame &a = frames[i * step];
      const wtFrame &b = frames[(i + 1) * step];
      for (size_t j = 1; j < step; j++) {
        wtFrame &f = frames[i * step + j];
        float t = (float)j / step;
        if (mode == MORPH_WAVE) {
          for (size_t k = 0; k < FS; k++)
            f.sample[k] = a.sample[k] + t * (b.sample[k] - a.sample[k]);
        }
        else {
          for (size_t k = 0; k < FS2; k++) {
            f.magnitude[k] = a.magnitude[k] + t * (b.magnitude[k] - a.magnitude[k]);
            // Shortest way round the circle.
            float d = b.phase[k] - a.phase[k];
            d -= 2.f * M_PI * std::floor((d + M_PI) / (2.f * M_PI));
            f.phase[k] = a.phase[k] + t * d;
          }
          f.calcIFFT();
        }
        f.morphed = true;
        f.used = true;
      }
    }
    nFrames = (fs - 1) * step + 1;
  }
};

static const int WT_LEVELS = 11;

struct wtPlayTable {
  size_t nFrames = 0;
  int len[WT_LEVELS];
  size_t off[WT_LEVELS];
  size_t frameStride = 0;
  std::vector<float> data;

  wtPlayTable() {
    // Level 0 is the frame itself (up to 1024 harmonics). Level k >= 1 keeps
    // 1024 >> k harmonics in a table twice that size, at least 64 samples.
    size_t o = 0;
    for (int k = 0; k < WT_LEVELS; k++) {
      len[k] = k == 0 ? FS : std::max(FS >> (k - 1), 64);
      off[k] = o;
      o += len[k] + 1; // +1 guard sample = first sample, for interpolation
    }
    frameStride = o;
  }

  const float *level(size_t frame, int k) const {
    return data.data() + frame * frameStride + off[k];
  }

  // samples: n frames of FS samples each.
  void build(const float *samples, size_t n) {
    nFrames = n;
    data.assign(n * frameStride, 0.f);
    // Level 1 uses an FFT of the same size, so keep the spectrum apart.
    std::vector<float> spectrum(FS);
    for (size_t f = 0; f < n; f++) {
      const float *src = samples + f * FS;
      float *dst = data.data() + f * frameStride;
      std::copy(src, src + FS, dst + off[0]);
      dst[off[0] + FS] = src[0];

      wtFFT &full = wtFFT::get(FS);
      std::copy(src, src + FS, full.in);
      full.forward();
      std::copy(full.out, full.out + FS, spectrum.begin());
      for (int k = 1; k < WT_LEVELS; k++) {
        int m = len[k];
        int harmonics = FS2 >> k;
        wtFFT &fft = wtFFT::get(m);
        std::fill(fft.in, fft.in + m, 0.f);
        fft.in[0] = spectrum[0];
        for (int b = 1; b <= harmonics; b++) {
          fft.in[2 * b] = spectrum[2 * b];
          fft.in[2 * b + 1] = spectrum[2 * b + 1];
        }
        fft.backward();
        float *lv = dst + off[k];
        for (int i = 0; i < m; i++) lv[i] = fft.out[i] * IFS;
        lv[m] = lv[0];
      }
    }
  }
};

template <typename T = float_4>
struct wtOscillator {
  bool soft = false;
  bool syncEnabled = false;
  T phase = 0.f;
  T lastSyncValue = 0.f;
  T syncDirection = 1.f;
  dsp::MinBlepGenerator<16, 16, T> minBLEP;

  // pos: 0..1 position in the table, level: mipmap level (whole numbers).
  static T read(const wtPlayTable *t, T phase, T pos, T level, int lanes) {
    // Only the table lookups are per lane; the interpolation runs on all 4 at once.
    T fp = pos * (float)(t->nFrames - 1);
    T f0 = simd::floor(fp);
    T ft = fp - f0;
    T x;
    for (int i = 0; i < 4; i++) x[i] = phase[i] * t->len[(int)level[i]];
    T x0 = simd::floor(x);
    T xt = x - x0;
    T a0 = 0.f, a1 = 0.f, b0 = 0.f, b1 = 0.f;
    int lastFrame = (int)t->nFrames - 1;
    for (int i = 0; i < lanes; i++) {
      int f = (int)f0[i];
      const float *a = t->level(f, (int)level[i]) + (int)x0[i];
      const float *b = f < lastFrame ? a + t->frameStride : a;
      a0[i] = a[0];
      a1[i] = a[1];
      b0[i] = b[0];
      b1[i] = b[1];
    }
    T va = a0 + xt * (a1 - a0);
    T vb = b0 + xt * (b1 - b0);
    return va + ft * (vb - va);
  }

  T process(const wtPlayTable *t, float deltaTime, T freq, T pos, T level, T syncValue, int lanes) {
    T deltaPhase = simd::clamp(freq * deltaTime, 1e-6f, 0.35f);
    if (soft)
      deltaPhase *= syncDirection;
    else
      syncDirection = 1.f;
    phase += deltaPhase;
    phase -= simd::floor(phase);

    if (syncEnabled) {
      T deltaSync = syncValue - lastSyncValue;
      T syncCrossing = -lastSyncValue / deltaSync;
      lastSyncValue = syncValue;
      T sync = (0.f < syncCrossing) & (syncCrossing <= 1.f) & (syncValue >= 0.f);
      int syncMask = simd::movemask(sync) & ((1 << lanes) - 1);
      if (syncMask) {
        if (soft) {
          syncDirection = simd::ifelse(sync, -syncDirection, syncDirection);
        }
        else {
          T newPhase = simd::ifelse(sync, (1.f - syncCrossing) * deltaPhase, phase);
          T jump = read(t, newPhase, pos, level, lanes) - read(t, phase, pos, level, lanes);
          for (int i = 0; i < lanes; i++) {
            if (syncMask & (1 << i)) {
              T mask = simd::movemaskInverse<T>(1 << i);
              minBLEP.insertDiscontinuity(syncCrossing[i] - 1.f, mask & jump);
            }
          }
          phase = newPhase;
        }
      }
    }

    return read(t, phase, pos, level, lanes) + minBLEP.process();
  }
};
