#pragma once
#include <system.hpp>
#include <dsp/common.hpp>

namespace waves {

std::vector<rack::dsp::Frame<1>> getMonoWav(const std::string path, const float currentSampleRate, std::string &waveFileName, std::string &waveExtension, int &sampleChannels, int &sampleRate, int &sampleCount);

std::vector<rack::dsp::Frame<2>> getStereoWav(const std::string path, const float currentSampleRate, std::string &waveFileName, std::string &waveExtension, int &sampleChannels, int &sampleRate, int &sampleCount);

void saveWave(std::vector<rack::dsp::Frame<2>> &sample, int sampleRate, std::string path);

}
