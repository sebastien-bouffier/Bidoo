#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"

// Four bands split at three crossovers, each band distorted before or after
// its filters.
struct BAFIS : BidooModule {
	enum ParamIds {
		FREQ_PARAM,
		Q_PARAM = FREQ_PARAM + 3,
		GAIN_PARAM = Q_PARAM + 4,
		TYPE_PARAM = GAIN_PARAM + 4,
		PREPOST_PARAM = TYPE_PARAM + 4,
		VOLUME_PARAM = PREPOST_PARAM + 4,
		NUM_PARAMS = VOLUME_PARAM + 4
	};
	enum InputIds {
		IN,
		FREQ_INPUT,
		Q_INPUT = FREQ_INPUT + 3,
		GAIN_INPUT = Q_INPUT + 4,
		TYPE_INPUT = GAIN_INPUT + 4,
		PREPOST_INPUT = TYPE_INPUT + 4,
		VOLUME_INPUT = PREPOST_INPUT + 4,
		NUM_INPUTS = VOLUME_INPUT + 4
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	// Per channel: band b goes through hp[b] (from crossover b-1, not band 0)
	// then lp[b] (to crossover b, not band 3).
	Svf hp[PORT_MAX_CHANNELS][4], lp[PORT_MAX_CHANNELS][4];
	SvfGainCache gain[PORT_MAX_CHANNELS][3];

	static float knobToPitch(float v) { return 4.5f + 9.5f * v; }
	static float freqToKnob(float f) { return (std::log2(f) - 4.5f) / 9.5f; }

	BAFIS() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		const float defaults[3] = {200.f, 1000.f, 5000.f};
		for (int i = 0; i < 3; i++) {
			configParam(FREQ_PARAM + i, 0.f, 1.f, freqToKnob(defaults[i]), "Crossover " + std::to_string(i + 1), " Hz", std::pow(2.f, 9.5f), std::pow(2.f, 4.5f));
			configInput(FREQ_INPUT + i, "Crossover " + std::to_string(i + 1) + " CV (1V/oct)");
		}
		for (int i = 0; i < 4; i++) {
			std::string band = "Band " + std::to_string(i + 1) + " ";
			configParam(Q_PARAM + i, .1f, 1.f, .1f, band + "resonance", "%", 0.f, 100.f);
			configParam(GAIN_PARAM + i, 1.f, 10.f, 1.f, band + "drive", "%", 0.f, 10.f, -10.f);
			configSwitch(TYPE_PARAM + i, 0.f, 2.f, 0.f, band + "distortion", {"tanh", "sin", "digital"});
			configSwitch(PREPOST_PARAM + i, 0.f, 1.f, 0.f, band + "distortion", {"Before the filter", "After the filter"});
			configParam(VOLUME_PARAM + i, 0.f, 1.f, 0.5f, band + "volume", "%", 0.f, 100.f);
			configInput(Q_INPUT + i, band + "resonance CV");
			configInput(GAIN_INPUT + i, band + "drive CV");
			configInput(TYPE_INPUT + i, band + "distortion CV (0-3.3 V tanh, 3.3-6.7 V sin, above digital)");
			configInput(PREPOST_INPUT + i, band + "pre/post CV (1 V and above: after the filter)");
			configInput(VOLUME_INPUT + i, band + "volume CV");
		}
		configInput(IN, "Audio");
		configOutput(OUT, "Audio");
	}

	static float distort(int type, float drive, float x) {
		if (type == 0)
			return std::tanh(drive * x);
		if (type == 1)
			return std::sin(drive * x);
		return clamp(drive * x, -1.f, 1.f);
	}

	void process(const ProcessArgs &args) override {
		int channels = std::max(1, inputs[IN].getChannels());

		for (int c = 0; c < channels; c++) {
			// crossovers kept in order
			float g[3];
			float pitch = 0.f;
			for (int i = 0; i < 3; i++) {
				float p = clamp(knobToPitch(params[FREQ_PARAM + i].getValue()) + inputs[FREQ_INPUT + i].getPolyVoltage(c), 4.5f, 14.f);
				pitch = i == 0 ? p : std::max(pitch, p);
				g[i] = gain[c][i].get(std::exp2(pitch), args.sampleRate);
			}

			float in = inputs[IN].getVoltage(c) * 0.2f;
			float out = 0.f;
			for (int b = 0; b < 4; b++) {
				float q = 10.0f * clamp(params[Q_PARAM + b].getValue() + inputs[Q_INPUT + b].getPolyVoltage(c) * 0.1f, 0.1f, 1.0f);
				float drive = clamp(params[GAIN_PARAM + b].getValue() + inputs[GAIN_INPUT + b].getPolyVoltage(c), 1.f, 10.0f);
				float volume = clamp(params[VOLUME_PARAM + b].getValue() + inputs[VOLUME_INPUT + b].getPolyVoltage(c) * 0.1f, 0.f, 1.0f);
				int type = inputs[TYPE_INPUT + b].isConnected()
					? clamp((int)(inputs[TYPE_INPUT + b].getPolyVoltage(c) * 0.3f), 0, 2)
					: (int)params[TYPE_PARAM + b].getValue();
				bool post = inputs[PREPOST_INPUT + b].isConnected()
					? inputs[PREPOST_INPUT + b].getPolyVoltage(c) >= 1.f
					: params[PREPOST_PARAM + b].getValue() > 0.5f;

				float x = post ? in : distort(type, drive, in);
				if (b > 0) {
					hp[c][b].set(g[b - 1], q);
					hp[c][b].process(x);
					x = hp[c][b].hp;
				}
				if (b < 3) {
					lp[c][b].set(g[b], q);
					lp[c][b].process(x);
					x = lp[c][b].lp;
				}
				if (post)
					x = distort(type, drive, x);
				out += x * volume;
			}
			outputs[OUT].setVoltage(bidooSoftLimit(out * 5.0f), c);
		}
		outputs[OUT].setChannels(channels);
	}
};

struct BAFISWidget : BidooWidget {
	BAFISWidget(BAFIS *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/BAFIS.svg"));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		addParam(createParam<BidooBlueKnob>(Vec(25, 30), module, BAFIS::FREQ_PARAM));
    addParam(createParam<BidooBlueKnob>(Vec(60, 30), module, BAFIS::FREQ_PARAM+1));
    addParam(createParam<BidooBlueKnob>(Vec(95, 30), module, BAFIS::FREQ_PARAM+2));

		addInput(createInput<TinyPJ301MPort>(Vec(32, 62), module, BAFIS::FREQ_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(67, 62), module, BAFIS::FREQ_INPUT+1));
		addInput(createInput<TinyPJ301MPort>(Vec(102, 62), module, BAFIS::FREQ_INPUT+2));

    for (int i=0; i<4; i++) {
      addParam(createParam<BidooSmallBlueKnob>(Vec(11+i*35, 85), module, BAFIS::Q_PARAM+i));
			addInput(createInput<TinyPJ301MPort>(Vec(15+i*35, 111), module, BAFIS::Q_INPUT+i));
  		addParam(createParam<BidooSmallBlueKnob>(Vec(11+i*35, 129), module, BAFIS::GAIN_PARAM+i));
			addInput(createInput<TinyPJ301MPort>(Vec(15+i*35, 155), module, BAFIS::GAIN_INPUT+i));
      addParam(createParam<BidooSmallSnapBlueKnob>(Vec(11+i*35, 173), module, BAFIS::TYPE_PARAM+i));
			addInput(createInput<TinyPJ301MPort>(Vec(15+i*35, 199), module, BAFIS::TYPE_INPUT+i));
      addParam(createParam<CKSS>(Vec(16+i*35, 218), module, BAFIS::PREPOST_PARAM+i));
			addInput(createInput<TinyPJ301MPort>(Vec(15+i*35, 242), module, BAFIS::PREPOST_INPUT+i));
      addParam(createParam<BidooSmallBlueKnob>(Vec(11+i*35, 261), module, BAFIS::VOLUME_PARAM+i));
			addInput(createInput<TinyPJ301MPort>(Vec(15+i*35, 287), module, BAFIS::VOLUME_INPUT+i));
    }

		addInput(createInput<PJ301MPort>(Vec(6.8f, 330), module, BAFIS::IN));
		addOutput(createOutput<PJ301MPort>(Vec(118.4f, 330), module, BAFIS::OUT));
	}
};

Model *modelBAFIS = createModel<BAFIS, BAFISWidget>("BAFIS");
