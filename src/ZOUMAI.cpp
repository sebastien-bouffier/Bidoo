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

struct trig {

	trig() {

	}

	bool isActive = false;
	float slide = 0.0f;
	bool isSleeping = false;
	int trigType = 0;
	int index = 0;
	int reference = 0;
	int trim = 0;
	int length = 190;
	int pulseCount = 1;
	int pulseDistance = 1;
	int octave = 0;
	int semitones = 0;
	float CV1 = 0.0f;
	float CV2 = 0.0f;
	int proba = 0;
	int count = 100;
	int countReset = 1;
	int inCount = 0;

	float getGateValue(const float trackPosition);
	int getTrimedReference();
	float getRelativeTrackPosition(const float trackPosition);
	int getFullLength();
	float getVO();
	float getCV1();
	float getCV2();
	void init(const bool fill, const bool pre, const bool nei);
	void reset();
	void randomize();
	void fullRandomize();
	bool hasProbability();
	void copy(const trig *t);
	int getOctave();
	int getSemiTones();
	void up();
	void down();
};

inline void trig::copy(const trig *t) {
	isActive = t->isActive;
	slide = t->slide;
	trigType = t->trigType;
	trim = t->trim;
	length = t->length;
	pulseCount = t->pulseCount;
	pulseDistance = t->pulseDistance;
	octave = t->octave;
	semitones = t->semitones;
	CV1 = t->CV1;
	CV2 = t->CV2;
	proba = t->proba;
	count = t->count;
	countReset = t->countReset;
}

inline void trig::reset() {
	isActive = false;
	slide = 0.0f;
	trigType = 0;
	trim = 0;
	length = 80;
	pulseCount = 1;
	pulseDistance = 1;
	octave = 0;
	semitones = 0;
	CV1 = 0.0f;
	CV2 = 0.0f;
	proba = 0;
	count = 100;
	countReset = 1;
}

inline void trig::randomize() {
	isActive = random::uniform()>0.5f;
	slide = random::uniform()*10.0f;
	trim = (int)(random::uniform()*10) * (random::uniform()>0.5f ? -1 : 1);
	length = (int)(random::uniform()*190);
	pulseCount = (int)(random::uniform()*10);
	pulseDistance = (int)(random::uniform()*700);
	octave = random::uniform()*4.0f - 4.0f;
	semitones = random::uniform()*11.0f;
	CV1 = random::uniform()*10.0f;
	CV2 = random::uniform()*10.0f;
	proba = 0;
	count = 100;
	countReset = 1;
}

inline void trig::fullRandomize() {
	isActive = random::uniform()>0.5f;
	slide = random::uniform()*10.0f;
	trigType = (int)(random::uniform()*2);
	trim = (int)(random::uniform()*10) * (random::uniform()>0.5f ? -1 : 1);
	length = (int)(random::uniform()*190);
	pulseCount = (int)(random::uniform()*10);
	pulseDistance = (int)(random::uniform()*700);
	octave = random::uniform()*5.0f - 5.0f;
	semitones = random::uniform()*11.0f;
	CV1 = random::uniform()*10.0f;
	CV2 = random::uniform()*10.0f;
	proba = (int)(random::uniform()*7);
	count = (int)(random::uniform()*99)+1;
	countReset = (int)(random::uniform()*99)+1;
}

inline bool trig::hasProbability() {
	return (proba != 4) && (proba != 5) && !((proba == 0) && (count == 100));
}

inline void trig::init(const bool fill, const bool pre, const bool nei) {
	switch (proba) {
		case 0:  // dice
			if (count<100) {
				isSleeping = (random::uniform()*100)>=count;
			}
			else
				isSleeping = false;
			break;
		case 1:  // COUNT
			isSleeping = (inCount != count);
			inCount = (inCount >= countReset) ? 1 : (inCount + 1);
			break;
		case 2:  // FILL
			isSleeping = !fill;
			break;
		case 3:  // !FILL
			isSleeping = fill;
			break;
		case 4:  // PRE
			isSleeping = !pre;
			break;
		case 5:  // !PRE
			isSleeping = pre;
			break;
		case 6:  // NEI
			isSleeping = !nei;
			break;
		case 7:  // !NEI
			isSleeping = nei;
			break;
		default:
			isSleeping = false;
			break;
	}
}

inline float trig::getGateValue(const float trackPosition) {
	if (isActive && !isSleeping) {
		float rTTP = getRelativeTrackPosition(trackPosition);
		if (rTTP >= 0) {
			if (rTTP<length) {
				return 10.0f;
			}
			else {
				int cPulses = (pulseDistance == 0) ? 0 : (int)(rTTP/(float)pulseDistance);
				return ((cPulses<pulseCount) && (rTTP>=(cPulses*pulseDistance)) && (rTTP<=((cPulses*pulseDistance)+length))) ? 10.0f : 0.0f;
			}
		}
		else
			return 0.0f;
	}
	else
		return 0.0f;
}

inline float trig::getRelativeTrackPosition(const float trackPosition) {
	return trackPosition - getTrimedReference();
}

inline int trig::getFullLength() {
	return pulseCount == 1 ? length : ((pulseCount*pulseDistance) + length);
}

inline int trig::getTrimedReference() {
	return reference + trim;
}

inline float trig::getVO() {
	return (float)octave + (float)semitones/12.0f;
}

inline float trig::getCV1() {
	return CV1;
}

inline float trig::getCV2() {
	return CV2;
}

inline int trig::getOctave() {
	return octave;
}

inline int trig::getSemiTones() {
	return semitones;
}

inline void trig::up() {
	if (semitones==11) {
		octave++;
		semitones=0;
	}
	else {
		semitones++;
	}
}

inline void trig::down() {
	if (semitones==0) {
		octave--;
		semitones=11;
	}
	else {
		semitones--;
	}
}

struct track {
	trig trigs[64];
	bool isActive = true;
	int length = 16;
	int readMode = 0;
	float trackIndex = 0.0f;
	float speed = 1.0f;
	trig *prevTrig = trigs;
	trig *memTrig = trigs;
	trig *currentTrig = trigs;
	int nextIndex = 0;
	bool fwd = true;
	bool pre = false;

	track() {
		for(int i=0;i<64;i++) {
			trigs[i].index = i;
			trigs[i].reference = i*192;
		}
	}

	trig getCurrentTrig();
	trig getMemTrig();
	float getGate();
	float getVO();
	float getCV1();
	float getCV2();
	void reset(const bool fill, const bool pNei);
	void moveNext(const bool fill, const bool pNei);
	void moveNextForward(const bool fill, const bool pNei);
	void moveNextBackward(const bool fill, const bool pNei);
	void moveNextPendulum(const bool fill, const bool pNei);
	void moveNextRandom(const bool fill, const bool pNei);
	void moveNextBrownian(const bool fill, const bool pNei);
	int getNextIndex();
	void clear();
	void randomize();
	void fullRandomize();
	void copy(const track *t);
	void up();
	void down();
	void left();
	void right();
};

inline void track::copy(const track *t) {
	isActive = t->isActive;
	length = t->length;
	readMode = t->readMode;
	speed = t->speed;
	for (int i = 0; i < 64; i++) {
		trigs[i].copy(t->trigs + i);
	}
}

inline void track::clear() {
	isActive = true;
	length = 16;
	readMode = 0;
	speed = 1.0f;
	pre = false;
	for (int i = 0; i < 64; i++) {
		trigs[i].reset();
	}
}

inline void track::randomize() {
	for (int i = 0; i < 64; i++) {
		trigs[i].randomize();
	}
}

inline void track::fullRandomize() {
	isActive = random::uniform()>0.5f;
	length = (int)(random::uniform()*64.0f);
	readMode = (int)(random::uniform()*4.0f);
	speed = 0.5f + (int)(random::uniform()*3.0f) + (random::uniform()>0.5f ? 0.5f : 0.0f);
	for (int i = 0; i < 64; i++) {
		trigs[i].fullRandomize();
	}
}

inline trig track::getCurrentTrig() {
	return *currentTrig;
}

inline trig track::getMemTrig() {
	return *memTrig;
}

inline float track::getGate() {
	return memTrig->getGateValue(trackIndex);
}

inline float track::getVO() {
	if (memTrig->slide == 0.0f) {
		return memTrig->getVO();
	}
	else
	{
		float subPhase = memTrig->getRelativeTrackPosition(trackIndex);
		float fullLength = memTrig->getFullLength();
		if (fullLength > 0.0f) {
			subPhase = subPhase/fullLength;
		}
		else {
			subPhase = subPhase / 192.0f;
		}
		return memTrig->getVO() - (1.0f - powf(subPhase, memTrig->slide/11.0f)) * (memTrig->getVO() - prevTrig->getVO());
	}
}

inline float track::getCV1() {
	return memTrig->getCV1();
}

inline float track::getCV2() {
	return memTrig->getCV2();
}

inline void track::reset(const bool fill, const bool pNei) {
	pre = false;
	if (readMode == 1)
	{
		currentTrig = trigs+length-1;
		trackIndex = currentTrig->reference + speed;
	}
	else
	{
		fwd = true;
		currentTrig = trigs;
		trackIndex = speed;
	}
	currentTrig->init(fill,pre,pNei);
	if (currentTrig->isActive && !currentTrig->isSleeping) {
		prevTrig = memTrig;
		memTrig = currentTrig;
	}
	nextIndex = getNextIndex();
}

inline int track::getNextIndex() {
	switch (readMode) {
		case 0:
				return (currentTrig->index+1) > (length-1) ? 0 : (currentTrig->index+1);
		case 1:
				return ((int)currentTrig->index-1) < 0 ? (length-1) : (currentTrig->index-1);
		case 2:
			if(fwd) {
				if (currentTrig->index == (length-1)) {
					fwd=!fwd;
					return ((int)currentTrig->index-1) < 0 ? (length-1) : (currentTrig->index-1);
				}
				else
					return (currentTrig->index+1)%(length);
				}
			else {
				if (currentTrig->index == 0) {
					fwd=!fwd;
					return (currentTrig->index+1)%(length);
				}
				else
					return ((int)currentTrig->index-1) < 0 ? (length-1) : (currentTrig->index-1);
			}
		case 3: return (int)(random::uniform()*(length-1));
		case 4:
		{
			float dice = random::uniform();
			if (dice>=0.5f)
				return (currentTrig->index+1) > (length-1) ? 0 : (currentTrig->index+1);
			else if (dice<=0.25f)
				return currentTrig->index == 0 ? (length-1) : (currentTrig->index-1);
			else
				return currentTrig->index;
		}
		default : return 0;
	}
}

inline void track::moveNextForward(const bool fill, const bool pNei) {
	if ((length>1) && (trackIndex>192) && (currentTrig->index==0) && (nextIndex==0)) nextIndex = getNextIndex();
	if (trackIndex>(length*192)) trackIndex=0.0f;
	if (((nextIndex != 0) && (nextIndex<64) && (trigs[nextIndex].getRelativeTrackPosition(trackIndex) >= 0)) || (trackIndex == 0.f)) {
		if ((nextIndex != currentTrig->index) || (length == 1)) {
			pre = (currentTrig->isActive && currentTrig->hasProbability()) ? !currentTrig->isSleeping : pre;
			trigs[nextIndex].init(fill,pre,pNei);
			currentTrig = trigs + nextIndex;
			if (currentTrig->isActive && !currentTrig->isSleeping) {
				prevTrig = memTrig;
				memTrig = currentTrig;
			}
			nextIndex = getNextIndex();
		}
	}
}

inline void track::moveNextBackward(const bool fill, const bool pNei) {
	if (trackIndex>(currentTrig->reference+191)) {
		pre = (currentTrig->isActive && currentTrig->hasProbability()) ? !currentTrig->isSleeping : pre;
		trigs[nextIndex].init(fill,pre,pNei);
		currentTrig = trigs + nextIndex;
		if (currentTrig->isActive && !currentTrig->isSleeping) {
			prevTrig = memTrig;
			memTrig = currentTrig;
		}
		trackIndex = currentTrig->reference;
		nextIndex = getNextIndex();
	}
}

inline void track::moveNextPendulum(const bool fill, const bool pNei){
	if (fwd) {
		moveNextForward(fill, pNei);
		if (currentTrig->index == length-1) fwd = false;
	}
	else {
		moveNextBackward(fill, pNei);
		if (currentTrig->index == 0) fwd = true;
	}
}

inline void track::moveNextRandom(const bool fill, const bool pNei){
	moveNextBackward(fill, pNei);
}

inline void track::moveNextBrownian(const bool fill, const bool pNei){
	moveNextBackward(fill, pNei);
}

void track::moveNext(const bool fill, const bool pNei) {
		trackIndex += speed;
		switch (readMode) {
			case 0: moveNextForward(fill, pNei); break;
			case 1: moveNextBackward(fill, pNei); break;
			case 2: moveNextPendulum(fill, pNei); break;
			case 3: moveNextRandom(fill, pNei); break;
			case 4: moveNextBrownian(fill, pNei); break;
		}
}

inline void track::up() {
	for (int i = 0; i < 64; i++) {
		trigs[i].up();
	}
}

inline void track::down() {
	for (int i = 0; i < 64; i++) {
		trigs[i].down();
	}
}

inline void track::left() {
	trig temp = trigs[0];
	for (int i = 0; i < length-1; i++){
	  trigs[i] = trigs[i + 1];
	}
	trigs[length-1] = temp;
	for (int i = 0; i < length; i++) {
		trigs[i].index=i;
		trigs[i].reference = i*192;
	}
}

inline void track::right() {
	trig temp = trigs[length-1];
	for (int i = length-1; i>0; i--){
	  trigs[i] = trigs[i - 1];
	}
	trigs[0] = temp;
	for (int i = 0; i < length; i++) {
		trigs[i].index=i;
		trigs[i].reference = i*192;
	}
}

struct pattern {
	track tracks[8];

	pattern() {

	};

	void moveNext(const bool fill);
	void reset(const bool fill);
	void copy(const pattern *p);
	void clear();
	void randomize();
	void fullRandomize();
};

inline void pattern::copy(const pattern *p) {
	for (int i = 0; i < 8; i++) {
		tracks[i].copy(p->tracks + i);
	}
}

inline void pattern::moveNext(const bool fill) {
	for (int i = 0; i < 8; i++) {
		tracks[i].moveNext(fill, i==0?false:tracks[i-1].pre);
	}
}

inline void pattern::reset(const bool fill) {
	for (int i = 0; i < 8; i++) {
		tracks[i].reset(fill, i==0?false:tracks[i-1].pre);
	}
}

inline void pattern::clear() {
	for (int i = 0; i < 8; i++) {
		tracks[i].clear();
	}
}

inline void pattern::randomize() {
	for (int i = 0; i < 8; i++) {
		tracks[i].randomize();
	}
}

inline void pattern::fullRandomize() {
	for (int i = 0; i < 8; i++) {
		tracks[i].fullRandomize();
	}
}

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
		TRACKSELECT_LIGHTS = TRACKSONOFF_LIGHTS + 8,
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

	pattern patterns[8];
	float lastTickCount = 0.0f;
	float currentTickCount = 0.0f;
	float phase = 0.0f;
	int currentPattern = 0;
	int previousPattern = -1;
	int currentTrack = 0;
	int currentTrig = 0;
	int selector = 1;
	int trigPage = 0;
	int ppqn = 0;
	int pageIndex = 0;
	bool fill = false;
	int copyTrackId = -1;
	int copyPatternId = -1;
	int copyTrigId = -1;

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

		configParam(TRACKLENGTH_PARAM, 0.0f, 64.0f, 16.0f);
		configParam(TRACKSPEED_PARAM, 1.0f, 32.0f, 2.0f);
		configParam(TRACKREADMODE_PARAM, 0.0f, 4.0f, 0.0f);

		configParam(TRIGCV1_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(TRIGCV2_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(TRIGLENGTH_PARAM, 0.0f, 192.0f*64.0f, 99.0f);
		configParam(TRIGPULSECOUNT_PARAM, 0.0f, 192.0f, 1.0f);
		configParam(TRIGPULSEDISTANCE_PARAM, 0.0f, 768.0f, 1.0f);
		configParam(TRIGTYPE_PARAM, 0.0f, 2.0f, 0.0f);
		configParam(TRIGTRIM_PARAM, -191.0f, 191.0f, 0.0f);
		configParam(TRIGSLIDE_PARAM, 0.0f, 10.0f, 0.0f);
		configParam(TRIGPROBA_PARAM, 0.0f, 7.0f, 0.0f);
		configParam(TRIGPROBACOUNT_PARAM, 1.0f, 100.0f, 100.0f);
		configParam(TRIGPROBACOUNTRESET_PARAM, 1.0f, 100.0f, 1.0f);

  	for (int i=0;i<16;i++){
      configParam(STEPS_PARAMS + i, 0.0f, 1.0f, 0.0f);
  	}

  	for (int i=0;i<8;i++){
      configParam(TRACKSONOFF_PARAMS + i, 0.0f, 1.0f, 0.0f);
			configParam(TRACKSELECT_PARAMS + i, 0.0f, 1.0f, 0.0f);
  	}
	}

	json_t *dataToJson() override {
		json_t *rootJ = json_object();
		json_object_set_new(rootJ, "currentPattern", json_integer(currentPattern));
		json_object_set_new(rootJ, "currentTrack", json_integer(currentTrack));
		json_object_set_new(rootJ, "currentTrig", json_integer(currentTrig));
		json_object_set_new(rootJ, "trigPage", json_integer(trigPage));
		for (int i = 0; i<8; i++) {
			json_t *patternJ = json_object();
			for (int j = 0; j < 8; j++) {
				json_t *trackJ = json_object();
				json_object_set_new(trackJ, "isActive", json_boolean(patterns[i].tracks[j].isActive));
				json_object_set_new(trackJ, "length", json_integer(patterns[i].tracks[j].length));
				json_object_set_new(trackJ, "speed", json_real(patterns[i].tracks[j].speed));
				json_object_set_new(trackJ, "readMode", json_integer(patterns[i].tracks[j].readMode));
				json_object_set_new(trackJ, "trackIndex", json_integer(patterns[i].tracks[j].trackIndex));
				for (int k = 0; k < patterns[i].tracks[j].length; k++) {
					json_t *trigJ = json_object();
					json_object_set_new(trigJ, "isActive", json_boolean(patterns[i].tracks[j].trigs[k].isActive));
					json_object_set_new(trigJ, "slide", json_real(patterns[i].tracks[j].trigs[k].slide));
					json_object_set_new(trigJ, "trigType", json_integer(patterns[i].tracks[j].trigs[k].trigType));
					json_object_set_new(trigJ, "index", json_integer(patterns[i].tracks[j].trigs[k].index));
					json_object_set_new(trigJ, "reference", json_integer(patterns[i].tracks[j].trigs[k].reference));
					json_object_set_new(trigJ, "trim", json_integer(patterns[i].tracks[j].trigs[k].trim));
					json_object_set_new(trigJ, "length", json_integer(patterns[i].tracks[j].trigs[k].length));
					json_object_set_new(trigJ, "pulseCount", json_integer(patterns[i].tracks[j].trigs[k].pulseCount));
					json_object_set_new(trigJ, "pulseDistance", json_integer(patterns[i].tracks[j].trigs[k].pulseDistance));
					json_object_set_new(trigJ, "proba", json_integer(patterns[i].tracks[j].trigs[k].proba));
					json_object_set_new(trigJ, "count", json_integer(patterns[i].tracks[j].trigs[k].count));
					json_object_set_new(trigJ, "countReset", json_integer(patterns[i].tracks[j].trigs[k].countReset));
					json_object_set_new(trigJ, "octave", json_integer(patterns[i].tracks[j].trigs[k].octave));
					json_object_set_new(trigJ, "semitones", json_integer(patterns[i].tracks[j].trigs[k].semitones));
					json_object_set_new(trigJ, "CV1", json_real(patterns[i].tracks[j].trigs[k].CV1));
					json_object_set_new(trigJ, "CV2", json_real(patterns[i].tracks[j].trigs[k].CV2));
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

		for (int i=0; i<8;i++) {
			json_t *patternJ = json_object_get(rootJ, ("pattern" + to_string(i)).c_str());
			if (patternJ){
				for(int j=0; j<8;j++) {
					json_t *trackJ = json_object_get(patternJ, ("track" + to_string(j)).c_str());
					if (trackJ){
						json_t *isActiveJ = json_object_get(trackJ, "isActive");
						if (isActiveJ)
							patterns[i].tracks[j].isActive = json_is_true(isActiveJ) ? 1 : 0;
						json_t *lengthJ = json_object_get(trackJ, "length");
						if (lengthJ)
							patterns[i].tracks[j].length = json_integer_value(lengthJ);
						json_t *speedJ = json_object_get(trackJ, "speed");
						if (speedJ)
							patterns[i].tracks[j].speed = json_number_value(speedJ);
						json_t *readModeJ = json_object_get(trackJ, "readMode");
						if (readModeJ)
							patterns[i].tracks[j].readMode = json_integer_value(readModeJ);
						json_t *trackIndexJ = json_object_get(trackJ, "trackIndex");
						if (trackIndexJ)
							patterns[i].tracks[j].trackIndex = json_integer_value(trackIndexJ);
					}
					for(int k=0;k<patterns[i].tracks[j].length;k++) {
						json_t *trigJ = json_object_get(trackJ, ("trig" + to_string(k)).c_str());
						if (trigJ) {
							json_t *isActiveJ = json_object_get(trigJ, "isActive");
							if (isActiveJ)
								patterns[i].tracks[j].trigs[k].isActive = json_is_true(isActiveJ) ? 1 : 0;
							json_t *slideJ = json_object_get(trigJ, "slide");
							if (slideJ)
								patterns[i].tracks[j].trigs[k].slide = json_number_value(slideJ);
							json_t *trigTypeJ = json_object_get(trigJ, "trigType");
							if (trigTypeJ)
								patterns[i].tracks[j].trigs[k].trigType = json_integer_value(trigTypeJ);
							json_t *indexJ = json_object_get(trigJ, "index");
							if (indexJ)
								patterns[i].tracks[j].trigs[k].index = json_integer_value(indexJ);
							json_t *referenceJ = json_object_get(trigJ, "reference");
							if (referenceJ)
								patterns[i].tracks[j].trigs[k].reference = json_integer_value(referenceJ);
							json_t *trimJ = json_object_get(trigJ, "trim");
							if (trimJ)
								patterns[i].tracks[j].trigs[k].trim = json_integer_value(trimJ);
							json_t *lengthJ = json_object_get(trigJ, "length");
							if (lengthJ)
								patterns[i].tracks[j].trigs[k].length = json_integer_value(lengthJ);
							json_t *pulseCountJ = json_object_get(trigJ, "pulseCount");
							if (pulseCountJ)
								patterns[i].tracks[j].trigs[k].pulseCount = json_integer_value(pulseCountJ);
							json_t *pulseDistanceJ = json_object_get(trigJ, "pulseDistance");
							if (pulseDistanceJ)
								patterns[i].tracks[j].trigs[k].pulseDistance = json_integer_value(pulseDistanceJ);
							json_t *probaJ = json_object_get(trigJ, "proba");
							if (probaJ)
								patterns[i].tracks[j].trigs[k].proba = json_integer_value(probaJ);
							json_t *countJ = json_object_get(trigJ, "count");
							if (countJ)
								patterns[i].tracks[j].trigs[k].count = json_integer_value(countJ);
							json_t *countResetJ = json_object_get(trigJ, "countReset");
							if (countResetJ)
								patterns[i].tracks[j].trigs[k].countReset = json_integer_value(countResetJ);
							json_t *octaveJ = json_object_get(trigJ, "octave");
							if (octaveJ)
								patterns[i].tracks[j].trigs[k].octave = json_integer_value(octaveJ);
							json_t *semitonesJ = json_object_get(trigJ, "semitones");
							if (semitonesJ)
								patterns[i].tracks[j].trigs[k].semitones = json_integer_value(semitonesJ);
							json_t *CV1J = json_object_get(trigJ, "CV1");
							if (CV1J)
								patterns[i].tracks[j].trigs[k].CV1 = json_number_value(CV1J);
							json_t *CV2J = json_object_get(trigJ, "CV2");
							if (CV2J)
								patterns[i].tracks[j].trigs[k].CV2 = json_number_value(CV2J);
						}
					}
				}
			}
		}
		updateTrackToParams();
		updateTrigToParams();
	}

	void onRandomize() override {
		patterns[currentPattern].randomize();
		updateTrackToParams();
		updateTrigToParams();
	}

	void onReset() override {
		patterns[currentPattern].clear();
		updateTrackToParams();
		updateTrigToParams();
	}

	void process(const ProcessArgs &args) override;

	void updateTrackToParams() {
		params[TRACKLENGTH_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].length);
		params[TRACKSPEED_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].speed*2.0f);
		params[TRACKREADMODE_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].readMode);
	}

	void updateTrigToParams() {
		params[TRIGLENGTH_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].length);
		params[TRIGSLIDE_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].slide);
		params[TRIGTYPE_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].trigType);
		params[TRIGTRIM_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].trim);
		params[TRIGPULSECOUNT_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].pulseCount);
		params[TRIGPULSEDISTANCE_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].pulseDistance);
		params[TRIGCV1_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].CV1);
		params[TRIGCV2_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].CV2);
		params[TRIGPROBA_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].proba);
		params[TRIGPROBACOUNT_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].count);
		params[TRIGPROBACOUNTRESET_PARAM].setValue(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].countReset);
	}

	void updateTrigVO() {
		for (int i=0;i<7;i++) {
			if (octaveTriggers[i].process(params[OCTAVE_PARAMS+i].getValue())) {
				patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].octave = i-3;
			}
			lights[OCTAVE_LIGHT+i].setBrightness(i-3==patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].octave?1.0f:0.f);
		}

		for (int i=0;i<12;i++) {
			if (noteTriggers[i].process(params[NOTE_PARAMS+i].getValue())) {
				if (patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].semitones == i) {
					patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].isActive = !patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].isActive;
				}
				else {
					patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].semitones = i;
					patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].isActive = true;
				}
			}
			if ((i!=1)&&(i!=3)&&(i!=6)&&(i!=8)&&(i!=10)) {
				lights[NOTES_LIGHT+i*3].setBrightness(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].semitones == i?(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].isActive?0.0f:0.0f):1.0f);
				lights[NOTES_LIGHT+(i*3)+1].setBrightness(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].semitones == i?(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].isActive?1.0f:0.5f):1.0f);
				lights[NOTES_LIGHT+(i*3)+2].setBrightness(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].semitones == i?(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].isActive?0.0f:0.0f):1.0f);
			}
			else {
				lights[NOTES_LIGHT+i*3].setBrightness(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].semitones == i?(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].isActive?0.0f:0.0f):0.0f);
				lights[NOTES_LIGHT+(i*3)+1].setBrightness(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].semitones == i?(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].isActive?1.0f:0.5f):0.0f);
				lights[NOTES_LIGHT+(i*3)+2].setBrightness(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].semitones == i?(patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].isActive?0.0f:0.0f):0.0f);
			}
		}
	}

	void updateParamsToTrack() {
		patterns[currentPattern].tracks[currentTrack].length = params[TRACKLENGTH_PARAM].getValue();
		patterns[currentPattern].tracks[currentTrack].speed = params[TRACKSPEED_PARAM].getValue()*0.5f;
		patterns[currentPattern].tracks[currentTrack].readMode = params[TRACKREADMODE_PARAM].getValue();
	}

	void updateParamsToTrig() {
		patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].length = params[TRIGLENGTH_PARAM].getValue();
		patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].slide = params[TRIGSLIDE_PARAM].getValue();
		patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].trigType = params[TRIGTYPE_PARAM].getValue();
		patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].trim = params[TRIGTRIM_PARAM].getValue();
		patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].pulseCount = params[TRIGPULSECOUNT_PARAM].getValue();
		patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].pulseDistance = params[TRIGPULSEDISTANCE_PARAM].getValue();
		patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].CV1 = params[TRIGCV1_PARAM].getValue();
		patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].CV2 = params[TRIGCV2_PARAM].getValue();
		patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].proba = params[TRIGPROBA_PARAM].getValue();
		patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].count = params[TRIGPROBACOUNT_PARAM].getValue();
		patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].countReset = params[TRIGPROBACOUNTRESET_PARAM].getValue();
	}

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

		if (pastePatternTrigger.process(params[PASTEPATTERN_PARAM].getValue())) {
			patterns[currentPattern].copy(&patterns[copyPatternId]);
			lights[PASTEPATTERN_LIGHT].setBrightness(1.0f);
			updateTrackToParams();
			updateTrigToParams();
		}

		if (copyTrackTrigger.process(params[COPYTRACK_PARAM].getValue())) {
				copyTrackId = currentTrack;
				lights[COPYTRACK_LIGHT].setBrightness(1.0f);
		}

		if (pasteTrackTrigger.process(params[PASTETRACK_PARAM].getValue())) {
			patterns[currentPattern].tracks[currentTrack].copy(&patterns[currentPattern].tracks[copyTrackId]);
			lights[PASTETRACK_LIGHT].setBrightness(1.0f);
			updateTrackToParams();
			updateTrigToParams();
		}

		if (copyTrigTrigger.process(params[COPYTRIG_PARAM].getValue())) {
				copyTrigId = currentTrig;
				lights[COPYTRIG_LIGHT].setBrightness(1.0f);
		}

		if (pasteTrigTrigger.process(params[PASTETRIG_PARAM].getValue())) {
			patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].copy(&patterns[currentPattern].tracks[currentTrack].trigs[copyTrigId]);
			lights[PASTETRIG_LIGHT].setBrightness(1.0f);
			updateTrackToParams();
			updateTrigToParams();
		}

		if (clearPatternTrigger.process(params[CLEARPATTERN_PARAM].getValue())) {
				patterns[currentPattern].clear();
				lights[CLEARPATTERN_LIGHT].setBrightness(1.0f);
				updateTrackToParams();
				updateTrigToParams();
		}

		if (clearTrackTrigger.process(params[CLEARTRACK_PARAM].getValue())) {
				patterns[currentPattern].tracks[currentTrack].clear();
				lights[CLEARTRACK_LIGHT].setBrightness(1.0f);
				updateTrackToParams();
				updateTrigToParams();
		}

		if (clearTrigTrigger.process(params[CLEARTRIG_PARAM].getValue())) {
				patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].reset();
				lights[CLEARTRIG_LIGHT].setBrightness(1.0f);
				updateTrigToParams();
		}

		if (rndPatternTrigger.process(params[RNDPATTERN_PARAM].getValue())) {
				patterns[currentPattern].randomize();
				lights[RNDPATTERN_LIGHT].setBrightness(1.0f);
				updateTrackToParams();
				updateTrigToParams();
		}

		if (rndTrackTrigger.process(params[RNDTRACK_PARAM].getValue())) {
				patterns[currentPattern].tracks[currentTrack].randomize();
				lights[RNDTRACK_LIGHT].setBrightness(1.0f);
				updateTrackToParams();
				updateTrigToParams();
		}

		if (rndTrigTrigger.process(params[RNDTRIG_PARAM].getValue())) {
				patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].randomize();
				lights[RNDTRIG_LIGHT].setBrightness(1.0f);
				updateTrigToParams();
		}

		if (fRndPatternTrigger.process(params[FRNDPATTERN_PARAM].getValue())) {
				patterns[currentPattern].fullRandomize();
				lights[FRNDPATTERN_LIGHT].setBrightness(1.0f);
				updateTrackToParams();
				updateTrigToParams();
		}

		if (fRndTrackTrigger.process(params[FRNDTRACK_PARAM].getValue())) {
				patterns[currentPattern].tracks[currentTrack].fullRandomize();
				lights[FRNDTRACK_LIGHT].setBrightness(1.0f);
				updateTrackToParams();
				updateTrigToParams();
		}

		if (fRndTrigTrigger.process(params[FRNDTRIG_PARAM].getValue())) {
				patterns[currentPattern].tracks[currentTrack].trigs[currentTrig].fullRandomize();
				lights[FRNDTRIG_LIGHT].setBrightness(1.0f);
				updateTrackToParams();
				updateTrigToParams();
		}

		if (upTrackTrigger.process(params[UPTRACK_PARAM].getValue())) {
				patterns[currentPattern].tracks[currentTrack].up();
				lights[UPTRACK_LIGHT].setBrightness(1.0f);
				updateTrigToParams();
		}

		if (downTrackTrigger.process(params[DOWNTRACK_PARAM].getValue())) {
				patterns[currentPattern].tracks[currentTrack].down();
				lights[DOWNTRACK_LIGHT].setBrightness(1.0f);
				updateTrigToParams();
		}

		if (rightTrackTrigger.process(params[RIGHTTRACK_PARAM].getValue())) {
				patterns[currentPattern].tracks[currentTrack].right();
				lights[RIGHTTRACK_LIGHT].setBrightness(1.0f);
				updateTrigToParams();
		}

		if (leftTrackTrigger.process(params[LEFTTRACK_PARAM].getValue())) {
				patterns[currentPattern].tracks[currentTrack].left();
				lights[LEFTTRACK_LIGHT].setBrightness(1.0f);
				updateTrigToParams();
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

	for (int i = 0; i<8; i++) {
		if (trackResetTriggers[i].process(inputs[TRACKRESET_INPUTS+i].getVoltage())) {
			// for (int j = 0; j<8; j++) {
			// 	patterns[j].tracks[i].reset(fill,i==0?false:patterns[j].tracks[i-1].pre);
			// }
			patterns[currentPattern].tracks[i].reset(fill,i==0?false:patterns[currentPattern].tracks[i-1].pre);
		}

		if (trackActiveTriggers[i].process(inputs[TRACKACTIVE_INPUTS+i].getVoltage() + params[TRACKSONOFF_PARAMS+i].getValue())) {
			patterns[currentPattern].tracks[i].isActive = !patterns[currentPattern].tracks[i].isActive;
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

		if (patterns[currentPattern].tracks[i].isActive) {
			lights[TRACKSONOFF_LIGHTS+i].setBrightness(1.0f);
		}
		else {
			lights[TRACKSONOFF_LIGHTS+i].setBrightness(0.0f);
		}
	}

	for (int i = 0; i<4; i++) {
		if (trigPageTriggers[i].process(params[TRIGPAGE_PARAM + i].getValue())) {
			trigPage = i;
			currentTrig = i*16+currentTrig%16;
			updateTrigToParams();
		}
		if (trigPage == i) {
			lights[TRIGPAGE_LIGHTS+(i*3)].setBrightness(0.0f);
			lights[TRIGPAGE_LIGHTS+(i*3)+1].setBrightness(0.0f);
			lights[TRIGPAGE_LIGHTS+(i*3)+2].setBrightness(1.0f);
		}
		else if ((patterns[currentPattern].tracks[currentTrack].getCurrentTrig().index >= (i*16)) && (patterns[currentPattern].tracks[currentTrack].getCurrentTrig().index<(16*(i+1)-1))) {
			lights[TRIGPAGE_LIGHTS+(i*3)].setBrightness(1.0f*(1-phase));
			lights[TRIGPAGE_LIGHTS+(i*3)+1].setBrightness(0.5f*(1-phase));
			lights[TRIGPAGE_LIGHTS+(i*3)+2].setBrightness(0.0f*(1-phase));
		}
		else {
			lights[TRIGPAGE_LIGHTS+(i*3)].setBrightness(0.0f);
			lights[TRIGPAGE_LIGHTS+(i*3)+1].setBrightness(0.0f);
			lights[TRIGPAGE_LIGHTS+(i*3)+2].setBrightness(0.0f);
		}
	}

	for (int i = 0; i<16; i++) {
		if (stepsTriggers[i].process(params[STEPS_PARAMS+i].getValue())) {
			currentTrig = i + (trigPage * 16);
			updateTrigToParams();
		}

		int shiftedIndex = i + (trigPage*16);
		if (patterns[currentPattern].tracks[currentTrack].getCurrentTrig().index == shiftedIndex) {
			lights[STEPS_LIGHTS+(i*3)].setBrightness(1.0f);
			lights[STEPS_LIGHTS+(i*3)+1].setBrightness(0.5f);
			lights[STEPS_LIGHTS+(i*3)+2].setBrightness(0.0f);
		}
		else if (patterns[currentPattern].tracks[currentTrack].trigs[shiftedIndex].isActive) {
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

	if (currentPattern != previousPattern) {
		previousPattern = currentPattern;
		updateTrackToParams();
		updateTrigToParams();
		for (int i = 0; i<8; i++) {
			patterns[currentPattern].tracks[i].reset(fill, i==0?false:patterns[currentPattern].tracks[i-1].pre);
		}
	}
	else {
		updateTrigVO();
		updateParamsToTrack();
		updateParamsToTrig();
	}

	performTools();

	if (inputs[EXTCLOCK_INPUT].isConnected()) {
		currentTickCount++;
		if (lastTickCount > 0.0f) {
			phase = currentTickCount / lastTickCount;
		}
		else {
			phase += args.sampleTime;
		}
		if (clockTrigger.process(inputs[EXTCLOCK_INPUT].getVoltage())) {
			lastTickCount = currentTickCount;
			currentTickCount = 0.0f;
			phase = 0.0f;
			ppqn = 0;
		}
		if (resetTrigger.process(inputs[RESET_INPUT].getVoltage())) {
			phase = 0.0f;
			currentTickCount = 0.0f;
			// for (int i = 0; i<8; i++) {
			// 	for (int j = 0; j < 8; j++) {
			// 		if (!inputs[TRACKRESET_INPUTS+j].isConnected()) patterns[i].tracks[j].reset(fill, i==0?false:patterns[i].tracks[j-1].pre);
			// 	}
			// }
			for (int i = 0; i<8; i++) {
				if (!inputs[TRACKRESET_INPUTS+i].isConnected()) patterns[currentPattern].tracks[i].reset(fill, i==0?false:patterns[currentPattern].tracks[i-1].pre);
			}
			ppqn = 0;
		}
		else if (phase*192>=ppqn) {
			// for (int i = 0; i<8; i++) {
			// 	patterns[i].moveNext(fill);
			// }
			patterns[currentPattern].moveNext(fill);
			ppqn++;
		}
	}

	for (int i = 0; i<8; i++) {
		if (patterns[currentPattern].tracks[i].isActive){
			float gate = patterns[currentPattern].tracks[i].getGate();
			if (gate>0.0f) {
				if (patterns[currentPattern].tracks[i].getMemTrig().trigType == 0)
					outputs[GATE_OUTPUTS + i].setVoltage(gate);
				else if (patterns[currentPattern].tracks[i].getMemTrig().trigType == 1)
					outputs[GATE_OUTPUTS + i].setVoltage(inputs[G1_INPUT].getVoltage());
				else if (patterns[currentPattern].tracks[i].getMemTrig().trigType == 2)
					outputs[GATE_OUTPUTS + i].setVoltage(inputs[G2_INPUT].getVoltage());
				else
					outputs[GATE_OUTPUTS + i].setVoltage(0.0f);
			}
			else
				outputs[GATE_OUTPUTS + i].setVoltage(0.0f);
		}
		else
			outputs[GATE_OUTPUTS + i].setVoltage(0.0f);

		outputs[VO_OUTPUTS + i].setVoltage(patterns[currentPattern].tracks[i].getVO());
		outputs[CV1_OUTPUTS + i].setVoltage(patterns[currentPattern].tracks[i].getCV1());
		outputs[CV2_OUTPUTS + i].setVoltage(patterns[currentPattern].tracks[i].getCV2());
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
			sSteps << module->patterns[module->currentPattern].tracks[module->currentTrack].length;
			sSpeed << fixed << setprecision(2) << module->patterns[module->currentPattern].tracks[module->currentTrack].speed;
			sRead << displayReadMode(module->patterns[module->currentPattern].tracks[module->currentTrack].readMode);
			sTrigHeader << "Trig " + to_string(module->currentTrig + 1);
			sLen << fixed << setprecision(2) << (float)module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].length/192.0f;
			sPuls << to_string(module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].pulseCount).c_str();
			sDist << fixed << setprecision(2) << (float)module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].pulseDistance/192.0f;
			sType << displayTrigType(module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].trigType).c_str();
			sTrim << to_string(module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].trim).c_str();
			sSlide << fixed << setprecision(2) << module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].slide;
			sVO << displayNote(&module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig]);
			sCV1 << fixed << setprecision(2) << module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].CV1;
			sCV2 << fixed << setprecision(2) << module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].CV2;
			sProb << displayProba(module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].proba);
			nvgFontSize(args.vg, 10.0f);
			if (module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].proba < 2)
			{
				nvgText(args.vg, portX1[3], portY0[6], "Val.", NULL);
				nvgText(args.vg, portX1[3], portY0[7], to_string(module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].count).c_str(), NULL);
			}
			if (module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].proba == 1)
			{
				nvgText(args.vg, portX1[4], portY0[6], "Base", NULL);
				nvgText(args.vg, portX1[4], portY0[7], to_string(module->patterns[module->currentPattern].tracks[module->currentTrack].trigs[module->currentTrig].countReset).c_str(), NULL);
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
		switch(value){
			case 0: return "DICE";
			case 1: return "COUNT";
			case 2: return "FILL";
			case 3: return "!FILL";
			case 4: return "PRE";
			case 5: return "!PRE";
			case 6: return "NEI";
			case 7: return "!NEI";
			default: return "";
		}
	}

	std::string displayNote(trig *t) {
		std::string result = "";
		if (t->semitones == 0)
			return "C" + to_string(t->octave+4);
		else if (t->semitones == 1)
			return "C#" + to_string(t->octave+4);
		else if (t->semitones == 2)
			return "D" + to_string(t->octave+4);
		else if (t->semitones == 3)
			return "D#" + to_string(t->octave+4);
		else if (t->semitones == 4)
			return "E" + to_string(t->octave+4);
		else if (t->semitones == 5)
			return "F" + to_string(t->octave+4);
		else if (t->semitones == 6)
			return "F#" + to_string(t->octave+4);
		else if (t->semitones == 7)
			return "G" + to_string(t->octave+4);
		else if (t->semitones == 8)
			return "G#" + to_string(t->octave+4);
		else if (t->semitones == 9)
			return "A" + to_string(t->octave+4);
		else if (t->semitones == 10)
			return "A#" + to_string(t->octave+4);
		else
			return "B" + to_string(t->octave+4);
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
		if (ref && this->paramQuantity && (this->paramQuantity->getValue() == 0.0f)) {
			ref->visible = true;
			refReset->visible = false;
		}
		else if (ref && this->paramQuantity && (this->paramQuantity->getValue() == 1.0f)) {
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
		addParam(createParam<RoundBlackSnapKnob>(Vec(7.f,portY0[5]-13.f), module, ZOUMAI::PATTERN_PARAM));

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

		addParam(createParam<MiniLEDButton>(Vec(portX0[1], 317.0f), module, ZOUMAI::TRIGPAGE_PARAM));
		addParam(createParam<MiniLEDButton>(Vec(portX0[2], 317.0f), module, ZOUMAI::TRIGPAGE_PARAM+1));
		addParam(createParam<MiniLEDButton>(Vec(portX0[3], 317.0f), module, ZOUMAI::TRIGPAGE_PARAM+2));
		addParam(createParam<MiniLEDButton>(Vec(portX0[4], 317.0f), module, ZOUMAI::TRIGPAGE_PARAM+3));

		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(portX0[1], 317.0f), module, ZOUMAI::TRIGPAGE_LIGHTS));
		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(portX0[2], 317.0f), module, ZOUMAI::TRIGPAGE_LIGHTS+3));
		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(portX0[3], 317.0f), module, ZOUMAI::TRIGPAGE_LIGHTS+6));
		addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(portX0[4], 317.0f), module, ZOUMAI::TRIGPAGE_LIGHTS+9));

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

		for (int i=0;i<8;i++){
			addInput(createInput<TinyPJ301MPort>(Vec(50.0f, 67.0f + i*28.0f), module, ZOUMAI::TRACKRESET_INPUTS + i));
			addInput(createInput<TinyPJ301MPort>(Vec(70.0f, 67.0f + i*28.0f), module, ZOUMAI::TRACKACTIVE_INPUTS + i));
			addParam(createParam<LEDBezel>(Vec(90.0f , 64.0f + i*28.0f), module, ZOUMAI::TRACKSONOFF_PARAMS + i));
			addChild(createLight<ZOUMAILight<GreenLight>>(Vec(92.0f, 66.0f + i*28.0f), module, ZOUMAI::TRACKSONOFF_LIGHTS + i));

			addParam(createParam<MiniLEDButton>(Vec(120.0f , 72.0f + i*28.0f), module, ZOUMAI::TRACKSELECT_PARAMS + i));
			addChild(createLight<SmallLight<BlueLight>>(Vec(120.0f, 72.0f + i*28.0f), module, ZOUMAI::TRACKSELECT_LIGHTS + i));

			addOutput(createOutput<TinyPJ301MPort>(Vec(375.0f, 65.0f + i * 20.0f), module, ZOUMAI::GATE_OUTPUTS + i));
			addOutput(createOutput<TinyPJ301MPort>(Vec(395.0f, 65.0f + i * 20.0f),  module, ZOUMAI::VO_OUTPUTS + i));
			addOutput(createOutput<TinyPJ301MPort>(Vec(415.0f, 65.0f + i * 20.0f),  module, ZOUMAI::CV1_OUTPUTS + i));
			addOutput(createOutput<TinyPJ301MPort>(Vec(435.0f, 65.0f + i * 20.0f),  module, ZOUMAI::CV2_OUTPUTS + i));
		}

		for (int i=0;i<16;i++){
			addParam(createParam<LEDBezel>(Vec(12.0f+ 28.0f*i, 330.0f), module, ZOUMAI::STEPS_PARAMS + i));
			addChild(createLight<ZOUMAILight<RedGreenBlueLight>>(Vec(14.0f+ 28.0f*i, 332.0f), module, ZOUMAI::STEPS_LIGHTS + i*3));
		}

		float xKeyboardAnchor = 140.0f;
		float yKeyboardAnchor = 255.0f;

		for (int i=0;i<7;i++) {
			addParam(createParam<MiniLEDButton>(Vec(xKeyboardAnchor + 55.0f + i * 13.0f, yKeyboardAnchor+5.0f), module, ZOUMAI::OCTAVE_PARAMS+i));
			addChild(createLight<SmallLight<BlueLight>>(Vec(xKeyboardAnchor + 55.0f  + i * 13.0f, yKeyboardAnchor+5.0f), module, ZOUMAI::OCTAVE_LIGHT+i));
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
