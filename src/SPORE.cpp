#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"
#include "dsp/digital.hpp"
#include "dsp/window.hpp"
#include "SampleRead.hpp"
#include <vector>
#include <algorithm>

using namespace std;

const int NbGraines = 200;
const int TableSize = 1024;

// Tukey window, alpha kept above 0 (alpha 0 divided by zero)
inline void tukey(float alpha, float* x, int len) {
	alpha = std::max(alpha, 0.001f);
	float s = 0.5f*alpha*(len-1);
	for (int i = 0; i < len; i++) {
		if (i <= s) {
			x[i] *= 0.5f*(1+std::cos(M_PI*(i/s - 1)));
		} else if (i >= (len-1)*(1-0.5f*alpha)) {
			x[i] *= 0.5f*(1+std::cos(M_PI*(i/s - 2/alpha + 1)));
		}
	}
}

inline void welch(float* x, int len) {
	float s = 0.5f*(len-1);
	for (int i = 0; i < len; i++) {
		float f = (i - s)/s;
		x[i] *= 1 - f*f;
	}
}

// A grain is a stretch of the input history: it waits until it is fully
// recorded, then plays (once, or repeated while the synthesis hop is longer
// than the grain).
struct GRAINE {
	enum { FREE, WRITING, READY, PLAYING };
	int status = FREE;
	long long start = 0;  // first sample, in input samples since the start
	int longueur = 0;
	float teteLecture = 0.f;
	int elapsed = 0;
};

struct SPORE : BidooModule {
	enum ParamIds {
		PITCH_PARAM,
		GRAINSIZE_PARAM,
		HOPSIZEANALYSIS_PARAM,
		HOPSIZESYNTHESIS_PARAM,
		WINDOW_PARAM,
		ATTACKRELEASE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		INPUT,
		PITCH_INPUT,
		GRAINSIZE_INPUT,
		HOPSIZEANALYSIS_INPUT,
		HOPSIZESYNTHESIS_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	GRAINE graines[NbGraines];
	// The grains in use (not FREE), so idle slots cost nothing.
	int actives[NbGraines];
	int nbActives = 0;
	int indexRecolte = 0, indexSeme = 0;
	int pasRecolte = 0, pasSeme = 0;

	// the last 4 s of input (a power of two in size)
	std::vector<float> history;
	long long mask = 0;
	long long now = 0;
	// band-limited copies of the history, for grains pitched up
	sampleread::LiveMip mip;
	// the overlap-add level: the sum of the playing grains' windows, smoothed
	float windowSum = 1.f;

	// the window shape, shared by every grain, read by grain position
	float table[TableSize + 1];
	int tableType = -1;
	float tableAlpha = -1.f;
	int tableCount = 0;

	SPORE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PITCH_PARAM, -12.f, 12.f, 0.f, "Pitch", " semitones");
		configParam(GRAINSIZE_PARAM, 1.f, 100.f, 10.f, "Grain size", " ms");
		configParam(HOPSIZEANALYSIS_PARAM, 1.f, 200.f, 4.f, "Analysis hop (a new grain every)", " ms");
		configParam(HOPSIZESYNTHESIS_PARAM, 1.f, 200.f, 4.f, "Synthesis hop (a grain played every)", " ms");
		configSwitch(WINDOW_PARAM, 0, 5, 0, "Window", {"Welch", "Tukey", "Hann", "Blackman", "Blackman-Nuttall", "Blackman-Harris"});
		configParam(ATTACKRELEASE_PARAM, 0, 1, 0.5f, "Window shape (Tukey and Blackman)", "%", 0.f, 100.f);
		configInput(INPUT, "Audio");
		configInput(PITCH_INPUT, "Pitch CV (1V/oct)");
		configInput(GRAINSIZE_INPUT, "Grain size CV (0-10 V: +0-100 ms)");
		configInput(HOPSIZEANALYSIS_INPUT, "Analysis hop CV (0-10 V: +0-200 ms)");
		configInput(HOPSIZESYNTHESIS_INPUT, "Synthesis hop CV (0-10 V: +0-200 ms)");
		configOutput(OUTPUT, "Audio");
		resize(48000.f);
	}

	void resize(float sampleRate) {
		long long n = 1;
		while (n < (long long)(4.0f * sampleRate))
			n <<= 1;
		history.assign(n, 0.f);
		mask = n - 1;
		mip.resize(n);
		now = 0;
		for (GRAINE &g : graines)
			g = GRAINE();
		nbActives = 0;
		windowSum = 1.f;
		indexRecolte = indexSeme = pasRecolte = pasSeme = 0;
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		resize(e.sampleRate);
	}

	void buildTable(int type, float alpha) {
		std::fill_n(table, TableSize + 1, 1.f);
		if (type == 0)
			welch(table, TableSize + 1);
		else if (type == 1)
			tukey(alpha, table, TableSize + 1);
		else if (type == 2)
			rack::dsp::hannWindow(table, TableSize + 1);
		else if (type == 3)
			rack::dsp::blackmanWindow(alpha, table, TableSize + 1);
		else if (type == 4)
			rack::dsp::blackmanNuttallWindow(table, TableSize + 1);
		else
			rack::dsp::blackmanHarrisWindow(table, TableSize + 1);
		tableType = type;
		tableAlpha = alpha;
	}

	float window(float x) const {
		float p = clamp(x, 0.f, 1.f) * TableSize;
		int i = std::min((int)p, TableSize - 1);
		return table[i] + (table[i + 1] - table[i]) * (p - i);
	}

	void process(const ProcessArgs &args) override {
		history[now & mask] = inputs[INPUT].getVoltage();
		mip.push(history.data(), mask, now);
		now++;
		// Nobody listening: the history goes on, the grains wait.
		if (!outputs[OUTPUT].isConnected()) {
			outputs[OUTPUT].setVoltage(0.f);
			return;
		}

		float ms = args.sampleRate * 0.001f;
		int len = std::max(4, (int)(clamp(params[GRAINSIZE_PARAM].getValue() + inputs[GRAINSIZE_INPUT].getVoltage() * 10.f, 1.f, 100.f) * ms));
		int hopA = std::max(1, (int)(clamp(params[HOPSIZEANALYSIS_PARAM].getValue() + inputs[HOPSIZEANALYSIS_INPUT].getVoltage() * 20.f, 1.f, 200.f) * ms));
		int hopS = std::max(1, (int)(clamp(params[HOPSIZESYNTHESIS_PARAM].getValue() + inputs[HOPSIZESYNTHESIS_INPUT].getVoltage() * 20.f, 1.f, 200.f) * ms));
		float semis = params[PITCH_PARAM].getValue() + inputs[PITCH_INPUT].getVoltage() * 12.f;
		float vitesse = clamp(dsp::exp2_taylor5(semis / 12.f), 0.25f, 4.f);

		int type = clamp((int)params[WINDOW_PARAM].getValue(), 0, 5);
		float alpha = params[ATTACKRELEASE_PARAM].getValue();
		// a moving shape knob rebuilds the window at most every 64 samples
		if (type != tableType || (alpha != tableAlpha && ++tableCount >= 64)) {
			buildTable(type, alpha);
			tableCount = 0;
		}

		long long keep = (long long)history.size() - 4;

		// a new grain every analysis hop, if a slot is free
		if (--pasRecolte <= 0 && graines[indexRecolte].status == GRAINE::FREE) {
			GRAINE &g = graines[indexRecolte];
			g.status = GRAINE::WRITING;
			g.start = now - 1;
			g.longueur = len;
			actives[nbActives++] = indexRecolte;
			indexRecolte = (indexRecolte + 1) % NbGraines;
			pasRecolte = hopA;
		}
		// one grain played every synthesis hop, in the order they were taken
		if (--pasSeme <= 0 && graines[indexSeme].status == GRAINE::READY) {
			GRAINE &g = graines[indexSeme];
			g.status = GRAINE::PLAYING;
			g.teteLecture = 0.f;
			g.elapsed = 0;
			indexSeme = (indexSeme + 1) % NbGraines;
			pasSeme = hopS;
		}

		float result = 0.f, wsum = 0.f;
		for (int a = nbActives - 1; a >= 0; a--) {
			int gi = actives[a];
			GRAINE &g = graines[gi];
			// ready once recorded, and once the band-limited copies have it
			if (g.status == GRAINE::WRITING && now - g.start >= g.longueur + sampleread::LiveMip::LATENCY)
				g.status = GRAINE::READY;
			// a grain left waiting longer than the history is dropped
			if ((g.status == GRAINE::READY || g.status == GRAINE::PLAYING) && now - g.start > keep - g.longueur) {
				if (g.status == GRAINE::READY && gi == indexSeme)
					indexSeme = (indexSeme + 1) % NbGraines;
				g.status = GRAINE::FREE;
			}
			else if (g.status == GRAINE::PLAYING) {
				float w = window(g.teteLecture / (g.longueur - 1));
				result += mip.read(history.data(), mask, (double)g.start + g.teteLecture, vitesse) * w;
				wsum += w;
				g.teteLecture += vitesse;
				g.elapsed++;
				if (g.teteLecture >= g.longueur - 1) {
					// repeated only to fill a synthesis hop longer than the grain
					if (g.elapsed + 0.5f * (g.longueur / vitesse) < hopS)
						g.teteLecture = 0.f;
					else
						g.status = GRAINE::FREE;
				}
			}
			if (g.status == GRAINE::FREE)
				actives[a] = actives[--nbActives];
		}

		// overlap-add level: the windows actually summed, smoothed over about
		// 20 ms, never below 1 (sparse grains are not boosted)
		windowSum += (wsum - windowSum) * std::min(1.f, 50.f * args.sampleTime);
		outputs[OUTPUT].setVoltage(bidooSoftLimit(result / std::max(1.f, windowSum)));
	}

};

struct SPOREWidget : BidooWidget {
	SPOREWidget(SPORE *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/SPORE.svg"));

		// No screws, as before. The original hand-drawn panel: four knobs down the middle, each joined by a line to its CV
		// jack, then the window and shape trimpots side by side, then IN and OUT. The column is the CV jacks' (the
		// panel's centre, where it draws the lines and centres IN and OUT); each control keeps its old top.
		const float kGroupPitch = 54.f;   // from one knob and its CV jack to the next
		const float kBoxPitch = 47.f;     // from the IN box to the OUT box
		const theme::Pos pitchCv = theme::specificCorner(15, 49.5f, theme::kTinyJackSize);
		const theme::Pos pitch = theme::colRow(pitchCv, theme::specificCorner(8, 15, theme::kBlueKnobSize));
		const int knobIds[4] = {SPORE::PITCH_PARAM, SPORE::GRAINSIZE_PARAM, SPORE::HOPSIZEANALYSIS_PARAM, SPORE::HOPSIZESYNTHESIS_PARAM};
		const int cvIds[4] = {SPORE::PITCH_INPUT, SPORE::GRAINSIZE_INPUT, SPORE::HOPSIZEANALYSIS_INPUT, SPORE::HOPSIZESYNTHESIS_INPUT};
		for (int i = 0; i < 4; i++) {
			addParam(theme::param<BidooBlueKnob>(pitch.down(kGroupPitch * i), module, knobIds[i]));
			addInput(theme::input<TinyPJ301MPort>(pitchCv.down(kGroupPitch * i), module, cvIds[i]));
		}

		// The trimpots keep their old corners, within 0.35 px of their WINDOW and SHAPE labels
		const float trimpotTop = 15 + 4.2f * kGroupPitch;
		addParam(theme::param<BidooBlueSnapTrimpot>(theme::specificCorner(2, trimpotTop, theme::kTrimpotSize), module, SPORE::WINDOW_PARAM));
		addParam(theme::param<BidooBlueTrimpot>(theme::specificCorner(24, trimpotTop, theme::kTrimpotSize), module, SPORE::ATTACKRELEASE_PARAM));

		const theme::Pos in = theme::colRow(pitchCv, theme::specificCorner(10, 283, theme::kJackSize));
		addInput(theme::input<PJ301MPort>(in, module, SPORE::INPUT));
		addOutput(theme::output<PJ301MPort>(in.down(kBoxPitch), module, SPORE::OUTPUT));
	}
};

Model *modelSPORE = createModel<SPORE, SPOREWidget>("SPORE");
