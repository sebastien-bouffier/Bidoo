#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include "window.hpp"
#include <thread>
#include "dsp/resampler.hpp"

#define FRAME_SIZE 2048
#define BINS 256
using namespace std;

template <int OVERSAMPLE, int QUALITY>
struct WavOscillator {
	bool soft = false;
	float lastSyncValue = 0.0f;
	float phase = 0.0f;
	float freq;
	float pitch;
	bool syncEnabled = false;
	bool syncDirection = false;
	dsp::Decimator<OVERSAMPLE, QUALITY> wavDecimator;
	float pitchSlew = 0.0f;
	int pitchSlewIndex = 0;

	float wavBuffer[OVERSAMPLE] = {};

	void setPitch(float pitchKnob, float pitchCv) {
		// Compute frequency
		pitch = pitchKnob;
		pitch = roundf(pitch);
		pitch += pitchCv;
		// Note C4
		freq = 261.626f * powf(2.0f, pitch / 12.0f);
	}

	void process(float deltaTime, float syncValue, float *wavTable) {
		// Advance phase
		float deltaPhase = clamp(freq * deltaTime, 1e-6, 0.5f);

		// Detect sync
		int syncIndex = -1; // Index in the oversample loop where sync occurs [0, OVERSAMPLE)
		float syncCrossing = 0.0f; // Offset that sync occurs [0.0f, 1.0f)
		if (syncEnabled) {
			syncValue -= 0.01f;
			if (syncValue > 0.0f && lastSyncValue <= 0.0f) {
				float deltaSync = syncValue - lastSyncValue;
				syncCrossing = 1.0f - syncValue / deltaSync;
				syncCrossing *= OVERSAMPLE;
				syncIndex = (int)syncCrossing;
				syncCrossing -= syncIndex;
			}
			lastSyncValue = syncValue;
		}

		if (syncDirection)
			deltaPhase *= -1.0f;

		for (int i = 0; i < OVERSAMPLE; i++) {
			if (syncIndex == i) {
				if (soft) {
					syncDirection = !syncDirection;
					deltaPhase *= -1.0f;
				}
				else {
					// phase = syncCrossing * deltaPhase / OVERSAMPLE;
					phase = 0.0f;
				}
			}

			wavBuffer[i] = 1.66f * interpolateLinear(wavTable, phase * 2047.f);

			phase += deltaPhase / OVERSAMPLE;
			phase = math::eucMod(phase, 1.0f);
		}
	}

	float wav() {
		return wavDecimator.process(wavBuffer);
	}
};

struct threadSynthData {
	float *magn;
	float *phas;
	float *wav;
};

void * threadSynthTask(threadSynthData data)
{
	float *tWav;
	tWav = (float*)calloc(FRAME_SIZE,sizeof(float));
	float amp = 1.0f;
  for(size_t i=0 ; i<FRAME_SIZE; i++) {
		for(size_t j=0; j<BINS; j++) {
			if (data.magn[j]>0.0f) {
				tWav[i]+= data.magn[j] * 0.01f * sin(i*j*2.0*M_PI/FRAME_SIZE + data.phas[j]);
			}
		}
		amp = max(amp,abs(tWav[i]));
	}
	for(size_t i=0 ; i<FRAME_SIZE; i++) {
		tWav[i] /= amp;
	}
	memcpy(data.wav,tWav,FRAME_SIZE*sizeof(float));
	free(tWav);
  return 0;
}

struct PENEQUE : Module {
	enum ParamIds {
		RESET_PARAM,
		SYNC_PARAM,
		FREQ_PARAM,
		FINE_PARAM,
		FM_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
		FM_INPUT,
		SYNC_INPUT,
		SYNCMODE_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

  float *magn;
  float *phas;
  float *wav;
	thread sThread;
	threadSynthData sData;
	dsp::SchmittTrigger resetTrigger;
	WavOscillator<16, 16> oscillator;

	PENEQUE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(SYNC_PARAM, 0.0f, 1.0f, 1.0f);
    configParam(FREQ_PARAM, -54.0f, 54.0f, 0.0f);
    configParam(FINE_PARAM, -1.0f, 1.0f, 0.0f);
    configParam(FM_PARAM, 0.0f, 1.0f, 0.0f);
    configParam(RESET_PARAM, 0.0f, 1.0f, 0.0f);

    magn = (float*)calloc(BINS,sizeof(float));
    phas = (float*)calloc(BINS,sizeof(float));
		wav = (float*)calloc(FRAME_SIZE,sizeof(float));

		sData.magn = magn;
		sData.phas = phas;
		sData.wav = wav;
	}

  ~PENEQUE() {
    free(magn);
		free(phas);
		free(wav);
	}

	void process(const ProcessArgs &args) override;

	void computeWavelet();

	json_t *dataToJson() override {
		json_t *rootJ = json_object();

		json_t *magnJ = json_array();
		json_t *phasJ = json_array();

		for (size_t i=0; i<BINS; i++) {
			json_t *magnI = json_real(magn[i]);
			json_array_append_new(magnJ, magnI);
			json_t *phasI = json_real(phas[i]);
			json_array_append_new(phasJ, phasI);
		}

		json_object_set_new(rootJ, "magn", magnJ);
		json_object_set_new(rootJ, "phas", phasJ);

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *magnJ = json_object_get(rootJ, "magn");
		json_t *phasJ = json_object_get(rootJ, "phas");
		if (magnJ && phasJ) {
			for (size_t i=0; i<BINS; i++) {
				magn[i] = json_number_value(json_array_get(magnJ, i));
				phas[i] = json_number_value(json_array_get(phasJ, i));
			}
		}
		computeWavelet();
	}

	void onRandomize() override {
		for (size_t i=0; i<BINS; i++) {
			magn[i]=random::uniform()*100.0f;
			phas[i]=random::uniform()*M_PI;
		}
		computeWavelet();
	}
};

void PENEQUE::computeWavelet() {
	sThread = thread(threadSynthTask, std::ref(sData));
	sThread.detach();
}

void PENEQUE::process(const ProcessArgs &args) {
	oscillator.soft = (params[SYNC_PARAM].value + inputs[SYNCMODE_INPUT].value) <= 0.0f;
	float pitchFine = 3.0f * dsp::quadraticBipolar(params[FINE_PARAM].value);
	float pitchCv = 12.0f * inputs[PITCH_INPUT].value;
	if (inputs[FM_INPUT].active) {
		pitchCv += dsp::quadraticBipolar(params[FM_PARAM].value) * 12.0f * inputs[FM_INPUT].value;
	}
	oscillator.setPitch(params[FREQ_PARAM].value, pitchFine + pitchCv);
	oscillator.syncEnabled = inputs[SYNC_INPUT].active;
	oscillator.process(args.sampleTime, inputs[SYNC_INPUT].value, wav);

	if (outputs[OUT].active)
		outputs[OUT].value = 5.0f * oscillator.wav();

	if (resetTrigger.process(params[RESET_PARAM].value))
	{
		memset(phas, 0, BINS*sizeof(float));
	  memset(magn, 0, BINS*sizeof(float));
		computeWavelet();
	}

}

struct PENEQUEMagnDisplay : OpaqueWidget {
	PENEQUE *module;
	shared_ptr<Font> font;
	const float width = 400.0f;
	const float heightMagn = 70.0f;
	const float heightPhas = 50.0f;
	const float graphGap = 30.0f;
	float zoomWidth = width * 6.0f;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refY = 0.0f;
	float refX = 0.0f;
	bool write = false;

	PENEQUEMagnDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void onButton(const event::Button &e) override {
		refX = e.pos.x;
		refY = e.pos.y;
		refIdx = ((e.pos.x - zoomLeftAnchor)/zoomWidth)*(float)BINS + 1;
		OpaqueWidget::onButton(e);
	}

	void onDragStart(const event::DragStart &e) override {
		appGet()->window->cursorLock();
		OpaqueWidget::onDragStart(e);
	}

	void onDragMove(const event::DragMove &e) override {
		if (!((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_CONTROL))) {
			if (refY<=heightMagn) {
				if (((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_CONTROL))) {
					module->magn[refIdx] = 0.0f;
				}
				else {
					module->magn[refIdx] -= e.mouseDelta.y/10.0f;
					module->magn[refIdx] = clamp(module->magn[refIdx],0.0f, 100.0f);
				}
			}
			else if (refY>=heightMagn+graphGap) {
				if ((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_CONTROL)) {
					module->phas[refIdx] = 0.0f;
				}
				else {
					module->phas[refIdx] -= e.mouseDelta.y/10.0f;
					module->phas[refIdx] = clamp(module->phas[refIdx],-1.0f * M_PI, M_PI);
				}
			}
			module->computeWavelet();
		}
		else {
			zoomLeftAnchor = clamp(refX - (refX - zoomLeftAnchor) + e.mouseDelta.x, width - zoomWidth,0.0f);
		}
		OpaqueWidget::onDragMove(e);
	}

	void onDragEnd(const event::DragEnd &e) override {
		appGet()->window->cursorUnlock();
		OpaqueWidget::onDragEnd(e);
		//module->computeWavelet();
	}

	void draw(NVGcontext *vg) override {
    if (module) {
      // Draw Magnitude & Phase
  		nvgSave(vg);
  		Rect b = Rect(Vec(zoomLeftAnchor, 0), Vec(zoomWidth, heightMagn + graphGap + heightPhas));
  		nvgScissor(vg, 0, b.pos.y, width, heightMagn + graphGap + heightPhas);
  		float invBins = 1.0f/ BINS;
  		size_t tag=1;
  		for (size_t i = 0; i < BINS-1; i++) {
  			float x, y;
  			x = (float)i * invBins;
  			y = module->magn[i+1]*0.01f;
  			Vec p;
  			p.x = b.pos.x + b.size.x * x;
  			p.y = heightMagn * y;

  			if ((i+1)==tag){
  				nvgBeginPath(vg);
  				nvgFillColor(vg, nvgRGBA(45, 114, 143, 100));
  				nvgRect(vg, p.x, 0, b.size.x * invBins, heightMagn);
  				nvgRect(vg, p.x, heightMagn + graphGap, b.size.x * invBins, heightPhas);
  				nvgClosePath(vg);
  				nvgLineCap(vg, NVG_MITER);
  				nvgStrokeWidth(vg, 0);
  				//nvgStroke(vg);
  				nvgFill(vg);
  				tag *=2;
  			}

  			if (p.x < width) {
  				nvgBeginPath(vg);
  				nvgStrokeColor(vg, YELLOW_BIDOO);
  				nvgFillColor(vg, YELLOW_BIDOO);
  				nvgRect(vg, p.x, heightMagn - p.y, b.size.x * invBins, p.y);
  				y = module->phas[i+1]/M_PI;
  				p.y = heightPhas * 0.5 * y;
  				nvgRect(vg, p.x, heightMagn + graphGap + heightPhas * 0.5f - p.y, b.size.x * invBins, p.y);
  				nvgClosePath(vg);
  				nvgLineCap(vg, NVG_MITER);
  				nvgStrokeWidth(vg, 1);
  				nvgStroke(vg);
  				nvgFill(vg);
  			}
  		}
  		nvgResetScissor(vg);
  		nvgRestore(vg);
    }
	}
};

struct PENEQUEWavDisplay : OpaqueWidget {
	PENEQUE *module;
	shared_ptr<Font> font;
	const float width = 200.0f;
	const float height = 100.0f;
	float zoomWidth = width;
	float zoomLeftAnchor = 0.0f;
	int refIdx = 0;
	float refX = 0.0f;

	PENEQUEWavDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

  void onButton(const event::Button &e) override {
    refX = e.pos.x;
    OpaqueWidget::onButton(e);
  }

  void onDragStart(const event::DragStart &e) override {
    appGet()->window->cursorLock();
    OpaqueWidget::onDragStart(e);
  }

  void onDragEnd(const event::DragEnd &e) override {
    appGet()->window->cursorUnlock();
    OpaqueWidget::onDragEnd(e);
  }

	void onDragMove(const event::DragMove &e) override {
		float zoom = 1.0f;
		if (e.mouseDelta.y > 0.0f) {
			zoom = 1.0f/(((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f);
		}
		else if (e.mouseDelta.y < 0.0f) {
			zoom = ((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f;
		}
		zoomWidth = clamp(zoomWidth*zoom,width,zoomWidth*(((appGet()->window->getMods() & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) ? 2.0f : 1.1f));
		zoomLeftAnchor = clamp(refX - (refX - zoomLeftAnchor)*zoom + e.mouseDelta.x, width - zoomWidth,0.0f);
		OpaqueWidget::onDragMove(e);
	}

	void draw(const DrawArgs &args) override {
    if (module) {
      // Draw ref line
  		nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x30));
  		nvgStrokeWidth(args.vg, 1);
  		{
  			nvgBeginPath(args.vg);
  			nvgMoveTo(args.vg, 0, height/2);
  			nvgLineTo(args.vg, width, height/2);
  			nvgClosePath(args.vg);
  		}
  		nvgStroke(args.vg);

  		// Draw waveform
  		nvgStrokeColor(args.vg, YELLOW_BIDOO);
  		nvgSave(args.vg);
  		Rect b = Rect(Vec(zoomLeftAnchor, 0), Vec(zoomWidth, height));
  		nvgScissor(args.vg, 0, b.pos.y, width, height);
  		nvgBeginPath(args.vg);
  		float invNbSample = 1.0f/ FRAME_SIZE;
  		for (size_t i = 0; i < FRAME_SIZE; i++) {
  			float x, y;
  			x = (float)i * invNbSample ;
  			y = module->wav[i] * 0.48f + 0.5f;
  			Vec p;
  			p.x = b.pos.x + b.size.x * x;
  			p.y = b.pos.y + b.size.y * (1.0f - y);
  			if (i == 0) {
  				nvgMoveTo(args.vg, p.x, p.y);
  			}
  			else {
  				nvgLineTo(args.vg, p.x, p.y);
  			}
  		}
  		//nvgClosePath(vg);
  		nvgLineCap(args.vg, NVG_MITER);
  		nvgStrokeWidth(args.vg, 1);
  		nvgStroke(args.vg);
  		nvgResetScissor(args.vg);
  		nvgRestore(args.vg);
    }	
	}
};

struct PENEQUEWidget : ModuleWidget {
	PENEQUEWidget(PENEQUE *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/PENEQUE.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

  	{
  		PENEQUEMagnDisplay *display = new PENEQUEMagnDisplay();
  		display->module = module ? module : NULL;
  		display->box.pos = Vec(24, 50);
  		display->box.size = Vec(500, 160);
  		addChild(display);
  	}

  	{
  		PENEQUEWavDisplay *display = new PENEQUEWavDisplay();
  		display->module = module ? module : NULL;
  		display->box.pos = Vec(23, 220);
  		display->box.size = Vec(200, 110);
  		addChild(display);
  	}

  	addParam(createParam<CKSS>(Vec(255, 240), module, PENEQUE::SYNC_PARAM));
  	addParam(createParam<BidooBlueKnob>(Vec(287, 235), module, PENEQUE::FREQ_PARAM));
  	addParam(createParam<BidooBlueKnob>(Vec(327, 235), module, PENEQUE::FINE_PARAM));
  	addParam(createParam<BidooBlueKnob>(Vec(367, 235), module, PENEQUE::FM_PARAM));
  	addParam(createParam<BlueCKD6>(Vec(408.0f, 237), module, PENEQUE::RESET_PARAM));

  	addInput(createInput<PJ301MPort>(Vec(250, 300), module, PENEQUE::SYNCMODE_INPUT));
  	addInput(createInput<PJ301MPort>(Vec(290, 300), module, PENEQUE::PITCH_INPUT));
  	addInput(createInput<PJ301MPort>(Vec(330, 300), module, PENEQUE::SYNC_INPUT));
  	addInput(createInput<PJ301MPort>(Vec(370, 300), module, PENEQUE::FM_INPUT));
  	addOutput(createOutput<PJ301MPort>(Vec(410, 300), module, PENEQUE::OUT));
  }
};

Model *modelPENEQUE = createModel<PENEQUE, PENEQUEWidget>("PeNEqUe");
