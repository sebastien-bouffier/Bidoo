#include "plugin.hpp"
#include "dsp/digital.hpp"
#include "BidooComponents.hpp"
#include <iostream>
#include <vector>
#include <random>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include "dep/quantizer.hpp"

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

	inline void randomizeProbs() {
		setTrigProba(random::uniform()*7.0f);
		setTrigCount(random::uniform()*100.0f);
		setTrigCountReset(random::uniform()*100.0f);
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

	inline void setTrigActive(const bool trigActive) {mainAttributes &= ~TRIG_ACTIVE; if (trigActive) mainAttributes |= TRIG_ACTIVE;}
	inline void setTrigInitialized(const bool trigInitialize) {mainAttributes &= ~TRIG_INITIALIZED; if (trigInitialize) mainAttributes |= TRIG_INITIALIZED;}
	inline void setTrigSleeping(const bool trigSleep) {mainAttributes &= ~TRIG_SLEEPING; if (trigSleep) mainAttributes |= TRIG_SLEEPING;}
	inline void setTrigType(const int trigType) {mainAttributes &= ~TRIG_TYPE; mainAttributes |= (trigType << trigTypeShift);}
	inline void setTrigIndex(const int trigIndex) {mainAttributes &= ~TRIG_INDEX; mainAttributes |= (trigIndex << trigIndexShift);}
	inline void setTrigPulseCount(const int trigPulseCount) {mainAttributes &= ~TRIG_PULSECOUNT; mainAttributes |= (trigPulseCount << trigPulseCountShift);}
	inline void setTrigOctave(const int trigOctave) {mainAttributes &= ~TRIG_OCTAVE; mainAttributes |= (trigOctave << trigOctaveShift);}
	inline void setTrigSemiTones(const int trigSemiTones) {mainAttributes &= ~TRIG_SEMITONES; mainAttributes |= (trigSemiTones << trigSemitonesShift);}

	inline void setTrigProba(const int trigProba) {probAttributes &= ~TRIG_PROBA; probAttributes |= trigProba;}
	inline void setTrigCount(const int trigCount) {probAttributes &= ~TRIG_COUNT; probAttributes |= (trigCount << trigCountShift);}
	inline void setTrigCountReset(const int trigCountReset) {probAttributes &= ~TRIG_COUNTRESET; probAttributes |= (trigCountReset << trigCountResetShift);}
	inline void setTrigInCount(const int trigInCount) {probAttributes &= ~TRIG_INCOUNT; probAttributes |= (trigInCount << trigInCountShift);}

	inline void toggleTrigActive() {mainAttributes ^= TRIG_ACTIVE;}

	inline void setMainAttributes(const unsigned long _mainAttributes) {mainAttributes = _mainAttributes;}
	inline void setProbAttributes(const unsigned long _probAttributes) {probAttributes = _probAttributes;}

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
			setTrigSemiTones(11);
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
					setTrigSleeping(getTrigInCount() > getTrigCount());
					setTrigInCount((getTrigInCount() >= getTrigCountReset()) ? 1 : (getTrigInCount() + 1));
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
	static const unsigned long TRACK_SPEED					= 0x3C000; static const unsigned long trackSpeedShift = 14;

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

	inline void setTrackActive(const bool trackActive) {mainAttributes &= ~TRACK_ACTIVE; if (trackActive) mainAttributes |= TRACK_ACTIVE;}
	inline void setTrackForward(const bool trackForward) {mainAttributes &= ~TRACK_FORWARD; if (trackForward) mainAttributes |= TRACK_FORWARD;}
	inline void setTrackPre(const bool trackPre) {mainAttributes &= ~TRACK_PRE; if (trackPre) mainAttributes |= TRACK_PRE;}
	inline void setTrackSolo(const bool trackSolo) {mainAttributes &= ~TRACK_SOLO; if (trackSolo) mainAttributes |= TRACK_SOLO;}
	inline void setTrackLength(const int trackLength) {mainAttributes &= ~TRACK_LENGTH; mainAttributes |= (trackLength << trackLengthShift);}
	inline void setTrackReadMode(const int trackReadMode) {mainAttributes &= ~TRACK_READMODE; mainAttributes |= (trackReadMode << trackReadModeShift);}
	inline void setTrackSpeed(const int trackSpeed) {mainAttributes &= ~TRACK_SPEED; mainAttributes |= (trackSpeed << trackSpeedShift);}

	inline void setTrackCurrentTrig(const int trackCurrentTrig) {refAttributes &= ~TRACK_CURRENTTRIG; refAttributes |= trackCurrentTrig;}
	inline void setTrackPlayedTrig(const int trackPlayedTrig) {refAttributes &= ~TRACK_PLAYEDTRIG; refAttributes |= (trackPlayedTrig << trackPlayedTrigShift);}
	inline void setTrackPrevTrig(const int trackPrevTrig) {refAttributes &= ~TRACK_PREVTRIG; refAttributes |= (trackPrevTrig << trackPrevTrigShift);}
	inline void setTrackNextTrig(const int trackNextTrig) {refAttributes &= ~TRACK_NEXTTRIG; refAttributes |= (trackNextTrig << trackNextTrigShift);}

	inline void toggleTrackActive() {mainAttributes ^= TRACK_ACTIVE;}
	inline void toggleTrackSolo() {mainAttributes ^= TRACK_SOLO;}

	inline void setMainAttributes(const unsigned long _mainAttributes) {mainAttributes = _mainAttributes;}
	inline void setRefAttributes(const unsigned long _refAttributes) {refAttributes = _refAttributes;}
};

struct ENCORE : BidooModule {
	enum ParamIds {
		STEPS_PARAMS,
		TRACKSONOFF_PARAMS = STEPS_PARAMS + 16,
		TRACKSELECT_PARAMS = TRACKSONOFF_PARAMS + 8,
		TRIGPAGE_PARAM = TRACKSELECT_PARAMS + 8,
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
		GATE_INPUT,
		VO_INPUT,
		RUN_INPUT,
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
		NOTE_LIGHTS = STEPS_LIGHTS + 16*3,
		TRACKSONOFF_LIGHTS = NOTE_LIGHTS + 12*3,
		TRACKSELECT_LIGHTS = TRACKSONOFF_LIGHTS + 8*3,
		TRIGPAGE_LIGHTS = TRACKSELECT_LIGHTS + 8*3,
		OCTAVE_LIGHTS = TRIGPAGE_LIGHTS + 4*3,
		FILL_LIGHT = OCTAVE_LIGHTS + 7*3,
		NUM_LIGHTS
	};

	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger runTrigger;
	dsp::SchmittTrigger trackResetTriggers[8];
	dsp::SchmittTrigger trackActiveTriggers[8];
	dsp::SchmittTrigger fillTrigger;

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
	int clockMaxCount = 0;
	bool noteIncoming = false;
	float currentIncomingVO = -100.0f;
	bool globalReset = false;
	bool clockTrigged = false;
	bool copyPageArmed = false;
	bool copyTrigArmed = false;
	bool copyTrackArmed = false;
	bool copyPatternArmed = false;

	int prevTrig[8] = {0};
	float prevVO[8] = {0.0f};

	quantizer::Quantizer quant;

	TrigAttibutes nTrigsAttibutes[8][8][64];
	TrackAttibutes nTracksAttibutes[8][8];
	float trigSlide[8][8][64] = {{{0.0f}}};
  bool trigSlideType[8][8][64] = {{{0}}};
	int trigTrim[8][8][64] = {{{0}}};
	int trigLength[8][8][64] = {{{0}}};
	int trigPulseDistance[8][8][64] = {{{0}}};
	float trigCV1[8][8][64] = {{{0.0f}}};
	float trigCV2[8][8][64] = {{{0.0f}}};
	int trackHead[8][8] = {{0}};
	int rootNote[8][8] = {{-1}};
	int scale[8][8] = {{0}};
	int quantizeCV1[8][8] = {{0}};
	bool slideMode[8][8] = {{0}};

	bool fills[8] = {0};
	bool forceTrigs[8] = {0};
	bool killTrigs[8] = {0};
	float trsp[8] = {0.0f};
	float dice[8] = {0.0f};
	int rotLeft[8] = {0};
	int rotRight[8] = {0};
	int rotLen[8] = {16,16,16,16,16,16,16,16};

	bool solo = false;

	float powTable[100][10000] = {{0.0f}};

  std::string labels[8] = {"Track 1","Track 2","Track 3","Track 4","Track 5","Track 6","Track 7","Track 8"};

	ENCORE() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);

		rightExpander.producerMessage = rightMessages[0];
		rightExpander.consumerMessage = rightMessages[1];

		configParam(FILL_PARAM, 0.0f, 1.0f, 0.0f);
    configParam(RUN_PARAM, 0.0f, 1.0f, 0.0f);

    configParam(TRIGPAGE_PARAM, 0.0f, 2.0f,  0.0f);
    configParam(TRIGPAGE_PARAM+1, 0.0f, 2.0f,  0.0f);
    configParam(TRIGPAGE_PARAM+2, 0.0f, 2.0f,  0.0f);
    configParam(TRIGPAGE_PARAM+3, 0.0f, 2.0f,  0.0f);

    configParam(PATTERN_PARAM, 0.0f, 7.0f, 0.0f);

		configParam(TRACKLENGTH_PARAM, 1.0f, 64.0f, 16.0f);
		configParam(TRACKSPEED_PARAM, 1.0f, 8.0f, 1.0f);
		configParam(TRACKREADMODE_PARAM, 0.0f, 4.0f, 0.0f);
		configParam(TRACKROOTNOTE_PARAM, -1.0f, quantizer::numNotes-2, -1.0f, "Root note","",0,1,1);
		configParam(TRACKSCALE_PARAM, 0.0f, quantizer::numScales-1, 1.0f, "Scale","",0,1,1);
		configParam(TRACKQUANTIZECV1_PARAM, 0.0f, 1.0f, 0.0f);

		configParam(TRIGCV1_PARAM, -10.0f, 10.0f, 0.0f);
		configParam(TRIGCV2_PARAM, -10.0f, 10.0f, 0.0f);
		configParam(TRIGLENGTH_PARAM, 1.0f, 2048.0f, 16.0f);
		configParam(TRIGPULSECOUNT_PARAM, 1.0f, 64.0f, 1.0f);
		configParam(TRIGPULSEDISTANCE_PARAM, 0.0f, 32.0f, 0.0f);
		configParam(TRIGTYPE_PARAM, 0.0f, 2.0f, 0.0f);
		configParam(TRIGTRIM_PARAM, -31.0f, 31.0f, 0.0f);
		configParam(TRIGSLIDE_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(TRIGPROBA_PARAM, 0.0f, 7.0f, 0.0f);
		configParam(TRIGPROBACOUNT_PARAM, 1.0f, 100.0f, 100.0f);
		configParam(TRIGPROBACOUNTRESET_PARAM, 1.0f, 100.0f, 1.0f);
    configParam(TRIGSLIDETYPE_PARAM, 0.0f, 1.0f, 0.0f);

		configParam(RECORD_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(QUANTIZE_PARAM, 0.0f, 1.0f, 1.0f);

  	for (int i=0;i<16;i++) {
      configParam(STEPS_PARAMS + i, 0.0f, 4.0f, 0.0f);
  	}

		for (int i=0;i<12;i++) {
      configParam(NOTE_PARAMS + i, 0.0f, 3.0f, 0.0f);
  	}

  	for (int i=0;i<8;i++) {
      configParam(TRACKSONOFF_PARAMS + i, 0.0f, 2.0f, 1.0f);
			configParam(TRACKSELECT_PARAMS + i, 0.0f, 1.0f, i == currentTrack ? 1.0f : 0.0f);
			for (int j=0;j<8;j++) {
				for (int k=0;k<64;k++) {
					nTrigsAttibutes[i][j][k].setTrigIndex(k);
				}
			}
  	}

		for(int i=0; i<100;i++) {
      for(int j=0; j<10000;j++) {
				powTable[i][j] = powf(j*0.0001f,i*0.01f);
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
    params[TRIGSLIDETYPE_PARAM].setValue(trigSlideType[currentPattern][currentTrack][currentTrig]);
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
	}

	void updateParamsToTrig() {
		trigLength[currentPattern][currentTrack][currentTrig] = params[TRIGLENGTH_PARAM].getValue();
    trigSlideType[currentPattern][currentTrack][currentTrig] = params[TRIGSLIDETYPE_PARAM].getValue();
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
		json_t *rootJ = BidooModule::dataToJson();
		json_object_set_new(rootJ, "currentPattern", json_integer(currentPattern));
		json_object_set_new(rootJ, "currentTrack", json_integer(currentTrack));
		json_object_set_new(rootJ, "currentTrig", json_integer(currentTrig));
		json_object_set_new(rootJ, "trigPage", json_integer(trigPage));
    for(size_t i=0; i<8; i++) {
      json_object_set_new(rootJ, ("label" + to_string(i)).c_str(), json_string(labels[i].c_str()));
    }
		for (size_t i = 0; i<8; i++) {
			json_t *patternJ = json_object();
			for (size_t j = 0; j < 8; j++) {
				json_t *trackJ = json_object();
				json_object_set_new(trackJ, "isActive", json_boolean(nTracksAttibutes[i][j].getTrackActive()));
				json_object_set_new(trackJ, "isSolo", json_boolean(nTracksAttibutes[i][j].getTrackSolo()));
				json_object_set_new(trackJ, "speed", json_real(nTracksAttibutes[i][j].getTrackSpeed()));
				json_object_set_new(trackJ, "readMode", json_integer(nTracksAttibutes[i][j].getTrackReadMode()));
				json_object_set_new(trackJ, "length", json_integer(nTracksAttibutes[i][j].getTrackLength()));
				json_object_set_new(trackJ, "rootNote", json_integer(rootNote[i][j]));
				json_object_set_new(trackJ, "scale", json_integer(scale[i][j]));
				json_object_set_new(trackJ, "quantizeCV1", json_integer(quantizeCV1[i][j]));
				json_object_set_new(trackJ, "slideMode", json_boolean(slideMode[i][j]));
				for (int k = 0; k < nTracksAttibutes[i][j].getTrackLength(); k++) {
					json_t *trigJ = json_object();
					json_object_set_new(trigJ, "isActive", json_boolean(nTrigsAttibutes[i][j][k].getTrigActive()));
					json_object_set_new(trigJ, "slide", json_real(trigSlide[i][j][k]));
					json_object_set_new(trigJ, "trigType", json_integer(nTrigsAttibutes[i][j][k].getTrigType()));
					json_object_set_new(trigJ, "index", json_integer(nTrigsAttibutes[i][j][k].getTrigIndex()));
					json_object_set_new(trigJ, "trim", json_integer(trigTrim[i][j][k]));
					json_object_set_new(trigJ, "length", json_integer(trigLength[i][j][k]));
					json_object_set_new(trigJ, "pulseCount", json_integer(nTrigsAttibutes[i][j][k].getTrigPulseCount()));
					json_object_set_new(trigJ, "pulseDistance", json_integer(trigPulseDistance[i][j][k]));
					json_object_set_new(trigJ, "proba", json_integer(nTrigsAttibutes[i][j][k].getTrigProba()));
					json_object_set_new(trigJ, "count", json_integer(nTrigsAttibutes[i][j][k].getTrigCount()));
					json_object_set_new(trigJ, "countReset", json_integer(nTrigsAttibutes[i][j][k].getTrigCountReset()));
					json_object_set_new(trigJ, "octave", json_integer(nTrigsAttibutes[i][j][k].getTrigOctave()-3));
					json_object_set_new(trigJ, "semitones", json_integer(nTrigsAttibutes[i][j][k].getTrigSemiTones()));
					json_object_set_new(trigJ, "CV1", json_real(trigCV1[i][j][k]));
					json_object_set_new(trigJ, "CV2", json_real(trigCV2[i][j][k]));
          json_object_set_new(trigJ, "trigSlideType", json_boolean(trigSlideType[i][j][k]));
					json_object_set_new(trackJ, ("trig" + to_string(k)).c_str() , trigJ);
				}
				json_object_set_new(patternJ, ("track" + to_string(j)).c_str() , trackJ);
			}
			json_object_set_new(rootJ, ("pattern" + to_string(i)).c_str(), patternJ);
		}
		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
    BidooModule::dataFromJson(rootJ);
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

    for(size_t i=0; i<8; i++) {
      json_t *labelJ=json_object_get(rootJ, ("label"+ to_string(i)).c_str());
      if (labelJ) {
        labels[i] = json_string_value(labelJ);
      }
    }

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
						json_t *rootNoteJ = json_object_get(trackJ, "rootNote");
						if (rootNoteJ)
							rootNote[i][j]=json_integer_value(rootNoteJ);
						json_t *scaleJ = json_object_get(trackJ, "scale");
						if (scaleJ)
							scale[i][j]=json_integer_value(scaleJ);
						json_t *quantizeCV1J = json_object_get(trackJ, "quantizeCV1");
						if (quantizeCV1J)
							quantizeCV1[i][j]=json_integer_value(quantizeCV1J);
						json_t *slideModeJ = json_object_get(trackJ, "slideMode");
						if (slideModeJ)
							slideMode[i][j]=json_boolean_value(slideModeJ);
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
								trigTrim[i][j][k] = json_integer_value(trimJ);
							json_t *lengthJ = json_object_get(trigJ, "length");
							if (lengthJ)
								trigLength[i][j][k] = json_integer_value(lengthJ);
							json_t *pulseCountJ = json_object_get(trigJ, "pulseCount");
							if (pulseCountJ)
								nTrigsAttibutes[i][j][k].setTrigPulseCount(json_integer_value(pulseCountJ));
							json_t *pulseDistanceJ = json_object_get(trigJ, "pulseDistance");
							if (pulseDistanceJ)
								trigPulseDistance[i][j][k] =  json_integer_value(pulseDistanceJ);
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
              json_t *trigSlideTypeJ = json_object_get(trigJ, "trigSlideType");
							if (trigSlideTypeJ)
								trigSlideType[i][j][k]=json_boolean_value(trigSlideTypeJ);
						}
					}
				}
			}
		}
		updateTrackToParams();
		updateTrigToParams();
	}

	void randomizeTrigNote(const int track, const int trig) {
		nTrigsAttibutes[currentPattern][track][trig].fullRandomize();
	}

	void randomizeTrigNotePlus(const int track, const int trig) {
		nTrigsAttibutes[currentPattern][track][trig].fullRandomize();
		trigSlide[currentPattern][track][trig]=random::uniform();
    trigSlideType[currentPattern][track][trig]=random::uniform()>0.5f?true:false;
		trigLength[currentPattern][track][trig]=random::uniform()*31.0f;
		trigPulseDistance[currentPattern][track][trig]=random::uniform()*31.0f;
	}

	void randomizeTrigProb(const int track, const int trig) {
		nTrigsAttibutes[currentPattern][track][trig].randomizeProbs();
	}

	void randomizeTrigCV1(const int track, const int trig) {
		trigCV1[currentPattern][track][trig]=random::uniform()*10.0f;
	}

	void randomizeTrigCV2(const int track, const int trig) {
		trigCV2[currentPattern][track][trig]=random::uniform()*10.0f;
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
		for (int i=0; i<64; i++) {
			randomizeTrigNote(track,i);
		}
	}

	void randomizeTrackTrigsNotesPlus(const int track) {
		for (int i=0; i<64; i++) {
			randomizeTrigNotePlus(track,i);
		}
	}

	void randomizeTrackTrigsProbs(const int track) {
		for (int i=0; i<64; i++) {
			randomizeTrigProb(track,i);
		}
	}

	void randomizeTrackTrigsCV1(const int track) {
		for (int i=0; i<64; i++) {
			randomizeTrigCV1(track,i);
		}
	}

	void randomizeTrackTrigsCV2(const int track) {
		for (int i=0; i<64; i++) {
			randomizeTrigCV2(track,i);
		}
	}

	void fullRandomizeTrack(const int track) {
		randomizeTrack(track);
		for (int i=0; i<64; i++) {
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
		for (int i=0; i<8; i++) {
			randomizeTrack(i);
		}
	}

	void randomizePatternNotes() {
		for (int i=0; i<8; i++) {
			randomizeTrackTrigsNotes(i);
		}
	}

	void randomizePatternNotesPlus() {
		for (int i=0; i<8; i++) {
			randomizeTrackTrigsNotesPlus(i);
		}
	}

	void randomizePatternNotesProbs() {
		for (int i=0; i<8; i++) {
			randomizeTrackTrigsProbs(i);
		}
	}

	void fullRandomizePattern() {
		for (int i=0; i<8; i++) {
			fullRandomizeTrack(i);
		}
	}

	void nTrackLeft(const int track, const size_t n, const int len = 0) {
		size_t tLen = len == 0 ? nTracksAttibutes[currentPattern][track].getTrackLength() : len;
		array_cycle_left(trigSlide[currentPattern][track], tLen , sizeof(float), n);
		array_cycle_left(trigTrim[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_left(trigLength[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_left(trigPulseDistance[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_left(trigCV1[currentPattern][track], (size_t) tLen, sizeof(float), n);
		array_cycle_left(trigCV2[currentPattern][track], (size_t) tLen, sizeof(float), n);
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
		array_cycle_right(trigTrim[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_right(trigLength[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_right(trigPulseDistance[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_right(trigCV1[currentPattern][track], tLen, sizeof(float), n);
		array_cycle_right(trigCV2[currentPattern][track], tLen, sizeof(float), n);
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
		for (int i = 0; i < 64; i++) {
			nTrigsAttibutes[currentPattern][track][i].up();
		}
	}

	void trackDown(const int track) {
		for (int i = 0; i < 64; i++) {
			nTrigsAttibutes[currentPattern][track][i].down();
		}
	}

	void trigUp(const int trig) {
		nTrigsAttibutes[currentPattern][currentTrack][trig].up();
	}

	void trigDown(const int trig) {
		nTrigsAttibutes[currentPattern][currentTrack][trig].down();
	}


	void onRandomize() override {
		randomizePattern();
		updateTrackToParams();
		updateTrigToParams();
	}

	void pasteTrack(const int fromPattern, const int fromTrack, const int toPattern, const int toTrack) {
		nTracksAttibutes[toPattern][toTrack].setMainAttributes(nTracksAttibutes[fromPattern][fromTrack].getMainAttributes());
		nTracksAttibutes[toPattern][toTrack].setRefAttributes(nTracksAttibutes[fromPattern][fromTrack].getRefAttributes());
		trackHead[toPattern][toTrack] = trackHead[fromPattern][fromTrack];
		rootNote[toPattern][toTrack] = rootNote[fromPattern][fromTrack];
		scale[toPattern][toTrack] = scale[fromPattern][fromTrack];
		quantizeCV1[toPattern][toTrack] = quantizeCV1[fromPattern][fromTrack];
		for (int i=0; i<64; i++) {
			pasteTrig(fromPattern,fromTrack,i,toPattern,toTrack,i);
		}
	}

	void pasteTrig(const int fromPattern, const int fromTrack, const int fromTrig, const int toPattern, const int toTrack, const int toTrig) {
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
    trigSlideType[toPattern][toTrack][toTrig] = trigSlideType[fromPattern][fromTrack][fromTrig];
	}

	void pastePattern() {
		for (int i=0; i<8; i++) {
			pasteTrack(copyPatternId,i,currentPattern,i);
			for (int j=0; j<64; j++) {
				pasteTrig(copyPatternId,i,j,currentPattern,i,j);
			}
		}
	}
	
	void pastePage(const int fromPage, const int toPage) {
		for(int i=0; i<16; i++) {
			pasteTrig(currentPattern,currentTrack,i+(fromPage*16),currentPattern,currentTrack,i+(toPage*16));
		}
	}

	void trigInit(const int pattern, const int track, const int trig) {
		nTrigsAttibutes[pattern][track][trig].init();
		trigSlide[pattern][track][trig] = 0.0f;
		trigTrim[pattern][track][trig] = 0;
		trigLength[pattern][track][trig] = 15;
		trigPulseDistance[pattern][track][trig] = 1;
		trigCV1[pattern][track][trig] = 0.0f;
		trigCV2[pattern][track][trig] = 0.0f;
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
		rootNote[pattern][track] = -1;
		scale[pattern][track] = 0;
		quantizeCV1[pattern][track] = 0;
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

	bool patternIsSoloed() {
		for (size_t i = 0; i < 8; i++) {
			if (nTracksAttibutes[currentPattern][i].getTrackSolo()) {
				return true;
			}
		}
		return false;
	}

	void trackSync(const int track, const int tHead) {
		trackHead[currentPattern][track] = tHead%nTracksAttibutes[currentPattern][track].getTrackLength();
	}

	float trackGetGate(const int track, const int tPT) {
		if (nTrigsAttibutes[currentPattern][track][tPT].getTrigActive() && !nTrigsAttibutes[currentPattern][track][tPT].getTrigSleeping()) {
			int rTP = trigGetRelativeTrackPosition(track, tPT);
			if (rTP >= 0) {
				if (rTP<trigLength[currentPattern][track][tPT]) {
					return 10.0f;
				}
				else {
					int cPulses = (trigPulseDistance[currentPattern][track][tPT] == 0) ? 0 : (rTP/trigPulseDistance[currentPattern][track][tPT]);
					return ((cPulses<nTrigsAttibutes[currentPattern][track][tPT].getTrigPulseCount())
					&& (rTP>=(cPulses*trigPulseDistance[currentPattern][track][tPT]))
					&& (rTP<=((cPulses*trigPulseDistance[currentPattern][track][tPT])+trigLength[currentPattern][track][tPT]))) ? 10.0f : 0.0f;
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

	bool trigGetIsRead(const int track, const int trig, const int trackPosition) {
		int tI = trigGetTrimedIndex(track,trig);
		return (trackPosition>=tI) && (trackPosition<=(tI+trigGetFullLength(track, trig)));
	}

	float trigGetTrimedIndex(const int track, const int trig) {
		return nTrigsAttibutes[currentPattern][track][trig].getTrigIndex()*32 + trigTrim[currentPattern][track][trig];
	}

	float trackGetVO(const int track, const int tPT, const bool quantize = false) {
		float vo = nTrigsAttibutes[currentPattern][track][tPT].getVO() + trsp[track];
		if (trigSlide[currentPattern][track][tPT] == 0) {
			return quantize ? std::get<0>(quant.closestVoltageInScale(vo, rootNote[currentPattern][track], scale[currentPattern][track])) : vo;
		}
		else
		{
			float fullLength = trigGetFullLength(track,tPT);
			float voQ = quantize ? std::get<0>(quant.closestVoltageInScale(vo, rootNote[currentPattern][track], scale[currentPattern][track])) : vo;
			if (fullLength > 0.0f) {
				if (slideMode[currentPattern][track]) {
          if (trigSlideType[currentPattern][track][tPT]) {
            float subPhase = clamp(trigGetRelativeTrackPosition(track, tPT),0.0f,32.0f)/32.0f;
  					return voQ - (1.0f - interpolateLinear(powTable[(int)(trigSlide[currentPattern][track][tPT]*99.0f)],9999.0f*subPhase)) * (voQ - prevVO[track]);
          }
          else
          {
            float subPhase = clamp(trigGetRelativeTrackPosition(track, tPT),0.0f,fullLength);
  					return voQ - (1.0f - interpolateLinear(powTable[(int)(trigSlide[currentPattern][track][tPT]*99.0f)],9999.0f*subPhase/fullLength)) * (voQ - prevVO[track]);
          }
				}
				else {
          if (trigSlideType[currentPattern][track][tPT]) {
            float subPhase = clamp(trigGetRelativeTrackPosition(track, tPT)/32.0f*(1.0f/max((int)abs(voQ - prevVO[track]),1)),0.0f,1.0f);
  					return voQ - (1.0f - interpolateLinear(powTable[(int)(trigSlide[currentPattern][track][tPT]*99.0f)],9999.0f*subPhase)) * (voQ - prevVO[track]);
          }
          else
          {
            float subPhase = clamp(trigGetRelativeTrackPosition(track, tPT)*(1.0f/max((int)abs(voQ - prevVO[track]),1)),0.0f,fullLength);
  					return voQ - (1.0f - interpolateLinear(powTable[(int)(trigSlide[currentPattern][track][tPT]*99.0f)],9999.0f*subPhase/fullLength)) * (voQ - prevVO[track]);
          }
				}
			}
			else {
				return voQ;
			}
		}
	}

	void trackSetCurrentTrig(const int track, const bool fill, const bool pNei, const bool force=false, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		int cI = nTracksAttibutes[currentPattern][track].getTrackCurrentTrig();
		if (((trackHead[currentPattern][track]/32) != cI) || force) {
			//nTracksAttibutes[currentPattern][track].setTrackPre((nTrigsAttibutes[currentPattern][track][cI].getTrigActive() && nTrigsAttibutes[currentPattern][track][cI].hasProbability()) ? !nTrigsAttibutes[currentPattern][track][cI].getTrigSleeping() : nTracksAttibutes[currentPattern][track].getTrackPre());
			nTrigsAttibutes[currentPattern][track][cI].setTrigInitialized(false);
			cI = trackHead[currentPattern][track]/32;
			nTracksAttibutes[currentPattern][track].setTrackCurrentTrig(cI);
			nTrigsAttibutes[currentPattern][track][cI].init(fill,nTracksAttibutes[currentPattern][track].getTrackPre(),pNei, forceTrig, killTrig, dice);
			nTracksAttibutes[currentPattern][track].setTrackPre((nTrigsAttibutes[currentPattern][track][cI].getTrigActive()
			&& nTrigsAttibutes[currentPattern][track][cI].hasProbability()) ? !nTrigsAttibutes[currentPattern][track][cI].getTrigSleeping() : nTracksAttibutes[currentPattern][track].getTrackPre());
			trackSetNextTrig(track);
			//nTrigsAttibutes[currentPattern][track][nTracksAttibutes[currentPattern][track].getTrackNextTrig()].init(fill,nTracksAttibutes[currentPattern][track].getTrackPre(), pNei, forceTrig, killTrig, dice);
		}

		int cPT = nTracksAttibutes[currentPattern][track].getTrackPlayedTrig();
		if (trigGetIsRead(track, cI, trackHead[currentPattern][track])) {
			if ((cI != cPT)	&& nTrigsAttibutes[currentPattern][track][cI].getTrigActive() && !nTrigsAttibutes[currentPattern][track][cI].getTrigSleeping()) {
				nTracksAttibutes[currentPattern][track].setTrackPrevTrig(cPT);
				nTracksAttibutes[currentPattern][track].setTrackPlayedTrig(cI);
			}
		}
		else {
			int cNT = nTracksAttibutes[currentPattern][track].getTrackNextTrig();
			if (trigGetIsRead(track, cNT, trackHead[currentPattern][track]) && (cNT != cPT)
			&& nTrigsAttibutes[currentPattern][track][cNT].getTrigActive()
			&& !nTrigsAttibutes[currentPattern][track][cNT].getTrigSleeping())
			{
				nTracksAttibutes[currentPattern][track].setTrackPrevTrig(cPT);
				nTracksAttibutes[currentPattern][track].setTrackPlayedTrig(cNT);
			}
		}
	}

	void trackReset(const int track, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		//nTracksAttibutes[currentPattern][track].setTrackPre(false);
		nTracksAttibutes[currentPattern][track].setTrackForward(true);

		if (nTracksAttibutes[currentPattern][track].getTrackReadMode() == 1)
		{
			nTracksAttibutes[currentPattern][track].setTrackForward(false);
			trackHead[currentPattern][track] = nTracksAttibutes[currentPattern][track].getTrackLength()*32;
			trackSetCurrentTrig(track, fill, pNei, true, forceTrig, killTrig, dice);
		}
		else
		{
			trackHead[currentPattern][track] = 0;
			trackSetCurrentTrig(track, fill, pNei, false, forceTrig, killTrig, dice);
		}
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
			case 3: nTracksAttibutes[currentPattern][track].setTrackNextTrig((int)(random::uniform()*(nTracksAttibutes[currentPattern][track].getTrackLength() - 1))); break;
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

	void trackMoveNextForward(const int track, const bool clock, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		nTracksAttibutes[currentPattern][track].setTrackForward(true);

    if (clock) {trackHead[currentPattern][track] += nTracksAttibutes[currentPattern][track].getTrackSpeed();}

		if (trackHead[currentPattern][track] >= nTracksAttibutes[currentPattern][track].getTrackLength()*32) {
			trackReset(track, fill, pNei, forceTrig, killTrig, dice);
			return;
		}

		trackSetCurrentTrig(track, fill, pNei, false, forceTrig, killTrig, dice);
	}

	void trackMoveNextBackward(const int track, const bool clock, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		nTracksAttibutes[currentPattern][track].setTrackForward(false);

    if (clock) {trackHead[currentPattern][track] -= nTracksAttibutes[currentPattern][track].getTrackSpeed();}

		if (trackHead[currentPattern][track] <= 0) {
			trackReset(track, fill, pNei, forceTrig, killTrig, dice);
			return;
		}

		trackSetCurrentTrig(track, fill, pNei, false, forceTrig, killTrig, dice);
	}

	void trackMoveNextPendulum(const int track, const bool clock, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		if (clock) {trackHead[currentPattern][track] = trackHead[currentPattern][track] + (nTracksAttibutes[currentPattern][track].getTrackForward() ? 1 : -1 ) * nTracksAttibutes[currentPattern][track].getTrackSpeed();}

		if (trackHead[currentPattern][track] >= nTracksAttibutes[currentPattern][track].getTrackLength()*32) {
			nTracksAttibutes[currentPattern][track].setTrackForward(false);
			trackHead[currentPattern][track] = (nTracksAttibutes[currentPattern][track].getTrackLength() == 1) ? 0 : (nTracksAttibutes[currentPattern][track].getTrackLength()-1)*32;
		}
		else if (trackHead[currentPattern][track] <= 0) {
			nTracksAttibutes[currentPattern][track].setTrackForward(true);
			trackHead[currentPattern][track] = 0;
		}

		trackSetCurrentTrig(track, fill, pNei, false, forceTrig, killTrig, dice);
	}

	void trackMoveNextRandom(const int track, const bool clock, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		if (clock) {trackHead[currentPattern][track] += nTracksAttibutes[currentPattern][track].getTrackSpeed();}

		if (trackHead[currentPattern][track] >= (nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()+1)*32) {
			trackHead[currentPattern][track] = nTracksAttibutes[currentPattern][track].getTrackNextTrig()*32;
			trackSetCurrentTrig(track, fill, pNei, true, forceTrig, killTrig, dice);
			return;
		}

		trackSetCurrentTrig(track, fill, pNei, false, forceTrig, killTrig, dice);
	}

	void trackMoveNextBrownian(const int track, const bool clock, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		nTracksAttibutes[currentPattern][track].setTrackForward(true);
		if (clock) {trackHead[currentPattern][track] += nTracksAttibutes[currentPattern][track].getTrackSpeed();}

		if (trackHead[currentPattern][track] >= (nTracksAttibutes[currentPattern][track].getTrackCurrentTrig()+1)*32) {
			trackHead[currentPattern][track] = nTracksAttibutes[currentPattern][track].getTrackNextTrig()*32;
			trackSetCurrentTrig(track, fill, pNei, true, forceTrig, killTrig, dice);
			return;
		}

		trackSetCurrentTrig(track, fill, pNei, false, forceTrig, killTrig);
	}

	void trackMoveNext(const int track, const bool clock, const bool fill, const bool pNei, const bool forceTrig = false, const bool killTrig = false, const float dice = 0.0f) {
		switch (nTracksAttibutes[currentPattern][track].getTrackReadMode()) {
			case 0: trackMoveNextForward(track, clock, fill, pNei, forceTrig, killTrig, dice); break;
			case 1: trackMoveNextBackward(track, clock, fill, pNei, forceTrig, killTrig, dice); break;
			case 2: trackMoveNextPendulum(track, clock, fill, pNei, forceTrig, killTrig, dice); break;
			case 3: trackMoveNextRandom(track, clock, fill, pNei, forceTrig, killTrig, dice); break;
			case 4: trackMoveNextBrownian(track, clock, fill, pNei, forceTrig, killTrig, dice); break;
		}
	}

};

void ENCORE::process(const ProcessArgs &args) {
	currentPattern = (int)clamp((inputs[PATTERN_INPUT].isConnected() ? rescale(clamp(inputs[PATTERN_INPUT].getVoltage(), 0.0f, 10.0f),0.0f,10.0f,0.0f,8.0f) : 0) + (int)params[PATTERN_PARAM].getValue(), 0.0f, 7.0f);

	if (rightExpander.module && rightExpander.module->model == modelENCOREExpander) {
		float *messagesFromExpander = (float*)rightExpander.consumerMessage;
		for (int i=0; i<8; i++) {
			fills[i]=messagesFromExpander[i];
			forceTrigs[i]=messagesFromExpander[i+8];
			killTrigs[i]=messagesFromExpander[i+16];
			trsp[i]=messagesFromExpander[i+24];
			dice[i]=messagesFromExpander[i+32];
			rotLeft[i]=messagesFromExpander[i+40];
			rotRight[i]=messagesFromExpander[i+48];
			rotLen[i]=messagesFromExpander[i+56];
		}

		float *messagesToExpander = (float*)(rightExpander.module->leftExpander.producerMessage);
		messagesToExpander[0] = currentPattern;
		rightExpander.module->leftExpander.messageFlipRequested = true;
	}

	if (inputs[FILL_INPUT].isConnected()) {
		if (((inputs[FILL_INPUT].getVoltage() > 0.0f) && !fill) || ((inputs[FILL_INPUT].getVoltage() == 0.0f) && fill)) fill=!fill;
	}
	if (fillTrigger.process(params[FILL_PARAM].getValue())) {
		fill = !fill;
	}
	lights[FILL_LIGHT].setBrightness(fill ? 1.0f : 0.0f);

	solo = patternIsSoloed();

	if (currentPattern != previousPattern) {
		for (size_t i = 0; i<8; i++) {
			trackSync(i, trackHead[previousPattern][i]);
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
			else if ((tCT >= (i*16)) && (tCT<(16*(i+1)-1))) {
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
			if (trackActiveTriggers[i].process(inputs[TRACKACTIVE_INPUTS+i].getVoltage())) {
				nTracksAttibutes[currentPattern][i].toggleTrackActive();
			}

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

	if (runTrigger.process(inputs[RUN_INPUT].getVoltage() + params[RUN_PARAM].getValue())) {
		run = !run;
		globalReset = true;
	}

	if (run) {
		if (resetTrigger.process(rescale(inputs[RESET_INPUT].getVoltage(), 0.1f, 2.f, 0.f, 1.f))) {
			globalReset = true;
		}
		else {
			globalReset = false;
		}

		if (clockTrigger.process(rescale(inputs[EXTCLOCK_INPUT].getVoltage(), 0.1f, 2.f, 0.f, 1.f))) {
			clockTrigged = true;
		}
		else {
			clockTrigged = false;
		}

		for (int i=0; i<8;i++) {
			if (trackResetTriggers[i].process(inputs[TRACKRESET_INPUTS+i].getVoltage())) {
				if (rotLeft[i]) {
					nTrackLeft(i,rotLeft[i], rotLen[i]);
					updateTrigToParams();
				}
				else if (rotRight[i]) {
					nTrackRight(i,rotRight[i], rotLen[i]);
					updateTrigToParams();
				}
				trackReset(i, fill || fills[i], i == 0 ? false : nTracksAttibutes[currentPattern][i-1].getTrackPre(), forceTrigs[i], killTrigs[i], dice[i]);
			}
      else if (!inputs[TRACKRESET_INPUTS+i].isConnected() && globalReset) {
				if (rotLeft[i]) {
					nTrackLeft(i,rotLeft[i], rotLen[i]);
					updateTrigToParams();
				}
				else if (rotRight[i]) {
					nTrackRight(i,rotRight[i], rotLen[i]);
					updateTrigToParams();
				}
				trackReset(i, fill || fills[i], i == 0 ? false : nTracksAttibutes[currentPattern][i-1].getTrackPre(), forceTrigs[i], killTrigs[i], dice[i]);
			}
			else {
				if (rotLeft[i]) {
					nTrackLeft(i,rotLeft[i], rotLen[i]);
					updateTrigToParams();
				}
				else if (rotRight[i]) {
					nTrackRight(i,rotRight[i], rotLen[i]);
					updateTrigToParams();
				}
				trackMoveNext(i, clockTrigged, fill || fills[i], i == 0 ? false : nTracksAttibutes[currentPattern][i-1].getTrackPre(), forceTrigs[i], killTrigs[i], dice[i]);
			}

			int tPT = nTracksAttibutes[currentPattern][i].getTrackPlayedTrig();

			if ((currentTrack == i) && (params[RECORD_PARAM].getValue() == 1.0f)) {
					if (inputs[GATE_INPUT].getVoltage()>0.0f) {
						if (!noteIncoming) {
							noteIncoming = true;
							nTrigsAttibutes[currentPattern][i][(long)trackHead[currentPattern][i]].setTrigActive(true);
							if (params[QUANTIZE_PARAM].getValue() == 0.0f) {
								trigTrim[currentPattern][i][(long)trackHead[currentPattern][i]] = trackHead[currentPattern][i] - (long)trackHead[currentPattern][i];
							} else {
								trigTrim[currentPattern][i][(long)trackHead[currentPattern][i]] = 0.0f;
							}
							currentIncomingVO = inputs[VO_INPUT].getVoltage();
							nTrigsAttibutes[currentPattern][i][(long)trackHead[currentPattern][i]].setTrigOctave((long)currentIncomingVO+3.0f);
							nTrigsAttibutes[currentPattern][i][(long)trackHead[currentPattern][i]].setTrigSemiTones((long)((currentIncomingVO-(long)currentIncomingVO)*12.f));
						}
						else if (currentIncomingVO != inputs[VO_INPUT].getVoltage()) {
							currentIncomingVO = inputs[VO_INPUT].getVoltage();
							if (trackHead[currentPattern][i]>nTrigsAttibutes[currentPattern][i][tPT].getTrigIndex()) {
								trigLength[currentPattern][i][tPT] = trackHead[currentPattern][i] - nTrigsAttibutes[currentPattern][i][tPT].getTrigIndex()-0.01f;
							}
							else {
								trigLength[currentPattern][i][tPT] = nTracksAttibutes[currentPattern][i].getTrackLength() - nTrigsAttibutes[currentPattern][i][tPT].getTrigIndex()-0.01f;
							}
							nTrigsAttibutes[currentPattern][i][(long)trackHead[currentPattern][i]].setTrigActive(true);
							if (params[QUANTIZE_PARAM].getValue() == 0.0f) {
								trigTrim[currentPattern][i][(long)trackHead[currentPattern][i]] = trackHead[currentPattern][i] - (long)trackHead[currentPattern][i];
							} else {
								trigTrim[currentPattern][i][(long)trackHead[currentPattern][i]] = 0.0f;
							}
							nTrigsAttibutes[currentPattern][i][(long)trackHead[currentPattern][i]].setTrigOctave((long)currentIncomingVO+3.0f);
							nTrigsAttibutes[currentPattern][i][(long)trackHead[currentPattern][i]].setTrigSemiTones((long)((currentIncomingVO-(long)currentIncomingVO)*12.f));
						}
					} else {
						if (noteIncoming) {
							noteIncoming = false;
							if (trackHead[currentPattern][i]>nTrigsAttibutes[currentPattern][i][tPT].getTrigIndex()) {
								trigLength[currentPattern][i][tPT] = trackHead[currentPattern][i] - nTrigsAttibutes[currentPattern][i][tPT].getTrigIndex();
							}
							else {
								trigLength[currentPattern][i][tPT] = nTracksAttibutes[currentPattern][i].getTrackLength() - nTrigsAttibutes[currentPattern][i][tPT].getTrigIndex()-0.01f;
							}
							currentIncomingVO = -100.0f;
						}
					}
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

			bool q = rootNote[currentPattern][i]>=0 && scale[currentPattern][i]>0;
			outputs[VO_OUTPUTS + i].setVoltage(trackGetVO(i, tPT, q));
			outputs[CV1_OUTPUTS + i].setVoltage(outputs[GATE_OUTPUTS + i].getVoltage() == 0.0f ? 0.0f : ((quantizeCV1[currentPattern][i]>0 && q) ? std::get<0>(quant.closestVoltageInScale(trigCV1[currentPattern][i][tPT]-4.0f, rootNote[currentPattern][i], scale[currentPattern][i])) : trigCV1[currentPattern][i][tPT]));
			outputs[CV2_OUTPUTS + i].setVoltage(outputs[GATE_OUTPUTS + i].getVoltage() == 0.0f ? 0.0f : trigCV2[currentPattern][i][tPT]);
		}
	}
	else {
		for (int i=0; i<8;i++) {
			outputs[GATE_OUTPUTS + i].setVoltage(0.0f);
		}
	}

}

struct EncorerecordBtn : SvgSwitch {
	ENCORE *module;

	EncorerecordBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/ledgrey.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/ledred.svg")));
		sw->wrap();
		shadow->opacity = 0.0f;
	}

};

struct EncorequantizeBtn : SvgSwitch {
	ENCORE *module;

	EncorequantizeBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/ledgrey.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/ledblue.svg")));
		sw->wrap();
		shadow->opacity = 0.0f;
	}

};

struct EncoreoctaveBtn : SmallLEDLightBezel<RedGreenBlueLight> {

	EncoreoctaveBtn() {

	}

	void onButton(const event::Button &e) override {
		ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			for (int i = 0; i < 7; i++) {
				if (i != getParamQuantity()->paramId - ENCORE::OCTAVE_PARAMS) {
					mod->params[ENCORE::OCTAVE_PARAMS+i].setValue(0.0f);
				}
				else {
					mod->nTrigsAttibutes[mod->currentPattern][mod->currentTrack][mod->currentTrig].setTrigOctave(i);
				}
			}
			e.consume(this);
			return;
		}
		SmallLEDLightBezel<RedGreenBlueLight>::onButton(e);
	}
};

struct EncoretrigPageBtn : SmallLEDLightBezel<RedGreenBlueLight> {

	EncoretrigPageBtn() {

	}

	void onButton(const event::Button &e) override {
		ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			mod->trigPage = getParamQuantity()->paramId - ENCORE::TRIGPAGE_PARAM;
			if (mod->currentTrig>48) mod->currentTrig = mod->currentTrig - 48;
			if (mod->currentTrig>32) mod->currentTrig = mod->currentTrig - 32;
			if (mod->currentTrig>16) mod->currentTrig = mod->currentTrig - 16;
			mod->currentTrig = mod->trigPage*16 + mod->currentTrig;
			mod->updateTrigToParams();
			e.consume(this);
		}
		SmallLEDLightBezel<RedGreenBlueLight>::onButton(e);
	}
	
	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			if (e.key == GLFW_KEY_C) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->copyPageId = getParamQuantity()->paramId - ENCORE::TRIGPAGE_PARAM;
				mod->copyPatternArmed = false;
				mod->copyTrackArmed = false;
				mod->copyTrigArmed = false;
				mod->copyPageArmed = true;
			}

			if (e.key == GLFW_KEY_V) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
			  mod->pastePage(mod->copyPageId, getParamQuantity()->paramId - ENCORE::TRIGPAGE_PARAM);
			  mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_E) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->pageInit(getParamQuantity()->paramId - ENCORE::TRIGPAGE_PARAM);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_T) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizePageTrigsNotes(getParamQuantity()->paramId - ENCORE::TRIGPAGE_PARAM);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_Y) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizePageTrigsNotesPlus(getParamQuantity()->paramId - ENCORE::TRIGPAGE_PARAM);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_U) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizePageTrigsProbs(getParamQuantity()->paramId - ENCORE::TRIGPAGE_PARAM);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_F) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizePageTrigsCV1(getParamQuantity()->paramId - ENCORE::TRIGPAGE_PARAM);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_G) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizePageTrigsCV2(getParamQuantity()->paramId - ENCORE::TRIGPAGE_PARAM);
				mod->updateTrigToParams();
			}
		}
		SmallLEDLightBezel<RedGreenBlueLight>::onHoverKey(e);
	}
};

struct EncoretrackSelectBtn : SmallLEDLightBezel<RedGreenBlueLight> {

	EncoretrackSelectBtn() {

	}

	void onButton(const event::Button &e) override {
		ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			for (int i = 0; i < 8; i++) {
				if (i != (getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS)) {
					mod->params[ENCORE::TRACKSELECT_PARAMS+i].setValue(0.0f);
				}
				else {
					mod->params[ENCORE::TRACKSELECT_PARAMS+i].setValue(1.0f);
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
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			if (e.key == GLFW_KEY_C) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->copyTrackId = getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS;
				mod->copyPatternId = mod->currentPattern;
				mod->copyPatternArmed = false;
				mod->copyTrackArmed = true;
				mod->copyTrigArmed = false;
			}

			if (e.key == GLFW_KEY_V) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->pasteTrack(mod->copyPatternId,mod->copyTrackId,mod->currentPattern,getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_E) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->trackInit(mod->currentPattern,getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_R) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizeTrack(getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_T) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizeTrackTrigsNotes(getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_Y) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizeTrackTrigsNotesPlus(getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_U) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizeTrackTrigsProbs(getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_F) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizeTrackTrigsCV1(getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_G) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizeTrackTrigsCV2(getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_W) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->trackUp(getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_S) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->trackDown(getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_A) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->nTrackLeft(getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS,1);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_D) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->nTrackRight(getParamQuantity()->paramId - ENCORE::TRACKSELECT_PARAMS,1);
				mod->updateTrigToParams();
			}

		}
		SmallLEDLightBezel<RedGreenBlueLight>::onHoverKey(e);
	}
};

struct EncoretrackOnOffBtn : LEDLightBezel<RedGreenBlueLight> {

	EncoretrackOnOffBtn() {

	}

	void onButton(const event::Button &e) override {
		ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);

		if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == GLFW_MOD_SHIFT) {
			for (int i = 0; i < 8; i++) {
				if (i != (getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS)) {
					if (mod->params[ENCORE::TRACKSELECT_PARAMS+i].getValue() == 1.0f) {
						mod->params[ENCORE::TRACKSELECT_PARAMS+i].setValue(0.0f);
					}
				}
				else {
					mod->nTracksAttibutes[mod->currentPattern][i].toggleTrackSolo();
					mod->params[ENCORE::TRACKSONOFF_PARAMS+i].setValue(mod->nTracksAttibutes[mod->currentPattern][getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS].getTrackSolo() ? 2.0f : 0.0f);
					mod->params[ENCORE::TRACKSELECT_PARAMS+i].setValue(1.0f);
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
				mod->nTracksAttibutes[mod->currentPattern][getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS].toggleTrackActive();
				if (mod->nTracksAttibutes[mod->currentPattern][getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS].getTrackActive()) {
					mod->params[getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS].setValue(1.0f);
				} else {
					mod->params[getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS].setValue(0.0f);
				}
			}
			e.consume(this);
			return;
		}
		LEDLightBezel<RedGreenBlueLight>::onButton(e);
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			if (e.key == GLFW_KEY_C) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->copyTrackId = getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS;
				mod->copyPatternId = mod->currentPattern;
				mod->copyPatternArmed = false;
				mod->copyTrackArmed = true;
				mod->copyTrigArmed = false;
			}

			if (e.key == GLFW_KEY_V) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->pasteTrack(mod->copyPatternId,mod->copyTrackId,mod->currentPattern,getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_E) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->trackInit(mod->currentPattern,getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_R) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizeTrack(getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_T) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizeTrackTrigsNotes(getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_Y) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizeTrackTrigsNotesPlus(getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_U) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizeTrackTrigsProbs(getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_F) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizeTrackTrigsCV1(getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_G) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizeTrackTrigsCV2(getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS);
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_W) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->trackUp(getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_S) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->trackDown(getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_A) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->nTrackLeft(getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS,1);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_D) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->nTrackRight(getParamQuantity()->paramId - ENCORE::TRACKSONOFF_PARAMS,1);
				mod->updateTrigToParams();
			}

		}
		LEDLightBezel<RedGreenBlueLight>::onHoverKey(e);
	}

};

struct EncorenoteBtn : LEDLightBezel<RedGreenBlueLight> {

	EncorenoteBtn() {

	}

	void onButton(const event::Button &e) override {
		if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
			bool focused = mod->nTrigsAttibutes[mod->currentPattern][mod->currentTrack][mod->currentTrig].getTrigSemiTones() == getParamQuantity()->paramId - ENCORE::NOTE_PARAMS;
			if (focused) {
				mod->nTrigsAttibutes[mod->currentPattern][mod->currentTrack][mod->currentTrig].toggleTrigActive();
			}
			else {
				mod->nTrigsAttibutes[mod->currentPattern][mod->currentTrack][mod->currentTrig].setTrigSemiTones(getParamQuantity()->paramId - ENCORE::NOTE_PARAMS);
				mod->nTrigsAttibutes[mod->currentPattern][mod->currentTrack][mod->currentTrig].setTrigActive(true);
			}
			e.consume(this);
			return;
		}
		LEDLightBezel<RedGreenBlueLight>::onButton(e);
	}
};


struct EncorestepBtn : LEDLightBezel<RedGreenBlueLight> {

	EncorestepBtn() {

	}

	void onButton(const event::Button &e) override {
		if (getParamQuantity() && getParamQuantity()->module && e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT && (e.mods & RACK_MOD_MASK) == (GLFW_MOD_SHIFT)) {
			ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
			mod->nTrigsAttibutes[mod->currentPattern][mod->currentTrack][getParamQuantity()->paramId - ENCORE::STEPS_PARAMS + mod->trigPage*16].toggleTrigActive();
			mod->currentTrig = getParamQuantity()->paramId - ENCORE::STEPS_PARAMS + mod->trigPage*16;
			mod->updateTrigToParams();
		}
		else if (e.button == GLFW_MOUSE_BUTTON_LEFT && e.action == GLFW_PRESS) {
			ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
			mod->currentTrig = getParamQuantity()->paramId - ENCORE::STEPS_PARAMS + mod->trigPage*16;
			mod->updateTrigToParams();
		}

		LEDLightBezel<RedGreenBlueLight>::onButton(e);
	}

	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
			int trigId = getParamQuantity()->paramId - ENCORE::STEPS_PARAMS + mod->trigPage*16;

			if (e.key == GLFW_KEY_C) {
				mod->copyTrigId = trigId;
				mod->copyTrackId = mod->currentTrack;
				mod->copyPatternId = mod->currentPattern;
				mod->copyPatternArmed = false;
				mod->copyTrackArmed = false;
				mod->copyTrigArmed = true;
			}

			if (e.key == GLFW_KEY_V) {
				mod->pasteTrig(mod->copyPatternId, mod->copyTrackId, mod->copyTrigId, mod->currentPattern, mod->currentTrack, trigId);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_E) {
				mod->trigInit(mod->currentPattern,mod->currentTrack, trigId);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_R) {
				mod->randomizeTrigNote(mod->currentTrack, trigId);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_T) {
				mod->randomizeTrigNotePlus(mod->currentTrack, trigId);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_Y) {
				mod->randomizeTrigProb(mod->currentTrack, trigId);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_U) {
				mod->fullRandomizeTrig(mod->currentTrack, trigId);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_F) {
				mod->randomizeTrigCV1(mod->currentTrack, trigId);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_G) {
				mod->randomizeTrigCV2(mod->currentTrack, trigId);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_W) {
				mod->trigUp(trigId);
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_S) {
				mod->trigDown(trigId);
				mod->updateTrigToParams();
			}

		}
		LEDLightBezel<RedGreenBlueLight>::onHoverKey(e);
	}


};

struct EncorezouPatternBtn : BidooRoundBlackSnapKnob {

	void onHoverKey(const HoverKeyEvent& e) override {
		if (e.action == GLFW_PRESS || e.action == GLFW_REPEAT) {
			if (e.key == GLFW_KEY_C) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->copyPatternId = mod->currentPattern;
				mod->copyPatternArmed = true;
				mod->copyTrackArmed = false;
				mod->copyTrigArmed = false;
			}

			if (e.key == GLFW_KEY_V) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->pastePattern();
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_E) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				for (int i=0; i<8; i++) {
					mod->trackInit(mod->currentPattern,i);
				}
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_R) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->randomizePattern();
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}

			if (e.key == GLFW_KEY_T) {
				ENCORE *mod = static_cast<ENCORE*>(getParamQuantity()->module);
				mod->fullRandomizePattern();
				mod->updateTrackToParams();
				mod->updateTrigToParams();
			}
		}
		BidooRoundBlackSnapKnob::onHoverKey(e);
	}

};

struct ENCOREDisplay : TransparentWidget {
	ENCORE *module;

	ENCOREDisplay() {

	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			stringstream sPatternHeader, sSteps, sSpeed, sRead, sRootNote, sScale, sQuantizeCV1;
			stringstream sTrigHeader, sLen, sPuls, sDist, sType, sTrim, sSlide, sVO, sCV1, sCV2, sProb, sProbBase,sSlideType;
			nvgBeginPath(args.vg);
			nvgRect(args.vg, 0.0f, 0.0f, 225.0f, 37.0f);
			nvgRect(args.vg, 0.0f, 77.0f, 225.0f, 63.0f);
			nvgClosePath(args.vg);
			nvgLineCap(args.vg, NVG_MITER);
			nvgStrokeWidth(args.vg, 4.0f);
			nvgStrokeColor(args.vg, nvgRGBA(180, 180, 180, 255));
			nvgStroke(args.vg);
			nvgFillColor(args.vg, nvgRGBA(0, 0, 0, 255));
			nvgFill(args.vg);
			static const float portX0[6] = {16.0f, 51.0f, 84.0f, 116.0f, 159.0f, 210.0f};
			static const float portX1[6] = {20.0f, 57.0f, 94.0f, 131.0F, 168.0f, 205.0f};
			static const float portY0[8] = {11.0f, 22.0f, 32.0f, 89.0f, 100.0f, 110.0f, 122.0f, 134.0f};
			nvgFillColor(args.vg, YELLOW_BIDOO);
			nvgTextAlign(args.vg, NVG_ALIGN_CENTER);

			if (module) {
				sPatternHeader << "Pattern " + to_string(module->currentPattern + 1) + " : " + module->labels[module->currentTrack];
				sSteps << module->nTracksAttibutes[module->currentPattern][module->currentTrack].getTrackLength();
				sSpeed << fixed << setprecision(2) << module->nTracksAttibutes[module->currentPattern][module->currentTrack].getTrackSpeed();
				sRead << displayReadMode(module->nTracksAttibutes[module->currentPattern][module->currentTrack].getTrackReadMode());
				sRootNote << quantizer::rootNotes[module->rootNote[module->currentPattern][module->currentTrack]+1].label.c_str();
				sScale << quantizer::scales[module->scale[module->currentPattern][module->currentTrack]].label.c_str();
				sQuantizeCV1 << (module->quantizeCV1[module->currentPattern][module->currentTrack] == 0 ? "Free" : "Qnt.");

				sTrigHeader << "Trig " + to_string(module->currentTrig + 1);
				sLen << fixed << setprecision(2) << (float)module->trigLength[module->currentPattern][module->currentTrack][module->currentTrig];
				sPuls << to_string(module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigPulseCount()).c_str();
				sDist << fixed << setprecision(2) << (float)module->trigPulseDistance[module->currentPattern][module->currentTrack][module->currentTrig];
				sType << displayTrigType(module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigType()).c_str();
				sTrim << fixed << setprecision(2) << module->trigTrim[module->currentPattern][module->currentTrack][module->currentTrig];
				sSlide << fixed << setprecision(2) << module->trigSlide[module->currentPattern][module->currentTrack][module->currentTrig];
				//sVO << displayNote(module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigSemiTones(), module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigOctave());
				sCV1 << fixed << setprecision(2) << module->trigCV1[module->currentPattern][module->currentTrack][module->currentTrig];
				sCV2 << fixed << setprecision(2) << module->trigCV2[module->currentPattern][module->currentTrack][module->currentTrig];
				sProb << displayProba(module->nTrigsAttibutes[module->currentPattern][module->currentTrack][module->currentTrig].getTrigProba());
        sSlideType << (module->trigSlideType[module->currentPattern][module->currentTrack][module->currentTrig] ? "1" : "FULL");

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
				sRootNote << "None";
				sScale << "None";
				sQuantizeCV1 << "Free";
				sTrigHeader << "Trig " + to_string(1);
				sLen << fixed << setprecision(2) << 0.99f;
				sPuls << to_string(1).c_str();
				sDist << fixed << setprecision(2) << 1.0f;
				sType << "INT.";
				sTrim << 0;
				sSlide << fixed << setprecision(2) << 0.0f;
				//sVO << "C4";
				sCV1 << fixed << setprecision(2) << 0.0f;
				sCV2 << fixed << setprecision(2) << 0.0f;
				sProb << "DICE";
        sSlideType << "1";
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
			nvgText(args.vg, portX0[3], portY0[1], "Root", NULL);
			nvgText(args.vg, portX0[3], portY0[2], sRootNote.str().c_str(), NULL);
			nvgText(args.vg, portX0[4], portY0[1], "Scale", NULL);
			nvgText(args.vg, portX0[4], portY0[2], sScale.str().c_str(), NULL);
			nvgText(args.vg, portX0[5], portY0[1], "CV1", NULL);
			nvgText(args.vg, portX0[5], portY0[2], sQuantizeCV1.str().c_str(), NULL);

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
			// nvgFillColor(args.vg, nvgRGBA(255, 255, 255, 255));
			// nvgText(args.vg, portX1[5], portY0[6], "V/Oct", NULL);
			// nvgText(args.vg, portX1[5], portY0[7], sVO.str().c_str(), NULL);
      nvgText(args.vg, portX1[5], portY0[6], "Type", NULL);
			nvgText(args.vg, portX1[5], portY0[7], sSlideType.str().c_str(), NULL);
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
struct ENCORELight : BASE {
	ENCORELight() {
		this->box.size = mm2px(Vec(6.0f, 6.0f));
	}
};

struct EncoreBidooProbBlueKnob : BidooBlueSnapKnob {
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



struct ENCOREWidget : BidooWidget {
	ENCOREWidget(ENCORE *module) {
		setModule(module);
    prepareThemes(asset::plugin(pluginInstance, "res/ENCORE.svg"));

		addChild(createWidget<ScrewSilver>(Vec(15, 0)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
		addChild(createWidget<ScrewSilver>(Vec(15, 365)));
		addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

		{
			ENCOREDisplay *display = new ENCOREDisplay();
			display->module = module;
			display->box.pos = Vec(140.0f, 30.0f);
			display->box.size = Vec(200.0f, 225.0f);
			addChild(display);
		}

		static const float portY0[7] = {30.0f, 75.0f, 120.0f, 165.0f, 210.0f, 255.0f, 294.0f};

		addInput(createInput<PJ301MPort>(Vec(10.0f, portY0[0]), module, ENCORE::EXTCLOCK_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10.0f, portY0[1]), module, ENCORE::RESET_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10.0f, portY0[2]), module, ENCORE::G1_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10.0f, portY0[3]), module, ENCORE::G2_INPUT));
		addInput(createInput<PJ301MPort>(Vec(10.0f, portY0[4]), module, ENCORE::PATTERN_INPUT));
		addParam(createParam<EncorezouPatternBtn>(Vec(7.f,portY0[5]-13.f), module, ENCORE::PATTERN_PARAM));

		static const float portX0Controls[6] = {142.0f, 175.0f, 208.0f, 241.0f, 284.0f, 336.0f};
		static const float portX1Controls[6] = {147.0f, 184.0f, 221.0f, 258.0f, 295.0f, 332.0f};

		addParam(createParam<BidooBlueSnapKnob>(Vec(portX0Controls[0],portY0[1]-3), module, ENCORE::TRACKLENGTH_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(portX0Controls[1],portY0[1]-3), module, ENCORE::TRACKSPEED_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(portX0Controls[2],portY0[1]-3), module, ENCORE::TRACKREADMODE_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(portX0Controls[3],portY0[1]-3), module, ENCORE::TRACKROOTNOTE_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(portX0Controls[4],portY0[1]-3), module, ENCORE::TRACKSCALE_PARAM));
		addParam(createParam<CKSS>(Vec(portX0Controls[5]+6,portY0[1]+2), module, ENCORE::TRACKQUANTIZECV1_PARAM));

		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[0],portY0[3]+10), module, ENCORE::TRIGLENGTH_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[1],portY0[3]+10), module, ENCORE::TRIGPULSECOUNT_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[2],portY0[3]+10), module, ENCORE::TRIGPULSEDISTANCE_PARAM));
		addParam(createParam<BidooBlueSnapKnob>(Vec(portX1Controls[3],portY0[3]+10), module, ENCORE::TRIGTYPE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[4],portY0[3]+10), module, ENCORE::TRIGTRIM_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[5],portY0[3]+10), module, ENCORE::TRIGSLIDE_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[0],portY0[4]), module, ENCORE::TRIGCV1_PARAM));
		addParam(createParam<BidooBlueKnob>(Vec(portX1Controls[1],portY0[4]), module, ENCORE::TRIGCV2_PARAM));
    addParam(createParam<CKSS>(Vec(portX1Controls[5]+7,portY0[4]+5), module, ENCORE::TRIGSLIDETYPE_PARAM));

		ParamWidget *probCountKnob = createParam<BidooBlueKnob>(Vec(portX1Controls[3],portY0[4]), module, ENCORE::TRIGPROBACOUNT_PARAM);
		addParam(probCountKnob);
		ParamWidget *probCountResetKnob = createParam<BidooBlueKnob>(Vec(portX1Controls[4],portY0[4]), module, ENCORE::TRIGPROBACOUNTRESET_PARAM);
		addParam(probCountResetKnob);

		ParamWidget *probKnob = createParam<EncoreBidooProbBlueKnob>(Vec(portX1Controls[2],portY0[4]), module, ENCORE::TRIGPROBA_PARAM);
		EncoreBidooProbBlueKnob *tProbKnob = dynamic_cast<EncoreBidooProbBlueKnob*>(probKnob);
		tProbKnob->ref = probCountKnob;
		tProbKnob->refReset = probCountResetKnob;
		addParam(tProbKnob);

		addInput(createInput<PJ301MPort>(Vec(10.0f, portY0[6]+4.0f), module, ENCORE::FILL_INPUT));
		addParam(createParam<BlueCKD6>(Vec(40.0f, portY0[6]-1.0f+3.0f), module, ENCORE::FILL_PARAM));
		addChild(createLight<SmallLight<BlueLight>>(Vec(51.5f, portY0[6]-8.0f), module, ENCORE::FILL_LIGHT));

		addInput(createInput<PJ301MPort>(Vec(74.0f, portY0[6]+4.0f), module, ENCORE::RUN_INPUT));
    addParam(createParam<BlueCKD6>(Vec(104.0f, portY0[6]-1.0f+3.0f), module, ENCORE::RUN_PARAM));

		//static const float portX0[5] = {374.0f, 389.0f, 404.0f, 419.0f, 434.0f};

		for (size_t i=0;i<4;i++) {
			addParam(createLightParam<EncoretrigPageBtn>(Vec(380+19*i-1.5f, 317.0f-2.5f), module, ENCORE::TRIGPAGE_PARAM+i, ENCORE::TRIGPAGE_LIGHTS+3*i));
		}

		for (size_t i=0;i<8;i++){
			addInput(createInput<TinyPJ301MPort>(Vec(50.0f, 67.0f + i*28.0f), module, ENCORE::TRACKRESET_INPUTS + i));
			addInput(createInput<TinyPJ301MPort>(Vec(70.0f, 67.0f + i*28.0f), module, ENCORE::TRACKACTIVE_INPUTS + i));

			addParam(createLightParam<EncoretrackOnOffBtn>(Vec(90.0f , 64.0f + i*28.0f), module, ENCORE::TRACKSONOFF_PARAMS+i, ENCORE::TRACKSONOFF_LIGHTS+3*i));


			addParam(createLightParam<EncoretrackSelectBtn>(Vec(120.0f -2.5f, 72.0f -2.5f + i*28.0f), module, ENCORE::TRACKSELECT_PARAMS+i, ENCORE::TRACKSELECT_LIGHTS+3*i));

			addOutput(createOutput<TinyPJ301MPort>(Vec(375.0f, 65.0f + i * 20.0f), module, ENCORE::GATE_OUTPUTS + i));
			addOutput(createOutput<TinyPJ301MPort>(Vec(395.0f, 65.0f + i * 20.0f),  module, ENCORE::VO_OUTPUTS + i));
			addOutput(createOutput<TinyPJ301MPort>(Vec(415.0f, 65.0f + i * 20.0f),  module, ENCORE::CV1_OUTPUTS + i));
			addOutput(createOutput<TinyPJ301MPort>(Vec(435.0f, 65.0f + i * 20.0f),  module, ENCORE::CV2_OUTPUTS + i));
		}

		for (size_t i=0;i<16;i++) {
			addParam(createLightParam<EncorestepBtn>(Vec(12.0f+ 28.0f*i, 330.0f), module, ENCORE::STEPS_PARAMS + i, ENCORE::STEPS_LIGHTS + i*3));
		}

		float xKeyboardAnchor = 140.0f;
		float yKeyboardAnchor = 255.0f;

		for (size_t i=0;i<7;i++) {
			addParam(createLightParam<EncoreoctaveBtn>(Vec(xKeyboardAnchor + 39.5f  + i * 19.0f, yKeyboardAnchor +1.5f), module, ENCORE::OCTAVE_PARAMS + i, ENCORE::OCTAVE_LIGHTS + i*3));
		}

		addParam(createLightParam<EncorenoteBtn>(Vec(xKeyboardAnchor, yKeyboardAnchor+40.0f), module, ENCORE::NOTE_PARAMS, ENCORE::NOTE_LIGHTS));
		addParam(createLightParam<EncorenoteBtn>(Vec(xKeyboardAnchor+15.0f, yKeyboardAnchor+20.0f), module, ENCORE::NOTE_PARAMS+1, ENCORE::NOTE_LIGHTS+3));
		addParam(createLightParam<EncorenoteBtn>(Vec(xKeyboardAnchor+30.0f, yKeyboardAnchor+40.0f), module, ENCORE::NOTE_PARAMS+2, ENCORE::NOTE_LIGHTS+3*2));
		addParam(createLightParam<EncorenoteBtn>(Vec(xKeyboardAnchor+45.0f, yKeyboardAnchor+20.0f), module, ENCORE::NOTE_PARAMS+3, ENCORE::NOTE_LIGHTS+3*3));
		addParam(createLightParam<EncorenoteBtn>(Vec(xKeyboardAnchor+60.0f, yKeyboardAnchor+40.0f), module, ENCORE::NOTE_PARAMS+4, ENCORE::NOTE_LIGHTS+3*4));
		addParam(createLightParam<EncorenoteBtn>(Vec(xKeyboardAnchor+90.0f, yKeyboardAnchor+40.0f), module, ENCORE::NOTE_PARAMS+5, ENCORE::NOTE_LIGHTS+3*5));
		addParam(createLightParam<EncorenoteBtn>(Vec(xKeyboardAnchor+105.0f, yKeyboardAnchor+20.0f), module, ENCORE::NOTE_PARAMS+6, ENCORE::NOTE_LIGHTS+3*6));
		addParam(createLightParam<EncorenoteBtn>(Vec(xKeyboardAnchor+120.0f, yKeyboardAnchor+40.0f), module, ENCORE::NOTE_PARAMS+7, ENCORE::NOTE_LIGHTS+3*7));
		addParam(createLightParam<EncorenoteBtn>(Vec(xKeyboardAnchor+135.0f, yKeyboardAnchor+20.0f), module, ENCORE::NOTE_PARAMS+8, ENCORE::NOTE_LIGHTS+3*8));
		addParam(createLightParam<EncorenoteBtn>(Vec(xKeyboardAnchor+150.0f, yKeyboardAnchor+40.0f), module, ENCORE::NOTE_PARAMS+9, ENCORE::NOTE_LIGHTS+3*9));
		addParam(createLightParam<EncorenoteBtn>(Vec(xKeyboardAnchor+165.0f, yKeyboardAnchor+20.0f), module, ENCORE::NOTE_PARAMS+10, ENCORE::NOTE_LIGHTS+3*10));
		addParam(createLightParam<EncorenoteBtn>(Vec(xKeyboardAnchor+180.0f, yKeyboardAnchor+40.0f), module, ENCORE::NOTE_PARAMS+11, ENCORE::NOTE_LIGHTS+3*11));


		EncorerecordBtn* rBtn;
		addParam(rBtn = createParam<EncorerecordBtn>(Vec(297.0f, 358.0f), module, ENCORE::RECORD_PARAM));
		rBtn->module = module ? module : NULL;

		addInput(createInput<TinyPJ301MPort>(Vec(327.0f, 360.0f), module, ENCORE::GATE_INPUT));
		addInput(createInput<TinyPJ301MPort>(Vec(356.0f, 360.0f), module, ENCORE::VO_INPUT));

		EncorequantizeBtn* qBtn;
		addParam(qBtn = createParam<EncorequantizeBtn>(Vec(385.0f, 358.0f), module, ENCORE::QUANTIZE_PARAM));
		qBtn->module = module ? module : NULL;

	}

	struct EncoreInitPageItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->pageInit(module->trigPage);
			module->updateTrigToParams();
		}
	};

	struct EncoreCopyPageItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->copyPageId = module->trigPage;
			module->copyPatternArmed = false;
			module->copyTrackArmed = false;
			module->copyTrigArmed = false;
			module->copyPageArmed = true;
		}
	};

	struct EncorePastePageItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->pastePage(module->copyPageId, module->trigPage);
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizePageTrigsNotesItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizePageTrigsNotes(module->trigPage);
			module->updateTrigToParams();
		}
	};
	
	struct EncoreRandomizePageTrigsNotesPlusItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizePageTrigsNotesPlus(module->trigPage);
			module->updateTrigToParams();
		}
	};
	
	struct EncoreRandomizePageTrigsProbsItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizePageTrigsProbs(module->trigPage);
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizePageTrigsCV1Item : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizePageTrigsCV1(module->trigPage);
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizePageTrigsCV2Item : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizePageTrigsCV2(module->trigPage);
			module->updateTrigToParams();
		}
	};
	
	struct EncoreInitTrigItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->trigInit(module->currentPattern,module->currentTrack, module->currentTrig);
			module->updateTrigToParams();
		}
	};

	struct EncoreCopyTrigItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->copyTrigId = module->currentTrig;
			module->copyTrackId = module->currentTrack;
			module->copyPatternId = module->currentPattern;
			module->copyPatternArmed = false;
			module->copyTrackArmed = false;
			module->copyTrigArmed = true;
		}
	};

	struct EncorePasteTrigItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->pasteTrig(module->copyPatternId, module->copyTrackId, module->copyTrigId, module->currentPattern, module->currentTrack, module->currentTrig);
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizeTrigNoteItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizeTrigNote(module->currentTrack, module->currentTrig);
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizeTrigNotePlusItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizeTrigNotePlus(module->currentTrack, module->currentTrig);
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizeTrigProbItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizeTrigProb(module->currentTrack, module->currentTrig);
			module->updateTrigToParams();
		}
	};

	struct EncoreFullRandomizeTrigItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->fullRandomizeTrig(module->currentTrack, module->currentTrig);
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizeTrigCV1Item : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizeTrigCV1(module->currentTrack, module->currentTrig);
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizeTrigCV2Item : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizeTrigCV2(module->currentTrack, module->currentTrig);
			module->updateTrigToParams();
		}
	};

	struct EncoreTrigUpItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->trigUp(module->currentTrig);
			module->updateTrigToParams();
		}
	};

	struct EncoreTrigDownItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->trigDown(module->currentTrig);
			module->updateTrigToParams();
		}
	};

	struct EncoreCopyTrackItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->copyTrackId = module->currentTrack;
			module->copyPatternId = module->currentPattern;
			module->copyPatternArmed = false;
			module->copyTrackArmed = true;
			module->copyTrigArmed = false;
		}
	};

	struct EncorePasteTrackItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->pasteTrack(module->copyPatternId, module->copyTrackId, module->currentPattern, module->currentTrack);
			module->updateTrackToParams();
			module->updateTrigToParams();
		}
	};

	struct EncoreInitTrackItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->trackInit(module->currentPattern, module->currentTrack);
			module->updateTrackToParams();
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizeTrackItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizeTrack(module->currentTrack);
			module->updateTrackToParams();
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizeTrackTrigsNotesItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizeTrackTrigsNotes(module->currentTrack);
			module->updateTrackToParams();
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizeTrackTrigsNotesPlusItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizeTrackTrigsNotesPlus(module->currentTrack);
			module->updateTrackToParams();
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizeTrackTrigsProbsItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizeTrackTrigsProbs(module->currentTrack);
			module->updateTrackToParams();
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizeTrackTrigsCV1Item : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizeTrackTrigsCV1(module->currentTrack);
			module->updateTrackToParams();
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizeTrackTrigsCV2Item : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizeTrackTrigsCV2(module->currentTrack);
			module->updateTrackToParams();
			module->updateTrigToParams();
		}
	};

	struct EncoreTrackSlideModeItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->slideMode[module->currentPattern][module->currentTrack] = !module->slideMode[module->currentPattern][module->currentTrack];
		}
	};

	struct EncoreTrackUpItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->trackUp(module->currentTrack);
			module->updateTrigToParams();
		}
	};

	struct EncoreTrackDownItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->trackDown(module->currentTrack);
			module->updateTrigToParams();
		}
	};

	struct EncoreTrackLeftItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->nTrackLeft(module->currentTrack,1);
			module->updateTrigToParams();
		}
	};

	struct EncoreTrackRightItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->nTrackRight(module->currentTrack,1);
			module->updateTrigToParams();
		}
	};

	struct EncoreCopyPatternItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->copyPatternId = module->currentPattern;
			module->copyPatternArmed = true;
			module->copyTrackArmed = false;
			module->copyTrigArmed = false;
		}
	};

	struct EncorePastePatternItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->pastePattern();
			module->updateTrackToParams();
			module->updateTrigToParams();
		}
	};

	struct EncoreInitPatternItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			for (int i=0; i<8; i++) {
				module->trackInit(module->currentPattern,i);
			}
			module->updateTrackToParams();
			module->updateTrigToParams();
		}
	};

	struct EncoreRandomizePatternItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->randomizePattern();
			module->updateTrackToParams();
			module->updateTrigToParams();
		}
	};

	struct EncoreFullRandomizePatternItem : MenuItem {
		ENCORE *module;
		void onAction(const event::Action &e) override {
			module->fullRandomizePattern();
			module->updateTrackToParams();
			module->updateTrigToParams();
		}
	};

  struct labelTextField : TextField {

    ENCORE *module;

    labelTextField()
    {
      this->box.pos.x = 50;
      this->box.size.x = 160;
      this->multiline = false;
    }

    void onChange(const event::Change& e) override {
      module->labels[module->currentTrack] = text;
    };

  };


	void appendContextMenu(Menu *menu) override {
      BidooWidget::appendContextMenu(menu);
			ENCORE *module = dynamic_cast<ENCORE*>(this->module);
			assert(module);

			menu->addChild(new MenuSeparator());

			InstantiateExpanderItem *expItem = createMenuItem<InstantiateExpanderItem>("Add expander", "");
			expItem->module = module;
			expItem->model = modelENCOREExpander;
			expItem->posit = box.pos.plus(math::Vec(box.size.x,0));
			menu->addChild(expItem);

			menu->addChild(new MenuSeparator());

			menu->addChild(createSubmenuItem("Trig", "", [=](ui::Menu* menu) {
				menu->addChild(construct<EncoreInitTrigItem>(&MenuItem::text, "Erase (over+E)", &EncoreInitTrigItem::module, module));
				menu->addChild(construct<EncoreCopyTrigItem>(&MenuItem::text, "Copy (over+C)", &EncoreCopyTrigItem::module, module));
				menu->addChild(construct<EncorePasteTrigItem>(&MenuItem::text, "Paste (over+V)", &EncorePasteTrigItem::module, module));
				menu->addChild(construct<EncoreRandomizeTrigNoteItem>(&MenuItem::text, "Rand Note (over+R)", &EncoreRandomizeTrigNoteItem::module, module));
				menu->addChild(construct<EncoreRandomizeTrigNotePlusItem>(&MenuItem::text, "Rand Note+ (over+T)", &EncoreRandomizeTrigNotePlusItem::module, module));
				menu->addChild(construct<EncoreRandomizeTrigProbItem>(&MenuItem::text, "Rand Prob (over+U)", &EncoreRandomizeTrigProbItem::module, module));
				menu->addChild(construct<EncoreFullRandomizeTrigItem>(&MenuItem::text, "Full Rand (over+Y)", &EncoreFullRandomizeTrigItem::module, module));
				menu->addChild(construct<EncoreRandomizeTrigCV1Item>(&MenuItem::text, "Rand CV1 (over+F)", &EncoreRandomizeTrigCV1Item::module, module));
				menu->addChild(construct<EncoreRandomizeTrigCV2Item>(&MenuItem::text, "Rand CV2 (over+G)", &EncoreRandomizeTrigCV2Item::module, module));
				menu->addChild(construct<EncoreTrigUpItem>(&MenuItem::text, "Move Up (over+W)", &EncoreTrigUpItem::module, module));
				menu->addChild(construct<EncoreTrigDownItem>(&MenuItem::text, "Move Down (over+S)", &EncoreTrigDownItem::module, module));
			}));

			menu->addChild(createSubmenuItem("Track", "", [=](ui::Menu* menu) {
				menu->addChild(construct<EncoreTrackSlideModeItem>(&MenuItem::text, module->slideMode[module->currentPattern][module->currentTrack] ? "Slide time✓/rate const." : "Slide time/rate✓ const.", &EncoreTrackSlideModeItem::module, module));
				menu->addChild(construct<EncoreInitTrackItem>(&MenuItem::text, "Erase (over+E)", &EncoreInitTrackItem::module, module));
				menu->addChild(construct<EncoreCopyTrackItem>(&MenuItem::text, "Copy (over+C)", &EncoreCopyTrackItem::module, module));
				menu->addChild(construct<EncorePasteTrackItem>(&MenuItem::text, "Paste (over+V)", &EncorePasteTrackItem::module, module));
				menu->addChild(construct<EncoreRandomizeTrackItem>(&MenuItem::text, "Rand (over+R)", &EncoreRandomizeTrackItem::module, module));
				menu->addChild(construct<EncoreRandomizeTrackTrigsNotesItem>(&MenuItem::text, "Rand Notes (over+T)", &EncoreRandomizeTrackTrigsNotesItem::module, module));
				menu->addChild(construct<EncoreRandomizeTrackTrigsNotesPlusItem>(&MenuItem::text, "Rand Notes+ (over+U)", &EncoreRandomizeTrackTrigsNotesPlusItem::module, module));
				menu->addChild(construct<EncoreRandomizeTrackTrigsProbsItem>(&MenuItem::text, "Rand Probs (over+Y)", &EncoreRandomizeTrackTrigsProbsItem::module, module));
				menu->addChild(construct<EncoreRandomizeTrackTrigsCV1Item>(&MenuItem::text, "Rand CV1 (over+F)", &EncoreRandomizeTrackTrigsCV1Item::module, module));
				menu->addChild(construct<EncoreRandomizeTrackTrigsCV2Item>(&MenuItem::text, "Rand CV2 (over+G)", &EncoreRandomizeTrackTrigsCV2Item::module, module));
				menu->addChild(construct<EncoreTrackUpItem>(&MenuItem::text, "Move Up (over+W)", &EncoreTrackUpItem::module, module));
				menu->addChild(construct<EncoreTrackDownItem>(&MenuItem::text, "Move Down (over+S)", &EncoreTrackDownItem::module, module));
				menu->addChild(construct<EncoreTrackLeftItem>(&MenuItem::text, "Move Left (over+A)", &EncoreTrackLeftItem::module, module));
				menu->addChild(construct<EncoreTrackRightItem>(&MenuItem::text, "Move Right (over+D)", &EncoreTrackRightItem::module, module));

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
				menu->addChild(construct<EncoreInitPatternItem>(&MenuItem::text, "Erase (over+E)", &EncoreInitPatternItem::module, module));
				menu->addChild(construct<EncoreCopyPatternItem>(&MenuItem::text, "Copy (over+C)", &EncoreCopyPatternItem::module, module));
				menu->addChild(construct<EncorePastePatternItem>(&MenuItem::text, "Paste (over+V)", &EncorePastePatternItem::module, module));
				menu->addChild(construct<EncoreRandomizePatternItem>(&MenuItem::text, "Rand (over+R)", &EncoreRandomizePatternItem::module, module));
				menu->addChild(construct<EncoreFullRandomizePatternItem>(&MenuItem::text, "Full Rand (over+T)", &EncoreFullRandomizePatternItem::module, module));
			}));

			menu->addChild(createSubmenuItem("Page", "", [=](ui::Menu* menu) {
				menu->addChild(construct<EncoreInitPageItem>(&MenuItem::text, "Erase (over+E)", &EncoreInitPageItem::module, module));
				menu->addChild(construct<EncoreCopyPageItem>(&MenuItem::text, "Copy (over+C)", &EncoreCopyPageItem::module, module));
				menu->addChild(construct<EncorePastePageItem>(&MenuItem::text, "Paste (over+V)", &EncorePastePageItem::module, module));
				menu->addChild(construct<EncoreRandomizePageTrigsNotesItem>(&MenuItem::text, "Rand Notes (over+T)", &EncoreRandomizePageTrigsNotesItem::module, module));
				menu->addChild(construct<EncoreRandomizePageTrigsNotesPlusItem>(&MenuItem::text, "Rand Notes+ (over+U)", &EncoreRandomizePageTrigsNotesPlusItem::module, module));
				menu->addChild(construct<EncoreRandomizePageTrigsProbsItem>(&MenuItem::text, "Rand Probs (over+Y)", &EncoreRandomizePageTrigsProbsItem::module, module));
				menu->addChild(construct<EncoreRandomizePageTrigsCV1Item>(&MenuItem::text, "Rand CV1 (over+F)", &EncoreRandomizePageTrigsCV1Item::module, module));
				menu->addChild(construct<EncoreRandomizePageTrigsCV2Item>(&MenuItem::text, "Rand CV2 (over+G)", &EncoreRandomizePageTrigsCV2Item::module, module));
			}));
		
		}
};



Model *modelENCORE = createModel<ENCORE, ENCOREWidget>("ENCORE");
