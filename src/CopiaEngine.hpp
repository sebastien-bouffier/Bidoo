// cOPIA's engine, without Rack: the clock-locked capture of the incoming mix, the loop played back on
// the clock, Mangle's glitch engine (bidoo-dsp effects/glitch, lite build) remixing it with eight
// slots and their lanes, eight scenes with a morph, and the IN/OUT mix with the live signal.
//
// The module runs it on blocks of kBlock samples (its inputs and outputs are kBlock samples late).
// Everything the audio thread reads that the UI writes is atomic (the scenes' patterns and slots, as
// in Mangle); the loops are fixed buffers made once, so nothing is allocated while it plays.
#pragma once
#define BIDOO_DSP_GLITCH_LITE 1
#include "dep/bidoo-dsp/bidoo/dsp/effects/glitch/Engine.h"
#include "dep/bidoo-dsp/bidoo/dsp/core/Random.h"
#include "dep/bidoo-dsp/bidoo/dsp/time/PulseClock.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace copia {
namespace g = bidoo::dsp::glitch;

constexpr int kSlots = g::kNumSlots;
constexpr int kLanes = g::Pattern::kLanes;   // the eight slots' lanes and the Chance lane
constexpr int kChance = g::Pattern::kChanceLane;
constexpr int kScenes = 8;
constexpr int kBlock = 32;
constexpr int kPeaks = 512;                  // the loop's waveform for the display
constexpr double kLoopSeconds = 32.0;        // each loop's room at 96 kHz (16 s at 192 kHz)
constexpr int kLoopFrames = (int)(kLoopSeconds * 96000.0);

// The effects a slot can play: Mangle's that need no compiled code (the lite build).
struct EffectChoice {
	g::Fx fx;
	const char *shortName;
};
inline const EffectChoice kEffects[] = {
	{g::Fx::Off, "Off"}, {g::Fx::Repeat, "Repeat"}, {g::Fx::Reverser, "Revers"}, {g::Fx::Slicer, "Slicer"},
	{g::Fx::Gate, "Gate"}, {g::Fx::Tape, "Tape"}, {g::Fx::Grain, "Grain"}, {g::Fx::Crush, "Crush"},
	{g::Fx::Drive, "Drive"}, {g::Fx::Filter, "Filter"}, {g::Fx::Delay, "Delay"}, {g::Fx::Ring, "Ring"},
	{g::Fx::Flanger, "Flange"}, {g::Fx::Phaser, "Phaser"}, {g::Fx::FilterPattern, "F.Patt"}, {g::Fx::Reactor, "React"},
};
constexpr int kNumEffects = (int)(sizeof(kEffects) / sizeof(kEffects[0]));

inline int effectIndex(g::Fx fx) {
	for (int i = 0; i < kNumEffects; i++)
		if (kEffects[i].fx == fx)
			return i;
	return 0;
}

// The pattern's grid: steps per beat
struct Grid {
	const char *label;
	int div;
};
inline const Grid kGrids[] = {{"1/8", 2}, {"1/8T", 3}, {"1/16", 4}, {"1/16T", 6}, {"1/32", 8}};
constexpr int kNumGrids = 5;

// The line-up of a new module
inline g::Fx defaultEffect(int slot) {
	static const g::Fx lineUp[kSlots] = {g::Fx::Repeat, g::Fx::Slicer, g::Fx::Reverser, g::Fx::Gate,
	                                     g::Fx::Tape, g::Fx::Grain, g::Fx::Crush, g::Fx::Filter};
	return lineUp[slot];
}

// A slot in a scene: its effect and settings (0..1 each), the source its capture effects read
struct SlotData {
	std::atomic<int> fx {(int)g::Fx::Off};
	std::atomic<float> p[g::kNumEffectParams];
	std::atomic<float> freq {0.5f};     // the slot's filter frequency (Mangle's map: 20 Hz to 20 kHz)
	std::atomic<int> filter {0};        // 0 none, 1 low pass, 2 high pass, 3 band pass, 4 notch
	std::atomic<bool> live {false};     // LIVE: the live input's history; LOOP: the captured loop
	std::atomic<float> weight {1.f};    // the Chance lane's odds

	SlotData() {
		for (auto &v : p)
			v.store(0.f);
	}
	g::Fx effect() const {
		return (g::Fx)fx.load(std::memory_order_relaxed);
	}
	void setEffect(g::Fx f) {
		for (int k = 0; k < g::kNumEffectParams; k++)
			p[k].store(g::paramDef(f, k).def);
		fx.store((int)f);
	}
	void copyFrom(const SlotData &o) {
		fx.store(o.fx.load());
		for (int k = 0; k < g::kNumEffectParams; k++)
			p[k].store(o.p[k].load());
		freq.store(o.freq.load());
		filter.store(o.filter.load());
		live.store(o.live.load());
		weight.store(o.weight.load());
	}
};

// A scene: the pattern (and its seed), the eight slots, the grid and the pattern's length in bars
struct Scene {
	std::atomic<bool> used {false};
	g::Pattern pattern;
	SlotData slot[kSlots];
	std::atomic<int> grid {2};
	std::atomic<int> bars {1};

	void copyFrom(const Scene &o) {
		pattern.copyFrom(o.pattern);
		for (int s = 0; s < kSlots; s++)
			slot[s].copyFrom(o.slot[s]);
		grid.store(o.grid.load());
		bars.store(o.bars.load());
		used.store(true);
	}
	int div() const {
		return kGrids[std::clamp(grid.load(std::memory_order_relaxed), 0, kNumGrids - 1)].div;
	}
	int steps(int beatsPerBar) const {
		return std::min(g::Pattern::kMaxSteps, bars.load(std::memory_order_relaxed) * beatsPerBar * div());
	}
};

// A captured loop: `pre` samples from just before its first bar (the wrap crossfades into them),
// then `frames` samples of the loop itself
struct Loop {
	std::vector<float> l, r;
	int64_t frames = 0;
	int pre = 0;
	double startPpq = 0.0;   // where its first bar is on the clock
	double beats = 0.0;
	float bpm = 0.f;
	float rate = 0.f;        // the sample rate it was made at
	std::atomic<bool> valid {false};
	float peaks[kPeaks] = {};

	void allocate() {
		l.assign((size_t)kLoopFrames, 0.f);
		r.assign((size_t)kLoopFrames, 0.f);
	}
	int64_t capacity() const {
		return (int64_t)l.size();
	}
};

// ---- pattern helpers (UI thread or audio thread; the pattern's cells are atomic) ----

// A new lane: blocks start more often on beats and for heavier slots (Mangle's Dice)
inline void randomizeLane(g::Pattern &pattern, int lane, int steps, int div, float weight, bidoo::dsp::Rng &rnd) {
	pattern.clearLane(lane);
	for (int st = 0; st < steps;) {
		const float chance = weight * (st % div == 0 ? 0.45f : 0.18f);
		if (rnd.uniform() < chance) {
			const int length = std::min(steps - st, 1 + (int)(rnd.uniform() * (float)std::max(1, div)));
			const g::Shape shape = rnd.uniform() < 0.6f ? g::Shape::Hold : (g::Shape)(1 + (int)(rnd.uniform() * ((int)g::Shape::count - 1)) % ((int)g::Shape::count - 1));
			g::BlockInfo info;
			info.level = (uint8_t)(60 + (int)(rnd.uniform() * 41.f) % 41);
			info.shape = (uint8_t)shape;
			if (rnd.uniform() < 0.12f)
				info.ratchets = (uint8_t)(2 + (int)(rnd.uniform() * 3.f) % 3);
			pattern.setBlock(lane, st, st + length - 1, info);
			st += length + (int)(rnd.uniform() * 2.f);
		}
		else {
			st++;
		}
	}
}

// Rand: a new seed, every lane and the slots' settings (Mangle's Rand)
inline void randomizeScene(Scene &sc, int beatsPerBar, bidoo::dsp::Rng &rnd) {
	const int steps = sc.steps(beatsPerBar), div = sc.div();
	sc.pattern.setSeed((int)(rnd.next() >> 2));
	for (int s = 0; s < kSlots; s++) {
		randomizeLane(sc.pattern, s, steps, div, 0.5f * sc.slot[s].weight.load(), rnd);
		const g::Fx fx = sc.slot[s].effect();
		for (int k = 0; k < g::kNumEffectParams; k++)
			if (g::paramDef(fx, k).kind != g::Kind::Unused)
				sc.slot[s].p[k].store(rnd.uniform());
	}
	randomizeLane(sc.pattern, kChance, steps, div, 0.5f, rnd);
	sc.used.store(true);
}

// Mutate: a few steps flip, the settings move a little (Mangle's Mutate, by `amount`)
inline void mutateScene(Scene &sc, int beatsPerBar, float amount, bidoo::dsp::Rng &rnd) {
	const int steps = sc.steps(beatsPerBar);
	if (rnd.uniform() < amount * 0.5f)
		sc.pattern.setSeed((int)(rnd.next() >> 2));
	for (int lane = 0; lane < kLanes; lane++) {
		for (int st = 0; st < steps; st++)
			if (rnd.uniform() < amount * 0.15f) {
				if (sc.pattern.isOn(lane, st))
					sc.pattern.erase(lane, st, st);
				else
					sc.pattern.setBlock(lane, st, st, 60 + (int)(rnd.uniform() * 41.f) % 41);
			}
		if (lane >= kSlots)
			continue;
		const g::Fx fx = sc.slot[lane].effect();
		for (int k = 0; k < g::kNumEffectParams; k++)
			if (g::paramDef(fx, k).kind != g::Kind::Unused) {
				float v = sc.slot[lane].p[k].load() + (rnd.uniform() - 0.5f) * amount * 0.6f;
				sc.slot[lane].p[k].store(std::clamp(v, 0.f, 1.f));
			}
	}
	sc.used.store(true);
}

// What the module sets before each block
struct Controls {
	int beatsPerBar = 4;
	int lengthBars = 4;        // what CAPTURE records
	float mix = 1.f;           // MIX knob + CV, 0..1
	int law = 0;               // Mangle's dry / wet laws (0 equal power, 1 X-fade, 2 dry stays, 3 wet stays, 4 wet only)
	bool wantIn = false;       // IN/OUT: the button or the gate
	int inQuant = 1;           // IN/OUT waits for: 0 nothing, 1 the next step, 2 beat, 3 bar
	int sceneQuant = 3;        // a scene change waits for: 0 nothing, 1 step, 2 beat, 3 bar
	int sceneTarget = 0;
	int morphB = 1;
	float morph = 0.f;
	float punch[kSlots] = {};  // PUNCH knob + CV, 0..1
	bool mute[kSlots] = {};
	bool autoIn = false;       // "Remix comes in after capture"
};

class Engine {
public:
	Scene scenes[kScenes];
	Loop loops[2];

	// for the UI and the module (written by the audio thread)
	std::atomic<int> front {-1};          // the loop that plays (-1: none yet)
	std::atomic<int> capState {0};        // 0 idle, 1 armed, 2 recording
	std::atomic<float> capProgress {0.f}; // 0..1 through the capture
	std::atomic<int> current {0};         // the scene that plays (morph A)
	std::atomic<float> loopPhase {-1.f};  // 0..1 through the loop, -1 when it does not play
	std::atomic<bool> isIn {false};       // the remix is in
	std::atomic<bool> laneGate[kSlots];
	std::atomic<bool> loopChanged {false}; // a capture ended (the patch storage is out of date)
	std::atomic<bool> autoInFired {false}; // a capture ended with "Remix comes in after capture": the module latches IN/OUT
	std::atomic<bool> clearRequest {false};
	std::atomic<bool> blockCapture {false}; // the UI loads or saves a loop: no capture starts meanwhile
	// for the display: what CAPTURE would take, written in place over LENGTH (inPhase: the write head, -1 stopped),
	// and the remix alone, written at the loop's phase (loopPhase); peaks 0..1 (racy reads are harmless)
	float inPeaks[kPeaks] = {};
	float outPeaks[kPeaks] = {};
	std::atomic<float> inPhase {-1.f};

	Engine() {
		for (auto &lp : loops)
			lp.allocate();
		for (auto &gt : laneGate)
			gt.store(false);
		for (auto &sc : scenes)
			for (int s = 0; s < kSlots; s++)
				sc.slot[s].setEffect(defaultEffect(s));
		scenes[0].used = true;
		bidoo::dsp::Rng rnd(1);
		randomizeScene(scenes[0], 4, rnd);
		prepare(44100.0);
	}

	void prepare(double sampleRate) {
		sr = sampleRate;
		glitch.prepare(sr, kBlock);
		live.prepare(sr, 12.0, 2);
		xf = std::clamp((int)(0.005 * sr), 64, kPreMax);
		preWrite = 0;
		std::fill(&preRing[0][0], &preRing[0][0] + 2 * kPreMax, 0.f);
		capState = 0;
		srcFade = 0;
		heard = front.load();
		jumpFade = 0;
		havePos = false;
		beatStep = 0.0;
		playGain = 0.f;
		stalled = 0;
		mixNow = -1.f;
		inGain = isIn ? 1.f : 0.f;
		inCoef = 1.f - std::exp(-1.f / (float)(0.015 * sr));
		mixCoef = 1.f - std::exp(-1.f / (float)(0.005 * sr));
		lastPpq = -1.0;
		pendingIn = isIn;
		inDue = -1.0;
		sceneDue = -1.0;
		std::fill(lastOutL, lastOutL + kBlock, 0.f);
		std::fill(lastOutR, lastOutR + kBlock, 0.f);
	}

	double sampleRate() const {
		return sr;
	}

	// CAPTURE (a press or a trigger): arms the capture on the next bar; armed again, it gives up
	void captureTrigger() {
		const int s = capState.load();
		if (s == 0 && !blockCapture.load())
			capState = 1;
		else if (s == 1)
			capState = 0;
	}

	// One block of kBlock samples. `inL/inR`: the input (+-1 for +-5 V); `ppq`: the clock's beats at each
	// sample; `running`: the clock runs; `bpm` its tempo. Writes the mix and the remix alone.
	void process(const float *inL, const float *inR, const double *ppq, bool running, double bpm, bool resampleOut,
	             const Controls &ctl, float *outL, float *outR, float *wetL, float *wetR) {
		const int n = kBlock;
		if (clearRequest.exchange(false)) {
			if (capState == 2)
				capState = 0;
			front = -1;
		}
		bpm = std::clamp(bpm, 20.0, 999.0);
		const int bpb = std::max(1, ctl.beatsPerBar);
		const float *capL = resampleOut ? lastOutL : inL;
		const float *capR = resampleOut ? lastOutR : inR;

		// ---- capture, loop playback: the engine's input ----
		for (int i = 0; i < n; i++) {
			const double pos = ppq[i];
			const bool barLine = lastPpq >= 0.0 && std::floor(pos / bpb + 1e-9) != std::floor(lastPpq / bpb + 1e-9);
			const bool jumped = lastPpq >= 0.0 && (pos < lastPpq || pos > lastPpq + 0.5);
			lastPpq = running ? pos : -1.0;
			int state = capState.load(std::memory_order_relaxed);
			if (state == 2 && (!running || jumped))
				capState = state = 0; // the clock stopped or jumped: the capture gives up
			if (state == 2) {
				Loop &lp = loops[capLoop];
				if (pos - capStartPpq >= capBeats - 1e-9) {
					finishCapture(bpm, ctl.autoIn);
					state = 0;
				}
				else if (lp.pre + capFrames >= lp.capacity()) {
					capState = state = 0; // no room left (the tempo fell): the old loop stays
				}
				else {
					const int64_t at = lp.pre + capFrames;
					lp.l[(size_t)at] = capL[i];
					lp.r[(size_t)at] = capR[i];
					const int b = std::clamp((int)((double)capFrames / capExpected * kPeaks), 0, kPeaks - 1);
					lp.peaks[b] = std::max(lp.peaks[b], std::max(std::fabs(capL[i]), std::fabs(capR[i])));
					capFrames++;
					capProgress = (float)std::clamp((pos - capStartPpq) / capBeats, 0.0, 1.0);
				}
			}
			if (state == 1 && running && barLine)
				startCapture(pos, bpb, bpm, ctl.lengthBars);
			// the pre-roll: the last few ms, for the next capture's wrap
			preRing[0][preWrite] = capL[i];
			preRing[1][preWrite] = capR[i];
			preWrite = (preWrite + 1) % kPreMax;
			if (running) {
				const double win = (double)(std::max(1, ctl.lengthBars) * bpb);
				double ph = pos / win;
				ph -= std::floor(ph);
				const int b = std::clamp((int)(ph * kPeaks), 0, kPeaks - 1);
				if (b != inBin) {
					inBin = b;
					inPeaks[b] = 0.f;
				}
				inPeaks[b] = std::max(inPeaks[b], std::max(std::fabs(capL[i]), std::fabs(capR[i])));
				inPhaseNow = (float)ph;
			}

			// the engine's input: the loop on the clock, or the live input while there is no loop. Another loop
			// (a capture, CLEAR, a loop loaded) crossfades from the one heard; a position that jumps (RESET, a
			// clock that came back) crossfades from where the loop was, carried on at its speed; the loop fades
			// out while the clock stops or stalls, and back in when it moves again.
			const int f = front.load(std::memory_order_relaxed);
			if (f != heard) {
				oldFront = heard;
				heard = f;
				srcFade = xf;
				jumpFade = 0;
				std::fill(outPeaks, outPeaks + kPeaks, 0.f);
				outBin = -1;
			}
			// a jump: back, or on by more than a 500th of a beat in one sample (48 samples at 120 BPM, 6 at 999)
			const double moved = pos - prevPos;
			if (havePos && (!running || moved < 0.0 || moved > 0.002) && srcFade == 0) {
				jumpFrom = prevPos + beatStep;
				jumpFade = xf;
			}
			else if (havePos && moved > 0.0) {
				beatStep = moved;
			}
			stalled = running && havePos && pos == prevPos ? stalled + 1 : 0;
			havePos = running;
			prevPos = pos;
			const float wantPlay = running && stalled < xf / 2 ? 1.f : 0.f;
			playGain += std::clamp(wantPlay - playGain, -1.f / (float)xf, 1.f / (float)xf);
			float l, r;
			source(f, pos, running, inL[i], inR[i], l, r);
			if (srcFade > 0) {
				float ol, orr;
				source(oldFront, pos, running, inL[i], inR[i], ol, orr);
				const float t = (float)srcFade / (float)xf;
				l = l + (ol - l) * t;
				r = r + (orr - r) * t;
				srcFade--;
			}
			else if (jumpFade > 0) {
				float ol, orr;
				source(f, jumpFrom, running, inL[i], inR[i], ol, orr);
				jumpFrom += beatStep;
				const float t = (float)jumpFade / (float)xf;
				l = l + (ol - l) * t;
				r = r + (orr - r) * t;
				jumpFade--;
			}
			engL[i] = l;
			engR[i] = r;
		}
		const int f = front.load();
		loopPhase = f >= 0 && running ? (float)phase(loops[f], ppq[n - 1]) : -1.f;
		inPhase = running ? inPhaseNow : -1.f;

		// ---- the live history, in step with the engine's own ----
		live.write(inL, inR, n);

		// ---- scenes: a switch on the quantize grid, the block cut there ----
		const int cur = current.load();
		const bool switching = ctl.sceneTarget != cur && ctl.sceneTarget >= 0 && ctl.sceneTarget < kScenes;
		int split = n;
		if (switching) {
			if (sceneFor != ctl.sceneTarget) {
				sceneFor = ctl.sceneTarget;
				sceneDue = dueAt(ppq[0], running, ctl.sceneQuant, scenes[cur].div(), bpb);
			}
			split = 0; // stopped, or no grid: at once
			if (running && ctl.sceneQuant != 0)
				while (split < n && ppq[split] < sceneDue - 1e-9)
					split++;
		}
		else {
			sceneFor = -1;
		}
		std::copy(engL, engL + n, wetL);
		std::copy(engR, engR + n, wetR);
		g::HostPosition host;
		host.valid = true;
		host.playing = running;
		host.bpm = bpm;
		if (split > 0) {
			host.ppq = ppq[0];
			buildSettings(ctl, cur);
			glitch.process(wetL, wetR, split, host, settings, *pattern);
		}
		if (split < n) {
			switchScene(ctl.sceneTarget);
			sceneFor = -1;
			host.ppq = ppq[split];
			buildSettings(ctl, current.load());
			glitch.process(wetL + split, wetR + split, n - split, host, settings, *pattern);
		}
		for (int s = 0; s < kSlots; s++)
			laneGate[s].store(glitch.isLaneOn(s));

		// ---- IN/OUT on its grid, then the mix ----
		if (ctl.wantIn != pendingIn) {
			pendingIn = ctl.wantIn;
			inDue = dueAt(ppq[0], running, ctl.inQuant, scenes[current.load()].div(), bpb);
		}
		if (mixNow < 0.f)
			mixNow = ctl.mix;
		for (int i = 0; i < n; i++) {
			mixNow += mixCoef * (ctl.mix - mixNow);
			if (inDue >= 0.0 && (!running || ctl.inQuant == 0 || ppq[i] >= inDue - 1e-9)) {
				isIn = pendingIn;
				inDue = -1.0;
			}
			inGain += inCoef * ((isIn ? 1.f : 0.f) - inGain);
			float dry, wet;
			g::lawGains(ctl.law, mixNow * inGain, dry, wet);
			outL[i] = inL[i] * dry + wetL[i] * wet;
			outR[i] = inR[i] * dry + wetR[i] * wet;
			if (f >= 0 && running) {
				const int b = std::clamp((int)(phase(loops[f], ppq[i]) * kPeaks), 0, kPeaks - 1);
				if (b != outBin) {
					outBin = b;
					outPeaks[b] = 0.f;
				}
				outPeaks[b] = std::max(outPeaks[b], std::max(std::fabs(wetL[i]), std::fabs(wetR[i])));
			}
		}
		std::copy(outL, outL + n, lastOutL);
		std::copy(outR, outR + n, lastOutR);
	}

	// for the display: the loop a capture records into
	int capturingLoop() const {
		return capLoop;
	}

	// for the display: the step under the play head (-1 stopped) and whether a slot sounds
	float playStep() const {
		return glitch.playStep.load();
	}
	bool sounding(int s) const {
		return glitch.sounding[s].load();
	}

	// A loop read back from the patch storage (UI thread, with blockCapture set): into the loop that is
	// not playing, then it plays
	void installLoop(const float *l, const float *r, int64_t total, int pre, double beats, double startPpq, float bpm, float rate) {
		const int f = front.load();
		const int k = f == 0 ? 1 : 0;
		Loop &lp = loops[k];
		lp.valid = false;
		total = std::min<int64_t>(total, lp.capacity());
		std::copy(l, l + total, lp.l.begin());
		std::copy(r, r + total, lp.r.begin());
		lp.pre = std::clamp(pre, 0, (int)std::max<int64_t>(0, total - 1));
		lp.frames = total - lp.pre;
		lp.beats = beats;
		lp.startPpq = startPpq;
		lp.bpm = bpm;
		lp.rate = rate;
		std::fill(lp.peaks, lp.peaks + kPeaks, 0.f);
		for (int64_t i = 0; i < lp.frames; i++) {
			const int b = (int)(i * kPeaks / std::max<int64_t>(1, lp.frames));
			lp.peaks[b] = std::max(lp.peaks[b], std::max(std::fabs(lp.l[(size_t)(lp.pre + i)]), std::fabs(lp.r[(size_t)(lp.pre + i)])));
		}
		if (lp.frames < 64)
			return;
		lp.valid = true;
		front = k; // the audio thread crossfades from the loop it heard
	}

private:
	static constexpr int kPreMax = 1024;

	// The loop's position at `pos` beats, 0..1
	static double phase(const Loop &lp, double pos) {
		double ph = (pos - lp.startPpq) / lp.beats;
		return ph - std::floor(ph);
	}

	// Loop `k`'s sample at `pos` beats (faded out while the clock stops), or the live input with no loop
	void source(int k, double pos, bool running, float inL, float inR, float &l, float &r) const {
		if (k < 0 || !loops[k].valid.load(std::memory_order_relaxed)) {
			l = inL;
			r = inR;
			return;
		}
		const Loop &lp = loops[k];
		const double at = phase(lp, pos) * (double)lp.frames;
		readLoop(lp, at, l, r);
		const double fadeFrom = (double)(lp.frames - lp.pre);
		if (lp.pre > 0 && at > fadeFrom) {
			// the end crossfades into what came just before the first bar, which runs on into it
			const double u = at - fadeFrom;
			float pl, pr;
			readRaw(lp, u, pl, pr);
			const float t = (float)(u / lp.pre);
			l += (pl - l) * t;
			r += (pr - r) * t;
		}
		l *= playGain;
		r *= playGain;
	}

	// Hermite on the loop itself (wrapping), and on the raw buffer (clamped)
	static float hermite(float xm1, float x0, float x1, float x2, float t) {
		const float c1 = 0.5f * (x1 - xm1);
		const float c2 = xm1 - 2.5f * x0 + 2.f * x1 - 0.5f * x2;
		const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
		return ((c3 * t + c2) * t + c1) * t + x0;
	}
	static void readLoop(const Loop &lp, double at, float &l, float &r) {
		const int64_t n = lp.frames;
		const int64_t i = (int64_t)std::floor(at);
		const float t = (float)(at - (double)i);
		auto idx = [&](int64_t j) { j %= n; if (j < 0) j += n; return (size_t)(lp.pre + j); };
		const size_t a = idx(i - 1), b = idx(i), c = idx(i + 1), d = idx(i + 2);
		l = hermite(lp.l[a], lp.l[b], lp.l[c], lp.l[d], t);
		r = hermite(lp.r[a], lp.r[b], lp.r[c], lp.r[d], t);
	}
	static void readRaw(const Loop &lp, double at, float &l, float &r) {
		const int64_t last = lp.pre + lp.frames - 1;
		const int64_t i = (int64_t)std::floor(at);
		const float t = (float)(at - (double)i);
		auto idx = [&](int64_t j) { return (size_t)std::clamp<int64_t>(j, 0, last); };
		const size_t a = idx(i - 1), b = idx(i), c = idx(i + 1), d = idx(i + 2);
		l = hermite(lp.l[a], lp.l[b], lp.l[c], lp.l[d], t);
		r = hermite(lp.r[a], lp.r[b], lp.r[c], lp.r[d], t);
	}

	// The next point of a quantize grid after `pos`: 1 the step, 2 the beat, 3 the bar (0: now)
	static double dueAt(double pos, bool running, int quant, int div, int bpb) {
		if (!running || quant <= 0)
			return pos;
		const double unit = quant == 1 ? 1.0 / div : quant == 2 ? 1.0 : (double)bpb;
		return std::ceil(pos / unit - 1e-9) * unit;
	}

	void startCapture(double pos, int bpb, double bpm, int lengthBars) {
		const int f = front.load();
		capLoop = f == 0 ? 1 : 0;
		Loop &lp = loops[capLoop];
		// shorter when the bars would not fit (a slow tempo)
		int bars = std::max(1, lengthBars);
		auto need = [&](int b) { return (double)(b * bpb) * 60.0 / bpm * sr + xf + 16; };
		while (bars > 1 && need(bars) > (double)lp.capacity())
			bars /= 2;
		if (need(bars) > (double)lp.capacity()) {
			capState = 0;
			return;
		}
		lp.valid = false;
		// the pre-roll: the xf samples before this one
		for (int k = 0; k < xf; k++) {
			const int j = (preWrite - xf + k + kPreMax) % kPreMax;
			lp.l[(size_t)k] = preRing[0][j];
			lp.r[(size_t)k] = preRing[1][j];
		}
		lp.pre = xf;
		std::fill(lp.peaks, lp.peaks + kPeaks, 0.f);
		capFrames = 0;
		capBeats = (double)(bars * bpb);
		capStartPpq = std::floor(pos / bpb + 1e-9) * bpb;
		capFirstPpq = pos; // the first sample, a fraction of a sample after the bar line: the loop plays from there
		capExpected = std::max(1.0, capBeats * 60.0 / bpm * sr);
		capProgress = 0.f;
		capState = 2;
	}

	void finishCapture(double bpm, bool autoIn) {
		Loop &lp = loops[capLoop];
		capState = 0;
		if (capFrames < 64)
			return;
		lp.frames = capFrames;
		lp.beats = capBeats;
		lp.startPpq = capFirstPpq;
		lp.bpm = (float)bpm;
		lp.rate = (float)sr;
		lp.valid = true;
		front = capLoop;
		loopChanged = true;
		if (autoIn)
			autoInFired = true;
	}

	void switchScene(int target) {
		const int cur = current.load();
		if (!scenes[target].used.load())
			scenes[target].copyFrom(scenes[cur]); // an empty scene starts as a copy (Mangle)
		current = target;
	}

	// The engine's settings for scene `cur` morphed towards scene morphB
	void buildSettings(const Controls &ctl, int cur) {
		const Scene &a = scenes[cur];
		const Scene &b = scenes[std::clamp(ctl.morphB, 0, kScenes - 1)];
		const float m = std::clamp(ctl.morph, 0.f, 1.f);
		const Scene &near = m < 0.5f ? a : b;
		auto &set = settings;
		pattern = &near.pattern;
		set.seed = near.pattern.getSeed();
		set.beats = std::max(1, near.bars.load()) * std::max(1, ctl.beatsPerBar);
		set.divisions = near.div();
		set.law = 0;            // the slots' own dry / wet: equal power
		set.masterMix = 1.f;    // the engine gives the remix alone
		set.masterGain = 1.f;
		set.masterPan = 0.f;
		set.internalClock = false;
		set.trigger = 0;
		set.gated = false;
		set.liveHistory = &live;
		for (int s = 0; s < kSlots; s++) {
			const SlotData &sa = a.slot[s], &sb = b.slot[s], &sn = near.slot[s];
			const g::Fx fx = sn.effect();
			const bool blend = m > 0.f && sa.effect() == sb.effect();
			auto &ss = set.slots[s];
			ss.fx = fx;
			for (int k = 0; k < g::kNumEffectParams; k++) {
				const float va = sa.p[k].load(std::memory_order_relaxed), vb = sb.p[k].load(std::memory_order_relaxed);
				ss.base[k] = blend ? va + (vb - va) * m : sn.p[k].load(std::memory_order_relaxed);
			}
			const float fa = sa.freq.load(std::memory_order_relaxed), fb = sb.freq.load(std::memory_order_relaxed);
			ss.base[g::kTargetFreq] = blend ? fa + (fb - fa) * m : sn.freq.load(std::memory_order_relaxed);
			// the drive (the block's level, or PUNCH) is the slot's wet mix
			ss.base[g::kTargetMix] = 0.f;
			for (int t = 0; t < g::kNumTargets; t++) {
				ss.amount[t] = t == g::kTargetMix ? 1.f : 0.f;
				ss.curve[t] = g::Curve::Linear;
			}
			ss.filter = sn.filter.load(std::memory_order_relaxed);
			ss.q = 0.707f;
			ss.pan = 0.f;
			ss.gain = 1.f;
			ss.knob = ctl.punch[s];
			ss.knobInControl = ctl.punch[s] > g::Slot::kKnobThreshold;
			set.laneOn[s] = !ctl.mute[s] && fx != g::Fx::Off;
			set.weight[s] = sn.weight.load(std::memory_order_relaxed);
			set.liveSource[s] = sn.live.load(std::memory_order_relaxed);
			set.order[s] = s;
		}
	}

	double sr = 44100.0;
	g::Engine glitch;
	g::History live;
	g::EngineSettings settings;
	const g::Pattern *pattern = &scenes[0].pattern;
	float engL[kBlock] = {}, engR[kBlock] = {};
	float lastOutL[kBlock] = {}, lastOutR[kBlock] = {};

	// capture
	float preRing[2][kPreMax] = {};
	int preWrite = 0, xf = 256;
	int capLoop = 0;
	int64_t capFrames = 0;
	double capBeats = 4.0, capStartPpq = 0.0, capFirstPpq = 0.0, capExpected = 1.0;
	double lastPpq = -1.0;
	int oldFront = -1, srcFade = 0;
	int heard = -1;                  // the loop the audio thread plays (front, once it has crossfaded to it)
	double prevPos = 0.0, jumpFrom = 0.0;
	double beatStep = 0.0;            // how far the position moved in the last sample that did not jump
	bool havePos = false;
	int jumpFade = 0, stalled = 0;
	float playGain = 0.f;            // the loop, faded out while the clock stops
	float mixNow = -1.f, mixCoef = 0.01f;
	int inBin = -1, outBin = -1;
	float inPhaseNow = 0.f;

	// IN/OUT, scenes
	bool pendingIn = false;
	double inDue = -1.0;
	float inGain = 0.f, inCoef = 0.001f;
	double sceneDue = -1.0;
	int sceneFor = -1;
};

}  // namespace copia
