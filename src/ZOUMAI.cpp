#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include "window.hpp"
#include <iomanip>
#include <sstream>


using namespace std;

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

	inline void randomize() {
		setTrigActive(random::uniform()>0.5f);
		setTrigOctave(random::uniform()*2.0f + 2.0f);
		setTrigSemiTones(random::uniform()*11.0f);
	}

	inline void fullRandomize() {
		randomize();
		setTrigPulseCount(random::uniform()*10.0f);
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

	inline void setTrigActive(bool trigActive) {mainAttributes &= ~TRIG_ACTIVE; if (trigActive) mainAttributes |= TRIG_ACTIVE;}
	inline void setTrigInitialized(bool trigInitialize) {mainAttributes &= ~TRIG_INITIALIZED; if (trigInitialize) mainAttributes |= TRIG_INITIALIZED;}
	inline void setTrigSleeping(bool trigSleep) {mainAttributes &= ~TRIG_SLEEPING; if (trigSleep) mainAttributes |= TRIG_SLEEPING;}
	inline void setTrigType(int trigType) {mainAttributes &= ~TRIG_TYPE; mainAttributes |= (trigType << trigTypeShift);}
	inline void setTrigIndex(int trigIndex) {mainAttributes &= ~TRIG_INDEX; mainAttributes |= (trigIndex << trigIndexShift);}
	inline void setTrigPulseCount(int trigPulseCount) {mainAttributes &= ~TRIG_PULSECOUNT; mainAttributes |= (trigPulseCount << trigPulseCountShift);}
	inline void setTrigOctave(int trigOctave) {mainAttributes &= ~TRIG_OCTAVE; mainAttributes |= (trigOctave << trigOctaveShift);}
	inline void setTrigSemiTones(int trigSemiTones) {mainAttributes &= ~TRIG_SEMITONES; mainAttributes |= (trigSemiTones << trigSemitonesShift);}

	inline void setTrigProba(int trigProba) {probAttributes &= ~TRIG_PROBA; probAttributes |= trigProba;}
	inline void setTrigCount(int trigCount) {probAttributes &= ~TRIG_COUNT; probAttributes |= (trigCount << trigCountShift);}
	inline void setTrigCountReset(int trigCountReset) {probAttributes &= ~TRIG_COUNTRESET; probAttributes |= (trigCountReset << trigCountResetShift);}
	inline void setTrigInCount(int trigInCount) {probAttributes &= ~TRIG_INCOUNT; probAttributes |= (trigInCount << trigInCountShift);}

	inline void toggleTrigActive() {mainAttributes ^= TRIG_ACTIVE;}

	inline void setMainAttributes(unsigned long _mainAttributes) {mainAttributes = _mainAttributes;}
	inline void setProbAttributes(unsigned long _probAttributes) {probAttributes = _probAttributes;}

	inline void up() {
		if (getTrigSemiTones()==11) {
			setTrigOctave(getTrigOctave()+1);
			setTrigSemiTones(0);
		}
		else {
			setTrigSemiTones(getTrigSemiTones()+1);
		}
	}

	inline void down() {
		if (getTrigSemiTones()==0) {
			setTrigOctave(getTrigOctave()-1);
			setTrigSemiTones(0);
		}
		else {
			setTrigSemiTones(getTrigSemiTones()-1);
		}
	}

	inline bool hasProbability() {
		return (getTrigProba() != 4) && (getTrigProba() != 5) && !((getTrigProba() == 0) && (getTrigCount() == 100));
	}

	inline float getVO() {
		return (float)getTrigOctave() - 3.0f + (float)getTrigSemiTones()/12.0f;
	}

	inline void init(const bool fill, const bool pre, const bool nei) {
		if (!getTrigInitialized()) {
			setTrigInitialized(true);
			switch (getTrigProba()) {
				case 0:  // DICE
					if (getTrigCount()<100) {
						setTrigSleeping((random::uniform()*100)>=getTrigCount());
					}
					else
						setTrigSleeping(false);
					break;
				case 1:  // COUNT
					setTrigSleeping(getTrigInCount() > getTrigCount());
					setTrigInCount((getTrigInCount() >= getTrigCountReset()) ? 1 : (getTrigInCount() + 1));
					break;
				case 2:  // FILL
					setTrigSleeping(!fill);
					break;
				case 3:  // !FILL
					setTrigSleeping(fill);
					break;
				case 4:  // PRE
					setTrigSleeping(!pre);
					break;
				case 5:  // !PRE
					setTrigSleeping(pre);
					break;
				case 6:  // NEI
					setTrigSleeping(!nei);
					break;
				case 7:  // !NEI
					setTrigSleeping(nei);
					break;
				default:
					setTrigSleeping(false);
					break;
			}
		}
	}
};

struct TrackAttibutes {

	unsigned long mainAttributes;
	unsigned long refAttributes;

	static const unsigned long TRACK_ACTIVE					= 0x1;
	static const unsigned long TRACK_FORWARD				= 0x2;
	static const unsigned long TRACK_PRE						= 0x4;
	static const unsigned long TRACK_SOLO						= 0x8;
	static const unsigned long TRACK_LENGTH					= 0x7F0; static const unsigned long trackLengthShift = 4;
	static const unsigned long TRACK_READMODE				= 0x3800; static const unsigned long trackReadModeShift = 11;
	static const unsigned long TRACK_SPEED					= 0x1C000; static const unsigned long trackSpeedShift = 14;

	static const unsigned long TRACK_CURRENTTRIG		= 0xFF;
	static const unsigned long TRACK_PLAYEDTRIG			= 0xFF00; static const unsigned long trackPlayedTrigShift = 8;
	static const unsigned long TRACK_PREVTRIG				= 0xFF0000; static const unsigned long trackPrevTrigShift = 16;
	static const unsigned long TRACK_NEXTTRIG				= 0xFF000000; static const unsigned long trackNextTrigShift = 24;

	static const unsigned long intiMainAttributes = 16643;
	static const unsigned long intiRefAttributes = 0;

	inline void init() {
		mainAttributes = intiMainAttributes;
		refAttributes = intiRefAttributes;
	}

	inline void randomize() {
		setTrackLength(1+random::uniform()*63);
		setTrackReadMode(random::uniform()*4);
	}

	inline void fullRandomize() {
		randomize();
		setTrackSpeed(1+random::uniform()*3);
	}

	inline bool getTrackActive() {return (mainAttributes & TRACK_ACTIVE) != 0;}
	inline bool getTrackForward() {return (mainAttributes & TRACK_FORWARD) != 0;}
	inline bool getTrackPre() {return (mainAttributes & TRACK_PRE) != 0;}
	inline bool getTrackSolo() {return (mainAttributes & TRACK_SOLO) != 0;}
	inline int getTrackLength() {return (mainAttributes & TRACK_LENGTH) >> trackLengthShift;}
	inline int getTrackReadMode() {return (mainAttributes & TRACK_READMODE) >> trackReadModeShift;}
	inline int getTrackSpeed() {return (mainAttributes & TRACK_SPEED) >> trackSpeedShift;}

	inline int getTrackCurrentTrig() {return (refAttributes & TRACK_CURRENTTRIG);}
	inline int getTrackPlayedTrig() {return (refAttributes & TRACK_PLAYEDTRIG) >> trackPlayedTrigShift;}
	inline int getTrackPrevTrig() {return (refAttributes & TRACK_PREVTRIG) >> trackPrevTrigShift;}
	inline int getTrackNextTrig() {return (refAttributes & TRACK_NEXTTRIG) >> trackNextTrigShift;}

	inline unsigned long getMainAttributes() {return mainAttributes;}
	inline unsigned long getRefAttributes() {return refAttributes;}

	inline void setTrackActive(bool trackActive) {mainAttributes &= ~TRACK_ACTIVE; if (trackActive) mainAttributes |= TRACK_ACTIVE;}
	inline void setTrackForward(bool trackForward) {mainAttributes &= ~TRACK_FORWARD; if (trackForward) mainAttributes |= TRACK_FORWARD;}
	inline void setTrackPre(bool trackPre) {mainAttributes &= ~TRACK_PRE; if (trackPre) mainAttributes |= TRACK_PRE;}
	inline void setTrackSolo(bool trackSolo) {mainAttributes &= ~TRACK_SOLO; if (trackSolo) mainAttributes |= TRACK_SOLO;}
	inline void setTrackLength(int trackLength) {mainAttributes &= ~TRACK_LENGTH; mainAttributes |= (trackLength << trackLengthShift);}
	inline void setTrackReadMode(int trackReadMode) {mainAttributes &= ~TRACK_READMODE; mainAttributes |= (trackReadMode << trackReadModeShift);}
	inline void setTrackSpeed(int trackSpeed) {mainAttributes &= ~TRACK_SPEED; mainAttributes |= (trackSpeed << trackSpeedShift);}

	inline void setTrackCurrentTrig(int trackCurrentTrig) {refAttributes &= ~TRACK_CURRENTTRIG; refAttributes |= trackCurrentTrig;}
	inline void setTrackPlayedTrig(int trackPlayedTrig) {refAttributes &= ~TRACK_PLAYEDTRIG; refAttributes |= (trackPlayedTrig << trackPlayedTrigShift);}
	inline void setTrackPrevTrig(int trackPrevTrig) {refAttributes &= ~TRACK_PREVTRIG; refAttributes |= (trackPrevTrig << trackPrevTrigShift);}
	inline void setTrackNextTrig(int trackNextTrig) {refAttributes &= ~TRACK_NEXTTRIG; refAttributes |= (trackNextTrig << trackNextTrigShift);}

	inline void toggleTrackActive() {mainAttributes ^= TRACK_ACTIVE;}
	inline void toggleTrackSolo() {mainAttributes ^= TRACK_SOLO;}

	inline void setMainAttributes(unsigned long _mainAttributes) {mainAttributes = _mainAttributes;}
	inline void setRefAttributes(unsigned long _refAttributes) {refAttributes = _refAttributes;}
};

struct ZOUMAI : Module {
	enum ParamIds {
		STEPS_PARAMS,
		TRACKSONOFF_PARAMS = STEPS_PARAMS + 16,
		TRACKSELECT_PARAMS = TRACKSONOFF_PARAMS + 8,
		TRIGPAGE_PARAM = TRACKSELECT_PARAMS + 8,
		FILL_PARAM = TRIGPAGE_PARAM + 4,
		OCTAVE_PARAMS,
		NOTE_PARAMS = OCTAVE_PARAMS + 7,
		PATTERN_PARAM = NOTE_PARAMS + 12,
		COPYTRACK_PARAM,
		COPYPATTERN_PARAM,
		COPYTRIG_PARAM,
		PASTETRACK_PARAM,
		PASTEPATTERN_PARAM,
		PASTETRIG_PARAM,
		CLEARTRACK_PARAM,
		CLEARPATTERN_PARAM,
		CLEARTRIG_PARAM,
		RNDTRACK_PARAM,
		RNDPATTERN_PARAM,
		RNDTRIG_PARAM,
		FRNDTRACK_PARAM,
		FRNDPATTERN_PARAM,
		FRNDTRIG_PARAM,
		RIGHTTRACK_PARAM,
		LEFTTRACK_PARAM,
		UPTRACK_PARAM,
		DOWNTRACK_PARAM,
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
		NUM_PARAMS
	};

	enum InputIds {
		EXTCLOCK_INPUT,
		RESET_INPUT,
		TRACKRESET_INPUTS,
		G1_INPUT = TRACKRESET_INPUTS + 8,
		G2_INPUT,
		PATTERN_INPUT,
		TRACKACTIVE_INPUTS,
		FILL_INPUT = TRACKACTIVE_INPUTS + 8,
		NUM_INPUTS
	};

	enum OutputIds {
		GATE_OUTPUTS,
		VO_OUTPUTS = GATE_OUTPUTS + 8,
		CV1_OUTPUTS = VO_OUTPUTS + 8,
		CV2_OUTPUTS = CV1_OUTPUTS + 8,
		NUM_OUTPUTS = CV2_OUTPUTS + 8
	};

	enum LightIds {
		STEPS_LIGHTS,
		TRACKSONOFF_LIGHTS = STEPS_LIGHTS + 16*3,
		TRACKSELECT_LIGHTS = TRACKSONOFF_LIGHTS + 8*3,
		TRIGPAGE_LIGHTS = TRACKSELECT_LIGHTS + 8,
		FILL_LIGHT = TRIGPAGE_LIGHTS + 4*3,
		OCTAVE_LIGHT,
		NOTES_LIGHT = OCTAVE_LIGHT + 7,
		COPYTRACK_LIGHT = NOTES_LIGHT + 12*3,
		COPYPATTERN_LIGHT,
		COPYTRIG_LIGHT,
		PASTETRACK_LIGHT,
		PASTETRIG_LIGHT,
		PASTEPATTERN_LIGHT,
		CLEARTRACK_LIGHT,
		CLEARPATTERN_LIGHT,
		CLEARTRIG_LIGHT,
		RNDTRACK_LIGHT,
		RNDPATTERN_LIGHT,
		RNDTRIG_LIGHT,
		FRNDTRACK_LIGHT,
		FRNDPATTERN_LIGHT,
		FRNDTRIG_LIGHT,
		RIGHTTRACK_LIGHT,
		LEFTTRACK_LIGHT,
		UPTRACK_LIGHT,
		DOWNTRACK_LIGHT,
		NUM_LIGHTS
	};

	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger trigPageTriggers[4];
	dsp::SchmittTrigger trackResetTriggers[8];
	dsp::SchmittTrigger trackActiveTriggers[8];
	dsp::SchmittTrigger trackSelectTriggers[8];
	dsp::SchmittTrigger stepsTriggers[16];
	dsp::SchmittTrigger octaveTriggers[7];
	dsp::SchmittTrigger noteTriggers[12];
	dsp::SchmittTrigger fillTrigger;
	dsp::SchmittTrigger copyTrackTrigger;
	dsp::SchmittTrigger copyPatternTrigger;
	dsp::SchmittTrigger copyTrigTrigger;
	dsp::SchmittTrigger pasteTrackTrigger;
	dsp::SchmittTrigger pastePatternTrigger;
	dsp::SchmittTrigger pasteTrigTrigger;
	dsp::SchmittTrigger clearTrackTrigger;
	dsp::SchmittTrigger clearPatternTrigger;
	dsp::SchmittTrigger clearTrigTrigger;
	dsp::SchmittTrigger rndTrackTrigger;
	dsp::SchmittTrigger rndPatternTrigger;
	dsp::SchmittTrigger rndTrigTrigger;
	dsp::SchmittTrigger fRndTrackTrigger;
	dsp::SchmittTrigger fRndPatternTrigger;
	dsp::SchmittTrigger fRndTrigTrigger;
	dsp::SchmittTrigger rightTrackTrigger;
	dsp::SchmittTrigger leftTrackTrigger;
	dsp::SchmittTrigger upTrackTrigger;
	dsp::SchmittTrigger downTrackTrigger;

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

	TrigAttibutes nTrigsAttibutes[8][8][64];
	TrackAttibutes nTracksAttibutes[8][8];
	float trigSlide[8][8][64] = {{{0.0f}}};
	float trigTrim[8][8][64] = {{{0.0f}}};
	float trigLength[8][8][64] = {{{0.0f}}};
	float trigPulseDistance[8][8][64] = {{{0.5f}}};
	float trigCV1[8][8][64] = {{{0.0f}}};
	float trigCV2[8][8][64] = {{{0.0f}}};
	float trackHead[8][8] = {{0.0f}};
	float trackCurrentTickCount[8][8] = {{0.0f}};
	float trackLastTickCount[8][8] = {{22500.0f}};

	ZOUMAI() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(FILL_PARAM, 0.0f, 1.0f, 0.0f);
    configParam(TRIGPAGE_PARAM, 0.0f, 1.0f,  0.0f);
    configParam(TRIGPAGE_PARAM+1, 0.0f, 1.0f,  0.0f);
    configParam(TRIGPAGE_PARAM+2, 0.0f, 1.0f,  0.0f);
    configParam(TRIGPAGE_PARAM+3, 0.0f, 1.0f,  0.0f);

    configParam(PATTERN_PARAM, 0.0f, 7.0f, 0.0f);
    configParam(COPYTRACK_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(COPYPATTERN_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(COPYTRIG_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(PASTETRACK_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(PASTEPATTERN_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(PASTETRIG_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(CLEARTRACK_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(CLEARPATTERN_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(CLEARTRIG_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(RNDTRACK_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(RNDPATTERN_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(RNDTRIG_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(FRNDTRACK_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(FRNDPATTERN_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(FRNDTRIG_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(RIGHTTRACK_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(LEFTTRACK_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(UPTRACK_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(DOWNTRACK_PARAM, 0.0f, 1.0f, 0.0f);

		configParam(TRACKLENGTH_PARAM, 1.0f, 64.0f, 16.0f);
		configParam(TRACKSPEED_PARAM, 1.0f, 4.0f, 1.0f);
		configParam(TRACKREADMODE_PARAM, 0.0f, 4.0f, 0.0f);

		configParam(TRIGCV1_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(TRIGCV2_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(TRIGLENGTH_PARAM, 0.0f, 64.0f, 0.9f);
		configParam(TRIGPULSECOUNT_PARAM, 0.0f, 64.0f, 1.0f);
		configParam(TRIGPULSEDISTANCE_PARAM, 0.0f, 64.0f, 0.1f);
		configParam(TRIGTYPE_PARAM, 0.0f, 2.0f, 0.0f);
		configParam(TRIGTRIM_PARAM, -1.0f, 1.0f, 0.0f);
		configParam(TRIGSLIDE_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(TRIGPROBA_PARAM, 0.0f, 7.0f, 0.0f);
		configParam(TRIGPROBACOUNT_PARAM, 1.0f, 100.0f, 100.0f);
		configParam(TRIGPROBACOUNTRESET_PARAM, 1.0f, 100.0f, 1.0f);

  	for (int i=0;i<16;i++) {
      configParam(STEPS_PARAMS + i, 0.0f, 1.0f, 0.0f);
  	}

  	for (int i=0;i<8;i++) {
      configParam(TRACKSONOFF_PARAMS + i, 0.0f, 10.0f, 0.0f);
			configParam(TRACKSELECT_PARAMS + i, 0.0f, 10.0f, 0.0f);
			for (int j=0;j<8;j++) {
				for (int k=0;k<64;k++) {
					nTrigsAttibutes[i][j][k].setTrigIndex(k);
				}
			}
  	}

		onReset();
	}

	void updateTrackToParams() {
		params[TRACKLENGTH_PARAM].setValue(nTracksAttibutes[currentPattern][currentTrack].getTrackLength());
		params[TRACKSPEED_PARAM].setValue(nTracksAttibutes[currentPattern][currentTrack].getTrackSpeed());
		params[TRACKREADMODE_PARAM].setValue(nTracksAttibutes[currentPattern][currentTrack].getTrackReadMode());
	}

	void updateTrigToParams() {
		params[TRIGLENGTH_PARAM].setValue(trigLength[currentPattern][currentTrack][currentTrig]);
		params[TRIGSLIDE_PARAM].setValue(trigSlide[currentPattern][currentTrack][currentTrig]);
		params[TRIGTYPE_PARAM].setValue(nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigType());
		params[TRIGTRIM_PARAM].setValue(trigTrim[currentPattern][currentTrack][currentTrig]);
		params[TRIGPULSECOUNT_PARAM].setValue(nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigPulseCount());
		params[TRIGPULSEDISTANCE_PARAM].setValue(trigPulseDistance[currentPattern][currentTrack][currentTrig]);
		params[TRIGCV1_PARAM].setValue(trigCV1[currentPattern][currentTrack][currentTrig]);
		params[TRIGCV2_PARAM].setValue(trigCV2[currentPattern][currentTrack][currentTrig]);
		params[TRIGPROBA_PARAM].setValue(nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigProba());
		params[TRIGPROBACOUNT_PARAM].setValue(nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigCount());
		params[TRIGPROBACOUNTRESET_PARAM].setValue(nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigCountReset());
	}

	void updateTrigVO() {
		for (int i=0;i<7;i++) {
			if (octaveTriggers[i].process(params[OCTAVE_PARAMS+i].getValue())) {
				nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigOctave(i);
			}
			lights[OCTAVE_LIGHT+i].setBrightness(i==nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigOctave()?1.0f:0.f);
		}

		for (int i=0;i<12;i++) {
			bool focused = nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigSemiTones() == i;
			if (noteTriggers[i].process(params[NOTE_PARAMS+i].getValue())) {
				if (focused) {
					nTrigsAttibutes[currentPattern][currentTrack][currentTrig].toggleTrigActive();
				}
				else {
					nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigSemiTones(i);
					nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigActive(true);
				}
			}
			if ((i!=1)&&(i!=3)&&(i!=6)&&(i!=8)&&(i!=10)) {
				lights[NOTES_LIGHT+i*3].setBrightness(focused ? (nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigActive() ? 0.0f : 0.0f) : 1.0f);
				lights[NOTES_LIGHT+(i*3)+1].setBrightness(focused ? (nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigActive() ? 1.0f : 0.5f) : 1.0f);
				lights[NOTES_LIGHT+(i*3)+2].setBrightness(focused ? (nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigActive() ? 0.0f : 0.0f) : 1.0f);
			}
			else {
				lights[NOTES_LIGHT+i*3].setBrightness(focused ? (nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigActive() ? 0.0f : 0.0f) : 0.0f);
				lights[NOTES_LIGHT+(i*3)+1].setBrightness(focused ? (nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigActive() ? 1.0f : 0.5f) : 0.0f);
				lights[NOTES_LIGHT+(i*3)+2].setBrightness(focused ? (nTrigsAttibutes[currentPattern][currentTrack][currentTrig].getTrigActive() ? 0.0f : 0.0f) : 0.0f);
			}
		}
	}

	void updateParamsToTrack() {
		nTracksAttibutes[currentPattern][currentTrack].setTrackLength(params[TRACKLENGTH_PARAM].getValue());
		nTracksAttibutes[currentPattern][currentTrack].setTrackSpeed(params[TRACKSPEED_PARAM].getValue());
		nTracksAttibutes[currentPattern][currentTrack].setTrackReadMode(params[TRACKREADMODE_PARAM].getValue());
	}

	void updateParamsToTrig() {
		trigLength[currentPattern][currentTrack][currentTrig] = params[TRIGLENGTH_PARAM].getValue();
		trigSlide[currentPattern][currentTrack][currentTrig] =  params[TRIGSLIDE_PARAM].getValue();
		nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigType(params[TRIGTYPE_PARAM].getValue());
		trigTrim[currentPattern][currentTrack][currentTrig] =  params[TRIGTRIM_PARAM].getValue();
		nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigPulseCount(params[TRIGPULSECOUNT_PARAM].getValue());
		trigPulseDistance[currentPattern][currentTrack][currentTrig] = params[TRIGPULSEDISTANCE_PARAM].getValue();
		trigCV1[currentPattern][currentTrack][currentTrig] = params[TRIGCV1_PARAM].getValue();
		trigCV2[currentPattern][currentTrack][currentTrig] = params[TRIGCV2_PARAM].getValue();
		nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigProba(params[TRIGPROBA_PARAM].getValue());
		nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigCount(params[TRIGPROBACOUNT_PARAM].getValue());
		nTrigsAttibutes[currentPattern][currentTrack][currentTrig].setTrigCountReset(params[TRIGPROBACOUNTRESET_PARAM].getValue());
	}


	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "currentPattern", json_integer(currentPattern));
		json_object_set_new(rootJ, "currentTrack", json_integer(currentTrack));
		json_object_set_new(rootJ, "currentTrig", json_integer(currentTrig));
		json_object_set_new(rootJ, "trigPage", json_integer(trigPage));
		for (size_t i = 0; i<8; i++) {
			json_t *patternJ = json_object();
			for (size_t j = 0; j < 8; j++) {
				json_t *trackJ = json_object();
				json_object_set_new(trackJ, "isActive", json_boolean(nTracksAttibutes[i][j].getTrackActive()));
				json_object_set_new(trackJ, "isSolo", json_boolean(nTracksAttibutes[i][j].getTrackSolo()));
				json_object_set_new(trackJ, "speed", json_real(nTracksAttibutes[i][j].getTrackSpeed()));
				json_object_set_new(trackJ, "readMode", json_integer(nTracksAttibutes[i][j].getTrackReadMode()));
				json_object_set_new(trackJ, "length", json_integer(nTracksAttibutes[i][j].getTrackLength()));
				for (int k = 0; k < nTracksAttibutes[i][j].getTrackLength(); k++) {
					json_t *trigJ = json_object();
					json_object_set_new(trigJ, "isActive", json_boolean(nTrigsAttibutes[i][j][k].getTrigActive()));
					json_object_set_new(trigJ, "slide", json_real(trigSlide[i][j][k]));
					json_object_set_new(trigJ, "trigType", json_integer(nTrigsAttibutes[i][j][k].getTrigType()));
					json_object_set_new(trigJ, "index", json_integer(nTrigsAttibutes[i][j][k].getTrigIndex()));
					json_object_set_new(trigJ, "trim", json_real(trigTrim[i][j][k]));
					json_object_set_new(trigJ, "length", json_real(trigLength[i][j][k]));
					json_object_set_new(trigJ, "pulseCount", json_integer(nTrigsAttibutes[i][j][k].getTrigPulseCount()));
					json_object_set_new(trigJ, "pulseDistance", json_real(trigPulseDistance[i][j][k]));
					json_object_set_new(trigJ, "proba", json_integer(nTrigsAttibutes[i][j][k].getTrigProba()));
					json_object_set_new(trigJ, "count", json_integer(nTrigsAttibutes[i][j][k].getTrigCount()));
					json_object_set_new(trigJ, "countReset", json_integer(nTrigsAttibutes[i][j][k].getTrigCountReset()));
					json_object_set_new(trigJ, "octave", json_integer(nTrigsAttibutes[i][j][k].getTrigOctave()-3));
					json_object_set_new(trigJ, "semitones", json_integer(nTrigsAttibutes[i][j][k].getTrigSemiTones()));
					json_object_set_new(trigJ, "CV1", json_real(trigCV1[i][j][k]));
					json_object_set_new(trigJ, "CV2", json_real(trigCV2[i][j][k]));
					json_object_set_new(trackJ, ("trig" + to_string(k)).c_str() , trigJ);
				}
				json_object_set_new(patternJ, ("track" + to_string(j)).c_str() , trackJ);
			}
			json_object_set_new(rootJ, ("pattern" + to_string(i)).c_str(), patternJ);
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
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

		for (size_t i=0; i<8;i++) {
			json_t *patternJ = json_object_get(rootJ, ("pattern" + to_string(i)).c_str());
			if (patternJ){
				for(size_t j=0; j<8;j++) {
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
							nTracksAttibutes[i][j].setTrackSpeed(json_number_value(speedJ));
						json_t *readModeJ = json_object_get(trackJ, "readMode");
						if (readModeJ)
							nTracksAttibutes[i][j].setTrackReadMode(json_integer_value(readModeJ));
					}
					for(int k=0;k<nTracksAttibutes[i][j].getTrackLength();k++) {
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
								trigTrim[i][j][k] = json_number_value(trimJ);
							json_t *lengthJ = json_object_get(trigJ, "length");
							if (lengthJ)
								trigLength[i][j][k] = json_number_value(lengthJ);
							json_t *pulseCountJ = json_object_get(trigJ, "pulseCount");
							if (pulseCountJ)
								nTrigsAttibutes[i][j][k].setTrigPulseCount(json_integer_value(pulseCountJ));
							json_t *pulseDistanceJ = json_object_get(trigJ, "pulseDistance");
							if (pulseDistanceJ)
								trigPulseDistance[i][j][k] =  json_number_value(pulseDistanceJ);
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
						}
					}
				}
			}
		}
		updateTrackToParams();
		updateTrigToParams();
	}

	void randomizeTrig(const int track, const int trig) {
		nTrigsAttibutes[currentPattern][track][trig].randomize();
	}

	void randomizeTrack(const int track) {
		nTracksAttibutes[currentPattern][track].randomize();
		for (int i=0; i<64; i++) {
			randomizeTrig(track,i);
		}
	}

	void randomizePattern() {
		for (int i=0; i<8; i++) {
			randomizeTrack(i);
		}
	}

	void fullRandomizeTrig(const int track, const int trig) {
		nTrigsAttibutes[currentPattern][track][trig].fullRandomize();
		trigSlide[currentPattern][track][trig]=random::uniform();
		trigLength[currentPattern][track][trig]=random::uniform()*2.0f;
		trigPulseDistance[currentPattern][track][trig]=random::uniform()*2.0f;
		trigCV1[currentPattern][track][trig]=random::uniform()*10.0f;
		trigCV1[currentPattern][track][trig]=random::uniform()*10.0f;
	}

	void fullRandomizeTrack(const int track) {
		nTracksAttibutes[currentPattern][track].fullRandomize();
		for (int i=0; i<64; i++) {
			fullRandomizeTrig(track,i);
		}
	}

	void fullRandomizePattern() {
		for (int i=0; i<8; i++) {
			fullRandomizeTrack(i);
		}
	}

	void trackLeft(const int track) {
		TrigAttibutes temp = nTrigsAttibutes[currentPattern][track][0];
		float tempTrigSlide = trigSlide[currentPattern][track][0];
		float tempTrigTrim = trigTrim[currentPattern][track][0];
		float tempTrigLength = trigLength[currentPattern][track][0];
		float tempTrigPulseDistance = trigPulseDistance[currentPattern][track][0];
		float tempTrigCV1 = trigCV1[currentPattern][track][0];
		float tempTrigCV2 = trigCV2[currentPattern][track][0];
		for (int i = 0; i < nTracksAttibutes[currentPattern][track].getTrackLength()-1; i++) {
			nTrigsAttibutes[currentPattern][track][i] = nTrigsAttibutes[currentPattern][track][i+1];
		  trigSlide[currentPattern][track][i] = trigSlide[currentPattern][track][i+1];
			trigTrim[currentPattern][track][i] = trigTrim[currentPattern][track][i+1];
			trigLength[currentPattern][track][i] = trigLength[currentPattern][track][i+1];
			trigPulseDistance[currentPattern][track][i] = trigPulseDistance[currentPattern][track][i+1];
			trigCV1[currentPattern][track][i] = trigCV1[currentPattern][track][i+1];
			trigCV2[currentPattern][track][i] = trigCV2[currentPattern][track][i+1];
		}
		nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1] = temp;
		trigSlide[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1] = tempTrigSlide;
		trigTrim[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1] = tempTrigTrim;
		trigLength[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1] = tempTrigLength;
		trigPulseDistance[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1] = tempTrigPulseDistance;
		trigCV1[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1] = tempTrigCV1;
		trigCV2[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1] = tempTrigCV2;
		for (size_t i = 0; i < 64; i++) {
			nTrigsAttibutes[currentPattern][track][i].setTrigIndex(i);
		}
	}

	void trackRight(const int track) {
		TrigAttibutes temp = nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1];
		float tempTrigSlide = trigSlide[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1];
		float tempTrigTrim = trigTrim[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1];
		float tempTrigLength = trigLength[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1];
		float tempTrigPulseDistance = trigPulseDistance[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1];
		float tempTrigCV1 = trigCV1[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1];
		float tempTrigCV2 = trigCV2[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackLength()-1];
		for (int i = nTracksAttibutes[currentPattern][track].getTrackLength()-1; i > 0; i--) {
			nTrigsAttibutes[currentPattern][track][i] = nTrigsAttibutes[currentPattern][track][i-1];
		  trigSlide[currentPattern][track][i] = trigSlide[currentPattern][track][i-1];
			trigTrim[currentPattern][track][i] = trigTrim[currentPattern][track][i-1];
			trigLength[currentPattern][track][i] = trigLength[currentPattern][track][i-1];
			trigPulseDistance[currentPattern][track][i] = trigPulseDistance[currentPattern][track][i-1];
			trigCV1[currentPattern][track][i] = trigCV1[currentPattern][track][i-1];
			trigCV2[currentPattern][track][i] = trigCV2[currentPattern][track][i-1];
		}
		nTrigsAttibutes[currentPattern][track][0] = temp;
		trigSlide[currentPattern][track][0] = tempTrigSlide;
		trigTrim[currentPattern][track][0] = tempTrigTrim;
		trigLength[currentPattern][track][0] = tempTrigLength;
		trigPulseDistance[currentPattern][track][0] = tempTrigPulseDistance;
		trigCV1[currentPattern][track][0] = tempTrigCV1;
		trigCV2[currentPattern][track][0] = tempTrigCV2;
		for (size_t i = 0; i < 64; i++) {
			nTrigsAttibutes[currentPattern][track][i].setTrigIndex(i);
		}
	}

	void trackUp(const int track) {
		for (int i = 0; i < 64; i++) {
			nTrigsAttibutes[currentPattern][currentTrack][i].up();
		}
	}

	void trackDown(const int track) {
		for (int i = 0; i < 64; i++) {
			nTrigsAttibutes[currentPattern][currentTrack][i].down();
		}
	}

	void onRandomize() override {
		randomizePattern();
		updateTrackToParams();
		updateTrigToParams();
	}

	void copyTrack(const int fromPattern, const int fromTrack, const int toPattern, const int toTrack) {
		nTracksAttibutes[toPattern][toTrack].setMainAttributes(nTracksAttibutes[fromPattern][fromTrack].getMainAttributes());
		nTracksAttibutes[toPattern][toTrack].setRefAttributes(nTracksAttibutes[fromPattern][fromTrack].getRefAttributes());
		trackHead[toPattern][toTrack] = trackHead[fromPattern][fromTrack];
		trackCurrentTickCount[toPattern][toTrack] = trackCurrentTickCount[fromPattern][fromTrack];
		trackLastTickCount[toPattern][toTrack] = trackLastTickCount[fromPattern][fromTrack];
		for (int i=0; i<64; i++) {
			copyTrig(fromPattern,fromTrack,i,toPattern,toTrack,i);
		}
	}

	void copyTrig(const int fromPattern, const int fromTrack, const int fromTrig, const int toPattern, const int toTrack, const int toTrig) {
		nTrigsAttibutes[toPattern][toTrack][toTrig].setMainAttributes(nTrigsAttibutes[fromPattern][fromTrack][fromTrig].getMainAttributes());
		nTrigsAttibutes[toPattern][toTrack][toTrig].setProbAttributes(nTrigsAttibutes[fromPattern][fromTrack][fromTrig].getProbAttributes());
		trigSlide[toPattern][toTrack][toTrig] = trigSlide[fromPattern][fromTrack][fromTrig];
		trigTrim[toPattern][toTrack][toTrig] = trigTrim[fromPattern][fromTrack][fromTrig];
		trigLength[toPattern][toTrack][toTrig] = trigLength[fromPattern][fromTrack][fromTrig];
		trigPulseDistance[toPattern][toTrack][toTrig] = trigPulseDistance[fromPattern][fromTrack][fromTrig];
		trigCV1[toPattern][toTrack][toTrig] = trigCV1[fromPattern][fromTrack][fromTrig];
		trigCV2[toPattern][toTrack][toTrig] = trigCV2[fromPattern][fromTrack][fromTrig];
	}

	void trigInit(const int pattern, const int track, const int trig) {
		nTrigsAttibutes[pattern][track][trig].init();
		trigSlide[pattern][track][trig] = 0.0f;
		trigTrim[pattern][track][trig] = 0.0f;
		trigLength[pattern][track][trig] = 0.9f;
		trigPulseDistance[pattern][track][trig] = 0.5f;
		trigCV1[pattern][track][trig] = 0.0f;
		trigCV2[pattern][track][trig] = 0.0f;
	}

	void trackInit(const int pattern, const int track) {
		nTracksAttibutes[pattern][track].init();
		trackHead[pattern][track] = 0.0f;
		trackCurrentTickCount[pattern][track] = 0.0f;
		trackLastTickCount[pattern][track] = 22500.0f;
		for (int i=0; i<64; i++) {
			trigInit(pattern, track, i);
			nTrigsAttibutes[pattern][track][i].setTrigIndex(i);
		}
	}

	void onReset() override {
		for (int i=0; i<8; i++) {
			for (int j=0; j<8; j++) {
				trackInit(i,j);
			}
		}
		updateTrackToParams();
		updateTrigToParams();
	}

	void process(const ProcessArgs &args) override;

	void performTools() {
		lights[LEFTTRACK_LIGHT].setBrightness(lights[LEFTTRACK_LIGHT].getBrightness()-0.0001f*lights[LEFTTRACK_LIGHT].getBrightness());
		lights[RIGHTTRACK_LIGHT].setBrightness(lights[RIGHTTRACK_LIGHT].getBrightness()-0.0001f*lights[RIGHTTRACK_LIGHT].getBrightness());
		lights[DOWNTRACK_LIGHT].setBrightness(lights[DOWNTRACK_LIGHT].getBrightness()-0.0001f*lights[DOWNTRACK_LIGHT].getBrightness());
		lights[UPTRACK_LIGHT].setBrightness(lights[UPTRACK_LIGHT].getBrightness()-0.0001f*lights[UPTRACK_LIGHT].getBrightness());
		lights[FRNDTRACK_LIGHT].setBrightness(lights[FRNDTRACK_LIGHT].getBrightness()-0.0001f*lights[FRNDTRACK_LIGHT].getBrightness());
		lights[FRNDPATTERN_LIGHT].setBrightness(lights[FRNDPATTERN_LIGHT].getBrightness()-0.0001f*lights[FRNDPATTERN_LIGHT].getBrightness());
		lights[FRNDTRIG_LIGHT].setBrightness(lights[FRNDTRIG_LIGHT].getBrightness()-0.0001f*lights[FRNDTRIG_LIGHT].getBrightness());
		lights[RNDTRACK_LIGHT].setBrightness(lights[RNDTRACK_LIGHT].getBrightness()-0.0001f*lights[RNDTRACK_LIGHT].getBrightness());
		lights[RNDPATTERN_LIGHT].setBrightness(lights[RNDPATTERN_LIGHT].getBrightness()-0.0001f*lights[RNDPATTERN_LIGHT].getBrightness());
		lights[RNDTRIG_LIGHT].setBrightness(lights[RNDTRIG_LIGHT].getBrightness()-0.0001f*lights[RNDTRIG_LIGHT].getBrightness());
		lights[CLEARTRACK_LIGHT].setBrightness(lights[CLEARTRACK_LIGHT].getBrightness()-0.0001f*lights[CLEARTRACK_LIGHT].getBrightness());
		lights[CLEARPATTERN_LIGHT].setBrightness(lights[CLEARPATTERN_LIGHT].getBrightness()-0.0001f*lights[CLEARPATTERN_LIGHT].getBrightness());
		lights[CLEARTRIG_LIGHT].setBrightness(lights[CLEARTRIG_LIGHT].getBrightness()-0.0001f*lights[CLEARTRIG_LIGHT].getBrightness());
		lights[COPYTRACK_LIGHT].setBrightness(lights[COPYTRACK_LIGHT].getBrightness()-0.0001f*lights[COPYTRACK_LIGHT].getBrightness());
		lights[COPYPATTERN_LIGHT].setBrightness(lights[COPYPATTERN_LIGHT].getBrightness()-0.0001f*lights[COPYPATTERN_LIGHT].getBrightness());
		lights[COPYTRIG_LIGHT].setBrightness(lights[COPYTRIG_LIGHT].getBrightness()-0.0001f*lights[COPYTRIG_LIGHT].getBrightness());
		lights[PASTETRACK_LIGHT].setBrightness(lights[PASTETRACK_LIGHT].getBrightness()-0.0001f*lights[PASTETRACK_LIGHT].getBrightness());
		lights[PASTEPATTERN_LIGHT].setBrightness(lights[PASTEPATTERN_LIGHT].getBrightness()-0.0001f*lights[PASTEPATTERN_LIGHT].getBrightness());
		lights[PASTETRIG_LIGHT].setBrightness(lights[PASTETRIG_LIGHT].getBrightness()-0.0001f*lights[PASTETRIG_LIGHT].getBrightness());

		if (copyPatternTrigger.process(params[COPYPATTERN_PARAM].getValue())) {
				copyPatternId = currentPattern;
				lights[COPYPATTERN_LIGHT].setBrightness(1.0f);
		}

		if (pastePatternTrigger.process(params[PASTEPATTERN_PARAM].getValue()) && (copyPatternId>=0)) {
			copyPattern();
			lights[PASTEPATTERN_LIGHT].setBrightness(1.0f);
			updateTrackToParams();
			updateTrigToParams();
		}

		if (copyTrackTrigger.process(params[COPYTRACK_PARAM].getValue())) {
				copyTrackId = currentTrack;
				lights[COPYTRACK_LIGHT].setBrightness(1.0f);
		}

		if (pasteTrackTrigger.process(params[PASTETRACK_PARAM].getValue()) && (copyTrackId>=0)) {
			copyTrack(currentPattern,copyTrackId,currentPattern,currentTrack);
			lights[PASTETRACK_LIGHT].setBrightness(1.0f);
			updateTrackToParams();
			updateTrigToParams();
		}

		if (copyTrigTrigger.process(params[COPYTRIG_PARAM].getValue())) {
				copyTrigId = currentTrig;
				lights[COPYTRIG_LIGHT].setBrightness(1.0f);
		}

		if (pasteTrigTrigger.process(params[PASTETRIG_PARAM].getValue()) && (copyTrigId>=0)) {
			copyTrig(currentPattern,currentTrack,copyTrigId,currentPattern,currentTrack,currentTrig);
			lights[PASTETRIG_LIGHT].setBrightness(1.0f);
			updateTrackToParams();
			updateTrigToParams();
		}

		if (clearPatternTrigger.process(params[CLEARPATTERN_PARAM].getValue())) {
			for (int i=0; i<8; i++) {
				trackInit(currentPattern,i);
			}
			lights[CLEARPATTERN_LIGHT].setBrightness(1.0f);
			updateTrackToParams();
			updateTrigToParams();
		}

		if (clearTrackTrigger.process(params[CLEARTRACK_PARAM].getValue())) {
			trackInit(currentPattern,currentTrack);
			lights[CLEARTRACK_LIGHT].setBrightness(1.0f);
			updateTrackToParams();
			updateTrigToParams();
		}

		if (clearTrigTrigger.process(params[CLEARTRIG_PARAM].getValue())) {
			trigInit(currentPattern,currentTrack,currentTrig);
			lights[CLEARTRIG_LIGHT].setBrightness(1.0f);
			updateTrigToParams();
		}

		if (rndPatternTrigger.process(params[RNDPATTERN_PARAM].getValue())) {
				randomizePattern();
				lights[RNDPATTERN_LIGHT].setBrightness(1.0f);
				updateTrackToParams();
				updateTrigToParams();
		}

		if (rndTrackTrigger.process(params[RNDTRACK_PARAM].getValue())) {
				randomizeTrack(currentTrack);
				lights[RNDTRACK_LIGHT].setBrightness(1.0f);
				updateTrackToParams();
				updateTrigToParams();
		}

		if (rndTrigTrigger.process(params[RNDTRIG_PARAM].getValue())) {
				randomizeTrig(currentTrack,currentTrig);
				lights[RNDTRIG_LIGHT].setBrightness(1.0f);
				updateTrigToParams();
		}

		if (fRndPatternTrigger.process(params[FRNDPATTERN_PARAM].getValue())) {
				fullRandomizePattern();
				lights[FRNDPATTERN_LIGHT].setBrightness(1.0f);
				updateTrackToParams();
				updateTrigToParams();
		}

		if (fRndTrackTrigger.process(params[FRNDTRACK_PARAM].getValue())) {
				fullRandomizeTrack(currentTrack);
				lights[FRNDTRACK_LIGHT].setBrightness(1.0f);
				updateTrackToParams();
				updateTrigToParams();
		}

		if (fRndTrigTrigger.process(params[FRNDTRIG_PARAM].getValue())) {
				fullRandomizeTrig(currentTrack,currentTrig);
				lights[FRNDTRIG_LIGHT].setBrightness(1.0f);
				updateTrackToParams();
				updateTrigToParams();
		}

		if (upTrackTrigger.process(params[UPTRACK_PARAM].getValue())) {
				trackUp(currentTrack);
				lights[UPTRACK_LIGHT].setBrightness(1.0f);
				updateTrigToParams();
		}

		if (downTrackTrigger.process(params[DOWNTRACK_PARAM].getValue())) {
				trackDown(currentTrack);
				lights[DOWNTRACK_LIGHT].setBrightness(1.0f);
				updateTrigToParams();
		}

		if (rightTrackTrigger.process(params[RIGHTTRACK_PARAM].getValue())) {
				trackRight(currentTrack);
				lights[RIGHTTRACK_LIGHT].setBrightness(1.0f);
				updateTrigToParams();
		}

		if (leftTrackTrigger.process(params[LEFTTRACK_PARAM].getValue())) {
				trackLeft(currentTrack);
				lights[LEFTTRACK_LIGHT].setBrightness(1.0f);
				updateTrigToParams();
		}
	}

	bool patternIsSoloed() {
		for (size_t i = 0; i < 8; i++) {
			if (nTracksAttibutes[currentPattern][i].getTrackSolo()) {
				return true;
			}
		}
		return false;
	}

	void copyPattern() {
		for (int i=0; i<8; i++) {
			copyTrack(copyPatternId,i,currentPattern,i);
			for (int j=0; j<64; j++) {
				copyTrig(copyPatternId,i,j,currentPattern,i,j);
			}
		}
	}

	void trackSync(const int track, const float cCount, const float lCount) {
		trackCurrentTickCount[currentPattern][track] = cCount;
		trackLastTickCount[currentPattern][track] = lCount;
	}

	float trackGetGate(const int track) {
		if (nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()].getTrigActive() && !nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()].getTrigSleeping()) {
			float rTP = trigGetRelativeTrackPosition(track, nTracksAttibutes[currentPattern][track].getTrackPlayedTrig());
			if (rTP >= 0) {
				if (rTP<trigLength[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()]) {
					return 10.0f;
				}
				else {
					int cPulses = (trigPulseDistance[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()] == 0) ? 0 : (int)(rTP/(float)trigPulseDistance[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()]);
					return ((cPulses<nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()].getTrigPulseCount())
					&& (rTP>=(cPulses*trigPulseDistance[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()]))
					&& (rTP<=((cPulses*trigPulseDistance[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()])+trigLength[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()]))) ? 10.0f : 0.0f;
				}
			}
			else
				return 0.0f;
		}
		else
			return 0.0f;
	}

	float trigGetRelativeTrackPosition(const int track, const int trig) {
		return trackHead[currentPattern][track] - trigGetTrimedIndex(track, trig);
	}

	float trigGetFullLength(const int track, const int trig) {
		return nTrigsAttibutes[currentPattern][track][trig].getTrigPulseCount() == 1 ? trigLength[currentPattern][track][trig] : ((nTrigsAttibutes[currentPattern][track][trig].getTrigPulseCount()*trigPulseDistance[currentPattern][track][trig]) + trigLength[currentPattern][track][trig]);
	}

	bool trigGetIsRead(const int track, const int trig, const float trackPosition) {
		return ((trackPosition>=trigGetTrimedIndex(track,trig)) && (trackPosition<=(trigGetTrimedIndex(track, trig)+trigGetFullLength(track, trig))));
	}

	float trigGetTrimedIndex(const int track, const int trig) {
		return nTrigsAttibutes[currentPattern][track][trig].getTrigIndex() + trigTrim[currentPattern][track][trig];
	}

	float trackGetVO(const int track) {
		if (trigSlide[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()] == 0.0f) {
			return nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()].getVO();
		}
		else
		{
			float fullLength = trigGetFullLength(track,nTracksAttibutes[currentPattern][track].getTrackPlayedTrig());
			if (fullLength > 0.0f) {
				float subPhase = trigGetRelativeTrackPosition(track, nTracksAttibutes[currentPattern][track].getTrackPlayedTrig());
				return nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()].getVO()
				- (1.0f - powf(subPhase/trigGetFullLength(track, nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()), trigSlide[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()]))
				* (nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()].getVO() - nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPrevTrig()].getVO());
			}
			else {
				return nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()].getVO();
			}
		}
	}

	void trackSetCurrentTrig(const int track, const bool fill, const bool pNei, const bool force=false) {
		int cI = nTracksAttibutes[currentPattern][track].getTrackCurrentTrig();
		if (((int)trackHead[currentPattern][track] != cI) || force) {
			nTracksAttibutes[currentPattern][track].setTrackPre((nTrigsAttibutes[currentPattern][track][cI].getTrigActive() && nTrigsAttibutes[currentPattern][track][cI].hasProbability()) ? !nTrigsAttibutes[currentPattern][track][cI].getTrigSleeping() : nTracksAttibutes[currentPattern][track].getTrackPre());
			nTrigsAttibutes[currentPattern][track][cI].setTrigInitialized(false);
			nTracksAttibutes[currentPattern][track].setTrackCurrentTrig((int)trackHead[currentPattern][track]);
			nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()].init(fill,nTracksAttibutes[currentPattern][track].getTrackPre(),pNei);
			nTracksAttibutes[currentPattern][track].setTrackPre((nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()].getTrigActive()
			&& nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()].hasProbability()) ? !nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()].getTrigSleeping() : nTracksAttibutes[currentPattern][track].getTrackPre());
			trackSetNextTrig(track);
			nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackNextTrig()].init(fill,nTracksAttibutes[currentPattern][track].getTrackPre(),pNei);
		}

		if (trigGetIsRead(track, nTracksAttibutes[currentPattern][track].getTrackCurrentTrig(), trackHead[currentPattern][track]) && (nTracksAttibutes[currentPattern][track].getTrackCurrentTrig() != nTracksAttibutes[currentPattern][track].getTrackPlayedTrig())
		&& nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()].getTrigActive() && !nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()].getTrigSleeping()) {
			nTracksAttibutes[currentPattern][track].setTrackPrevTrig(nTracksAttibutes[currentPattern][track].getTrackPlayedTrig());
			nTracksAttibutes[currentPattern][track].setTrackPlayedTrig(nTracksAttibutes[currentPattern][track].getTrackCurrentTrig());
		}
		else if (trigGetIsRead(track, nTracksAttibutes[currentPattern][track].getTrackNextTrig(), trackHead[currentPattern][track]) && (nTracksAttibutes[currentPattern][track].getTrackNextTrig() != nTracksAttibutes[currentPattern][track].getTrackPlayedTrig()) && nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackNextTrig()].getTrigActive() && !nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackNextTrig()].getTrigSleeping()) {
			nTracksAttibutes[currentPattern][track].setTrackPrevTrig(nTracksAttibutes[currentPattern][track].getTrackPlayedTrig());
			nTracksAttibutes[currentPattern][track].setTrackPlayedTrig(nTracksAttibutes[currentPattern][track].getTrackNextTrig());
		}
	}

	void trackReset(const int track, const bool fill, const bool pNei) {
		nTracksAttibutes[currentPattern][track].setTrackPre(false);
		nTracksAttibutes[currentPattern][track].setTrackForward(true);

		if (nTracksAttibutes[currentPattern][track].getTrackReadMode() == 1)
		{
			nTracksAttibutes[currentPattern][track].setTrackForward(false);
			trackHead[currentPattern][track] = nTracksAttibutes[currentPattern][track].getTrackLength()-1;
			trackSetCurrentTrig(track, fill, pNei,true);
			trackHead[currentPattern][track] = nTracksAttibutes[currentPattern][track].getTrackLength();
		}
		else
		{
			trackHead[currentPattern][track] = 0.0f;
			trackSetCurrentTrig(track, fill, pNei);
		}
	}

	void trackSetNextTrig(const int track) {
		switch (nTracksAttibutes[currentPattern][track].getTrackReadMode()) {
			case 0:
					nTracksAttibutes[currentPattern][track].setTrackNextTrig((nTracksAttibutes[currentPattern][track].getTrackCurrentTrig() == (nTracksAttibutes[currentPattern][track].getTrackLength()-1)) ? 0 : (nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()+1)); break;
			case 1:
					nTracksAttibutes[currentPattern][track].setTrackNextTrig((nTracksAttibutes[currentPattern][track].getTrackCurrentTrig() == 0) ? (nTracksAttibutes[currentPattern][track].getTrackLength()-1) : (nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()-1)); break;
			case 2: {
				if (nTracksAttibutes[currentPattern][track].getTrackCurrentTrig() == 0) {
					nTracksAttibutes[currentPattern][track].setTrackNextTrig(nTracksAttibutes[currentPattern][track].getTrackLength() > 1 ? 1: 0);
				}
				else if (nTracksAttibutes[currentPattern][track].getTrackCurrentTrig() == (nTracksAttibutes[currentPattern][track].getTrackLength() - 1)) {
					nTracksAttibutes[currentPattern][track].setTrackNextTrig(nTracksAttibutes[currentPattern][track].getTrackLength() > 1 ? (nTracksAttibutes[currentPattern][track].getTrackLength()-2) : 0);
				}
				else {
					nTracksAttibutes[currentPattern][track].setTrackNextTrig(clamp(nTracksAttibutes[currentPattern][track].getTrackCurrentTrig() + (nTracksAttibutes[currentPattern][track].getTrackForward() ? 1 : -1),0,nTracksAttibutes[currentPattern][track].getTrackLength() - 1));
				}
			  break;
			}
			case 3: nTracksAttibutes[currentPattern][track].setTrackNextTrig((int)(random::uniform()*(nTracksAttibutes[currentPattern][track].getTrackLength() - 1))); break;
			case 4:
			{
				float dice = random::uniform();
				if (dice>=0.5f)
					nTracksAttibutes[currentPattern][track].setTrackNextTrig(((nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()+1) > (nTracksAttibutes[currentPattern][track].getTrackLength() - 1) ? 0 : (nTracksAttibutes[currentPattern][track].getTrackCurrentTrig() + 1)));
				else if (dice<=0.25f)
					nTracksAttibutes[currentPattern][track].setTrackNextTrig(nTracksAttibutes[currentPattern][track].getTrackCurrentTrig() == 0 ? (nTracksAttibutes[currentPattern][track].getTrackLength() - 1) : (nTracksAttibutes[currentPattern][track].getTrackCurrentTrig() - 1));
				else
					nTracksAttibutes[currentPattern][track].setTrackNextTrig(nTracksAttibutes[currentPattern][track].getTrackCurrentTrig());
				break;
			}
			default : nTracksAttibutes[currentPattern][track].setTrackNextTrig(nTracksAttibutes[currentPattern][track].getTrackCurrentTrig());
		}
	}

	void trackMoveNextForward(const int track, const bool step, const bool fill, const bool pNei) {
		nTracksAttibutes[currentPattern][track].setTrackForward(true);
		if (step) {
			trackHead[currentPattern][track] = round(trackHead[currentPattern][track]);
			trackLastTickCount[currentPattern][track] = trackCurrentTickCount[currentPattern][track];
			trackCurrentTickCount[currentPattern][track] = 0.0f;
		}
		else {
			trackCurrentTickCount[currentPattern][track]++;
			trackHead[currentPattern][track] += nTracksAttibutes[currentPattern][track].getTrackSpeed()/trackLastTickCount[currentPattern][track];
		}

		if (trackHead[currentPattern][track] >= nTracksAttibutes[currentPattern][track].getTrackLength()) {
			trackReset(track, fill, pNei);
			return;
		}

		trackSetCurrentTrig(track, fill, pNei);
	}

	void trackMoveNextBackward(const int track, const bool step, const bool fill, const bool pNei) {
		nTracksAttibutes[currentPattern][track].setTrackForward(false);
		if (step) {
			trackHead[currentPattern][track] = round(trackHead[currentPattern][track]);
			trackLastTickCount[currentPattern][track] = trackCurrentTickCount[currentPattern][track];
			trackCurrentTickCount[currentPattern][track] = 0.0f;
		}
		else {
			trackCurrentTickCount[currentPattern][track]++;
			trackHead[currentPattern][track] -= nTracksAttibutes[currentPattern][track].getTrackSpeed()/trackLastTickCount[currentPattern][track];
		}

		if (trackHead[currentPattern][track] <= 0) {
			trackReset(track, fill, pNei);
			return;
		}

		trackSetCurrentTrig(track, fill, pNei);
	}

	void trackMoveNextPendulum(const int track, const bool step, const bool fill, const bool pNei) {
		if (step) {
			trackHead[currentPattern][track] = round(trackHead[currentPattern][track]);
			trackLastTickCount[currentPattern][track] = trackCurrentTickCount[currentPattern][track];
			trackCurrentTickCount[currentPattern][track] = 0.0f;
		}
		else {
			trackCurrentTickCount[currentPattern][track]++;
			trackHead[currentPattern][track] = trackHead[currentPattern][track] + (nTracksAttibutes[currentPattern][track].getTrackForward() ? 1 : -1 ) * nTracksAttibutes[currentPattern][track].getTrackSpeed()/trackLastTickCount[currentPattern][track];
		}

		if (trackHead[currentPattern][track] >= nTracksAttibutes[currentPattern][track].getTrackLength()) {
			nTracksAttibutes[currentPattern][track].setTrackForward(false);
			trackHead[currentPattern][track] = (nTracksAttibutes[currentPattern][track].getTrackLength() == 1) ? 1 : (nTracksAttibutes[currentPattern][track].getTrackLength()-1);
		}
		else if (trackHead[currentPattern][track] <= 0) {
			nTracksAttibutes[currentPattern][track].setTrackForward(true);
			trackHead[currentPattern][track] = nTracksAttibutes[currentPattern][track].getTrackLength() > 1 ? 1 : 0;
		}

		trackSetCurrentTrig(track, fill, pNei);
	}

	void trackMoveNextRandom(const int track, const bool step, const bool fill, const bool pNei) {
		nTracksAttibutes[currentPattern][track].setTrackForward(true);
		if (step) {
			trackHead[currentPattern][track] = round(trackHead[currentPattern][track]);
			trackLastTickCount[currentPattern][track] = trackCurrentTickCount[currentPattern][track];
			trackCurrentTickCount[currentPattern][track] = 0.0f;
		}
		else {
			trackCurrentTickCount[currentPattern][track]++;
			trackHead[currentPattern][track] += nTracksAttibutes[currentPattern][track].getTrackSpeed()/trackLastTickCount[currentPattern][track];
		}

		if (trackHead[currentPattern][track] >= nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()+1) {
			trackHead[currentPattern][track] = nTracksAttibutes[currentPattern][track].getTrackNextTrig();
			trackSetCurrentTrig(track, fill, pNei, true);
			return;
		}

		trackSetCurrentTrig(track, fill, pNei);
	}

	void trackMoveNextBrownian(const int track, const bool step, const bool fill, const bool pNei) {
		nTracksAttibutes[currentPattern][track].setTrackForward(true);
		if (step) {
			trackHead[currentPattern][track] = round(trackHead[currentPattern][track]);
			trackLastTickCount[currentPattern][track] = trackCurrentTickCount[currentPattern][track];
			trackCurrentTickCount[currentPattern][track] = 0.0f;
		}
		else {
			trackCurrentTickCount[currentPattern][track]++;
			trackHead[currentPattern][track] += nTracksAttibutes[currentPattern][track].getTrackSpeed()/trackLastTickCount[currentPattern][track];
		}

		if (trackHead[currentPattern][track] >= nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()+1) {
			trackHead[currentPattern][track] = nTracksAttibutes[currentPattern][track].getTrackNextTrig();
			trackSetCurrentTrig(track, fill, pNei, true);
			return;
		}

		trackSetCurrentTrig(track, fill, pNei);
	}

	void trackMoveNext(const int track, const bool step, const bool fill, const bool pNei) {
		switch (nTracksAttibutes[currentPattern][track].getTrackReadMode()) {
			case 0: trackMoveNextForward(track, step, fill, pNei); break;
			case 1: trackMoveNextBackward(track, step, fill, pNei); break;
			case 2: trackMoveNextPendulum(track, step, fill, pNei); break;
			case 3: trackMoveNextRandom(track, step, fill, pNei); break;
			case 4: trackMoveNextBrownian(track, step, fill, pNei); break;
		}
	}

};

void ZOUMAI::process(const ProcessArgs &args) {
	currentPattern = (int)clamp((inputs[PATTERN_INPUT].isConnected() ? rescale(clamp(inputs[PATTERN_INPUT].getVoltage(), 0.0f, 10.0f),0.0f,10.0f,0.0f,7.0f) : 0) + (int)params[PATTERN_PARAM].getValue(), 0.0f, 7.0f);

	if (inputs[FILL_INPUT].isConnected()) {
		if (((inputs[FILL_INPUT].getVoltage() > 0.0f) && !fill) || ((inputs[FILL_INPUT].getVoltage() == 0.0f) && fill)) fill=!fill;
	}
	if (fillTrigger.process(params[FILL_PARAM].getValue())) {
		fill = !fill;
	}
	lights[FILL_LIGHT].setBrightness(fill ? 1.0f : 0.0f);

	bool solo = patternIsSoloed();

	if (currentPattern != previousPattern) {
		for (size_t i = 0; i<8; i++) {
			trackSync(i,trackCurrentTickCount[previousPattern][i], trackLastTickCount[previousPattern][i]);
			trackReset(i, fill, i == 0 ? false : nTracksAttibutes[currentPattern][i-1].getTrackPre());
		}
		previousPattern = currentPattern;
		updateTrackToParams();
		updateTrigToParams();
	}
	else {
		updateTrigVO();
		updateParamsToTrack();
		updateParamsToTrig();
	}

	performTools();

	for (int i = 0; i<4; i++) {
		if (trigPageTriggers[i].process(params[TRIGPAGE_PARAM + i].getValue())) {
			trigPage = i;
			if (currentTrig>48) currentTrig = currentTrig - 48;
			if (currentTrig>32) currentTrig = currentTrig - 32;
			if (currentTrig>16) currentTrig = currentTrig - 16;
			currentTrig = i*16+currentTrig;
			updateTrigToParams();
		}
		if (trigPage == i) {
			lights[TRIGPAGE_LIGHTS+(i*3)].setBrightness(0.0f);
			lights[TRIGPAGE_LIGHTS+(i*3)+1].setBrightness(0.0f);
			lights[TRIGPAGE_LIGHTS+(i*3)+2].setBrightness(1.0f);
		}
		else if ((nTracksAttibutes[currentPattern][currentTrack].getTrackCurrentTrig() >= (i*16)) && (nTracksAttibutes[currentPattern][currentTrack].getTrackCurrentTrig()<(16*(i+1)-1))) {
			lights[TRIGPAGE_LIGHTS+(i*3)].setBrightness(1.0f);
			lights[TRIGPAGE_LIGHTS+(i*3)+1].setBrightness(0.5f);
			lights[TRIGPAGE_LIGHTS+(i*3)+2].setBrightness(0.0f);
		}
		else {
			lights[TRIGPAGE_LIGHTS+(i*3)].setBrightness(0.0f);
			lights[TRIGPAGE_LIGHTS+(i*3)+1].setBrightness(0.0f);
			lights[TRIGPAGE_LIGHTS+(i*3)+2].setBrightness(0.0f);
		}
	}

	int pageOffset = trigPage*16;
	for (size_t i = 0; i<16; i++) {
		if (stepsTriggers[i].process(params[STEPS_PARAMS+i].getValue())) {
			currentTrig = i + pageOffset;
			updateTrigToParams();
		}

		int shiftedIndex = i + pageOffset;
		if (nTracksAttibutes[currentPattern][currentTrack].getTrackCurrentTrig() == shiftedIndex) {
			lights[STEPS_LIGHTS+(i*3)].setBrightness(1.0f);
			lights[STEPS_LIGHTS+(i*3)+1].setBrightness(0.5f);
			lights[STEPS_LIGHTS+(i*3)+2].setBrightness(0.0f);
		}
		else if (nTrigsAttibutes[currentPattern][currentTrack][shiftedIndex].getTrigActive()) {
			if (shiftedIndex == currentTrig) {
				lights[STEPS_LIGHTS+(i*3)].setBrightness(0.0f);
				lights[STEPS_LIGHTS+(i*3)+1].setBrightness(0.0f);
				lights[STEPS_LIGHTS+(i*3)+2].setBrightness(1.0f);
			}
			else {
				lights[STEPS_LIGHTS+(i*3)].setBrightness(0.0f);
				lights[STEPS_LIGHTS+(i*3)+1].setBrightness(1.0f);
				lights[STEPS_LIGHTS+(i*3)+2].setBrightness(0.0f);
			}
		}
		else if (currentTrig == shiftedIndex) {
			lights[STEPS_LIGHTS+(i*3)].setBrightness(0.0f);
			lights[STEPS_LIGHTS+(i*3)+1].setBrightness(0.0f);
			lights[STEPS_LIGHTS+(i*3)+2].setBrightness(0.5f);
		}
		else {
			lights[STEPS_LIGHTS+(i*3)].setBrightness(0.0f);
			lights[STEPS_LIGHTS+(i*3)+1].setBrightness(0.0f);
			lights[STEPS_LIGHTS+(i*3)+2].setBrightness(0.0f);
		}
	}

	if (inputs[EXTCLOCK_INPUT].isConnected()) {

		bool globalReset = resetTrigger.process(inputs[RESET_INPUT].getVoltage());
		bool clockTrigged = clockTrigger.process(inputs[EXTCLOCK_INPUT].getVoltage());

		for (int i=0; i<8;i++) {

			if (trackActiveTriggers[i].process(inputs[TRACKACTIVE_INPUTS+i].getVoltage() + params[TRACKSONOFF_PARAMS+i].getValue())) {
				nTracksAttibutes[currentPattern][i].toggleTrackActive();
			}

			if (trackSelectTriggers[i].process(params[TRACKSELECT_PARAMS+i].getValue())) {
				currentTrack=i;
				updateTrackToParams();
				updateTrigToParams();
			}

			lights[TRACKSELECT_LIGHTS+i].setBrightness(0.0f);
			if (currentTrack==i) {
				lights[TRACKSELECT_LIGHTS+i].setBrightness(1.0f);
			}

			if (!solo && nTracksAttibutes[currentPattern][i].getTrackActive()) {
				lights[TRACKSONOFF_LIGHTS+(i*3)].setBrightness(0.0f);
				lights[TRACKSONOFF_LIGHTS+(i*3)+1].setBrightness(1.0f);
				lights[TRACKSONOFF_LIGHTS+(i*3)+2].setBrightness(0.0f);
			}
			else if (solo && nTracksAttibutes[currentPattern][i].getTrackSolo()) {
				lights[TRACKSONOFF_LIGHTS+(i*3)].setBrightness(1.0f);
				lights[TRACKSONOFF_LIGHTS+(i*3)+1].setBrightness(0.0f);
				lights[TRACKSONOFF_LIGHTS+(i*3)+2].setBrightness(0.0f);
			}
			else {
				lights[TRACKSONOFF_LIGHTS+(i*3)].setBrightness(0.0f);
				lights[TRACKSONOFF_LIGHTS+(i*3)+1].setBrightness(0.0f);
				lights[TRACKSONOFF_LIGHTS+(i*3)+2].setBrightness(0.0f);
			}

			if (clockTrigged) {
				trackMoveNext(i, true, fill, i == 0 ? false : nTracksAttibutes[currentPattern][i-1].getTrackPre());
			}
			else {
				trackMoveNext(i, false, fill, i == 0 ? false : nTracksAttibutes[currentPattern][i-1].getTrackPre());
			}

			if (trackResetTriggers[i].process(inputs[TRACKRESET_INPUTS+i].getVoltage()) || (!inputs[TRACKRESET_INPUTS+i].isConnected() && globalReset)) {
				trackReset(i, fill, i == 0 ? false : nTracksAttibutes[currentPattern][i-1].getTrackPre());
			}

			if ((patternIsSoloed() && nTracksAttibutes[currentPattern][i].getTrackSolo()) || (!patternIsSoloed() && nTracksAttibutes[currentPattern][i].getTrackActive())) {
				float gate = trackGetGate(i);
				if (gate>0.0f) {
					if (nTrigsAttibutes[currentPattern][i][nTracksAttibutes[currentPattern][i].getTrackPlayedTrig()].getTrigType() == 0)
						outputs[GATE_OUTPUTS + i].setVoltage(gate);
					else if (nTrigsAttibutes[currentPattern][i][nTracksAttibutes[currentPattern][i].getTrackPlayedTrig()].getTrigType() == 1)
						outputs[GATE_OUTPUTS + i].setVoltage(inputs[G1_INPUT].getVoltage());
					else if (nTrigsAttibutes[currentPattern][i][nTracksAttibutes[currentPattern][i].getTrackPlayedTrig()].getTrigType() == 2)
						outputs[GATE_OUTPUTS + i].setVoltage(inputs[G2_INPUT].getVoltage());
					else
						outputs[GATE_OUTPUTS + i].setVoltage(0.0f);
				}
				else
					outputs[GATE_OUTPUTS + i].setVoltage(0.0f);
			}
			else
				outputs[GATE_OUTPUTS + i].setVoltage(0.0f);

			outputs[VO_OUTPUTS + i].setVoltage(trackGetVO(i));
			outputs[CV1_OUTPUTS + i].setVoltage(trigCV1[currentPattern][i][nTracksAttibutes[currentPattern][i].getTrackPlayedTrig()]);
			outputs[CV2_OUTPUTS + i].setVoltage(trigCV2[currentPattern][i][nTracksAttibutes[currentPattern][i].getTrackPlayedTrig()]);
		}
	}
}

struct ZOUMAIDisplay : TransparentWidget {
	ZOUMAI *module;
	shared_ptr<Font> font;

	ZOUMAIDisplay() {
		font = APP->window->loadFont(asset::plugin(pluginInstance, "res/DejaVuSansMono.ttf"));
	}

	void draw(const DrawArgs &args) override {
		stringstream sPatternHeader, sSteps, sSpeed, sRead;
		stringstream sTrigHeader, sLen, sPuls, sDist, sType, sTrim, sSlide, sVO, sCV1, sCV2, sProb, sProbBase;
		nvgBeginPath(args.vg);
		nvgRect(args.vg, 25.0f, 0.0f, 175.0f, 37.0f);
		nvgRect(args.vg, 0.0f, 77.0f, 225.0f, 63.0f);
		nvgClosePath(args.vg);
		nvgLineCap(args.vg, NVG_MITER);
		nvgStrokeWidth(args.vg, 4.0f);
		nvgStrokeColor(args.vg, nvgRGBA(180, 180, 180, 255));
		nvgStroke(args.vg);
		nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 255));
		nvgFill(args.vg);
		static const float portX0[3] = {45.0f, 112.5f, 180.0f};
		static const float portX1[6] = {20.0f, 57.0f, 94.0f, 131.0F, 168.0f, 205.0f};
		static const float portY0[8] = {11.0f, 22.0f, 32.0f, 89.0f, 100.0f, 110.0f, 122.0f, 134.0f};
		nvgFillColor(args.vg, YELLOW_BIDOO);
		nvgTextAlign(args.vg, NVG_ALIGN_CENTER);

		if (module) {
			sPatternHeader << "Pattern " + to_string(module->currentPattern + 1) + " : Track " + to_string(module->currentTrack + 1);
			sSteps << module->nTracksAttibutes[module->currentPattern][module->currentTrack].getTrackLength();
			sSpeed << fixed << setprecision(2) << module->nTracksAttibutes[module->currentPattern][module->currentTrack].getTrackSpeed();
			sRead << displayReadMode(module->nTracksAttibutes[module->currentPattern][module->currentTrack].getTrackReadMode());
			sTrigHeader << "Trig " + to_string(module->currentTrig + 1);
			sLen << fixed << setprecision(2) << (float)module->trigLength[module->currentPattern][module->currentTrack][module->currentTrig];
			sPuls << to_string(module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigPulseCount()).c_str();
			sDist << fixed << setprecision(2) << (float)module->trigPulseDistance[module->currentPattern][module->currentTrack][module->currentTrig];
			sType << displayTrigType(module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigType()).c_str();
			sTrim << fixed << setprecision(2) << module->trigTrim[module->currentPattern][module->currentTrack][module->currentTrig];
			sSlide << fixed << setprecision(2) << module->trigSlide[module->currentPattern][module->currentTrack][module->currentTrig];
			sVO << displayNote(module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigSemiTones(), module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigOctave());
			sCV1 << fixed << setprecision(2) << module->trigCV1[module->currentPattern][module->currentTrack][module->currentTrig];
			sCV2 << fixed << setprecision(2) << module->trigCV2[module->currentPattern][module->currentTrack][module->currentTrig];
			sProb << displayProba(module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigProba());
			nvgFontSize(args.vg, 10.0f);
			if (module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigProba() < 2)
			{
				nvgText(args.vg, portX1[3], portY0[6], "Val.", NULL);
				nvgText(args.vg, portX1[3], portY0[7], to_string(module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigCount()).c_str(), NULL);
			}
			if (module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigProba() == 1)
			{
				nvgText(args.vg, portX1[4], portY0[6], "Base", NULL);
				nvgText(args.vg, portX1[4], portY0[7], to_string(module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigCountReset()).c_str(), NULL);
			}
		}
		else {
			sPatternHeader << "Pattern 0 : Track 0";
			sSteps << 16;
			sSpeed << fixed << setprecision(2) << 1;
			sRead << "►";
			sTrigHeader << "Trig " + to_string(1);
			sLen << fixed << setprecision(2) << 0.99f;
			sPuls << to_string(1).c_str();
			sDist << fixed << setprecision(2) << 1.0f;
			sType << "INT.";
			sTrim << 0;
			sSlide << fixed << setprecision(2) << 0.0f;
			sVO << "C4";
			sCV1 << fixed << setprecision(2) << 0.0f;
			sCV2 << fixed << setprecision(2) << 0.0f;
			sProb << "DICE";
		}

		nvgFontSize(args.vg, 12.0f);
		nvgText(args.vg, 112.5f, portY0[0], sPatternHeader.str().c_str(), NULL);
		nvgFontSize(args.vg, 10.0f);
		nvgText(args.vg, portX0[0], portY0[1], "Steps", NULL);
		nvgText(args.vg, portX0[0], portY0[2], sSteps.str().c_str(), NULL);
		nvgText(args.vg, portX0[1], portY0[1], "Speed", NULL);
		nvgText(args.vg, portX0[1], portY0[2], ("x" + sSpeed.str()).c_str(), NULL);
		nvgText(args.vg, portX0[2], portY0[1], "Read", NULL);
		nvgText(args.vg, portX0[2], portY0[2], sRead.str().c_str(), NULL);

		nvgFontSize(args.vg, 12.0f);
		nvgText(args.vg, 112.5f, portY0[3], sTrigHeader.str().c_str(), NULL);
		nvgFontSize(args.vg, 10.0f);
		nvgText(args.vg, portX1[0], portY0[4], "Len.", NULL);
		nvgText(args.vg, portX1[0], portY0[5], sLen.str().c_str(), NULL);
		nvgText(args.vg, portX1[1], portY0[4], "Puls.", NULL);
		nvgText(args.vg, portX1[1], portY0[5], sPuls.str().c_str(), NULL);
		nvgText(args.vg, portX1[2], portY0[4], "Dist.", NULL);
		nvgText(args.vg, portX1[2], portY0[5], sDist.str().c_str(), NULL);
		nvgText(args.vg, portX1[3], portY0[4], "Type", NULL);
		nvgText(args.vg, portX1[3], portY0[5], sType.str().c_str(), NULL);
		nvgText(args.vg, portX1[4], portY0[4], "Trim", NULL);
		nvgText(args.vg, portX1[4], portY0[5], sTrim.str().c_str(), NULL);
		nvgText(args.vg, portX1[5], portY0[4], "Slide", NULL);
		nvgText(args.vg, portX1[5], portY0[5], sSlide.str().c_str(), NULL);
		nvgText(args.vg, portX1[0], portY0[6], "CV1", NULL);
		nvgText(args.vg, portX1[0], portY0[7], sCV1.str().c_str(), NULL);
		nvgText(args.vg, portX1[1], portY0[6], "CV2", NULL);
		nvgText(args.vg, portX1[1], portY0[7], sCV2.str().c_str(), NULL);
		nvgText(args.vg, portX1[2], portY0[6], "Prob.", NULL);
		nvgText(args.vg, portX1[2], portY0[7], sProb.str().c_str(), NULL);
		nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 255));
		nvgText(args.vg, portX1[5], portY0[6], "V/Oct", NULL);
		nvgText(args.vg, portX1[5], portY0[7], sVO.str().c_str(), NULL);
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
		else {
			return "!NEI";
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

template <typename BASE>
struct ZOUMAILight : BASE {
	ZOUMAILight() {
		this->box.size = mm2px(Vec(6.0f, 6.0f));
	}
};

struct BidooProbBlueKnob : BidooBlueSnapKnob {
	Widget *ref, *refReset;
	void draw(const DrawArgs &args) override {
		if (ref && this->paramQuantity && (this->paramQuantity->getValue() < 1.0f)) {
			ref->visible = true;
			refReset->visible = false;
		}
		else if (ref && this->paramQuantity && (this->paramQuantity->getValue() < 2.0f)) {
			ref->visible = true;
			refReset->visible = true;
		}
		else {
			ref->visible = false;
			refReset->visible = false;
		}
		BidooBlueSnapKnob::draw(args);
	}
};

struct ZoumaiLEDBezel : LEDBezel {
	int index=0;
	void onButton(const event::Button &e) override {
		if (paramQuantity && paramQuantity->module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) {
			ZOUMAI* zou = dynamic_cast<ZOUMAI*>(this->paramQuantity->module);
			zou->nTracksAttibutes[zou->currentPattern][index].toggleTrackSolo();
			zou->currentTrack = index;
			zou->updateTrackToParams();
			zou->updateTrigToParams();
		}
		LEDBezel::onButton(e);
	}
};

struct ZoumaiTrigLEDBezel : LEDBezel {
	int index=0;
	void onButton(const event::Button &e) override {
		if (paramQuantity && paramQuantity->module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) {
			ZOUMAI* zou = dynamic_cast<ZOUMAI*>(this->paramQuantity->module);
			zou->nTrigsAttibutes[zou->currentPattern][zou->currentTrack][index+zou->trigPage*16].toggleTrigActive();
		}
		LEDBezel::onButton(e);
	}
};

struct ZOUMAIWidget : ModuleWidget {
	ZOUMAIWidget(ZOUMAI *module) {
		setModule(module);
		setPanel(APP->window->loadSvg(asset::plugin(pluginInstance, "res/ZOUMAI.svg")));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			ZOUMAIDisplay *display = new ZOUMAIDisplay();
			display->module = module;
			display->box.pos = Vec(140.0f, 30.0f);
			display->box.size = Vec(200.0f, 225.0f);
			addChild(display);
		}

		static const float portY0[7] = {30.0f, 75.0f, 120.0f, 165.0f, 210.0f, 255.0f, 294.0f};

		addInput(createInput<PJ301MPort>(Vec(10.0f, portY0[0]), module, ZOUMAI::EXTCLOCK_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10.0f, portY0[1]), module, ZOUMAI::RESET_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10.0f, portY0[2]), module, ZOUMAI::G1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10.0f, portY0[3]), module, ZOUMAI::G2_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10.0f, portY0[4]), module, ZOUMAI::PATTERN_INPUT));
		addParam(createParam<BidooRoundBlackSnapKnob>(Vec(7.f,portY0[5]-13.f), module, ZOUMAI::PATTERN_PARAM));

		static const float portX0Controls[3] = {170.0f, 238.5f, 305.0f};
		static const float portX1Controls[6] = {147.0f, 184.0f, 221.0f, 258.0f, 295.0f, 332.0f};

		addParam(createParam<BidooBlueSnapKnob>(Vec(portX0Controls[0],portY0[1]-3), module, ZOUMAI::TRACKLENGTH_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(portX0Controls[1],portY0[1]-3), module, ZOUMAI::TRACKSPEED_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(portX0Controls[2],portY0[1]-3), module, ZOUMAI::TRACKREADMODE_PARAM));

		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[0],portY0[3]+10), module, ZOUMAI::TRIGLENGTH_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[1],portY0[3]+10), module, ZOUMAI::TRIGPULSECOUNT_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[2],portY0[3]+10), module, ZOUMAI::TRIGPULSEDISTANCE_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(portX1Controls[3],portY0[3]+10), module, ZOUMAI::TRIGTYPE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[4],portY0[3]+10), module, ZOUMAI::TRIGTRIM_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[5],portY0[3]+10), module, ZOUMAI::TRIGSLIDE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[0],portY0[4]), module, ZOUMAI::TRIGCV1_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[1],portY0[4]), module, ZOUMAI::TRIGCV2_PARAM));

		// addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[2],portY0[4]), module, ZOUMAI::TRIGPROBA_PARAM));
		// addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[3],portY0[4]), module, ZOUMAI::TRIGPROBACOUNT_PARAM));
		// addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[4],portY0[4]), module, ZOUMAI::TRIGPROBACOUNTRESET_PARAM));

		ParamWidget *probCountKnob = createParam<BidooBlueKnob>(Vec(portX1Controls[3],portY0[4]), module, ZOUMAI::TRIGPROBACOUNT_PARAM);
		addParam(probCountKnob);
		ParamWidget *probCountResetKnob = createParam<BidooBlueKnob>(Vec(portX1Controls[4],portY0[4]), module, ZOUMAI::TRIGPROBACOUNTRESET_PARAM);
		addParam(probCountResetKnob);

		ParamWidget *probKnob = createParam<BidooProbBlueKnob>(Vec(portX1Controls[2],portY0[4]), module, ZOUMAI::TRIGPROBA_PARAM);
		BidooProbBlueKnob *tProbKnob = dynamic_cast<BidooProbBlueKnob*>(probKnob);
		tProbKnob->ref = probCountKnob;
		tProbKnob->refReset = probCountResetKnob;
		addParam(tProbKnob);

		addInput(createInput<PJ301MPort>(Vec(10.0f, portY0[6]), module, ZOUMAI::FILL_INPUT));
		addParam(createParam<BlueCKD6>(Vec(40.0f, portY0[6]-1.0f), module, ZOUMAI::FILL_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(73.0f, portY0[6]+10.0f), module, ZOUMAI::FILL_LIGHT));

		static const float portX0[5] = {374.0f, 389.0f, 404.0f, 419.0f, 434.0f};

		addParam(createParam<LEDButton>(Vec(380-6, 317.0f-6), module, ZOUMAI::TRIGPAGE_PARAM));
		addParam(createParam<LEDButton>(Vec(380+19-6, 317.0f-6), module, ZOUMAI::TRIGPAGE_PARAM+1));
		addParam(createParam<LEDButton>(Vec(380+2*19-6, 317.0f-6), module, ZOUMAI::TRIGPAGE_PARAM+2));
		addParam(createParam<LEDButton>(Vec(380+3*19-6, 317.0f-6), module, ZOUMAI::TRIGPAGE_PARAM+3));

		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(380, 317.0f), module, ZOUMAI::TRIGPAGE_LIGHTS));
		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(380+19, 317.0f), module, ZOUMAI::TRIGPAGE_LIGHTS+3));
		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(380+2*19, 317.0f), module, ZOUMAI::TRIGPAGE_LIGHTS+6));
		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(380+3*19, 317.0f), module, ZOUMAI::TRIGPAGE_LIGHTS+9));

		static const float portYFunctions[4] = {244.0f, 256.0f, 268.0f, 280.0f};

		addParam(createParam<MiniLEDButton>(Vec(portX0[0], portYFunctions[0]), module, ZOUMAI::COPYPATTERN_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[0], portYFunctions[0]), module, ZOUMAI::COPYPATTERN_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[1], portYFunctions[0]), module, ZOUMAI::PASTEPATTERN_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[1], portYFunctions[0]), module, ZOUMAI::PASTEPATTERN_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[2], portYFunctions[0]), module, ZOUMAI::CLEARPATTERN_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[2], portYFunctions[0]), module, ZOUMAI::CLEARPATTERN_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[3], portYFunctions[0]), module, ZOUMAI::RNDPATTERN_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[3], portYFunctions[0]), module, ZOUMAI::RNDPATTERN_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[4], portYFunctions[0]), module, ZOUMAI::FRNDPATTERN_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[4], portYFunctions[0]), module, ZOUMAI::FRNDPATTERN_LIGHT));

		addParam(createParam<MiniLEDButton>(Vec(portX0[0], portYFunctions[1]), module, ZOUMAI::COPYTRIG_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[0], portYFunctions[1]), module, ZOUMAI::COPYTRIG_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[1], portYFunctions[1]), module, ZOUMAI::PASTETRIG_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[1], portYFunctions[1]), module, ZOUMAI::PASTETRIG_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[2], portYFunctions[1]), module, ZOUMAI::CLEARTRIG_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[2], portYFunctions[1]), module, ZOUMAI::CLEARTRIG_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[3], portYFunctions[1]), module, ZOUMAI::RNDTRIG_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[3], portYFunctions[1]), module, ZOUMAI::RNDTRIG_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[4], portYFunctions[1]), module, ZOUMAI::FRNDTRIG_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[4], portYFunctions[1]), module, ZOUMAI::FRNDTRIG_LIGHT));

		addParam(createParam<MiniLEDButton>(Vec(portX0[0], portYFunctions[2]), module, ZOUMAI::COPYTRACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[0], portYFunctions[2]), module, ZOUMAI::COPYTRACK_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[1], portYFunctions[2]), module, ZOUMAI::PASTETRACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[1], portYFunctions[2]), module, ZOUMAI::PASTETRACK_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[2], portYFunctions[2]), module, ZOUMAI::CLEARTRACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[2], portYFunctions[2]), module, ZOUMAI::CLEARTRACK_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[3], portYFunctions[2]), module, ZOUMAI::RNDTRACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[3], portYFunctions[2]), module, ZOUMAI::RNDTRACK_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[4], portYFunctions[2]), module, ZOUMAI::FRNDTRACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[4], portYFunctions[2]), module, ZOUMAI::FRNDTRACK_LIGHT));

		addParam(createParam<MiniLEDButton>(Vec(portX0[0], portYFunctions[3]), module, ZOUMAI::RIGHTTRACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[0], portYFunctions[3]), module, ZOUMAI::RIGHTTRACK_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[1], portYFunctions[3]), module, ZOUMAI::LEFTTRACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[1], portYFunctions[3]), module, ZOUMAI::LEFTTRACK_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[2], portYFunctions[3]), module, ZOUMAI::UPTRACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[2], portYFunctions[3]), module, ZOUMAI::UPTRACK_LIGHT));
		addParam(createParam<MiniLEDButton>(Vec(portX0[3], portYFunctions[3]), module, ZOUMAI::DOWNTRACK_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(portX0[3], portYFunctions[3]), module, ZOUMAI::DOWNTRACK_LIGHT));

		for (size_t i=0;i<8;i++){
			addInput(createInput<TinyPJ301MPort>(Vec(50.0f, 67.0f + i*28.0f), module, ZOUMAI::TRACKRESET_INPUTS + i));
			addInput(createInput<TinyPJ301MPort>(Vec(70.0f, 67.0f + i*28.0f), module, ZOUMAI::TRACKACTIVE_INPUTS + i));

			ZoumaiLEDBezel *p = createParam<ZoumaiLEDBezel>(Vec(90.0f , 64.0f + i*28.0f), module, ZOUMAI::TRACKSONOFF_PARAMS + i);
			p->index=i;
			addParam(p);

			addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(92.0f, 66.0f + i*28.0f), module, ZOUMAI::TRACKSONOFF_LIGHTS + i*3));

			addParam(createParam<LEDButton>(Vec(120.0f - 6, 72.0f - 6 + i*28.0f), module, ZOUMAI::TRACKSELECT_PARAMS + i));
			addChild(createLight<SmallLight<BlueLight>>(Vec(120.0f, 72.0f + i*28.0f), module, ZOUMAI::TRACKSELECT_LIGHTS + i));

			addOutput(createOutput<TinyPJ301MPort>(Vec(375.0f, 65.0f + i * 20.0f), module, ZOUMAI::GATE_OUTPUTS + i));
			addOutput(createOutput<TinyPJ301MPort>(Vec(395.0f, 65.0f + i * 20.0f),  module, ZOUMAI::VO_OUTPUTS + i));
			addOutput(createOutput<TinyPJ301MPort>(Vec(415.0f, 65.0f + i * 20.0f),  module, ZOUMAI::CV1_OUTPUTS + i));
			addOutput(createOutput<TinyPJ301MPort>(Vec(435.0f, 65.0f + i * 20.0f),  module, ZOUMAI::CV2_OUTPUTS + i));
		}

		for (size_t i=0;i<16;i++) {
			ZoumaiTrigLEDBezel *p = createParam<ZoumaiTrigLEDBezel>(Vec(12.0f+ 28.0f*i, 330.0f), module, ZOUMAI::STEPS_PARAMS + i);
			p->index=i;
			addParam(p);

			addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(14.0f+ 28.0f*i, 332.0f), module, ZOUMAI::STEPS_LIGHTS + i*3));
		}

		float xKeyboardAnchor = 140.0f;
		float yKeyboardAnchor = 255.0f;

		for (size_t i=0;i<7;i++) {
			addParam(createParam<LEDButton>(Vec(xKeyboardAnchor + 35.0f + i * 19.0f, yKeyboardAnchor - 1.0f), module, ZOUMAI::OCTAVE_PARAMS+i));
			addChild(createLight<SmallLight<BlueLight>>(Vec(xKeyboardAnchor + 41.0f  + i * 19.0f, yKeyboardAnchor+5.0f), module, ZOUMAI::OCTAVE_LIGHT+i));
		}

		addParam(createParam<LEDBezel>(Vec(xKeyboardAnchor, yKeyboardAnchor+40.0f), module, ZOUMAI::NOTE_PARAMS));
		addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(xKeyboardAnchor+2.0f, yKeyboardAnchor+40.0f+2.0f), module, ZOUMAI::NOTES_LIGHT));
		addParam(createParam<LEDBezel>(Vec(xKeyboardAnchor+15.0f, yKeyboardAnchor+20.0f), module, ZOUMAI::NOTE_PARAMS+1));
		addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(xKeyboardAnchor+15.0f+2.0f, yKeyboardAnchor+20.0f+2.0f), module, ZOUMAI::NOTES_LIGHT+1*3));
		addParam(createParam<LEDBezel>(Vec(xKeyboardAnchor+30.0f, yKeyboardAnchor+40.0f), module, ZOUMAI::NOTE_PARAMS+2));
		addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(xKeyboardAnchor+30.0f+2.0f, yKeyboardAnchor+40.0f+2.0f), module, ZOUMAI::NOTES_LIGHT+2*3));
		addParam(createParam<LEDBezel>(Vec(xKeyboardAnchor+45.0f, yKeyboardAnchor+20.0f), module, ZOUMAI::NOTE_PARAMS+3));
		addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(xKeyboardAnchor+45.0f+2.0f, yKeyboardAnchor+20.0f+2.0f), module, ZOUMAI::NOTES_LIGHT+3*3));
		addParam(createParam<LEDBezel>(Vec(xKeyboardAnchor+60.0f, yKeyboardAnchor+40.0f), module, ZOUMAI::NOTE_PARAMS+4));
		addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(xKeyboardAnchor+60.0f+2.0f, yKeyboardAnchor+40.0f+2.0f), module, ZOUMAI::NOTES_LIGHT+4*3));
		addParam(createParam<LEDBezel>(Vec(xKeyboardAnchor+90.0f, yKeyboardAnchor+40.0f), module, ZOUMAI::NOTE_PARAMS+5));
		addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(xKeyboardAnchor+90.0f+2.0f, yKeyboardAnchor+40.0f+2.0f), module, ZOUMAI::NOTES_LIGHT+5*3));
		addParam(createParam<LEDBezel>(Vec(xKeyboardAnchor+105.0f, yKeyboardAnchor+20.0f), module, ZOUMAI::NOTE_PARAMS+6));
		addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(xKeyboardAnchor+105.0f+2.0f, yKeyboardAnchor+20.0f+2.0f), module, ZOUMAI::NOTES_LIGHT+6*3));
		addParam(createParam<LEDBezel>(Vec(xKeyboardAnchor+120.0f, yKeyboardAnchor+40.0f), module, ZOUMAI::NOTE_PARAMS+7));
		addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(xKeyboardAnchor+120.0f+2.0f, yKeyboardAnchor+40.0f+2.0f), module, ZOUMAI::NOTES_LIGHT+7*3));
		addParam(createParam<LEDBezel>(Vec(xKeyboardAnchor+135.0f, yKeyboardAnchor+20.0f), module, ZOUMAI::NOTE_PARAMS+8));
		addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(xKeyboardAnchor+135.0f+2.0f, yKeyboardAnchor+20.0f+2.0f), module, ZOUMAI::NOTES_LIGHT+8*3));
		addParam(createParam<LEDBezel>(Vec(xKeyboardAnchor+150.0f, yKeyboardAnchor+40.0f), module, ZOUMAI::NOTE_PARAMS+9));
		addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(xKeyboardAnchor+150.0f+2.0f, yKeyboardAnchor+40.0f+2.0f), module, ZOUMAI::NOTES_LIGHT+9*3));
		addParam(createParam<LEDBezel>(Vec(xKeyboardAnchor+165.0f, yKeyboardAnchor+20.0f), module, ZOUMAI::NOTE_PARAMS+10));
		addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(xKeyboardAnchor+165.0f+2.0f, yKeyboardAnchor+20.0f+2.0f), module, ZOUMAI::NOTES_LIGHT+10*3));
		addParam(createParam<LEDBezel>(Vec(xKeyboardAnchor+180.0f, yKeyboardAnchor+40.0f), module, ZOUMAI::NOTE_PARAMS+11));
		addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(xKeyboardAnchor+180.0f+2.0f, yKeyboardAnchor+40.0f+2.0f), module, ZOUMAI::NOTES_LIGHT+11*3));

	}
};

Model *modelZOUMAI = createModel<ZOUMAI, ZOUMAIWidget>("ZOUMAI");
