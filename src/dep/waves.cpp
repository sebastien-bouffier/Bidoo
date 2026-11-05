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
  static std::vector<rack::dsp::Frame<N>> resample(const std::vector<rack::dsp::Frame<N>> &in, float fromRate, float toRate) {
    rack::dsp::SampleRateConverter<N> conv;
    conv.setRates(fromRate, toRate);
    conv.setQuality(SPEEX_RESAMPLER_QUALITY_DESKTOP);
    int count = in.size();
    std::vector<rack::dsp::Frame<N>> out((size_t)std::ceil((double)count * toRate / fromRate) + 256);
    int inDone = 0, outDone = 0;
    while (inDone < count && outDone < (int)out.size()) {
      int inCount = count - inDone;
      int outCount = out.size() - outDone;
      conv.process(&in[inDone], &inCount, &out[outDone], &outCount);
      if (inCount == 0 && outCount == 0)
        break;
      inDone += inCount;
      outDone += outCount;
    }
    out.resize(outDone);
    return out;
  }

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
      result = resample<N>(result, sampleRate, currentSampleRate);
      sampleCount = result.size();
    }

    return result;
  }

  std::vector<rack::dsp::Frame<1>> getMonoWav(const std::string path, const float currentSampleRate, std::string &waveFileName, std::string &waveExtension, int &sampleChannels, int &sampleRate, int &sampleCount) {
    return loadFrames<1>(path, currentSampleRate, waveFileName, waveExtension, sampleChannels, sampleRate, sampleCount);
  }

  std::vector<rack::dsp::Frame<2>> getStereoWav(const std::string path, const float currentSampleRate, std::string &waveFileName, std::string &waveExtension, int &sampleChannels, int &sampleRate, int &sampleCount) {
    return loadFrames<2>(path, currentSampleRate, waveFileName, waveExtension, sampleChannels, sampleRate, sampleCount);
  }

  std::vector<rack::dsp::Frame<2>> resampleStereo(const std::vector<rack::dsp::Frame<2>> &frames, float fromRate, float toRate) {
    return resample<2>(frames, fromRate, toRate);
  }

  // Float, so nothing above full scale clips (a recording of a 10 V signal
  // is stored at 2.0).
  bool saveWave(const std::vector<rack::dsp::Frame<2>> &sample, int sampleRate, std::string path) {
    drwav_data_format format;
    format.container = drwav_container_riff;
    format.format = DR_WAVE_FORMAT_IEEE_FLOAT;
    format.channels = 2;
    format.sampleRate = sampleRate;
    format.bitsPerSample = 32;

    drwav wav;
    if (!drwav_init_file_write(&wav, path.c_str(), &format, NULL))
      return false;
    // Frame<2> is two packed floats: the frames are already interleaved.
    drwav_uint64 written = drwav_write_pcm_frames(&wav, sample.size(), sample.data());
    drwav_uninit(&wav);
    return written == sample.size();
  }

}
