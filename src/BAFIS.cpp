#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"
#include "dep/bidoo-dsp/bidoo/dsp/shaping/TanhPade.h"

// Four bands split at three Linkwitz-Riley crossovers (24 dB/oct), each band
// distorted before or after its filters. The bands are built so they add up
// flat: band 1 is LP(f1) then allpasses at f2 and f3, band 2 HP(f1) LP(f2)
// then an allpass at f3, band 3 HP(f1) HP(f2) LP(f3), band 4 HP(f1) HP(f2)
// HP(f3), every LP / HP a pair of Butterworth stages. A band's resonance
// raises the Q of the second stage at its own edges: at its minimum the bands
// sum flat.

// coefficients of a TPT state-variable stage
struct BafisSvfCoef {
	float g = 0.f, r2g = 1.f, h = 1.f, fourR = 2.828427f;
	void set(float gain, float q) {
		float R = 0.5f / q;
		g = gain;
		r2g = 2.f * R + g;
		h = 1.f / (1.f + 2.f * R * g + g * g);
		fourR = 4.f * R;
	}
};

// one stage's state
struct BafisSvf {
	float m1 = 0.f, m2 = 0.f;
	inline float lp(float x, const BafisSvfCoef &c) {
		float hp = (x - c.r2g * m1 - m2) * c.h;
		float bp = c.g * hp + m1;
		float lo = c.g * bp + m2;
		m1 = c.g * hp + bp;
		m2 = c.g * bp + lo;
		return lo;
	}
	inline float hp(float x, const BafisSvfCoef &c) {
		float hi = (x - c.r2g * m1 - m2) * c.h;
		float bp = c.g * hi + m1;
		float lo = c.g * bp + m2;
		m1 = c.g * hi + bp;
		m2 = c.g * bp + lo;
		return hi;
	}
	// allpass: lp + hp - 2R bp = x - 4R bp
	inline float ap(float x, const BafisSvfCoef &c) {
		float hi = (x - c.r2g * m1 - m2) * c.h;
		float bp = c.g * hi + m1;
		float lo = c.g * bp + m2;
		m1 = c.g * hi + bp;
		m2 = c.g * bp + lo;
		return x - c.fourR * bp;
	}
};

// The distortions, at twice the rate (a polyphase IIR halfband each way): the
// harmonics up to 1.5 x Nyquist are filtered out instead of folding back. Past
// a drive of about 2, tanh and the hard clip also use first-order antiderivative
// anti-aliasing (ADAA) at that rate, which takes the rest of the folding down
// by another 10-15 dB (blended in between, as ADAA's two-sample average would
// otherwise dull the top of a clean band by 1.6 dB at 18 kHz). The drive knob
// (1 to 10) sets d from 0.1 (near linear) to 10, and each curve is scaled so a
// full-scale input (5 V) still reaches full scale: tanh(d x) / tanh(d),
// sin(d x) / sin(d) up to d = pi / 2, clamp(d x) / min(d, 1).
struct BafisShaper {
	Halfband2x over;
	float u0 = 0.f, F0 = 0.f;  // ADAA: the previous (2x) sample and its antiderivative
	bool primed = true;        // F0 belongs to u0 (the previous sample took the ADAA path)

	static float logCosh(float u) {
		float a = std::fabs(u);
		return a + std::log1p(std::exp(-2.f * a)) - 0.69314718f;
	}
	static float clipF(float u) {
		float a = std::fabs(u);
		return a <= 1.f ? 0.5f * u * u : a - 0.5f;
	}

	inline float shape(int type, float u, float w) {
		if (type == 1)
			return std::sin(u);
		float plain = type == 0 ? bidoo::dsp::tanhPade(u) : clamp(u, -1.f, 1.f);
		if (w <= 0.f) {
			primed = false;
			return plain;
		}
		float F1 = type == 0 ? logCosh(u) : clipF(u);
		float du = u - u0;
		float adaa;
		if (!primed)
			adaa = plain;
		else if (std::fabs(du) < 2e-3f)
			adaa = type == 0 ? bidoo::dsp::tanhPade(0.5f * (u + u0)) : clamp(0.5f * (u + u0), -1.f, 1.f);
		else
			adaa = (F1 - F0) / du;
		u0 = u;
		F0 = F1;
		primed = true;
		return plain + (adaa - plain) * w;
	}

	float process(int type, float d, float norm, float x) {
		// ADAA from d = 1 (fully from d = 2)
		float w = type == 1 ? 0.f : clamp(d - 1.f, 0.f, 1.f);
		float a, b;
		over.up(x, a, b);
		a = shape(type, d * a, w);
		b = shape(type, d * b, w);
		return over.down(a, b) * norm;
	}

	static float driveToD(float drive) {
		return 0.1f + (drive - 1.f) * (9.9f / 9.f);
	}
	static float normFor(int type, float d) {
		if (type == 0)
			return 1.f / std::tanh(d);
		if (type == 1)
			return d < 1.5707963f ? 1.f / std::sin(d) : 1.f;
		return 1.f / std::min(d, 1.f);
	}
};

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

	static constexpr int CONTROL = 16;
	static constexpr float BUTTERWORTH = 0.70710678f;

	struct Band {
		// the filter stages, in the order of the chain described above
		BafisSvf s[6];
		BafisShaper shaper;
		// what is playing, and what the knobs / CVs ask for
		int type = 0, wantType = 0;
		bool post = false, wantPost = false;
		// a type or pre/post change ducks the band for 2.5 ms, switches, then
		// brings it back in 2.5 ms
		float duck = 1.f;
		float d = 0.1f, norm = 1.f, volume = 0.5f;          // smoothed
		float dTarget = 0.1f, volumeTarget = 0.5f;
	};

	struct Channel {
		Band band[4];
		// crossover i: Butterworth stage; band b's resonant stages at its lower
		// (hpRes) and upper (lpRes) edges
		BafisSvfCoef bw[3], lpRes[4], hpRes[4];
		SvfGainCache gain[3];
	};

	Channel ch[PORT_MAX_CHANNELS];
	dsp::ClockDivider control;
	int lastChannels = 0;

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
			configParam(Q_PARAM + i, .1f, 1.f, .1f, band + "resonance (minimum: the bands add up flat)", "%", 0.f, 100.f);
			configParam(GAIN_PARAM + i, 1.f, 10.f, 1.f, band + "drive (1: clean)", "x");
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
		control.setDivision(CONTROL);
	}

	// knobs and CVs, every CONTROL samples (and at once when the channel count changes)
	void readControls(int channels, float sampleRate) {
		for (int c = 0; c < channels; c++) {
			Channel &k = ch[c];
			// crossovers kept in order
			float g[3];
			float pitch = 0.f;
			for (int i = 0; i < 3; i++) {
				float p = clamp(knobToPitch(params[FREQ_PARAM + i].getValue()) + inputs[FREQ_INPUT + i].getPolyVoltage(c), 4.5f, 14.f);
				pitch = i == 0 ? p : std::max(pitch, p);
				g[i] = k.gain[i].get(dsp::exp2_taylor5(pitch), sampleRate);
				k.bw[i].set(g[i], BUTTERWORTH);
			}
			for (int b = 0; b < 4; b++) {
				Band &band = k.band[b];
				// resonance: Butterworth at the knob's minimum, up to Q 10
				float r = clamp((params[Q_PARAM + b].getValue() + inputs[Q_INPUT + b].getPolyVoltage(c) * 0.1f - 0.1f) / 0.9f, 0.f, 1.f);
				float q = BUTTERWORTH * std::pow(10.f / BUTTERWORTH, r);
				if (b < 3)
					k.lpRes[b].set(g[b], q);
				if (b > 0)
					k.hpRes[b].set(g[b - 1], q);
				float drive = clamp(params[GAIN_PARAM + b].getValue() + inputs[GAIN_INPUT + b].getPolyVoltage(c), 1.f, 10.0f);
				band.dTarget = BafisShaper::driveToD(drive);
				band.volumeTarget = clamp(params[VOLUME_PARAM + b].getValue() + inputs[VOLUME_INPUT + b].getPolyVoltage(c) * 0.1f, 0.f, 1.0f);
				band.wantType = inputs[TYPE_INPUT + b].isConnected()
					? clamp((int)(inputs[TYPE_INPUT + b].getPolyVoltage(c) * 0.3f), 0, 2)
					: clamp((int)params[TYPE_PARAM + b].getValue(), 0, 2);
				band.wantPost = inputs[PREPOST_INPUT + b].isConnected()
					? inputs[PREPOST_INPUT + b].getPolyVoltage(c) >= 1.f
					: params[PREPOST_PARAM + b].getValue() > 0.5f;
			}
		}
	}

	// the band's filters (s: its stages, see the top)
	static float filterBand(int b, Band &band, const Channel &k, float x) {
		BafisSvf *s = band.s;
		switch (b) {
			case 0:
				x = s[1].lp(s[0].lp(x, k.bw[0]), k.lpRes[0]);
				x = s[2].ap(x, k.bw[1]);
				return s[3].ap(x, k.bw[2]);
			case 1:
				x = s[1].hp(s[0].hp(x, k.bw[0]), k.hpRes[1]);
				x = s[3].lp(s[2].lp(x, k.bw[1]), k.lpRes[1]);
				return s[4].ap(x, k.bw[2]);
			case 2:
				x = s[1].hp(s[0].hp(x, k.bw[0]), k.bw[0]);
				x = s[3].hp(s[2].hp(x, k.bw[1]), k.hpRes[2]);
				return s[5].lp(s[4].lp(x, k.bw[2]), k.lpRes[2]);
			default:
				x = s[1].hp(s[0].hp(x, k.bw[0]), k.bw[0]);
				x = s[3].hp(s[2].hp(x, k.bw[1]), k.bw[1]);
				return s[5].hp(s[4].hp(x, k.bw[2]), k.hpRes[3]);
		}
	}

	void process(const ProcessArgs &args) override {
		int channels = std::max(1, inputs[IN].getChannels());
		if (control.process() || channels != lastChannels) {
			readControls(channels, args.sampleRate);
			if (channels != lastChannels) {
				// new channels start from what their knobs say
				for (int c = lastChannels; c < channels; c++)
					for (Band &band : ch[c].band) {
						band.type = band.wantType;
						band.post = band.wantPost;
						band.d = band.dTarget;
						band.volume = band.volumeTarget;
						band.norm = BafisShaper::normFor(band.type, band.d);
					}
				lastChannels = channels;
			}
		}
		// 5 ms glides for drive and volume, 2.5 ms ducks for a type or pre/post change
		float smooth = std::min(1.f, 1.f / (0.005f * args.sampleRate));
		float duckStep = 1.f / (0.0025f * args.sampleRate);

		for (int c = 0; c < channels; c++) {
			Channel &k = ch[c];
			float in = inputs[IN].getVoltage(c) * 0.2f;
			float out = 0.f;
			for (int b = 0; b < 4; b++) {
				Band &band = k.band[b];
				bool change = band.wantType != band.type || band.wantPost != band.post;
				if (change) {
					band.duck -= duckStep;
					if (band.duck <= 0.f) {
						band.duck = 0.f;
						band.type = band.wantType;
						band.post = band.wantPost;
						band.shaper = BafisShaper();
						band.norm = BafisShaper::normFor(band.type, band.d);
					}
				}
				else if (band.duck < 1.f)
					band.duck = std::min(1.f, band.duck + duckStep);
				if (band.d != band.dTarget) {
					band.d += (band.dTarget - band.d) * smooth;
					if (std::fabs(band.dTarget - band.d) < 1e-4f)
						band.d = band.dTarget;
					band.norm = BafisShaper::normFor(band.type, band.d);
				}
				band.volume += (band.volumeTarget - band.volume) * smooth;

				float x = band.post ? in : band.shaper.process(band.type, band.d, band.norm, in);
				x = filterBand(b, band, k, x);
				if (band.post)
					x = band.shaper.process(band.type, band.d, band.norm, x);
				out += x * band.volume * band.duck;
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

		theme::addScrews(this);

		// The original hand-drawn panel: three frequency knobs with their CV jacks over the filter-shape drawing; four
		// columns of Q, gain, type, pre/post and volume, each with its tiny CV jack; IN and OUT. Old corners kept (each
		// column's members within 0.5 px of each other; the rows have no labels to align to), except IN and OUT, now on
		// their boxes' centres (OUT was 0.6 px left).
		const float kFreqRight = 35.f;   // across, for the frequency knobs and the four columns
		const theme::Size kSmallKnobSize = {mm2px(8.f), mm2px(8.f)};   // BidooSmallBlueKnob
		const theme::Size kCKSSSize = {14.f, 20.64111f};               // CKSS
		const theme::Pos freq = theme::specificCorner(25, 30, theme::kBlueKnobSize);
		const theme::Pos freqCv = theme::specificCorner(32, 62, theme::kTinyJackSize);
		for (int i = 0; i < 3; i++)
			addParam(theme::param<BidooBlueKnob>(freq.right(kFreqRight * i), module, BAFIS::FREQ_PARAM + i));
		for (int i = 0; i < 3; i++)
			addInput(theme::input<TinyPJ301MPort>(freqCv.right(kFreqRight * i), module, BAFIS::FREQ_INPUT + i));
		const theme::Pos q = theme::specificCorner(11, 85, kSmallKnobSize);
		const theme::Pos qCv = theme::specificCorner(15, 111, theme::kTinyJackSize);
		const theme::Pos prePost = theme::specificCorner(16, 218, kCKSSSize);
		for (int i = 0; i < 4; i++) {
			const float dx = kFreqRight * i;
			addParam(theme::param<BidooSmallBlueKnob>(q.right(dx), module, BAFIS::Q_PARAM + i));
			addInput(theme::input<TinyPJ301MPort>(qCv.right(dx), module, BAFIS::Q_INPUT + i));
			addParam(theme::param<BidooSmallBlueKnob>(q.offset(dx, 44), module, BAFIS::GAIN_PARAM + i));
			addInput(theme::input<TinyPJ301MPort>(qCv.offset(dx, 44), module, BAFIS::GAIN_INPUT + i));
			addParam(theme::param<BidooSmallSnapBlueKnob>(q.offset(dx, 88), module, BAFIS::TYPE_PARAM + i));
			addInput(theme::input<TinyPJ301MPort>(qCv.offset(dx, 88), module, BAFIS::TYPE_INPUT + i));
			addParam(theme::param<CKSS>(prePost.right(dx), module, BAFIS::PREPOST_PARAM + i));
			addInput(theme::input<TinyPJ301MPort>(qCv.offset(dx, 131), module, BAFIS::PREPOST_INPUT + i));
			addParam(theme::param<BidooSmallBlueKnob>(q.offset(dx, 176), module, BAFIS::VOLUME_PARAM + i));
			addInput(theme::input<TinyPJ301MPort>(qCv.offset(dx, 176), module, BAFIS::VOLUME_INPUT + i));
		}
		const theme::Pos ioRow = theme::specificCorner(6.8f, 330, theme::kJackSize);
		addInput(theme::input<PJ301MPort>(theme::colRow(theme::specific(19.156f, 0), ioRow), module, BAFIS::IN));     // the IN box's centre
		addOutput(theme::output<PJ301MPort>(theme::colRow(theme::specific(130.844f, 0), ioRow), module, BAFIS::OUT));  // the OUT box's centre
	}
};

Model *modelBAFIS = createModel<BAFIS, BAFISWidget>("BAFIS");
