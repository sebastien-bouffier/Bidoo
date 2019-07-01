#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/resampler.hpp"
#include "dsp/filter.hpp"

using namespace std;

extern float sawTable[2048];
extern float triTable[2048];

const int OVER = 16;
const int QUAL = 16;

struct TIARE : Module {
	enum ParamIds {
		PITCH_PARAM,
		FINE_PARAM,
		DIST_X_PARAM,
		DIST_Y_PARAM,
		MODE_PARAM,
		SYNC_PARAM,
		FM_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		PITCH_INPUT,
		SYNC_INPUT,
		DIST_X_INPUT,
		DIST_Y_INPUT,
		FM_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		SIN_OUTPUT,
		TRI_OUTPUT,
		SAW_OUTPUT,
		SQR_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};
	bool analog = false;
	bool soft = false;
	float lastSyncValue = 0.0f;
	float phase = 0.0f, phaseDist = 0.0f;
	float phaseDistX = 0.5f, phaseDistY = 0.5f;
	float freq;
	float pitch;
	bool syncEnabled = false;
	bool syncDirection = false;
	int freqFactor = 1;

	dsp::Decimator<OVER, QUAL> sinDecimator;
	dsp::Decimator<OVER, QUAL> triDecimator;
	dsp::Decimator<OVER, QUAL> sawDecimator;
	dsp::Decimator<OVER, QUAL> sqrDecimator;
	dsp::RCFilter sqrFilter;

	// For analog detuning effect
	float pitchSlew = 0.0f;
	int pitchSlewIndex = 0;

	float sinBuffer[OVER] = { 0.0f };
	float triBuffer[OVER] = { 0.0f };
	float sawBuffer[OVER] = { 0.0f };
	float sqrBuffer[OVER] = { 0.0f };

	void setPitch(float pitchKnob, float pitchCv, int factor) {
		// Compute frequency
		pitch = pitchKnob;
		if (analog) {
			// Apply pitch slew
			const float pitchSlewAmount = 3.0f;
			pitch += pitchSlew * pitchSlewAmount;
		} else {
			// Quantize coarse knob if digital mode
			pitch = std::round(pitch);
		}
		pitch += pitchCv;
		// Note C3
		freq = dsp::FREQ_C4 * std::pow(2.0f, pitch / 12.0f) / factor;
	}

	void process(float deltaTime, float syncValue) {
		if (analog) {
			// Adjust pitch slew
			if (++pitchSlewIndex > 32) {
				const float pitchSlewTau = 100.0f; // Time constant for leaky integrator in seconds
				pitchSlew += (random::normal() - pitchSlew / pitchSlewTau) / deltaTime;
				pitchSlewIndex = 0;
			}
		}

		// Advance phase
		float deltaPhase = clamp(freq * deltaTime, 1e-6f, 0.5f);

		// Detect sync
		int syncIndex = -1; // Index in the oversample loop where sync occurs [0, OVERSAMPLE)
		float syncCrossing = 0.0f; // Offset that sync occurs [0.0, 1.0)
		if (syncEnabled) {
			syncValue -= 0.01f;
			if (syncValue > 0.0f && lastSyncValue <= 0.0f) {
				float deltaSync = syncValue - lastSyncValue;
				syncCrossing = 1.0f - syncValue / deltaSync;
				syncCrossing *= OVER;
				syncIndex = (int)syncCrossing;
				syncCrossing -= syncIndex;
			}
			lastSyncValue = syncValue;
		}

		if (syncDirection)
			deltaPhase *= -1.0f;

		sqrFilter.setCutoff(40.0f * deltaTime);

		for (int i = 0; i < OVER; i++) {
			if (syncIndex == i) {
				if (soft) {
					syncDirection = !syncDirection;
					deltaPhase *= -1.0f;
				} else {
					phase = 0.0f;
					phaseDist = 0.0f;
				}
			}

			if (analog) {
				// Quadratic approximation of sine, slightly richer harmonics
				if (phaseDist < 0.5f)
					sinBuffer[i] = 1.f - 16.f * std::pow(phaseDist - 0.25f, 2);
				else
					sinBuffer[i] = -1.f + 16.f * std::pow(phaseDist - 0.75f, 2);
				sinBuffer[i] *= 1.08f;
			} else {
				sinBuffer[i] = std::sin(2.f * M_PI * phaseDist);
			}
			if (analog) {
				triBuffer[i] = 1.25f * interpolateLinear(triTable, phaseDist * 2047.f);
			} else {
				if (phaseDist < 0.25f)
					triBuffer[i] = 4.f * phaseDist;
				else if (phaseDist < 0.75f)
					triBuffer[i] = 2.f - 4.f * phaseDist;
				else
					triBuffer[i] = -4.f + 4.f * phaseDist;
			}
			if (analog) {
				sawBuffer[i] = 1.66f * interpolateLinear(sawTable, phaseDist * 2047.f);
			} else {
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

			// Advance phase
			phase += deltaPhase / OVER;
			// if ((phase>1) || (phase<-1))
			// 	phaseDist = 0.0f;
			phase = eucMod(phase, 1.0f);

			if (phase <= phaseDistX)
				phaseDist = phaseDist + (deltaPhase / OVER) * phaseDistY / phaseDistX;
			else
				phaseDist = phaseDist + (deltaPhase / OVER) * (1.0f - phaseDistY) / (1.0f - phaseDistX);

			phaseDist = eucMod(phaseDist, 1.0f);

		}
	}

	float sin() {
		return sinDecimator.process(sinBuffer);
	}
	float tri() {
		return triDecimator.process(triBuffer);
	}
	float saw() {
		return sawDecimator.process(sawBuffer);
	}
	float sqr() {
		return sqrDecimator.process(sqrBuffer);
	}
	float light() {
		return std::sin(2.0f*M_PI * phase);
	}

	///tooltip
	struct tpMode : ParamQuantity {
		std::string getDisplayValueString() override {
			if (getValue() > 0.f)
				return "Analogue Unavailble";
			else
				return "Digital";
		}
	};
	struct tpSync : ParamQuantity {
		std::string getDisplayValueString() override {
			if (getValue() > 0.f)
				return "Hard";
			else
				return "Soft";
		}
	};

	TIARE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam<tpMode>(MODE_PARAM, 0.f, 1.f, 0.f, "Mode");
		configParam<tpSync>(SYNC_PARAM, 0.f, 1.f, 1.f, "Sync");
		configParam(PITCH_PARAM, -54.f, 54.f, 0.f, "Frequency", " Hz", std::pow(2.f, 1.f / 12.f), dsp::FREQ_C4);
		configParam(FINE_PARAM, -1.f, 1.f, 0.f, "Fine tune", " cents", 0.f, 50.f);
		configParam(FM_PARAM, 0.f, 1.f, 0.f, "Frequency modulation", "%", 0.f, 100.f);
	}

	void process(const ProcessArgs &args) override {
		analog = params[MODE_PARAM].getValue() == 1.0f;
		soft = params[SYNC_PARAM].getValue() <= 0.0f;
		if (inputs[DIST_X_INPUT].isConnected())
			phaseDistX = rescale(clamp(inputs[DIST_X_INPUT].getVoltage(), 0.0f, 10.0f), 0.0f, 10.0f, 0.01f, 0.98f);

		if (inputs[DIST_Y_INPUT].isConnected())
			phaseDistY = rescale(clamp(inputs[DIST_Y_INPUT].getVoltage(), 0.0f, 10.0f), 0.0f, 10.0f, 0.01f, 0.98f);

		float pitchFine = 3.0f * dsp::quadraticBipolar(params[FINE_PARAM].getValue());
		float pitchCv = 12.0f * inputs[PITCH_INPUT].getVoltage();
		if (inputs[FM_INPUT].isConnected()) {
			pitchCv += dsp::quadraticBipolar(params[FM_PARAM].getValue()) * 12.0f * inputs[FM_INPUT].getVoltage();
		}
		setPitch(params[PITCH_PARAM].getValue(), pitchFine + pitchCv, freqFactor);
		syncEnabled = inputs[SYNC_INPUT].isConnected();

		process(1.0f / args.sampleRate, inputs[SYNC_INPUT].getVoltage());

		// Set output
		if (outputs[SIN_OUTPUT].isConnected())
			outputs[SIN_OUTPUT].setVoltage(5.0f * sin());
		if (outputs[TRI_OUTPUT].isConnected())
			outputs[TRI_OUTPUT].setVoltage(5.0f * tri());
		if (outputs[SAW_OUTPUT].isConnected())
			outputs[SAW_OUTPUT].setVoltage(5.0f * saw());
		if (outputs[SQR_OUTPUT].isConnected())
			outputs[SQR_OUTPUT].setVoltage(5.0f * sqr());
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "phaseDistX", json_real(phaseDistX));
		json_object_set_new(rootJ, "phaseDistY", json_real(phaseDistY));
		json_object_set_new(rootJ, "freqFactor", json_integer(freqFactor));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		json_t *phaseDistXJ = json_object_get(rootJ, "phaseDistX");
		if (phaseDistXJ) {
			phaseDistX = json_number_value(phaseDistXJ);
		}
		json_t *phaseDistYJ = json_object_get(rootJ, "phaseDistY");
		if (phaseDistYJ) {
			phaseDistY = json_number_value(phaseDistYJ);
		}
		json_t *freqFactorJ = json_object_get(rootJ, "freqFactor");
		if (freqFactorJ) {
			freqFactor = json_integer_value(freqFactorJ);
		}
	}

	void onRandomize() override {
		if (!inputs[TIARE::DIST_X_INPUT].isConnected()) {
			phaseDistX = random::uniform();
		}
		if (!inputs[TIARE::DIST_Y_INPUT].isConnected()) {
			phaseDistY = random::uniform();
		}
	}

	void onReset() override {
		if (!inputs[TIARE::DIST_X_INPUT].isConnected()) {
			phaseDistX = 0.5f;
		}
		if (!inputs[TIARE::DIST_Y_INPUT].isConnected()) {
			phaseDistY = 0.5f;
		}
	}
};

struct TIAREDisplay : TransparentWidget {
	TIARE *module;
	float initX = 0.0f;
	float initY = 0.0f;
	float dragX = 0.0f;
	float dragY = 0.0f;

	TIAREDisplay() {}

	void onDragStart(const event::DragStart &e) override {
		dragX = APP->scene->rack->mousePos.x;
		dragY = APP->scene->rack->mousePos.y;
	}

	void onDragMove(const event::DragMove &e) override {
		if (!module->inputs[TIARE::DIST_X_INPUT].isConnected()) {
			float newDragX = APP->scene->rack->mousePos.x;
			module->phaseDistX = rescale(clamp(initX + (newDragX - dragX), 0.0f, 140.0f), 0.0f, 140.0f, 0.01f, 0.98f);
		}
		if (!module->inputs[TIARE::DIST_Y_INPUT].isConnected()) {
			float newDragY = APP->scene->rack->mousePos.y;
			module->phaseDistY = rescale(clamp(initY - (newDragY - dragY), 0.0f, 140.0f), 0.0f, 140.0f, 0.01f, 1.0f);
		}
	}

	void onButton(const event::Button &e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT) {
			e.consume(this);
			e.setTarget(this);
			initX = e.pos.x;
			initY = 140.0f - e.pos.y;
		}
	}

	void draw(const DrawArgs &args) override {
		// Draw ref lines
		nvgStrokeColor(args.vg, nvgRGBA(0xff, 0xff, 0xff, 0x80));
		{
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, 0, 70);
			nvgLineTo(args.vg, 140, 70);
			nvgMoveTo(args.vg, 70, 0);
			nvgLineTo(args.vg, 70, 140);
			nvgClosePath(args.vg);
		}
		nvgStroke(args.vg);

		// Draw phase distortion
		nvgStrokeColor(args.vg, nvgRGBA(42, 87, 117, 255));
		{
			nvgBeginPath(args.vg);
			nvgMoveTo(args.vg, 0, 140);
			nvgLineTo(args.vg, (int)(rescale(module->phaseDistX, 0, 1, 0, 140)), 140 - (int)(rescale(module->phaseDistY, 0, 1, 0.01, 140)));
			nvgMoveTo(args.vg, (int)(rescale(module->phaseDistX, 0, 1, 0, 140)), 140 - (int)(rescale(module->phaseDistY, 0, 1, 0.01, 140)));
			nvgLineTo(args.vg, 140, 0);
			nvgClosePath(args.vg);
		}
		nvgStroke(args.vg);
	}
};

struct moduleModeItem : MenuItem {
	TIARE *mode;
	void onAction(const event::Action &e) override {
		mode->freqFactor = mode->freqFactor == 1 ? 100 : 1;
	}
};

struct TIAREWidget : ModuleWidget {

	TIAREWidget(TIARE *module) {

		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/TIARE.svg")));

		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
		addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

		if (module) {
			{
				TIAREDisplay *display = new TIAREDisplay();
				display->module = module;
				display->box.pos = Vec(5, 119);
				display->box.size = Vec(140, 140);
				addChild(display);
			}
		}

		addParam(createParam<CKSS>(Vec(15, 80), module, TIARE::MODE_PARAM));
		addParam(createParam<CKSS>(Vec(119, 80), module, TIARE::SYNC_PARAM));

		addParam(createParam<BidooLargeBlueKnob>(Vec(57, 45), module, TIARE::PITCH_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(114, 45), module, TIARE::FINE_PARAM));
		addParam(createParam<BidooBlueTrimpot>(Vec(18, 45), module, TIARE::FM_PARAM));

		addInput(createInput<TinyPJ301MPort>(Vec(38, 83), module, TIARE::FM_INPUT));
		addInput(createInput<PJ301MPort>(Vec(11, 276), module, TIARE::PITCH_INPUT));
		addInput(createInput<PJ301MPort>(Vec(45, 276), module, TIARE::SYNC_INPUT));
		addInput(createInput<PJ301MPort>(Vec(80, 276), module, TIARE::DIST_X_INPUT));
		addInput(createInput<PJ301MPort>(Vec(114, 276), module, TIARE::DIST_Y_INPUT));

		addOutput(createOutput<PJ301MPort>(Vec(11, 320), module, TIARE::SIN_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(45, 320), module, TIARE::TRI_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(80, 320), module, TIARE::SAW_OUTPUT));
		addOutput(createOutput<PJ301MPort>(Vec(114, 320), module, TIARE::SQR_OUTPUT));
	}

	void appendContextMenu(Menu *menu) override {
		TIARE *mode = dynamic_cast<TIARE*>(this->module);
		menu->addChild(new MenuEntry);
		moduleModeItem *modeItem = new moduleModeItem;
		modeItem->text = "Mode: ";
		modeItem->rightText = mode->freqFactor == 1 ? "OSC✔ LFO" : "OSC  LFO✔";
		modeItem->mode = mode;
		menu->addChild(modeItem);

	}
};

Model *modelTIARE = createModel<TIARE, TIAREWidget>("TiARE");
