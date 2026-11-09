#include "plugin.hpp"
#include "BidooComponents.hpp"

using namespace std;

// Formant oscillator: four formants, each a pulse train at the pitch (its
// width set by the formant's bandwidth) multiplied by a carrier crossfaded
// between the two harmonics around the formant frequency.

//Approximates cos(pi*x) for x in [-1,1]: a rough curve (0.125 at x = 0.5),
// kept for the Hann window of the formant table so the table stays as it was.
inline float fast_cos(const float x)
{
  float x2=x*x;
  return 1.0f+x2*(-4.0f+2.0f*x2);
}

// cos(pi x) for x in [-1, 1], four at once, within 3e-5 (the carriers): folded
// to [-1/2, 1/2] (cos(pi x) = -cos(pi (1 - |x|))), then a degree-8 Taylor
// polynomial. fast_cos's error (0.125) was a third harmonic of every carrier,
// a ghost formant at three times its frequency.
inline simd::float_4 cosPi4(simd::float_4 x)
{
  using simd::float_4;
  float_4 a = simd::fabs(x);
  float_4 far = a > 0.5f;
  float_4 y = simd::ifelse(far, 1.0f - a, a);
  float_4 z = y * y * 9.8696044f; // (pi y)^2
  float_4 c = 1.0f + z * (-0.5f + z * (1.0f / 24.0f + z * (-1.0f / 720.0f + z * (1.0f / 40320.0f))));
  return simd::ifelse(far, -c, c);
}

// x wrapped into [-1, 1) (floor-based, no fmodf)
inline simd::float_4 wrapUnit4(simd::float_4 x)
{
  x += 1.0f;
  return x - 2.0f * simd::floor(x * 0.5f) - 1.0f;
}
//Length of the table
#define L_TABLE (256+1) //The last entry of the table equals the first (to avoid a modulo)
//Maximal formant width
#define I_MAX 64

//Formantic function of width I (used to fill the table of formants)
static float fonc_formant(float p,const float I)
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

// The table of formants, built once for every FORK, and for each width a gain
// between its RMS and its peak: a vowel keeps about the same loudness across
// the keyboard, and the narrow pulses of low notes do not peak too high.
struct FormantTable {
	float TF[L_TABLE*I_MAX];
	float norm[I_MAX];

	FormantTable() {
		float coef=2.0f/(L_TABLE-1);
		for (int I=0; I<I_MAX; I++) {
			double sq = 0.0, peak = 0.0;
			for (int P=0; P<L_TABLE; P++) {
				TF[P+I*L_TABLE]=fonc_formant(-1+P*coef,float(I));
				if (P < L_TABLE-1)
					sq += (double)TF[P+I*L_TABLE] * TF[P+I*L_TABLE];
				peak = std::max(peak, (double)std::fabs(TF[P+I*L_TABLE]));
			}
			double rms = std::sqrt(std::max(sq / (L_TABLE-1), 1e-6));
			norm[I] = (float)(1.0 / std::sqrt(rms * std::max(peak, 1e-3)));
		}
	}

	static const FormantTable &get() {
		static const FormantTable table;
		return table;
	}

	//This function emulates the function fonc_formant thanks to the table TF,
	// with a bilinear interpolation, normalized (see norm).
	float formant(float p,float i) const {
		i=i<0?0:i>I_MAX-2?I_MAX-2:i;    // width limitation
		float P=(L_TABLE-1)*(p+1)*0.5f; // phase normalisation
		int P0=std::min((int)P, L_TABLE-2);
		float fP=P-P0;  // Integer and fractional
		int I0=(int)i;
		float fI=i-I0;  // parts of the phase (p) and width (i).
		int i00=P0+L_TABLE*I0;
		int i10=i00+L_TABLE;
		float v = (1-fI)*(TF[i00] + fP*(TF[i00+1]-TF[i00])) + fI*(TF[i10] + fP*(TF[i10+1]-TF[i10]));
		return v * ((1-fI)*norm[I0] + fI*norm[I0+1]);
	}
};

// Double carrier, for the four formants at once.
// h : position (float harmonic number), at least the first harmonic so that a
//     formant below the pitch gives the fundamental, never a DC offset
// p : phase
static simd::float_4 porteuse4(simd::float_4 h, const float p)
{
  h = simd::fmax(h, 1.0f);
  simd::float_4 h0 = simd::floor(h);  //integer and
  simd::float_4 hf = h - h0;          //decimal part of harmonic number.
  // p*h0 and p*(h0+1) brought back into [-1,1)
  simd::float_4 porteuse0 = cosPi4(wrapUnit4(p * h0));
  simd::float_4 porteuse1 = cosPi4(wrapUnit4(p * (h0 + 1.0f)));
  // crossfade between the two carriers.
  return porteuse0 + hf * (porteuse1 - porteuse0);
}

struct ForkVoice {
	float p0 = 0.0f;
	simd::float_4 f = {730.0f, 1090.0f, 2440.0f, 3400.0f};
	simd::float_4 a = 0.0f;
	float dcIn = 0.0f, dcOut = 0.0f;
};

struct FORK : BidooModule {
	enum ParamIds {
		PITCH_PARAM,
		PRESET_PARAM,
		F_PARAM,
		A_PARAM = F_PARAM + 4,
		NUM_PARAMS = A_PARAM + 4
	};
	enum InputIds {
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

	static constexpr int NUM_PRESETS = 9;
	// Vowel presets: formant frequencies (Hz) and levels.
	const float PF[4][NUM_PRESETS] = {
		{  730.0f,  200.0f,  400.0f,  250.0f,  190.0f,  350.0f,  550.0f,  550.0f,  450.0f},
		{ 1090.0f, 2100.0f,  900.0f, 1700.0f,  800.0f, 1900.0f, 1600.0f,  850.0f, 1100.0f},
		{ 2440.0f, 3100.0f, 2300.0f, 2100.0f, 2000.0f, 2500.0f, 2250.0f, 1900.0f, 1500.0f},
		{ 3400.0f, 4700.0f, 3000.0f, 3300.0f, 3400.0f, 3700.0f, 3200.0f, 3000.0f, 3000.0f}};
	const float PA[4][NUM_PRESETS] = {
		{ 1.0f, 0.5f, 1.0f, 1.0f, 0.7f, 1.0f, 1.0f, 0.3f, 1.0f},
		{ 2.0f, 0.5f, 0.7f, 0.7f,0.35f, 0.3f, 0.5f, 1.0f, 0.7f},
		{ 0.3f,0.15f, 0.2f, 0.4f, 0.1f, 0.3f, 0.7f, 0.2f, 0.2f},
		{ 0.2f, 0.1f, 0.2f, 0.3f, 0.1f, 0.1f, 0.3f, 0.2f, 0.3f}};
	// Knob ranges of the formants and their levels.
	const float F_MIN[4] = {190.0f, 800.0f, 1500.0f, 3000.0f};
	const float F_MAX[4] = {730.0f, 2100.0f, 3100.0f, 4700.0f};
	const float A_MAX[4] = {1.0f, 2.0f, 0.7f, 0.3f};
	// Bandwidths of the formants (Hz) and their mix.
	const float BW[4] = {100.0f, 120.0f, 150.0f, 300.0f};
	const float MIX[4] = {1.0f, 0.7f, 1.0f, 1.0f};
	static constexpr float GAIN = 1.5f;        // output level (V per unit)
	static constexpr float GLIDE = 0.02f;      // formant and level glide (s)

	const FormantTable &table = FormantTable::get();
	int preset = 0;
	ForkVoice voices[16];
	dsp::SchmittTrigger presets;
	float lastSampleTime = 0.0f, glideCoef = 0.0f, fTop = 12000.0f, dcCoef = 1.0f;

	FORK() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(PITCH_PARAM, -54.0f, 54.0f, 0.0f, "Pitch", " Hz", std::pow(2.0f, 1.0f / 12.0f), 261.626f);
		configButton(PRESET_PARAM, "Next vowel preset");
		for (int k = 0; k < 4; k++) {
			configParam(F_PARAM + k, F_MIN[k], F_MAX[k], PF[k][0], rack::string::f("Formant %d frequency", k + 1), " Hz");
			configParam(A_PARAM + k, 0.0f, A_MAX[k], PA[k][0], rack::string::f("Formant %d level", k + 1));
			configInput(F_INPUT + k, rack::string::f("Formant %d frequency (1V/oct)", k + 1));
			configInput(A_INPUT + k, rack::string::f("Formant %d level (10 V = full range)", k + 1));
		}
		configInput(PITCH_INPUT, "1V/oct pitch (polyphonic)");
		configOutput(SIGNAL_OUTPUT, "Audio");
	}

	void onReset() override {
		preset = 0;
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "preset", json_integer(preset));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		json_t *presetJ = json_object_get(rootJ, "preset");
		if (presetJ)
			preset = clamp((int)json_integer_value(presetJ), 0, NUM_PRESETS - 1);
	}

	void process(const ProcessArgs &args) override;
};


void FORK::process(const ProcessArgs &args) {
	if (presets.process(params[PRESET_PARAM].getValue())) {
		preset = (preset + 1) % NUM_PRESETS;
		for (int k = 0; k < 4; k++) {
			params[F_PARAM + k].setValue(PF[k][preset]);
			params[A_PARAM + k].setValue(PA[k][preset]);
		}
	}

	int channels = std::max(inputs[PITCH_INPUT].getChannels(), 1);
	if (args.sampleTime != lastSampleTime) {
		lastSampleTime = args.sampleTime;
		glideCoef = 1.0f - std::exp(-args.sampleTime / GLIDE);
		fTop = std::min(12000.0f, 0.45f * args.sampleRate);
		// DC blocker at about 10 Hz.
		dcCoef = 1.0f - 2.0f * float(M_PI) * 10.0f * args.sampleTime;
	}
	const float r = glideCoef;

	// the knobs, read once for every voice
	simd::float_4 fKnob, aKnob, aMax, mix, bw;
	bool fCv[4], aCv[4];
	for (int k = 0; k < 4; k++) {
		fKnob[k] = params[F_PARAM + k].getValue();
		aKnob[k] = params[A_PARAM + k].getValue();
		aMax[k] = A_MAX[k];
		mix[k] = MIX[k];
		bw[k] = BW[k];
		fCv[k] = inputs[F_INPUT + k].isConnected();
		aCv[k] = inputs[A_INPUT + k].isConnected();
	}
	float pitchKnob = params[PITCH_PARAM].getValue();

	for (int c = 0; c < channels; c++) {
		ForkVoice &v = voices[c];
		float f0 = 261.626f * dsp::exp2_taylor5(clamp(pitchKnob + 12.0f * inputs[PITCH_INPUT].getVoltage(c), -54.0f, 54.0f) / 12.0f);
		v.p0 += f0 * (2.0f / args.sampleRate);
		v.p0 -= 2.0f * (v.p0 > 1.0f);
		float un_f0 = 1.0f / f0;

		// The frequency CV is 1V/oct from the knob; the level CV adds to the knob.
		simd::float_4 fT = fKnob, aT = aKnob;
		for (int k = 0; k < 4; k++) {
			if (fCv[k])
				fT[k] = clamp(fKnob[k] * dsp::exp2_taylor5(clamp(inputs[F_INPUT + k].getPolyVoltage(c), -5.0f, 5.0f)), 50.0f, fTop);
			if (aCv[k])
				aT[k] = aKnob[k] + inputs[A_INPUT + k].getPolyVoltage(c) * A_MAX[k] * 0.1f;
		}
		aT = simd::clamp(aT, 0.0f, aMax);
		v.f += r * (fT - v.f);
		v.a += r * (aT - v.a);
		simd::float_4 width = bw * un_f0;
		simd::float_4 form;
		for (int k = 0; k < 4; k++)
			form[k] = table.formant(v.p0, width[k]);
		simd::float_4 o = mix * v.a * form * porteuse4(v.f * un_f0, v.p0);
		float out = o[0] + o[1] + o[2] + o[3];

		float y = out - v.dcIn + dcCoef * v.dcOut;
		v.dcIn = out;
		v.dcOut = y;
		outputs[SIGNAL_OUTPUT].setVoltage(GAIN * y, c);
	}
	outputs[SIGNAL_OUTPUT].setChannels(channels);
}

struct FORKWidget : BidooWidget {
	FORKWidget(FORK *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/FORK.svg"));

		// No screws, as before. The original hand-drawn panel: the pitch knob; four formants, each an F and a G row of a
		// trimpot, its letter and its CV jack; the presets button; V/O and OUT. Each formant row sits on its letter's
		// middle (the old code had the first ones up to 1.1 px below it); V/O and OUT on their boxes' centres.
		addParam(theme::param<BidooLargeBlueKnob>(theme::specificCorner(26, 40, theme::kLargeKnobSize), module, FORK::PITCH_PARAM));
		addParam(theme::param<BlueCKD6>(theme::specificCorner(30, 274, theme::kCKD6Size), module, FORK::PRESET_PARAM));

		const theme::Pos trimpotCol = theme::specificCorner(32, 85, theme::kTrimpotSize);
		const theme::Pos cvCol = theme::specificCorner(67, 87, theme::kTinyJackSize);
		const theme::Pos letter[8] = {theme::specific(0, 93.469f), theme::specific(0, 113.688f), theme::specific(0, 142.156f), theme::specific(0, 162.406f),
			theme::specific(0, 190.344f), theme::specific(0, 210.562f), theme::specific(0, 238.219f), theme::specific(0, 258.438f)};   // F, G, F, G...
		for (int i = 0; i < 4; i++) {
			addParam(theme::param<BidooBlueTrimpot>(theme::colRow(trimpotCol, letter[2 * i]), module, FORK::F_PARAM + i));
			addInput(theme::input<TinyPJ301MPort>(theme::colRow(cvCol, letter[2 * i]), module, FORK::F_INPUT + i));
			addParam(theme::param<BidooBlueTrimpot>(theme::colRow(trimpotCol, letter[2 * i + 1]), module, FORK::A_PARAM + i));
			addInput(theme::input<TinyPJ301MPort>(theme::colRow(cvCol, letter[2 * i + 1]), module, FORK::A_INPUT + i));
		}

		const theme::Pos bottomRow = theme::specificCorner(7, 330, theme::kJackSize);
		addInput(theme::input<PJ301MPort>(theme::colRow(theme::specific(19.562f, 0), bottomRow), module, FORK::PITCH_INPUT));    // the V/O box's centre
		addOutput(theme::output<PJ301MPort>(theme::colRow(theme::specific(70.469f, 0), bottomRow), module, FORK::SIGNAL_OUTPUT));   // the OUT box's centre
	}
};

Model *modelFORK = createModel<FORK, FORKWidget>("ForK");
