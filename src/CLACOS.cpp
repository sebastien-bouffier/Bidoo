#include "Bidoo.hpp"
#include "BidooComponents.hpp"
#include "dsp/samplerate.hpp"
#include "dsp/decimator.hpp"
#include "dsp/filter.hpp"

using namespace std;

extern float sawTable[2048];
extern float triTable[2048];

struct CLACOS : Module {
	enum ParamIds {
		PITCH_PARAM,
    FINE_PARAM,
		DIST_X_PARAM,
    DIST_Y_PARAM = DIST_X_PARAM + 4,
		WAVEFORM_PARAM = DIST_Y_PARAM + 4,
		MODE_PARAM = WAVEFORM_PARAM + 4,
		SYNC_PARAM,
		FM_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
    SYNC_INPUT,
		DIST_X_INPUT,
    DIST_Y_INPUT = DIST_X_INPUT + 4,
		WAVEFORM_INPUT = DIST_Y_PARAM + 4,
		FM_INPUT = WAVEFORM_INPUT + 4,
		NUM_INPUTS
	};
	enum OutputIds {
		MAIN_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};
	bool analog = false;
	bool soft = false;
	float lastSyncValue = 0.0;
	float phase = 0.0;
	float phaseDist = 0.0;
	float phaseDistX[4] = {0.5};
	float phaseDistY[4] = {0.5};
	int waveFormIndex[4] = {0};
	float freq;
	float pitch;
	bool syncEnabled = false;
	bool syncDirection = false;
	int index = 0, prevIndex = 3;

	Decimator<16, 16> mainDecimator;
	RCFilter sqrFilter;
	RCFilter mainFilter;

	// For analog detuning effect
	float pitchSlew = 0.0;
	int pitchSlewIndex = 0;

	float sinBuffer[16] = {};
	float triBuffer[16] = {};
	float sawBuffer[16] = {};
	float sqrBuffer[16] = {};
	float mainBuffer[16] = {};

	void setPitch(float pitchKnob, float pitchCv) {
		// Compute frequency
		pitch = pitchKnob;
		if (analog) {
			// Apply pitch slew
			const float pitchSlewAmount = 3.0;
			pitch += pitchSlew * pitchSlewAmount;
		}
		else {
			// Quantize coarse knob if digital mode
			pitch = roundf(pitch);
		}
		pitch += pitchCv;
		// Note C3
		freq = 261.626 * powf(2.0, pitch / 12.0);
	}

	void process(float deltaTime, float syncValue) {
		if (analog) {
			// Adjust pitch slew
			if (++pitchSlewIndex > 32) {
				const float pitchSlewTau = 100.0; // Time constant for leaky integrator in seconds
				pitchSlew += (randomNormal() - pitchSlew / pitchSlewTau) / engineGetSampleRate();
				pitchSlewIndex = 0;
			}
		}

		// Advance phase
		float deltaPhase = clampf(freq * deltaTime, 1e-6, 0.5);

		// Detect sync
		int syncIndex = -1; // Index in the oversample loop where sync occurs [0, OVERSAMPLE)
		float syncCrossing = 0.0; // Offset that sync occurs [0.0, 1.0)
		if (syncEnabled) {
			syncValue -= 0.01;
			if (syncValue > 0.0 && lastSyncValue <= 0.0) {
				float deltaSync = syncValue - lastSyncValue;
				syncCrossing = 1.0 - syncValue / deltaSync;
				syncCrossing *= 16;
				syncIndex = (int)syncCrossing;
				syncCrossing -= syncIndex;
			}
			lastSyncValue = syncValue;
		}

		if (syncDirection)
			deltaPhase *= -1.0;

		sqrFilter.setCutoff(40.0 * deltaTime);
		mainFilter.setCutoff(22000.0 * deltaTime);

		for (int i = 0; i < 16; i++) {
			if (syncIndex == i) {
				if (soft) {
					syncDirection = !syncDirection;
					deltaPhase *= -1.0;
				}
				else {
					phase = 0.0;
					phaseDist = 0.0;
				}
			}

			if (phase<0.25)
				index = 0;
			else if ((phase>=0.25) && (phase<0.50))
				index = 1;
			else if ((phase>=0.50) && (phase<0.75))
				index = 2;
			else
				index = 3;

			if (analog) {
				// Quadratic approximation of sine, slightly richer harmonics
				if (phaseDist < 0.5f)
					sinBuffer[i] = 1.f - 16.f * powf(phaseDist - 0.25f, 2);
				else
					sinBuffer[i] = -1.f + 16.f * powf(phaseDist - 0.75f, 2);
				sinBuffer[i] *= 1.08f;
			}
			else {
				sinBuffer[i] = sinf(2.f * M_PI * phaseDist);
			}
			if (analog) {
				triBuffer[i] = 1.25f * interpf(triTable, phaseDist * 2047.f);
			}
			else {
				if (phaseDist < 0.25f)
					triBuffer[i] = 4.f * phaseDist;
				else if (phaseDist < 0.75f)
					triBuffer[i] = 2.f - 4.f * phaseDist;
				else
					triBuffer[i] = -4.f + 4.f * phaseDist;
			}
			if (analog) {
				sawBuffer[i] = 1.66f * interpf(sawTable, phaseDist * 2047.f);
			}
			else {
				if (phaseDist < 0.5f)
					sawBuffer[i] = 2.f * phaseDist;
				else
					sawBuffer[i] = -2.f + 2.f * phaseDist;
			}
			sqrBuffer[i] = (phaseDist < 0.5) ? 1.f : -1.f;
			if (analog) {
				// Simply filter here
				sqrFilter.process(sqrBuffer[i]);
				sqrBuffer[i] = 0.71f * sqrFilter.highpass();
			}

			waveFormIndex[index] = inputs[WAVEFORM_INPUT+index].active ? clampi(rescalef(inputs[WAVEFORM_INPUT+index].value,0,10,0,3),0,3) : clampi(params[WAVEFORM_PARAM+index].value,0,3);
			if (waveFormIndex[index] == 0)
				mainBuffer[i]=sinBuffer[i];
			else if (waveFormIndex[index] == 1)
				mainBuffer[i]=triBuffer[i];
			else if (waveFormIndex[index] == 2)
				mainBuffer[i]=sawBuffer[i];
			else if (waveFormIndex[index] == 3)
				mainBuffer[i]=sqrBuffer[i];

			mainFilter.process(mainBuffer[i]);
			mainBuffer[i]=mainFilter.lowpass();

			// Advance phase
			phase += deltaPhase / 16;
			phase = eucmodf(phase, 1.0);
			if (phase<=0.25)
				index = 0;
			else if ((phase>0.25) && (phase<=0.5))
				index = 1;
			else if ((phase>0.5) && (phase<=0.75))
				index = 2;
			else
				index = 3;

			if (prevIndex!=index){
				phaseDist = phase;
				prevIndex = index;
			}
			else {
				if (rescalef(phase,index*0.25,(index+1)*0.25,0,1)<=(phaseDistX[index]))
					phaseDist = min(phaseDist + (deltaPhase / 16) * phaseDistY[index]/phaseDistX[index], (index+1)*0.25f);
				else
					phaseDist = min(phaseDist + (deltaPhase / 16) * (1-phaseDistY[index])/(1-phaseDistX[index]), (index+1)*0.25f);
				phaseDist = eucmodf(phaseDist, 1.0);
			}
		}
	}

	float main() {
		return mainDecimator.process(mainBuffer);
	}
	float light() {
		return sinf(2*M_PI * phase);
	}

	CLACOS() : Module(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS) {
	}

	void step() override;

	json_t *toJson() override {
		json_t *rootJ = json_object();
		for (int i=0; i<4; i++) {
			json_object_set_new(rootJ, ("phaseDistX" + to_string(i)).c_str(), json_real(phaseDistX[i]));
			json_object_set_new(rootJ, ("phaseDistY" + to_string(i)).c_str(), json_real(phaseDistY[i]));
		}
		return rootJ;
	}

	void fromJson(json_t *rootJ) override {
		for (int i=0; i<4; i++) {
			json_t *phaseDistXJ = json_object_get(rootJ, ("phaseDistX" + to_string(i)).c_str());
			if (phaseDistXJ) {
				phaseDistX[i] = json_real_value(phaseDistXJ);
			}
			json_t *phaseDistYJ = json_object_get(rootJ, ("phaseDistY" + to_string(i)).c_str());
			if (phaseDistYJ) {
				phaseDistY[i] = json_real_value(phaseDistYJ);
			}
		}
	}

	void randomize() override {
		for (int i=0; i<4; i++) {
			if ((!inputs[CLACOS::DIST_X_INPUT+i].active) && (!inputs[CLACOS::DIST_X_INPUT+i].active)) {
				phaseDistX[i] = randomf();
				phaseDistY[i] = randomf();
			}
		}
	}

	void reset() override {
		for (int i=0; i<4; i++) {
			if ((!inputs[CLACOS::DIST_X_INPUT+i].active) && (!inputs[CLACOS::DIST_X_INPUT+i].active)) {
				phaseDistX[i] = 0.5;
				phaseDistY[i] = 0.5;
			}
		}
	}
};

void CLACOS::step() {
	analog = params[MODE_PARAM].value > 0.0;
	soft = params[SYNC_PARAM].value <= 0.0;

	for (int i=0; i<4; i++) {
	if (inputs[DIST_X_INPUT+i].active)
		phaseDistX[i] = rescalef(clampf(inputs[DIST_X_INPUT+i].value,0,10),0,10,0.01,0.99);

	if (inputs[DIST_Y_INPUT+i].active)
		phaseDistY[i] = rescalef(clampf(inputs[DIST_Y_INPUT+i].value,0,10),0,10,0.01,0.99);
	}

	float pitchFine = 3.0 * quadraticBipolar(params[FINE_PARAM].value);
	float pitchCv = 12.0 * inputs[PITCH_INPUT].value;
	if (inputs[FM_INPUT].active) {
		pitchCv += quadraticBipolar(params[FM_PARAM].value) * 12.0 * inputs[FM_INPUT].value;
	}
	setPitch(params[PITCH_PARAM].value, pitchFine + pitchCv);
	syncEnabled = inputs[SYNC_INPUT].active;

	process(1.0 / engineGetSampleRate(), inputs[SYNC_INPUT].value);

	// Set output
	outputs[MAIN_OUTPUT].value = 5.0 * main();
}

struct CLACOSDisplay : TransparentWidget {
	CLACOS *module;
	int frame = 0;
	string waveForm;
	int segmentNumber = 0;
	float initX = 0;
	float initY = 0;
	float dragX = 0;
	float dragY = 0;

CLACOSDisplay() {}

void onDragStart(EventDragStart &e) override {
	dragX = gRackWidget->lastMousePos.x;
	dragY = gRackWidget->lastMousePos.y;
}

void onDragMove(EventDragMove &e) override {
	if ((!module->inputs[CLACOS::DIST_X_INPUT + segmentNumber].active) && (!module->inputs[CLACOS::DIST_X_INPUT + segmentNumber].active)) {
		float newDragX = gRackWidget->lastMousePos.x;
		float newDragY = gRackWidget->lastMousePos.y;
		module->phaseDistX[segmentNumber] = rescalef(clampf(initX+(newDragX-dragX),0,70), 0, 70, 0.01,0.99);
		module->phaseDistY[segmentNumber]  = rescalef(clampf(initY-(newDragY-dragY),0,70), 0, 70, 0.01,0.99);
	}
}

void onMouseDown(EventMouseDown &e) override {
	if (e.button == 0) {
		e.consumed = true;
		e.target = this;
		initX = e.pos.x;
		initY = 70 - e.pos.y;
	}
}

void draw(NVGcontext *vg) override {
	if (++frame >= 4) {
		frame = 0;
		if (module->waveFormIndex[segmentNumber] == 0)
			waveForm="SIN";
		else if (module->waveFormIndex[segmentNumber] == 1)
			waveForm="TRI";
		else if (module->waveFormIndex[segmentNumber] == 2)
			waveForm="SAW";
		else if (module->waveFormIndex[segmentNumber] == 3)
			waveForm="SQR";
	}
	nvgFontSize(vg, 10);
	nvgFillColor(vg, nvgRGBA(42, 87, 117, 255));
	nvgText(vg, 12, 79, waveForm.c_str(), NULL);

	// Draw ref lines
	nvgStrokeColor(vg, nvgRGBA(0xff, 0xff, 0xff, 0x80));
	{
		nvgBeginPath(vg);
		nvgMoveTo(vg, 0, 35);
		nvgLineTo(vg, 70, 35);
		nvgMoveTo(vg, 35, 0);
		nvgLineTo(vg, 35, 70);
		nvgClosePath(vg);
	}
	nvgStroke(vg);

	// Draw phase distortion
	nvgStrokeColor(vg, nvgRGBA(42, 87, 117, 255));
	{
		nvgBeginPath(vg);
		nvgMoveTo(vg, 0 , 70);
		nvgLineTo(vg, (int)(rescalef(module->phaseDistX[segmentNumber], 0,1,0,70)) , 70 - (int)(rescalef(module->phaseDistY[segmentNumber], 0,1,0.01,70)));
		nvgMoveTo(vg, (int)(rescalef(module->phaseDistX[segmentNumber], 0,1,0,70)) , 70 - (int)(rescalef(module->phaseDistY[segmentNumber], 0,1,0.01,70)));
		nvgLineTo(vg, 70 , 0);
		nvgClosePath(vg);
	}
	nvgStroke(vg);
}
};

CLACOSWidget::CLACOSWidget() {
	CLACOS *module = new CLACOS();
	setModule(module);
	box.size = Vec(15*10, 380);

	{
		SVGPanel *panel = new SVGPanel();
		panel->box.size = box.size;
		panel->setBackground(SVG::load(assetPlugin(plugin, "res/CLACOS.svg")));
		addChild(panel);
	}

	for (int i = 0; i < 4; i++)
	{
		{
			CLACOSDisplay *display = new CLACOSDisplay();
			display->module = module;
			display->segmentNumber = i;
			display->box.pos = Vec(3 + 74 * (i%2), 113 + 102 * round(i/2));
			display->box.size = Vec(70, 70);
			addChild(display);
			addParam(createParam<BidooBlueTrimpot>(Vec(2 + 74 * (i%2), 194 + 102 * round(i/2)), module, CLACOS::WAVEFORM_PARAM + i, 0, 3, 0));
			addInput(createInput<TinyPJ301MPort>(Vec(22 + 74 * (i%2), 196 + 102 * round(i/2)), module, CLACOS::WAVEFORM_INPUT + i));
			addInput(createInput<TinyPJ301MPort>(Vec(40 + 74 * (i%2), 196 + 102 * round(i/2)), module, CLACOS::DIST_X_INPUT + i));
			addInput(createInput<TinyPJ301MPort>(Vec(57 + 74 * (i%2), 196 + 102 * round(i/2)), module, CLACOS::DIST_Y_INPUT + i));
		}
	}

	addParam(createParam<CKSS>(Vec(15, 80), module, CLACOS::MODE_PARAM, 0.0, 1.0, 1.0));
	addParam(createParam<CKSS>(Vec(119, 80), module, CLACOS::SYNC_PARAM, 0.0, 1.0, 1.0));

	addParam(createParam<BidooLargeBlueKnob>(Vec(56, 45), module, CLACOS::PITCH_PARAM, -54, 54, 0));
  addParam(createParam<BidooBlueTrimpot>(Vec(114,45), module, CLACOS::FINE_PARAM, -1, 1, 0));
	addParam(createParam<BidooBlueTrimpot>(Vec(18,45), module, CLACOS::FM_PARAM, 0, 1, 0));
	addInput(createInput<TinyPJ301MPort>(Vec(38, 83), module, CLACOS::FM_INPUT));

  addInput(createInput<PJ301MPort>(Vec(11, 330), module, CLACOS::PITCH_INPUT));
	addInput(createInput<PJ301MPort>(Vec(45, 330), module, CLACOS::SYNC_INPUT));
	addOutput(createOutput<PJ301MPort>(Vec(114, 330), module, CLACOS::MAIN_OUTPUT));

	addChild(createScrew<ScrewSilver>(Vec(15, 0)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createScrew<ScrewSilver>(Vec(15, 365)));
	addChild(createScrew<ScrewSilver>(Vec(box.size.x-30, 365)));
}
