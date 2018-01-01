#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/samplerate.hpp"

using namespace std;

//Approximates cos(pi*x) for x in [-1,1].
inline float fast_cos(const float x)
{
  float x2=x*x;
  return 1+x2*(-4+2*x2);
}
//Length of the table
#define L_TABLE (256+1) //The last entry of the table equals the first (to avoid a modulo)
//Maximal formant width
#define I_MAX 64
//Table of formants
float TF[L_TABLE*I_MAX];
//Formantic function of width I (used to fill the table of formants)
float fonc_formant(float p,const float I)
{
  float a=0.5f;
  int hmax=int(10*I)>L_TABLE/2?L_TABLE/2:int(10*I);
  float phi=0.0f;
  for(int h=1;h<hmax;h++)
  {
    phi+=3.14159265359f*p;
    float hann=0.5f+0.5f*fast_cos(h*(1.0f/hmax));
    float gaussienne=0.85f*exp(-h*h/(I*I));
    float jupe=0.15f;
    float harmonique=cosf(phi);
    a+=hann*(gaussienne+jupe)*harmonique;
   }
  return a;
}
//Initialisation of the table TF with the fonction fonc_formant.
void init_formant(void)
{ float coef=2.0f/(L_TABLE-1);
  for(int I=0;I<I_MAX;I++)
    for(int P=0;P<L_TABLE;P++)
      TF[P+I*L_TABLE]=fonc_formant(-1+P*coef,float(I));
}
//This function emulates the function fonc_formant
// thanks to the table TF. A bilinear interpolation is
// performed
float formant(float p,float i)
{
  i=i<0?0:i>I_MAX-2?I_MAX-2:i;    // width limitation
 float P=(L_TABLE-1)*(p+1)*0.5f; // phase normalisation
 int P0=(int)P;  float fP=P-P0;  // Integer and fractional
 int I0=(int)i;  float fI=i-I0;  // parts of the phase (p) and width (i).
 int i00=P0+L_TABLE*I0;  int i10=i00+L_TABLE;
 //bilinear interpolation.
 return (1-fI)*(TF[i00] + fP*(TF[i00+1]-TF[i00]))
        +    fI*(TF[i10] + fP*(TF[i10+1]-TF[i10]));
}

// Double carrier.
// h : position (float harmonic number)
// p : phase
float porteuse(const float h,const float p)
{
  float h0=floor(h);  //integer and
  float hf=h-h0;      //decimal part of harmonic number.
  // modulos pour ramener p*h0 et p*(h0+1) dans [-1,1]
  float phi0=fmodf(p* h0   +1+1000,2.0f)-1.0f;
  float phi1=fmodf(p*(h0+1)+1+1000,2.0f)-1.0f;
  // two carriers.
  float Porteuse0=fast_cos(phi0);  float Porteuse1=fast_cos(phi1);
  // crossfade between the two carriers.
  return Porteuse0+hf*(Porteuse1-Porteuse0);
}

struct FORK : Module {
	enum ParamIds {
		FORMANT_TYPE_PARAM,
		PITCH_PARAM,
		PRESET_PARAM,
		F_PARAM,
		A_PARAM = F_PARAM + 4,
		NUM_PARAMS = A_PARAM + 4
	};
	enum InputIds {
		FORMANT_TYPE_INPUT,
		PITCH_INPUT,
		F_INPUT,
		A_INPUT = F_INPUT + 4,
		NUM_INPUTS = A_INPUT + 4
	};
	enum OutputIds {
		SIGNAL_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	float F1[9]={  730,  200,  400,  250,  190,  350,  550,  550,  450};
	float A1[9]={ 1.0f, 0.5f, 1.0f, 1.0f, 0.7f, 1.0f, 1.0f, 0.3f, 1.0f};
	float F2[9]={ 1090, 2100,  900, 1700,  800, 1900, 1600,  850, 1100};
	float A2[9]={ 2.0f, 0.5f, 0.7f, 0.7f,0.35f, 0.3f, 0.5f, 1.0f, 0.7f};
	float F3[9]={ 2440, 3100, 2300, 2100, 2000, 2500, 2250, 1900, 1500};
	float A3[9]={ 0.3f,0.15f, 0.2f, 0.4f, 0.1f, 0.3f, 0.7f, 0.2f, 0.2f};
	float F4[9]={ 3400, 4700, 3000, 3300, 3400, 3700, 3200, 3000, 3000};
	float A4[9]={ 0.2f, 0.1f, 0.2f, 0.3f, 0.1f, 0.1f, 0.3f, 0.2f, 0.3f};
	int preset=0;
	float f0,dp0,p0=0.0f;
	float f1,f2,f3,f4,a1,a2,a3,a4;

	FORK() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
		init_formant();
		f1=f2=f3=f4=100.0f;a1=a2=a3=a4=0.0f;
	}

	void step() override;
};


void FORK::step() {
	f0=261.626 * powf(2.0, clampf(params[PITCH_PARAM].value + 12 * inputs[PITCH_INPUT].value,-54,54) / 12.0);
	dp0=f0*(2/engineGetSampleRate());
	float un_f0=1.0f/f0;
	p0+=dp0;
	p0-=2*(p0>1);
	{
		float r=0.001f;
		f1+=r*(clampf(params[F_PARAM].value + rescalef(inputs[F_INPUT].value,0,10,190,730),190,730)-f1);
		f2+=r*(clampf(params[F_PARAM+1].value + rescalef(inputs[F_INPUT+1].value,00,10,800,2100),800,2100)-f2);
		f3+=r*(clampf(params[F_PARAM+2].value + rescalef(inputs[F_INPUT+2].value,0,10,1500,3100),1500,3100)-f3);
		f4+=r*(clampf(params[F_PARAM+3].value + rescalef(inputs[F_INPUT+3].value,0,10,3000,4700),3000,4700)-f4);
		a1+=r*(clampf(params[A_PARAM].value + rescalef(inputs[A_INPUT].value,0,10,0.0f,1.0f),0.0f,1.0f)-a1);
		a2+=r*(clampf(params[A_PARAM+1].value + rescalef(inputs[A_INPUT+1].value,0,10,0.0f,2.0f),0.0f,2.0f)-a2);
		a3+=r*(clampf(params[A_PARAM+2].value + rescalef(inputs[A_INPUT+2].value,0,10,0.0f,0.7f),0.0f,0.7f)-a3);
		a4+=r*(clampf(params[A_PARAM+3].value + rescalef(inputs[A_INPUT+3].value,0,10,0.0f,0.3f),0.0f,0.3f)-a4);
	}

	float out=
				 a1*(f0/f1)*formant(p0,100*un_f0)*porteuse(f1*un_f0,p0)
	 +0.7f*a2*(f0/f2)*formant(p0,120*un_f0)*porteuse(f2*un_f0,p0)
	 +     a3*(f0/f3)*formant(p0,150*un_f0)*porteuse(f3*un_f0,p0)
	 +     a4*(f0/f4)*formant(p0,300*un_f0)*porteuse(f4*un_f0,p0);
	outputs[SIGNAL_OUTPUT].value =10.0f*out;
}

struct FORKCKD6 : CKD6 {
	void onMouseDown(EventMouseDown &e) override {
		FORKWidget *parent = dynamic_cast<FORKWidget*>(this->parent);
		FORK *module = dynamic_cast<FORK*>(this->module);
		if (parent && module) {
			module->preset = (module->preset + 1)%8;
			parent->F1->setValue(module->F1[module->preset]);
			parent->F2->setValue(module->F2[module->preset]);
			parent->F3->setValue(module->F3[module->preset]);
			parent->F4->setValue(module->F4[module->preset]);
			parent->A1->setValue(module->A1[module->preset]);
			parent->A2->setValue(module->A2[module->preset]);
			parent->A3->setValue(module->A3[module->preset]);
			parent->A4->setValue(module->A4[module->preset]);
		}
		CKD6::onMouseDown(e);
	}
};

FORKWidget::FORKWidget() {
	FORK *module = new FORK();
	setModule(module);
	box.size = Vec(15*6, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/FORK.svg")));
		addChild(panel);
	}

	addParam(createParam<RoundBlackKnob>(Vec(26,40), module, FORK::PITCH_PARAM, -54, 54, 0));
	addParam(createParam<FORKCKD6>(Vec(30,274), module, FORK::PRESET_PARAM, 0.0, 8, 0.0));

	int xtpots = 34;
	int xtinyports = 67;

	F1 = createParam<Trimpot>(Vec(xtpots,35 + 25 * 2), module, FORK::F_PARAM, 190, 730, 190);
	addParam(F1);
	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 + 25 * 2 + 2), module, FORK::F_INPUT));
	A1 = createParam<Trimpot>(Vec(xtpots,35 + 25 * 2 + 20), module, FORK::A_PARAM, 0.0f, 1.0f, 1.0f);
	addParam(A1);
	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 + 25 * 2 + 20 + 2), module, FORK::A_INPUT));

	F2 = createParam<Trimpot>(Vec(xtpots,35 - 2 + 25 * 4), module, FORK::F_PARAM + 1, 800, 2100, 1090);
	addParam(F2);
	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 - 2 + 25 * 4 + 2), module, FORK::F_INPUT + 1));
	A2 = createParam<Trimpot>(Vec(xtpots,35 - 2 + 25 * 4 + 20), module, FORK::A_PARAM + 1, 0.0f, 2.0f, 1.0f);
	addParam(A2);
	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 - 2 + 25 * 4 + 20 + 2), module, FORK::A_INPUT + 1));

	F3 = createParam<Trimpot>(Vec(xtpots,35 - 4 + 25 * 6), module, FORK::F_PARAM + 2, 1500, 3100, 2440);
	addParam(F3);
	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 - 4 + 25 * 6 + 2), module, FORK::F_INPUT + 2));
	A3 = createParam<Trimpot>(Vec(xtpots,35 - 4 + 25 * 6 + 20), module, FORK::A_PARAM + 2, 0.0f, 0.7f, 0.3f);
	addParam(A3);
	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 - 4 + 25 * 6 + 20 + 2), module, FORK::A_INPUT + 2));

	F4 = createParam<Trimpot>(Vec(xtpots,35 - 6 + 25 * 8), module, FORK::F_PARAM + 3, 3000, 4700, 3400);
	addParam(F4);
	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 - 6 + 25 * 8 + 2), module, FORK::F_INPUT + 3));
	A4 = createParam<Trimpot>(Vec(xtpots,35 - 6 + 25 * 8 + 20), module, FORK::A_PARAM + 3, 0.0f, 0.3f, 0.2f);
	addParam(A4);
	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 - 6 + 25 * 8 + 20 + 2), module, FORK::A_INPUT + 3));

	addInput(createInput<TinyPJ301MPort>(Vec(20,327), module, FORK::PITCH_INPUT));
	addOutput(createOutput<TinyPJ301MPort>(Vec(55,327), module, FORK::SIGNAL_OUTPUT));

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));
}
