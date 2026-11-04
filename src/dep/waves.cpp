#include "waves.hpp"
#include "AudioFile/AudioFile.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav/dr_wav.h"
#include <dsp/resampler.hpp>

namespace waves {

  // Reads a WAV or AIFF file into frames of N channels (mono: the mean of
  // the first two channels; stereo: the first two, a mono file doubled), then
  // resamples it to currentSampleRate. On failure the result is empty and
  // sampleCount is 0.
  template <int N>
  static std::vector<rack::dsp::Frame<N>> loadFrames(const std::string &path, const float currentSampleRate, std::string &waveFileName, std::string &waveExtension, int &sampleChannels, int &sampleRate, int &sampleCount) {
    waveFileName = rack::system::getFilename(path);
    waveExtension = rack::system::getExtension(waveFileName);
    std::string ext = rack::string::uppercase(waveExtension);
    std::vector<rack::dsp::Frame<N>> result;
    sampleCount = 0;

    auto push = [&](float l, float r) {
      rack::dsp::Frame<N> frame;
      if (N == 1)
        frame.samples[0] = 0.5f * (l + r);
      else {
        frame.samples[0] = l;
        frame.samples[N - 1] = r;
      }
      result.push_back(frame);
    };

    if (ext == ".WAV") {
      unsigned int c;
      unsigned int sr;
      drwav_uint64 frames;
      float* pSampleData = drwav_open_file_and_read_pcm_frames_f32(path.c_str(), &c, &sr, &frames, NULL);
      if (pSampleData != NULL)  {
        sampleChannels = c;
        sampleRate = sr;
        if (c > 0) {
          result.reserve(frames);
          // The data holds `frames` frames of c interleaved samples each.
          for (drwav_uint64 f = 0; f < frames; f++) {
            const float *s = pSampleData + f * c;
            push(s[0], c >= 2 ? s[1] : s[0]);
          }
        }
        drwav_free(pSampleData, NULL);
      }
    }
    else if ((ext == ".AIFF") || (ext == ".AIF")) {
      AudioFile<float> audioFile;
      if (audioFile.load (path.c_str()))  {
        sampleChannels = audioFile.getNumChannels();
        sampleRate = audioFile.getSampleRate();
        int frames = audioFile.getNumSamplesPerChannel();
        if (sampleChannels > 0) {
          result.reserve(frames);
          for (int i = 0; i < frames; i++)
            push(audioFile.samples[0][i], sampleChannels >= 2 ? audioFile.samples[1][i] : audioFile.samples[0][i]);
        }
      }
    }
    sampleCount = result.size();

    if ((sampleCount > 0) && (sampleRate > 0) && (sampleRate != currentSampleRate)) {
      rack::dsp::SampleRateConverter<N> conv;
      conv.setRates(sampleRate, currentSampleRate);
      conv.setQuality(SPEEX_RESAMPLER_QUALITY_DESKTOP);
      std::vector<rack::dsp::Frame<N>> subResult((size_t)std::ceil((double)sampleCount * currentSampleRate / sampleRate) + 256);
      int inDone = 0, outDone = 0;
      while (inDone < sampleCount && outDone < (int)subResult.size()) {
        int inCount = sampleCount - inDone;
        int outCount = subResult.size() - outDone;
        conv.process(&result[inDone], &inCount, &subResult[outDone], &outCount);
        if (inCount == 0 && outCount == 0)
          break;
        inDone += inCount;
        outDone += outCount;
      }
      subResult.resize(outDone);
      sampleCount = outDone;
      return subResult;
    }

    return result;
  }

  std::vector<rack::dsp::Frame<1>> getMonoWav(const std::string path, const float currentSampleRate, std::string &waveFileName, std::string &waveExtension, int &sampleChannels, int &sampleRate, int &sampleCount) {
    return loadFrames<1>(path, currentSampleRate, waveFileName, waveExtension, sampleChannels, sampleRate, sampleCount);
  }

  std::vector<rack::dsp::Frame<2>> getStereoWav(const std::string path, const float currentSampleRate, std::string &waveFileName, std::string &waveExtension, int &sampleChannels, int &sampleRate, int &sampleCount) {
    return loadFrames<2>(path, currentSampleRate, waveFileName, waveExtension, sampleChannels, sampleRate, sampleCount);
  }

  void saveWave(std::vector<rack::dsp::Frame<2>> &sample, int sampleRate, std::string path) {
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_PCM;
    format.channels = 2;
    format.sampleRate = sampleRate;
    format.bitsPerSample = 32;

    int *pSamples = (int*)calloc(2*sample.size(),sizeof(int));
    memset(pSamples, 0, 2*sample.size()*sizeof(int));
    for (unsigned int i = 0; i < sample.size(); i++) {
    	*(pSamples+2*i)= floor(sample[i].samples[0]*1990000000);
    	*(pSamples+2*i+1)= floor(sample[i].samples[1]*1990000000);
    }

    drwav wav;
    drwav_init_file_write(&wav, path.c_str(), &format, NULL);
    drwav_write_pcm_frames(&wav, 2*sample.size(), pSamples);
    drwav_uninit(&wav);

    free(pSamples);
  }

}
