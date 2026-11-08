// FiLTRe: all 71 filter types of the Potion synth, the same code (src/dep/potion/Filter.h), with a screen showing the
// spectrum of the signal and the measured curve of the selected filter. Stereo, up to 16 voices.
#include "plugin.hpp"
#include "FiltreEngine.hpp"
#include "Svf.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

struct FILTRE : BidooModule {
	enum ParamIds {
		TYPE_PARAM,
		CUTOFF_PARAM,
		RES_PARAM,
		DRIVE_PARAM,
		MORPH_PARAM,
		MIX_PARAM,
		TYPE_CV_PARAM,
		CUTOFF_CV_PARAM,
		RES_CV_PARAM,
		DRIVE_CV_PARAM,
		MORPH_CV_PARAM,
		MIX_CV_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		INL_INPUT,
		INR_INPUT,
		TYPE_INPUT,
		CUTOFF_INPUT,
		RES_INPUT,
		DRIVE_INPUT,
		MORPH_INPUT,
		MIX_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUTL_OUTPUT,
		OUTR_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	static constexpr float kVolts = 5.f;  // 5 V is the filter's 1.0
	static constexpr int kMaxVoices = 16;

	filtre::Voice voices[kMaxVoices];
	float mixes[kMaxVoices] = {};
	int channels = 1, run = 1, lastRun = 0;
	int tick = 0;
	int rate = 0;  // saved: 0, 1, 2 = every 16, 4, 1 samples
	int idle = 0;  // samples of silence with nothing patched in
	bool anyIn = false;

	// for the screen: the rings of the signal, voice 1's settings
	filtre::CaptureRing ringIn, ringOut;
	std::atomic<int> shownId {1};
	std::atomic<float> shownCut {1000.f}, shownRes {0.f}, shownDrive {0.f}, shownMorph {0.f}, shownMix {1.f};
	std::atomic<float> sampleRate {48000.f};

	// The tooltip says what Morph does for the type
	struct MorphQuantity : ParamQuantity {
		std::string getLabel() override {
			FILTRE *m = dynamic_cast<FILTRE *>(module);
			if (!m)
				return "Morph";
			const char *what = filtre::typeAt(filtre::positionOfId(m->shownId.load())).morph;
			return what ? std::string("Morph: ") + what : "Morph (not used by this type)";
		}
	};

	FILTRE() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		std::vector<std::string> names;
		for (int i = 0; i < filtre::kNumTypes; i++)
			names.push_back(filtre::typeAt(i).name);
		configSwitch(TYPE_PARAM, 0.f, filtre::kNumTypes - 1, (float)filtre::positionOfId(1), "Type", names);
		configParam(CUTOFF_PARAM, 0.f, 1.f, 0.6f, "Cutoff", " Hz", 1000.f, 20.f);
		configParam(RES_PARAM, 0.f, 1.f, 0.1f, "Resonance", "%", 0.f, 100.f);
		configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);
		configParam<MorphQuantity>(MORPH_PARAM, 0.f, 1.f, 0.f, "Morph", "%", 0.f, 100.f);
		configParam(MIX_PARAM, 0.f, 1.f, 1.f, "Mix", "%", 0.f, 100.f);
		configParam(TYPE_CV_PARAM, -1.f, 1.f, 1.f, "Type CV amount (100%: 1 V = 7 types)", "%", 0.f, 100.f);
		configParam(CUTOFF_CV_PARAM, -1.f, 1.f, 1.f, "Cutoff CV amount (100%: 1 V/oct)", "%", 0.f, 100.f);
		configParam(RES_CV_PARAM, -1.f, 1.f, 1.f, "Resonance CV amount (100%: 10 V = full range)", "%", 0.f, 100.f);
		configParam(DRIVE_CV_PARAM, -1.f, 1.f, 1.f, "Drive CV amount (100%: 10 V = full range)", "%", 0.f, 100.f);
		configParam(MORPH_CV_PARAM, -1.f, 1.f, 1.f, "Morph CV amount (100%: 10 V = full range)", "%", 0.f, 100.f);
		configParam(MIX_CV_PARAM, -1.f, 1.f, 1.f, "Mix CV amount (100%: 10 V = full range)", "%", 0.f, 100.f);
		configInput(INL_INPUT, "Left (polyphonic)");
		configInput(INR_INPUT, "Right (polyphonic; unpatched: the left)");
		configInput(TYPE_INPUT, "Type CV (polyphonic)");
		configInput(CUTOFF_INPUT, "Cutoff CV, V/oct (polyphonic)");
		configInput(RES_INPUT, "Resonance CV (polyphonic)");
		configInput(DRIVE_INPUT, "Drive CV (polyphonic)");
		configInput(MORPH_INPUT, "Morph CV (polyphonic)");
		configInput(MIX_INPUT, "Mix CV (polyphonic)");
		configOutput(OUTL_OUTPUT, "Left");
		configOutput(OUTR_OUTPUT, "Right");
		configBypass(INL_INPUT, OUTL_OUTPUT);
		configBypass(INR_INPUT, OUTR_OUTPUT);
		prepare(APP->engine->getSampleRate());
	}

	void prepare(float fs) {
		sampleRate = fs;
		for (auto &v : voices)
			v.prepare(fs);
		lastRun = 0;
		tick = 0;
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		prepare(e.sampleRate);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "rate", json_integer(rate));
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		if (json_t *j = json_object_get(rootJ, "rate"))
			rate = clamp((int)json_integer_value(j), 0, 2);
	}

	// As Potion: filters set up every 16 samples unless asked otherwise
	void control() {
		const bool anyOut = outputs[OUTL_OUTPUT].isConnected() || outputs[OUTR_OUTPUT].isConnected();
		anyIn = inputs[INL_INPUT].isConnected() || inputs[INR_INPUT].isConnected();
		channels = clamp(std::max(inputs[INL_INPUT].getChannels(), inputs[INR_INPUT].getChannels()), 1, kMaxVoices);
		run = anyOut ? channels : 1;  // nothing patched out: only what the screen needs
		for (int c = lastRun; c < run; c++)
			voices[c].reset();        // a voice that was not playing starts from silence
		lastRun = run;
		outputs[OUTL_OUTPUT].setChannels(channels);
		outputs[OUTR_OUTPUT].setChannels(channels);

		const float typeKnob = params[TYPE_PARAM].getValue(), typeAmount = params[TYPE_CV_PARAM].getValue() * 7.f;
		const float cutKnob = 20.f * std::pow(1000.f, params[CUTOFF_PARAM].getValue()), cutAmount = params[CUTOFF_CV_PARAM].getValue();
		const float resKnob = params[RES_PARAM].getValue(), resAmount = params[RES_CV_PARAM].getValue() * 0.1f;
		const float driveKnob = params[DRIVE_PARAM].getValue(), driveAmount = params[DRIVE_CV_PARAM].getValue() * 0.1f;
		const float morphKnob = params[MORPH_PARAM].getValue(), morphAmount = params[MORPH_CV_PARAM].getValue() * 0.1f;
		const float mixKnob = params[MIX_PARAM].getValue(), mixAmount = params[MIX_CV_PARAM].getValue() * 0.1f;
		const bool cutPatched = inputs[CUTOFF_INPUT].isConnected();
		for (int c = 0; c < run; c++) {
			const int position = clamp((int)std::round(typeKnob + typeAmount * cv(TYPE_INPUT, c)), 0, filtre::kNumTypes - 1);
			const int id = filtre::typeAt(position).id;
			const float cut = cutPatched ? cutKnob * std::pow(2.f, cutAmount * cv(CUTOFF_INPUT, c)) : cutKnob;
			const float res = clamp(resKnob + resAmount * cv(RES_INPUT, c), 0.f, 1.f);
			const float drive = clamp(driveKnob + driveAmount * cv(DRIVE_INPUT, c), 0.f, 1.f);
			const float morph = clamp(morphKnob + morphAmount * cv(MORPH_INPUT, c), 0.f, 1.f);
			voices[c].set(id, cut, res, drive, morph);
			mixes[c] = clamp(mixKnob + mixAmount * cv(MIX_INPUT, c), 0.f, 1.f);
			if (c == 0) {
				shownId.store(id, std::memory_order_relaxed);
				shownCut.store(clamp(cut, 15.f, 0.45f * sampleRate.load(std::memory_order_relaxed)), std::memory_order_relaxed);
				shownRes.store(res, std::memory_order_relaxed);
				shownDrive.store(drive, std::memory_order_relaxed);
				shownMorph.store(morph, std::memory_order_relaxed);
				shownMix.store(mixes[0], std::memory_order_relaxed);
			}
		}
	}

	float cv(int input, int c) {
		const float v = inputs[input].getPolyVoltage(c);
		return std::isfinite(v) ? v : 0.f;
	}

	void process(const ProcessArgs &args) override {
		static const int kTicks[3] = {16, 4, 1};
		if (--tick <= 0) {
			tick = kTicks[rate];
			control();
		}
		// nothing patched in and the tail is over: nothing to do
		if (!anyIn && idle > (int)args.sampleRate) {
			for (int c = 0; c < channels; c++) {
				outputs[OUTL_OUTPUT].setVoltage(0.f, c);
				outputs[OUTR_OUTPUT].setVoltage(0.f, c);
			}
			return;
		}
		const bool rPatched = inputs[INR_INPUT].isConnected();
		float sumIn = 0.f, sumOut = 0.f;
		bool quiet = true;
		for (int c = 0; c < run; c++) {
			float xl = inputs[INL_INPUT].getPolyVoltage(c) / kVolts;
			float xr = rPatched ? inputs[INR_INPUT].getPolyVoltage(c) / kVolts : xl;
			if (!std::isfinite(xl))
				xl = 0.f;
			if (!std::isfinite(xr))
				xr = 0.f;
			float l = xl, r = xr;
			voices[c].process(l, r);
			if (!std::isfinite(l) || !std::isfinite(r)) {
				voices[c].reset();
				l = xl;
				r = xr;
			}
			const float mix = mixes[c];
			l = xl + mix * (l - xl);
			r = xr + mix * (r - xr);
			outputs[OUTL_OUTPUT].setVoltage(bidooSoftLimit(l * kVolts), c);
			outputs[OUTR_OUTPUT].setVoltage(bidooSoftLimit(r * kVolts), c);
			sumIn += xl + xr;
			sumOut += l + r;
			if (std::fabs(l) + std::fabs(r) > 1.0e-5f)
				quiet = false;
		}
		ringIn.push(0.5f * sumIn);
		ringOut.push(0.5f * sumOut);
		idle = anyIn || !quiet ? 0 : idle + 1;
	}
};

#include "FiltreWidget.hpp"

Model *modelFILTRE = createModel<FILTRE, FILTREWidget>("FILTRE");
