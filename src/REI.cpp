#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include "Svf.hpp"
#include "dep/bidoo-dsp/bidoo/dsp/reverb/Prism.h"
#include "dep/bidoo-dsp/bidoo/dsp/reverb/DuckGate.h"

using namespace std;

using bidoo::dsp::reverb::PrismReverb;
using bidoo::dsp::reverb::DuckGate;

// REI: the Prism reverb of FadaBox (eight algorithms and a shimmer), stereo, in Prism's dense mode: the
// network algorithms on 16 lines with diffused early reflections.
// Since 2026-11-26 (22 HP) it has Grotte's controls (cabanon plugins/Grotte): STYLE (Prism's algorithms), SIZE and
// STRETCH (the decay time: the size's own time x 25 % to 400 %), PREDELAY with SYNC and OFFSET, MOTION, DENSITY, DEPTH,
// AIR, WIDTH, BUILD, MIX, the shimmer's amount, TONE and INTERVAL, DUCK, the auto GATE with HOLD and SYNC, FREEZE,
// input and output levels and pans; a CLOCK input for the SYNCs (one pulse per quarter note). Grotte's decay and tail
// equalisers are not here.
// Prism works on blocks: the wet signal comes 32 samples (0.7 ms at 48 kHz) late.
struct REI : BidooModule {
	// The ids of the 7 HP REI stay where they were (old patches load); what some of them do changed on 2026-11-26 and
	// fromJson converts an older patch.
	enum ParamIds {
		STRETCH_PARAM,   // was DECAY: 0.5 = 100 %
		AIR_PARAM,       // was DAMP (inverted)
		FREEZE_PARAM,    // a latched button now
		WIDTH_PARAM,
		INPUT_PARAM,     // was DRY
		MIX_PARAM,       // was WET
		SHIMMER_PARAM,
		INTERVAL_PARAM,
		CLIPPING_PARAM,
		BUILDUP_PARAM,
		MOTION_PARAM,    // was MOD
		TONE_PARAM,
		SIZE_PARAM,      // the first id of this REI: a patch with it was saved by this REI
		STYLE_PARAM,
		PREDELAY_PARAM,
		PRESYNC_PARAM,
		PREOFFSET_PARAM,
		DENSITY_PARAM,
		DEPTH_PARAM,
		DUCK_PARAM,
		GATE_PARAM,
		HOLD_PARAM,
		GATESYNC_PARAM,
		OUTPUT_PARAM,
		INPAN_PARAM,
		OUTPAN_PARAM,
		NUM_PARAMS
	};
	enum InputIds {
		IN_L_INPUT,
		IN_R_INPUT,
		STRETCH_INPUT,
		AIR_INPUT,
		FREEZE_INPUT,
		WIDTH_INPUT,
		SHIMMER_INPUT,
		INTERVAL_INPUT,
		DUCK_INPUT,      // was DRY CV
		MIX_INPUT,       // was WET CV
		BUILDUP_INPUT,
		MOTION_INPUT,
		TONE_INPUT,
		SIZE_INPUT,
		STYLE_INPUT,
		PREDELAY_INPUT,
		DENSITY_INPUT,
		DEPTH_INPUT,
		HOLD_INPUT,
		GATE_INPUT,
		CLOCK_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		OUT_L_OUTPUT,
		OUT_R_OUTPUT,
		NUM_OUTPUTS
	};
	enum LightIds {
		NUM_LIGHTS
	};

	static constexpr int BLOCK = 32;
	static constexpr int NUM_INTERVALS = 10;
	static constexpr float MAX_PREDELAY_MS = 300.f;   // Prism's limit
	// the 7 HP REI's menus, for its patches
	static const std::vector<float> &legacyPreDelays() {
		static const std::vector<float> v {0.f, 10.f, 20.f, 40.f, 80.f, 150.f};
		return v;
	}
	static const std::vector<float> &legacyMods() {
		static const std::vector<float> v {0.f, 0.15f, 0.3f, 0.5f, 0.75f, 1.f};
		return v;
	}
	// Grotte's sync choices (cabanon plugins/Grotte/GrotteEngine.h): predelay Off, 1/4 ... 1/32; the gate's in quarter notes
	static const std::vector<std::string> &preSyncNames() {
		static const std::vector<std::string> n {"Off", "1/4", "1/8", "1/16", "1/32"};
		return n;
	}
	static const std::vector<std::string> &gateSyncNames() {
		static const std::vector<std::string> n {"Off", "2/1", "1/1", "1/2", "1/2.", "1/2T", "1/4", "1/4.", "1/4T", "1/8", "1/8.", "1/8T", "1/16", "1/16.", "1/16T", "1/32"};
		return n;
	}
	static float gateSyncQuarters(int i) {
		static const float q[] {0.f, 8.f, 4.f, 2.f, 3.f, 4.f / 3.f, 1.f, 1.5f, 2.f / 3.f, .5f, .75f, 1.f / 3.f, .25f, .375f, 1.f / 6.f, .125f};
		return q[clamp(i, 0, 15)];
	}
	// knob <-> ms: PREDELAY 300 v^3 (37.5 ms in the middle), HOLD 10 x 200^v
	static float predelayMs(float v) { return MAX_PREDELAY_MS * v * v * v; }
	static float predelayKnob(float ms) { return std::cbrt(clamp(ms, 0.f, MAX_PREDELAY_MS) / MAX_PREDELAY_MS); }
	static float holdMs(float v) { return 10.f * std::pow(200.f, v); }

	// knob <-> dB: DUCK 30 v^2 (VCV's levels are hot, so the dip is nearly always full: a linear knob went from none to too
	// much in its first fifth; now 1.2 dB at 20 %, 7.5 dB at 50 %)
	static float duckDb(float v) { return 30.f * v * v; }
	struct DuckQuantity : ParamQuantity {
		float getDisplayValue() override { return duckDb(getValue()); }
		void setDisplayValue(float db) override { setValue(std::sqrt(clamp(db, 0.f, 30.f) / 30.f)); }
	};

	struct PredelayQuantity : ParamQuantity {
		float getDisplayValue() override { return predelayMs(getValue()); }
		void setDisplayValue(float ms) override { setValue(predelayKnob(ms)); }
	};

	PrismReverb prism;
	PrismReverb::Settings settings;
	DuckGate duckGate;
	float inL[BLOCK] = {}, inR[BLOCK] = {}, wetL[BLOCK] = {}, wetR[BLOCK] = {};
	int pos = 0;
	// idle: input and wet output under -100 dB for IDLE_SECONDS, then Prism is
	// skipped (and while no output is patched) until the input moves again
	static constexpr float IDLE_LEVEL = 1e-5f;  // Prism units, 1 = 5 V
	static constexpr float IDLE_SECONDS = 1.5f;
	int quietBlocks = 0;
	bool idle = false;
	float blockPeak = 0.f;

	dsp::SchmittTrigger freezeTrigger, gateTrigger, clockTrigger;
	// the clock: samples between its last two pulses (a quarter note), 0 until two pulses came
	int clockCount = 0, clockPeriod = 0;
	float sampleRate = 48000.f;

	// Prism freezes its network algorithms only. For SPRING, NON-LIN and
	// REVERSE, REI loops the last second of the wet output instead, with a
	// crossfade at the loop point, and shuts the input out, like Prism does.
	std::vector<float> histL, histR;  // the wet output, kept while not looping
	int histW = 0, loopLen = 1, loopX = 1, loopStart = 0, loopPos = 0;
	bool looping = false;
	float loopMix = 0.f, loopStep = 0.f;
	// smoothed gains (20 ms): dry, wet, input and output levels with their pans
	float gDry = 1.f, gWet = 0.f, gInL = 1.f, gInR = 1.f, gOutL = 1.f, gOutR = 1.f, smoothA = 0.f;
	// the level knobs in dB and their gains, worked out again only when a knob moves
	float inDb = 0.f, inGain = 1.f, outDb = 0.f, outGain = 1.f;
	// an older patch's menu values, read by dataFromJson for fromJson (-1: none)
	int legacyAlgo = -1, legacyPreDelay = -1;

	REI() {
		config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		std::vector<std::string> styles;
		for (const std::string &n : PrismReverb::algoNames())
			styles.push_back(n);
		configSwitch(STYLE_PARAM, 0.f, PrismReverb::NumAlgos - 1, PrismReverb::Hall, "Style", styles);
		configParam(SIZE_PARAM, 0.f, 1.f, .5f, "Size", "%", 0.f, 100.f);
		configParam(STRETCH_PARAM, 0.f, 1.f, .5f, "Stretch (the decay time against the size's own)", "%", 16.f, 25.f);
		configParam<PredelayQuantity>(PREDELAY_PARAM, 0.f, 1.f, predelayKnob(10.f), "Predelay", " ms");
		configSwitch(PRESYNC_PARAM, 0.f, 4.f, 0.f, "Predelay sync (to CLOCK, or 120 BPM)", preSyncNames());
		configParam(PREOFFSET_PARAM, 0.f, 1.f, .5f, "Predelay offset (synced)", "%", 4.f, 50.f);
		configParam(BUILDUP_PARAM, 0.f, 1.f, 0.f, "Build up (sparse early reflections first, thickening into the tail)", "%", 0.f, 100.f);
		configParam(MOTION_PARAM, 0.f, 1.f, legacyMods()[2], "Motion (the tail's modulation)", "%", 0.f, 100.f);
		configParam(DENSITY_PARAM, 0.f, 1.f, .5f, "Density (diffusion)", "%", 0.f, 100.f);
		configParam(DEPTH_PARAM, 0.f, 1.f, .3f, "Depth (the source from close to far)", "%", 0.f, 100.f);
		configParam(AIR_PARAM, 0.f, 1.f, .6f, "Air (the tail's highs)", "%", 0.f, 100.f);
		configParam(WIDTH_PARAM, 0.f, 1.f, .5f, "Stereo width of the reverb", "%", 0.f, 200.f);
		configParam(MIX_PARAM, 0.f, 1.f, .3f, "Mix", "%", 0.f, 100.f);
		std::vector<std::string> intervals;
		for (const std::string &n : PrismReverb::intervalNames())
			intervals.push_back(n);
		configSwitch(INTERVAL_PARAM, 0.f, NUM_INTERVALS - 1, 4.f, "Shimmer interval", intervals);
		configParam(SHIMMER_PARAM, 0.f, 1.f, 0.f, "Shimmer", "%", 0.f, 100.f);
		configParam(TONE_PARAM, 0.f, 1.f, .4f, "Shimmer tone (its high cut, darker each pass)", " kHz", 12.f, 1.f);
		configParam<DuckQuantity>(DUCK_PARAM, 0.f, 1.f, 0.f, "Ducking (the reverb dips while the input plays)", " dB");
		configSwitch(GATE_PARAM, 0.f, 1.f, 0.f, "Auto gate (the tail closes when the input stops)", {"Off", "On"});
		configParam(HOLD_PARAM, 0.f, 1.f, std::log(25.f) / std::log(200.f), "Gate hold", " ms", 200.f, 10.f);
		configSwitch(GATESYNC_PARAM, 0.f, 15.f, 0.f, "Gate hold sync (to CLOCK, or 120 BPM)", gateSyncNames());
		configSwitch(FREEZE_PARAM, 0.f, 1.f, 0.f, "Freeze", {"Off", "On"});
		configParam(INPUT_PARAM, -24.f, 24.f, 0.f, "Input level", " dB");
		configParam(OUTPUT_PARAM, -24.f, 24.f, 0.f, "Output level", " dB");
		configParam(INPAN_PARAM, -1.f, 1.f, 0.f, "Input pan", "%", 0.f, 100.f);
		configParam(OUTPAN_PARAM, -1.f, 1.f, 0.f, "Output pan", "%", 0.f, 100.f);
		configSwitch(CLIPPING_PARAM, 0, 1, 0, "Output limit", {"Soft (under 10 V)", "Hard clip at 10 V"});
		configInput(IN_L_INPUT, "Left");
		configInput(IN_R_INPUT, "Right (follows left when unpatched)");
		configInput(STYLE_INPUT, "Style CV (1 V per style)");
		configInput(SIZE_INPUT, "Size CV (0-10 V: full range)");
		configInput(STRETCH_INPUT, "Stretch CV (0-10 V: full range)");
		configInput(PREDELAY_INPUT, "Predelay CV (0-10 V: full range)");
		configInput(BUILDUP_INPUT, "Build up CV (0-10 V: full range)");
		configInput(MOTION_INPUT, "Motion CV (0-10 V: full range)");
		configInput(DENSITY_INPUT, "Density CV (0-10 V: full range)");
		configInput(DEPTH_INPUT, "Depth CV (0-10 V: full range)");
		configInput(AIR_INPUT, "Air CV (0-10 V: full range)");
		configInput(WIDTH_INPUT, "Width CV (0-10 V: full range)");
		configInput(MIX_INPUT, "Mix CV (0-10 V: full range)");
		configInput(INTERVAL_INPUT, "Shimmer interval CV (1 V per step)");
		configInput(SHIMMER_INPUT, "Shimmer CV (0-10 V: full range)");
		configInput(TONE_INPUT, "Shimmer tone CV (0-10 V: full range)");
		configInput(DUCK_INPUT, "Ducking CV (0-10 V: full range)");
		configInput(GATE_INPUT, "Auto gate toggle trigger");
		configInput(HOLD_INPUT, "Gate hold CV (0-10 V: full range)");
		configInput(FREEZE_INPUT, "Freeze toggle trigger");
		configInput(CLOCK_INPUT, "Clock (one pulse per quarter note) for the SYNCs");
		configOutput(OUT_L_OUTPUT, "Left");
		configOutput(OUT_R_OUTPUT, "Right");
		configBypass(IN_L_INPUT, OUT_L_OUTPUT);
		configBypass(IN_R_INPUT, OUT_R_OUTPUT);
		// a short glide of the line lengths, so a moving SIZE or STRETCH bends instead of clicking
		settings.glideMs = 100.f;
		prepare(48000.f);
	}

	void prepare(float sr) {
		sampleRate = sr;
		prism.prepare(sr, true);
		duckGate.prepare(sr);
		smoothA = 1.f - std::exp(-1.f / (0.02f * sr));
		loopLen = (int)sr;            // 1 s loop
		loopX = (int)(0.05f * sr);    // 50 ms crossfades
		histL.assign(loopLen + loopX + 1, 0.f);
		histR.assign(loopLen + loopX + 1, 0.f);
		histW = loopPos = loopStart = 0;
		looping = false;
		loopMix = 0.f;
		loopStep = 1.f / loopX;
		pos = 0;
		clockCount = clockPeriod = 0;
	}

	void onSampleRateChange(const SampleRateChangeEvent &e) override {
		prepare(e.sampleRate);
	}

	void onReset() override {
		prism.reset();
		prepare(APP && APP->engine ? APP->engine->getSampleRate() : 48000.f);
	}

	// The wet output, through the loop when it freezes.
	void loopFreeze(bool freeze, int algo, float &oL, float &oR) {
		bool want = freeze && !bidoo::dsp::reverb::prism_detail::isFdn(algo);
		int n = histL.size();
		if (want && !looping) {
			// the last second plus a crossfade's length, from the history
			looping = true;
			loopStart = (histW - (loopLen + loopX) + n) % n;
			loopPos = 0;
		}
		if (!looping) {
			histL[histW] = oL;
			histR[histW] = oR;
			histW = (histW + 1) % n;
			return;
		}
		float lL = histL[(loopStart + loopPos) % n], lR = histR[(loopStart + loopPos) % n];
		if (loopPos < loopX) {
			// the loop's start fades in over the audio that followed its end
			float w = (float)loopPos / loopX * (float)M_PI * 0.5f;
			float gIn = std::sin(w), gOut = std::cos(w);
			int t = (loopStart + loopLen + loopPos) % n;
			lL = lL * gIn + histL[t] * gOut;
			lR = lR * gIn + histR[t] * gOut;
		}
		if (++loopPos >= loopLen)
			loopPos = 0;
		loopMix = want ? std::min(loopMix + loopStep, 1.f) : std::max(loopMix - loopStep, 0.f);
		oL += (lL - oL) * loopMix;
		oR += (lR - oR) * loopMix;
		if (!want && loopMix <= 0.f)
			looping = false;
	}

	// a quarter note in ms: the clock's, else 120 BPM
	float quarterMs() const {
		return clockPeriod > 0 ? 1000.f * clockPeriod / sampleRate : 500.f;
	}

	// Prism's decay knob (0..1 over the style's range) for SIZE and STRETCH: the size gives a time inside the style's
	// range, the stretch multiplies it (x 0.25 to x 4); kept under Prism's FREEZE (a button of its own here).
	static float decayFor(int algo, float size, float stretch01) {
		const float lo = PrismReverb::decaySeconds(algo, 0.f), hi = PrismReverb::decaySeconds(algo, 1.f);
		const float base = lo * std::pow(hi / lo, 0.1f + 0.6f * size);
		const float t = base * 0.25f * std::pow(16.f, stretch01);
		return clamp(std::log(t / lo) / std::log(hi / lo), 0.f, 0.985f);
	}

	float knob(int p, int in, float perVolt = 0.1f) {
		return params[p].getValue() + inputs[in].getVoltage() * perVolt;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);
		// FREEZE does not stay on in a patch: it opens with an empty reverb, and a held empty tail with the input shut
		// out would only be silence (Rack reads the params before the data)
		params[FREEZE_PARAM].setValue(0.f);
		// the 7 HP REI kept its algorithm, pre-delay and modulation in its data (menus)
		legacyAlgo = legacyPreDelay = -1;
		if (json_t *j = json_object_get(rootJ, "algo"))
			legacyAlgo = clamp((int)json_integer_value(j), 0, PrismReverb::NumAlgos - 1);
		if (json_t *j = json_object_get(rootJ, "preDelay"))
			legacyPreDelay = clamp((int)json_integer_value(j), 0, (int)legacyPreDelays().size() - 1);
		if (json_t *j = json_object_get(rootJ, "mod"))
			params[MOTION_PARAM].setValue(legacyMods()[clamp((int)json_integer_value(j), 0, (int)legacyMods().size() - 1)]);
	}

	// Patches of the gverb REI (2.1.1 and older, no "algo" in their data) keep
	// the old knobs' raw values: ROOM SIZE 0-2 (heard up to 1) and PITCH SHIFT
	// 0.5-4 x.
	static float decayFromRoomSize(float size) {
		return 0.9f * clamp(size, 0.f, 1.f);
	}

	static int intervalFromRatio(float ratio) {
		float semis = 12.f * std::log2(clamp(ratio, 0.5f, 4.f));
		if (std::fabs(semis) < 2.5f)
			return 4;  // the old default (1 x, no shift) has no match: the new default, +OCT
		static const float single[] = {-12.f, -7.f, 5.f, 7.f, 12.f, 19.f, 24.f};  // intervals 0-6
		int best = 0;
		for (int i = 1; i < 7; i++)
			if (std::fabs(single[i] - semis) < std::fabs(single[best] - semis))
				best = i;
		return best;
	}

	void fromJson(json_t *rootJ) override {
		BidooModule::fromJson(rootJ);
		convertOlderPatch(rootJ);
	}

	// After the params and the data are read: an older REI's patch (no id from SIZE_PARAM on) converted to these controls
	void convertOlderPatch(json_t *rootJ) {
		// what the patch saved, by id, before the ranges clamped it
		json_t *paramsJ = json_object_get(rootJ, "params");
		float raw[NUM_PARAMS] = {};
		bool has[NUM_PARAMS] = {};
		int maxId = -1;
		size_t i;
		json_t *paramJ;
		json_array_foreach(paramsJ, i, paramJ) {
			json_t *idJ = json_object_get(paramJ, "id");
			json_t *valueJ = json_object_get(paramJ, "value");
			if (!idJ || !valueJ)
				continue;
			int id = (int)json_integer_value(idJ);
			if (id < 0 || id >= NUM_PARAMS)
				continue;
			raw[id] = (float)json_number_value(valueJ);
			has[id] = true;
			maxId = std::max(maxId, id);
		}
		if (maxId >= SIZE_PARAM)
			return;   // saved by this REI
		// the 7 HP REI (up to 2026-11-26): DECAY, DAMP, DRY and WET, the menus; or the gverb REI before it
		json_t *dataJ = json_object_get(rootJ, "data");
		const bool gverb = !(dataJ && json_object_get(dataJ, "algo"));
		if (has[STRETCH_PARAM])
			params[STRETCH_PARAM].setValue(gverb ? decayFromRoomSize(raw[STRETCH_PARAM]) : clamp(raw[STRETCH_PARAM], 0.f, 1.f));
		if (gverb && has[INTERVAL_PARAM])
			params[INTERVAL_PARAM].setValue((float)intervalFromRatio(raw[INTERVAL_PARAM]));
		if (has[AIR_PARAM])
			params[AIR_PARAM].setValue(1.f - clamp(raw[AIR_PARAM], 0.f, 1.f));
		const float dry = has[INPUT_PARAM] ? clamp(raw[INPUT_PARAM], 0.f, 1.f) : 1.f;
		const float wet = has[MIX_PARAM] ? clamp(raw[MIX_PARAM], 0.f, 1.f) : .5f;
		params[INPUT_PARAM].setValue(0.f);
		params[MIX_PARAM].setValue(dry + wet > 0.f ? wet / (dry + wet) : .3f);
		if (legacyAlgo >= 0)
			params[STYLE_PARAM].setValue((float)legacyAlgo);
		params[PREDELAY_PARAM].setValue(predelayKnob(legacyPreDelays()[legacyPreDelay >= 0 ? legacyPreDelay : 1]));
		INFO("REI: a patch of the %s REI converted", gverb ? "gverb" : "7 HP");
	}

	void process(const ProcessArgs &args) override {
		if (freezeTrigger.process(inputs[FREEZE_INPUT].getVoltage()))
			params[FREEZE_PARAM].setValue(params[FREEZE_PARAM].getValue() > .5f ? 0.f : 1.f);
		if (gateTrigger.process(inputs[GATE_INPUT].getVoltage()))
			params[GATE_PARAM].setValue(params[GATE_PARAM].getValue() > .5f ? 0.f : 1.f);
		const bool freeze = params[FREEZE_PARAM].getValue() > .5f;
		// the clock: a quarter note between two pulses (forgotten after 4 s without one)
		if (inputs[CLOCK_INPUT].isConnected()) {
			++clockCount;
			if (clockTrigger.process(inputs[CLOCK_INPUT].getVoltage(), 0.1f, 1.f)) {
				if (clockCount > 1)
					clockPeriod = clockCount;
				clockCount = 0;
			}
			else if (clockCount > (int)(4.f * args.sampleRate))
				clockPeriod = 0;
		}
		else
			clockPeriod = clockCount = 0;
		const int algo = clamp((int)std::round(knob(STYLE_PARAM, STYLE_INPUT, 1.f)), 0, PrismReverb::NumAlgos - 1);

		// input level and pan (a balance), smoothed
		const float inDbNow = params[INPUT_PARAM].getValue();
		if (inDbNow != inDb) {
			inDb = inDbNow;
			inGain = std::pow(10.f, inDb / 20.f);
		}
		const float inG = inGain;
		const float inPan = params[INPAN_PARAM].getValue();
		gInL += smoothA * (inG * std::min(1.f, 1.f - inPan) - gInL);
		gInR += smoothA * (inG * std::min(1.f, 1.f + inPan) - gInR);
		float l = inputs[IN_L_INPUT].getVoltageSum();
		float r = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltageSum() : l;
		l *= gInL;
		r *= gInR;

		// Prism runs at +-1 for +-5 V; a looping freeze shuts the input out, as Prism's own freeze does
		bool shut = freeze && !bidoo::dsp::reverb::prism_detail::isFdn(algo);
		inL[pos] = shut ? 0.f : l * 0.2f;
		inR[pos] = shut ? 0.f : r * 0.2f;
		blockPeak = std::max(blockPeak, std::max(std::max(std::fabs(inL[pos]), std::fabs(inR[pos])), std::max(std::fabs(wetL[pos]), std::fabs(wetR[pos]))));
		float oL = wetL[pos] * 5.0f, oR = wetR[pos] * 5.0f;
		loopFreeze(freeze, algo, oL, oR);
		const float dg = duckGate.process(l * 0.2f, r * 0.2f);
		if (++pos >= BLOCK) {
			pos = 0;
			bool heard = outputs[OUT_L_OUTPUT].isConnected() || outputs[OUT_R_OUTPUT].isConnected();
			if (blockPeak >= IDLE_LEVEL || looping) {
				quietBlocks = 0;
				idle = false;
			}
			else if (!idle && ++quietBlocks * BLOCK > (int)(IDLE_SECONDS * args.sampleRate))
				idle = true;
			blockPeak = 0.f;
			// the ducking and the gate's settings, once per block (their gains glide per sample)
			const int gateSync = (int)params[GATESYNC_PARAM].getValue();
			const float hold = gateSync > 0 ? quarterMs() * gateSyncQuarters(gateSync) : holdMs(clamp(knob(HOLD_PARAM, HOLD_INPUT), 0.f, 1.f));
			duckGate.set(duckDb(clamp(knob(DUCK_PARAM, DUCK_INPUT), 0.f, 1.f)), params[GATE_PARAM].getValue() > .5f, hold);
			if (idle || !heard) {
				std::fill(wetL, wetL + BLOCK, 0.f);
				std::fill(wetR, wetR + BLOCK, 0.f);
			}
			else {
				// synced predelay: 1/4 to 1/32 of the clock x the offset (50 to 200 %)
				const int preSync = (int)params[PRESYNC_PARAM].getValue();
				const float preMs = preSync > 0
					? quarterMs() / (float)(1 << (preSync - 1)) * 0.5f * std::pow(4.f, params[PREOFFSET_PARAM].getValue())
					: predelayMs(clamp(knob(PREDELAY_PARAM, PREDELAY_INPUT), 0.f, 1.f));
				const float size = clamp(knob(SIZE_PARAM, SIZE_INPUT), 0.f, 1.f);
				settings.algo = algo;
				settings.preMs = clamp(preMs, 0.f, MAX_PREDELAY_MS);
				settings.decay = decayFor(algo, size, clamp(knob(STRETCH_PARAM, STRETCH_INPUT), 0.f, 1.f));
				settings.size = size;
				settings.damp = 1.f - clamp(knob(AIR_PARAM, AIR_INPUT), 0.f, 1.f);
				settings.mod = clamp(knob(MOTION_PARAM, MOTION_INPUT), 0.f, 1.f);
				settings.density = clamp(knob(DENSITY_PARAM, DENSITY_INPUT), 0.f, 1.f);
				settings.depth = clamp(knob(DEPTH_PARAM, DEPTH_INPUT), 0.f, 1.f);
				settings.buildUp = clamp(knob(BUILDUP_PARAM, BUILDUP_INPUT), 0.f, 1.f);
				settings.shimmer = clamp(knob(SHIMMER_PARAM, SHIMMER_INPUT), 0.f, 1.f);
				settings.shimmerTone = clamp(knob(TONE_PARAM, TONE_INPUT), 0.f, 1.f);
				settings.interval = clamp((int)std::round(knob(INTERVAL_PARAM, INTERVAL_INPUT, 1.f)), 0, NUM_INTERVALS - 1);
				settings.freeze = freeze;
				prism.process(inL, inR, wetL, wetR, BLOCK, settings);
			}
		}

		// width on the wet only: 0 mono, 100 % as Prism makes it, 200 % twice the side; then the ducking and the gate
		float width = clamp(knob(WIDTH_PARAM, WIDTH_INPUT), 0.f, 1.f) * 2.f;
		float mid = 0.5f * (oL + oR) * dg, side = 0.5f * (oL - oR) * width * dg;
		// Grotte's mix: the dry at full up to 50 %, the wet at full from 50 %
		const float mix = clamp(knob(MIX_PARAM, MIX_INPUT), 0.f, 1.f);
		gDry += smoothA * (std::min(1.f, 2.f * (1.f - mix)) - gDry);
		gWet += smoothA * (std::min(1.f, 2.f * mix) - gWet);
		const float outDbNow = params[OUTPUT_PARAM].getValue();
		if (outDbNow != outDb) {
			outDb = outDbNow;
			outGain = std::pow(10.f, outDb / 20.f);
		}
		const float outG = outGain;
		const float outPan = params[OUTPAN_PARAM].getValue();
		gOutL += smoothA * (outG * std::min(1.f, 1.f - outPan) - gOutL);
		gOutR += smoothA * (outG * std::min(1.f, 1.f + outPan) - gOutR);
		float outL = (gDry * l + gWet * (mid + side)) * gOutL;
		float outR = (gDry * r + gWet * (mid - side)) * gOutR;
		if (params[CLIPPING_PARAM].getValue() > 0.5f) {
			outL = clamp(outL, -10.0f, 10.0f);
			outR = clamp(outR, -10.0f, 10.0f);
		}
		else {
			outL = bidooSoftLimit(outL);
			outR = bidooSoftLimit(outR);
		}
		outputs[OUT_L_OUTPUT].setVoltage(outL);
		outputs[OUT_R_OUTPUT].setVoltage(outR);
	}
};

struct REIWidget : BidooWidget {
	REIWidget(REI *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, "res/REI.svg"));
		theme::addScrews(this);

		// Placed as tools/panels/rei.py draws them, grouped by theme (SPACE and FREEZE, CHARACTER and MIX, SHIMMER,
		// DYNAMICS): three rows of seven cells (a control and its small CV or trigger jack under it, joined by the panel's
		// vertical line), then the levels just under grid row 1 and the jacks in their boxes on row 0. The cells' rows are off the
		// grid (three rows of 80 px under the title and the groups' names); their columns are the grid's.
		const int n = 7;
		const float kRowY[3] = {74.f, 154.f, 234.f};   // rei.py ROW_Y
		const float kLevelsY = 300.f;                   // rei.py LEVELS_Y: 4 px under grid row 1, leaving the cells room
		const float kJackDy = 28.f;                     // rei.py JACK_DY
		enum Kind { KNOB, SNAP, SMALL, SMALLSNAP, BUTTON };
		struct Cell { int col, row, param, input; Kind kind; };
		const Cell kCells[] = {   // rei.py CELLS, in the same order
			{0, 0, REI::STYLE_PARAM, REI::STYLE_INPUT, SNAP}, {1, 0, REI::SIZE_PARAM, REI::SIZE_INPUT, KNOB},
			{2, 0, REI::STRETCH_PARAM, REI::STRETCH_INPUT, KNOB}, {3, 0, REI::PREDELAY_PARAM, REI::PREDELAY_INPUT, KNOB},
			{4, 0, REI::PRESYNC_PARAM, -1, SMALLSNAP}, {5, 0, REI::PREOFFSET_PARAM, -1, SMALL},
			{6, 0, REI::FREEZE_PARAM, REI::FREEZE_INPUT, BUTTON},
			{0, 1, REI::MOTION_PARAM, REI::MOTION_INPUT, KNOB}, {1, 1, REI::DENSITY_PARAM, REI::DENSITY_INPUT, KNOB},
			{2, 1, REI::DEPTH_PARAM, REI::DEPTH_INPUT, KNOB}, {3, 1, REI::AIR_PARAM, REI::AIR_INPUT, KNOB},
			{4, 1, REI::WIDTH_PARAM, REI::WIDTH_INPUT, KNOB}, {5, 1, REI::BUILDUP_PARAM, REI::BUILDUP_INPUT, KNOB},
			{6, 1, REI::MIX_PARAM, REI::MIX_INPUT, KNOB},
			{0, 2, REI::SHIMMER_PARAM, REI::SHIMMER_INPUT, KNOB}, {1, 2, REI::TONE_PARAM, REI::TONE_INPUT, KNOB},
			{2, 2, REI::INTERVAL_PARAM, REI::INTERVAL_INPUT, SNAP}, {3, 2, REI::DUCK_PARAM, REI::DUCK_INPUT, KNOB},
			{4, 2, REI::GATE_PARAM, REI::GATE_INPUT, BUTTON}, {5, 2, REI::HOLD_PARAM, REI::HOLD_INPUT, KNOB},
			{6, 2, REI::GATESYNC_PARAM, -1, SMALLSNAP},
		};
		for (const Cell &c : kCells) {
			const theme::Pos cell = theme::sameCol(theme::at(c.col, n, 0, box.size.x), kRowY[c.row]);
			const theme::Pos &p = cell;
			switch (c.kind) {
				case KNOB: addParam(theme::param<BidooBlueKnob>(p, module, c.param)); break;
				case SNAP: addParam(theme::param<BidooBlueSnapKnob>(p, module, c.param)); break;
				case SMALL: addParam(theme::param<BidooSmallBlueKnob>(p, module, c.param)); break;
				case SMALLSNAP: addParam(theme::param<BidooSmallSnapBlueKnob>(p, module, c.param)); break;
				case BUTTON: addParam(theme::param<BidooRoundButton>(p, module, c.param)); break;
			}
			if (c.input >= 0)
				addInput(theme::input<TinyPJ301MPort>(cell.down(kJackDy), module, c.input));
		}

		auto at = [&](float i, int) { return theme::sameCol(theme::at(i, n, 1, box.size.x), kLevelsY); };
		addParam(theme::param<BidooBlueTrimpot>(at(0, 1), module, REI::INPUT_PARAM));
		addParam(theme::param<BidooBlueTrimpot>(at(1, 1), module, REI::INPAN_PARAM));
		addParam(theme::param<CKSS>(at(3, 1), module, REI::CLIPPING_PARAM));
		addParam(theme::param<BidooBlueTrimpot>(at(5, 1), module, REI::OUTPUT_PARAM));
		addParam(theme::param<BidooBlueTrimpot>(at(6, 1), module, REI::OUTPAN_PARAM));

		auto jack = [&](float i) { return theme::jackAt(i, n, 0, box.size.x); };
		addInput(theme::input<PJ301MPort>(jack(0), module, REI::IN_L_INPUT));
		addInput(theme::input<PJ301MPort>(jack(1), module, REI::IN_R_INPUT));
		addInput(theme::input<PJ301MPort>(jack(3), module, REI::CLOCK_INPUT));
		addOutput(theme::output<PJ301MPort>(jack(5), module, REI::OUT_L_OUTPUT));
		addOutput(theme::output<PJ301MPort>(jack(6), module, REI::OUT_R_OUTPUT));
	}
};

Model *modelREI = createModel<REI, REIWidget>("REI");
