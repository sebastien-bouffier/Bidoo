#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/ringbuffer.hpp"
#include "dsp/digital.hpp"
#include "dsp/window.hpp"
#include <vector>
#include <algorithm>

using namespace std;

const int NbGraines = 200;
const int LongueurMax = 5000;

inline void tukey (float alpha, float* x, int len) {
  float s = 0.5f*alpha*(len-1);

	for (int i = 0; i < len; i++) {
		if( i <= s ) {
	    x[i] *= 0.5f*(1+simd::cos(M_PI*(i/s - 1)));
	  } else if ( i >= (len-1)*(1-0.5f*alpha) ) {
	    x[i] *= 0.5f*(1+simd::cos(M_PI*(i/s - 2/alpha + 1)));
	  }
	}
}

inline void welch (float* x, int len) {
  float s = 0.5f*(len-1);
	for (int i = 0; i < len; i++) {
		float f = (i - s)/s;
		x[i] *= 1 - f*f;
	}
}

inline float hermit(const float* p, float x, int size) {
	int xi = x;
	float xf = x - xi;
	if ((xi<1) && (xi>size-4)) {
		return crossfade(p[xi], p[xi + 1], xf);
	}
	else {
		float c0 = p[xi];
		float c1 = 0.5f*(p[xi+1]-p[xi-1]);
		float c2 = p[xi-1] - 2.5f*p[xi] + 2*p[xi+1] - 0.5f*p[xi+2];
		float c3 = 0.5f*(p[xi+2]-p[xi-1]) + 1.5f*(p[xi]-p[xi+1]);
		return ((c3*xf+c2)*xf+c1)*xf+c0;
	}
}



struct GRAINE {
	int status = 0;
	float donnees[LongueurMax];
	float teteLecture = 0;
	int teteEcriture = 0;
	float volume[LongueurMax];
	int longueur = 0;
	int dureeGermination = 0;

	void init(int taille, int type, float attack, int duree) {
		longueur = taille;
		std::fill_n(donnees, longueur, 0);
		std::fill_n(volume, longueur, 1);
		if (type == 0) {
			welch(volume,longueur);
		}
		else if (type == 1) {
			tukey(attack,volume,longueur);
		}
		else if (type == 2) {
			rack::dsp::hannWindow(volume,longueur);
		}
		else if (type == 3) {
			rack::dsp::blackmanWindow(attack,volume,longueur);
		}
		else if (type == 4) {
			rack::dsp::blackmanNuttallWindow(volume,longueur);
		}
		else {
			rack::dsp::blackmanHarrisWindow(volume,longueur);
		}
		teteLecture = 0;
		teteEcriture = 0;
		dureeGermination = max(longueur,duree);
		status = 1;
	}

	void ecrit(float valeur) {
		if (teteEcriture<longueur) {
			donnees[teteEcriture] = valeur;
			teteEcriture++;
		}
		if (teteEcriture==longueur) {
			status = 2;
		}
	}

	float ecoute() {
		if (status == 3) {
			return hermit(donnees, teteLecture, longueur)*hermit(volume, teteLecture, longueur);
		}
		else {
			return 0.0f;
		}
	}

};

struct PAYSAN {
	GRAINE graines[NbGraines];
	int pasRecolte = 0;
	int pasSeme = 0;
	int indexRecolte = 0;
	int indexSeme = 0;

	void recolte(float valeur, int distance, int taille, int type, float attack, int dureeGermination) {
		if ((pasRecolte <= 0) && (graines[indexRecolte].status == 0)) {
			graines[indexRecolte].init(taille, type, attack, dureeGermination);
			indexRecolte = (indexRecolte+1)%NbGraines;
			pasRecolte = distance;
		}

		for (int i = 0; i < NbGraines; i++) {
			if (graines[i].status == 1)	graines[i].ecrit(valeur);
		}
		pasRecolte--;
	}

	void seme (int distance) {
		if (pasSeme <= 0 && (graines[indexSeme].status == 2)) {
			graines[indexSeme].status = 3;
			indexSeme = (indexSeme+1)%NbGraines;
			pasSeme = distance;
		}
		pasSeme--;
	}

	float felibre(float vitesse) {
		int count = 0;
		float result = 0.0f;
		for (int i = 0; i < NbGraines; i++) {
			if (graines[i].status == 3) {
				result += graines[i].ecoute();
				graines[i].teteLecture+=vitesse;
				graines[i].dureeGermination--;
				if (graines[i].teteLecture>=graines[i].longueur-1) {
					if (graines[i].dureeGermination<=0) {
						graines[i].status = 0;
					}
					else {
						graines[i].teteLecture=0;
					}
				}
				count++;
			}
		}
		return result/max(count,1);
	}
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

	PAYSAN paysan;

	int hopsizeAnalysis=0;
	int hopsizeSynthesis=0;
	size_t grainSize = 200;
	float pitch = 1.0f;

	SPORE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PITCH_PARAM, 0.5f, 2.0f, 1.0f, "Pitch");

		configParam(GRAINSIZE_PARAM, 20, LongueurMax, 500, "Grain Size");
		configParam(HOPSIZEANALYSIS_PARAM, 10, (float)LongueurMax*2.0f, 200, "Analysis hopsize");
		configParam(HOPSIZESYNTHESIS_PARAM, 10, (float)LongueurMax*2.0f, 200, "Synthesis hopsize");

		configParam(WINDOW_PARAM, 0, 5, 0, "Window type");
		configParam(ATTACKRELEASE_PARAM, 0, 1, 0.5f, "A/R window");
	}

	~SPORE() {

	}

	void process(const ProcessArgs &args) override {

		grainSize = clamp(params[GRAINSIZE_PARAM].getValue() + rescale(inputs[GRAINSIZE_INPUT].getVoltage(),0.0f,10.0f,100.0f,5000.0f),20.0f,(float)LongueurMax);
		hopsizeAnalysis = clamp(params[HOPSIZEANALYSIS_PARAM].getValue() + rescale(inputs[HOPSIZEANALYSIS_INPUT].getVoltage(),0.0f,10.0f,0.0f,(float)LongueurMax*2.0f),10.0f,(float)LongueurMax*2.0f);
		hopsizeSynthesis = clamp(params[HOPSIZESYNTHESIS_PARAM].getValue() + rescale(inputs[HOPSIZESYNTHESIS_INPUT].getVoltage(),0.0f,10.0f,0.0f,(float)LongueurMax*2.0f),10.0f,(float)LongueurMax*2.0f);
		pitch = clamp(params[PITCH_PARAM].getValue() + rescale(inputs[PITCH_INPUT].getVoltage(),0.0f,10.0f,0.0f,2.0f),0.5f,2.0f);

		paysan.recolte(inputs[INPUT].getVoltage(), hopsizeAnalysis, grainSize, params[WINDOW_PARAM].getValue(), params[ATTACKRELEASE_PARAM].getValue(),hopsizeSynthesis);
		paysan.seme(hopsizeSynthesis);

		outputs[OUTPUT].setVoltage(paysan.felibre(pitch));
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
