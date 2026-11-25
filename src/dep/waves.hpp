#pragma once
#include <rack.hpp>

namespace waves {

std::vector<rack::dsp::Frame<1>> getMonoWav(const std::string path, const float currentSampleRate, std::string &waveFileName, std::string &waveExtension, int &sampleChannels, int &sampleRate, int &sampleCount);

// maxSeconds > 0: only about that much of the file (at its own rate) is decoded (WAV) and resampled.
std::vector<rack::dsp::Frame<2>> getStereoWav(const std::string path, const float currentSampleRate, std::string &waveFileName, std::string &waveExtension, int &sampleChannels, int &sampleRate, int &sampleCount, double maxSeconds = 0.0);

// Writes a 32-bit float stereo WAV; returns false if the file can't be written.
bool saveWave(const std::vector<rack::dsp::Frame<2>> &sample, int sampleRate, std::string path);

// The frames converted from one sample rate to another.
std::vector<rack::dsp::Frame<2>> resampleStereo(const std::vector<rack::dsp::Frame<2>> &frames, float fromRate, float toRate);

}
