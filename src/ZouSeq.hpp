#pragma once
#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <mutex>
#include "dep/quantizer.hpp"
#include "dep/bidoo-dsp/bidoo/dsp/time/Euclid.h"

using namespace std;

// The engine, widgets and menus shared by ZOUMAI and ENCORE: 16 patterns of 8
// tracks of 64 trigs. TRAITS holds what differs: ZOUMAI's playhead also moves
// between clocks, ENCORE's only moves on a clock. Everything is resolved at
// compile time: no virtual call and no test per sample. Patches depend on the
// ids, the JSON keys and the expander messages here.
//
// Timing is exact: every position, trim, length and distance is an integer, in
// units of 1/TICKS of a step (ZOUMAI 1/1008000, which holds tenths, thousandths,
// thirds, sevenths and 32nds exactly; ENCORE 1/32). The head at a sample is the
// floor of its exact position, and a trig starts on the first sample where the
// head has reached it. No float decides a sample, so the same musical position
// always falls on the same sample, whatever the compiler or the platform.

struct TrigAttibutes {

	unsigned long mainAttributes;
	unsigned long probAttributes;

	static const unsigned long TRIG_ACTIVE					= 0x1;
	static const unsigned long TRIG_INITIALIZED			= 0x2;
	static const unsigned long TRIG_SLEEPING				= 0x4;
	static const unsigned long TRIG_TYPE						= 0x18; static const unsigned long trigTypeShift = 3;
	static const unsigned long TRIG_INDEX						= 0xFE0; static const unsigned long trigIndexShift = 5;
	static const unsigned long TRIG_PULSECOUNT			= 0x7F000; static const unsigned long trigPulseCountShift = 12;
	static const unsigned long TRIG_OCTAVE					= 0x780000; static const unsigned long trigOctaveShift = 19;
	static const unsigned long TRIG_SEMITONES				= 0x7800000; static const unsigned long trigSemitonesShift = 23;

	static const unsigned long TRIG_PROBA						= 0xFF;
	static const unsigned long TRIG_COUNT						= 0xFF00; static const unsigned long trigCountShift = 8;
	static const unsigned long TRIG_COUNTRESET			= 0xFF0000; static const unsigned long trigCountResetShift = 16;
	static const unsigned long TRIG_INCOUNT					= 0xFF000000; static const unsigned long trigInCountShift = 24;


	static const unsigned long intiMainAttributes = 1576960;
	static const unsigned long intiProbAttributes = 91136;

	inline void init() {
		mainAttributes = intiMainAttributes;
		probAttributes = intiProbAttributes;
	}

	// The conditions, in the order of the "proba" value kept in patches.
	enum Condition { DICE, COUNT, FILL, NOT_FILL, PRE, NOT_PRE, NEI, NOT_NEI, FIRST, LAST, NUM_CONDITIONS };
	// Random condition draws the eight that keep playing in a running pattern:
	// a random FIRST or LAST would mostly silence the trig.
	static const int NUM_RANDOM_CONDITIONS = FIRST;

	// A note among the n given, semitones 0..11 from C.
	inline void randomize(const int *notes, const int n) {
		setTrigActive(random::uniform()>0.5f);
		setTrigOctave(random::uniform()*2.0f + 2.0f);
		setTrigSemiTones(notes[std::min((int)(random::uniform()*n), n - 1)]);
	}

	inline void randomizeProbs() {
		setTrigProba(random::uniform()*NUM_RANDOM_CONDITIONS);
		setTrigCount(std::min(1 + (int)(random::uniform()*100.0f), 100));
		setTrigCountReset(std::min(1 + (int)(random::uniform()*100.0f), 100));
	}

	inline void fullRandomize(const int *notes, const int n) {
		randomize(notes, n);
		setTrigPulseCount(1 + random::uniform()*9.0f);
	}

	inline bool getTrigActive() {return (mainAttributes & TRIG_ACTIVE) != 0;}
	inline bool getTrigInitialized() {return (mainAttributes & TRIG_INITIALIZED) != 0;}
	inline bool getTrigSleeping() {return (mainAttributes & TRIG_SLEEPING) != 0;}
	inline int getTrigType() {return (mainAttributes & TRIG_TYPE) >> trigTypeShift;}
	inline int getTrigIndex() {return (mainAttributes & TRIG_INDEX) >> trigIndexShift;}
	inline int getTrigPulseCount() {return (mainAttributes & TRIG_PULSECOUNT) >> trigPulseCountShift;}
	inline int getTrigOctave() {return (mainAttributes & TRIG_OCTAVE) >> trigOctaveShift;}
	inline int getTrigSemiTones() {return (mainAttributes & TRIG_SEMITONES) >> trigSemitonesShift;}

	inline int getTrigProba() {return (probAttributes & TRIG_PROBA);}
	inline int getTrigCount() {return (probAttributes & TRIG_COUNT) >> trigCountShift;}
	inline int getTrigCountReset() {return (probAttributes & TRIG_COUNTRESET) >> trigCountResetShift;}
	inline int getTrigInCount() {return (probAttributes & TRIG_INCOUNT) >> trigInCountShift;}

	inline unsigned long getMainAttributes() {return mainAttributes;}
	inline unsigned long getProbAttributes() {return probAttributes;}

	inline void setTrigActive(const bool trigActive) {mainAttributes &= ~TRIG_ACTIVE; if (trigActive) mainAttributes |= TRIG_ACTIVE;}
	inline void setTrigInitialized(const bool trigInitialize) {mainAttributes &= ~TRIG_INITIALIZED; if (trigInitialize) mainAttributes |= TRIG_INITIALIZED;}
	inline void setTrigSleeping(const bool trigSleep) {mainAttributes &= ~TRIG_SLEEPING; if (trigSleep) mainAttributes |= TRIG_SLEEPING;}
	inline void setTrigType(const int trigType) {mainAttributes &= ~TRIG_TYPE; mainAttributes |= (((unsigned long)trigType << trigTypeShift) & TRIG_TYPE);}
	inline void setTrigIndex(const int trigIndex) {mainAttributes &= ~TRIG_INDEX; mainAttributes |= (((unsigned long)trigIndex << trigIndexShift) & TRIG_INDEX);}
	inline void setTrigPulseCount(const int trigPulseCount) {mainAttributes &= ~TRIG_PULSECOUNT; mainAttributes |= (((unsigned long)trigPulseCount << trigPulseCountShift) & TRIG_PULSECOUNT);}
	inline void setTrigOctave(const int trigOctave) {mainAttributes &= ~TRIG_OCTAVE; mainAttributes |= (((unsigned long)trigOctave << trigOctaveShift) & TRIG_OCTAVE);}
	inline void setTrigSemiTones(const int trigSemiTones) {mainAttributes &= ~TRIG_SEMITONES; mainAttributes |= (((unsigned long)trigSemiTones << trigSemitonesShift) & TRIG_SEMITONES);}

	inline void setTrigProba(const int trigProba) {probAttributes &= ~TRIG_PROBA; probAttributes |= ((unsigned long)trigProba & TRIG_PROBA);}
	inline void setTrigCount(const int trigCount) {probAttributes &= ~TRIG_COUNT; probAttributes |= (((unsigned long)trigCount << trigCountShift) & TRIG_COUNT);}
	inline void setTrigCountReset(const int trigCountReset) {probAttributes &= ~TRIG_COUNTRESET; probAttributes |= (((unsigned long)trigCountReset << trigCountResetShift) & TRIG_COUNTRESET);}
	inline void setTrigInCount(const int trigInCount) {probAttributes &= ~TRIG_INCOUNT; probAttributes |= (((unsigned long)trigInCount << trigInCountShift) & TRIG_INCOUNT);}

	inline void toggleTrigActive() {mainAttributes ^= TRIG_ACTIVE;}

	inline void setMainAttributes(const unsigned long _mainAttributes) {mainAttributes = _mainAttributes;}
	inline void setProbAttributes(const unsigned long _probAttributes) {probAttributes = _probAttributes;}

	// Octaves 0..6 as on the panel (-3..+3 V).
	static const int MAX_OCTAVE = 6;

	inline void up() {
		if (getTrigSemiTones()==11) {
			if (getTrigOctave() >= MAX_OCTAVE) return;
			setTrigOctave(getTrigOctave()+1);
			setTrigSemiTones(0);
		}
		else {
			setTrigSemiTones(getTrigSemiTones()+1);
		}
	}

	inline void down() {
		if (getTrigSemiTones()==0) {
			if (getTrigOctave() <= 0) return;
			setTrigOctave(getTrigOctave()-1);
			setTrigSemiTones(11);
		}
		else {
			setTrigSemiTones(getTrigSemiTones()-1);
		}
	}

	// Sets octave and semitone from a 1V/oct voltage, rounded to the nearest semitone.
	inline void setVO(const float vo) {
		int semis = (int)std::round(vo * 12.f);
		int octave = (int)std::floor(semis / 12.f);
		setTrigOctave(clamp(octave + 3, 0, MAX_OCTAVE));
		setTrigSemiTones(semis - octave * 12);
	}

	inline bool hasProbability() {
		return (getTrigProba() != 4) && (getTrigProba() != 5) && !((getTrigProba() == 0) && (getTrigCount() == 100));
	}

	inline float getVO() {
		// In double, then rounded once: the same value whatever the compiler does with floats.
		return (float)((double)getTrigOctave() - 3.0 + (double)getTrigSemiTones() / 12.0);
	}

	inline void init(const bool fill, const bool pre, const bool nei, const bool force, const bool kill, const float dice) {
		if (!getTrigInitialized()) {

			setTrigInitialized(true);

			switch (getTrigProba()) {
				case 0:  // DICE
					if (force) {
						setTrigSleeping(false);
						break;
					}
					if (getTrigCount()<100) {
						if (kill) {
							setTrigSleeping(true);
							break;
						}
						setTrigSleeping((random::uniform()*100)>=(clamp(getTrigCount()+dice*100,0.0f,100.0f)));
					}
					else
						setTrigSleeping(false);
					break;
				case 1:  // COUNT
					if (force) {
						setTrigSleeping(false);
						break;
					}
					if (kill) {
						setTrigSleeping(true);
						break;
					}
					// The counter cycles from 1 to Base (0 is a trig not played yet).
					setTrigSleeping(std::max(getTrigInCount(), 1) > getTrigCount());
					setTrigInCount((std::max(getTrigInCount(), 1) >= getTrigCountReset()) ? 1 : (std::max(getTrigInCount(), 1) + 1));
					break;
				case 2:  // FILL
					if (force) {
						setTrigSleeping(false);
						break;
					}
					if (kill) {
						setTrigSleeping(true);
						break;
					}
					setTrigSleeping(!fill);
					break;
				case 3:  // !FILL
					if (force) {
						setTrigSleeping(false);
						break;
					}
					if (kill) {
						setTrigSleeping(true);
						break;
					}
					setTrigSleeping(fill);
					break;
				case 4:  // PRE
					if (force) {
						setTrigSleeping(false);
						break;
					}
					if (kill) {
						setTrigSleeping(true);
						break;
					}
					setTrigSleeping(!pre);
					break;
				case 5:  // !PRE
					if (force) {
						setTrigSleeping(false);
						break;
					}
					if (kill) {
						setTrigSleeping(true);
						break;
					}
					setTrigSleeping(pre);
					break;
				case 6:  // NEI
					if (force) {
						setTrigSleeping(false);
						break;
					}
					if (kill) {
						setTrigSleeping(true);
						break;
					}
					setTrigSleeping(!nei);
					break;
				case 7:  // !NEI
					if (force) {
						setTrigSleeping(false);
						break;
					}
					if (kill) {
						setTrigSleeping(true);
						break;
					}
					setTrigSleeping(nei);
					break;
				default:
					setTrigSleeping(false);
					break;
			}
		}
	}

	// FIRST and LAST: the track knows, see ZouSeq::trigInitCondition.
	inline void initPass(const bool first, const bool last, const bool force, const bool kill) {
		if (!getTrigInitialized()) {
			setTrigInitialized(true);
			if (force)
				setTrigSleeping(false);
			else if (kill)
				setTrigSleeping(true);
			else
				setTrigSleeping(getTrigProba() == FIRST ? !first : !last);
		}
	}
};

// Track speeds in knob order, slowest first, in eighths of a step (ENCORE: of a
// tick) per clock. The track keeps the index (TRACK_SPEED, 4 bits); the patch
// keeps the factor. ZOUMAI goes up to x4, ENCORE to x8.
static const int zouSpeedEighths[16] = {1, 2, 4, 6, 8, 12, 16, 24, 32, 40, 48, 56, 64, 64, 64, 64};
static const char *const zouSpeedLabels[13] = {"x1/8", "x1/4", "x1/2", "x3/4", "x1", "x3/2", "x2", "x3", "x4", "x5", "x6", "x7", "x8"};

struct TrackAttibutes {

	unsigned long mainAttributes;
	unsigned long refAttributes;

	static const unsigned long TRACK_ACTIVE					= 0x1;
	static const unsigned long TRACK_FORWARD				= 0x2;
	static const unsigned long TRACK_PRE						= 0x4;
	static const unsigned long TRACK_SOLO						= 0x8;
	static const unsigned long TRACK_LENGTH					= 0x7F0; static const unsigned long trackLengthShift = 4;
	static const unsigned long TRACK_READMODE				= 0x3800; static const unsigned long trackReadModeShift = 11;
	static const unsigned long TRACK_SPEED					= 0x3C000; static const unsigned long trackSpeedShift = 14;

	static const unsigned long TRACK_CURRENTTRIG		= 0xFF;
	static const unsigned long TRACK_PLAYEDTRIG			= 0xFF00; static const unsigned long trackPlayedTrigShift = 8;
	static const unsigned long TRACK_PREVTRIG				= 0xFF0000; static const unsigned long trackPrevTrigShift = 16;
	static const unsigned long TRACK_NEXTTRIG				= 0xFF000000; static const unsigned long trackNextTrigShift = 24;

	static const int SPEED_X1 = 4;
	static const unsigned long intiMainAttributes = 65795;
	static const unsigned long intiRefAttributes = 0;

	inline void init() {
		mainAttributes = intiMainAttributes;
		refAttributes = intiRefAttributes;
	}

	inline void randomize() {
		setTrackLength(std::min(1+(int)(random::uniform()*64), 64));
		setTrackReadMode(random::uniform()*5);
	}

	inline bool getTrackActive() {return (mainAttributes & TRACK_ACTIVE) != 0;}
	inline bool getTrackForward() {return (mainAttributes & TRACK_FORWARD) != 0;}
	inline bool getTrackPre() {return (mainAttributes & TRACK_PRE) != 0;}
	inline bool getTrackSolo() {return (mainAttributes & TRACK_SOLO) != 0;}
	inline int getTrackLength() {return (mainAttributes & TRACK_LENGTH) >> trackLengthShift;}
	inline int getTrackReadMode() {return (mainAttributes & TRACK_READMODE) >> trackReadModeShift;}
	inline int getTrackSpeed() {return (mainAttributes & TRACK_SPEED) >> trackSpeedShift;}
	inline int getTrackSpeedEighths() {return zouSpeedEighths[getTrackSpeed()];}

	inline int getTrackCurrentTrig() {return (refAttributes & TRACK_CURRENTTRIG);}
	inline int getTrackPlayedTrig() {return (refAttributes & TRACK_PLAYEDTRIG) >> trackPlayedTrigShift;}
	inline int getTrackPrevTrig() {return (refAttributes & TRACK_PREVTRIG) >> trackPrevTrigShift;}
	inline int getTrackNextTrig() {return (refAttributes & TRACK_NEXTTRIG) >> trackNextTrigShift;}

	inline unsigned long getMainAttributes() {return mainAttributes;}
	inline unsigned long getRefAttributes() {return refAttributes;}

	inline void setTrackActive(const bool trackActive) {mainAttributes &= ~TRACK_ACTIVE; if (trackActive) mainAttributes |= TRACK_ACTIVE;}
	inline void setTrackForward(const bool trackForward) {mainAttributes &= ~TRACK_FORWARD; if (trackForward) mainAttributes |= TRACK_FORWARD;}
	inline void setTrackPre(const bool trackPre) {mainAttributes &= ~TRACK_PRE; if (trackPre) mainAttributes |= TRACK_PRE;}
	inline void setTrackSolo(const bool trackSolo) {mainAttributes &= ~TRACK_SOLO; if (trackSolo) mainAttributes |= TRACK_SOLO;}
	inline void setTrackLength(const int trackLength) {mainAttributes &= ~TRACK_LENGTH; mainAttributes |= (((unsigned long)trackLength << trackLengthShift) & TRACK_LENGTH);}
	inline void setTrackReadMode(const int trackReadMode) {mainAttributes &= ~TRACK_READMODE; mainAttributes |= (((unsigned long)trackReadMode << trackReadModeShift) & TRACK_READMODE);}
	inline void setTrackSpeed(const int trackSpeed) {mainAttributes &= ~TRACK_SPEED; mainAttributes |= (((unsigned long)trackSpeed << trackSpeedShift) & TRACK_SPEED);}

	inline void setTrackCurrentTrig(const int trackCurrentTrig) {refAttributes &= ~TRACK_CURRENTTRIG; refAttributes |= ((unsigned long)trackCurrentTrig & TRACK_CURRENTTRIG);}
	inline void setTrackPlayedTrig(const int trackPlayedTrig) {refAttributes &= ~TRACK_PLAYEDTRIG; refAttributes |= (((unsigned long)trackPlayedTrig << trackPlayedTrigShift) & TRACK_PLAYEDTRIG);}
	inline void setTrackPrevTrig(const int trackPrevTrig) {refAttributes &= ~TRACK_PREVTRIG; refAttributes |= (((unsigned long)trackPrevTrig << trackPrevTrigShift) & TRACK_PREVTRIG);}
	inline void setTrackNextTrig(const int trackNextTrig) {refAttributes &= ~TRACK_NEXTTRIG; refAttributes |= (((unsigned long)trackNextTrig << trackNextTrigShift) & TRACK_NEXTTRIG);}

	inline void toggleTrackActive() {mainAttributes ^= TRACK_ACTIVE;}
	inline void toggleTrackSolo() {mainAttributes ^= TRACK_SOLO;}

	inline void setMainAttributes(const unsigned long _mainAttributes) {mainAttributes = _mainAttributes;}
	inline void setRefAttributes(const unsigned long _refAttributes) {refAttributes = _refAttributes;}
};

template <typename TRAITS>
struct ZouSeq : BidooModule {
	static constexpr int NUM_PATTERNS = 16;
	static constexpr int NUM_TRACKS = 8;
	static constexpr int NUM_TRIGS = 64;
	static constexpr int NUM_SPEEDS = TrackAttibutes::SPEED_X1 + TRAITS::MAX_SPEED + 1;

	using Traits = TRAITS;
	using Time = int;
	static constexpr bool INTERPOLATE = TRAITS::INTERPOLATE;
	// Units per step of all positions and lengths.
	static constexpr int TICKS = TRAITS::TICKS;
	// Clocks a step lasts at x1.
	static constexpr int CLOCKS_PER_STEP = TRAITS::TICKS / TRAITS::UNITS_PER_CLOCK;
	// A knob holds a trig's time as a float, in steps (ZOUMAI) or ticks (ENCORE).
	static float toKnob(const Time t) {return (float)((double)t * TRAITS::KNOB_PER_STEP / TICKS);}
	static Time fromKnob(const float v) {return (Time)std::llround((double)v * TICKS / TRAITS::KNOB_PER_STEP);}
	// The knob's float does not always hold the exact value: only a moved knob writes.
	static Time knobTime(const float v, const Time current) {return (v == toKnob(current)) ? current : fromKnob(v);}
	static int swingFromKnob(const float v) {return (int)std::llround((double)v * 100.0);}
	// 25 % to 400 %, 100 % at the middle of the knob.
	static int distRampFromKnob(const float v) {return (int)std::llround(2500.0 * std::pow(16.0, (double)v));}
	static float distRampToKnob(const int ramp) {return (float)(std::log(ramp / 2500.0) / std::log(16.0));}
	static float inSteps(const long long t) {return (float)((double)t / TICKS);}

	enum ParamIds {
		STEPS_PARAMS,
		TRACKSONOFF_PARAMS = STEPS_PARAMS + 16,
		TRACKSELECT_PARAMS = TRACKSONOFF_PARAMS + NUM_TRACKS,
		TRIGPAGE_PARAM = TRACKSELECT_PARAMS + NUM_TRACKS,
		FILL_PARAM = TRIGPAGE_PARAM + 4,
    RUN_PARAM,
		OCTAVE_PARAMS,
		NOTE_PARAMS = OCTAVE_PARAMS + 7,
		PATTERN_PARAM = NOTE_PARAMS + 12,
		TRACKLENGTH_PARAM,
		TRACKREADMODE_PARAM,
		TRACKSPEED_PARAM,
		TRIGSLIDE_PARAM,
		TRIGTYPE_PARAM,
		TRIGTRIM_PARAM,
		TRIGLENGTH_PARAM,
		TRIGPULSECOUNT_PARAM,
		TRIGPULSEDISTANCE_PARAM,
		TRIGCV1_PARAM,
		TRIGCV2_PARAM,
		TRIGPROBA_PARAM,
		TRIGPROBACOUNT_PARAM,
		TRIGPROBACOUNTRESET_PARAM,
		RECORD_PARAM,
		QUANTIZE_PARAM,
		TRACKROOTNOTE_PARAM,
		TRACKSCALE_PARAM,
		TRACKQUANTIZECV1_PARAM,
    TRIGSLIDETYPE_PARAM,
		// Page 2 of the trig: appended, so the ids above never move.
		TRIGCV1END_PARAM,
		TRIGCV2END_PARAM,
		TRIGPITCHRAMP_PARAM,
		TRIGDISTRAMP_PARAM,
		TRIGPAGE2_PARAM,
		TRACKPAGE2_PARAM,
		TRACKSWING_PARAM,
		NUM_PARAMS
	};

	enum InputIds {
		EXTCLOCK_INPUT,
		RESET_INPUT,
		TRACKRESET_INPUTS,
		G1_INPUT = TRACKRESET_INPUTS + NUM_TRACKS,
		G2_INPUT,
		PATTERN_INPUT,
		TRACKACTIVE_INPUTS,
		FILL_INPUT = TRACKACTIVE_INPUTS + NUM_TRACKS,
		GATE_INPUT,
		VO_INPUT,
		RUN_INPUT,
		NUM_INPUTS
	};

	enum OutputIds {
		GATE_OUTPUTS,
		VO_OUTPUTS = GATE_OUTPUTS + NUM_TRACKS,
		CV1_OUTPUTS = VO_OUTPUTS + NUM_TRACKS,
		CV2_OUTPUTS = CV1_OUTPUTS + NUM_TRACKS,
		POLY_GATE_OUTPUT = CV2_OUTPUTS + NUM_TRACKS,
		POLY_VO_OUTPUT,
		POLY_CV1_OUTPUT,
		POLY_CV2_OUTPUT,
		NUM_OUTPUTS
	};

	enum LightIds {
		STEPS_LIGHTS,
		NOTE_LIGHTS = STEPS_LIGHTS + 16*3,
		TRACKSONOFF_LIGHTS = NOTE_LIGHTS + 12*3,
		TRACKSELECT_LIGHTS = TRACKSONOFF_LIGHTS + NUM_TRACKS*3,
		TRIGPAGE_LIGHTS = TRACKSELECT_LIGHTS + NUM_TRACKS*3,
		OCTAVE_LIGHTS = TRIGPAGE_LIGHTS + 4*3,
		FILL_LIGHT = OCTAVE_LIGHTS + 7*3,
		NUM_LIGHTS
	};

	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger runTrigger;
	dsp::SchmittTrigger trackResetTriggers[NUM_TRACKS];
	dsp::SchmittTrigger trackActiveTriggers[NUM_TRACKS];
	dsp::SchmittTrigger fillTrigger;
	dsp::SchmittTrigger fillInputTrigger;

	float rightMessages[2][64] = {{0.0f}};

	int currentPattern = 0;
	int previousPattern = -1;
	int currentTrack = 0;
	int currentTrig = 0;
	int trigPage = 0;
	int pageIndex = 0;
	bool fill = false;
	int copyTrackId = -1;
	int copyPatternId = -1;
	int copyTrigId = -1;
	int copyPageId = -1;
	bool run = false;
	// Samples between the last two clocks (0 until two clocks were seen) and since
	// the last clock (-1 before the first one). Counted while stopped too.
	long clockInterval = 0;
	long clockCount = -1;
	// Samples left in which a clock still belongs to the reset before it.
	long resetHold = 0;
	bool noteIncoming = false;
	bool globalReset = false;
	bool clockTrigged = false;
	bool copyPageArmed = false;
	bool copyTrigArmed = false;
	bool copyTrackArmed = false;
	bool copyPatternArmed = false;

	int prevTrig[NUM_TRACKS] = {0};
	float prevVO[NUM_TRACKS] = {0.0f};

	quantizer::Quantizer quant;

	TrigAttibutes nTrigsAttibutes[NUM_PATTERNS][NUM_TRACKS][NUM_TRIGS];
	TrackAttibutes nTracksAttibutes[NUM_PATTERNS][NUM_TRACKS];
	float trigSlide[NUM_PATTERNS][NUM_TRACKS][NUM_TRIGS] = {{{0.0f}}};
  bool trigSlideType[NUM_PATTERNS][NUM_TRACKS][NUM_TRIGS] = {{{0}}};
	Time trigTrim[NUM_PATTERNS][NUM_TRACKS][NUM_TRIGS] = {};
	Time trigLength[NUM_PATTERNS][NUM_TRACKS][NUM_TRIGS] = {};
	Time trigPulseDistance[NUM_PATTERNS][NUM_TRACKS][NUM_TRIGS] = {};
	float trigCV1[NUM_PATTERNS][NUM_TRACKS][NUM_TRIGS] = {{{0.0f}}};
	float trigCV2[NUM_PATTERNS][NUM_TRACKS][NUM_TRIGS] = {{{0.0f}}};
	// Ratchet ramps, over the pulses of a trig. CV End: the volts of the last pulse; equal
	// to the trig's CV it is no ramp, and it follows the CV. Pitch: semitones per pulse.
	// Distance: each gap in 1/100 % of the previous one; 0 and 10000 are no ramp.
	float trigCV1End[NUM_PATTERNS][NUM_TRACKS][NUM_TRIGS] = {{{0.0f}}};
	float trigCV2End[NUM_PATTERNS][NUM_TRACKS][NUM_TRIGS] = {{{0.0f}}};
	int trigPitchRamp[NUM_PATTERNS][NUM_TRACKS][NUM_TRIGS] = {{{0}}};
	int trigDistRamp[NUM_PATTERNS][NUM_TRACKS][NUM_TRIGS] = {{{0}}};
	Time trackHead[NUM_PATTERNS][NUM_TRACKS] = {};
	// What the track has played: at its last clock, in eighths of a unit, as a
	// speed is in eighths; and since that clock, see Interpolation.
	long long trackPlayedAtClock[NUM_PATTERNS][NUM_TRACKS] = {{0}};
	int trackLastWhole[NUM_PATTERNS][NUM_TRACKS] = {{0}};
	// ZOUMAI: floor(j * perClock / clockInterval) units j samples after a clock,
	// kept up by adding the quotient and carrying the remainder, as an exact rational.
	struct Interpolation {
		int value = 0, carry = 0, quotient = 0, remainder = 0;
		// After a reset between two clocks: below `resetBoundary` the head counts from the reset.
		int resetBoundary = 0, resetValue = 0;
	};
	Interpolation trackInterpolation[NUM_PATTERNS][NUM_TRACKS];
	int rootNote[NUM_PATTERNS][NUM_TRACKS] = {};
	int scale[NUM_PATTERNS][NUM_TRACKS] = {{0}};
	int quantizeCV1[NUM_PATTERNS][NUM_TRACKS] = {{0}};
	bool slideMode[NUM_PATTERNS][NUM_TRACKS] = {{0}};
	// CV1 and CV2 keep the last trig's value while the gate is off, as V/O does; off: 0 V.
	bool cvHold[NUM_PATTERNS][NUM_TRACKS] = {{0}};
	// The last trig's CV1 and CV2, kept across a reset and a pattern change; 0 V after a load.
	float heldCV1[NUM_TRACKS] = {}, heldCV2[NUM_TRACKS] = {};
	// Swing in 1/100 %, 5000..7500, and the delay of the even-numbered steps it makes.
	int trackSwing[NUM_PATTERNS][NUM_TRACKS] = {{0}};
	int trackSwingUnits[NUM_PATTERNS][NUM_TRACKS] = {{0}};
	// Page 2 of each screen: only what the panel shows.
	bool trackPage2 = false, trigPage2 = false;

	// The pulses of a trig with a distance ramp, from the first one: exact integers,
	// each gap rounded to the nearest unit from the one before. Rebuilt when they change.
	struct PulseTable {
		int pattern = -1, trig = -1, count = -1, ramp = -1;
		Time distance = -1, length = -1;
		long long position[64];
		Time pulse[64];
	};
	// Three per track: the trig playing, the one the head is on and the next.
	PulseTable pulseTables[NUM_TRACKS][3];
	int pulseTableNext[NUM_TRACKS] = {0};

	bool fills[NUM_TRACKS] = {0};
	bool forceTrigs[NUM_TRACKS] = {0};
	bool killTrigs[NUM_TRACKS] = {0};
	float trsp[NUM_TRACKS] = {0.0f};
	float dice[NUM_TRACKS] = {0.0f};
	int rotLeft[NUM_TRACKS] = {0};
	int rotRight[NUM_TRACKS] = {0};
	int rotLen[NUM_TRACKS] = {16,16,16,16,16,16,16,16};
	// Rotations received while the UI holds the edit lock, applied on the next sample.
	int pendingRotLeft[NUM_TRACKS] = {0};
	int pendingRotRight[NUM_TRACKS] = {0};

	// Control rate: the knob <-> trig sync and the lights run every CONTROL_DIV
	// samples (clocks, gates, CVs and slides stay per sample).
	static constexpr int CONTROL_DIV = 16;
	int controlCounter = 0;

	// Quantized VO and CV1 per track, searched again only when the value, the
	// root or the scale changes (audio thread only).
	struct QuantCache {
		float in = NAN;
		int root = -1, scale = -1;
		float out = 0.f;
	};
	QuantCache voQuant[NUM_TRACKS], cv1Quant[NUM_TRACKS], voRampQuant[NUM_TRACKS];

	float quantizeCached(QuantCache &c, float v, int root, int sc) {
		if (v != c.in || root != c.root || sc != c.scale) {
			c.in = v;
			c.root = root;
			c.scale = sc;
			c.out = std::get<0>(quant.closestVoltageInScale(v, root, sc));
		}
		return c.out;
	}

	bool solo = false;

	// For the FIRST condition: the track is on its first pass since the pattern
	// started or a reset; the steps random read modes have played in it.
	bool trackFirstPass[NUM_TRACKS] = {true,true,true,true,true,true,true,true};
	int trackPassSteps[NUM_TRACKS] = {0};

	// The settings of the euclidean fill, in the menu (not saved).
	float euclidHits = 4.0f;
	float euclidRotation = 0.0f;

	// Pattern asked for by the knob and CV. While running it takes over at the
	// end of the current pattern, when patternClock reaches patternLengthTicks().
	int requestedPattern = 0;
	int patternClock = 0;

	// Live recording on the selected track.
	int recTrig = -1;
	Time recStart = 0;
	float recVO = 0.f;

	// Held by the UI thread while it changes the selection or the pattern data.
	// The audio thread only try_locks it to copy the trig and track knobs, so a
	// selection change never mixes the knobs of two trigs.
	std::recursive_mutex editMutex;

  std::string labels[NUM_TRACKS] = {"Track 1","Track 2","Track 3","Track 4","Track 5","Track 6","Track 7","Track 8"};

	// A CV End knob resets to no ramp: the trig's CV.
	struct CvEndQuantity : ParamQuantity {
		int cvParam = 0;
		float getDefaultValue() override {return module->params[cvParam].getValue();}
	};

	ZouSeq() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configInput(PATTERN_INPUT, "Pattern (0.625 V per pattern: 0-10 V adds 0-16 to the knob)");
		configOutput(POLY_GATE_OUTPUT, "Polyphonic gates (tracks 1-8)");
		configOutput(POLY_VO_OUTPUT, "Polyphonic V/Oct (tracks 1-8)");
		configOutput(POLY_CV1_OUTPUT, "Polyphonic CV1 (tracks 1-8)");
		configOutput(POLY_CV2_OUTPUT, "Polyphonic CV2 (tracks 1-8)");

		rightExpander.producerMessage = rightMessages[0];
		rightExpander.consumerMessage = rightMessages[1];

		configButton(FILL_PARAM, "Fill");
    configButton(RUN_PARAM, "Run");

		// Page buttons: the value only drives the light, the page is set on click.
		for (int i=0;i<4;i++) {
			configParam(TRIGPAGE_PARAM+i, 0.0f, 2.0f,  0.0f, "Page " + to_string(i+1) + " (steps " + to_string(i*16+1) + "-" + to_string(i*16+16) + ")");
		}

    configParam(PATTERN_PARAM, 0.0f, NUM_PATTERNS - 1.0f, 0.0f, "Pattern", "", 0.f, 1.f, 1.f);

		configParam(TRACKLENGTH_PARAM, 1.0f, 64.0f, 16.0f, "Track length", " steps");
		configSwitch(TRACKSPEED_PARAM, 0.0f, NUM_SPEEDS - 1.0f, (float)TrackAttibutes::SPEED_X1, "Track speed", std::vector<std::string>(zouSpeedLabels, zouSpeedLabels + NUM_SPEEDS));
		configSwitch(TRACKREADMODE_PARAM, 0.0f, 4.0f, 0.0f, "Read mode", {"► Forward", "◄ Backward", "►◄ Pendulum", "►* Random", "►? Random walk"});
		configSwitch(TRACKROOTNOTE_PARAM, 0.0f, 11.0f, 0.0f, "Root note", {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"});
		configParam(TRACKSCALE_PARAM, 0.0f, quantizer::numScales-1, 0.0f, "Scale (No: not quantized)");
		paramQuantities[TRACKSCALE_PARAM]->snapEnabled = true;
		configSwitch(TRACKQUANTIZECV1_PARAM, 0.0f, 1.0f, 0.0f, "CV1 quantize", {"Free", "Quantized to scale"});

		configParam(TRIGCV1_PARAM, -10.0f, 10.0f, 0.0f, "Trig CV1", " V");
		configParam(TRIGCV2_PARAM, -10.0f, 10.0f, 0.0f, "Trig CV2", " V");
		// ENCORE's knobs are in ticks, 32 per step; shown in steps.
		const float toSteps = 1.f / TRAITS::KNOB_PER_STEP;
		configParam(TRIGLENGTH_PARAM, toKnob(TRAITS::MIN_LENGTH), toKnob(TRAITS::MAX_LENGTH), toKnob(TRAITS::DEFAULT_LENGTH), "Trig length", " steps", 0.f, toSteps);
		configParam(TRIGPULSECOUNT_PARAM, 1.0f, 64.0f, 1.0f, "Pulse count");
		configParam(TRIGPULSEDISTANCE_PARAM, 0.0f, toKnob(TRAITS::MAX_DISTANCE), toKnob(TRAITS::DEFAULT_DISTANCE), "Pulse distance", " steps", 0.f, toSteps);
		configSwitch(TRIGTYPE_PARAM, 0.0f, 2.0f, 0.0f, "Trig type", {"INT. (internal gate)", "GATE1 (G1 input)", "GATE2 (G2 input)"});
		configParam(TRIGTRIM_PARAM, -toKnob(TRAITS::MAX_TRIM), toKnob(TRAITS::MAX_TRIM), 0.0f, "Trim", " steps", 0.f, toSteps);
		configParam(TRIGSLIDE_PARAM, 0.0f, 1.0f, 0.0f, "Slide");
		configSwitch(TRIGPROBA_PARAM, 0.0f, TrigAttibutes::NUM_CONDITIONS - 1.0f, 0.0f, "Trig condition", {"DICE (probability)", "COUNT (first Val. of every Base)", "FILL (only on fill)", "!FILL (not on fill)", "PRE (previous condition played)", "!PRE (previous condition not played)", "NEI (track above played)", "!NEI (track above not played)", "FIRST (first pass only)", "LAST (last pass before a pattern change)"});
		configParam(TRIGPROBACOUNT_PARAM, 1.0f, 100.0f, 100.0f, "Condition value (DICE: %, COUNT: Val.)");
		configParam(TRIGPROBACOUNTRESET_PARAM, 1.0f, 100.0f, 1.0f, "Count base (COUNT cycle length)");
    configSwitch(TRIGSLIDETYPE_PARAM, 0.0f, 1.0f, 0.0f, "Slide type", {"FULL (whole trig)", "1 (one step)"});

		configButton(TRIGPAGE2_PARAM, "Trig page 2 (CV ends, pitch and distance ramps)");
		getParamQuantity(TRIGPAGE2_PARAM)->randomizeEnabled = false;
		configButton(TRACKPAGE2_PARAM, "Track page 2 (swing)");
		getParamQuantity(TRACKPAGE2_PARAM)->randomizeEnabled = false;
		configParam(TRACKSWING_PARAM, 50.0f, 75.0f, 50.0f, "Swing", " %");
		getParamQuantity(TRACKSWING_PARAM)->randomizeEnabled = false;
		configParam<CvEndQuantity>(TRIGCV1END_PARAM, -10.0f, 10.0f, 0.0f, "CV1 End (last pulse)", " V")->cvParam = TRIGCV1_PARAM;
		getParamQuantity(TRIGCV1END_PARAM)->description = "Double-click: no ramp (back to CV1)";
		configParam<CvEndQuantity>(TRIGCV2END_PARAM, -10.0f, 10.0f, 0.0f, "CV2 End (last pulse)", " V")->cvParam = TRIGCV2_PARAM;
		getParamQuantity(TRIGCV2END_PARAM)->description = "Double-click: no ramp (back to CV2)";
		configParam(TRIGPITCHRAMP_PARAM, -12.0f, 12.0f, 0.0f, "Pitch ramp", " semitones per pulse")->snapEnabled = true;
		configParam(TRIGDISTRAMP_PARAM, 0.0f, 1.0f, 0.5f, "Distance ramp", " % of the previous gap", 16.0f, 25.0f);
		for (int id : {TRIGCV1END_PARAM, TRIGCV2END_PARAM, TRIGPITCHRAMP_PARAM, TRIGDISTRAMP_PARAM})
			getParamQuantity(id)->randomizeEnabled = false;

		configSwitch(RECORD_PARAM, 0.0f, 1.0f, 0.0f, "Record on selected track", {"Off", "On"});
		configSwitch(QUANTIZE_PARAM, 0.0f, 1.0f, 1.0f, "Record quantize to steps", {"Off", "On"});

  	for (int i=0;i<16;i++) {
      configParam(STEPS_PARAMS + i, 0.0f, 4.0f, 0.0f, "Step " + to_string(i+1) + " (Shift+click: on/off)");
  	}

		// Octave buttons were never configured: keep Rack's implicit 0..1 range.
		for (int i=0;i<7;i++) {
			configParam(OCTAVE_PARAMS + i, 0.0f, 1.0f, 0.0f, "Octave " + to_string(i+1));
		}

		static const char *noteNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
		for (int i=0;i<12;i++) {
      configParam(NOTE_PARAMS + i, 0.0f, 3.0f, 0.0f, std::string("Note ") + noteNames[i] + " (click again: trig on/off)");
  	}

  	for (int i=0;i<NUM_TRACKS;i++) {
      configSwitch(TRACKSONOFF_PARAMS + i, 0.0f, 2.0f, 1.0f, "Track " + to_string(i+1) + " on/off (Shift+click: solo)", {"Off", "On", "Solo"});
			configParam(TRACKSELECT_PARAMS + i, 0.0f, 1.0f, i == currentTrack ? 1.0f : 0.0f, "Select track " + to_string(i+1));
			for (int j=0;j<NUM_PATTERNS;j++) {
				for (int k=0;k<NUM_TRIGS;k++) {
					nTrigsAttibutes[j][i][k].setTrigIndex(k);
				}
			}
  	}

		onReset();
	}

  unsigned int calc_GCD(unsigned int a, unsigned int b)
  {
    unsigned int shift, tmp;
    if(a == 0) return b;
    if(b == 0) return a;
    for(shift = 0; ((a | b) & 1) == 0; shift++) { a >>= 1; b >>= 1; }
    while((a & 1) == 0) a >>= 1;
    do
    {
      while((b & 1) == 0) b >>= 1;
      if(a > b) { tmp = a; a = b; b = tmp; }
      b = b - a;
    }
    while(b != 0);
    return a << shift;
  }

  void array_cycle_left(void *_ptr, size_t n, size_t es, size_t shift)
  {
    char *ptr = (char*)_ptr;
    if(n <= 1 || !shift) return;
    shift = shift % n;
    size_t i, j, k, gcd = calc_GCD(n, shift);
    char tmp[es];
    for(i = 0; i < gcd; i++) {
      memcpy(tmp, ptr+es*i, es);
      for(j = i; 1; j = k) {
        k = j+shift;
        if(k >= n) k -= n;
        if(k == i) break;
        memcpy(ptr+es*j, ptr+es*k, es);
      }
      memcpy(ptr+es*j, tmp, es);
    }
  }

  void array_cycle_right(void *_ptr, size_t n, size_t es, size_t shift)
  {
    if(!n || !shift) return;
    shift = shift % n;
    array_cycle_left(_ptr, n, es, n - shift);
  }

	void updateTrackToParams() {
		params[TRACKLENGTH_PARAM].setValue(nTracksAttibutes[currentPattern][currentTrack].getTrackLength());
		params[TRACKSPEED_PARAM].setValue(nTracksAttibutes[currentPattern][currentTrack].getTrackSpeed());
		params[TRACKREADMODE_PARAM].setValue(nTracksAttibutes[currentPattern][currentTrack].getTrackReadMode());
		params[TRACKROOTNOTE_PARAM].setValue(rootNote[currentPattern][currentTrack]);
		params[TRACKSCALE_PARAM].setValue(scale[currentPattern][currentTrack]);
		params[TRACKQUANTIZECV1_PARAM].setValue(quantizeCV1[currentPattern][currentTrack]);
		params[TRACKSCALE_PARAM].setValue(scale[currentPattern][currentTrack]);
		params[TRACKROOTNOTE_PARAM].setValue(rootNote[currentPattern][currentTrack]);
		params[TRACKQUANTIZECV1_PARAM].setValue(quantizeCV1[currentPattern][currentTrack]);
		params[TRACKSWING_PARAM].setValue(trackSwing[currentPattern][currentTrack] / 100.0f);
	}

	void updateTrigToParams() {
		params[TRIGLENGTH_PARAM].setValue(toKnob(trigLength[currentPattern][currentTrack][currentTrig]));
		params[TRIGSLIDE_PARAM].setValue(trigSlide[currentPattern][currentTrack][currentTrig]);
		params[TRIGTYPE_PARAM].setValue(nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigType());
		params[TRIGTRIM_PARAM].setValue(toKnob(trigTrim[currentPattern][currentTrack][currentTrig]));
		params[TRIGPULSECOUNT_PARAM].setValue(nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigPulseCount());
		params[TRIGPULSEDISTANCE_PARAM].setValue(toKnob(trigPulseDistance[currentPattern][currentTrack][currentTrig]));
		params[TRIGCV1_PARAM].setValue(trigCV1[currentPattern][currentTrack][currentTrig]);
		params[TRIGCV2_PARAM].setValue(trigCV2[currentPattern][currentTrack][currentTrig]);
		params[TRIGPROBA_PARAM].setValue(nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigProba());
		params[TRIGPROBACOUNT_PARAM].setValue(nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigCount());
		params[TRIGPROBACOUNTRESET_PARAM].setValue(nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigCountReset());
    params[TRIGSLIDETYPE_PARAM].setValue(trigSlideType[currentPattern][currentTrack][currentTrig]);
		params[TRIGCV1END_PARAM].setValue(trigCV1End[currentPattern][currentTrack][currentTrig]);
		params[TRIGCV2END_PARAM].setValue(trigCV2End[currentPattern][currentTrack][currentTrig]);
		params[TRIGPITCHRAMP_PARAM].setValue(trigPitchRamp[currentPattern][currentTrack][currentTrig]);
		params[TRIGDISTRAMP_PARAM].setValue(distRampToKnob(trigDistRamp[currentPattern][currentTrack][currentTrig]));
	}

	void updateTrigVO() {
		for (int i=0;i<7;i++) {
			if (nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigOctave()==i) {
				params[OCTAVE_PARAMS+i].setValue(1.0f);
			}
			else {
				params[OCTAVE_PARAMS+i].setValue(0.0f);
			}
		}

		for (int i=0;i<12;i++) {
			bool focused = nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigSemiTones() == i;
			bool tA = nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigActive();
			if ((i!=1)&&(i!=3)&&(i!=6)&&(i!=8)&&(i!=10)) {
				lights[NOTE_LIGHTS+3*i].setBrightness(focused ? 0.0f : 1.0f);
				lights[NOTE_LIGHTS+3*i+1].setBrightness(focused ? (tA ? 1.0f : 0.5f) : 1.0f);
				lights[NOTE_LIGHTS+3*i+2].setBrightness(focused ? 0.0f : 1.0f);
			}
			else {
				lights[NOTE_LIGHTS+3*i+1].setBrightness(focused ? (tA ? 1.0f : 0.5f) : 0.0f);
			}
		}
	}

	void updateParamsToTrack() {
		nTracksAttibutes[currentPattern][currentTrack].setTrackLength(params[TRACKLENGTH_PARAM].getValue());
		nTracksAttibutes[currentPattern][currentTrack].setTrackSpeed(params[TRACKSPEED_PARAM].getValue());
		nTracksAttibutes[currentPattern][currentTrack].setTrackReadMode(params[TRACKREADMODE_PARAM].getValue());
		rootNote[currentPattern][currentTrack]=params[TRACKROOTNOTE_PARAM].getValue();
		scale[currentPattern][currentTrack]=params[TRACKSCALE_PARAM].getValue();
		quantizeCV1[currentPattern][currentTrack]=params[TRACKQUANTIZECV1_PARAM].getValue();
		const int swing = swingFromKnob(params[TRACKSWING_PARAM].getValue());
		if (swing != trackSwing[currentPattern][currentTrack])
			setSwing(currentPattern, currentTrack, swing);
	}

	// A CV moved takes its End along while they are equal, that is while there is no ramp.
	static void setCV(float &cv, float &end, const float v) {
		if (end == cv)
			end = v;
		cv = v;
	}

	void syncCvKnobs(float &cv, float &end, const int cvParam, const int endParam) {
		const float newEnd = params[endParam].getValue();
		if (newEnd != end) {
			end = newEnd;
			cv = params[cvParam].getValue();
			return;
		}
		setCV(cv, end, params[cvParam].getValue());
		if (end != newEnd)
			params[endParam].setValue(end);
	}

	void updateParamsToTrig() {
		trigLength[currentPattern][currentTrack][currentTrig] = knobTime(params[TRIGLENGTH_PARAM].getValue(), trigLength[currentPattern][currentTrack][currentTrig]);
    trigSlideType[currentPattern][currentTrack][currentTrig] = params[TRIGSLIDETYPE_PARAM].getValue();
		trigSlide[currentPattern][currentTrack][currentTrig] =  params[TRIGSLIDE_PARAM].getValue();
		nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigType(params[TRIGTYPE_PARAM].getValue());
		trigTrim[currentPattern][currentTrack][currentTrig] = knobTime(params[TRIGTRIM_PARAM].getValue(), trigTrim[currentPattern][currentTrack][currentTrig]);
		nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigPulseCount(params[TRIGPULSECOUNT_PARAM].getValue());
		trigPulseDistance[currentPattern][currentTrack][currentTrig] = knobTime(params[TRIGPULSEDISTANCE_PARAM].getValue(), trigPulseDistance[currentPattern][currentTrack][currentTrig]);
		syncCvKnobs(trigCV1[currentPattern][currentTrack][currentTrig], trigCV1End[currentPattern][currentTrack][currentTrig], TRIGCV1_PARAM, TRIGCV1END_PARAM);
		syncCvKnobs(trigCV2[currentPattern][currentTrack][currentTrig], trigCV2End[currentPattern][currentTrack][currentTrig], TRIGCV2_PARAM, TRIGCV2END_PARAM);
		trigPitchRamp[currentPattern][currentTrack][currentTrig] = (int)std::lround(params[TRIGPITCHRAMP_PARAM].getValue());
		trigDistRamp[currentPattern][currentTrack][currentTrig] = distRampFromKnob(params[TRIGDISTRAMP_PARAM].getValue());
		nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigProba(params[TRIGPROBA_PARAM].getValue());
		nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigCount(params[TRIGPROBACOUNT_PARAM].getValue());
		nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigCountReset(params[TRIGPROBACOUNTRESET_PARAM].getValue());
	}


	// Trig times: reals, in steps, in ZOUMAI; integers, in ticks, in ENCORE.
	static json_t *timeToJson(const Time t) {
		if constexpr (INTERPOLATE)
			return json_real((double)t / TICKS);
		else
			return json_integer(t);
	}

	// The knob position of a whole speed.
	static int wholeSpeed(const int speed) {
		return speed + (speed > 1 ? TrackAttibutes::SPEED_X1 : TrackAttibutes::SPEED_X1 - 1);
	}

	// The knob position of the factor in a patch: 1..MAX_SPEED as ever, and the fractions.
	static int speedFromFactor(const double factor) {
		for (int i=0; i<NUM_SPEEDS; i++) {
			if (zouSpeedEighths[i] == factor * 8.0)
				return i;
		}
		return wholeSpeed(clamp((int)factor, 1, (int)TRAITS::MAX_SPEED));
	}

	static Time timeFromJson(json_t *j) {
		if constexpr (INTERPOLATE)
			return (Time)std::llround(json_number_value(j) * TICKS);
		else
			return json_integer_value(j);
	}

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "currentPattern", json_integer(currentPattern));
		json_object_set_new(rootJ, "currentTrack", json_integer(currentTrack));
		json_object_set_new(rootJ, "currentTrig", json_integer(currentTrig));
		json_object_set_new(rootJ, "trigPage", json_integer(trigPage));
    for(size_t i=0; i<NUM_TRACKS; i++) {
      json_object_set_new(rootJ, ("label" + to_string(i)).c_str(), json_string(labels[i].c_str()));
    }
		for (size_t i = 0; i<NUM_PATTERNS; i++) {
			json_t *patternJ = json_object();
			bool patternDefault = true;
			for (size_t j = 0; j < NUM_TRACKS; j++) {
				json_t *trackJ = json_object();
				// What equals the values of trackInit and trigInit is left out: every loader
				// starts from those. slideMode is always written: trackInit keeps it.
				TrackAttibutes &t = nTracksAttibutes[i][j];
				if (!t.getTrackActive())
					json_object_set_new(trackJ, "isActive", json_false());
				if (t.getTrackSolo())
					json_object_set_new(trackJ, "isSolo", json_true());
				if (t.getTrackSpeed() != TrackAttibutes::SPEED_X1)
					json_object_set_new(trackJ, "speed", json_real(t.getTrackSpeedEighths() / 8.0));
				if (t.getTrackReadMode() != 0)
					json_object_set_new(trackJ, "readMode", json_integer(t.getTrackReadMode()));
				if (t.getTrackLength() != 16)
					json_object_set_new(trackJ, "length", json_integer(t.getTrackLength()));
				if (rootNote[i][j] != 0)
					json_object_set_new(trackJ, "rootNote", json_integer(rootNote[i][j]));
				if (scale[i][j] != 0)
					json_object_set_new(trackJ, "scale", json_integer(scale[i][j]));
				if (quantizeCV1[i][j] != 0)
					json_object_set_new(trackJ, "quantizeCV1", json_integer(quantizeCV1[i][j]));
				json_object_set_new(trackJ, "slideMode", json_boolean(slideMode[i][j]));
				if (cvHold[i][j])
					json_object_set_new(trackJ, "cvHold", json_true());
				if (trackSwing[i][j] != 5000)
					json_object_set_new(trackJ, "swing", json_real(trackSwing[i][j] / 100.0));
				for (int k = 0; k < NUM_TRIGS; k++) {
					if (trigIsDefault(i, j, k))
						continue;
					TrigAttibutes &g = nTrigsAttibutes[i][j][k];
					json_t *trigJ = json_object();
					if (g.getTrigActive())
						json_object_set_new(trigJ, "isActive", json_true());
					if (trigSlide[i][j][k] != 0.0f)
						json_object_set_new(trigJ, "slide", json_real(trigSlide[i][j][k]));
					if (g.getTrigType() != 0)
						json_object_set_new(trigJ, "trigType", json_integer(g.getTrigType()));
					if (trigTrim[i][j][k] != 0)
						json_object_set_new(trigJ, "trim", timeToJson(trigTrim[i][j][k]));
					if (trigLength[i][j][k] != TRAITS::DEFAULT_LENGTH)
						json_object_set_new(trigJ, "length", timeToJson(trigLength[i][j][k]));
					if (g.getTrigPulseCount() != 1)
						json_object_set_new(trigJ, "pulseCount", json_integer(g.getTrigPulseCount()));
					if (trigPulseDistance[i][j][k] != TRAITS::DEFAULT_DISTANCE)
						json_object_set_new(trigJ, "pulseDistance", timeToJson(trigPulseDistance[i][j][k]));
					if (g.getTrigProba() != 0)
						json_object_set_new(trigJ, "proba", json_integer(g.getTrigProba()));
					if (g.getTrigCount() != 100)
						json_object_set_new(trigJ, "count", json_integer(g.getTrigCount()));
					if (g.getTrigCountReset() != 1)
						json_object_set_new(trigJ, "countReset", json_integer(g.getTrigCountReset()));
					if (g.getTrigOctave() != 3)
						json_object_set_new(trigJ, "octave", json_integer(g.getTrigOctave()-3));
					if (g.getTrigSemiTones() != 0)
						json_object_set_new(trigJ, "semitones", json_integer(g.getTrigSemiTones()));
					if (trigCV1[i][j][k] != 0.0f)
						json_object_set_new(trigJ, "CV1", json_real(trigCV1[i][j][k]));
					if (trigCV2[i][j][k] != 0.0f)
						json_object_set_new(trigJ, "CV2", json_real(trigCV2[i][j][k]));
					if (trigCV1End[i][j][k] != trigCV1[i][j][k])
						json_object_set_new(trigJ, "CV1End", json_real(trigCV1End[i][j][k]));
					if (trigCV2End[i][j][k] != trigCV2[i][j][k])
						json_object_set_new(trigJ, "CV2End", json_real(trigCV2End[i][j][k]));
					if (trigPitchRamp[i][j][k] != 0)
						json_object_set_new(trigJ, "pitchRamp", json_integer(trigPitchRamp[i][j][k]));
					if (trigDistRamp[i][j][k] != 10000)
						json_object_set_new(trigJ, "distRamp", json_real(trigDistRamp[i][j][k] / 100.0));
					if (trigSlideType[i][j][k])
						json_object_set_new(trigJ, "trigSlideType", json_true());
					json_object_set_new(trackJ, ("trig" + to_string(k)).c_str() , trigJ);
				}
				if (json_object_size(trackJ) > 1 || slideMode[i][j])
					patternDefault = false;
				json_object_set_new(patternJ, ("track" + to_string(j)).c_str() , trackJ);
			}
			// A pattern all at its default is left out.
			if (patternDefault)
				json_decref(patternJ);
			else
				json_object_set_new(rootJ, ("pattern" + to_string(i)).c_str(), patternJ);
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
    BidooModule::dataFromJson(rootJ);
		std::lock_guard<std::recursive_mutex> lock(editMutex);
		for (int i=0; i<NUM_PATTERNS; i++)
			for (int j=0; j<NUM_TRACKS; j++) {
				trackInit(i,j);
				slideMode[i][j] = false;
			}
		for (int j=0; j<NUM_TRACKS; j++)
			heldCV1[j] = heldCV2[j] = 0.0f;
		json_t *currentPatternJ = json_object_get(rootJ, "currentPattern");
		if (currentPatternJ)
			currentPattern = json_integer_value(currentPatternJ);

		json_t *currentTrackJ = json_object_get(rootJ, "currentTrack");
		if (currentTrackJ)
			currentTrack = json_integer_value(currentTrackJ);

		json_t *currentTrigJ = json_object_get(rootJ, "currentTrig");
		if (currentTrigJ)
			currentTrig = json_integer_value(currentTrigJ);

		json_t *trigPageJ = json_object_get(rootJ, "trigPage");
		if (trigPageJ)
			trigPage = json_integer_value(trigPageJ);

    for(size_t i=0; i<NUM_TRACKS; i++) {
      json_t *labelJ=json_object_get(rootJ, ("label"+ to_string(i)).c_str());
      if (labelJ) {
        labels[i] = json_string_value(labelJ);
      }
    }

		for (size_t i=0; i<NUM_PATTERNS;i++) {
			json_t *patternJ = json_object_get(rootJ, ("pattern" + to_string(i)).c_str());
			if (patternJ){
				for(size_t j=0; j<NUM_TRACKS;j++) {
					json_t *trackJ = json_object_get(patternJ, ("track" + to_string(j)).c_str());
					if (trackJ) {
						json_t *isActiveJ = json_object_get(trackJ, "isActive");
						if (isActiveJ)
							nTracksAttibutes[i][j].setTrackActive(json_boolean_value(isActiveJ));
						json_t *isSoloJ = json_object_get(trackJ, "isSolo");
						if (isSoloJ)
							nTracksAttibutes[i][j].setTrackSolo(json_boolean_value(isSoloJ));
						json_t *lengthJ = json_object_get(trackJ, "length");
						if (lengthJ)
							nTracksAttibutes[i][j].setTrackLength(json_integer_value(lengthJ));
						json_t *speedJ = json_object_get(trackJ, "speed");
						if (speedJ)
							nTracksAttibutes[i][j].setTrackSpeed(speedFromFactor(json_number_value(speedJ)));
						json_t *readModeJ = json_object_get(trackJ, "readMode");
						if (readModeJ)
							nTracksAttibutes[i][j].setTrackReadMode(json_integer_value(readModeJ));
						json_t *rootNoteJ = json_object_get(trackJ, "rootNote");
						if (rootNoteJ)
							rootNote[i][j]=clamp((int)json_integer_value(rootNoteJ), 0, 11);
						json_t *scaleJ = json_object_get(trackJ, "scale");
						if (scaleJ)
							scale[i][j]=clamp((int)json_integer_value(scaleJ), 0, quantizer::numScales - 1);
						json_t *quantizeCV1J = json_object_get(trackJ, "quantizeCV1");
						if (quantizeCV1J)
							quantizeCV1[i][j]=json_integer_value(quantizeCV1J);
						json_t *slideModeJ = json_object_get(trackJ, "slideMode");
						if (slideModeJ)
							slideMode[i][j]=json_boolean_value(slideModeJ);
						json_t *cvHoldJ = json_object_get(trackJ, "cvHold");
						if (cvHoldJ)
							cvHold[i][j]=json_boolean_value(cvHoldJ);
						json_t *swingJ = json_object_get(trackJ, "swing");
						if (swingJ)
							setSwing(i, j, (int)std::llround(json_number_value(swingJ) * 100.0));
					}
					for(int k=0;k<NUM_TRIGS;k++) {
						json_t *trigJ = json_object_get(trackJ, ("trig" + to_string(k)).c_str());
						if (trigJ) {
							json_t *isActiveJ = json_object_get(trigJ, "isActive");
							if (isActiveJ)
								nTrigsAttibutes[i][j][k].setTrigActive(json_boolean_value(isActiveJ));
							json_t *slideJ = json_object_get(trigJ, "slide");
							if (slideJ)
								trigSlide[i][j][k] = json_number_value(slideJ);
							json_t *trigTypeJ = json_object_get(trigJ, "trigType");
							if (trigTypeJ)
								nTrigsAttibutes[i][j][k].setTrigType(json_integer_value(trigTypeJ));
							json_t *indexJ = json_object_get(trigJ, "index");
							if (indexJ)
								nTrigsAttibutes[i][j][k].setTrigIndex(json_integer_value(indexJ));
							json_t *trimJ = json_object_get(trigJ, "trim");
							if (trimJ)
								trigTrim[i][j][k] = timeFromJson(trimJ);
							json_t *lengthJ = json_object_get(trigJ, "length");
							if (lengthJ)
								trigLength[i][j][k] = timeFromJson(lengthJ);
							json_t *pulseCountJ = json_object_get(trigJ, "pulseCount");
							if (pulseCountJ)
								nTrigsAttibutes[i][j][k].setTrigPulseCount(json_integer_value(pulseCountJ));
							json_t *pulseDistanceJ = json_object_get(trigJ, "pulseDistance");
							if (pulseDistanceJ)
								trigPulseDistance[i][j][k] = timeFromJson(pulseDistanceJ);
							json_t *probaJ = json_object_get(trigJ, "proba");
							if (probaJ)
								nTrigsAttibutes[i][j][k].setTrigProba(json_integer_value(probaJ));
							json_t *countJ = json_object_get(trigJ, "count");
							if (countJ)
								nTrigsAttibutes[i][j][k].setTrigCount(json_integer_value(countJ));
							json_t *countResetJ = json_object_get(trigJ, "countReset");
							if (countResetJ)
								nTrigsAttibutes[i][j][k].setTrigCountReset(json_integer_value(countResetJ));
							json_t *octaveJ = json_object_get(trigJ, "octave");
							if (octaveJ)
								nTrigsAttibutes[i][j][k].setTrigOctave(json_integer_value(octaveJ)+3);
							json_t *semitonesJ = json_object_get(trigJ, "semitones");
							if (semitonesJ)
								nTrigsAttibutes[i][j][k].setTrigSemiTones(json_integer_value(semitonesJ));
							json_t *CV1J = json_object_get(trigJ, "CV1");
							if (CV1J)
								trigCV1[i][j][k] = json_number_value(CV1J);
							json_t *CV2J = json_object_get(trigJ, "CV2");
							if (CV2J)
								trigCV2[i][j][k] = json_number_value(CV2J);
							json_t *cv1EndJ = json_object_get(trigJ, "CV1End");
							trigCV1End[i][j][k] = cv1EndJ ? clamp((float)json_number_value(cv1EndJ), -10.f, 10.f) : trigCV1[i][j][k];
							json_t *cv2EndJ = json_object_get(trigJ, "CV2End");
							trigCV2End[i][j][k] = cv2EndJ ? clamp((float)json_number_value(cv2EndJ), -10.f, 10.f) : trigCV2[i][j][k];
							if (json_t *pitchRampJ = json_object_get(trigJ, "pitchRamp"))
								trigPitchRamp[i][j][k] = clamp((int)json_integer_value(pitchRampJ), -12, 12);
							if (json_t *distRampJ = json_object_get(trigJ, "distRamp"))
								trigDistRamp[i][j][k] = clamp((int)std::llround(json_number_value(distRampJ) * 100.0), 2500, 40000);
              json_t *trigSlideTypeJ = json_object_get(trigJ, "trigSlideType");
							if (trigSlideTypeJ)
								trigSlideType[i][j][k]=json_boolean_value(trigSlideTypeJ);
						}
					}
				}
			}
		}
		sanitize();
		updateTrackToParams();
		updateTrigToParams();
	}

	// Brings values read from a patch back into range.
	void sanitize() {
		currentPattern = clamp(currentPattern, 0, NUM_PATTERNS - 1);
		requestedPattern = currentPattern;
		currentTrack = clamp(currentTrack, 0, NUM_TRACKS - 1);
		currentTrig = clamp(currentTrig, 0, NUM_TRIGS - 1);
		trigPage = clamp(trigPage, 0, 3);
		for (int i=0; i<NUM_PATTERNS; i++) {
			for (int j=0; j<NUM_TRACKS; j++) {
				TrackAttibutes &t = nTracksAttibutes[i][j];
				t.setTrackLength(clamp(t.getTrackLength(), 1, 64));
				t.setTrackSpeed(clamp(t.getTrackSpeed(), 0, NUM_SPEEDS - 1));
				t.setTrackReadMode(clamp(t.getTrackReadMode(), 0, 4));
				for (int k=0; k<NUM_TRIGS; k++) {
					TrigAttibutes &g = nTrigsAttibutes[i][j][k];
					g.setTrigIndex(k);
					g.setTrigOctave(clamp(g.getTrigOctave(), 0, (int)TrigAttibutes::MAX_OCTAVE));
					g.setTrigSemiTones(clamp(g.getTrigSemiTones(), 0, 11));
					g.setTrigType(clamp(g.getTrigType(), 0, 2));
					g.setTrigProba(clamp(g.getTrigProba(), 0, TrigAttibutes::NUM_CONDITIONS - 1));
					g.setTrigPulseCount(clamp(g.getTrigPulseCount(), 1, 64));
					trigTrim[i][j][k] = clamp(trigTrim[i][j][k], (Time)-TRAITS::MAX_TRIM, (Time)TRAITS::MAX_TRIM);
					trigLength[i][j][k] = clamp(trigLength[i][j][k], (Time)TRAITS::MIN_LENGTH, (Time)TRAITS::MAX_LENGTH);
					trigPulseDistance[i][j][k] = clamp(trigPulseDistance[i][j][k], (Time)0, (Time)TRAITS::MAX_DISTANCE);
				}
			}
		}
	}

	bool trigIsDefault(const int pattern, const int track, const int trig) {
		TrigAttibutes d;
		d.init();
		d.setTrigIndex(trig);
		const TrigAttibutes &g = nTrigsAttibutes[pattern][track][trig];
		// Ignore the playback state kept in the same words.
		const unsigned long mainMask = ~(TrigAttibutes::TRIG_INITIALIZED | TrigAttibutes::TRIG_SLEEPING);
		const unsigned long probMask = ~TrigAttibutes::TRIG_INCOUNT;
		return (g.mainAttributes & mainMask) == (d.mainAttributes & mainMask) && (g.probAttributes & probMask) == (d.probAttributes & probMask)
			&& trigSlide[pattern][track][trig] == 0.0f && trigTrim[pattern][track][trig] == 0
			&& trigLength[pattern][track][trig] == TRAITS::DEFAULT_LENGTH && trigPulseDistance[pattern][track][trig] == TRAITS::DEFAULT_DISTANCE
			&& trigCV1[pattern][track][trig] == 0.0f && trigCV2[pattern][track][trig] == 0.0f
			&& trigCV1End[pattern][track][trig] == 0.0f && trigCV2End[pattern][track][trig] == 0.0f
			&& trigPitchRamp[pattern][track][trig] == 0 && trigDistRamp[pattern][track][trig] == 10000
			&& !trigSlideType[pattern][track][trig];
	}

	// The notes a random note can be: those of the track's scale from its root,
	// all twelve when it has none.
	int trackNotes(const int track, int *notes) {
		const int sc = scale[currentPattern][track];
		if (sc <= 0 || quantizer::scales[sc].numNotes <= 0) {
			for (int i=0; i<12; i++)
				notes[i] = i;
			return 12;
		}
		const quantizer::Scale &q = quantizer::scales[sc];
		for (int i=0; i<q.numNotes; i++)
			notes[i] = (rootNote[currentPattern][track] + q.intervals[i]) % 12;
		return q.numNotes;
	}

	void randomizeTrigNote(const int track, const int trig) {
		int notes[12];
		const int n = trackNotes(track, notes);
		nTrigsAttibutes[currentPattern][track][trig].fullRandomize(notes, n);
	}

	void randomizeTrigNotePlus(const int track, const int trig) {
		randomizeTrigNote(track, trig);
		trigSlide[currentPattern][track][trig]=random::uniform();
    trigSlideType[currentPattern][track][trig]=random::uniform()>0.5f?true:false;
		if constexpr (INTERPOLATE) {
			trigLength[currentPattern][track][trig]=random::uniform()*2.0*TICKS;
			trigPulseDistance[currentPattern][track][trig]=random::uniform()*2.0*TICKS;
		}
		else {
			trigLength[currentPattern][track][trig]=std::min(1+(int)(random::uniform()*TICKS),TICKS);
			trigPulseDistance[currentPattern][track][trig]=std::min((int)(random::uniform()*(TICKS+1)),TICKS);
		}
	}

	void randomizeTrigProb(const int track, const int trig) {
		nTrigsAttibutes[currentPattern][track][trig].randomizeProbs();
	}

	void randomizeTrigCV1(const int track, const int trig) {
		setCV(trigCV1[currentPattern][track][trig], trigCV1End[currentPattern][track][trig], random::uniform()*10.0f);
	}

	void randomizeTrigCV2(const int track, const int trig) {
		setCV(trigCV2[currentPattern][track][trig], trigCV2End[currentPattern][track][trig], random::uniform()*10.0f);
	}

	void fullRandomizeTrig(const int track, const int trig) {
		randomizeTrigNotePlus(track, trig);
		randomizeTrigProb(track, trig);
		randomizeTrigCV1(track, trig);
		randomizeTrigCV2(track, trig);
	}

	void randomizeTrack(const int track) {
		nTracksAttibutes[currentPattern][track].randomize();
	}

	void randomizeTrackTrigsNotes(const int track) {
		for (int i=0; i<NUM_TRIGS; i++) {
			randomizeTrigNote(track,i);
		}
	}

	void randomizeTrackTrigsNotesPlus(const int track) {
		for (int i=0; i<NUM_TRIGS; i++) {
			randomizeTrigNotePlus(track,i);
		}
	}

	void randomizeTrackTrigsProbs(const int track) {
		for (int i=0; i<NUM_TRIGS; i++) {
			randomizeTrigProb(track,i);
		}
	}

	void randomizeTrackTrigsCV1(const int track) {
		for (int i=0; i<NUM_TRIGS; i++) {
			randomizeTrigCV1(track,i);
		}
	}

	void randomizeTrackTrigsCV2(const int track) {
		for (int i=0; i<NUM_TRIGS; i++) {
			randomizeTrigCV2(track,i);
		}
	}

	void fullRandomizeTrack(const int track) {
		randomizeTrack(track);
		// Whole speeds only: a fraction on a long track would hold a pattern for hundreds of clocks.
		nTracksAttibutes[currentPattern][track].setTrackSpeed(wholeSpeed(std::min(1+(int)(random::uniform()*TRAITS::MAX_SPEED),(int)TRAITS::MAX_SPEED)));
		for (int i=0; i<NUM_TRIGS; i++) {
			fullRandomizeTrig(track,i);
		}
	}

	void randomizePageTrigsNotes(const int page) {
		const int start = page * 16;
		for (int i=start; i<start+16; i++) {
			randomizeTrigNote(currentTrack,i);
		}
	}

	void randomizePageTrigsNotesPlus(const int page) {
		const int start = page * 16;
		for (int i=start; i<start+16; i++) {
			randomizeTrigNotePlus(currentTrack,i);
		}
	}

	void randomizePageTrigsProbs(const int page) {
		const int start = page * 16;
		for (int i=start; i<start+16; i++) {
			randomizeTrigProb(currentTrack,i);
		}
	}

	void randomizePageTrigsCV1(const int page) {
		const int start = page * 16;
		for (int i=start; i<start+16; i++) {
			randomizeTrigCV1(currentTrack,i);
		}
	}

	void randomizePageTrigsCV2(const int page) {
		const int start = page * 16;
		for (int i=start; i<start+16; i++) {
			randomizeTrigCV2(currentTrack,i);
		}
	}

	void fullRandomizePage(const int page) {
		const int start = page * 16;
		for (int i=start; i<start+16; i++) {
			fullRandomizeTrig(currentTrack,i);
		}
	}

	void randomizePattern() {
		for (int i=0; i<NUM_TRACKS; i++) {
			randomizeTrack(i);
		}
	}

	void randomizePatternNotes() {
		for (int i=0; i<NUM_TRACKS; i++) {
			randomizeTrackTrigsNotes(i);
		}
	}

	void randomizePatternNotesPlus() {
		for (int i=0; i<NUM_TRACKS; i++) {
			randomizeTrackTrigsNotesPlus(i);
		}
	}

	void randomizePatternNotesProbs() {
		for (int i=0; i<NUM_TRACKS; i++) {
			randomizeTrackTrigsProbs(i);
		}
	}

	void fullRandomizePattern() {
		for (int i=0; i<NUM_TRACKS; i++) {
			fullRandomizeTrack(i);
		}
	}

	void nTrackLeft(const int track, const size_t n, const int len = 0) {
		size_t tLen = len == 0 ? nTracksAttibutes[currentPattern][track].getTrackLength() : len;
		array_cycle_left(trigSlide[currentPattern][track], tLen , sizeof(float), n);
		array_cycle_left(trigTrim[currentPattern][track], tLen, sizeof(Time), n);
		array_cycle_left(trigLength[currentPattern][track], tLen, sizeof(Time), n);
		array_cycle_left(trigPulseDistance[currentPattern][track], tLen, sizeof(Time), n);
		array_cycle_left(trigCV1[currentPattern][track], (size_t) tLen, sizeof(float), n);
		array_cycle_left(trigCV2[currentPattern][track], (size_t) tLen, sizeof(float), n);
		array_cycle_left(trigCV1End[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_left(trigCV2End[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_left(trigPitchRamp[currentPattern][track], tLen, sizeof(int), n);
		array_cycle_left(trigDistRamp[currentPattern][track], tLen, sizeof(int), n);
    array_cycle_left(trigSlideType[currentPattern][track], (size_t) tLen, sizeof(bool), n);

		for (size_t j=0; j<n; j++) {
			TrigAttibutes temp = nTrigsAttibutes[currentPattern][track][0];
			for (size_t i = 0; i < tLen-1; i++) {
				nTrigsAttibutes[currentPattern][track][i] = nTrigsAttibutes[currentPattern][track][i+1];
				nTrigsAttibutes[currentPattern][track][i].setTrigIndex(i);
			}
			nTrigsAttibutes[currentPattern][track][tLen-1] = temp;
			nTrigsAttibutes[currentPattern][track][tLen-1].setTrigIndex(tLen-1);
		}
	}


	void nTrackRight(const int track, const size_t n, const int len = 0) {
		size_t tLen = len == 0 ? nTracksAttibutes[currentPattern][track].getTrackLength() : len;
		array_cycle_right(trigSlide[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_right(trigTrim[currentPattern][track], tLen, sizeof(Time), n);
		array_cycle_right(trigLength[currentPattern][track], tLen, sizeof(Time), n);
		array_cycle_right(trigPulseDistance[currentPattern][track], tLen, sizeof(Time), n);
		array_cycle_right(trigCV1[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_right(trigCV2[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_right(trigCV1End[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_right(trigCV2End[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_right(trigPitchRamp[currentPattern][track], tLen, sizeof(int), n);
		array_cycle_right(trigDistRamp[currentPattern][track], tLen, sizeof(int), n);
    array_cycle_right(trigSlideType[currentPattern][track], (size_t) tLen, sizeof(bool), n);

		for (size_t j=0; j<n; j++) {
			TrigAttibutes temp = nTrigsAttibutes[currentPattern][track][tLen-1];
			for (size_t i = tLen-1; i > 0; i--) {
				nTrigsAttibutes[currentPattern][track][i] = nTrigsAttibutes[currentPattern][track][i-1];
				nTrigsAttibutes[currentPattern][track][i].setTrigIndex(i);
			}
			nTrigsAttibutes[currentPattern][track][0] = temp;
			nTrigsAttibutes[currentPattern][track][0].setTrigIndex(0);
		}
	}

	void trackUp(const int track) {
		for (int i = 0; i < NUM_TRIGS; i++) {
			nTrigsAttibutes[currentPattern][track][i].up();
		}
	}

	void trackDown(const int track) {
		for (int i = 0; i < NUM_TRIGS; i++) {
			nTrigsAttibutes[currentPattern][track][i].down();
		}
	}

	void trigUp(const int trig) {
		nTrigsAttibutes[currentPattern][currentTrack][trig].up();
	}

	void trigDown(const int trig) {
		nTrigsAttibutes[currentPattern][currentTrack][trig].down();
	}


	// Turns the track's trigs on and off, nothing else; its Steps are the length.
	void euclidFill(const int track) {
		const int len = nTracksAttibutes[currentPattern][track].getTrackLength();
		const uint64_t mask = bidoo::dsp::euclidMask((int)euclidHits, len, (int)euclidRotation);
		for (int i=0; i<len; i++)
			nTrigsAttibutes[currentPattern][track][i].setTrigActive((mask >> i) & 1);
	}

	void onRandomize() override {
		std::lock_guard<std::recursive_mutex> lock(editMutex);
		randomizePattern();
		updateTrackToParams();
		updateTrigToParams();
	}

	void pasteTrack(const int fromPattern, const int fromTrack, const int toPattern, const int toTrack) {
		if (fromPattern < 0 || fromTrack < 0)
			return;
		nTracksAttibutes[toPattern][toTrack].setMainAttributes(nTracksAttibutes[fromPattern][fromTrack].getMainAttributes());
		nTracksAttibutes[toPattern][toTrack].setRefAttributes(nTracksAttibutes[fromPattern][fromTrack].getRefAttributes());
		trackHead[toPattern][toTrack] = trackHead[fromPattern][fromTrack];
		trackPlayedAtClock[toPattern][toTrack] = trackPlayedAtClock[fromPattern][fromTrack];
		trackInterpolation[toPattern][toTrack] = trackInterpolation[fromPattern][fromTrack];
		rootNote[toPattern][toTrack] = rootNote[fromPattern][fromTrack];
		scale[toPattern][toTrack] = scale[fromPattern][fromTrack];
		quantizeCV1[toPattern][toTrack] = quantizeCV1[fromPattern][fromTrack];
		cvHold[toPattern][toTrack] = cvHold[fromPattern][fromTrack];
		setSwing(toPattern, toTrack, trackSwing[fromPattern][fromTrack]);
		for (int i=0; i<NUM_TRIGS; i++) {
			pasteTrig(fromPattern,fromTrack,i,toPattern,toTrack,i);
		}
	}

	void pasteTrig(const int fromPattern, const int fromTrack, const int fromTrig, const int toPattern, const int toTrack, const int toTrig) {
		if (fromPattern < 0 || fromTrack < 0 || fromTrig < 0)
			return;
		int index = nTrigsAttibutes[toPattern][toTrack][toTrig].getTrigIndex();
		nTrigsAttibutes[toPattern][toTrack][toTrig].setMainAttributes(nTrigsAttibutes[fromPattern][fromTrack][fromTrig].getMainAttributes());
		nTrigsAttibutes[toPattern][toTrack][toTrig].setTrigIndex(index);
		nTrigsAttibutes[toPattern][toTrack][toTrig].setProbAttributes(nTrigsAttibutes[fromPattern][fromTrack][fromTrig].getProbAttributes());
		trigSlide[toPattern][toTrack][toTrig] = trigSlide[fromPattern][fromTrack][fromTrig];
		trigTrim[toPattern][toTrack][toTrig] = trigTrim[fromPattern][fromTrack][fromTrig];
		trigLength[toPattern][toTrack][toTrig] = trigLength[fromPattern][fromTrack][fromTrig];
		trigPulseDistance[toPattern][toTrack][toTrig] = trigPulseDistance[fromPattern][fromTrack][fromTrig];
		trigCV1[toPattern][toTrack][toTrig] = trigCV1[fromPattern][fromTrack][fromTrig];
		trigCV2[toPattern][toTrack][toTrig] = trigCV2[fromPattern][fromTrack][fromTrig];
		trigCV1End[toPattern][toTrack][toTrig] = trigCV1End[fromPattern][fromTrack][fromTrig];
		trigCV2End[toPattern][toTrack][toTrig] = trigCV2End[fromPattern][fromTrack][fromTrig];
		trigPitchRamp[toPattern][toTrack][toTrig] = trigPitchRamp[fromPattern][fromTrack][fromTrig];
		trigDistRamp[toPattern][toTrack][toTrig] = trigDistRamp[fromPattern][fromTrack][fromTrig];
    trigSlideType[toPattern][toTrack][toTrig] = trigSlideType[fromPattern][fromTrack][fromTrig];
	}

	void pastePattern() {
		if (copyPatternId < 0)
			return;
		for (int i=0; i<NUM_TRACKS; i++) {
			pasteTrack(copyPatternId,i,currentPattern,i);
		}
	}

	void pastePage(const int fromPage, const int toPage) {
		if (fromPage < 0)
			return;
		for(int i=0; i<16; i++) {
			pasteTrig(currentPattern,currentTrack,i+(fromPage*16),currentPattern,currentTrack,i+(toPage*16));
		}
	}

	// The delay in units, to the nearest one: (swing - 50) * 2 % of a step.
	void setSwing(const int pattern, const int track, const int swing) {
		trackSwing[pattern][track] = clamp(swing, 5000, 7500);
		trackSwingUnits[pattern][track] = (int)(((long long)(trackSwing[pattern][track] - 5000) * TICKS * 2 + 5000) / 10000);
	}

	void trigInit(const int pattern, const int track, const int trig) {
		nTrigsAttibutes[pattern][track][trig].init();
		trigSlide[pattern][track][trig] = 0.0f;
		trigTrim[pattern][track][trig] = 0;
		trigLength[pattern][track][trig] = TRAITS::DEFAULT_LENGTH;
		trigPulseDistance[pattern][track][trig] = TRAITS::DEFAULT_DISTANCE;
		trigCV1[pattern][track][trig] = 0.0f;
		trigCV2[pattern][track][trig] = 0.0f;
		trigCV1End[pattern][track][trig] = 0.0f;
		trigCV2End[pattern][track][trig] = 0.0f;
		trigPitchRamp[pattern][track][trig] = 0;
		trigDistRamp[pattern][track][trig] = 10000;
    trigSlideType[pattern][track][trig] = false;
	}

	void pageInit(const int page) {
		const int start = page*16;
		for (int i=start; i<start+16; i++) {
			trigInit(currentPattern, currentTrack, i);
			nTrigsAttibutes[currentPattern][currentTrack][i].setTrigIndex(i);
		}
	}

	void trackInit(const int pattern, const int track) {
		nTracksAttibutes[pattern][track].init();
		trackHead[pattern][track] = 0;
		trackPlayedAtClock[pattern][track] = 0;
		trackInterpolation[pattern][track] = Interpolation();
		trackLastWhole[pattern][track] = 0;
		rootNote[pattern][track] = 0;
		scale[pattern][track] = 0;
		quantizeCV1[pattern][track] = 0;
		cvHold[pattern][track] = false;
		setSwing(pattern, track, 5000);
		for (int i=0; i<NUM_TRIGS; i++) {
			trigInit(pattern, track, i);
			nTrigsAttibutes[pattern][track][i].setTrigIndex(i);
		}
	}

	void onReset() override {
		std::lock_guard<std::recursive_mutex> lock(editMutex);
		for (int i=0; i<NUM_PATTERNS; i++) {
			for (int j=0; j<NUM_TRACKS; j++) {
				trackInit(i,j);
			}
		}
		for (int j=0; j<NUM_TRACKS; j++)
			heldCV1[j] = heldCV2[j] = 0.0f;
		updateTrackToParams();
		updateTrigToParams();
	}

	void process(const ProcessArgs &args) override;

	// What undo keeps of a trig or a track: the edited data, not what playback
	// keeps in the same words.
	struct TrigData {
		unsigned long main, prob;
		float slide, cv1, cv2, cv1End, cv2End;
		Time trim, length, distance;
		int pitchRamp, distRamp;
		bool slideType;
		bool operator==(const TrigData &o) const {
			return main == o.main && prob == o.prob && slide == o.slide && cv1 == o.cv1 && cv2 == o.cv2 && cv1End == o.cv1End && cv2End == o.cv2End && pitchRamp == o.pitchRamp && distRamp == o.distRamp && trim == o.trim && length == o.length && distance == o.distance && slideType == o.slideType;
		}
	};
	struct TrackData {
		unsigned long main;
		int root, scale, quantizeCV1, swing;
		bool slideMode, cvHold;
		float onOff;
		bool operator==(const TrackData &o) const {
			return main == o.main && root == o.root && scale == o.scale && quantizeCV1 == o.quantizeCV1 && swing == o.swing && slideMode == o.slideMode && cvHold == o.cvHold && onOff == o.onOff;
		}
	};
	static constexpr unsigned long TRIG_MAIN_DATA = ~(TrigAttibutes::TRIG_INITIALIZED | TrigAttibutes::TRIG_SLEEPING);
	static constexpr unsigned long TRIG_PROB_DATA = ~TrigAttibutes::TRIG_INCOUNT;
	static constexpr unsigned long TRACK_MAIN_DATA = ~(TrackAttibutes::TRACK_FORWARD | TrackAttibutes::TRACK_PRE);

	TrigData getTrigData(const int pattern, const int track, const int trig) {
		const TrigAttibutes &g = nTrigsAttibutes[pattern][track][trig];
		return {g.mainAttributes & TRIG_MAIN_DATA, g.probAttributes & TRIG_PROB_DATA, trigSlide[pattern][track][trig], trigCV1[pattern][track][trig], trigCV2[pattern][track][trig], trigCV1End[pattern][track][trig], trigCV2End[pattern][track][trig],
			trigTrim[pattern][track][trig], trigLength[pattern][track][trig], trigPulseDistance[pattern][track][trig], trigPitchRamp[pattern][track][trig], trigDistRamp[pattern][track][trig], trigSlideType[pattern][track][trig]};
	}

	void setTrigData(const int pattern, const int track, const int trig, const TrigData &d) {
		TrigAttibutes &g = nTrigsAttibutes[pattern][track][trig];
		g.mainAttributes = (g.mainAttributes & ~TRIG_MAIN_DATA) | d.main;
		g.probAttributes = (g.probAttributes & ~TRIG_PROB_DATA) | d.prob;
		trigSlide[pattern][track][trig] = d.slide;
		trigCV1[pattern][track][trig] = d.cv1;
		trigCV2[pattern][track][trig] = d.cv2;
		trigCV1End[pattern][track][trig] = d.cv1End;
		trigCV2End[pattern][track][trig] = d.cv2End;
		trigPitchRamp[pattern][track][trig] = d.pitchRamp;
		trigDistRamp[pattern][track][trig] = d.distRamp;
		trigTrim[pattern][track][trig] = d.trim;
		trigLength[pattern][track][trig] = d.length;
		trigPulseDistance[pattern][track][trig] = d.distance;
		trigSlideType[pattern][track][trig] = d.slideType;
	}

	TrackData getTrackData(const int pattern, const int track) {
		return {nTracksAttibutes[pattern][track].mainAttributes & TRACK_MAIN_DATA, rootNote[pattern][track], scale[pattern][track], quantizeCV1[pattern][track], trackSwing[pattern][track], slideMode[pattern][track], cvHold[pattern][track], params[TRACKSONOFF_PARAMS + track].getValue()};
	}

	void setTrackData(const int pattern, const int track, const TrackData &d) {
		TrackAttibutes &t = nTracksAttibutes[pattern][track];
		t.mainAttributes = (t.mainAttributes & ~TRACK_MAIN_DATA) | d.main;
		rootNote[pattern][track] = d.root;
		scale[pattern][track] = d.scale;
		quantizeCV1[pattern][track] = d.quantizeCV1;
		slideMode[pattern][track] = d.slideMode;
		cvHold[pattern][track] = d.cvHold;
		setSwing(pattern, track, d.swing);
		params[TRACKSONOFF_PARAMS + track].setValue(d.onOff);
	}

	// The trigs and tracks an edit changed, before and after: a full pattern is
	// only a few memory copies under the lock, against a few ms for the patch JSON.
	struct TrigChange {
		short track, trig;
		TrigData before, after;
	};
	struct TrackChange {
		short track;
		TrackData before, after;
	};

	struct EditAction : history::ModuleAction {
		int pattern;
		std::vector<TrigChange> trigs;
		std::vector<TrackChange> tracks;

		void apply(const bool undo) {
			ZouSeq *m = dynamic_cast<ZouSeq*>(APP->engine->getModule(moduleId));
			if (!m)
				return;
			std::lock_guard<std::recursive_mutex> lock(m->editMutex);
			for (const TrigChange &c : trigs)
				m->setTrigData(pattern, c.track, c.trig, undo ? c.before : c.after);
			for (const TrackChange &c : tracks)
				m->setTrackData(pattern, c.track, undo ? c.before : c.after);
			m->updateTrackToParams();
			m->updateTrigToParams();
		}
		void undo() override {apply(true);}
		void redo() override {apply(false);}
	};

	// Runs an edit of the current pattern and adds it to Rack's undo history if it
	// changed anything. tracks: the tracks it may touch, from firstTrack; trigs:
	// the same for the trigs of those tracks; withTrack: also the track settings.
	// UI thread, under the edit lock.
	template <typename F>
	void edit(const char *name, const int firstTrack, const int tracks, const int firstTrig, const int trigs, const bool withTrack, F change) {
		const int pattern = currentPattern;
		std::vector<TrigData> trigsBefore;
		std::vector<TrackData> tracksBefore;
		trigsBefore.reserve(tracks * trigs);
		for (int t=firstTrack; t<firstTrack+tracks; t++) {
			for (int g=firstTrig; g<firstTrig+trigs; g++)
				trigsBefore.push_back(getTrigData(pattern, t, g));
			if (withTrack)
				tracksBefore.push_back(getTrackData(pattern, t));
		}
		change();
		if (!APP->history)
			return;
		EditAction *a = new EditAction;
		a->name = name;
		a->moduleId = id;
		a->pattern = pattern;
		size_t i = 0;
		for (int t=firstTrack; t<firstTrack+tracks; t++) {
			for (int g=firstTrig; g<firstTrig+trigs; g++, i++) {
				const TrigData after = getTrigData(pattern, t, g);
				if (!(after == trigsBefore[i]))
					a->trigs.push_back({(short)t, (short)g, trigsBefore[i], after});
			}
			if (withTrack) {
				const TrackData after = getTrackData(pattern, t);
				if (!(after == tracksBefore[t-firstTrack]))
					a->tracks.push_back({(short)t, tracksBefore[t-firstTrack], after});
			}
		}
		if (a->trigs.empty() && a->tracks.empty())
			delete a;
		else
			APP->history->push(a);
	}

	// A knob on a trig or a track writes its value to the selected one. Rack's own
	// undo of a knob only moves the knob back, which would write the old value to
	// whatever trig is selected by then: it is replaced by this one, tied to the
	// trig or track the knob was on.
	static bool isTrackKnob(const int p) {
		return p == TRACKSWING_PARAM || p == TRACKLENGTH_PARAM || p == TRACKREADMODE_PARAM || p == TRACKSPEED_PARAM || p == TRACKROOTNOTE_PARAM || p == TRACKSCALE_PARAM || p == TRACKQUANTIZECV1_PARAM;
	}

	static bool isTrigKnob(const int p) {
		return p == TRIGSLIDE_PARAM || p == TRIGTYPE_PARAM || p == TRIGTRIM_PARAM || p == TRIGLENGTH_PARAM || p == TRIGPULSECOUNT_PARAM || p == TRIGPULSEDISTANCE_PARAM
			|| p == TRIGCV1END_PARAM || p == TRIGCV2END_PARAM || p == TRIGPITCHRAMP_PARAM || p == TRIGDISTRAMP_PARAM
			|| p == TRIGCV1_PARAM || p == TRIGCV2_PARAM || p == TRIGPROBA_PARAM || p == TRIGPROBACOUNT_PARAM || p == TRIGPROBACOUNTRESET_PARAM || p == TRIGSLIDETYPE_PARAM;
	}

	// The same conversions as updateParamsToTrack and updateParamsToTrig.
	void setKnobValue(const int pattern, const int track, const int trig, const int p, const float v) {
		TrackAttibutes &t = nTracksAttibutes[pattern][track];
		TrigAttibutes &g = nTrigsAttibutes[pattern][track][trig];
		switch (p) {
			case TRACKSWING_PARAM: setSwing(pattern, track, swingFromKnob(v)); break;
			case TRACKLENGTH_PARAM: t.setTrackLength(v); break;
			case TRACKSPEED_PARAM: t.setTrackSpeed(v); break;
			case TRACKREADMODE_PARAM: t.setTrackReadMode(v); break;
			case TRACKROOTNOTE_PARAM: rootNote[pattern][track] = v; break;
			case TRACKSCALE_PARAM: scale[pattern][track] = v; break;
			case TRACKQUANTIZECV1_PARAM: quantizeCV1[pattern][track] = v; break;
			case TRIGLENGTH_PARAM: trigLength[pattern][track][trig] = fromKnob(v); break;
			case TRIGSLIDETYPE_PARAM: trigSlideType[pattern][track][trig] = v; break;
			case TRIGSLIDE_PARAM: trigSlide[pattern][track][trig] = v; break;
			case TRIGTYPE_PARAM: g.setTrigType(v); break;
			case TRIGTRIM_PARAM: trigTrim[pattern][track][trig] = fromKnob(v); break;
			case TRIGPULSECOUNT_PARAM: g.setTrigPulseCount(v); break;
			case TRIGPULSEDISTANCE_PARAM: trigPulseDistance[pattern][track][trig] = fromKnob(v); break;
			case TRIGCV1END_PARAM: trigCV1End[pattern][track][trig] = v; break;
			case TRIGCV2END_PARAM: trigCV2End[pattern][track][trig] = v; break;
			case TRIGPITCHRAMP_PARAM: trigPitchRamp[pattern][track][trig] = (int)std::lround(v); break;
			case TRIGDISTRAMP_PARAM: trigDistRamp[pattern][track][trig] = distRampFromKnob(v); break;
			case TRIGCV1_PARAM: setCV(trigCV1[pattern][track][trig], trigCV1End[pattern][track][trig], v); break;
			case TRIGCV2_PARAM: setCV(trigCV2[pattern][track][trig], trigCV2End[pattern][track][trig], v); break;
			case TRIGPROBA_PARAM: g.setTrigProba(v); break;
			case TRIGPROBACOUNT_PARAM: g.setTrigCount(v); break;
			case TRIGPROBACOUNTRESET_PARAM: g.setTrigCountReset(v); break;
			default: break;
		}
	}

	struct KnobAction : history::ModuleAction {
		int pattern, track, trig, paramId;
		float oldValue, newValue;

		void apply(const float v) {
			ZouSeq *m = dynamic_cast<ZouSeq*>(APP->engine->getModule(moduleId));
			if (!m)
				return;
			std::lock_guard<std::recursive_mutex> lock(m->editMutex);
			m->setKnobValue(pattern, track, trig, paramId, v);
			// The knob only shows the value if it is on that trig or track.
			if (pattern == m->currentPattern && track == m->currentTrack && (isTrackKnob(paramId) || trig == m->currentTrig))
			{
				m->params[paramId].setValue(v);
				if (paramId == TRIGCV1_PARAM)
					m->params[TRIGCV1END_PARAM].setValue(m->trigCV1End[pattern][track][trig]);
				if (paramId == TRIGCV2_PARAM)
					m->params[TRIGCV2END_PARAM].setValue(m->trigCV2End[pattern][track][trig]);
			}
		}
		void undo() override {apply(oldValue);}
		void redo() override {apply(newValue);}
	};

	// UI thread, once a frame: Rack pushes its action when the drag ends.
	void adoptKnobHistory() {
		history::State *h = APP->history;
		if (!h || h->actionIndex <= 0 || h->actionIndex > (int)h->actions.size())
			return;
		history::ParamChange *pc = dynamic_cast<history::ParamChange*>(h->actions[h->actionIndex - 1]);
		if (!pc || pc->moduleId != id || !(isTrackKnob(pc->paramId) || isTrigKnob(pc->paramId)))
			return;
		KnobAction *a = new KnobAction;
		a->name = pc->name;
		a->moduleId = id;
		a->pattern = currentPattern;
		a->track = currentTrack;
		a->trig = currentTrig;
		a->paramId = pc->paramId;
		a->oldValue = pc->oldValue;
		a->newValue = pc->newValue;
		h->actions[h->actionIndex - 1] = a;
		delete pc;
	}

	// The swing slider: a drag is one action.
	struct SwingAction : history::ModuleAction {
		int pattern, track, oldValue, newValue;

		void apply(const int v) {
			ZouSeq *m = dynamic_cast<ZouSeq*>(APP->engine->getModule(moduleId));
			if (!m)
				return;
			std::lock_guard<std::recursive_mutex> lock(m->editMutex);
			m->setSwing(pattern, track, v);
			if (pattern == m->currentPattern && track == m->currentTrack)
				m->params[TRACKSWING_PARAM].setValue(v / 100.0f);
		}
		void undo() override {apply(oldValue);}
		void redo() override {apply(newValue);}
	};

	void editSwing(const int track, const int value) {
		std::lock_guard<std::recursive_mutex> lock(editMutex);
		const int old = trackSwing[currentPattern][track];
		setSwing(currentPattern, track, value);
		if (track == currentTrack)
			params[TRACKSWING_PARAM].setValue(trackSwing[currentPattern][track] / 100.0f);
		history::State *h = APP->history;
		if (!h || trackSwing[currentPattern][track] == old)
			return;
		if (h->actionIndex > 0 && h->actionIndex <= (int)h->actions.size()) {
			SwingAction *last = dynamic_cast<SwingAction*>(h->actions[h->actionIndex - 1]);
			if (last && last->moduleId == id && last->pattern == currentPattern && last->track == track) {
				last->newValue = trackSwing[currentPattern][track];
				return;
			}
		}
		SwingAction *a = new SwingAction;
		a->name = "track swing";
		a->moduleId = id;
		a->pattern = currentPattern;
		a->track = track;
		a->oldValue = old;
		a->newValue = trackSwing[currentPattern][track];
		h->push(a);
	}

	// A label: the keys typed in a row are one action.
	struct LabelAction : history::ModuleAction {
		int track;
		std::string oldText, newText;

		void apply(const std::string &text) {
			ZouSeq *m = dynamic_cast<ZouSeq*>(APP->engine->getModule(moduleId));
			if (!m)
				return;
			std::lock_guard<std::recursive_mutex> lock(m->editMutex);
			m->labels[track] = text;
		}
		void undo() override {apply(oldText);}
		void redo() override {apply(newText);}
	};

	void editLabel(const int track, const std::string &text) {
		const std::string old = labels[track];
		labels[track] = text;
		history::State *h = APP->history;
		if (!h)
			return;
		if (h->actionIndex > 0 && h->actionIndex <= (int)h->actions.size()) {
			LabelAction *last = dynamic_cast<LabelAction*>(h->actions[h->actionIndex - 1]);
			if (last && last->moduleId == id && last->track == track) {
				last->newText = text;
				return;
			}
		}
		LabelAction *a = new LabelAction;
		a->name = "edit label";
		a->moduleId = id;
		a->track = track;
		a->oldText = old;
		a->newText = text;
		h->push(a);
	}

	// Live recording into the selected track, at the playhead (in steps or ticks).
	void recordStep(const int i) {
		int len = nTracksAttibutes[currentPattern][i].getTrackLength();
		Time head = clamp(trackHead[currentPattern][i], 0, len * TICKS - 1);
		float vo = inputs[VO_INPUT].getVoltage();
		bool quantize = params[QUANTIZE_PARAM].getValue() != 0.0f;

		auto endNote = [&]() {
			if (recTrig < 0)
				return;
			Time length = head - recStart;
			if (length <= 0)
				length += len * TICKS;
			trigLength[currentPattern][i][recTrig] = clamp(length, (Time)TRAITS::SHORTEST_NOTE, (Time)TRAITS::MAX_LENGTH);
			if (recTrig == currentTrig)
				updateTrigToParams();
			recTrig = -1;
		};
		auto startNote = [&]() {
			int step;
			Time trim;
			if (quantize) {
				step = ((head + TICKS / 2) / TICKS) % len;
				trim = 0;
			}
			else {
				step = head / TICKS;
				trim = head - step * TICKS;
			}
			const Time played = step * TICKS + trim;
			if (!quantize && (step & 1))
				trim = std::max(trim - trackSwingUnits[currentPattern][i], -(Time)TRAITS::MAX_TRIM);
			TrigAttibutes &t = nTrigsAttibutes[currentPattern][i][step];
			t.setTrigActive(true);
			t.setVO(vo);
			trigTrim[currentPattern][i][step] = trim;
			trigLength[currentPattern][i][step] = TRAITS::SHORTEST_NOTE;
			recTrig = step;
			recStart = played;
			recVO = vo;
			if (step == currentTrig)
				updateTrigToParams();
		};

		if (inputs[GATE_INPUT].getVoltage() > 0.0f) {
			if (!noteIncoming) {
				noteIncoming = true;
				startNote();
			}
			else if (std::fabs(vo - recVO) > 1.f / 24.f) {
				// Legato: a new pitch under the same gate starts a new note.
				endNote();
				startNote();
			}
		}
		else if (noteIncoming) {
			noteIncoming = false;
			endNote();
		}
	}

	bool patternIsSoloed() {
		for (size_t i = 0; i < NUM_TRACKS; i++) {
			if (nTracksAttibutes[currentPattern][i].getTrackSolo()) {
				return true;
			}
		}
		return false;
	}


	// A ramp on the distance needs its pulses worked out; 100 % and one pulse do not.
	bool trigHasPulseTable(const int track, const int trig) {
		return trigDistRamp[currentPattern][track][trig] != 10000 && nTrigsAttibutes[currentPattern][track][trig].getTrigPulseCount() > 1 && trigPulseDistance[currentPattern][track][trig] > 0;
	}

	// The first gap is the Dist. knob, each next one is Distance % of it, to the nearest
	// unit; no gap under 1/64 of a step or over MAX_DISTANCE. A pulse lasts its length, at
	// most half the gap after it, so its gate falls before the next one rises.
	const PulseTable &trigPulseTable(const int track, const int trig) {
		const int count = nTrigsAttibutes[currentPattern][track][trig].getTrigPulseCount();
		const int ramp = trigDistRamp[currentPattern][track][trig];
		const Time distance = trigPulseDistance[currentPattern][track][trig];
		const Time length = trigLength[currentPattern][track][trig];
		for (const PulseTable &t : pulseTables[track])
			if (t.pattern == currentPattern && t.trig == trig && t.count == count && t.ramp == ramp && t.distance == distance && t.length == length)
				return t;
		PulseTable &t = pulseTables[track][pulseTableNext[track]];
		pulseTableNext[track] = (pulseTableNext[track] + 1) % 3;
		{
			t.pattern = currentPattern;
			t.trig = trig;
			t.count = count;
			t.ramp = ramp;
			t.distance = distance;
			t.length = length;
			const long long smallest = std::max(TICKS / 64, 2);
			long long gap = clamp((long long)distance, smallest, (long long)TRAITS::MAX_DISTANCE);
			t.position[0] = 0;
			for (int i = 1; i < count; i++) {
				t.position[i] = t.position[i-1] + gap;
				t.pulse[i-1] = (Time)std::min((long long)length, gap / 2);
				gap = clamp((gap * ramp + 5000) / 10000, smallest, (long long)TRAITS::MAX_DISTANCE);
			}
			t.pulse[count-1] = length;
		}
		return t;
	}

	// Which pulse the track is at: the last one started, the first before the trig plays.
	int trigPulseAt(const int track, const int trig) {
		const Time rTP = trigGetRelativeTrackPosition(track, trig);
		const int count = nTrigsAttibutes[currentPattern][track][trig].getTrigPulseCount();
		if (rTP <= 0 || count < 2)
			return 0;
		if (trigHasPulseTable(track, trig)) {
			const PulseTable &t = trigPulseTable(track, trig);
			return (int)(std::upper_bound(t.position, t.position + count, (long long)rTP) - t.position) - 1;
		}
		const Time distance = trigPulseDistance[currentPattern][track][trig];
		return distance == 0 ? 0 : std::min(rTP / distance, count - 1);
	}

	float trackGetGate(const int track, const int tPT) {
		if (!nTrigsAttibutes[currentPattern][track][tPT].getTrigActive() || nTrigsAttibutes[currentPattern][track][tPT].getTrigSleeping())
			return 0.0f;
		const Time rTP = trigGetRelativeTrackPosition(track, tPT);
		if (rTP < 0)
			return 0.0f;
		if (trigHasPulseTable(track, tPT)) {
			const PulseTable &t = trigPulseTable(track, tPT);
			const int pulse = (int)(std::upper_bound(t.position, t.position + t.count, (long long)rTP) - t.position) - 1;
			return (long long)rTP < t.position[pulse] + t.pulse[pulse] ? 10.0f : 0.0f;
		}
		const Time length = trigLength[currentPattern][track][tPT];
		if (rTP < length)
			return 10.0f;
		const Time distance = trigPulseDistance[currentPattern][track][tPT];
		if (distance == 0)
			return 0.0f;
		const int cPulses = rTP / distance;
		return (cPulses < nTrigsAttibutes[currentPattern][track][tPT].getTrigPulseCount() && (long long)rTP < (long long)cPulses * distance + length) ? 10.0f : 0.0f;
	}

	// A CV at the present pulse: equal steps from the first pulse to the last.
	float trigRampedCV(const int track, const int trig, const float cv, const float end) {
		const int count = nTrigsAttibutes[currentPattern][track][trig].getTrigPulseCount();
		if (end == cv || count < 2)
			return cv;
		const int pulse = trigPulseAt(track, trig);
		return pulse == count - 1 ? end : (float)((double)cv + ((double)end - (double)cv) * pulse / (count - 1));
	}

	Time trigGetRelativeTrackPosition(const int track, const int trig) {
		return trackHead[currentPattern][track] - trigGetTrimedIndex(track, trig);
	}

	long long trigGetFullLength(const int track, const int trig) {
		if (trigHasPulseTable(track, trig)) {
			const PulseTable &t = trigPulseTable(track, trig);
			return t.position[t.count-1] + t.pulse[t.count-1];
		}
		const Time length = trigLength[currentPattern][track][trig];
		const int pulses = nTrigsAttibutes[currentPattern][track][trig].getTrigPulseCount();
		return pulses == 1 ? length : (long long)pulses * trigPulseDistance[currentPattern][track][trig] + length;
	}

	bool trigGetIsRead(const int track, const int trig, const Time trackPosition) {
		Time tI = trigGetTrimedIndex(track,trig);
		return (trackPosition>=tI) && ((long long)trackPosition<(long long)tI+std::max(trigGetFullLength(track, trig),1LL));
	}

	// Even-numbered steps (index 1, 3...) start later on a swung track, added to their
	// trim. A swung trig stays inside its own step, far enough from the end for the head
	// to land on it, where the track finds it: never lost or played twice.
	Time trigGetTrimedIndex(const int track, const int trig) {
		const int index = nTrigsAttibutes[currentPattern][track][trig].getTrigIndex();
		const Time trim = trigTrim[currentPattern][track][trig];
		const Time swing = trackSwingUnits[currentPattern][track];
		if (swing != 0 && (index & 1)) {
			const Time room = INTERPOLATE ? TICKS / 64 : (nTracksAttibutes[currentPattern][track].getTrackSpeedEighths() + 7) / 8;
			return index * TICKS + std::min(trim + swing, TICKS - room);
		}
		return index * TICKS + trim;
	}

	// Slide shape: x in 0..1, slide amount in 0..1 sets the exponent.
	// pow(x, (int)(slide * 99) / 100), from a table shared by every instance.
	static float slideCurve(const float slide, const float x) {
		static const int POINTS = 1024;
		static const std::vector<float> table = [] {
			std::vector<float> t(100 * (POINTS + 1));
			for (int i = 0; i < 100; i++)
				for (int j = 0; j <= POINTS; j++)
					t[i * (POINTS + 1) + j] = std::pow((float)j / POINTS, i * 0.01f);
			return t;
		}();
		int curve = clamp((int)(slide * 99.0f), 0, 99);
		float pos = clamp(x, 0.0f, 1.0f) * POINTS;
		int j = std::min((int)pos, POINTS - 1);
		const float *row = table.data() + curve * (POINTS + 1);
		return row[j] + (pos - j) * (row[j + 1] - row[j]);
	}

	// Clocks until the longest track of the current pattern loops.
	int patternLengthTicks() {
		int ticks = 1;
		for (int i = 0; i < NUM_TRACKS; i++) {
			TrackAttibutes &t = nTracksAttibutes[currentPattern][i];
			int speed = t.getTrackSpeedEighths();
			ticks = std::max(ticks, (t.getTrackLength() * CLOCKS_PER_STEP * 8 + speed - 1) / speed);
		}
		return ticks;
	}

	float trackGetVO(const int track, const int tPT, const bool quantize = false) {
		const float played = trackGetSlidVO(track, tPT, quantize);
		const int ramp = trigPitchRamp[currentPattern][track][tPT];
		if (ramp == 0)
			return played;
		const int pulse = trigPulseAt(track, tPT);
		if (pulse == 0)
			return played;
		// Semitones per pulse from the trig's note, each pulse quantized on its own;
		// a slide goes on underneath, towards the trig's note.
		const float vo = nTrigsAttibutes[currentPattern][track][tPT].getVO() + trsp[track];
		const float raised = clamp((float)((double)vo + (double)(pulse * ramp) / 12.0), -10.f, 10.f);
		const float target = quantize ? quantizeCached(voRampQuant[track], raised, rootNote[currentPattern][track], scale[currentPattern][track]) : raised;
		if (trigSlide[currentPattern][track][tPT] == 0.0f)
			return target;
		const float note = quantize ? quantizeCached(voQuant[track], vo, rootNote[currentPattern][track], scale[currentPattern][track]) : vo;
		return played + target - note;
	}

	float trackGetSlidVO(const int track, const int tPT, const bool quantize = false) {
		float vo = nTrigsAttibutes[currentPattern][track][tPT].getVO() + trsp[track];
		if (trigSlide[currentPattern][track][tPT] == 0.0f) {
			return quantize ? quantizeCached(voQuant[track], vo, rootNote[currentPattern][track], scale[currentPattern][track]) : vo;
		}
		else
		{
			const float fullLength = inSteps(trigGetFullLength(track,tPT));
			const float rel = inSteps(trigGetRelativeTrackPosition(track, tPT));
			float voQ = quantize ? quantizeCached(voQuant[track], vo, rootNote[currentPattern][track], scale[currentPattern][track]) : vo;
			if (fullLength > 0.0f) {
				const float x = slideMode[currentPattern][track] ? rel : rel*(1.0f/max((int)(std::fabs(voQ - prevVO[track]) + 0.0001f),1));
				const float subPhase = trigSlideType[currentPattern][track][tPT] ? clamp(x,0.0f,1.0f) : clamp(x,0.0f,fullLength)/fullLength;
				return voQ - (1.0f - slideCurve(trigSlide[currentPattern][track][tPT], subPhase)) * (voQ - prevVO[track]);
			}
			else {
				return voQ;
			}
		}
	}

	// FIRST: the pass of the track since the pattern started or a reset. A trig
	// looked at ahead of time belongs to the pass it will be played in.
	bool trackIsFirstPass(const int track, const int trig, const bool ahead) {
		if (!trackFirstPass[track])
			return false;
		if (!ahead)
			return true;
		const int len = nTracksAttibutes[currentPattern][track].getTrackLength();
		switch (nTracksAttibutes[currentPattern][track].getTrackReadMode()) {
			case 0: case 2: return trig != 0;
			case 1: return trig != len - 1;
			default: return trackPassSteps[track] + 1 < len;
		}
	}

	// LAST: a pattern change is asked for, and the time left in this pattern is at
	// most one pass of the track: each step's last turn before the change.
	bool trackIsLastPass(const int track, const bool ahead) {
		if (requestedPattern == currentPattern)
			return false;
		TrackAttibutes &t = nTracksAttibutes[currentPattern][track];
		const int len = t.getTrackLength();
		const int speed = t.getTrackSpeedEighths();
		// In eighths of a position, as clocks.
		const int pass = (t.getTrackReadMode() == 2) ? std::max(2 * len - 2, 1) : len;
		int left = (patternLengthTicks() - patternClock) * speed;
		if constexpr (INTERPOLATE)
			left -= (int)((long long)trackInterpolation[currentPattern][track].value * 8 / TICKS);
		if (ahead)
			left -= CLOCKS_PER_STEP * 8;
		return left <= pass * CLOCKS_PER_STEP * 8;
	}

	// Decides whether a trig plays. A FIRST or LAST trig is decided again when the
	// track reaches it, as a reset or a pattern request may have come since.
	void trigInitCondition(const int track, const int trig, const bool ahead, const bool fill, const bool pNei, const bool forceTrig, const bool killTrig, const float dice) {
		TrigAttibutes &g = nTrigsAttibutes[currentPattern][track][trig];
		if (g.getTrigProba() < TrigAttibutes::FIRST) {
			g.init(fill, nTracksAttibutes[currentPattern][track].getTrackPre(), pNei, forceTrig, killTrig, dice);
			return;
		}
		if (!ahead)
			g.setTrigInitialized(false);
		g.initPass(trackIsFirstPass(track, trig, ahead), trackIsLastPass(track, ahead), forceTrig, killTrig);
	}

	void trackSetCurrentTrig(const int track, const bool fill, const bool pNei, const bool force=false, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		int cI = nTracksAttibutes[currentPattern][track].getTrackCurrentTrig();
		const int headStep = trackHead[currentPattern][track] / TICKS;
		if ((headStep != cI) || force) {
			nTracksAttibutes[currentPattern][track].setTrackPre((nTrigsAttibutes[currentPattern][track][cI].getTrigActive() && nTrigsAttibutes[currentPattern][track][cI].hasProbability()) ? !nTrigsAttibutes[currentPattern][track][cI].getTrigSleeping() : nTracksAttibutes[currentPattern][track].getTrackPre());
			nTrigsAttibutes[currentPattern][track][cI].setTrigInitialized(false);
			nTracksAttibutes[currentPattern][track].setTrackCurrentTrig(headStep);
			cI = nTracksAttibutes[currentPattern][track].getTrackCurrentTrig();
			trigInitCondition(track, cI, false, fill, pNei, forceTrig, killTrig, dice);
			nTracksAttibutes[currentPattern][track].setTrackPre((nTrigsAttibutes[currentPattern][track][cI].getTrigActive()
			&& nTrigsAttibutes[currentPattern][track][cI].hasProbability()) ? !nTrigsAttibutes[currentPattern][track][cI].getTrigSleeping() : nTracksAttibutes[currentPattern][track].getTrackPre());
			// A next trig drawn early (see below) but not reached must draw again later.
			int oldNext = nTracksAttibutes[currentPattern][track].getTrackNextTrig();
			if (oldNext != cI)
				nTrigsAttibutes[currentPattern][track][oldNext].setTrigInitialized(false);
			trackSetNextTrig(track);
			trigInitCondition(track, nTracksAttibutes[currentPattern][track].getTrackNextTrig(), true, fill, pNei, forceTrig, killTrig, dice);
		}

		int cPT = nTracksAttibutes[currentPattern][track].getTrackPlayedTrig();
		// A trig that does not play leaves the way to the next one, trimmed early.
		if (nTrigsAttibutes[currentPattern][track][cI].getTrigActive() && !nTrigsAttibutes[currentPattern][track][cI].getTrigSleeping() && trigGetIsRead(track, cI, trackHead[currentPattern][track])) {
			if (cI != cPT) {
				nTracksAttibutes[currentPattern][track].setTrackPrevTrig(cPT);
				nTracksAttibutes[currentPattern][track].setTrackPlayedTrig(cI);
			}
		}
		else {
			int cNT = nTracksAttibutes[currentPattern][track].getTrackNextTrig();
			if ((cNT != cI) && trigGetIsRead(track, cNT, trackHead[currentPattern][track])) {
				// A trig trimmed early plays before its step begins: draw its
				// condition now, once, instead of using the previous loop's result.
				trigInitCondition(track, cNT, true, fill, pNei, forceTrig, killTrig, dice);
				if ((cNT != cPT) && nTrigsAttibutes[currentPattern][track][cNT].getTrigActive()
				&& !nTrigsAttibutes[currentPattern][track][cNT].getTrigSleeping())
				{
					nTracksAttibutes[currentPattern][track].setTrackPrevTrig(cPT);
					nTracksAttibutes[currentPattern][track].setTrackPlayedTrig(cNT);
				}
			}
		}
	}

	void trackReset(const int track, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		nTracksAttibutes[currentPattern][track].setTrackPre(false);
		nTracksAttibutes[currentPattern][track].setTrackForward(true);
		if (nTracksAttibutes[currentPattern][track].getTrackReadMode() == 1)
		{
			nTracksAttibutes[currentPattern][track].setTrackForward(false);
			trackHead[currentPattern][track] = trackTop(track);
		}
		else
			trackHead[currentPattern][track] = 0;
		trackSetCurrentTrig(track, fill, pNei, true, forceTrig, killTrig, dice);
	}

	void trackSetNextTrig(const int track) {
		int cI = nTracksAttibutes[currentPattern][track].getTrackCurrentTrig();
		switch (nTracksAttibutes[currentPattern][track].getTrackReadMode()) {
			case 0:
					nTracksAttibutes[currentPattern][track].setTrackNextTrig((cI == (nTracksAttibutes[currentPattern][track].getTrackLength()-1)) ? 0 : (cI+1)); break;
			case 1:
					nTracksAttibutes[currentPattern][track].setTrackNextTrig((cI == 0) ? (nTracksAttibutes[currentPattern][track].getTrackLength()-1) : (cI-1)); break;
			case 2: {
				if (cI == 0) {
					nTracksAttibutes[currentPattern][track].setTrackNextTrig(nTracksAttibutes[currentPattern][track].getTrackLength() > 1 ? 1: 0);
				}
				else if (cI == (nTracksAttibutes[currentPattern][track].getTrackLength() - 1)) {
					nTracksAttibutes[currentPattern][track].setTrackNextTrig(nTracksAttibutes[currentPattern][track].getTrackLength() > 1 ? (nTracksAttibutes[currentPattern][track].getTrackLength()-2) : 0);
				}
				else {
					nTracksAttibutes[currentPattern][track].setTrackNextTrig(clamp(cI + (nTracksAttibutes[currentPattern][track].getTrackForward() ? 1 : -1),0,nTracksAttibutes[currentPattern][track].getTrackLength() - 1));
				}
			  break;
			}
			case 3: nTracksAttibutes[currentPattern][track].setTrackNextTrig(std::min((int)(random::uniform()*nTracksAttibutes[currentPattern][track].getTrackLength()), nTracksAttibutes[currentPattern][track].getTrackLength() - 1)); break;
			case 4:
			{
				float dice = random::uniform();
				if (dice>=0.5f)
					nTracksAttibutes[currentPattern][track].setTrackNextTrig(((cI+1) > (nTracksAttibutes[currentPattern][track].getTrackLength() - 1) ? 0 : (cI + 1)));
				else if (dice<=0.25f)
					nTracksAttibutes[currentPattern][track].setTrackNextTrig(cI == 0 ? (nTracksAttibutes[currentPattern][track].getTrackLength() - 1) : (cI - 1));
				else
					nTracksAttibutes[currentPattern][track].setTrackNextTrig(cI);
				break;
			}
			default : nTracksAttibutes[currentPattern][track].setTrackNextTrig(cI);
		}
	}

	// The last unit of the track.
	int trackTop(const int track) {
		return nTracksAttibutes[currentPattern][track].getTrackLength() * TICKS - 1;
	}

	// ZOUMAI: starts the part between two clocks, j samples after the last one.
	void trackInterpolationStart(const int track, const long j) {
		Interpolation &in = trackInterpolation[currentPattern][track];
		in = Interpolation();
		if (clockInterval > 0) {
			const long long perClock = (long long)nTracksAttibutes[currentPattern][track].getTrackSpeedEighths() * TRAITS::UNITS_PER_CLOCK / 8;
			in.quotient = perClock / clockInterval;
			in.remainder = perClock % clockInterval;
			if (j > 0) {
				const long jin = std::min(j, clockInterval - 1);
				in.value = perClock * jin / clockInterval;
				in.carry = perClock * jin % clockInterval;
			}
		}
	}

	// A real start of the track: the position restarts. Between two clocks the
	// steps stay on the clock's subdivisions, so step 0 only lasts until the next
	// one, and the head counts from 0 in it: its gate is not cut.
	void trackRestart(const int track) {
		trackFirstPass[track] = true;
		trackPassSteps[track] = 0;
		trackPlayedAtClock[currentPattern][track] = 0;
		trackLastWhole[currentPattern][track] = 0;
		if constexpr (INTERPOLATE) {
			trackInterpolationStart(track, std::max(clockCount, 0L));
			Interpolation &in = trackInterpolation[currentPattern][track];
			in.resetValue = in.value;
			in.resetBoundary = (in.value / TICKS + 1) * TICKS;
		}
	}

	// The units played: at each clock `speed` more; between clocks floor(j * rate),
	// held at the last sample if the clock is late, so it never passes the next clock.
	long long trackPlayed(const int track, const bool step) {
		long long &played = trackPlayedAtClock[currentPattern][track];
		if (step)
			played += (long long)nTracksAttibutes[currentPattern][track].getTrackSpeedEighths() * TRAITS::UNITS_PER_CLOCK;
		if constexpr (INTERPOLATE) {
			Interpolation &in = trackInterpolation[currentPattern][track];
			if (step)
				trackInterpolationStart(track, 0);
			else if (clockCount >= 1 && clockCount < clockInterval) {
				in.value += in.quotient;
				in.carry += in.remainder;
				if (in.carry >= clockInterval) {
					in.carry -= clockInterval;
					in.value++;
				}
			}
			return (played >> 3) + (in.value < in.resetBoundary ? in.value - in.resetValue : in.value);
		}
		else
			return played >> 3;
	}

	// Wraps at the end of the track: the track starts again from the top, keeping
	// what it played past the end.
	long long trackWrap(const int track, long long v, const bool fill, const bool pNei, const bool forceTrig, const bool killTrig, const float dice) {
		const long long len = nTracksAttibutes[currentPattern][track].getTrackLength() * (long long)TICKS;
		if (v < len)
			return v;
		const long long rest = v % len;
		trackPlayedAtClock[currentPattern][track] -= (v - rest) * 8;
		trackFirstPass[track] = false;
		trackReset(track, fill, pNei, forceTrig, killTrig, dice);
		return rest;
	}

	void trackMoveNextForward(const int track, const bool step, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		nTracksAttibutes[currentPattern][track].setTrackForward(true);
		trackHead[currentPattern][track] = trackWrap(track, trackPlayed(track, step), fill, pNei, forceTrig, killTrig, dice);
		trackSetCurrentTrig(track, fill, pNei, false, forceTrig, killTrig, dice);
	}

	void trackMoveNextBackward(const int track, const bool step, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		nTracksAttibutes[currentPattern][track].setTrackForward(false);
		const long long v = trackWrap(track, trackPlayed(track, step), fill, pNei, forceTrig, killTrig, dice);
		trackHead[currentPattern][track] = trackTop(track) - v;
		trackSetCurrentTrig(track, fill, pNei, false, forceTrig, killTrig, dice);
	}

	// 0, 1, .. len-1, len-2, .. 1, then 0 again.
	void trackMoveNextPendulum(const int track, const bool step, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		const int len = nTracksAttibutes[currentPattern][track].getTrackLength();
		long long v = trackPlayed(track, step);
		if (len < 2) {
			nTracksAttibutes[currentPattern][track].setTrackForward(true);
			trackHead[currentPattern][track] = trackWrap(track, v, fill, pNei, forceTrig, killTrig, dice);
			trackSetCurrentTrig(track, fill, pNei, false, forceTrig, killTrig, dice);
			return;
		}
		const long long period = (2LL * len - 2) * TICKS;
		if (v >= period) {
			const long long rest = v % period;
			trackFirstPass[track] = false;
			trackPlayedAtClock[currentPattern][track] -= (v - rest) * 8;
			v = rest;
		}
		const bool forward = v < (long long)len * TICKS;
		nTracksAttibutes[currentPattern][track].setTrackForward(forward);
		trackHead[currentPattern][track] = forward ? v : (2LL * len - 1) * TICKS - 1 - v;
		trackSetCurrentTrig(track, fill, pNei, false, forceTrig, killTrig, dice);
	}

	// Random read modes have no end: a pass is as many steps as the track has.
	void trackEndsPassStep(const int track) {
		if (++trackPassSteps[track] >= nTracksAttibutes[currentPattern][track].getTrackLength())
			trackFirstPass[track] = false;
	}

	// Random and brownian: each whole step played jumps to the next trig drawn.
	void trackMoveNextJump(const int track, const bool step, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		nTracksAttibutes[currentPattern][track].setTrackForward(true);
		long long v = trackPlayed(track, step);
		int &last = trackLastWhole[currentPattern][track];
		if (v >= 1024LL * TICKS) {
			trackPlayedAtClock[currentPattern][track] -= 1024LL * TICKS * 8;
			last -= 1024;
			v -= 1024LL * TICKS;
		}
		const int whole = v / TICKS;
		const Time inStep = v - (long long)whole * TICKS;
		if (whole != last) {
			last = whole;
			trackEndsPassStep(track);
			trackHead[currentPattern][track] = nTracksAttibutes[currentPattern][track].getTrackNextTrig() * TICKS + inStep;
			trackSetCurrentTrig(track, fill, pNei, true, forceTrig, killTrig, dice);
			return;
		}
		trackHead[currentPattern][track] = nTracksAttibutes[currentPattern][track].getTrackCurrentTrig() * TICKS + inStep;
		trackSetCurrentTrig(track, fill, pNei, false, forceTrig, killTrig, dice);
	}

	void trackMoveNext(const int track, const bool step, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		switch (nTracksAttibutes[currentPattern][track].getTrackReadMode()) {
			case 0: trackMoveNextForward(track, step, fill, pNei, forceTrig, killTrig, dice); break;
			case 1: trackMoveNextBackward(track, step, fill, pNei, forceTrig, killTrig, dice); break;
			case 2: trackMoveNextPendulum(track, step, fill, pNei, forceTrig, killTrig, dice); break;
			case 3:
			case 4: trackMoveNextJump(track, step, fill, pNei, forceTrig, killTrig, dice); break;
		}
	}

};

template <typename TRAITS>
void ZouSeq<TRAITS>::process(const ProcessArgs &args) {
	requestedPattern = (int)clamp((inputs[PATTERN_INPUT].isConnected() ? rescale(clamp(inputs[PATTERN_INPUT].getVoltage(), 0.0f, 10.0f),0.0f,10.0f,0.0f,(float)NUM_PATTERNS) : 0) + (int)params[PATTERN_PARAM].getValue(), 0.0f, NUM_PATTERNS - 1.0f);
	// Stopped: switch at once. Running: at the end of the pattern, below.
	if (!run)
		currentPattern = requestedPattern;

	if (rightExpander.module && rightExpander.module->model == TRAITS::expanderModel()) {
		float *messagesFromExpander = (float*)rightExpander.consumerMessage;
		for (int i=0; i<NUM_TRACKS; i++) {
			fills[i]=messagesFromExpander[i];
			forceTrigs[i]=messagesFromExpander[i+8];
			killTrigs[i]=messagesFromExpander[i+16];
			trsp[i]=messagesFromExpander[i+24];
			dice[i]=messagesFromExpander[i+32];
			rotLeft[i]=messagesFromExpander[i+40];
			rotRight[i]=messagesFromExpander[i+48];
			rotLen[i]=clamp((int)messagesFromExpander[i+56], 0, 64);
		}

		float *messagesToExpander = (float*)(rightExpander.module->leftExpander.producerMessage);
		messagesToExpander[0] = currentPattern;
		rightExpander.module->leftExpander.messageFlipRequested = true;
	}
	else {
		// No expander: nothing it set may stay applied.
		for (int i=0; i<NUM_TRACKS; i++) {
			fills[i] = forceTrigs[i] = killTrigs[i] = false;
			trsp[i] = dice[i] = 0.0f;
			rotLeft[i] = rotRight[i] = 0;
			rotLen[i] = 16;
		}
	}

	// With a cable the input sets fill (high above 1 V, low under 0.1 V);
	// without one the button toggles it.
	bool fillPressed = fillTrigger.process(params[FILL_PARAM].getValue());
	if (inputs[FILL_INPUT].isConnected()) {
		fillInputTrigger.process(inputs[FILL_INPUT].getVoltage(), 0.1f, 1.f);
		fill = fillInputTrigger.isHigh();
	}
	else if (fillPressed) {
		fill = !fill;
	}
	solo = patternIsSoloed();

	bool control = --controlCounter < 0;
	if (control)
		controlCounter = CONTROL_DIV - 1;

	// Track on/off trigger inputs stay per sample.
	for (int i = 0; i < NUM_TRACKS; i++) {
		if (trackActiveTriggers[i].process(inputs[TRACKACTIVE_INPUTS+i].getVoltage())) {
			nTracksAttibutes[currentPattern][i].toggleTrackActive();
		}
	}

	// Rotations from the expander, also while stopped: applied on the sample
	// they come in, the knob sync at control rate; one try-lock for both,
	// skipped while the UI is changing the selection or the data.
	bool rotPending = false;
	for (int i=0; i<NUM_TRACKS; i++) {
		pendingRotLeft[i] += rotLeft[i];
		pendingRotRight[i] += rotRight[i];
		rotPending = rotPending || pendingRotLeft[i] || pendingRotRight[i];
	}
	if (control || rotPending) {
		std::unique_lock<std::recursive_mutex> lock(editMutex, std::try_to_lock);
		if (lock.owns_lock()) {
			if (control) {
				if (currentPattern != previousPattern) {
					previousPattern = currentPattern;
					updateTrackToParams();
					updateTrigToParams();
				}
				else {
					updateTrigVO();
					updateParamsToTrack();
					updateParamsToTrig();
				}
			}
			for (int i=0; i<NUM_TRACKS; i++) {
				if (pendingRotLeft[i] || pendingRotRight[i]) {
					int shift = pendingRotLeft[i] - pendingRotRight[i];
					if (shift > 0)
						nTrackLeft(i, shift, rotLen[i]);
					else if (shift < 0)
						nTrackRight(i, -shift, rotLen[i]);
					pendingRotLeft[i] = pendingRotRight[i] = 0;
					if (i == currentTrack)
						updateTrigToParams();
				}
			}
		}
	}

	if (control) {
		lights[FILL_LIGHT].setBrightness(fill ? 1.0f : 0.0f);

		int pageOffset = trigPage*16;
		int tCT = nTracksAttibutes[currentPattern][currentTrack].getTrackCurrentTrig();
		for (int i = 0; i<16; i++) {
			int shiftedIndex = i + pageOffset;
			if (tCT == shiftedIndex) {
				lights[STEPS_LIGHTS+3*i].setBrightness(1.0f);
				lights[STEPS_LIGHTS+3*i+1].setBrightness(0.0f);
				lights[STEPS_LIGHTS+3*i+2].setBrightness(0.0f);
			}
			else if (nTrigsAttibutes[currentPattern][currentTrack][shiftedIndex].getTrigActive()) {
				if (shiftedIndex == currentTrig) {
					lights[STEPS_LIGHTS+3*i].setBrightness(0.0f);
					lights[STEPS_LIGHTS+3*i+1].setBrightness(0.0f);
					lights[STEPS_LIGHTS+3*i+2].setBrightness(1.0f);
				}
				else {
					lights[STEPS_LIGHTS+3*i].setBrightness(0.0f);
					lights[STEPS_LIGHTS+3*i+1].setBrightness(1.0f);
					lights[STEPS_LIGHTS+3*i+2].setBrightness(0.0f);
				}
			}
			else if (currentTrig == shiftedIndex) {
				lights[STEPS_LIGHTS+3*i].setBrightness(0.0f);
				lights[STEPS_LIGHTS+3*i+1].setBrightness(0.0f);
				lights[STEPS_LIGHTS+3*i+2].setBrightness(0.5f);
			}
			else {
				lights[STEPS_LIGHTS+3*i].setBrightness(0.1f);
				lights[STEPS_LIGHTS+3*i+1].setBrightness(0.1f);
				lights[STEPS_LIGHTS+3*i+2].setBrightness(0.1f);
			}

			if (i<4) {
				if (i == trigPage) {
					lights[TRIGPAGE_LIGHTS+3*i].setBrightness(0.0f);
					lights[TRIGPAGE_LIGHTS+3*i+2].setBrightness(1.0f);
				}
				else if ((tCT >= (i*16)) && (tCT<(16*(i+1)))) {
					lights[TRIGPAGE_LIGHTS+3*i].setBrightness(1.0f);
					lights[TRIGPAGE_LIGHTS+3*i+2].setBrightness(0.0f);
				}
				else {
					lights[TRIGPAGE_LIGHTS+3*i].setBrightness(0.0f);
					lights[TRIGPAGE_LIGHTS+3*i+2].setBrightness(0.0f);
				}
			}

			if (i<7) {
					if (nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigOctave()==i) {
						lights[OCTAVE_LIGHTS+3*i+2].setBrightness(1.0f);
					}
					else {
						lights[OCTAVE_LIGHTS+3*i+2].setBrightness(0.0f);
					}
			}

			if (i<8) {
				if (currentTrack==i) {
					lights[TRACKSELECT_LIGHTS+i*3+1].setBrightness(0.0f);
					lights[TRACKSELECT_LIGHTS+i*3+2].setBrightness(1.0f);
				}
				else {
					lights[TRACKSELECT_LIGHTS+i*3+1].setBrightness(0.0f);
					lights[TRACKSELECT_LIGHTS+i*3+2].setBrightness(0.0f);
				}

				if (!solo && nTracksAttibutes[currentPattern][i].getTrackActive()) {
					lights[TRACKSONOFF_LIGHTS+i*3+1].setBrightness(1.0f);
					lights[TRACKSONOFF_LIGHTS+i*3+2].setBrightness(0.0f);
				}
				else if (solo && nTracksAttibutes[currentPattern][i].getTrackSolo()) {
					lights[TRACKSONOFF_LIGHTS+i*3+1].setBrightness(0.0f);
					lights[TRACKSONOFF_LIGHTS+i*3+2].setBrightness(1.0f);
				}
				else {
					lights[TRACKSONOFF_LIGHTS+i*3+1].setBrightness(0.0f);
					lights[TRACKSONOFF_LIGHTS+i*3+2].setBrightness(0.0f);
				}
			}
		}
	}

	clockTrigged = clockTrigger.process(inputs[EXTCLOCK_INPUT].getVoltage(), 0.1f, 1.f);
	long ms = 0;
	if constexpr (INTERPOLATE) {
		// The clock interval is measured between two clocks, also while stopped.
		if (clockCount >= 0)
			clockCount++;
		if (clockTrigged) {
			if (clockCount > 0)
				clockInterval = clockCount;
			clockCount = 0;
		}
		ms = (std::lround(args.sampleRate) + 500) / 1000;
	}

	bool runStarted = false;
	if (runTrigger.process(inputs[RUN_INPUT].getVoltage() + params[RUN_PARAM].getValue())) {
		run = !run;
		runStarted = run;
	}

	if (run) {
		// Starting plays from the top, like the reset input; it also restarts
		// the tracks with their own RST input, which the reset input leaves alone.
		globalReset = resetTrigger.process(inputs[RESET_INPUT].getVoltage(), 0.1f, 1.f) || runStarted;
		bool step = clockTrigged;
		if constexpr (INTERPOLATE) {
			// A clock up to 1 ms after a reset belongs to it: it does not move on, but
			// the time between clocks counts from it.
			step = clockTrigged && resetHold <= 0 && !globalReset;
			if (clockTrigged && !step && !globalReset) {
				for (int i=0; i<NUM_TRACKS; i++) {
					if (trackPlayedAtClock[currentPattern][i] == 0)
						trackInterpolationStart(i, 0);
				}
			}
			if (globalReset)
				resetHold = ms;
			else if (resetHold > 0)
				resetHold--;
		}

		// A requested pattern takes over when the current one ends, and starts
		// all its tracks from the top.
		bool patternStart = false;
		if (globalReset) {
			currentPattern = requestedPattern;
			patternClock = 0;
		}
		else if (step && ++patternClock >= patternLengthTicks()) {
			patternClock = 0;
			if (requestedPattern != currentPattern) {
				currentPattern = requestedPattern;
				patternStart = true;
			}
		}

		for (int i=0; i<NUM_TRACKS;i++) {
			if (trackResetTriggers[i].process(inputs[TRACKRESET_INPUTS+i].getVoltage(), 0.1f, 1.f)) {
				trackRestart(i);
				trackReset(i, fill || fills[i], i == 0 ? false : nTracksAttibutes[currentPattern][i-1].getTrackPre(), forceTrigs[i], killTrigs[i], dice[i]);
			}
			else if ((!inputs[TRACKRESET_INPUTS+i].isConnected() && globalReset) || runStarted || patternStart) {
				trackRestart(i);
				trackReset(i, fill || fills[i], i == 0 ? false : nTracksAttibutes[currentPattern][i-1].getTrackPre(), forceTrigs[i], killTrigs[i], dice[i]);
			}
			else {
				trackMoveNext(i, step, fill || fills[i], i == 0 ? false : nTracksAttibutes[currentPattern][i-1].getTrackPre(), forceTrigs[i], killTrigs[i], dice[i]);
			}

			int tPT = nTracksAttibutes[currentPattern][i].getTrackPlayedTrig();

			if ((currentTrack == i) && (params[RECORD_PARAM].getValue() == 1.0f))
				recordStep(i);
			else if (i == currentTrack && noteIncoming) {
				noteIncoming = false;
				recTrig = -1;
			}

			if ((solo && nTracksAttibutes[currentPattern][i].getTrackSolo()) || (!solo && nTracksAttibutes[currentPattern][i].getTrackActive())) {
				float gate = trackGetGate(i, tPT);
				if (gate>0.0f) {
					if (nTrigsAttibutes[currentPattern][i][tPT].getTrigType() == 0)
						outputs[GATE_OUTPUTS + i].setVoltage(gate);
					else if (nTrigsAttibutes[currentPattern][i][tPT].getTrigType() == 1)
						outputs[GATE_OUTPUTS + i].setVoltage(inputs[G1_INPUT].getVoltage());
					else if (nTrigsAttibutes[currentPattern][i][tPT].getTrigType() == 2)
						outputs[GATE_OUTPUTS + i].setVoltage(inputs[G2_INPUT].getVoltage());
					else
						outputs[GATE_OUTPUTS + i].setVoltage(0.0f);
				}
				else
					outputs[GATE_OUTPUTS + i].setVoltage(0.0f);
			}
			else
				outputs[GATE_OUTPUTS + i].setVoltage(0.0f);

			if (tPT != prevTrig[i]) {
				prevVO[i] = outputs[VO_OUTPUTS + i].getVoltage();
				prevTrig[i] = tPT;
			}

			bool q = scale[currentPattern][i]>0;
			outputs[VO_OUTPUTS + i].setVoltage(trackGetVO(i, tPT, q));
			// While the gate is on, CV1 and CV2 follow the trig (ramps included) and are kept;
			// while it is off they are 0 V, or the kept values when the track holds them.
			if (outputs[GATE_OUTPUTS + i].getVoltage() != 0.0f) {
				heldCV1[i] = (quantizeCV1[currentPattern][i]>0 && q) ? quantizeCached(cv1Quant[i], trigRampedCV(i, tPT, trigCV1[currentPattern][i][tPT], trigCV1End[currentPattern][i][tPT])-4.0f, rootNote[currentPattern][i], scale[currentPattern][i]) : trigRampedCV(i, tPT, trigCV1[currentPattern][i][tPT], trigCV1End[currentPattern][i][tPT]);
				heldCV2[i] = trigRampedCV(i, tPT, trigCV2[currentPattern][i][tPT], trigCV2End[currentPattern][i][tPT]);
				outputs[CV1_OUTPUTS + i].setVoltage(heldCV1[i]);
				outputs[CV2_OUTPUTS + i].setVoltage(heldCV2[i]);
			}
			else {
				const bool hold = cvHold[currentPattern][i];
				outputs[CV1_OUTPUTS + i].setVoltage(hold ? heldCV1[i] : 0.0f);
				outputs[CV2_OUTPUTS + i].setVoltage(hold ? heldCV2[i] : 0.0f);
			}
		}
	}
	else {
		for (int i=0; i<NUM_TRACKS;i++) {
			outputs[GATE_OUTPUTS + i].setVoltage(0.0f);
		}
	}

	// the same 8 tracks on one polyphonic cable each: channel n = track n
	for (int o=0; o<4; o++) {
		if (outputs[POLY_GATE_OUTPUT + o].isConnected()) {
			for (int i=0; i<NUM_TRACKS; i++)
				outputs[POLY_GATE_OUTPUT + o].setVoltage(outputs[GATE_OUTPUTS + o*NUM_TRACKS + i].getVoltage(), i);
			outputs[POLY_GATE_OUTPUT + o].setChannels(8);
		}
	}
}

// The edits a key or a menu item can ask for, written once: every button and
// menu item below goes through these. They run in the UI thread, under the
// module's edit lock (see ZouSeq::editMutex).
enum class ZouTrigOp { ERASE, COPY, PASTE, RAND_NOTE, RAND_NOTE_PLUS, RAND_PROB, FULL_RAND, RAND_CV1, RAND_CV2, UP, DOWN };
enum class ZouTrackOp { ERASE, COPY, PASTE, RAND, RAND_NOTES, RAND_NOTES_PLUS, RAND_PROBS, RAND_CV1, RAND_CV2, UP, DOWN, LEFT, RIGHT, SLIDE_MODE, CV_HOLD, EUCLID };
enum class ZouPageOp { ERASE, COPY, PASTE, RAND_NOTES, RAND_NOTES_PLUS, RAND_PROBS, RAND_CV1, RAND_CV2 };
enum class ZouPatternOp { ERASE, COPY, PASTE, RAND, FULL_RAND };

template <typename M>
void zouTrigEdit(M *mod, const ZouTrigOp op, const int trig) {
	const int track = mod->currentTrack;
	auto edit = [&](const char *name, auto change) {
		mod->edit(name, track, 1, trig, 1, false, change);
	};
	switch (op) {
		case ZouTrigOp::COPY:
			mod->copyTrigId = trig;
			mod->copyTrackId = track;
			mod->copyPatternId = mod->currentPattern;
			mod->copyPatternArmed = false;
			mod->copyTrackArmed = false;
			mod->copyTrigArmed = true;
			return;
		case ZouTrigOp::ERASE: edit("erase trig", [&]() {mod->trigInit(mod->currentPattern, track, trig);}); break;
		case ZouTrigOp::PASTE: edit("paste trig", [&]() {mod->pasteTrig(mod->copyPatternId, mod->copyTrackId, mod->copyTrigId, mod->currentPattern, track, trig);}); break;
		case ZouTrigOp::RAND_NOTE: edit("random note", [&]() {mod->randomizeTrigNote(track, trig);}); break;
		case ZouTrigOp::RAND_NOTE_PLUS: edit("random note+", [&]() {mod->randomizeTrigNotePlus(track, trig);}); break;
		case ZouTrigOp::RAND_PROB: edit("random condition", [&]() {mod->randomizeTrigProb(track, trig);}); break;
		case ZouTrigOp::FULL_RAND: edit("full random trig", [&]() {mod->fullRandomizeTrig(track, trig);}); break;
		case ZouTrigOp::RAND_CV1: edit("random CV1", [&]() {mod->randomizeTrigCV1(track, trig);}); break;
		case ZouTrigOp::RAND_CV2: edit("random CV2", [&]() {mod->randomizeTrigCV2(track, trig);}); break;
		case ZouTrigOp::UP: edit("trig up", [&]() {mod->trigUp(trig);}); break;
		case ZouTrigOp::DOWN: edit("trig down", [&]() {mod->trigDown(trig);}); break;
	}
	mod->updateTrigToParams();
}

template <typename M>
void zouTrackEdit(M *mod, const ZouTrackOp op, const int track) {
	auto edit = [&](const char *name, auto change) {
		mod->edit(name, track, 1, 0, M::NUM_TRIGS, true, change);
	};
	switch (op) {
		case ZouTrackOp::COPY:
			mod->copyTrackId = track;
			mod->copyPatternId = mod->currentPattern;
			mod->copyPatternArmed = false;
			mod->copyTrackArmed = true;
			mod->copyTrigArmed = false;
			return;
		case ZouTrackOp::SLIDE_MODE:
			edit("slide mode", [&]() {mod->slideMode[mod->currentPattern][track] = !mod->slideMode[mod->currentPattern][track];});
			return;
		case ZouTrackOp::CV_HOLD:
			edit("hold CV1 and CV2", [&]() {mod->cvHold[mod->currentPattern][track] = !mod->cvHold[mod->currentPattern][track];});
			return;
		case ZouTrackOp::UP: edit("track up", [&]() {mod->trackUp(track);}); mod->updateTrigToParams(); return;
		case ZouTrackOp::DOWN: edit("track down", [&]() {mod->trackDown(track);}); mod->updateTrigToParams(); return;
		case ZouTrackOp::LEFT: edit("track left", [&]() {mod->nTrackLeft(track, 1);}); mod->updateTrigToParams(); return;
		case ZouTrackOp::RIGHT: edit("track right", [&]() {mod->nTrackRight(track, 1);}); mod->updateTrigToParams(); return;
		case ZouTrackOp::EUCLID: edit("euclidean fill", [&]() {mod->euclidFill(track);}); mod->updateTrigToParams(); return;
		case ZouTrackOp::ERASE: edit("erase track", [&]() {mod->trackInit(mod->currentPattern, track);}); break;
		case ZouTrackOp::PASTE: edit("paste track", [&]() {mod->pasteTrack(mod->copyPatternId, mod->copyTrackId, mod->currentPattern, track);}); break;
		case ZouTrackOp::RAND: edit("random track", [&]() {mod->randomizeTrack(track);}); break;
		case ZouTrackOp::RAND_NOTES: edit("random track notes", [&]() {mod->randomizeTrackTrigsNotes(track);}); break;
		case ZouTrackOp::RAND_NOTES_PLUS: edit("random track notes+", [&]() {mod->randomizeTrackTrigsNotesPlus(track);}); break;
		case ZouTrackOp::RAND_PROBS: edit("random track conditions", [&]() {mod->randomizeTrackTrigsProbs(track);}); break;
		case ZouTrackOp::RAND_CV1: edit("random track CV1", [&]() {mod->randomizeTrackTrigsCV1(track);}); break;
		case ZouTrackOp::RAND_CV2: edit("random track CV2", [&]() {mod->randomizeTrackTrigsCV2(track);}); break;
	}
	mod->updateTrackToParams();
	mod->updateTrigToParams();
}

template <typename M>
void zouPageEdit(M *mod, const ZouPageOp op, const int page) {
	auto edit = [&](const char *name, auto change) {
		mod->edit(name, mod->currentTrack, 1, page * 16, 16, false, change);
	};
	switch (op) {
		case ZouPageOp::COPY:
			mod->copyPageId = page;
			mod->copyPatternArmed = false;
			mod->copyTrackArmed = false;
			mod->copyTrigArmed = false;
			mod->copyPageArmed = true;
			return;
		case ZouPageOp::ERASE: edit("erase page", [&]() {mod->pageInit(page);}); break;
		case ZouPageOp::PASTE: edit("paste page", [&]() {mod->pastePage(mod->copyPageId, page);}); break;
		case ZouPageOp::RAND_NOTES: edit("random page notes", [&]() {mod->randomizePageTrigsNotes(page);}); break;
		case ZouPageOp::RAND_NOTES_PLUS: edit("random page notes+", [&]() {mod->randomizePageTrigsNotesPlus(page);}); break;
		case ZouPageOp::RAND_PROBS: edit("random page conditions", [&]() {mod->randomizePageTrigsProbs(page);}); break;
		case ZouPageOp::RAND_CV1: edit("random page CV1", [&]() {mod->randomizePageTrigsCV1(page);}); break;
		case ZouPageOp::RAND_CV2: edit("random page CV2", [&]() {mod->randomizePageTrigsCV2(page);}); break;
	}
	mod->updateTrigToParams();
}

template <typename M>
void zouPatternEdit(M *mod, const ZouPatternOp op) {
	auto edit = [&](const char *name, auto change) {
		mod->edit(name, 0, M::NUM_TRACKS, 0, M::NUM_TRIGS, true, change);
	};
	switch (op) {
		case ZouPatternOp::COPY:
			mod->copyPatternId = mod->currentPattern;
			mod->copyPatternArmed = true;
			mod->copyTrackArmed = false;
			mod->copyTrigArmed = false;
			return;
		case ZouPatternOp::ERASE:
			edit("erase pattern", [&]() {
				for (int i=0; i<M::NUM_TRACKS; i++) {
					mod->trackInit(mod->currentPattern, i);
				}
			});
			break;
		case ZouPatternOp::PASTE: edit("paste pattern", [&]() {mod->pastePattern();}); break;
		case ZouPatternOp::RAND: edit("random pattern", [&]() {mod->randomizePattern();}); break;
		case ZouPatternOp::FULL_RAND: edit("full random pattern", [&]() {mod->fullRandomizePattern();}); break;
	}
	mod->updateTrackToParams();
	mod->updateTrigToParams();
}

// The keys over a SEL or ON/OFF button.
template <typename M>
void zouTrackKey(M *mod, const int key, const int track) {
	switch (key) {
		case GLFW_KEY_C: zouTrackEdit(mod, ZouTrackOp::COPY, track); break;
		case GLFW_KEY_V: zouTrackEdit(mod, ZouTrackOp::PASTE, track); break;
		case GLFW_KEY_E: zouTrackEdit(mod, ZouTrackOp::ERASE, track); break;
		case GLFW_KEY_R: zouTrackEdit(mod, ZouTrackOp::RAND, track); break;
		case GLFW_KEY_T: zouTrackEdit(mod, ZouTrackOp::RAND_NOTES, track); break;
		case GLFW_KEY_Y: zouTrackEdit(mod, ZouTrackOp::RAND_NOTES_PLUS, track); break;
		case GLFW_KEY_U: zouTrackEdit(mod, ZouTrackOp::RAND_PROBS, track); break;
		case GLFW_KEY_F: zouTrackEdit(mod, ZouTrackOp::RAND_CV1, track); break;
		case GLFW_KEY_G: zouTrackEdit(mod, ZouTrackOp::RAND_CV2, track); break;
		case GLFW_KEY_W: zouTrackEdit(mod, ZouTrackOp::UP, track); break;
		case GLFW_KEY_S: zouTrackEdit(mod, ZouTrackOp::DOWN, track); break;
		case GLFW_KEY_A: zouTrackEdit(mod, ZouTrackOp::LEFT, track); break;
		case GLFW_KEY_D: zouTrackEdit(mod, ZouTrackOp::RIGHT, track); break;
		case GLFW_KEY_B: zouTrackEdit(mod, ZouTrackOp::EUCLID, track); break;
		default: break;
	}
}

struct ZouRecordBtn : SvgSwitch {
	ZouRecordBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/ledgrey.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/ledred.svg")));
		sw->wrap();
		shadow->opacity = 0.0f;
	}
};

struct ZouQuantizeBtn : SvgSwitch {
	ZouQuantizeBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/ledgrey.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/ledblue.svg")));
		sw->wrap();
		shadow->opacity = 0.0f;
	}
};

template <typename M>
struct ZouOctaveBtn : SmallLEDLightBezel<RedGreenBlueLight> {
	void onButton(const event::Button &e) override {
		M *mod = static_cast<M*>(getParamQuantity()->module);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			for (int i = 0; i < 7; i++) {
				if (i != getParamQuantity()->paramId - M::OCTAVE_PARAMS) {
					mod->params[M::OCTAVE_PARAMS+i].setValue(0.0f);
				}
				else {
					mod->edit("change octave", mod->currentTrack, 1, mod->currentTrig, 1, false, [&]() {
						mod->nTrigsAttibutes[mod->currentPattern][mod->currentTrack][mod->currentTrig].setTrigOctave(i);
					});
				}
			}
			e.consume(this);
			return;
		}
		SmallLEDLightBezel<RedGreenBlueLight>::onButton(e);
	}
};

// The P2 button in the corner of a screen, in the colours of the screen; lit, its page
// is on. It shows another page and writes nothing to the module's data.
template <typename M>
struct ZouScreenBtn : ParamWidget {
	ZouScreenBtn() {
		box.size = Vec(14.0f, 14.0f);
	}

	bool &page(M *mod) {
		return paramId == M::TRACKPAGE2_PARAM ? mod->trackPage2 : mod->trigPage2;
	}

	void onButton(const event::Button &e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (module) {
				bool &on = page(static_cast<M*>(module));
				on = !on;
			}
			e.consume(this);
			return;
		}
		ParamWidget::onButton(e);
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			const bool on = module && page(static_cast<M*>(module));
			nvgBeginPath(args.vg);
			nvgRoundedRect(args.vg, 1.0f, 1.0f, 12.0f, 12.0f, 2.5f);
			nvgFillColor(args.vg, on ? YELLOW_BIDOO : nvgRGBA(0, 0, 0, 255));
			nvgFill(args.vg);
			nvgStrokeWidth(args.vg, 1.0f);
			nvgStrokeColor(args.vg, YELLOW_BIDOO);
			nvgStroke(args.vg);
			nvgFontSize(args.vg, 9.0f);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
			nvgFillColor(args.vg, on ? nvgRGBA(0, 0, 0, 255) : YELLOW_BIDOO);
			nvgText(args.vg, 7.0f, 7.5f, "P2", NULL);
		}
		ParamWidget::drawLayer(args, layer);
	}
};

template <typename M>
struct ZouPageBtn : SmallLEDLightBezel<RedGreenBlueLight> {
	void onButton(const event::Button &e) override {
		M *mod = static_cast<M*>(getParamQuantity()->module);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			mod->trigPage = getParamQuantity()->paramId - M::TRIGPAGE_PARAM;
			mod->currentTrig = mod->trigPage*16 + mod->currentTrig%16;
			mod->updateTrigToParams();
			e.consume(this);
		}
		SmallLEDLightBezel<RedGreenBlueLight>::onButton(e);
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			M *mod = static_cast<M*>(getParamQuantity()->module);
			const int page = getParamQuantity()->paramId - M::TRIGPAGE_PARAM;
			switch (e.key) {
				case GLFW_KEY_C: zouPageEdit(mod, ZouPageOp::COPY, page); break;
				case GLFW_KEY_V: zouPageEdit(mod, ZouPageOp::PASTE, page); break;
				case GLFW_KEY_E: zouPageEdit(mod, ZouPageOp::ERASE, page); break;
				case GLFW_KEY_T: zouPageEdit(mod, ZouPageOp::RAND_NOTES, page); break;
				case GLFW_KEY_Y: zouPageEdit(mod, ZouPageOp::RAND_NOTES_PLUS, page); break;
				case GLFW_KEY_U: zouPageEdit(mod, ZouPageOp::RAND_PROBS, page); break;
				case GLFW_KEY_F: zouPageEdit(mod, ZouPageOp::RAND_CV1, page); break;
				case GLFW_KEY_G: zouPageEdit(mod, ZouPageOp::RAND_CV2, page); break;
				default: break;
			}
		}
		SmallLEDLightBezel<RedGreenBlueLight>::onHoverKey(e);
	}
};

template <typename M>
struct ZouTrackSelectBtn : SmallLEDLightBezel<RedGreenBlueLight> {
	void onButton(const event::Button &e) override {
		M *mod = static_cast<M*>(getParamQuantity()->module);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			for (int i = 0; i < M::NUM_TRACKS; i++) {
				if (i != (getParamQuantity()->paramId - M::TRACKSELECT_PARAMS)) {
					mod->params[M::TRACKSELECT_PARAMS+i].setValue(0.0f);
				}
				else {
					mod->params[M::TRACKSELECT_PARAMS+i].setValue(1.0f);
					mod->currentTrack=i;
					mod->updateTrackToParams();
					mod->updateTrigToParams();
				}
			}
			e.consume(this);
		}
		SmallLEDLightBezel<RedGreenBlueLight>::onButton(e);
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT)
			zouTrackKey(static_cast<M*>(getParamQuantity()->module), e.key, getParamQuantity()->paramId - M::TRACKSELECT_PARAMS);
		SmallLEDLightBezel<RedGreenBlueLight>::onHoverKey(e);
	}
};

template <typename M>
struct ZouTrackOnOffBtn : LEDLightBezel<RedGreenBlueLight> {
	void onButton(const event::Button &e) override {
		M *mod = static_cast<M*>(getParamQuantity()->module);
		const int track = getParamQuantity()->paramId - M::TRACKSONOFF_PARAMS;

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
			for (int i = 0; i < M::NUM_TRACKS; i++) {
				if (i != track) {
					if (mod->params[M::TRACKSELECT_PARAMS+i].getValue() == 1.0f) {
						mod->params[M::TRACKSELECT_PARAMS+i].setValue(0.0f);
					}
				}
				else {
					mod->edit("track solo", i, 1, 0, 0, true, [&]() {
						mod->nTracksAttibutes[mod->currentPattern][i].toggleTrackSolo();
						mod->params[M::TRACKSONOFF_PARAMS+i].setValue(mod->nTracksAttibutes[mod->currentPattern][track].getTrackSolo() ? 2.0f : 0.0f);
					});
					mod->params[M::TRACKSELECT_PARAMS+i].setValue(1.0f);
					mod->currentTrack=i;
					mod->updateTrackToParams();
					mod->updateTrigToParams();
				}
			}
			e.consume(this);
			return;
		}
		else if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			if (!mod->solo) {
				mod->edit("track on/off", track, 1, 0, 0, true, [&]() {
					mod->nTracksAttibutes[mod->currentPattern][track].toggleTrackActive();
					mod->params[M::TRACKSONOFF_PARAMS+track].setValue(mod->nTracksAttibutes[mod->currentPattern][track].getTrackActive() ? 1.0f : 0.0f);
				});
			}
			e.consume(this);
			return;
		}
		LEDLightBezel<RedGreenBlueLight>::onButton(e);
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT)
			zouTrackKey(static_cast<M*>(getParamQuantity()->module), e.key, getParamQuantity()->paramId - M::TRACKSONOFF_PARAMS);
		LEDLightBezel<RedGreenBlueLight>::onHoverKey(e);
	}
};

template <typename M>
struct ZouNoteBtn : LEDLightBezel<RedGreenBlueLight> {
	void onButton(const event::Button &e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			M *mod = static_cast<M*>(getParamQuantity()->module);
			TrigAttibutes &t = mod->nTrigsAttibutes[mod->currentPattern][mod->currentTrack][mod->currentTrig];
			const int note = getParamQuantity()->paramId - M::NOTE_PARAMS;
			if (t.getTrigSemiTones() == note) {
				mod->edit("trig on/off", mod->currentTrack, 1, mod->currentTrig, 1, false, [&]() {
					t.toggleTrigActive();
				});
			}
			else {
				mod->edit("change note", mod->currentTrack, 1, mod->currentTrig, 1, false, [&]() {
					t.setTrigSemiTones(note);
					t.setTrigActive(true);
				});
			}
			e.consume(this);
			return;
		}
		LEDLightBezel<RedGreenBlueLight>::onButton(e);
	}
};

template <typename M>
struct ZouStepBtn : LEDLightBezel<RedGreenBlueLight> {
	void onButton(const event::Button &e) override {
		if (getParamQuantity() && getParamQuantity()->module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) {
			M *mod = static_cast<M*>(getParamQuantity()->module);
			const int trig = getParamQuantity()->paramId - M::STEPS_PARAMS + mod->trigPage*16;
			mod->edit("trig on/off", mod->currentTrack, 1, trig, 1, false, [&]() {
				mod->nTrigsAttibutes[mod->currentPattern][mod->currentTrack][trig].toggleTrigActive();
			});
			mod->currentTrig = trig;
			mod->updateTrigToParams();
		}
		else if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			M *mod = static_cast<M*>(getParamQuantity()->module);
			mod->currentTrig = getParamQuantity()->paramId - M::STEPS_PARAMS + mod->trigPage*16;
			mod->updateTrigToParams();
		}

		LEDLightBezel<RedGreenBlueLight>::onButton(e);
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			M *mod = static_cast<M*>(getParamQuantity()->module);
			const int trig = getParamQuantity()->paramId - M::STEPS_PARAMS + mod->trigPage*16;
			switch (e.key) {
				case GLFW_KEY_C: zouTrigEdit(mod, ZouTrigOp::COPY, trig); break;
				case GLFW_KEY_V: zouTrigEdit(mod, ZouTrigOp::PASTE, trig); break;
				case GLFW_KEY_E: zouTrigEdit(mod, ZouTrigOp::ERASE, trig); break;
				case GLFW_KEY_R: zouTrigEdit(mod, ZouTrigOp::RAND_NOTE, trig); break;
				case GLFW_KEY_T: zouTrigEdit(mod, ZouTrigOp::RAND_NOTE_PLUS, trig); break;
				case GLFW_KEY_Y: zouTrigEdit(mod, ZouTrigOp::RAND_PROB, trig); break;
				case GLFW_KEY_U: zouTrigEdit(mod, ZouTrigOp::FULL_RAND, trig); break;
				case GLFW_KEY_F: zouTrigEdit(mod, ZouTrigOp::RAND_CV1, trig); break;
				case GLFW_KEY_G: zouTrigEdit(mod, ZouTrigOp::RAND_CV2, trig); break;
				case GLFW_KEY_W: zouTrigEdit(mod, ZouTrigOp::UP, trig); break;
				case GLFW_KEY_S: zouTrigEdit(mod, ZouTrigOp::DOWN, trig); break;
				default: break;
			}
		}
		LEDLightBezel<RedGreenBlueLight>::onHoverKey(e);
	}
};

template <typename M>
struct ZouPatternBtn : BidooRoundBlackSnapKnob {
	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			M *mod = static_cast<M*>(getParamQuantity()->module);
			switch (e.key) {
				case GLFW_KEY_C: zouPatternEdit(mod, ZouPatternOp::COPY); break;
				case GLFW_KEY_V: zouPatternEdit(mod, ZouPatternOp::PASTE); break;
				case GLFW_KEY_E: zouPatternEdit(mod, ZouPatternOp::ERASE); break;
				case GLFW_KEY_R: zouPatternEdit(mod, ZouPatternOp::RAND); break;
				case GLFW_KEY_T: zouPatternEdit(mod, ZouPatternOp::FULL_RAND); break;
				default: break;
			}
		}
		BidooRoundBlackSnapKnob::onHoverKey(e);
	}
};

template <typename M>
struct ZouSeqDisplay : TransparentWidget {
	M *module;

	// The title stays clear of the P2 button: a label too long loses its end.
	static std::string fitTitle(NVGcontext *vg, const std::string &before, const std::string &label, const std::string &after) {
		static const float WIDEST = 188.0f;
		std::string title = before + label + after;
		std::string shown = label;
		while (!shown.empty() && nvgTextBounds(vg, 0.0f, 0.0f, title.c_str(), NULL, NULL) > WIDEST) {
			char c;
			do {
				c = shown.back();
				shown.pop_back();
			} while (!shown.empty() && (c & 0xC0) == 0x80);
			title = before + shown + ".." + after;
		}
		return title;
	}

	// The two screens, drawn here and not in the panel: the track screen at the top, the trig screen below it.
	// Each is a table, one column per knob of the row under it, a label cell over a value cell.
	// The grey border lies outside the black (stroked, then filled over); the header band holds the P2 button.
	static constexpr float W = 225.0f, BORDER = 2.0f;
	static constexpr float TRACK_TOP = 0.0f, TRACK_BOTTOM = 37.0f, TRIG_TOP = 77.0f, TRIG_BOTTOM = 140.0f;
	static constexpr float HEADER = 17.0f, LABEL = 9.0f;	// the P2 button spans 2..14, the rule stays clear of it

	// Column centres: the knobs' centres (ZouSeqWidget: corner - 140 + half the knob), the CV1 switch's for the last track column.
	static constexpr float TRACK_X[6] = {16.76f, 49.76f, 82.76f, 115.76f, 158.76f, 209.0f};
	static constexpr float TRIG_X[6] = {21.76f, 58.76f, 95.76f, 132.76f, 169.76f, 206.76f};
	// Column edges: half-way between knobs, the scale's column wider for its long names.
	static constexpr float TRACK_EDGES[7] = {0.0f, 33.26f, 66.26f, 99.26f, 130.0f, 190.0f, W};
	static constexpr float TRIG_EDGES[7] = {0.0f, 40.26f, 77.26f, 114.26f, 151.26f, 188.26f, W};

	struct Cell {
		const char *label = nullptr;	// nullptr: no knob under this column on this page, the cell stays empty
		std::string value;
	};

	// The size that fits the cell, down to minSize; then the end goes.
	static std::string fitText(NVGcontext *vg, const std::string &text, const float width, const float size, const float minSize) {
		float s = size;
		nvgFontSize(vg, s);
		while (s > minSize && nvgTextBounds(vg, 0.0f, 0.0f, text.c_str(), NULL, NULL) > width) {
			s -= 0.5f;
			nvgFontSize(vg, s);
		}
		std::string shown = text;
		while (shown.size() > 1 && nvgTextBounds(vg, 0.0f, 0.0f, (shown + ".").c_str(), NULL, NULL) > width) {
			char c;
			do {
				c = shown.back();
				shown.pop_back();
			} while (!shown.empty() && (c & 0xC0) == 0x80);
			if (nvgTextBounds(vg, 0.0f, 0.0f, (shown + ".").c_str(), NULL, NULL) <= width)
				return shown + ".";
		}
		return shown;
	}

	// Over its knob, moved into its cell when it is wider than the knob's side of the cell.
	static void cellText(NVGcontext *vg, const std::string &text, const float centre, const float left, const float right, const float y, const float size, const float minSize) {
		static const float PAD = 1.5f;
		const std::string shown = fitText(vg, text, right - left - 2.0f * PAD, size, minSize);
		const float half = nvgTextBounds(vg, 0.0f, 0.0f, shown.c_str(), NULL, NULL) * 0.5f;
		const float x = clamp(centre, left + PAD + half, right - PAD - half);
		nvgText(vg, x, y, shown.c_str(), NULL);
	}

	static void screen(NVGcontext *vg, const float top, const float bottom) {
		nvgBeginPath(vg);
		nvgRect(vg, 0.0f, top, W, bottom - top);
		nvgLineJoin(vg, NVG_MITER);
		nvgStrokeWidth(vg, 2.0f * BORDER);
		nvgStrokeColor(vg, nvgRGBA(180, 180, 180, 255));
		nvgStroke(vg);
		nvgFillColor(vg, nvgRGBA(0, 0, 0, 255));
		nvgFill(vg);
	}

	static void hRule(NVGcontext *vg, const float y, const float alpha) {
		nvgBeginPath(vg);
		nvgMoveTo(vg, 0.0f, y);
		nvgLineTo(vg, W, y);
		nvgStrokeWidth(vg, 1.0f);
		nvgStrokeColor(vg, nvgRGBA(255, 233, 0, alpha));
		nvgStroke(vg);
	}

	// A table row from top to bottom: the label band, the column rules, then the six cells.
	static void tableRow(NVGcontext *vg, const float *edges, const float *centres, const float top, const float bottom, const Cell *cells) {
		nvgBeginPath(vg);
		nvgRect(vg, 0.0f, top, W, LABEL);
		nvgFillColor(vg, nvgRGBA(255, 233, 0, 30));
		nvgFill(vg);
		hRule(vg, top, 110);
		nvgBeginPath(vg);
		for (int i = 1; i < 6; i++) {
			nvgMoveTo(vg, edges[i], top);
			nvgLineTo(vg, edges[i], bottom);
		}
		nvgStrokeWidth(vg, 1.0f);
		nvgStrokeColor(vg, nvgRGBA(255, 233, 0, 70));
		nvgStroke(vg);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		const float valueY = (top + LABEL + bottom) * 0.5f + 0.5f;
		for (int i = 0; i < 6; i++) {
			if (!cells[i].label)
				continue;
			nvgFillColor(vg, nvgRGBA(255, 233, 0, 175));
			cellText(vg, cells[i].label, centres[i], edges[i], edges[i + 1], top + LABEL * 0.5f + 0.5f, 8.5f, 7.0f);
			nvgFillColor(vg, YELLOW_BIDOO);
			cellText(vg, cells[i].value, centres[i], edges[i], edges[i + 1], valueY, 10.0f, 7.0f);
		}
	}

	static void header(NVGcontext *vg, const float top, const std::string &title) {
		nvgFontSize(vg, 12.0f);
		nvgFillColor(vg, YELLOW_BIDOO);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		nvgText(vg, W * 0.5f, top + 8.5f, title.c_str(), NULL);	// level with the P2 button
	}

	static std::string fixed2(const float v) {
		char s[16];
		snprintf(s, sizeof(s), "%.2f", v);
		return s;
	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			NVGcontext *vg = args.vg;
			Cell track[6], trigA[6], trigB[6];
			std::string trackTitle, trigTitle;
			bool trigRowB = true;

			if (module) {
				const int p = module->currentPattern, t = module->currentTrack, g = module->currentTrig;
				TrigAttibutes &trig = module->nTrigsAttibutes[p][t][g];

				if (module->trackPage2) {
					char value[16];
					snprintf(value, sizeof(value), "%.4g%%", module->trackSwing[p][t] / 100.0);
					track[0] = {"Swing", value};
				}
				else {
					track[0] = {"Steps", to_string(module->nTracksAttibutes[p][t].getTrackLength())};
					track[1] = {"Speed", zouSpeedLabels[module->nTracksAttibutes[p][t].getTrackSpeed()]};
					track[2] = {"Read", displayReadMode(module->nTracksAttibutes[p][t].getTrackReadMode())};
					track[3] = {"Root", quantizer::rootNotes[module->rootNote[p][t]+1].label};
					track[4] = {"Scale", quantizer::scales[module->scale[p][t]].label};
					track[5] = {"CV1", module->quantizeCV1[p][t] == 0 ? "Free" : M::Traits::QUANTIZED_LABEL};
				}

				if (module->trigPage2) {
					char value[16];
					trigA[0] = {"CV1 End", fixed2(module->trigCV1End[p][t][g])};
					trigA[1] = {"CV2 End", fixed2(module->trigCV2End[p][t][g])};
					snprintf(value, sizeof(value), "%+d", module->trigPitchRamp[p][t][g]);
					trigA[2] = {"Pitch", value};
					snprintf(value, sizeof(value), "%.3g%%", module->trigDistRamp[p][t][g] / 100.0);
					trigA[3] = {"Dist.", value};
					trigRowB = false;
				}
				else {
					trigA[0] = {"Len.", fixed2(module->trigLength[p][t][g] / (float)M::TICKS)};
					trigA[1] = {"Puls.", to_string(trig.getTrigPulseCount())};
					trigA[2] = {"Dist.", fixed2(module->trigPulseDistance[p][t][g] / (float)M::TICKS)};
					trigA[3] = {"Type", displayTrigType(trig.getTrigType())};
					trigA[4] = {"Trim", fixed2(module->trigTrim[p][t][g] / (float)M::TICKS)};
					trigA[5] = {"Slide", fixed2(module->trigSlide[p][t][g])};
					trigB[0] = {"CV1", fixed2(module->trigCV1[p][t][g])};
					trigB[1] = {"CV2", fixed2(module->trigCV2[p][t][g])};
					trigB[2] = {"Prob.", displayProba(trig.getTrigProba())};
					if (trig.getTrigProba() < 2)
						trigB[3] = {"Val.", to_string(trig.getTrigCount())};
					if (trig.getTrigProba() == 1)
						trigB[4] = {"Base", to_string(trig.getTrigCountReset())};
					trigB[5] = {"Type", module->trigSlideType[p][t][g] ? "1" : "FULL"};
				}

				const int swingNow = module->trackSwing[p][t];
				std::string after;
				if (module->trackPage2)
					after = " - page 2";
				else if (swingNow != 5000) {
					char swing[24];
					snprintf(swing, sizeof(swing), "  swing %.4g%%", swingNow / 100.0);
					after = swing;
				}
				nvgFontSize(vg, 12.0f);
				trackTitle = fitTitle(vg, "Pattern " + to_string(p + 1) + " : ", module->labels[t], after);
				trigTitle = "Trig " + to_string(g + 1) + (module->trigPage2 ? " - page 2" : "");
			}
			else {
				track[0] = {"Steps", "16"};
				track[1] = {"Speed", "x1"};
				track[2] = {"Read", "►"};
				track[3] = {"Root", "None"};
				track[4] = {"Scale", "None"};
				track[5] = {"CV1", "Free"};
				trigA[0] = {"Len.", fixed2(M::Traits::PREVIEW_LENGTH)};
				trigA[1] = {"Puls.", "1"};
				trigA[2] = {"Dist.", fixed2(M::Traits::PREVIEW_DISTANCE)};
				trigA[3] = {"Type", "INT."};
				trigA[4] = {"Trim", "0"};
				trigA[5] = {"Slide", fixed2(0.0f)};
				trigB[0] = {"CV1", fixed2(0.0f)};
				trigB[1] = {"CV2", fixed2(0.0f)};
				trigB[2] = {"Prob.", "DICE"};
				trigB[5] = {"Type", "1"};
				trackTitle = "Pattern 0 : Track 0";
				trigTitle = "Trig 1";
			}

			// Track screen: the header, then one row over the knob row.
			screen(vg, TRACK_TOP, TRACK_BOTTOM);
			header(vg, TRACK_TOP, trackTitle);
			tableRow(vg, TRACK_EDGES, TRACK_X, TRACK_TOP + HEADER, TRACK_BOTTOM, track);

			// Trig screen: the header, then a row per knob row; page 2 has one knob row.
			screen(vg, TRIG_TOP, TRIG_BOTTOM);
			header(vg, TRIG_TOP, trigTitle);
			const float rowsTop = TRIG_TOP + HEADER, rowsBottom = TRIG_BOTTOM, middle = (rowsTop + rowsBottom) * 0.5f;
			tableRow(vg, TRIG_EDGES, TRIG_X, rowsTop, middle, trigA);
			if (trigRowB)
				tableRow(vg, TRIG_EDGES, TRIG_X, middle, rowsBottom, trigB);
		}
		Widget::drawLayer(args, layer);
	}

	std::string displayReadMode(int value) {
		switch(value){
			case 0: return "►";
			case 1: return "◄";
			case 2: return "►◄";
			case 3: return "►*";
			case 4: return "►?";
			default: return "";
		}
	}

	std::string displayTrigType(int value) {
		switch(value){
			case 0: return "INT.";
			case 1: return "GATE1";
			case 2: return "GATE2";
			default: return "";
		}
	}

	std::string displayProba(int value) {
		if (value < 1) {
			return "DICE";
		}
		else if (value < 2) {
			return "COUNT";
		}
		else if (value < 3) {
			return "FILL";
		}
		else if (value < 4) {
			return "!FILL";
		}
		else if (value < 5) {
			return "PRE";
		}
		else if (value < 6) {
			return "!PRE";
		}
		else if (value < 7) {
			return "NEI";
		}
		else if (value < 8) {
			return "!NEI";
		}
		else if (value < 9) {
			return "FIRST";
		}
		else {
			return "LAST";
		}
	}

	std::string displayNote(int semitones, int octave) {
		std::string result = "";
		if (semitones == 0)
			return "C" + to_string(octave+1);
		else if (semitones == 1)
			return "C#" + to_string(octave+1);
		else if (semitones == 2)
			return "D" + to_string(octave+1);
		else if (semitones == 3)
			return "D#" + to_string(octave+1);
		else if (semitones == 4)
			return "E" + to_string(octave+1);
		else if (semitones == 5)
			return "F" + to_string(octave+1);
		else if (semitones == 6)
			return "F#" + to_string(octave+1);
		else if (semitones == 7)
			return "G" + to_string(octave+1);
		else if (semitones == 8)
			return "G#" + to_string(octave+1);
		else if (semitones == 9)
			return "A" + to_string(octave+1);
		else if (semitones == 10)
			return "A#" + to_string(octave+1);
		else
			return "B" + to_string(octave+1);
	}
};


struct ZouProbKnob : BidooBlueSnapKnob {
	Widget *ref, *refReset;

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (ref && this->getParamQuantity() && (this->getParamQuantity()->getValue() < 1.0f)) {
				ref->visible = true;
				refReset->visible = false;
			}
			else if (ref && this->getParamQuantity() && (this->getParamQuantity()->getValue() < 2.0f)) {
				ref->visible = true;
				refReset->visible = true;
			}
			else {
				ref->visible = false;
				refReset->visible = false;
			}
		}
		Widget::drawLayer(args, layer);
	}
};


template <typename M>
struct ZouSeqWidget : BidooWidget {
	// Every button and key handler on the panel edits the module from the UI
	// thread: run them under the module's edit lock (see ZouSeq::editMutex).
	void onButton(const ButtonEvent &e) override {
		M *m = dynamic_cast<M*>(module);
		if (!m) {
			BidooWidget::onButton(e);
			return;
		}
		std::lock_guard<std::recursive_mutex> lock(m->editMutex);
		BidooWidget::onButton(e);
	}

	void onHoverKey(const HoverKeyEvent &e) override {
		M *m = dynamic_cast<M*>(module);
		if (!m) {
			BidooWidget::onHoverKey(e);
			return;
		}
		std::lock_guard<std::recursive_mutex> lock(m->editMutex);
		BidooWidget::onHoverKey(e);
	}

	std::vector<Widget*> trackKnobsPage1, trackKnobsPage2, trigKnobsPage1, trigKnobsPage2;
	bool shownTrackPage2 = false, shownTrigPage2 = false;

	void showPages(const bool track, const bool trig) {
		if (track != shownTrackPage2) {
			shownTrackPage2 = track;
			for (Widget *w : trackKnobsPage1)
				w->visible = !track;
			for (Widget *w : trackKnobsPage2)
				w->visible = track;
		}
		if (trig != shownTrigPage2) {
			shownTrigPage2 = trig;
			for (Widget *w : trigKnobsPage1)
				w->visible = !trig;
			for (Widget *w : trigKnobsPage2)
				w->visible = trig;
		}
	}

	void step() override {
		BidooWidget::step();
		if (module) {
			M *m = static_cast<M*>(module);
			m->adoptKnobHistory();
			showPages(m->trackPage2, m->trigPage2);
		}
	}

	ZouSeqWidget(M *module) {
		setModule(module);
		prepareThemes(asset::plugin(pluginInstance, M::Traits::PANEL));
		// zOu MAi and ENCORE share this widget and its layout. A dense, hand-drawn panel whose controls already sit within
		// 0.4 px of their labels and boxes: every position keeps its old corner (specific), with the widget's own size.

		theme::addScrews(this);

		{
			ZouSeqDisplay<M> *display = new ZouSeqDisplay<M>();
			display->module = module;
			display->box = theme::specificRect(140.0f, 30.0f, 200.0f, 225.0f);
			addChild(display);
		}

		static const float portY0[7] = {30.0f, 75.0f, 120.0f, 165.0f, 210.0f, 255.0f, 294.0f};

		addInput(theme::atCorner(createInput<PJ301MPort>(Vec(), module, M::EXTCLOCK_INPUT), 10.0f, portY0[0]));
		addInput(theme::atCorner(createInput<PJ301MPort>(Vec(), module, M::RESET_INPUT), 10.0f, portY0[1]));
		addInput(theme::atCorner(createInput<PJ301MPort>(Vec(), module, M::G1_INPUT), 10.0f, portY0[2]));
		addInput(theme::atCorner(createInput<PJ301MPort>(Vec(), module, M::G2_INPUT), 10.0f, portY0[3]));
		addInput(theme::atCorner(createInput<PJ301MPort>(Vec(), module, M::PATTERN_INPUT), 10.0f, portY0[4]));
		addParam(theme::atCorner(createParam<ZouPatternBtn<M>>(Vec(), module, M::PATTERN_PARAM), 7.f, portY0[5]-13.f));

		static const float portX0Controls[6] = {142.0f, 175.0f, 208.0f, 241.0f, 284.0f, 336.0f};
		static const float portX1Controls[6] = {147.0f, 184.0f, 221.0f, 258.0f, 295.0f, 332.0f};

		addParam(theme::atCorner(createParam<BidooBlueSnapKnob>(Vec(), module, M::TRACKLENGTH_PARAM), portX0Controls[0], portY0[1]-3));
		addParam(theme::atCorner(createParam<BidooBlueSnapKnob>(Vec(), module, M::TRACKSPEED_PARAM), portX0Controls[1], portY0[1]-3));
		addParam(theme::atCorner(createParam<BidooBlueSnapKnob>(Vec(), module, M::TRACKREADMODE_PARAM), portX0Controls[2], portY0[1]-3));
		addParam(theme::atCorner(createParam<BidooBlueSnapKnob>(Vec(), module, M::TRACKROOTNOTE_PARAM), portX0Controls[3], portY0[1]-3));
		addParam(theme::atCorner(createParam<BidooBlueSnapKnob>(Vec(), module, M::TRACKSCALE_PARAM), portX0Controls[4], portY0[1]-3));
		addParam(theme::atCorner(createParam<CKSS>(Vec(), module, M::TRACKQUANTIZECV1_PARAM), portX0Controls[5]+6, portY0[1]+2));

		addParam(theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, M::TRIGLENGTH_PARAM), portX1Controls[0], portY0[3]+10));
		addParam(theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, M::TRIGPULSECOUNT_PARAM), portX1Controls[1], portY0[3]+10));
		addParam(theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, M::TRIGPULSEDISTANCE_PARAM), portX1Controls[2], portY0[3]+10));
		addParam(theme::atCorner(createParam<BidooBlueSnapKnob>(Vec(), module, M::TRIGTYPE_PARAM), portX1Controls[3], portY0[3]+10));
		addParam(theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, M::TRIGTRIM_PARAM), portX1Controls[4], portY0[3]+10));
		addParam(theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, M::TRIGSLIDE_PARAM), portX1Controls[5], portY0[3]+10));
		addParam(theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, M::TRIGCV1_PARAM), portX1Controls[0], portY0[4]));
		addParam(theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, M::TRIGCV2_PARAM), portX1Controls[1], portY0[4]));
    addParam(theme::atCorner(createParam<CKSS>(Vec(), module, M::TRIGSLIDETYPE_PARAM), portX1Controls[5]+7, portY0[4]+5));

		ParamWidget *probCountKnob = theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, M::TRIGPROBACOUNT_PARAM), portX1Controls[3], portY0[4]);
		addParam(probCountKnob);
		ParamWidget *probCountResetKnob = theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, M::TRIGPROBACOUNTRESET_PARAM), portX1Controls[4], portY0[4]);
		addParam(probCountResetKnob);

		ParamWidget *probKnob = theme::atCorner(createParam<ZouProbKnob>(Vec(), module, M::TRIGPROBA_PARAM), portX1Controls[2], portY0[4]);
		ZouProbKnob *tProbKnob = dynamic_cast<ZouProbKnob*>(probKnob);
		tProbKnob->ref = probCountKnob;
		tProbKnob->refReset = probCountResetKnob;
		addParam(tProbKnob);

		// Page 2: the top row of the trig section, the rest hides.
		for (int id : {M::TRIGLENGTH_PARAM, M::TRIGPULSECOUNT_PARAM, M::TRIGPULSEDISTANCE_PARAM, M::TRIGTYPE_PARAM, M::TRIGTRIM_PARAM, M::TRIGSLIDE_PARAM, M::TRIGCV1_PARAM, M::TRIGCV2_PARAM, M::TRIGPROBA_PARAM, M::TRIGPROBACOUNT_PARAM, M::TRIGPROBACOUNTRESET_PARAM, M::TRIGSLIDETYPE_PARAM})
			trigKnobsPage1.push_back(getParam(id));
		{
			static const int ids[4] = {M::TRIGCV1END_PARAM, M::TRIGCV2END_PARAM, M::TRIGPITCHRAMP_PARAM, M::TRIGDISTRAMP_PARAM};
			for (int i = 0; i < 4; i++) {
				ParamWidget *w = i == 2 ? (ParamWidget*)theme::atCorner(createParam<BidooBlueSnapKnob>(Vec(), module, ids[i]), portX1Controls[i], portY0[3]+10) : (ParamWidget*)theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, ids[i]), portX1Controls[i], portY0[3]+10);
				w->visible = false;
				addParam(w);
				trigKnobsPage2.push_back(w);
			}
		}
		// Track page 2: the swing, the rest hides.
		for (int id : {M::TRACKLENGTH_PARAM, M::TRACKSPEED_PARAM, M::TRACKREADMODE_PARAM, M::TRACKROOTNOTE_PARAM, M::TRACKSCALE_PARAM, M::TRACKQUANTIZECV1_PARAM})
			trackKnobsPage1.push_back(getParam(id));
		{
			ParamWidget *w = theme::atCorner(createParam<BidooBlueKnob>(Vec(), module, M::TRACKSWING_PARAM), portX0Controls[0], portY0[1]-3);
			w->visible = false;
			addParam(w);
			trackKnobsPage2.push_back(w);
		}
		// In the top left corner of each screen.
		addParam(theme::atCorner(createParam<ZouScreenBtn<M>>(Vec(), module, M::TRACKPAGE2_PARAM), 141.0f, 31.0f));
		addParam(theme::atCorner(createParam<ZouScreenBtn<M>>(Vec(), module, M::TRIGPAGE2_PARAM), 141.0f, 108.0f));

		addInput(theme::atCorner(createInput<PJ301MPort>(Vec(), module, M::FILL_INPUT), 10.0f, portY0[6]+4.0f));
		addParam(theme::atCorner(createParam<BlueCKD6>(Vec(), module, M::FILL_PARAM), 40.0f, portY0[6]-1.0f+3.0f));
		addChild(theme::atCorner(createLight<SmallLight<BlueLight>>(Vec(), module, M::FILL_LIGHT), 51.5f, portY0[6]-8.0f));

		addInput(theme::atCorner(createInput<PJ301MPort>(Vec(), module, M::RUN_INPUT), 74.0f, portY0[6]+4.0f));
    addParam(theme::atCorner(createParam<BlueCKD6>(Vec(), module, M::RUN_PARAM), 104.0f, portY0[6]-1.0f+3.0f));

		//static const float portX0[5] = {374.0f, 389.0f, 404.0f, 419.0f, 434.0f};

		for (size_t i=0;i<4;i++) {
			addParam(theme::atCorner(createLightParam<ZouPageBtn<M>>(Vec(), module, M::TRIGPAGE_PARAM+i, M::TRIGPAGE_LIGHTS+3*i), 380+19*i-1.5f, 317.0f-2.5f));
		}

		for (size_t i=0;i<8;i++){
			addInput(theme::atCorner(createInput<TinyPJ301MPort>(Vec(), module, M::TRACKRESET_INPUTS + i), 50.0f, 67.0f + i*28.0f));
			addInput(theme::atCorner(createInput<TinyPJ301MPort>(Vec(), module, M::TRACKACTIVE_INPUTS + i), 70.0f, 67.0f + i*28.0f));

			addParam(theme::atCorner(createLightParam<ZouTrackOnOffBtn<M>>(Vec(), module, M::TRACKSONOFF_PARAMS+i, M::TRACKSONOFF_LIGHTS+3*i), 90.0f, 64.0f + i*28.0f));


			addParam(theme::atCorner(createLightParam<ZouTrackSelectBtn<M>>(Vec(), module, M::TRACKSELECT_PARAMS+i, M::TRACKSELECT_LIGHTS+3*i), 120.0f -2.5f, 72.0f -2.5f + i*28.0f));

			addOutput(theme::atCorner(createOutput<TinyPJ301MPort>(Vec(), module, M::GATE_OUTPUTS + i), 375.0f, 65.0f + i * 20.0f));
			addOutput(theme::atCorner(createOutput<TinyPJ301MPort>(Vec(), module, M::VO_OUTPUTS + i), 395.0f, 65.0f + i * 20.0f));
			addOutput(theme::atCorner(createOutput<TinyPJ301MPort>(Vec(), module, M::CV1_OUTPUTS + i), 415.0f, 65.0f + i * 20.0f));
			addOutput(theme::atCorner(createOutput<TinyPJ301MPort>(Vec(), module, M::CV2_OUTPUTS + i), 435.0f, 65.0f + i * 20.0f));
		}

		addOutput(theme::atCorner(createOutput<TinyPJ301MPort>(Vec(), module, M::POLY_GATE_OUTPUT), 375.0f, 225.0f));
		addOutput(theme::atCorner(createOutput<TinyPJ301MPort>(Vec(), module, M::POLY_VO_OUTPUT), 395.0f, 225.0f));
		addOutput(theme::atCorner(createOutput<TinyPJ301MPort>(Vec(), module, M::POLY_CV1_OUTPUT), 415.0f, 225.0f));
		addOutput(theme::atCorner(createOutput<TinyPJ301MPort>(Vec(), module, M::POLY_CV2_OUTPUT), 435.0f, 225.0f));

		for (size_t i=0;i<16;i++) {
			addParam(theme::atCorner(createLightParam<ZouStepBtn<M>>(Vec(), module, M::STEPS_PARAMS + i, M::STEPS_LIGHTS + i*3), 12.0f+ 28.0f*i, 330.0f));
		}

		float xKeyboardAnchor = 140.0f;
		float yKeyboardAnchor = 255.0f;

		for (size_t i=0;i<7;i++) {
			addParam(theme::atCorner(createLightParam<ZouOctaveBtn<M>>(Vec(), module, M::OCTAVE_PARAMS + i, M::OCTAVE_LIGHTS + i*3), xKeyboardAnchor + 39.5f  + i * 19.0f, yKeyboardAnchor +1.5f));
		}

		addParam(theme::atCorner(createLightParam<ZouNoteBtn<M>>(Vec(), module, M::NOTE_PARAMS, M::NOTE_LIGHTS), xKeyboardAnchor, yKeyboardAnchor+40.0f));
		addParam(theme::atCorner(createLightParam<ZouNoteBtn<M>>(Vec(), module, M::NOTE_PARAMS+1, M::NOTE_LIGHTS+3), xKeyboardAnchor+15.0f, yKeyboardAnchor+20.0f));
		addParam(theme::atCorner(createLightParam<ZouNoteBtn<M>>(Vec(), module, M::NOTE_PARAMS+2, M::NOTE_LIGHTS+3*2), xKeyboardAnchor+30.0f, yKeyboardAnchor+40.0f));
		addParam(theme::atCorner(createLightParam<ZouNoteBtn<M>>(Vec(), module, M::NOTE_PARAMS+3, M::NOTE_LIGHTS+3*3), xKeyboardAnchor+45.0f, yKeyboardAnchor+20.0f));
		addParam(theme::atCorner(createLightParam<ZouNoteBtn<M>>(Vec(), module, M::NOTE_PARAMS+4, M::NOTE_LIGHTS+3*4), xKeyboardAnchor+60.0f, yKeyboardAnchor+40.0f));
		addParam(theme::atCorner(createLightParam<ZouNoteBtn<M>>(Vec(), module, M::NOTE_PARAMS+5, M::NOTE_LIGHTS+3*5), xKeyboardAnchor+90.0f, yKeyboardAnchor+40.0f));
		addParam(theme::atCorner(createLightParam<ZouNoteBtn<M>>(Vec(), module, M::NOTE_PARAMS+6, M::NOTE_LIGHTS+3*6), xKeyboardAnchor+105.0f, yKeyboardAnchor+20.0f));
		addParam(theme::atCorner(createLightParam<ZouNoteBtn<M>>(Vec(), module, M::NOTE_PARAMS+7, M::NOTE_LIGHTS+3*7), xKeyboardAnchor+120.0f, yKeyboardAnchor+40.0f));
		addParam(theme::atCorner(createLightParam<ZouNoteBtn<M>>(Vec(), module, M::NOTE_PARAMS+8, M::NOTE_LIGHTS+3*8), xKeyboardAnchor+135.0f, yKeyboardAnchor+20.0f));
		addParam(theme::atCorner(createLightParam<ZouNoteBtn<M>>(Vec(), module, M::NOTE_PARAMS+9, M::NOTE_LIGHTS+3*9), xKeyboardAnchor+150.0f, yKeyboardAnchor+40.0f));
		addParam(theme::atCorner(createLightParam<ZouNoteBtn<M>>(Vec(), module, M::NOTE_PARAMS+10, M::NOTE_LIGHTS+3*10), xKeyboardAnchor+165.0f, yKeyboardAnchor+20.0f));
		addParam(theme::atCorner(createLightParam<ZouNoteBtn<M>>(Vec(), module, M::NOTE_PARAMS+11, M::NOTE_LIGHTS+3*11), xKeyboardAnchor+180.0f, yKeyboardAnchor+40.0f));


		ZouRecordBtn* rBtn;
		addParam(rBtn = theme::atCorner(createParam<ZouRecordBtn>(Vec(), module, M::RECORD_PARAM), 297.0f, 358.0f));

		addInput(theme::atCorner(createInput<TinyPJ301MPort>(Vec(), module, M::GATE_INPUT), 327.0f, 360.0f));
		addInput(theme::atCorner(createInput<TinyPJ301MPort>(Vec(), module, M::VO_INPUT), 356.0f, 360.0f));

		ZouQuantizeBtn* qBtn;
		addParam(qBtn = theme::atCorner(createParam<ZouQuantizeBtn>(Vec(), module, M::QUANTIZE_PARAM), 385.0f, 358.0f));
		
	}

	struct labelTextField : TextField {
		M *module;

		labelTextField() {
			this->box.pos.x = 50;
			this->box.size.x = 160;
			this->multiline = false;
		}

		void onChange(const event::Change& e) override {
			std::lock_guard<std::recursive_mutex> lock(module->editMutex);
			module->editLabel(module->currentTrack, text);
		};
	};

	// A slider of the euclidean fill: hits up to the track's Steps, rotation below them.
	struct EuclidQuantity : Quantity {
		M *module;
		bool rotation;

		float *value() {return rotation ? &module->euclidRotation : &module->euclidHits;}
		void setValue(float v) override {*value() = clamp(v, getMinValue(), getMaxValue());}
		float getValue() override {return *value();}
		float getMinValue() override {return 0.0f;}
		float getMaxValue() override {
			const int len = module->nTracksAttibutes[module->currentPattern][module->currentTrack].getTrackLength();
			return rotation ? len - 1 : len;
		}
		float getDefaultValue() override {return rotation ? 0.0f : 4.0f;}
		std::string getLabel() override {return rotation ? "Rotation" : "Hits";}
		std::string getDisplayValueString() override {return to_string((int)getValue());}
	};

	struct SwingQuantity : Quantity {
		M *module;
		void setValue(float v) override {module->editSwing(module->currentTrack, (int)std::lround(clamp(v, 50.f, 75.f) * 100.f));}
		float getValue() override {return module->trackSwing[module->currentPattern][module->currentTrack] / 100.f;}
		float getMinValue() override {return 50.f;}
		float getMaxValue() override {return 75.f;}
		float getDefaultValue() override {return 50.f;}
		std::string getLabel() override {return "Swing";}
		std::string getUnit() override {return " %";}
	};

	struct SwingSlider : ui::Slider {
		SwingSlider(M *module) {
			SwingQuantity *q = new SwingQuantity;
			q->module = module;
			quantity = q;
			box.size.x = 200.0f;
		}
		~SwingSlider() {
			delete quantity;
		}
	};

	struct EuclidSlider : ui::Slider {
		EuclidSlider(M *module, const bool rotation) {
			EuclidQuantity *q = new EuclidQuantity;
			q->module = module;
			q->rotation = rotation;
			quantity = q;
			box.size.x = 200.0f;
		}
		~EuclidSlider() {
			delete quantity;
		}
	};

	// A menu line: runs the edit under the module's edit lock.
	template <typename F>
	static MenuItem *item(M *module, const std::string &text, F edit) {
		return createMenuItem(text, "", [=]() {
			std::lock_guard<std::recursive_mutex> lock(module->editMutex);
			edit();
		});
	}

	void appendContextMenu(Menu *menu) override {
		BidooWidget::appendContextMenu(menu);
		M *module = dynamic_cast<M*>(this->module);
		assert(module);

		menu->addChild(new MenuSeparator());

		InstantiateExpanderItem *expItem = createMenuItem<InstantiateExpanderItem>("Add expander", "");
		expItem->module = module;
		expItem->model = M::Traits::expanderModel();
		expItem->posit = box.pos.plus(math::Vec(box.size.x,0));
		menu->addChild(expItem);

		menu->addChild(new MenuSeparator());

		menu->addChild(createSubmenuItem("Trig", "", [=](ui::Menu* menu) {
			static const std::pair<const char*, ZouTrigOp> items[] = {
				{"Erase (over+E)", ZouTrigOp::ERASE}, {"Copy (over+C)", ZouTrigOp::COPY}, {"Paste (over+V)", ZouTrigOp::PASTE},
				{"Rand Note (over+R)", ZouTrigOp::RAND_NOTE}, {"Rand Note+ (over+T)", ZouTrigOp::RAND_NOTE_PLUS}, {"Rand Prob (over+Y)", ZouTrigOp::RAND_PROB},
				{"Full Rand (over+U)", ZouTrigOp::FULL_RAND}, {"Rand CV1 (over+F)", ZouTrigOp::RAND_CV1}, {"Rand CV2 (over+G)", ZouTrigOp::RAND_CV2},
				{"Move Up (over+W)", ZouTrigOp::UP}, {"Move Down (over+S)", ZouTrigOp::DOWN}};
			for (const auto &it : items) {
				const ZouTrigOp op = it.second;
				menu->addChild(item(module, it.first, [=]() { zouTrigEdit(module, op, module->currentTrig); }));
			}
		}));

		menu->addChild(createSubmenuItem("Track", "", [=](ui::Menu* menu) {
			static const std::pair<const char*, ZouTrackOp> items[] = {
				{"Erase (over+E)", ZouTrackOp::ERASE}, {"Copy (over+C)", ZouTrackOp::COPY}, {"Paste (over+V)", ZouTrackOp::PASTE},
				{"Rand (over+R)", ZouTrackOp::RAND}, {"Rand Notes (over+T)", ZouTrackOp::RAND_NOTES}, {"Rand Notes+ (over+Y)", ZouTrackOp::RAND_NOTES_PLUS},
				{"Rand Probs (over+U)", ZouTrackOp::RAND_PROBS}, {"Rand CV1 (over+F)", ZouTrackOp::RAND_CV1}, {"Rand CV2 (over+G)", ZouTrackOp::RAND_CV2},
				{"Move Up (over+W)", ZouTrackOp::UP}, {"Move Down (over+S)", ZouTrackOp::DOWN}, {"Move Left (over+A)", ZouTrackOp::LEFT}, {"Move Right (over+D)", ZouTrackOp::RIGHT}};
			menu->addChild(item(module, module->slideMode[module->currentPattern][module->currentTrack] ? "Slide time✓/rate const." : "Slide time/rate✓ const.", [=]() { zouTrackEdit(module, ZouTrackOp::SLIDE_MODE, module->currentTrack); }));
			menu->addChild(createMenuItem("Hold CV1 and CV2 between notes", CHECKMARK(module->cvHold[module->currentPattern][module->currentTrack]), [=]() {
				std::lock_guard<std::recursive_mutex> lock(module->editMutex);
				zouTrackEdit(module, ZouTrackOp::CV_HOLD, module->currentTrack);
			}));
			for (const auto &it : items) {
				const ZouTrackOp op = it.second;
				menu->addChild(item(module, it.first, [=]() { zouTrackEdit(module, op, module->currentTrack); }));
			}

			menu->addChild(new SwingSlider(module));
			menu->addChild(createSubmenuItem("Euclidean", "", [=](ui::Menu* menu) {
				menu->addChild(new EuclidSlider(module, false));
				menu->addChild(new EuclidSlider(module, true));
				menu->addChild(item(module, "Fill on the track's Steps (over+B)", [=]() { zouTrackEdit(module, ZouTrackOp::EUCLID, module->currentTrack); }));
			}));

			// Add label input
			auto holder = new rack::Widget;
			holder->box.size.x = 220; // This value will determine the width of the menu
			holder->box.size.y = 20;

			auto lab = new rack::Label;
			lab->text = "Label: ";
			lab->box.size = 50; // label box size determins the bounding box around #1, #2, #3 etc.
			holder->addChild(lab);

			auto textfield = new labelTextField();
			textfield->module = module;
			textfield->text = module->labels[module->currentTrack];
			holder->addChild(textfield);

			menu->addChild(holder);
		}));

		menu->addChild(createSubmenuItem("Pattern", "", [=](ui::Menu* menu) {
			static const std::pair<const char*, ZouPatternOp> items[] = {
				{"Erase (over+E)", ZouPatternOp::ERASE}, {"Copy (over+C)", ZouPatternOp::COPY}, {"Paste (over+V)", ZouPatternOp::PASTE},
				{"Rand (over+R)", ZouPatternOp::RAND}, {"Full Rand (over+T)", ZouPatternOp::FULL_RAND}};
			for (const auto &it : items) {
				const ZouPatternOp op = it.second;
				menu->addChild(item(module, it.first, [=]() { zouPatternEdit(module, op); }));
			}
		}));

		menu->addChild(createSubmenuItem("Page", "", [=](ui::Menu* menu) {
			static const std::pair<const char*, ZouPageOp> items[] = {
				{"Erase (over+E)", ZouPageOp::ERASE}, {"Copy (over+C)", ZouPageOp::COPY}, {"Paste (over+V)", ZouPageOp::PASTE},
				{"Rand Notes (over+T)", ZouPageOp::RAND_NOTES}, {"Rand Notes+ (over+Y)", ZouPageOp::RAND_NOTES_PLUS}, {"Rand Probs (over+U)", ZouPageOp::RAND_PROBS},
				{"Rand CV1 (over+F)", ZouPageOp::RAND_CV1}, {"Rand CV2 (over+G)", ZouPageOp::RAND_CV2}};
			for (const auto &it : items) {
				const ZouPageOp op = it.second;
				menu->addChild(item(module, it.first, [=]() { zouPageEdit(module, op, module->trigPage); }));
			}
		}));
	}
};
