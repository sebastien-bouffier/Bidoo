#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"

using namespace std;

#define BANDS 16
#define BANDS2 8
#define BANDS4 4
using simd::float_4;

// Band-pass biquad coefficients, four bands per float_4; shared by every channel's state.
struct ZBiquad {
	float_4 a0 = 1.f, a1 = 0.f, a2 = 0.f, b1 = 0.f, b2 = 0.f;

	void setBandPass(float_4 Fc, float_4 Q);
};

// out of line as before: inlined, -funsafe-math rounds the coefficients differently
void ZBiquad::setBandPass(float_4 Fc, float_4 Q) {
	float_4 norm;
	float_4 K = tan(M_PI * Fc);
	norm = 1 / (1 + K / Q + K * K);
	a0 = K / Q * norm;
	a1 = 0;
	a2 = -a0;
	b1 = 2 * (K * K - 1) * norm;
	b2 = (1 - K / Q + K * K) * norm;
}

// One filter's state (transposed direct form II).
struct ZBiquadState {
	float_4 z1 = 0.f, z2 = 0.f;

	inline float_4 process(const ZBiquad &f, float_4 in) {
		float_4 out = in * f.a0 + z1;
		z1 = in * f.a1 + z2 - f.b1 * out;
		z2 = in * f.a2 - f.b2 * out;
		return out;
	}
};

struct ZINC : BidooModule {
	enum ParamIds {
		BG_PARAM,
		ATTACK_PARAM = BG_PARAM + BANDS,
		DECAY_PARAM,
		Q1_PARAM,
		Q2_PARAM,
		Q3_PARAM,
		Q4_PARAM,
		GMOD_PARAM,
		GCARR_PARAM,
		G_PARAM,
		SHAPE_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_MOD,
		IN_CARR,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	// one modulator analysis; one carrier filter bank per carrier channel, same coefficients
	ZBiquad iFilter[BANDS2];
	ZBiquad cFilter[BANDS2];
	ZBiquadState iState[BANDS2];
	ZBiquadState cState[PORT_MAX_CHANNELS][BANDS2];
	int carrierChannels = 1;
	float_4 mem[BANDS4] = { 0.0f };
	// 15 bands spread evenly on a log scale from 100 Hz to 10 kHz (0.47 octave
	// apart), and a 16th for the air above them (13.9 kHz, the next step)
	float_4 freq[BANDS4];
	// the Q at which a band's -3 dB width meets its neighbours' (0.47 octave)
	static constexpr float Q_SPACING = 3.03f;
	float tunedRate = 0.f;
	float tunedQ[4] = {0.f, 0.f, 0.f, 0.f};
	// the level each path's two filters are scaled by, so a wider or narrower
	// band (Q) passes about the same power
	float modNorm = 1.f, carNorm = 1.f;
	// envelope follower: one-pole coefficients for the attack and decay knobs
	float tunedAttack = -1.f, tunedDecay = -1.f, envRate = 0.f;
	float cAttack = 1.f, cDecay = 1.f;

	// the knob (1 to 10, 5 in the middle) as a Q: 5 meets the neighbours
	static float knobToQ(float k) {
		return Q_SPACING * k / 5.f;
	}

	// Band centres under Nyquist; filters retuned only when a Q or the rate moves.
	void tune(float sampleRate) {
		float q[4] = {params[Q1_PARAM].getValue(), params[Q2_PARAM].getValue(), params[Q3_PARAM].getValue(), params[Q4_PARAM].getValue()};
		if (sampleRate == tunedRate && q[0] == tunedQ[0] && q[1] == tunedQ[1] && q[2] == tunedQ[2] && q[3] == tunedQ[3])
			return;
		float Q[4];
		for (int k = 0; k < 4; k++)
			Q[k] = knobToQ(q[k]);
		for (int i = 0; i < BANDS4; i++) {
			float_4 fc = simd::fmin(freq[i], 0.45f * sampleRate) / sampleRate;
			iFilter[i].setBandPass(fc, Q[0]);
			iFilter[i + BANDS4].setBandPass(fc, Q[1]);
			cFilter[i].setBandPass(fc, Q[2]);
			cFilter[i + BANDS4].setBandPass(fc, Q[3]);
		}
		// two band-passes in a row pass a power about inversely proportional
		// to their Q (geometric mean): the amplitude is scaled by the root back
		// to the spacing's Q
		modNorm = std::sqrt(std::sqrt(Q[0] * Q[1]) / Q_SPACING);
		carNorm = std::sqrt(std::sqrt(Q[2] * Q[3]) / Q_SPACING);
		tunedRate = sampleRate;
		for (int k = 0; k < 4; k++)
			tunedQ[k] = q[k];
	}

	// the envelope's time constant: 1.5 ms at the knob's minimum to 600 ms at its top
	static float knobToSeconds(float v) {
		return 0.0015f * std::pow(400.f, v / 0.25f);
	}

	void tuneEnvelope(float sampleRate) {
		float a = params[ATTACK_PARAM].getValue(), d = params[DECAY_PARAM].getValue();
		if (a == tunedAttack && d == tunedDecay && sampleRate == envRate)
			return;
		tunedAttack = a;
		tunedDecay = d;
		envRate = sampleRate;
		cAttack = 1.f - std::exp(-1.f / (knobToSeconds(a) * sampleRate));
		cDecay = 1.f - std::exp(-1.f / (knobToSeconds(d) * sampleRate));
	}

	ZINC() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		for (int i = 0; i < BANDS; i++) {
			configParam(BG_PARAM + i, 0.f, 2.f, 1.f, "Band " + std::to_string(i + 1) + " gain", " dB", -10, 20);
		}
		// the envelope follows with a time constant of 1.5 ms x 400^(knob / 0.25)
		configParam(ATTACK_PARAM, 0., 0.25, 0., "Attack time", " ms", 2.56e10f, 1.5f);
		configParam(DECAY_PARAM, 0., 0.25, 0., "Decay time", " ms", 2.56e10f, 1.5f);
		configParam(GMOD_PARAM, 1.f, 10.f, 1.f, "Modulator gain", " dB", -10.f, 20.f);
		configParam(GCARR_PARAM, 1.f, 10.f, 1.f, "Carrier gain", " dB", -10.f, 20.f);
		configParam(G_PARAM, 1.f, 10.f, 1.f, "Output gain", " dB", -10.f, 20.f);
		// 5: the bands' widths meet (Q 3); lower is wider, higher narrower
		configParam(Q1_PARAM, 1.f, 10.f, 5.f, "Modulator filter 1 Q", "", 0.f, Q_SPACING / 5.f);
		configParam(Q2_PARAM, 1.f, 10.f, 5.f, "Modulator filter 2 Q", "", 0.f, Q_SPACING / 5.f);
		configParam(Q3_PARAM, 1.f, 10.f, 5.f, "Carrier filter 1 Q", "", 0.f, Q_SPACING / 5.f);
		configParam(Q4_PARAM, 1.f, 10.f, 5.f, "Carrier filter 2 Q", "", 0.f, Q_SPACING / 5.f);
		configInput(IN_MOD, "Modulator");
		configInput(IN_CARR, "Carrier");
		configOutput(OUT, "Audio");
		configBypass(IN_CARR, OUT);

		for (int i = 0; i < BANDS; i++)
			freq[i / 4][i % 4] = 100.f * std::pow(100.f, (float)i / 14.f);
		// filters tuned on the first process(), once the sample rate is known
	}

	void process(const ProcessArgs &args) override {
		if (!inputs[IN_MOD].isConnected() && !inputs[IN_CARR].isConnected()) {
			// idle: where the envelopes would have decayed to
			for (int i = 0; i < BANDS4; i++)
				mem[i] = 0.f;
			outputs[OUT].setVoltage(0.f);
			outputs[OUT].setChannels(1);
			return;
		}
		// a polyphonic modulator is analysed as its sum (the mix you would hear); mono is unchanged
		float inM = inputs[IN_MOD].getVoltageSum() / 5.0f;
		const int channels = std::max(1, inputs[IN_CARR].getChannels());
		// carrier channels that come back start from silence
		for (int c = carrierChannels; c < channels; c++)
			for (int i = 0; i < BANDS2; i++)
				cState[c][i] = ZBiquadState();
		carrierChannels = channels;
		tune(args.sampleRate);
		tuneEnvelope(args.sampleRate);
		float gMod = params[GMOD_PARAM].getValue() * modNorm;
		float gCarr = params[GCARR_PARAM].getValue() * carNorm;
		float gOut = params[G_PARAM].getValue();

		float_4 bgs[BANDS4];
		for (int i = 0; i < BANDS4; i++) {
			float_4 coeff = mem[i];
			float_4 peak = abs(iState[i + BANDS4].process(iFilter[i + BANDS4], iState[i].process(iFilter[i], inM * gMod)));
			// the envelope: a one-pole, its attack or its decay
			float_4 c = simd::ifelse(peak > coeff, float_4(cAttack), float_4(cDecay));
			coeff += c * (peak - coeff);
			mem[i] = coeff;
			bgs[i] = {params[BG_PARAM + i*4].getValue(), params[BG_PARAM + i*4+1].getValue(), params[BG_PARAM + i*4+2].getValue(), params[BG_PARAM + i*4+3].getValue()};
		}

		for (int ch = 0; ch < channels; ch++) {
			float inC = inputs[IN_CARR].getVoltage(ch) / 5.0f;
			float out = 0.0f;
			ZBiquadState *st = cState[ch];
			for (int i = 0; i < BANDS4; i++) {
				float_4 corr = st[i + BANDS4].process(cFilter[i + BANDS4], st[i].process(cFilter[i], inC * gCarr)) * mem[i] * bgs[i];
				out += corr[0]+corr[1]+corr[2]+corr[3];
			}
			outputs[OUT].setVoltage(bidooSoftLimit(out * 5.0f * gOut), ch);
		}
		outputs[OUT].setChannels(channels);
	}
};

struct ZINCDisplay : TransparentWidget {
	ZINC *module;
	BidooWidget *panel = nullptr;   // its shown theme: the band frequencies are panel text

	ZINCDisplay() {

	}

	// The frequencies under the knobs are printed on the panel: drawn as the panel is (dimmed with the room light), in
	// the panel text's colour of the theme shown (black, the light grey Dark and Black turn it into)
	void draw(const DrawArgs& args) override {
		{
			const bool light = !panel || panel->shownTheme <= THEME_LIGHT;
			const NVGcolor text = light ? theme::kPanelText : nvgRGB(180, 180, 180);
			nvgFontSize(args.vg, 12);
			nvgStrokeWidth(args.vg, 2);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);
			static const int portX0[4] = { 20, 63, 106, 149 };
			if (module) {
				for (int i = 0; i < BANDS; i++) {
					char fVal[10];
					float f = module->freq[i/4][i%4];
					if (f >= 1000.f)
						snprintf(fVal, sizeof(fVal), "%.1fk", f / 1000.f);
					else
						snprintf(fVal, sizeof(fVal), "%1i", (int)f);
					nvgFillColor(args.vg, text);
					nvgText(args.vg, portX0[i % (BANDS / 4)], 23 + 45 * (int)(i / 4), fVal, NULL);
				}
			}
		}
		Widget::draw(args);
	}

};


struct BidooziNCColoredKnob : RoundKnob {
	NSVGshape *tShape = NULL;
	NSVGshape *tShapeInterior = NULL;
	int index = 0;

	BidooziNCColoredKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/ziNCBlueKnobBidoo-bg.svg")));
		// its own copy of the blue knob: draw() recolours the cached SVG, which BidooBlueKnob must not share
		shadow->opacity = 0.0f;
		box.size = Vec(28, 28);

		if (bg->svg && bg->svg->handle && bg->svg->handle->shapes) {
			for (NSVGshape *shape = bg->svg->handle->shapes; shape != NULL; shape = shape->next) {
				std::string str(shape->id);
				if (str == "bidooKnob") {
					tShape = shape;
				}
				if (str == "bidooInterior") {
					tShapeInterior = shape;
				}
			}
		}
	}

	// The tint shows the band's envelope (what multiplies the carrier's band), in dB: from kFloorDb (the plain blue)
	// to kTopDb (the red of the output boxes). Measured with the default Q and times: pink noise peaking at 5 V gives
	// about -32 dB a band, a 110 Hz saw at 5 V -10 dB in its lowest band, a 5 V sine on a band's centre -4 dB.
	static constexpr float kFloorDb = -48.f, kTopDb = -6.f;
	static constexpr int kSteps = 255;
	float level = 0.f;   // 0..kSteps

	// render again only when the tint moved by a step
	void step() override {
		if (getParamQuantity() && getParamQuantity()->module) {
			ZINC* zinc = dynamic_cast<ZINC*>(getParamQuantity()->module);
			const int band = getParamQuantity()->paramId - ZINC::BG_PARAM;
			const float env = zinc->mem[band / 4][band % 4];
			const float db = 20.f * std::log10(std::max(env, 1e-6f));
			const float l = std::round(clamp((db - kFloorDb) / (kTopDb - kFloorDb), 0.f, 1.f) * kSteps);
			if (l != level) {
				level = l;
				fb->dirty = true;
			}
		}
		RoundKnob::step();
	}

	void draw(const DrawArgs& args) override {
		// the 16 knobs share the SVG: set this knob's colour just before its framebuffer renders.
		// From the knob's blue (42, 87, 117) to the output boxes' red (205, 31, 0), straight across
		const float t = level / kSteps;
		const unsigned r = (unsigned)std::round(42.f + t * (205.f - 42.f));
		const unsigned g = (unsigned)std::round(87.f + t * (31.f - 87.f));
		const unsigned b = (unsigned)std::round(117.f + t * (0.f - 117.f));
		const unsigned colour = r | (g << 8) | (b << 16) | (255u << 24);
		if (tShape)
			tShape->fill.color = colour;
		if (tShapeInterior)
			tShapeInterior->fill.color = colour;
		RoundKnob::draw(args);
	}

};

struct ZINCWidget : BidooWidget {

	ZINCWidget(ZINC *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/ZINC.svg"));

		theme::addScrews(this);

		// The original hand-drawn panel: a 4 x 4 grid of band knobs (no labels), six trimpots under their labels, the
		// MOD, CAR and GAIN knobs, three boxed jacks. Old corners kept, except the CAR and GAIN knobs, now under their
		// labels (the old code had them 0.4 and 0.7 px right), and the jacks on their boxes' centres (up to 0.65 px off).
		ZINCDisplay *display = new ZINCDisplay();
		display->module = module;
		display->panel = this;
		display->box = theme::specificRect(14, 14, 110, 70);
		addChild(display);

		const theme::Size kBandKnobSize = {28.f, 28.f};   // BidooziNCColoredKnob sets its own box
		const float kBandRight[4] = {0.f, 43.f, 86.f, 129.f};
		const float kBandBelow = 45.f;
		const theme::Pos firstBand = theme::specificCorner(19, 41, kBandKnobSize);
		for (int i = 0; i < BANDS; i++) {
			addParam(theme::param<BidooziNCColoredKnob>(firstBand.offset(kBandRight[i % (BANDS / 4)], kBandBelow * (i / 4)), module, ZINC::BG_PARAM + i));
		}

		const float kTrimpotRight = 32.f;
		const theme::Pos attack = theme::specificCorner(8, 230, theme::kTrimpotSize);
		const int trimpotIds[6] = {ZINC::ATTACK_PARAM, ZINC::DECAY_PARAM, ZINC::Q1_PARAM, ZINC::Q2_PARAM, ZINC::Q3_PARAM, ZINC::Q4_PARAM};
		for (int i = 0; i < 6; i++)
			addParam(theme::param<BidooBlueTrimpot>(attack.right(kTrimpotRight * i), module, trimpotIds[i]));

		const theme::Pos gainRow = theme::specificCorner(41, 268, theme::kBlueKnobSize);
		addParam(theme::param<BidooBlueKnob>(gainRow, module, ZINC::GMOD_PARAM));
		addParam(theme::param<BidooBlueKnob>(theme::colRow(theme::specific(97.375f, 0), gainRow), module, ZINC::GCARR_PARAM));   // under CAR
		addParam(theme::param<BidooBlueKnob>(theme::colRow(theme::specific(139.094f, 0), gainRow), module, ZINC::G_PARAM));      // under GAIN

		const theme::Pos jackRow = theme::specificCorner(7, 330, theme::kJackSize);
		addInput(theme::input<PJ301MPort>(theme::colRow(theme::specific(19.156f, 0), jackRow), module, ZINC::IN_MOD));     // the boxes' centres
		addInput(theme::input<PJ301MPort>(theme::colRow(theme::specific(97.5f, 0), jackRow), module, ZINC::IN_CARR));
		addOutput(theme::output<PJ301MPort>(theme::colRow(theme::specific(175.844f, 0), jackRow), module, ZINC::OUT));
	}

};

Model *modelZINC = createModel<ZINC, ZINCWidget>("ziNC");
