#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"
#include "dsp/digital.hpp"
#include "dsp/window.hpp"
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
	int indexRecolte = 0, indexSeme = 0;
	int pasRecolte = 0, pasSeme = 0;

	// the last 4 s of input (a power of two in size)
	std::vector<float> history;
	long long mask = 0;
	long long now = 0;

	// the window shape, shared by every grain, read by grain position
	float table[TableSize + 1];
	float tableMean = 0.5f;
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
		now = 0;
		for (GRAINE &g : graines)
			g = GRAINE();
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
		double sum = 0;
		for (int i = 0; i <= TableSize; i++)
			sum += table[i];
		tableMean = std::max((float)(sum / (TableSize + 1)), 0.01f);
		tableType = type;
		tableAlpha = alpha;
	}

	float window(float x) const {
		float p = clamp(x, 0.f, 1.f) * TableSize;
		int i = std::min((int)p, TableSize - 1);
		return table[i] + (table[i + 1] - table[i]) * (p - i);
	}

	float hermite(double pos) const {
		long long xi = (long long)std::floor(pos);
		float xf = (float)(pos - (double)xi);
		float p0 = history[(xi - 1) & mask], p1 = history[xi & mask], p2 = history[(xi + 1) & mask], p3 = history[(xi + 2) & mask];
		float c1 = 0.5f * (p2 - p0);
		float c2 = p0 - 2.5f * p1 + 2.f * p2 - 0.5f * p3;
		float c3 = 0.5f * (p3 - p0) + 1.5f * (p1 - p2);
		return ((c3 * xf + c2) * xf + c1) * xf + p1;
	}

	void process(const ProcessArgs &args) override {
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

		history[now & mask] = inputs[INPUT].getVoltage();
		now++;
		long long keep = (long long)history.size() - 4;

		// a new grain every analysis hop, if a slot is free
		if (--pasRecolte <= 0 && graines[indexRecolte].status == GRAINE::FREE) {
			GRAINE &g = graines[indexRecolte];
			g.status = GRAINE::WRITING;
			g.start = now - 1;
			g.longueur = len;
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

		float result = 0.f;
		for (GRAINE &g : graines) {
			if (g.status == GRAINE::WRITING && now - g.start >= g.longueur)
				g.status = GRAINE::READY;
			// a grain left waiting longer than the history is dropped
			if ((g.status == GRAINE::READY || g.status == GRAINE::PLAYING) && now - g.start > keep - g.longueur) {
				if (g.status == GRAINE::READY && &g == &graines[indexSeme])
					indexSeme = (indexSeme + 1) % NbGraines;
				g.status = GRAINE::FREE;
				continue;
			}
			if (g.status != GRAINE::PLAYING)
				continue;
			result += hermite((double)g.start + g.teteLecture) * window(g.teteLecture / (g.longueur - 1));
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

		// overlap-add level: how many windows overlap on average
		float overlap = std::max(1.f, (len / vitesse) * tableMean / hopS);
		outputs[OUTPUT].setVoltage(bidooSoftLimit(result / overlap));
	}

};

struct SPOREWidget : BidooWidget {
	SPOREWidget(SPORE *module) {
		setModule(module);
    prepareThemes(asset::plugin(pluginInstance, "res/SPORE.svg"));

		const float controlYAnchor = 15;
		const float controlYOffset = 54.0f;
		const float inputYAnchor = 49.5f;
		const float inputXAnchor = 15.0f;

		addParam(createParam<BidooBlueKnob>(Vec(8, controlYAnchor), module, SPORE::PITCH_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(inputXAnchor, inputYAnchor), module, SPORE::PITCH_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(8, controlYAnchor + controlYOffset), module, SPORE::GRAINSIZE_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(inputXAnchor, inputYAnchor + controlYOffset), module, SPORE::GRAINSIZE_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(8, controlYAnchor + 2*controlYOffset), module, SPORE::HOPSIZEANALYSIS_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(inputXAnchor, inputYAnchor + 2*controlYOffset), module, SPORE::HOPSIZEANALYSIS_INPUT));

		addParam(createParam<BidooBlueKnob>(Vec(8, controlYAnchor + 3*controlYOffset), module, SPORE::HOPSIZESYNTHESIS_PARAM));
		addInput(createInput<TinyPJ301MPort>(Vec(inputXAnchor, inputYAnchor + 3*controlYOffset), module, SPORE::HOPSIZESYNTHESIS_INPUT));


		addParam(createParam<BidooBlueSnapTrimpot>(Vec(2, controlYAnchor + 4.2f*controlYOffset), module, SPORE::WINDOW_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(24, controlYAnchor + 4.2f*controlYOffset), module, SPORE::ATTACKRELEASE_PARAM));

		addInput(createInput<PJ301MPort>(Vec(10, 283.0f), module, SPORE::INPUT));
		addOutput(createOutput<PJ301MPort>(Vec(10, 330), module, SPORE::OUTPUT));
	}
};

Model *modelSPORE = createModel<SPORE, SPOREWidget>("SPORE");
