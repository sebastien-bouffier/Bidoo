// Plays factory presets in Rack's own engine, without a window, and prints what each output does: a way to hear a
// sequencer preset on paper. Each preset (presets/<slug>/*.vcvm) is loaded into a fresh module (fromJson, as Rack's
// preset menu does), a clock is fed to the inputs named like a clock, and the module runs. For each output, on every
// clock tick: a gate output prints x (a rising edge since the last tick) or . ; a CV output prints its voltage at the
// tick, as a note name when it looks like V/oct (C4 = 0 V).
//
// Run: python tools/tests/preset_play.py SLUG [SLUG ...] [--bpm 120] [--ppq 4] [--ticks 32] [--in NAME=SPEC ...]
//   --ppq: clock pulses per beat fed to the clock inputs (4: sixteenths); --in: an input by (part of) its name,
//   SPEC = clock (the clock), gate:HZ, cv:VOLTS, off. Without --in, inputs named "reset" are patched and held at 0 V,
//   inputs named "clock" get the clock (unless they also name a tempo, a start or a run: off), others are unpatched.
//   Every input starts low, so a clock's or a gate's first rising edge comes at frame 1.
//   --spectrum F0: for audio, each output's RMS, peak and harmonics 1-16 of F0 (Hz) in dB under the strongest,
//   over the last 0.5 s (C4 = 261.63; use --ticks for the length, e.g. --ticks 8 at 120 BPM is 1 s).
// Build: like module_harness.cpp (see it), with this file.
#include <common.hpp>
#include <context.hpp>
#include <asset.hpp>
#include <logger.hpp>
#include <settings.hpp>
#include <random.hpp>
#include <system.hpp>
#include <plugin.hpp>
#include <plugin/Plugin.hpp>
#include <plugin/Model.hpp>
#include <engine/Engine.hpp>
#include <engine/Module.hpp>
#include <engine/PortInfo.hpp>
#include <patch.hpp>
#include <history.hpp>
#include <jansson.h>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>

using namespace rack;

struct InSpec {
	std::string match;   // lower-case part of the input's name
	std::string kind;    // clock, reset, gate, cv, off
	float value = 0.f;
};

static std::string lower(std::string s) {
	std::transform(s.begin(), s.end(), s.begin(), ::tolower);
	return s;
}

static std::string noteName(float v) {
	static const char *names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
	const float semis = v * 12.f;
	const int n = (int)std::lround(semis);
	if (std::fabs(semis - n) > 0.08f)
		return string::f("%.2fV", v);
	const int oct = 4 + (int)std::floor(n / 12.0);
	return string::f("%s%d", names[((n % 12) + 12) % 12], oct);
}

int main(int argc, char **argv) {
	if (argc < 3) {
		std::printf("usage: preset_play <user folder> <presets folder> SLUG... [--bpm B] [--ppq P] [--ticks N] [--in NAME=SPEC]\n");
		return 2;
	}
	std::string presetsDir = argv[2];
	std::vector<std::string> slugs;
	std::vector<InSpec> specs;
	float bpm = 120.f, ppq = 4.f;
	int ticks = 32;
	float spectrumF0 = 0.f;   // --spectrum F0: the levels of harmonics 1-16 of F0 on each audio output, over the last 0.5 s
	for (int i = 3; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--bpm" && i + 1 < argc) bpm = std::atof(argv[++i]);
		else if (a == "--ppq" && i + 1 < argc) ppq = std::atof(argv[++i]);
		else if (a == "--ticks" && i + 1 < argc) ticks = std::atoi(argv[++i]);
		else if (a == "--spectrum" && i + 1 < argc) spectrumF0 = std::atof(argv[++i]);
		else if (a == "--in" && i + 1 < argc) {
			std::string s = argv[++i];
			size_t eq = s.find('=');
			if (eq == std::string::npos) continue;
			InSpec sp;
			sp.match = lower(s.substr(0, eq));
			std::string k = s.substr(eq + 1);
			size_t c = k.find(':');
			sp.kind = k.substr(0, c);
			if (c != std::string::npos) sp.value = std::atof(k.c_str() + c + 1);
			specs.push_back(sp);
		}
		else slugs.push_back(a);
	}

	settings::headless = true;
	system::init();
	asset::systemDir = "C:/Program Files/VCV/Rack2Pro";
	asset::userDir = argv[1];
	logger::logPath = asset::user("log.txt");
	logger::init();
	random::init();
	settings::init();
	plugin::init();
	contextSet(new Context);
	APP->engine = new engine::Engine;
	APP->history = new history::State;
	APP->patch = new patch::Manager;
	const float sr = 48000.f;
	APP->engine->setSampleRate(sr);
	plugin::Plugin *bidoo = plugin::getPlugin("Bidoo");
	if (!bidoo) {
		std::printf("Bidoo not loaded from %s\n", argv[1]);
		return 2;
	}

	int problems = 0;
	for (const std::string &slug : slugs) {
		plugin::Model *model = bidoo->getModel(slug);
		if (!model) {
			std::printf("%s: no such model\n", slug.c_str());
			problems++;
			continue;
		}
		std::vector<std::string> files = system::getEntries(presetsDir + "/" + slug);
		std::sort(files.begin(), files.end());
		for (const std::string &path : files) {
			if (system::getExtension(path) != ".vcvm")
				continue;
			json_error_t err;
			json_t *root = json_load_file(path.c_str(), 0, &err);
			if (!root) {
				std::printf("%s: bad JSON\n", path.c_str());
				problems++;
				continue;
			}
			engine::Module *m = model->createModule();
			APP->engine->addModule(m);
			for (engine::Output &out : m->outputs)
				out.channels = 1;
			m->fromJson(root);
			json_decref(root);
			// modules that load on a worker thread (a sample, an image, a wavetable): give it time before playing
			std::this_thread::sleep_for(std::chrono::milliseconds(300));

			// what each input gets
			std::vector<InSpec> feed(m->inputs.size());
			for (size_t i = 0; i < m->inputs.size(); i++) {
				std::string name = lower(m->inputInfos[i]->getName());
				InSpec sp;
				sp.kind = "off";
				// reset first: a reset's description often says "the next clock..."; an input named for its tempo or
				// for start/stop is not the clock
				if (name.find("reset") != std::string::npos || name.find("rst") != std::string::npos) sp.kind = "reset";
				else if (name.find("tempo") != std::string::npos || name.find("start") != std::string::npos || name.find("run") != std::string::npos) sp.kind = "off";
				else if (name.find("clock") != std::string::npos || name.find("clk") != std::string::npos) sp.kind = "clock";
				for (const InSpec &s : specs)
					if (name.find(s.match) != std::string::npos)
						sp = s;
				feed[i] = sp;
			}
			std::printf("\n=== %s / %s\n", slug.c_str(), system::getFilename(path).c_str());
			for (size_t i = 0; i < feed.size(); i++)
				if (feed[i].kind != "off")
					std::printf("  in  %-22s %s\n", m->inputInfos[i]->getName().c_str(), feed[i].kind.c_str());

			const double tickFrames = sr * 60.0 / (bpm * ppq);
			const int pulseFrames = (int)std::min(tickFrames * 0.5, (double)(sr * 0.005));
			const int64_t total = (int64_t)(tickFrames * ticks) + 64;
			const int nOut = (int)m->outputs.size();
			std::vector<std::string> gateLine(nOut);
			std::vector<std::vector<float>> cvAt(nOut);
			std::vector<int> edges(nOut, 0), edgesTotal(nOut, 0), maxCh(nOut, 0);
			std::vector<bool> high(nOut, false);
			std::vector<float> lo(nOut, 1e9f), hi(nOut, -1e9f);
			std::vector<std::set<int>> distinct(nOut);
			const int64_t tailFrames = (int64_t)(sr * 0.5f);
			std::vector<std::vector<float>> tail(nOut);
			bool bad = false;
			engine::Module::ProcessArgs args;
			args.sampleRate = sr;
			args.sampleTime = 1.f / sr;
			int nextTick = 1;
			// inputs start low: the clock's first edge is frame 1 (a module never sees an edge already high at frame 0)
			for (int64_t f = 0; f < total; f++) {
				const double inTick = std::fmod((double)(f - 1), tickFrames);
				for (size_t i = 0; i < feed.size(); i++) {
					engine::Input &in = m->inputs[i];
					const InSpec &s = feed[i];
					float v = 0.f;
					in.channels = s.kind == "off" ? 0 : 1;
					if (s.kind == "clock") v = f >= 1 && inTick < pulseFrames ? 10.f : 0.f;
					// "reset": patched, held at 0 V (a fresh module starts at its first step)
					else if (s.kind == "gate") v = f >= 1 && std::fmod((f - 1) * s.value / sr, 1.0) < 0.5 ? 10.f : 0.f;
					else if (s.kind == "cv") v = s.value;
					if (in.channels)
						in.setVoltage(v, 0);
				}
				args.frame = f;
				m->process(args);
				for (int o = 0; o < nOut; o++) {
					engine::Output &out = m->outputs[o];
					if (spectrumF0 > 0.f && f >= total - tailFrames)
						tail[o].push_back(out.getVoltage(0));
					maxCh[o] = std::max(maxCh[o], out.getChannels());
					const float v = out.getVoltage(0);
					if (!std::isfinite(v) || std::fabs(v) > 12.f)
						bad = true;
					lo[o] = std::min(lo[o], v);
					hi[o] = std::max(hi[o], v);
					const bool h = v > 1.f;
					if (h && !high[o]) { edges[o]++; edgesTotal[o]++; }
					high[o] = h;
				}
				// sample just before the next clock edge: what the last tick produced
				if (f == (int64_t)(tickFrames * nextTick) - 1) {
					for (int o = 0; o < nOut; o++) {
						gateLine[o] += edges[o] ? (edges[o] > 1 ? '2' + std::min(edges[o] - 2, 7) : 'x') : '.';
						edges[o] = 0;
						const float v = m->outputs[o].getVoltage(0);
						cvAt[o].push_back(v);
						distinct[o].insert((int)std::lround(v * 1000.f));
					}
					nextTick++;
				}
			}
			for (int o = 0; o < nOut; o++) {
				const std::string name = m->outputInfos[o]->getName();
				if (hi[o] - lo[o] < 1e-4f && std::fabs(hi[o]) < 1e-4f) {
					std::printf("  out %-22s silent\n", name.c_str());
					continue;
				}
				// a gate output: only 0 and ~10 V
				bool gate = lo[o] > -0.01f && distinct[o].size() <= 3 && hi[o] > 4.f;
				for (int d : distinct[o])
					if (d > 100 && d < 4000) gate = false;
				if (spectrumF0 > 0.f && !tail[o].empty()) {
					// a DFT at each harmonic, Hann window: dB against the strongest; the RMS and the peak
					const size_t n = tail[o].size();
					double amp[17] = {}, top = 1e-12, rms = 0.0, pk = 0.0;
					for (size_t t = 0; t < n; t++) { rms += tail[o][t] * tail[o][t]; pk = std::max(pk, (double)std::fabs(tail[o][t])); }
					rms = std::sqrt(rms / n);
					for (int h = 1; h <= 16; h++) {
						double re = 0.0, im = 0.0, w = 2.0 * M_PI * spectrumF0 * h / sr;
						for (size_t t = 0; t < n; t++) {
							const double win = 0.5 - 0.5 * std::cos(2.0 * M_PI * t / (n - 1));
							re += tail[o][t] * win * std::cos(w * t);
							im += tail[o][t] * win * std::sin(w * t);
						}
						amp[h] = std::sqrt(re * re + im * im);
						top = std::max(top, amp[h]);
					}
					std::printf("  out %-22s rms %.2f V peak %.2f V  harmonics dB:", name.c_str(), rms, pk);
					for (int h = 1; h <= 16; h++)
						std::printf(" %d", (int)std::lround(20.0 * std::log10(std::max(amp[h], 1e-12) / top)));
					std::printf("\n");
				}
				else if (gate) {
					std::printf("  out %-22s gates %3d  %s\n", name.c_str(), edgesTotal[o], gateLine[o].c_str());
				}
				else {
					std::printf("  out %-22s cv %.2f..%.2f V (%d ch)  ", name.c_str(), lo[o], hi[o], maxCh[o]);
					for (size_t t = 0; t < cvAt[o].size(); t++)
						std::printf("%s ", noteName(cvAt[o][t]).c_str());
					std::printf("\n");
				}
			}
			if (bad) {
				std::printf("  PROBLEM: an output went past 12 V or not finite\n");
				problems++;
			}
			APP->engine->removeModule(m);
			delete m;
		}
	}
	std::printf("\n%d problems\n", problems);
	std::fflush(stdout);
	std::_Exit(problems ? 1 : 0);
}
