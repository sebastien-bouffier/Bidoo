#include "plugin.hpp"
#include "BidooComponents.hpp"

using namespace std;

// One slope of a Serge-style universal slope generator, 0 to 10 V.
// A trigger rises to 10 V then falls back to rest (IN if patched, else 0 V);
// triggers are ignored while rising and restart the rise from the current
// level while falling. At rest the output slews to IN.
struct BancauSlope {
	enum Stage { IDLE, RISE, FALL };
	Stage stage = IDLE;
	float out = 0.f;
	double u = 0.0;          // progress through the current segment, 0..1 (double: 20 s segments)
	float endPulse = 0.f;    // seconds left of the end-of-cycle pulse
	dsp::SchmittTrigger trig;

	static constexpr float TOP = 10.f;
	static constexpr float K = 4.f;  // exponential curve: a capacitor charging
	static constexpr float EXP_MINUS_K = 0.0183156389f;          // exp(-K)
	static constexpr float CURVE_SPAN = 1.f - EXP_MINUS_K;       // 1 - exp(-K)
	static constexpr float INV_CURVE_SPAN = 1.f / CURVE_SPAN;

	// segment shape: x (0..1 of the way) against u (0..1 of the time)
	static float shape(float u, bool curved) {
		return curved ? (1.f - std::exp(-K * u)) * INV_CURVE_SPAN : u;
	}
	static float inverse(float x, bool curved) {
		x = clamp(x, 0.f, 1.f);
		return curved ? -std::log(1.f - x * CURVE_SPAN) * (1.f / K) : x;
	}

	void startRise(bool riseCurve) {
		stage = RISE;
		u = inverse(out / TOP, riseCurve);
	}

	// dt: sample time; riseT and fallT: seconds for a full 0-10 V segment
	void process(float dt, float riseT, float fallT, bool riseCurve, bool fallCurve, bool cycle,
	             bool trigger, bool inPatched, float in) {
		float rest = inPatched ? in : 0.f;
		if (trigger && stage != RISE)
			startRise(riseCurve);
		if (stage == IDLE && cycle)
			startRise(riseCurve);

		if (stage == IDLE) {
			// slew to IN, at the rise or fall time per 10 V
			if (inPatched && out != in) {
				bool up = in > out;
				float t = up ? riseT : fallT;
				bool curved = up ? riseCurve : fallCurve;
				if (curved) {
					out += (in - out) * (1.f - std::exp(-K * dt / t));
					if (std::fabs(in - out) < 1e-5f)
						out = in;
				}
				else {
					float step = TOP * dt / t;
					out = up ? std::min(out + step, in) : std::max(out - step, in);
				}
			}
		}
		else {
			// a segment may end inside the sample: the time left carries over
			float left = dt;
			for (int n = 0; n < 4 && left > 0.f && stage != IDLE; n++) {
				if (stage == RISE) {
					double nu = u + (double)left / riseT;
					if (nu < 1.f) {
						u = nu;
						out = TOP * shape((float)u, riseCurve);
						left = 0.f;
					}
					else {
						left = (float)((nu - 1.0) * riseT);
						stage = FALL;
						u = 0.0;
						out = TOP;
					}
				}
				else {
					float span = TOP - rest;
					if (span <= 1e-4f) {
						out = rest;
						stage = IDLE;
						break;
					}
					double nu = u + (double)left / fallT;
					if (nu < 1.f) {
						u = nu;
						out = rest + span * (1.f - shape((float)u, fallCurve));
						left = 0.f;
					}
					else {
						left = (float)((nu - 1.0) * fallT);
						out = rest;
						stage = IDLE;
						endPulse = 1e-3f;
						if (cycle) {
							stage = RISE;
							u = inverse(out / TOP, riseCurve);
						}
					}
				}
			}
		}
		if (endPulse > 0.f)
			endPulse -= dt;
	}

	// high at rest, and for 1 ms at the end of each cycle
	bool end() const {
		return stage == IDLE || endPulse > 0.f;
	}
};

struct BANCAU : BidooModule {
	enum ParamIds {
		RISE1_PARAM,
		RISECV1_PARAM,
		FALL1_PARAM,
		FALLCV1_PARAM,
		RISE2_PARAM,
		RISECV2_PARAM,
		FALL2_PARAM,
		FALLCV2_PARAM,
		RISEEXP1_PARAM,
		FALLEXP1_PARAM,
		RISEEXP2_PARAM,
		FALLEXP2_PARAM,
		CYCLE1_PARAM,
		CYCLE2_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN1_INPUT,
		TRIG1_INPUT,
		VOCT1_INPUT,
		RISECV1_INPUT,
		FALLCV1_INPUT,
		BOTHCV1_INPUT,
		IN2_INPUT,
		TRIG2_INPUT,
		VOCT2_INPUT,
		RISECV2_INPUT,
		FALLCV2_INPUT,
		BOTHCV2_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		PULSE1_OUTPUT,
		END1_OUTPUT,
		OUT1_OUTPUT,
		BP2_OUTPUT,
		END2_OUTPUT,
		OUT2_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		ONE_LIGHT,
		TWO_LIGHT,
		NUM_LIGHTS
	};

	BancauSlope slope[2][PORT_MAX_CHANNELS];

	// knob 0..1: 0.2 ms to 20 s for a full 0-10 V segment
	static float knobTime(float k) {
		return 0.0002f * std::pow(10.f, 5.f * k);
	}

	// knobTime of the 4 time knobs, computed again only when a knob moves
	float knobIn[4] = {NAN, NAN, NAN, NAN};
	float knobOut[4] = {};
	float cachedKnobTime(int n, float k) {
		if (k != knobIn[n]) {
			knobIn[n] = k;
			knobOut[n] = knobTime(k);
		}
		return knobOut[n];
	}

	BANCAU() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		for (int s = 0; s < 2; s++) {
			std::string n = std::to_string(s + 1) + " ";
			int o = s * 4;
			configParam(RISE1_PARAM + o, 0.f, 1.f, 0.5f, n + "Rise time", " ms", 100000.f, 0.2f);
			configParam(FALL1_PARAM + o, 0.f, 1.f, 0.5f, n + "Fall time", " ms", 100000.f, 0.2f);
			configParam(RISECV1_PARAM + o, -1.f, 1.f, 0.f, n + "Rise CV amount (octaves per volt)", "%", 0.f, 100.f);
			configParam(FALLCV1_PARAM + o, -1.f, 1.f, 0.f, n + "Fall CV amount (octaves per volt)", "%", 0.f, 100.f);
			configSwitch(RISEEXP1_PARAM + 2 * s, 0.f, 1.f, 0.f, n + "Rise curve", {"Linear", "Exponential"});
			configSwitch(FALLEXP1_PARAM + 2 * s, 0.f, 1.f, 0.f, n + "Fall curve", {"Linear", "Exponential"});
			configSwitch(CYCLE1_PARAM + s, 0.f, 1.f, 0.f, n + "Cycle", {"Off", "On"});
			int i = s * 6;
			configInput(IN1_INPUT + i, n + "In (slewed at rest, the level a trigger falls back to)");
			configInput(TRIG1_INPUT + i, n + "Trigger");
			configInput(VOCT1_INPUT + i, n + "V/oct (rise and fall)");
			configInput(RISECV1_INPUT + i, n + "Rise CV");
			configInput(FALLCV1_INPUT + i, n + "Fall CV");
			configInput(BOTHCV1_INPUT + i, n + "Rise and fall CV");
			configOutput(OUT1_OUTPUT + 3 * s, n + "Out (0-10 V)");
			configOutput(END1_OUTPUT + 3 * s, n + "End (high at rest, 1 ms pulse per cycle)");
		}
		configOutput(PULSE1_OUTPUT, "1 Gate (10 V while Out is above 5 V)");
		configOutput(BP2_OUTPUT, "2 Norm (Out centred: -5 to 5 V)");
	}

	void process(const ProcessArgs &args) override {
		for (int s = 0; s < 2; s++) {
			int p = s * 4, i = s * 6;
			Input &in = inputs[IN1_INPUT + i], &trig = inputs[TRIG1_INPUT + i], &voct = inputs[VOCT1_INPUT + i];
			Input &riseCv = inputs[RISECV1_INPUT + i], &fallCv = inputs[FALLCV1_INPUT + i], &bothCv = inputs[BOTHCV1_INPUT + i];
			int channels = 1;
			for (Input *x : {&in, &trig, &voct, &riseCv, &fallCv, &bothCv})
				channels = std::max(channels, x->getChannels());
			float riseBase = cachedKnobTime(2 * s, params[RISE1_PARAM + p].getValue());
			float fallBase = cachedKnobTime(2 * s + 1, params[FALL1_PARAM + p].getValue());
			float riseAmt = params[RISECV1_PARAM + p].getValue(), fallAmt = params[FALLCV1_PARAM + p].getValue();
			bool riseCurve = params[RISEEXP1_PARAM + 2 * s].getValue() > 0.5f;
			bool fallCurve = params[FALLEXP1_PARAM + 2 * s].getValue() > 0.5f;
			bool cycle = params[CYCLE1_PARAM + s].getValue() > 0.5f;
			for (int c = 0; c < channels; c++) {
				BancauSlope &sl = slope[s][c];
				float both = bothCv.getPolyVoltage(c), oct = voct.getPolyVoltage(c);
				// each volt halves the time (positive amount) or doubles it
				float riseT = clamp(riseBase * dsp::exp2_taylor5(-(riseAmt * (riseCv.getPolyVoltage(c) + both) + oct)), args.sampleTime, 200.f);
				float fallT = clamp(fallBase * dsp::exp2_taylor5(-(fallAmt * (fallCv.getPolyVoltage(c) + both) + oct)), args.sampleTime, 200.f);
				bool t = trig.isConnected() && sl.trig.process(trig.getPolyVoltage(c));
				sl.process(args.sampleTime, riseT, fallT, riseCurve, fallCurve, cycle, t, in.isConnected(), in.getPolyVoltage(c));
				outputs[OUT1_OUTPUT + 3 * s].setVoltage(sl.out, c);
				outputs[END1_OUTPUT + 3 * s].setVoltage(sl.end() ? 10.f : 0.f, c);
				if (s == 0)
					outputs[PULSE1_OUTPUT].setVoltage(sl.out > 5.f ? 10.f : 0.f, c);
				else
					outputs[BP2_OUTPUT].setVoltage(sl.out - 5.f, c);
			}
			for (int o = 0; o < 3; o++)
				outputs[3 * s + o].setChannels(channels);
		}
	}
};

struct BANCAUWidget : BidooWidget {
	BANCAUWidget(BANCAU *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/BANCAU.svg"));

		const float xPilars[6] = {10-2.5f,45-2.5f,80-2.5f,120-2.5f,155-2.5f,190-2.5f};
		const int yPilars[6] = {60,110,160,208,260,320};
		const int portOffset = 3;
		const int ckssOffset = 8;

		addParam(createParam<BidooBlueKnob>(Vec(xPilars[0], yPilars[0]), module, BANCAU::RISE1_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(xPilars[2], yPilars[0]), module, BANCAU::FALL1_PARAM));

		addParam(createParam<BidooBlueKnob>(Vec(xPilars[0], yPilars[1]), module, BANCAU::RISECV1_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(xPilars[2], yPilars[1]), module, BANCAU::FALLCV1_PARAM));

		addInput(createInput<PJ301MPort>(Vec(xPilars[0]+portOffset, yPilars[2]+portOffset), module, BANCAU::RISECV1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[1]+portOffset, yPilars[2]+portOffset), module, BANCAU::BOTHCV1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[2]+portOffset, yPilars[2]+portOffset), module, BANCAU::FALLCV1_INPUT));

		addParam(createParam<CKSS>(Vec(xPilars[0]+ckssOffset, yPilars[3]+5), module, BANCAU::RISEEXP1_PARAM));
		addParam(createParam<CKSS>(Vec(xPilars[1]+ckssOffset, yPilars[3]+5), module, BANCAU::CYCLE1_PARAM));
		addParam(createParam<CKSS>(Vec(xPilars[2]+ckssOffset, yPilars[3]+5), module, BANCAU::FALLEXP1_PARAM));

		addInput(createInput<PJ301MPort>(Vec(xPilars[0]+portOffset, yPilars[4]+portOffset), module, BANCAU::VOCT1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[1]+portOffset, yPilars[4]+portOffset), module, BANCAU::IN1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[2]+portOffset, yPilars[4]+portOffset), module, BANCAU::TRIG1_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(xPilars[0]+portOffset, yPilars[5]+portOffset), module, BANCAU::OUT1_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(xPilars[1]+portOffset, yPilars[5]+portOffset), module, BANCAU::PULSE1_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(xPilars[2]+portOffset, yPilars[5]+portOffset), module, BANCAU::END1_OUTPUT));

		addParam(createParam<BidooBlueKnob>(Vec(xPilars[3], yPilars[0]), module, BANCAU::RISE2_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(xPilars[5], yPilars[0]), module, BANCAU::FALL2_PARAM));

		addParam(createParam<BidooBlueKnob>(Vec(xPilars[3], yPilars[1]), module, BANCAU::RISECV2_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(xPilars[5], yPilars[1]), module, BANCAU::FALLCV2_PARAM));

		addInput(createInput<PJ301MPort>(Vec(xPilars[3]+portOffset, yPilars[2]+portOffset), module, BANCAU::RISECV2_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[4]+portOffset, yPilars[2]+portOffset), module, BANCAU::BOTHCV2_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[5]+portOffset, yPilars[2]+portOffset), module, BANCAU::FALLCV2_INPUT));

		addParam(createParam<CKSS>(Vec(xPilars[3]+ckssOffset, yPilars[3]+5), module, BANCAU::RISEEXP2_PARAM));
		addParam(createParam<CKSS>(Vec(xPilars[4]+ckssOffset, yPilars[3]+5), module, BANCAU::CYCLE2_PARAM));
		addParam(createParam<CKSS>(Vec(xPilars[5]+ckssOffset, yPilars[3]+5), module, BANCAU::FALLEXP2_PARAM));

		addInput(createInput<PJ301MPort>(Vec(xPilars[3]+portOffset, yPilars[4]+portOffset), module, BANCAU::VOCT2_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[4]+portOffset, yPilars[4]+portOffset), module, BANCAU::IN2_INPUT));
		addInput(createInput<PJ301MPort>(Vec(xPilars[5]+portOffset, yPilars[4]+portOffset), module, BANCAU::TRIG2_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(xPilars[3]+portOffset, yPilars[5]+portOffset), module, BANCAU::OUT2_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(xPilars[4]+portOffset, yPilars[5]+portOffset), module, BANCAU::BP2_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(xPilars[5]+portOffset, yPilars[5]+portOffset), module, BANCAU::END2_OUTPUT));
	}
};

Model *modelBANCAU = createModel<BANCAU, BANCAUWidget>("BanCau");
