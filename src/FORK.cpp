#include "plugin.hpp"
#include "BidooComponents.hpp"

using namespace std;

//Approximates cos(pi*x) for x in [-1,1].
inline float fast_cos(const float x)
{
  float x2=x*x;
  return 1.0f+x2*(-4.0f+2.0f*x2);
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
 int P0=(int)P;
 float fP=P-P0;  // Integer and fractional
 int I0=(int)i;
 float fI=i-I0;  // parts of the phase (p) and width (i).
 int i00=P0+L_TABLE*I0;
 int i10=i00+L_TABLE;
 //bilinear interpolation.
 return (1-fI)*(TF[i00] + fP*(TF[i00+1]-TF[i00])) + fI*(TF[i10] + fP*(TF[i10+1]-TF[i10]));
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
  float Porteuse0=fast_cos(phi0);
  float Porteuse1=fast_cos(phi1);
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

	float F1[9]={  730.0f,  200.0f,  400.0f,  250.0f,  190.0f,  350.0f,  550.0f,  550.0f,  450.0f};
	float A1[9]={ 1.0f, 0.5f, 1.0f, 1.0f, 0.7f, 1.0f, 1.0f, 0.3f, 1.0f};
	float F2[9]={ 1090.0f, 2100.0f,  900.0f, 1700.0f,  800.0f, 1900.0f, 1600.0f,  850.0f, 1100.0f};
	float A2[9]={ 2.0f, 0.5f, 0.7f, 0.7f,0.35f, 0.3f, 0.5f, 1.0f, 0.7f};
	float F3[9]={ 2440.0f, 3100.0f, 2300.0f, 2100.0f, 2000.0f, 2500.0f, 2250.0f, 1900.0f, 1500.0f};
	float A3[9]={ 0.3f,0.15f, 0.2f, 0.4f, 0.1f, 0.3f, 0.7f, 0.2f, 0.2f};
	float F4[9]={ 3400.0f, 4700.0f, 3000.0f, 3300.0f, 3400.0f, 3700.0f, 3200.0f, 3000.0f, 3000.0f};
	float A4[9]={ 0.2f, 0.1f, 0.2f, 0.3f, 0.1f, 0.1f, 0.3f, 0.2f, 0.3f};
	int preset=0;
	float f0,dp0,p0=0.0f;
	float f1,f2,f3,f4,a1,a2,a3,a4;
  dsp::SchmittTrigger presets;

	FORK() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PITCH_PARAM, -54.0f, 54.0f, 0);
    configParam(PRESET_PARAM, 0.0f, 8.0f, 0.0f);
    configParam(F_PARAM, 190.0f, 730.0f, 190.0f);
    configParam(A_PARAM, 0.0f, 1.0f, 1.0f);
    configParam(F_PARAM + 1.0f, 800.0f, 2100.0f, 1090.0f);
    configParam(A_PARAM + 1, 0.0f, 2.0f, 1.0f);
    configParam(F_PARAM + 2, 1500.0f, 3100.0f, 2440.0f);
    configParam(A_PARAM + 2, 0.0f, 0.7f, 0.3f);
    configParam(F_PARAM + 3, 3000.0f, 4700.0f, 3400.0f);
    configParam(A_PARAM + 3, 0.0f, 0.3f, 0.2f);

		init_formant();
		f1=f2=f3=f4=100.0f;a1=a2=a3=a4=0.0f;
	}

	void process(const ProcessArgs &args) override;
};


void FORK::process(const ProcessArgs &args) {
  if (presets.process(params[PRESET_PARAM].getValue())) {
    preset = (preset + 1)%8;
    params[F_PARAM].setValue(F1[preset]);
    params[F_PARAM+1].setValue(F2[preset]);
    params[F_PARAM+2].setValue(F3[preset]);
    params[F_PARAM+3].setValue(F4[preset]);
    params[A_PARAM].setValue(A1[preset]);
    params[A_PARAM+1].setValue(A2[preset]);
    params[A_PARAM+2].setValue(A3[preset]);
    params[A_PARAM+3].setValue(A4[preset]);
  }

	f0=261.626f * powf(2.0f, clamp(params[PITCH_PARAM].getValue() + 12.0f * inputs[PITCH_INPUT].getVoltage(),-54.0f,54.0f) / 12.0f);
	dp0=f0*(2/args.sampleRate);
	float un_f0=1.0f/f0;
	p0+=dp0;
	p0-=2.0f*(p0>1.0f);
	{
		float r=0.001f;
		f1+=r*(clamp(params[F_PARAM].getValue() + rescale(inputs[F_INPUT].getVoltage(),0.0f,10.0f,190.0f,730.0f),190.0f,730.0f)-f1);
		f2+=r*(clamp(params[F_PARAM+1].getValue() + rescale(inputs[F_INPUT+1].getVoltage(),0.0f,10.0f,800.0f,2100.0f),800.0f,2100.0f)-f2);
		f3+=r*(clamp(params[F_PARAM+2].getValue() + rescale(inputs[F_INPUT+2].getVoltage(),0.0f,10.0f,1500.0f,3100.0f),1500.0f,3100.0f)-f3);
		f4+=r*(clamp(params[F_PARAM+3].getValue() + rescale(inputs[F_INPUT+3].getVoltage(),0.0f,10.0f,3000.0f,4700.0f),3000.0f,4700.0f)-f4);
		a1+=r*(clamp(params[A_PARAM].getValue() + rescale(inputs[A_INPUT].getVoltage(),0.0f,10.0f,0.0f,1.0f),0.0f,1.0f)-a1);
		a2+=r*(clamp(params[A_PARAM+1].getValue() + rescale(inputs[A_INPUT+1].getVoltage(),0.0f,10.0f,0.0f,2.0f),0.0f,2.0f)-a2);
		a3+=r*(clamp(params[A_PARAM+2].getValue() + rescale(inputs[A_INPUT+2].getVoltage(),0.0f,10.0f,0.0f,0.7f),0.0f,0.7f)-a3);
		a4+=r*(clamp(params[A_PARAM+3].getValue() + rescale(inputs[A_INPUT+3].getVoltage(),0.0f,10.0f,0.0f,0.3f),0.0f,0.3f)-a4);
	}

	float out= a1*(f0/f1)*formant(p0,100.0f*un_f0)*porteuse(f1*un_f0,p0)
	 + 0.7f*a2*(f0/f2)*formant(p0,120.0f*un_f0)*porteuse(f2*un_f0,p0)
	 +     a3*(f0/f3)*formant(p0,150.0f*un_f0)*porteuse(f3*un_f0,p0)
	 +     a4*(f0/f4)*formant(p0,300.0f*un_f0)*porteuse(f4*un_f0,p0);
	outputs[SIGNAL_OUTPUT].setVoltage(5.0f*out);
}

struct FORKWidget : ModuleWidget {
	FORKWidget(FORK *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/FORK.svg")));

  	addParam(createParam<BidooLargeBlueKnob>(Vec(26.0f,40.0f), module, FORK::PITCH_PARAM));
  	addParam(createParam<BlueCKD6>(Vec(30.0f,274.0f), module, FORK::PRESET_PARAM));

  	int xtpots = 32;
  	int xtinyports = 67;

  	addParam(createParam<BidooBlueTrimpot>(Vec(xtpots,35.0f + 25.0f * 2.0f), module, FORK::F_PARAM));
  	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35.0f + 25.0f * 2.0f + 2.0f), module, FORK::F_INPUT));
  	addParam(createParam<BidooBlueTrimpot>(Vec(xtpots,35.0f + 25.0f * 2.0f + 20.0f), module, FORK::A_PARAM));
  	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35.0f + 25.0f * 2.0f + 20.0f + 2.0f), module, FORK::A_INPUT));

  	addParam(createParam<BidooBlueTrimpot>(Vec(xtpots,35.0f - 2.0f + 25.0f * 4.0f), module, FORK::F_PARAM + 1.0f));
  	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 - 2.0f + 25.0f * 4.0f + 2.0f), module, FORK::F_INPUT + 1));
  	addParam(createParam<BidooBlueTrimpot>(Vec(xtpots,35.0f - 2.0f + 25.0f * 4.0f + 20.0f), module, FORK::A_PARAM + 1));
  	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 - 2 + 25 * 4 + 20 + 2), module, FORK::A_INPUT + 1));

  	addParam(createParam<BidooBlueTrimpot>(Vec(xtpots,35.0f - 4.0f + 25.0f * 6.0f), module, FORK::F_PARAM + 2));
  	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 - 4 + 25 * 6 + 2), module, FORK::F_INPUT + 2));
  	addParam(createParam<BidooBlueTrimpot>(Vec(xtpots,35 - 4 + 25 * 6 + 20), module, FORK::A_PARAM + 2));
  	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 - 4 + 25 * 6 + 20 + 2), module, FORK::A_INPUT + 2));

  	addParam(createParam<BidooBlueTrimpot>(Vec(xtpots,35 - 6 + 25 * 8), module, FORK::F_PARAM + 3));
  	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 - 6 + 25 * 8 + 2), module, FORK::F_INPUT + 3));
  	addParam(createParam<BidooBlueTrimpot>(Vec(xtpots,35 - 6 + 25 * 8 + 20), module, FORK::A_PARAM + 3));
  	addInput(createInput<TinyPJ301MPort>(Vec(xtinyports,35 - 6 + 25 * 8 + 20 + 2), module, FORK::A_INPUT + 3));

  	addInput(createInput<PJ301MPort>(Vec(7,330), module, FORK::PITCH_INPUT));
  	addOutput(createOutput<PJ301MPort>(Vec(58.3f,330), module, FORK::SIGNAL_OUTPUT));
  }
};

Model *modelFORK = createModel<FORK, FORKWidget>("ForK");
