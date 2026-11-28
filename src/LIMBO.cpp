// Based on Will Pirkle's courses & Vadim Zavalishin's book

#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"

using namespace std;
using simd::float_4;

// The filter runs four channels at once: T is simd::float_4, one channel per lane, the same arithmetic in each lane
// as the one-channel code it replaces (the same output with exact rounding; Rack's -funsafe-math-optimizations lets
// the compiler reorder scalar and lane code differently, about -120 dB). A mono patch runs it in plain floats.

// tanh by its Pade 7/6 approximant (bidoo-dsp's shaping/TanhPade.h, the same operations), for lanes.
template <typename T>
inline T tanhPadeT(T x) {
	x = simd::fmin(simd::fmax(x, T(-4.97f)), T(4.97f));   // where the approximant reaches 1
	const T x2 = x * x;
	const T n = x * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
	const T d = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + 28.0f * x2));
	return simd::fmin(simd::fmax(n / d, T(-1.0f)), T(1.0f));
}

// One-pole zero-delay-feedback stage. G = g / (1 + g) with g = tan(pi fc / fs).
template <typename T>
struct FilterStage {
	T mem = 0.0f;

	T filter(T in, T G) {
		T v = (in - mem) * G;
		T out = v + mem;
		mem = out + v;
		return out;
	}
};

// What the filter needs, worked out once per sample for all four stages (and both sides): G, the feedback, and the
// saturation's scaling.
template <typename T>
struct LadderCoefs {
	T G = 0.0f, inv = 1.0f, G4 = 0.0f, k = 0.0f;
	T drive = 1.0f, invDrive = 1.0f;
	bool nonLinear = false;

	void set(T g, T kk, T d, bool nl) {
		G = g / (1.0f + g);
		inv = 1.0f / (1.0f + g);
		G4 = G * G * G * G;
		k = kk;
		nonLinear = nl;
		if (nl) {
			drive = d;
			invDrive = 1.0f / d;
		}
	}
};

// 4-pole ladder (Zavalishin): the feedback is solved from the stages' states,
// S = G^3 s1 + G^2 s2 + G s3 + s4 (each s divided by 1 + g). Saturated, each
// stage's input goes through tanh(d x) / d: a slope of 1 at rest whatever the
// drive, so the gain boost no longer raises the resonance.
template <typename T>
struct LadderFilter {
	FilterStage<T> stages[4];

	T process(T sample, const LadderCoefs<T> &c) {
		T S = 0.0f;
		for (int i = 0; i < 4; i++)
			S = S * c.G + stages[i].mem * c.inv;
		T u = (sample - c.k * S) / (1.0f + c.k * c.G4);
		if (c.nonLinear) {
			for (int i = 0; i < 4; i++)
				u = stages[i].filter(tanhPadeT(u * c.drive) * c.invDrive, c.G);
		}
		else {
			for (int i = 0; i < 4; i++)
				u = stages[i].filter(u, c.G);
		}
		return u;
	}
};

// Linear up to 8 V, then bends to stay under 10 V (9.95 V at most).
inline float softLimit(float x) {
	return bidooSoftLimit(x);
}

struct LIMBO : BidooModule {
	enum ParamIds {
		CUTOFF_PARAM,
		Q_PARAM,
		CMOD_PARAM,
		MUG_PARAM,
		MODE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_L,
		IN_R,
		CUTOFF_INPUT,
		Q_INPUT,
		MUG_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L,
		OUT_R,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	// One group of channels: four in float_4 lanes, or the single channel of a mono patch in plain floats (as fast as
	// the one-channel code was: lanes would cost more there).
	template <typename T>
	struct Side {
		LadderFilter<T> filter, old;   // old: the mode a MODE change fades out from
		// Saturated mode runs at twice the rate: the tanh's harmonics above
		// Nyquist are filtered out instead of folding back
		Halfband2xT<T> over;
	};
	template <typename T>
	struct Group {
		Side<T> l, r;
		LadderCoefs<T> coefs, oldCoefs;
	};
	Group<float_4> groups[4];
	Group<float> mono;
	bool monoActive = true;   // channel 0's state is in mono, not in groups[0]'s first lane
	static_assert(sizeof(Side<float_4>) == 4 * sizeof(Side<float>), "a side is floats only");

	// channel 0's filter state, between mono and the first lane of groups[0]
	static void moveLane(Side<float> &one, Side<float_4> &four, bool toFour) {
		float *o = reinterpret_cast<float *>(&one);
		float *f = reinterpret_cast<float *>(&four);
		for (size_t i = 0; i < sizeof(Side<float>) / sizeof(float); i++) {
			if (toFour)
				f[4 * i] = o[i];
			else
				o[i] = f[4 * i];
		}
	}

	SvfGainCache gainCache[16];
	// the saturation's drive and output gain, worked out when the boost moves
	float lastBoost[16], pre[16], post[16];
	// the boost glides (about 3 ms): its gain jumped with a CV or a knob; its exp2, worked out when it moves
	float boostNow[16], boostOf[16], boostGain[16];
	bool wasNonLinear = false;
	// a MODE change crossfades over 5 ms from the old mode, run on a copy of each filter
	int modeFadeLen = 240;   // 5 ms at the rate of the change
	int modeFadeLeft = 0;
	bool oldNonLinear = false;

	LIMBO() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(CUTOFF_PARAM, 0.0f, 1.0f, 1.0f, "Cutoff", " Hz", std::pow(2.0f, 9.5f), std::pow(2.0f, 4.5f));
		configParam(Q_PARAM, 0.0f, 1.0f, 0.0f, "Resonance", "%", 0.f, 100.f);
		configParam(MUG_PARAM, 0.0f, 1.0f, 0.0f, "Gain boost", "%", 0.f, 100.f);
		configParam(CMOD_PARAM, -1.0f, 1.0f, 0.0f, "Cutoff CV amount (100% = 1V/oct)", "%", 0.f, 100.f);
		configSwitch(MODE_PARAM, 0.0f, 1.0f, 0.0f, "Mode", {"Linear", "Saturated"});
		configInput(IN_L, "Left (polyphonic)");
		configInput(IN_R, "Right (normalled to left)");
		configInput(CUTOFF_INPUT, "Cutoff CV (1V/oct at 100%)");
		configInput(Q_INPUT, "Resonance CV (10 V = 200%)");
		configInput(MUG_INPUT, "Gain boost CV (10 V = 200%)");
		configOutput(OUT_L, "Left");
		configOutput(OUT_R, "Right");
		configBypass(IN_L, OUT_L);
		configBypass(IN_R, OUT_R);
		for (int c = 0; c < 16; c++) {
			lastBoost[c] = -1.0f;
			boostNow[c] = -1.0f;
			boostOf[c] = -1.0f;
			boostGain[c] = 1.0f;
			pre[c] = post[c] = 1.0f;
		}
	}

	// what one sample's groups need from process()
	struct Frame {
		int channels;
		bool rightIn, nonLinear, fading;
		float fadeNew, rate, oldRate, fMax, knobOct, cmod, qKnob, boostKnob, boostGlide;
	};

	static float_4 loadT(const float *a, float_4) { return float_4::load(a); }
	static float loadT(const float *a, float) { return a[0]; }
	static float_4 voltsT(Input &in, int c, float_4) { return in.getPolyVoltageSimd<float_4>(c); }
	static float voltsT(Input &in, int c, float) { return in.getPolyVoltage(c); }
	static float laneT(float_4 v, int j) { return v[j]; }
	static float laneT(float v, int) { return v; }

	// Bypassed: the routes of configBypass, plus the normalling of process(). Without a right input, OUT R carries
	// the left as when the module runs, so a mono source stays on both sides instead of losing its right.
	void processBypass(const ProcessArgs &args) override {
		Module::processBypass(args);
		if (!inputs[IN_R].isConnected()) {
			const int channels = inputs[IN_L].getChannels();
			for (int c = 0; c < channels; c++)
				outputs[OUT_R].setVoltage(inputs[IN_L].getVoltage(c), c);
			outputs[OUT_R].setChannels(channels);
		}
	}

	void process(const ProcessArgs &args) override {
		Frame f;
		f.channels = std::max(std::max(inputs[IN_L].getChannels(), inputs[IN_R].getChannels()), 1);
		f.rightIn = inputs[IN_R].isConnected();
		f.nonLinear = params[MODE_PARAM].getValue() > 0.5f;
		if (f.nonLinear != wasNonLinear) {
			// the old mode carries on, on a copy of the filters, while the new one fades in; going saturated, the
			// oversampling filters start from rest (the old, linear, path does not use them)
			for (Group<float_4> &g : groups)
				startFade(g, f.nonLinear);
			startFade(mono, f.nonLinear);
			oldNonLinear = wasNonLinear;
			modeFadeLen = std::max(16, (int)(0.005f * args.sampleRate));
			modeFadeLeft = modeFadeLen;
			wasNonLinear = f.nonLinear;
		}
		f.fading = modeFadeLeft > 0;
		f.fadeNew = f.fading ? 1.f - (float)modeFadeLeft / (float)modeFadeLen : 1.f;
		if (f.fading)
			modeFadeLeft--;
		// saturated, the ladder runs at 2x
		f.rate = f.nonLinear ? 2.0f * args.sampleRate : args.sampleRate;
		f.oldRate = oldNonLinear ? 2.0f * args.sampleRate : args.sampleRate;
		f.fMax = 0.45f * args.sampleRate;
		f.knobOct = 4.5f + 9.5f * params[CUTOFF_PARAM].getValue();
		f.cmod = params[CMOD_PARAM].getValue();
		f.qKnob = params[Q_PARAM].getValue();
		f.boostKnob = params[MUG_PARAM].getValue();
		f.boostGlide = std::min(1.f, 336.f / args.sampleRate);   // 0.007 a sample at 48 kHz

		const bool monoNow = f.channels == 1;
		if (monoNow != monoActive) {
			moveLane(mono.l, groups[0].l, !monoNow);
			moveLane(mono.r, groups[0].r, !monoNow);
			monoActive = monoNow;
		}
		if (monoNow)
			runGroup(mono, 0, f);
		else
			for (int c0 = 0; c0 < f.channels; c0 += 4)
				runGroup(groups[c0 / 4], c0, f);
		outputs[OUT_L].setChannels(f.channels);
		outputs[OUT_R].setChannels(f.channels);
	}

	template <typename T>
	static void startFade(Group<T> &g, bool toNonLinear) {
		g.l.old = g.l.filter;
		g.r.old = g.r.filter;
		if (toNonLinear) {
			g.l.over.reset();
			g.r.over.reset();
		}
	}

	// one group: its channels' controls, then the filters on all its lanes at once
	template <typename T>
	void runGroup(Group<T> &grp, int c0, const Frame &f) {
		constexpr int W = sizeof(T) / sizeof(float);
		const int lanes = std::min(W, f.channels - c0);
		// each channel's controls (lanes past the last channel are worked out too, and play silence)
		alignas(16) float gA[4], kA[4], dA[4], inA[4], outA[4], ogA[4], oInA[4], oOutA[4];
		for (int j = 0; j < W; j++) {
			const int c = c0 + j;
			float fc = clamp(dsp::exp2_taylor5(clamp(f.knobOct + f.cmod * inputs[CUTOFF_INPUT].getPolyVoltage(c), 3.0f, 15.0f)), 10.0f, f.fMax);
			gA[j] = gainCache[c].get(fc, f.rate);
			kA[j] = 3.5f * clamp(f.qKnob + inputs[Q_INPUT].getPolyVoltage(c) * 0.2f, 0.0f, 1.0f);
			const float boostTarget = clamp(f.boostKnob + inputs[MUG_INPUT].getPolyVoltage(c) * 0.2f, 0.0f, 1.0f);
			if (boostNow[c] < 0.f || std::fabs(boostTarget - boostNow[c]) < 1e-5f)
				boostNow[c] = boostTarget;
			else
				boostNow[c] += f.boostGlide * (boostTarget - boostNow[c]);
			const float boostIn = boostNow[c];
			if (boostIn != boostOf[c]) {
				boostOf[c] = boostIn;
				boostGain[c] = dsp::exp2_taylor5(3.0f * boostIn);
			}
			const float boost = boostGain[c];
			// saturated: the stages saturate from 1 / d (d = boost / 3, as before);
			// the input is raised by s^3 and the output by s, s = d / tanh(d): the
			// gain and the ceiling of the old stages (each had a slope of s), the
			// resonance of the linear filter
			if ((f.nonLinear || f.fading) && boostIn != lastBoost[c]) {
				lastBoost[c] = boostIn;
				float d = boost / 3.0f;
				float s = d / std::tanh(d);
				pre[c] = s * s * s;
				post[c] = s;
			}
			dA[j] = boost / 3.0f;
			inA[j] = f.nonLinear ? 0.2f * pre[c] : 0.2f;
			outA[j] = 5.0f * (f.nonLinear ? post[c] : boost);
			if (f.fading) {
				// the old mode, at its own rate and gains
				ogA[j] = svfGain(fc, f.oldRate);
				oInA[j] = oldNonLinear ? 0.2f * pre[c] : 0.2f;
				oOutA[j] = 5.0f * (oldNonLinear ? post[c] : boost);
			}
		}
		const T k = loadT(kA, T()), d = loadT(dA, T());
		const T inGain = loadT(inA, T()), outGain = loadT(outA, T());
		grp.coefs.set(loadT(gA, T()), k, d, f.nonLinear);

		const T xL = voltsT(inputs[IN_L], c0, T());
		T yL = run(grp.l.filter, grp.l.over, xL * inGain, grp.coefs) * outGain;
		const T xR = f.rightIn ? voltsT(inputs[IN_R], c0, T()) : T(0.f);
		T yR = f.rightIn ? run(grp.r.filter, grp.r.over, xR * inGain, grp.coefs) * outGain : T(0.f);
		if (f.fading) {
			grp.oldCoefs.set(loadT(ogA, T()), k, d, oldNonLinear);
			const T oIn = loadT(oInA, T()), oOut = loadT(oOutA, T());
			const T oL = run(grp.l.old, grp.l.over, xL * oIn, grp.oldCoefs) * oOut;
			yL = oL + f.fadeNew * (yL - oL);
			if (f.rightIn) {
				const T oR = run(grp.r.old, grp.r.over, xR * oIn, grp.oldCoefs) * oOut;
				yR = oR + f.fadeNew * (yR - oR);
			}
		}
		for (int j = 0; j < lanes; j++) {
			const float l = softLimit(laneT(yL, j));
			outputs[OUT_L].setVoltage(l, c0 + j);
			// without a right input, the right side would be the same filter on the same input
			outputs[OUT_R].setVoltage(f.rightIn ? softLimit(laneT(yR, j)) : l, c0 + j);
		}
	}

	template <typename T>
	static T run(LadderFilter<T> &f, Halfband2xT<T> &over, T x, const LadderCoefs<T> &c) {
		if (!c.nonLinear)
			return f.process(x, c);
		T a, b;
		over.up(x, a, b);
		a = f.process(a, c);
		b = f.process(b, c);
		return over.down(a, b);
	}
};

struct LIMBOWidget : BidooWidget {
	LIMBOWidget(LIMBO *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/LIMBO.svg"));

		theme::addScrews(this);

		// The original hand-drawn panel: the cutoff knob, RES and GAIN either side below it, F.MOD under RES and the mode
		// switch beside it; three boxed CV jacks; the stereo in and out. Old corners kept (specific) except where a
		// control was off its label or box: GAIN mirrored from RES about the cutoff knob, the CV jacks on their boxes'
		// centres, each stereo jack under its L or R label.
		const theme::Size kCKSSSize = {14.f, 20.64111f};   // CKSS
		const theme::Pos cutoff = theme::specificCorner(32, 61, theme::kHugeKnobSize);
		const theme::Pos q = theme::specificCorner(12, 143, theme::kLargeKnobSize);
		const theme::Pos mug = q.right(2.f * (cutoff.x - q.x));   // RES mirrored about the cutoff knob
		addParam(theme::param<BidooHugeBlueKnob>(cutoff, module, LIMBO::CUTOFF_PARAM));
		addParam(theme::param<BidooLargeBlueKnob>(q, module, LIMBO::Q_PARAM));
		addParam(theme::param<BidooLargeBlueKnob>(mug, module, LIMBO::MUG_PARAM));
		addParam(theme::param<BidooLargeBlueKnob>(theme::specificCorner(12, 208, theme::kLargeKnobSize), module, LIMBO::CMOD_PARAM));
		addParam(theme::param<CKSS>(theme::specificCorner(82.5f, 217, kCKSSSize), module, LIMBO::MODE_PARAM));

		const float kBoxPitch = 40.844f;   // from one CV box's centre to the next
		const theme::Pos cutoffCv = theme::colRow(theme::specific(19.156f, 0), theme::specificCorner(7, 283, theme::kJackSize));   // the FREQ box's centre
		addInput(theme::input<PJ301MPort>(cutoffCv, module, LIMBO::CUTOFF_INPUT));
		addInput(theme::input<PJ301MPort>(cutoffCv.right(kBoxPitch), module, LIMBO::Q_INPUT));
		addInput(theme::input<PJ301MPort>(cutoffCv.right(2.f * kBoxPitch), module, LIMBO::MUG_INPUT));

		// Each stereo jack under its L or R label (the panel spaces the IN labels 23.5 px apart and the OUT ones 21.5)
		const theme::Pos stereoRow = theme::specificCorner(8, 340, theme::kTinyJackSize);
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specific(14.594f, 0), stereoRow), module, LIMBO::IN_L));
		addInput(theme::input<TinyPJ301MPort>(theme::colRow(theme::specific(38.125f, 0), stereoRow), module, LIMBO::IN_R));
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(theme::specific(82.281f, 0), stereoRow), module, LIMBO::OUT_L));
		addOutput(theme::output<TinyPJ301MPort>(theme::colRow(theme::specific(103.812f, 0), stereoRow), module, LIMBO::OUT_R));
	}
};

Model *modelLIMBO = createModel<LIMBO, LIMBOWidget>("lIMbO");
