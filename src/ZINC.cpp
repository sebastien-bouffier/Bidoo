#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/resampler.hpp"

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
	float_4 freq[BANDS4] = { {125.0f, 185.0f, 270.0f, 350.0f}, {430.0f, 530.0f, 630.0f, 780.0f},
						  {950.0f, 1150.0f, 1380.0f, 1680.0f}, {2070.0f, 2780.0f, 3800.0f, 6400.0f} };
	float_4 peaks[BANDS4] = { 0.0f };
	const float slewMin = 0.001f;
	const float slewMax = 500.0f;
	const float shapeScale = 0.1f;

	ZINC() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		for (int i = 0; i < BANDS; i++) {
			configParam(BG_PARAM + i, 0.f, 2.f, 1.f, "Band " + std::to_string(i + 1) + " Gain", "dB", -10, 40);//gets cast to int
		}
		configParam(ATTACK_PARAM, 0., 0.25, 0., "Attack Time", "s", params[ATTACK_PARAM].getValue(), 1000.f);
		configParam(DECAY_PARAM, 0., 0.25, 0., "Decay Time", "s", params[DECAY_PARAM].getValue(), 1000.f);
		configParam(GMOD_PARAM, 1.f, 10.f, 1.f, "Modifier", "%", 0.f, 10.f);
		configParam(GCARR_PARAM, 1.f, 10.f, 1.f, "Carrier", "%", 0.f, 10.f);
		configParam(G_PARAM, 1.f, 10.f, 1.f, "Output Gain", "%", 0.f, 10.f);
		configParam(Q1_PARAM, 1.f, 10.f, 5.f, "Q", "dB", 0.f, 1.f);
		configParam(Q2_PARAM, 1.f, 10.f, 5.f, "Q", "dB", 0.f, 1.f);
		configParam(Q3_PARAM, 1.f, 10.f, 5.f, "Q", "dB", 0.f, 1.f);
		configParam(Q4_PARAM, 1.f, 10.f, 5.f, "Q", "dB", 0.f, 1.f);

		for (int i = 0; i < BANDS2; i++) {
			iFilter[i] = new ZBiquad(freq[i%4] / APP->engine->getSampleRate(), {5.0f}, {6.0f});
			cFilter[i] = new ZBiquad(freq[i%4] / APP->engine->getSampleRate(), {5.0f}, {6.0f});
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
		float attack = params[ATTACK_PARAM].getValue();
		float decay = params[DECAY_PARAM].getValue();
		float slewAttack = slewMax * powf(slewMin / slewMax, attack);
		float slewDecay = slewMax * powf(slewMin / slewMax, decay);
		float_4 q1 = {params[Q1_PARAM].getValue()};
		float_4 q2 = {params[Q1_PARAM].getValue()};
		float_4 q3 = {params[Q1_PARAM].getValue()};
		float_4 q4 = {params[Q1_PARAM].getValue()};
		float out = 0.0f;

		for (int i = 0; i < BANDS4; i++) {
			float_4 coeff = mem[i];
			iFilter[i]->setQ(q1);
			iFilter[i + BANDS4]->setQ(q2);
			float_4 peak = abs(iFilter[i + BANDS4]->process(iFilter[i]->process(inM * params[GMOD_PARAM].getValue())));
			for (int j=0; j<4; j++) {
				if (peak[j] > coeff[j]) {
					coeff[j] += slewAttack * shapeScale * (peak[j] - coeff[j]) / args.sampleRate;
					if (coeff[j] > peak[j])
						coeff[j] = peak[j];
				} else if (peak[j] < coeff[j]) {
					coeff[j] -= slewDecay * shapeScale * (coeff[j] - peak[j]) / args.sampleRate;
					if (coeff[j] < peak[j])
						coeff[j] = peak[j];
				}
			}
			peaks[i] = peak;
			mem[i] = coeff;
			cFilter[i]->setQ(q3);
			cFilter[i + BANDS4]->setQ(q4);
			float_4 bg = {params[BG_PARAM + i*4].getValue(), params[BG_PARAM + i*4+1].getValue(), params[BG_PARAM + i*4+2].getValue(), params[BG_PARAM + i*4+3].getValue()};
			float_4 corr = cFilter[i + BANDS4]->process(cFilter[i]->process(inC*params[GCARR_PARAM].getValue())) * coeff * bg;
			out += corr[0]+corr[1]+corr[2]+corr[3];
		}
		outputs[OUT].setVoltage(out * 5.0f * params[G_PARAM].getValue());
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
					snprintf(fVal, sizeof(fVal), "%1i", (int)module->freq[i/4][i%4]);
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

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		static const float portX0[4] = { 20, 63, 106, 149 };

		{
			ZINCDisplay *display = new ZINCDisplay();
			display->module = module;
			display->box.pos = Vec(14, 14);
			display->box.size = Vec(110, 70);
			addChild(display);
		}

		for (int i = 0; i < BANDS; i++) {
			controls[i]=createParam<BidooziNCColoredKnob>(Vec(portX0[i%(BANDS/4)]-1, 41+45*(int)(i/4)), module, ZINC::BG_PARAM + i);
			addParam(controls[i]);
		}

		const float xAnchor = 8.0f;
		const float xOffset = 32.0f;

		addParam(createParam<BidooBlueTrimpot>(Vec(xAnchor, 230), module, ZINC::ATTACK_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(xAnchor+ xOffset, 230), module, ZINC::DECAY_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(xAnchor+ 2*xOffset, 230), module, ZINC::Q1_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(xAnchor+ 3*xOffset, 230), module, ZINC::Q2_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(xAnchor+ 4*xOffset, 230), module, ZINC::Q3_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(xAnchor+ 5*xOffset, 230), module, ZINC::Q4_PARAM));


		addParam(createParam<BidooBlueKnob>(Vec(portX0[0] + 21, 268), module, ZINC::GMOD_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[1] + 20, 268), module, ZINC::GCARR_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX0[2] + 19, 268), module, ZINC::G_PARAM));

		addInput(createInput<PJ301MPort>(Vec(7.f, 330), module, ZINC::IN_MOD));
		addInput(createInput<PJ301MPort>(Vec(85.f, 330), module, ZINC::IN_CARR));
		addOutput(createOutput<PJ301MPort>(Vec(164.5f, 330), module, ZINC::OUT));
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
