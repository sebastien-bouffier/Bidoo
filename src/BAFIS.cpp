#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/resampler.hpp"

using namespace std;

#define pi 3.14159265359

struct MultiFilter {
	float q;
	float freq;
	float smpRate;
	float hp = 0.0f, bp = 0.0f, lp = 0.0f, mem1 = 0.0f, mem2 = 0.0f;

	void setParams(float freq, float q, float smpRate) {
		this->freq = freq;
		this->q = q;
		this->smpRate = smpRate;
	}

	void calcOutput(float sample) {
		float g = tan(pi * freq / smpRate);
		float R = 1.0f / (2.0f*q);
		hp = (sample - (2.0f*R + g)*mem1 - mem2) / (1.0f + 2.0f * R * g + g * g);
		bp = g * hp + mem1;
		lp = g * bp + mem2;
		mem1 = g * hp + bp;
		mem2 = g * bp + lp;
	}
};

struct BAFIS : Module {
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

	MultiFilter filter0, filter1,filter2, filter3, filter4, filter5;

	///Tooltip
	struct tpType : ParamQuantity {
		std::string getDisplayValueString() override {
			if (getValue() == 0.f)
				return "tanh";
			else if (getValue() == 1.f)
				return "sin";
			else
				return "digi";
		}
	};

	struct tpPrePost : ParamQuantity {
		std::string getDisplayValueString() override {
			if (getValue() == 0.f)
				return "PRE";
			else
				return "POST";
		}
	};
	///Tooltip

	BAFIS() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(FREQ_PARAM, 0.f, 1.f, 1.0f, "Freq.", " Hz", std::pow(2, 10.f), dsp::FREQ_C4 / std::pow(2, 5.f));
		configParam(FREQ_PARAM+1, 0.f, 1.f, 0.5f, "Freq.", " Hz", std::pow(2, 10.f), dsp::FREQ_C4 / std::pow(2, 5.f));
		configParam(FREQ_PARAM+2, 0.f, 1.f, 0.0f, "Freq.", " Hz", std::pow(2, 10.f), dsp::FREQ_C4 / std::pow(2, 5.f));

    for (int i=0;i<4;i++) {
      configParam(Q_PARAM+i, .1f, 1.f, .1f, "Q", "%", 0.f, 100.f);
      configParam(GAIN_PARAM+i, 1.f, 10.f, 1.f, "Drive", "%", 0.f, 10.f,-10.f);
      configParam<tpType>(TYPE_PARAM+i, 0.f, 2.f, 0.f, "Dist. type");
      configParam<tpPrePost>(PREPOST_PARAM+i, 0.f, 1.f, 0.f, "Pre/Post");
      configParam(VOLUME_PARAM+i, 0.f, 1.f, 0.5f, "Volume", "%", 0.f, 100.f);
    }
	}

	void process(const ProcessArgs &args) override {
    float in = inputs[IN].getVoltage() * 0.2f; //normalise to -1/+1 we consider VCV Rack standard is #+5/-5V on VCO1
    float out = 0.f;
		float c0freq = std::pow(2.0f, rescale(clamp(params[FREQ_PARAM].getValue() + inputs[FREQ_INPUT].getVoltage() * 0.2f, 0.0f, 1.0f), 0.0f, 1.0f, 4.5f, 14.0f));
		float c1freq = std::pow(2.0f, rescale(clamp(params[FREQ_PARAM+1].getValue() + inputs[FREQ_INPUT+1].getVoltage() * 0.2f, 0.0f, 1.0f), 0.0f, 1.0f, 4.5f, 14.0f));
		float c2freq = std::pow(2.0f, rescale(clamp(params[FREQ_PARAM+2].getValue() + inputs[FREQ_INPUT+2].getVoltage() * 0.2f, 0.0f, 1.0f), 0.0f, 1.0f, 4.5f, 14.0f));

    float q0 = 10.0f * clamp(params[Q_PARAM].getValue() + inputs[Q_INPUT].getVoltage() / 10.f, 0.1f, 1.0f);
    float q1 = 10.0f * clamp(params[Q_PARAM+1].getValue() + inputs[Q_INPUT+1].getVoltage() / 10.f, 0.1f, 1.0f);
    float q2 = 10.0f * clamp(params[Q_PARAM+2].getValue() + inputs[Q_INPUT+2].getVoltage() / 10.f, 0.1f, 1.0f);
    float q3 = 10.0f * clamp(params[Q_PARAM+3].getValue() + inputs[Q_INPUT+3].getVoltage() / 10.f, 0.1f, 1.0f);

    float g0 = clamp(params[GAIN_PARAM].getValue() + inputs[GAIN_INPUT].getVoltage(), 1.f, 10.0f);
    float g1 = clamp(params[GAIN_PARAM+1].getValue() + inputs[GAIN_INPUT+1].getVoltage(), 1.f, 10.0f);
    float g2 = clamp(params[GAIN_PARAM+2].getValue() + inputs[GAIN_INPUT+2].getVoltage(), 1.f, 10.0f);
    float g3 = clamp(params[GAIN_PARAM+3].getValue() + inputs[GAIN_INPUT+3].getVoltage(), 1.f, 10.0f);

		float v0 = clamp(params[VOLUME_PARAM].getValue() + inputs[VOLUME_INPUT].getVoltage() * 0.1f, 0.f, 1.0f);
    float v1 = clamp(params[VOLUME_PARAM+1].getValue() + inputs[VOLUME_INPUT+1].getVoltage() * 0.1f, 0.f, 1.0f);
    float v2 = clamp(params[VOLUME_PARAM+2].getValue() + inputs[VOLUME_INPUT+2].getVoltage() * 0.1f, 0.f, 1.0f);
    float v3 = clamp(params[VOLUME_PARAM+3].getValue() + inputs[VOLUME_INPUT+3].getVoltage() * 0.1f, 0.f, 1.0f);

    if ((params[PREPOST_PARAM].getValue() == 0.f) || (inputs[PREPOST_INPUT].isConnected() && (inputs[PREPOST_INPUT].getVoltage()<1.f))) {
      float dIn = 0.f;
      if ((params[TYPE_PARAM].getValue() == 0.f) || (inputs[TYPE_INPUT].isConnected() && (inputs[TYPE_INPUT].getVoltage() == 0.f))) {
        dIn = tanh(g0*in);
      }
      else if ((params[TYPE_PARAM].getValue() == 1.f) || (inputs[TYPE_INPUT].isConnected() && (inputs[TYPE_INPUT].getVoltage() == 1.f))) {
        dIn = sin(g0*in);
      }
      else {
        dIn = clamp(g0*in,-1.0f,1.0f);
      }
      filter0.setParams(c0freq, q0, args.sampleRate);
    	filter0.calcOutput(dIn);
  		out = filter0.lp * v0 * 30517578125e-15f;
    }
    else {
      filter0.setParams(c0freq, q0, args.sampleRate);
    	filter0.calcOutput(in);
      if ((params[TYPE_PARAM].getValue() == 0.f) || (inputs[TYPE_INPUT].isConnected() && (inputs[TYPE_INPUT].getVoltage() == 0.f))) {
        out = tanh(g0*filter0.lp) * v0 * 30517578125e-15f;
      }
      else if ((params[TYPE_PARAM].getValue() == 1.f) || (inputs[TYPE_INPUT].isConnected() && (inputs[TYPE_INPUT].getVoltage() == 1.f))) {
        out = sin(g0*filter0.lp) * v0 * 30517578125e-15f;
      }
      else {
        out = clamp(g0*filter0.lp,-1.f,1.f) * v0 * 30517578125e-15f;
      }
    }

    if ((params[PREPOST_PARAM+1].getValue() == 0.f) || (inputs[PREPOST_INPUT+1].isConnected() && (inputs[PREPOST_INPUT+1].getVoltage()<1.f))) {
      float dIn = 0.f;
      if ((params[TYPE_PARAM+1].getValue() == 0.f) || (inputs[TYPE_INPUT+1].isConnected() && (inputs[TYPE_INPUT+1].getVoltage() == 0.f))) {
        dIn = tanh(g1*in);
      }
      else if ((params[TYPE_PARAM+1].getValue() == 1.f) || (inputs[TYPE_INPUT+1].isConnected() && (inputs[TYPE_INPUT+1].getVoltage() == 1.f))) {
        dIn = sin(g1*in);
      }
      else {
        dIn = clamp(g1*in,-1.0f,1.0f);
      }
      filter1.setParams(c0freq, q1, args.sampleRate);
    	filter1.calcOutput(dIn);
      filter2.setParams(c1freq, q1, args.sampleRate);
    	filter2.calcOutput(filter1.hp);
      out += filter2.lp * v1 * 30517578125e-15f;
    }
    else {
      filter1.setParams(c0freq, q1, args.sampleRate);
    	filter1.calcOutput(in);
      filter2.setParams(c1freq, q1, args.sampleRate);
    	filter2.calcOutput(filter1.hp);
      if ((params[TYPE_PARAM+1].getValue() == 0.f) || (inputs[TYPE_INPUT+1].isConnected() && (inputs[TYPE_INPUT+1].getVoltage() == 0.f))) {
        out += tanh(g1*filter2.lp) * v1 * 30517578125e-15f;
      }
      else if ((params[TYPE_PARAM+1].getValue() == 1.f) || (inputs[TYPE_INPUT+1].isConnected() && (inputs[TYPE_INPUT+1].getVoltage() == 1.f))) {
        out += sin(g1*filter2.lp) * v1 * 30517578125e-15f;
      }
      else {
        out += clamp(g1*filter2.lp,-1.f,1.f) * v1 * 30517578125e-15f;
      }
    }

    if ((params[PREPOST_PARAM+2].getValue() == 0.f) || (inputs[PREPOST_INPUT+2].isConnected() && (inputs[PREPOST_INPUT+2].getVoltage()<1.f))) {
      float dIn = 0.f;
      if ((params[TYPE_PARAM+2].getValue() == 0.f) || (inputs[TYPE_INPUT+2].isConnected() && (inputs[TYPE_INPUT+2].getVoltage() == 0.f))) {
        dIn = tanh(g2*in);
      }
      else if ((params[TYPE_PARAM+2].getValue() == 1.f) || (inputs[TYPE_INPUT+2].isConnected() && (inputs[TYPE_INPUT+2].getVoltage() == 1.f))) {
        dIn = sin(g2*in);
      }
      else {
        dIn = clamp(g2*in,-1.0f,1.0f);
      }
      filter3.setParams(c1freq, q2, args.sampleRate);
    	filter3.calcOutput(dIn);
      filter4.setParams(c2freq, q2, args.sampleRate);
    	filter4.calcOutput(filter3.hp);
      out += filter4.lp * v2 * 30517578125e-15f;
    }
    else {
      filter3.setParams(c1freq, q2, args.sampleRate);
    	filter3.calcOutput(in);
      filter4.setParams(c2freq, q2, args.sampleRate);
    	filter4.calcOutput(filter1.hp);
      if ((params[TYPE_PARAM+2].getValue() == 0.f) || (inputs[TYPE_INPUT+2].isConnected() && (inputs[TYPE_INPUT+2].getVoltage() == 0.f))) {
        out += tanh(g2*filter4.lp) * v2 * 30517578125e-15f;
      }
      else if ((params[TYPE_PARAM+2].getValue() == 1.f) || (inputs[TYPE_INPUT+2].isConnected() && (inputs[TYPE_INPUT+2].getVoltage() == 1.f))) {
        out += sin(g2*filter4.lp) * v2 * 30517578125e-15f;
      }
      else {
        out += clamp(g2*filter4.lp,-1.f,1.f) * v2 * 30517578125e-15f;
      }
    }

    if ((params[PREPOST_PARAM+3].getValue() == 0.f) || (inputs[PREPOST_INPUT+3].isConnected() && (inputs[PREPOST_INPUT+3].getVoltage()<1.f))) {
      float dIn = 0.f;
      if ((params[TYPE_PARAM+3].getValue() == 0.f) || (inputs[TYPE_INPUT+3].isConnected() && (inputs[TYPE_INPUT+3].getVoltage() == 0.f))) {
        dIn = tanh(g3*in);
      }
      else if ((params[TYPE_PARAM+3].getValue() == 1.f) || (inputs[TYPE_INPUT+3].isConnected() && (inputs[TYPE_INPUT+3].getVoltage() == 1.f))) {
        dIn = sin(g3*in);
      }
      else {
        dIn = clamp(g3*in,-1.0f,1.0f);
      }
      filter5.setParams(c2freq, q3, args.sampleRate);
    	filter5.calcOutput(dIn);
      out += filter5.hp * v3 * 30517578125e-15f;
    }
    else {
      filter5.setParams(c2freq, q3, args.sampleRate);
    	filter5.calcOutput(in);
      if ((params[TYPE_PARAM+3].getValue() == 0.f) || (inputs[TYPE_INPUT+3].isConnected() && (inputs[TYPE_INPUT+3].getVoltage() == 0.f))) {
        out += tanh(g3*filter5.hp) * v3 * 30517578125e-15f;
      }
      else if ((params[TYPE_PARAM+3].getValue() == 1.f) || (inputs[TYPE_INPUT+3].isConnected() && (inputs[TYPE_INPUT+3].getVoltage() == 1.f))) {
        out += sin(g3*filter5.hp) * v3 * 30517578125e-15f;
      }
      else {
        out += clamp(g3*filter5.hp,-1.f,1.f) * v3 * 30517578125e-15f;
      }
    }

    outputs[OUT].setVoltage(out * 32768.0f * 5.0f);
	}

};

struct BAFISWidget : ModuleWidget {
	BAFISWidget(BAFIS *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/BAFIS.svg")));

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

		addInput(createInput<PJ301MPort>(Vec(10, 320), module, BAFIS::IN));
		addOutput(createOutput<PJ301MPort>(Vec(116, 320), module, BAFIS::OUT));
	}
};

Model *modelBAFIS = createModel<BAFIS, BAFISWidget>("BAFIS");
