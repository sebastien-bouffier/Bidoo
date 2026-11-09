#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "Svf.hpp"

using namespace std;

#define BANDS 16
#define BANDS2 8
#define BANDS4 4
using simd::float_4;

class ZBiquad {
public:
    ZBiquad();
    ZBiquad(float_4 Fc, float_4 Q, float_4 peakGainDB);
    ~ZBiquad();
    void setQ(float_4 Q);
    void setFc(float_4 Fc);
    void setPeakGain(float_4 peakGainDB);
    void setBiquad(float_4 Fc, float_4 Q, float_4 peakGain);
    float_4 process(float_4 in);

protected:
    void calcBiquad(void);

    float_4 a0, a1, a2, b1, b2;
    float_4 Fc, Q, peakGain;
    float_4 z1, z2;
};

inline float_4 ZBiquad::process(float_4 in) {
    float_4 out = in * a0 + z1;
    z1 = in * a1 + z2 - b1 * out;
    z2 = in * a2 - b2 * out;
    return out;
}

ZBiquad::ZBiquad() {
    a0 = 1.0;
    a1 = a2 = b1 = b2 = 0.0;
    Fc = 0.50;
    Q = 0.707;
    peakGain = 0.0;
    z1 = z2 = 0.0;
}

ZBiquad::ZBiquad(float_4 Fc, float_4 Q, float_4 peakGainDB) {
    setBiquad(Fc, Q, peakGainDB);
    z1 = z2 = 0.0;
}

ZBiquad::~ZBiquad() {
}

void ZBiquad::setQ(float_4 Q) {
    this->Q = Q;
    calcBiquad();
}

void ZBiquad::setFc(float_4 Fc) {
    this->Fc = Fc;
    calcBiquad();
}

void ZBiquad::setPeakGain(float_4 peakGainDB) {
    this->peakGain = peakGainDB;
    calcBiquad();
}

void ZBiquad::setBiquad(float_4 Fc, float_4 Q, float_4 peakGainDB) {
    this->Q = Q;
    this->Fc = Fc;
    setPeakGain(peakGainDB);
}

void ZBiquad::calcBiquad(void) {
    float_4 norm;
    float_4 K = tan(M_PI * Fc);
    norm = 1 / (1 + K / Q + K * K);
    a0 = K / Q * norm;
    a1 = 0;
    a2 = -a0;
    b1 = 2 * (K * K - 1) * norm;
    b2 = (1 - K / Q + K * K) * norm;
    return;
}

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

	ZBiquad* iFilter[BANDS2];
	ZBiquad* cFilter[BANDS2];
	float_4 mem[BANDS4] = { 0.0f };
	// 15 bands spread evenly on a log scale from 100 Hz to 10 kHz (0.47 octave
	// apart), and a 16th for the air above them (13.9 kHz, the next step)
	float_4 freq[BANDS4];
	float_4 peaks[BANDS4] = { 0.0f };
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
			iFilter[i]->setBiquad(fc, Q[0], 0.f);
			iFilter[i + BANDS4]->setBiquad(fc, Q[1], 0.f);
			cFilter[i]->setBiquad(fc, Q[2], 0.f);
			cFilter[i + BANDS4]->setBiquad(fc, Q[3], 0.f);
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

		for (int i = 0; i < BANDS; i++)
			freq[i / 4][i % 4] = 100.f * std::pow(100.f, (float)i / 14.f);

		// tuned on the first process(), once the sample rate is known
		for (int i = 0; i < BANDS2; i++) {
			iFilter[i] = new ZBiquad();
			cFilter[i] = new ZBiquad();
		}
	}

	~ZINC() override {
		for (int i = 0; i < BANDS2; i++) {
			delete iFilter[i];
			delete cFilter[i];
		}
	}

	void process(const ProcessArgs &args) override {
		float inM = inputs[IN_MOD].getVoltage() / 5.0f;
		float inC = inputs[IN_CARR].getVoltage() / 5.0f;
		float out = 0.0f;
		tune(args.sampleRate);
		tuneEnvelope(args.sampleRate);
		float gMod = params[GMOD_PARAM].getValue() * modNorm;
		float gCarr = params[GCARR_PARAM].getValue() * carNorm;

		for (int i = 0; i < BANDS4; i++) {
			float_4 coeff = mem[i];
			float_4 peak = abs(iFilter[i + BANDS4]->process(iFilter[i]->process(inM * gMod)));
			// the envelope: a one-pole, its attack or its decay
			float_4 c = simd::ifelse(peak > coeff, float_4(cAttack), float_4(cDecay));
			coeff += c * (peak - coeff);
			peaks[i] = peak;
			mem[i] = coeff;
			float_4 bg = {params[BG_PARAM + i*4].getValue(), params[BG_PARAM + i*4+1].getValue(), params[BG_PARAM + i*4+2].getValue(), params[BG_PARAM + i*4+3].getValue()};
			float_4 corr = cFilter[i + BANDS4]->process(cFilter[i]->process(inC * gCarr)) * coeff * bg;
			out += corr[0]+corr[1]+corr[2]+corr[3];
		}
		outputs[OUT].setVoltage(bidooSoftLimit(out * 5.0f * params[G_PARAM].getValue()));
	}
};

struct ZINCDisplay : TransparentWidget {
	ZINC *module;

	ZINCDisplay() {

	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
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
					nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 255));
					nvgText(args.vg, portX0[i % (BANDS / 4)], 23 + 45 * (int)(i / 4), fVal, NULL);
				}
			}
		}
		Widget::drawLayer(args, layer);
	}

};


struct BidooziNCColoredKnob : RoundKnob {
	float corrCoef = 0;
	NSVGshape *tShape = NULL;
	NSVGshape *tShapeInterior = NULL;
	int index = 0;

	BidooziNCColoredKnob() {
		setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueKnobBidoo.svg")));
		bg->setSvg(Svg::load(asset::plugin(pluginInstance,"res/ComponentLibrary/BlueKnobBidoo-bg.svg")));
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

	void draw(const DrawArgs& args) override {
		if (getParamQuantity() && getParamQuantity()->module) {
			ZINC* zinc = dynamic_cast<ZINC*>(getParamQuantity()->module);
			corrCoef = rescale(clamp(zinc->peaks[(getParamQuantity()->paramId-ZINC::BG_PARAM)/4][(getParamQuantity()->paramId-ZINC::BG_PARAM)%4],0.f,2.f),0.f,2.f,0.f,255.f);
		}

		if (tShape) {
			tShape->fill.color = (((unsigned int)clamp(42.f+corrCoef,0.f,255.f)) | ((unsigned int)clamp(87.f-corrCoef,0.f,255.f) << 8) | ((unsigned int)clamp(117.f-corrCoef,0.f,255.f) << 16));
			tShape->fill.color |= (unsigned int)(255) << 24;
		}

		if (tShapeInterior) {
			tShapeInterior->fill.color = (((unsigned int)clamp(42.f+corrCoef,0.f,255.f)) | ((unsigned int)clamp(87.f-corrCoef,0.f,255.f) << 8) | ((unsigned int)clamp(117.f-corrCoef,0.f,255.f) << 16));
			tShapeInterior->fill.color |= (unsigned int)(255) << 24;
		}
		RoundKnob::draw(args);
	}

};

struct ZINCWidget : BidooWidget {
	ParamWidget *controls[16];

	ZINCWidget(ZINC *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/ZINC.svg"));

		theme::addScrews(this);

		// The original hand-drawn panel: a 4 x 4 grid of band knobs (no labels), six trimpots under their labels, the
		// MOD, CAR and GAIN knobs, three boxed jacks. Old corners kept, except the CAR and GAIN knobs, now under their
		// labels (the old code had them 0.4 and 0.7 px right), and the jacks on their boxes' centres (up to 0.65 px off).
		ZINCDisplay *display = new ZINCDisplay();
		display->module = module;
		display->box = theme::specificRect(14, 14, 110, 70);
		addChild(display);

		const theme::Size kBandKnobSize = {28.f, 28.f};   // BidooziNCColoredKnob sets its own box
		const float kBandRight[4] = {0.f, 43.f, 86.f, 129.f};
		const float kBandBelow = 45.f;
		const theme::Pos firstBand = theme::specificCorner(19, 41, kBandKnobSize);
		for (int i = 0; i < BANDS; i++) {
			controls[i] = theme::param<BidooziNCColoredKnob>(firstBand.offset(kBandRight[i % (BANDS / 4)], kBandBelow * (i / 4)), module, ZINC::BG_PARAM + i);
			addParam(controls[i]);
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

	void step() override;
};

void ZINCWidget::step() {
	for (int i = 0; i < BANDS; i++) {
			BidooziNCColoredKnob* knob = dynamic_cast<BidooziNCColoredKnob*>(controls[i]);
			knob->fb->dirty = true;
	}
	BidooWidget::step();
}

Model *modelZINC = createModel<ZINC, ZINCWidget>("ziNC");
