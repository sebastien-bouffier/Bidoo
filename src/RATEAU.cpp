#include "plugin.hpp"
#include "BidooComponents.hpp"
#include "dsp/digital.hpp"
#include <sstream>
#include <iomanip>
#include "dep/quantizer.hpp"
#include <random>

using namespace std;

enum pageType { pDiv, pStep, pProb, pSpeed, pClockShift, pTimeShift, pPulseWidth};
enum playMode { indepEditA, indepEditB, matrix};

struct RATEAU : BidooModule {
	enum ParamIds {
		DIV_PARAM,
		STEP_PARAM,
		PROB_PARAM,
		SPEED_PARAM,
		CLOCKSHIFT_PARAM,
		TIMESHIFT_PARAM,
		PULSEWIDTH_PARAM,
		BANK_PARAM,
		INDEPEDITA_PARAM,
		INDEPEDITB_PARAM,
		MATRIX_PARAM,
		STARTSTOP_PARAM,
		MUTE_PARAMS,
		CONTROLS_PARAMS = MUTE_PARAMS + 8,
		NUM_PARAMS = CONTROLS_PARAMS + 16
	};
	enum InputIds {
		CLOCK_INPUT,
		RESET_INPUT,
		BANK_INPUT,
		MODE_INPUT,
		STARTSTOP_INPUT,
		NUM_INPUTS
	};
	enum OutputIds {
		GATE_OUTPUTS,
		NUM_OUTPUTS = GATE_OUTPUTS + 8
	};
	enum LightIds {
		INDEPEDITA_LIGHT,
		INDEPEDITB_LIGHT,
		MATRIX_LIGHT,
		MUTE_LIGHTS,
		NUM_LIGHTS = MUTE_LIGHTS + 8*3
	};

	int page = 0;
	int bank = 0;
	int fCntrl = 0;
	int pMode = matrix;
	int previousMode = matrix;
	bool clockT = false;
	bool resetT = false;
	bool start = true;
	bool run = false;
	dsp::SchmittTrigger divTrigger;
	dsp::SchmittTrigger stepTrigger;
	dsp::SchmittTrigger probTrigger;
	dsp::SchmittTrigger speedTrigger;
	dsp::SchmittTrigger clockshiftTrigger;
	dsp::SchmittTrigger timeshiftTrigger;
	dsp::SchmittTrigger pulsewidthTrigger;
	dsp::SchmittTrigger clockTrigger;
	dsp::SchmittTrigger resetTrigger;
	dsp::SchmittTrigger indepEditATrigger;
	dsp::SchmittTrigger indepEditBTrigger;
	dsp::SchmittTrigger matrixTrigger;
	dsp::SchmittTrigger modeInputTrigger;
	dsp::SchmittTrigger startStopTrigger;
	dsp::SchmittTrigger muteTriggers[8];
	int gDivs[16][16][3]={{{0}}};
	int gSteps[16][16][3]={{{0}}};
	float gProbs[16][16][3]={{{1.0f}}};
	int gSpeeds[16][16][3]={{{2}}};
	int gClockShifts[16][16][3]={{{0}}};
	float gTimeShifts[16][16][3]={{{0.0f}}};
	float gPulseWidths[16][16][3]={{{0.25f}}};
	int patternModes[16]={matrix};

	dsp::PulseGenerator gatePulses[16][3];
	float delays[16][3]={{0.0f}};
	bool gates[16][3]={{false}};
	bool opened[16][3]={{true}};
	bool mutes[8]={false};
	float tickAccumulator = 1.0f;
	float currentTickCount = 1.0f;

	float speeds[16]={0.25f,0.5f,1.0f,2.0f,3.0f,4.0f,5.0f,6.0f,7.0f,8.0f,9.0f,10.0f,11.0f,12.0f,14.0f,16.0f};

	RATEAU() {
    config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
		configParam(BANK_PARAM, 0.0f, 15.0f, 0.0f);
		configParam(DIV_PARAM, 0.0f, 1.0f, 1.0f);
		configParam(STEP_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(PROB_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(SPEED_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(CLOCKSHIFT_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(TIMESHIFT_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(PULSEWIDTH_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(INDEPEDITA_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(INDEPEDITB_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(MATRIX_PARAM, 0.0f, 1.0f, 0.0f);
		configParam(STARTSTOP_PARAM, 0.0f, 1.0f, 0.0f);
		for (int b = 0; b < 16; b++) {
			for (int i = 0; i < 16; i++) {
				gDivs[bank][i][indepEditA]=0;
				gSteps[bank][i][indepEditA]=0;
				gProbs[bank][i][indepEditA]=1.0f;
				gSpeeds[bank][i][indepEditA]=2;
				gClockShifts[bank][i][indepEditA]=0;
				gTimeShifts[bank][i][indepEditA]=0.0f;
				gPulseWidths[bank][i][indepEditA]=0.25f;

				gDivs[bank][i][indepEditB]=0;
				gSteps[bank][i][indepEditB]=0;
				gProbs[bank][i][indepEditB]=1.0f;
				gSpeeds[bank][i][indepEditB]=2;
				gClockShifts[bank][i][indepEditB]=0;
				gTimeShifts[bank][i][indepEditB]=0.0f;
				gPulseWidths[bank][i][indepEditB]=0.25f;

				gDivs[bank][i][matrix]=0;
				gSteps[bank][i][matrix]=0;
				gProbs[bank][i][matrix]=1.0f;
				gSpeeds[bank][i][matrix]=2;
				gClockShifts[bank][i][matrix]=0;
				gTimeShifts[bank][i][matrix]=0.0f;
				gPulseWidths[bank][i][matrix]=0.25f;
			}
		}
  	for (int i = 0; i < 16; i++) {
      configParam(CONTROLS_PARAMS + i, 0.0f, 10.0f, 0.0f);
  	}
		for (int i = 0; i < 8; i++) {
      configParam(MUTE_PARAMS + i, 0.0f, 10.0f, 0.0f);
  	}
		updateControls();
		random::init();
  }

  void process(const ProcessArgs &args) override;

	json_t *dataToJson() override {
		json_t *rootJ = BidooModule::dataToJson();

		json_t *banksJ = json_array();
		json_t *banksModesJ = json_array();

		for(int b=0; b<16; b++) {
			json_t *modeJ = json_integer(patternModes[b]);
			json_array_append_new(banksModesJ, modeJ);

			json_t *bankJ = json_array();

			json_t *bankIndepAJ = json_array();
			json_t *divsIndepAJ = json_array();
			json_t *stepsIndepAJ = json_array();
			json_t *probsIndepAJ = json_array();
			json_t *speedsIndepAJ = json_array();
			json_t *clockShiftsIndepAJ = json_array();
			json_t *timeShiftsIndepAJ = json_array();
			json_t *pulseWidthsIndepAJ = json_array();

			json_t *bankIndepBJ = json_array();
			json_t *divsIndepBJ = json_array();
			json_t *stepsIndepBJ = json_array();
			json_t *probsIndepBJ = json_array();
			json_t *speedsIndepBJ = json_array();
			json_t *clockShiftsIndepBJ = json_array();
			json_t *timeShiftsIndepBJ = json_array();
			json_t *pulseWidthsIndepBJ = json_array();

			json_t *bankMatrixJ = json_array();
			json_t *divsMatrixJ = json_array();
			json_t *stepsMatrixJ = json_array();
			json_t *probsMatrixJ = json_array();
			json_t *speedsMatrixJ = json_array();
			json_t *clockShiftsMatrixJ = json_array();
			json_t *timeShiftsMatrixJ = json_array();
			json_t *pulseWidthsMatrixJ = json_array();

			for (int i = 0; i < 16; i++) {
				json_t *divIndepAJ = json_real(gDivs[b][i][indepEditA]);
				json_array_append_new(divsIndepAJ, divIndepAJ);
				json_t *stepIndepAJ = json_real(gSteps[b][i][indepEditA]);
				json_array_append_new(stepsIndepAJ, stepIndepAJ);
				json_t *probIndepAJ = json_real(gProbs[b][i][indepEditA]);
				json_array_append_new(probsIndepAJ, probIndepAJ);
				json_t *speedIndepAJ = json_real(gSpeeds[b][i][indepEditA]);
				json_array_append_new(speedsIndepAJ, speedIndepAJ);
				json_t *clockShiftIndepAJ = json_real(gClockShifts[b][i][indepEditA]);
				json_array_append_new(clockShiftsIndepAJ, clockShiftIndepAJ);
				json_t *timeShiftIndepAJ = json_real(gTimeShifts[b][i][indepEditA]);
				json_array_append_new(timeShiftsIndepAJ, timeShiftIndepAJ);
				json_t *pulseWidthIndepAJ = json_real(gPulseWidths[b][i][indepEditA]);
				json_array_append_new(pulseWidthsIndepAJ, pulseWidthIndepAJ);

				json_t *divIndepBJ = json_real(gDivs[b][i][indepEditB]);
				json_array_append_new(divsIndepBJ, divIndepBJ);
				json_t *stepIndepBJ = json_real(gSteps[b][i][indepEditB]);
				json_array_append_new(stepsIndepBJ, stepIndepBJ);
				json_t *probIndepBJ = json_real(gProbs[b][i][indepEditB]);
				json_array_append_new(probsIndepBJ, probIndepBJ);
				json_t *speedIndepBJ = json_real(gSpeeds[b][i][indepEditB]);
				json_array_append_new(speedsIndepBJ, speedIndepBJ);
				json_t *clockShiftIndepBJ = json_real(gClockShifts[b][i][indepEditB]);
				json_array_append_new(clockShiftsIndepBJ, clockShiftIndepBJ);
				json_t *timeShiftIndepBJ = json_real(gTimeShifts[b][i][indepEditB]);
				json_array_append_new(timeShiftsIndepBJ, timeShiftIndepBJ);
				json_t *pulseWidthIndepBJ = json_real(gPulseWidths[b][i][indepEditB]);
				json_array_append_new(pulseWidthsIndepBJ, pulseWidthIndepBJ);

				json_t *divMatrixJ = json_real(gDivs[b][i][matrix]);
				json_array_append_new(divsMatrixJ, divMatrixJ);
				json_t *stepMatrixJ = json_real(gSteps[b][i][matrix]);
				json_array_append_new(stepsMatrixJ, stepMatrixJ);
				json_t *probMatrixJ = json_real(gProbs[b][i][matrix]);
				json_array_append_new(probsMatrixJ, probMatrixJ);
				json_t *speedMatrixJ = json_real(gSpeeds[b][i][matrix]);
				json_array_append_new(speedsMatrixJ, speedMatrixJ);
				json_t *clockShiftMatrixJ = json_real(gClockShifts[b][i][matrix]);
				json_array_append_new(clockShiftsMatrixJ, clockShiftMatrixJ);
				json_t *timeShiftMatrixJ = json_real(gTimeShifts[b][i][matrix]);
				json_array_append_new(timeShiftsMatrixJ, timeShiftMatrixJ);
				json_t *pulseWidthMatrixJ = json_real(gPulseWidths[b][i][matrix]);
				json_array_append_new(pulseWidthsMatrixJ, pulseWidthMatrixJ);
			}
			json_array_append_new(bankIndepAJ, divsIndepAJ);
			json_array_append_new(bankIndepAJ, stepsIndepAJ);
			json_array_append_new(bankIndepAJ, probsIndepAJ);
			json_array_append_new(bankIndepAJ, speedsIndepAJ);
			json_array_append_new(bankIndepAJ, clockShiftsIndepAJ);
			json_array_append_new(bankIndepAJ, timeShiftsIndepAJ);
			json_array_append_new(bankIndepAJ, pulseWidthsIndepAJ);
			json_array_append_new(bankJ, bankIndepAJ);

			json_array_append_new(bankIndepBJ, divsIndepBJ);
			json_array_append_new(bankIndepBJ, stepsIndepBJ);
			json_array_append_new(bankIndepBJ, probsIndepBJ);
			json_array_append_new(bankIndepBJ, speedsIndepBJ);
			json_array_append_new(bankIndepBJ, clockShiftsIndepBJ);
			json_array_append_new(bankIndepBJ, timeShiftsIndepBJ);
			json_array_append_new(bankIndepBJ, pulseWidthsIndepBJ);
			json_array_append_new(bankJ, bankIndepBJ);

			json_array_append_new(bankMatrixJ, divsMatrixJ);
			json_array_append_new(bankMatrixJ, stepsMatrixJ);
			json_array_append_new(bankMatrixJ, probsMatrixJ);
			json_array_append_new(bankMatrixJ, speedsMatrixJ);
			json_array_append_new(bankMatrixJ, clockShiftsMatrixJ);
			json_array_append_new(bankMatrixJ, timeShiftsMatrixJ);
			json_array_append_new(bankMatrixJ, pulseWidthsMatrixJ);
			json_array_append_new(bankJ, bankMatrixJ);

			json_array_append_new(banksJ, bankJ);
		}
		json_object_set_new(rootJ, "banks", banksJ);
		json_object_set_new(rootJ, "modes", banksModesJ);

		return rootJ;
	}

	void dataFromJson(json_t *rootJ) override {
		BidooModule::dataFromJson(rootJ);

		json_t *banksJ = json_object_get(rootJ, "banks");
		json_t *banksModesJ = json_object_get(rootJ, "modes");

		if (banksModesJ) {
			for (int i = 0; i < 16; i++) {
				json_t *modeJ=json_array_get(banksModesJ, i);
				if (modeJ) {
					patternModes[i] = json_number_value(modeJ);
				}
			}
		}

		if (banksJ) {
			for (int b = 0; b < 16; b++) {
				json_t *bankJ = json_array_get(banksJ, b);
				if (bankJ) {
					json_t *bankIndepAJ = json_array_get(bankJ, 0);
					if (bankIndepAJ) {
						json_t *divsIndepAJ = json_array_get(bankIndepAJ, 0);
						json_t *stepsIndepAJ = json_array_get(bankIndepAJ, 1);
						json_t *probsIndepAJ = json_array_get(bankIndepAJ, 2);
						json_t *speedsIndepAJ = json_array_get(bankIndepAJ, 3);
						json_t *clockShiftsIndepAJ = json_array_get(bankIndepAJ, 4);
						json_t *timeShiftsIndepAJ = json_array_get(bankIndepAJ, 5);
						json_t *pulseWidthsIndepAJ = json_array_get(bankIndepAJ, 6);
						if (divsIndepAJ && stepsIndepAJ && probsIndepAJ && speedsIndepAJ && clockShiftsIndepAJ && timeShiftsIndepAJ && pulseWidthsIndepAJ) {
							for (int i = 0; i < 16; i++) {
								json_t *divIndepAJ=json_array_get(divsIndepAJ, i);
								if (divIndepAJ) {
									gDivs[b][i][indepEditA] = json_number_value(divIndepAJ);
								}
								json_t *stepIndepAJ=json_array_get(stepsIndepAJ, i);
								if (stepIndepAJ) {
									gSteps[b][i][indepEditA] = json_number_value(stepIndepAJ);
								}
								json_t *probIndepAJ=json_array_get(probsIndepAJ, i);
								if (probIndepAJ) {
									gProbs[b][i][indepEditA] = json_number_value(probIndepAJ);
								}
								json_t *speedIndepAJ=json_array_get(speedsIndepAJ, i);
								if (speedIndepAJ) {
									gSpeeds[b][i][indepEditA] = json_number_value(speedIndepAJ);
								}
								json_t *clockShiftIndepAJ=json_array_get(clockShiftsIndepAJ, i);
								if (clockShiftIndepAJ) {
									gClockShifts[b][i][indepEditA] = json_number_value(clockShiftIndepAJ);
								}
								json_t *timeShiftIndepAJ=json_array_get(timeShiftsIndepAJ, i);
								if (timeShiftIndepAJ) {
									gTimeShifts[b][i][indepEditA] = json_number_value(timeShiftIndepAJ);
								}
								json_t *pulseWidthIndepAJ=json_array_get(pulseWidthsIndepAJ, i);
								if (pulseWidthIndepAJ) {
									gPulseWidths[b][i][indepEditA] = json_number_value(pulseWidthIndepAJ);
								}
							}
						}
					}

					json_t *bankIndepBJ = json_array_get(bankJ, 1);
					if (bankIndepBJ) {
						json_t *divsIndepBJ = json_array_get(bankIndepBJ, 0);
						json_t *stepsIndepBJ = json_array_get(bankIndepBJ, 1);
						json_t *probsIndepBJ = json_array_get(bankIndepBJ, 2);
						json_t *speedsIndepBJ = json_array_get(bankIndepBJ, 3);
						json_t *clockShiftsIndepBJ = json_array_get(bankIndepBJ, 4);
						json_t *timeShiftsIndepBJ = json_array_get(bankIndepBJ, 5);
						json_t *pulseWidthsIndepBJ = json_array_get(bankIndepBJ, 6);
						if (divsIndepBJ && stepsIndepBJ && probsIndepBJ && speedsIndepBJ && clockShiftsIndepBJ && timeShiftsIndepBJ && pulseWidthsIndepBJ) {
							for (int i = 0; i < 16; i++) {
								json_t *divIndepBJ=json_array_get(divsIndepBJ, i);
								if (divIndepBJ) {
									gDivs[b][i][indepEditB] = json_number_value(divIndepBJ);
								}
								json_t *stepIndepBJ=json_array_get(stepsIndepBJ, i);
								if (stepIndepBJ) {
									gSteps[b][i][indepEditB] = json_number_value(stepIndepBJ);
								}
								json_t *probIndepBJ=json_array_get(probsIndepBJ, i);
								if (probIndepBJ) {
									gProbs[b][i][indepEditB] = json_number_value(probIndepBJ);
								}
								json_t *speedIndepBJ=json_array_get(speedsIndepBJ, i);
								if (speedIndepBJ) {
									gSpeeds[b][i][indepEditB] = json_number_value(speedIndepBJ);
								}
								json_t *clockShiftIndepBJ=json_array_get(clockShiftsIndepBJ, i);
								if (clockShiftIndepBJ) {
									gClockShifts[b][i][indepEditB] = json_number_value(clockShiftIndepBJ);
								}
								json_t *timeShiftIndepBJ=json_array_get(timeShiftsIndepBJ, i);
								if (timeShiftIndepBJ) {
									gTimeShifts[b][i][indepEditB] = json_number_value(timeShiftIndepBJ);
								}
								json_t *pulseWidthIndepBJ=json_array_get(pulseWidthsIndepBJ, i);
								if (pulseWidthIndepBJ) {
									gPulseWidths[b][i][indepEditB] = json_number_value(pulseWidthIndepBJ);
								}
							}
						}
					}

					json_t *bankMatrixJ = json_array_get(bankJ, 2);
					if (bankMatrixJ) {
						json_t *divsMatrixJ = json_array_get(bankMatrixJ, 0);
						json_t *stepsMatrixJ = json_array_get(bankMatrixJ, 1);
						json_t *probsMatrixJ = json_array_get(bankMatrixJ, 2);
						json_t *speedsMatrixJ = json_array_get(bankMatrixJ, 3);
						json_t *clockShiftsMatrixJ = json_array_get(bankMatrixJ, 4);
						json_t *timeShiftsMatrixJ = json_array_get(bankMatrixJ, 5);
						json_t *pulseWidthsMatrixJ = json_array_get(bankMatrixJ, 6);
						if (divsMatrixJ && stepsMatrixJ && probsMatrixJ && speedsMatrixJ && clockShiftsMatrixJ && timeShiftsMatrixJ && pulseWidthsMatrixJ) {
							for (int i = 0; i < 16; i++) {
								json_t *divMatrixJ=json_array_get(divsMatrixJ, i);
								if (divMatrixJ) {
									gDivs[b][i][matrix] = json_number_value(divMatrixJ);
								}
								json_t *stepMatrixJ=json_array_get(stepsMatrixJ, i);
								if (stepMatrixJ) {
									gSteps[b][i][matrix] = json_number_value(stepMatrixJ);
								}
								json_t *probMatrixJ=json_array_get(probsMatrixJ, i);
								if (probMatrixJ) {
									gProbs[b][i][matrix] = json_number_value(probMatrixJ);
								}
								json_t *speedMatrixJ=json_array_get(speedsMatrixJ, i);
								if (speedMatrixJ) {
									gSpeeds[b][i][matrix] = json_number_value(speedMatrixJ);
								}
								json_t *clockShiftMatrixJ=json_array_get(clockShiftsMatrixJ, i);
								if (clockShiftMatrixJ) {
									gClockShifts[b][i][matrix] = json_number_value(clockShiftMatrixJ);
								}
								json_t *timeShiftMatrixJ=json_array_get(timeShiftsMatrixJ, i);
								if (timeShiftMatrixJ) {
									gTimeShifts[b][i][matrix] = json_number_value(timeShiftMatrixJ);
								}
								json_t *pulseWidthMatrixJ=json_array_get(pulseWidthsMatrixJ, i);
								if (pulseWidthMatrixJ) {
									gPulseWidths[b][i][matrix] = json_number_value(pulseWidthMatrixJ);
								}
							}
						}
					}
				}
			}
		}
		page = pDiv;
		params[DIV_PARAM].setValue(1.0f);
		params[STEP_PARAM].setValue(0.0f);
		params[PROB_PARAM].setValue(0.0f);
		params[SPEED_PARAM].setValue(0.0f);
		params[CLOCKSHIFT_PARAM].setValue(0.0f);
		params[TIMESHIFT_PARAM].setValue(0.0f);
		params[PULSEWIDTH_PARAM].setValue(0.0f);
	}

	void onReset() override {
		for (int b = 0; b < 16; b++) {
			for (int i = 0; i < 16; i++) {
				gDivs[bank][i][indepEditA]=0;
				gSteps[bank][i][indepEditA]=0;
				gProbs[bank][i][indepEditA]=1.0f;
				gSpeeds[bank][i][indepEditA]=2;
				gClockShifts[bank][i][indepEditA]=0;
				gTimeShifts[bank][i][indepEditA]=0.0f;
				gPulseWidths[bank][i][indepEditA]=0.25f;

				gDivs[bank][i][indepEditB]=0;
				gSteps[bank][i][indepEditB]=0;
				gProbs[bank][i][indepEditB]=1.0f;
				gSpeeds[bank][i][indepEditB]=2;
				gClockShifts[bank][i][indepEditB]=0;
				gTimeShifts[bank][i][indepEditB]=0.0f;
				gPulseWidths[bank][i][indepEditB]=0.25f;

				gDivs[bank][i][matrix]=0;
				gSteps[bank][i][matrix]=0;
				gProbs[bank][i][matrix]=1.0f;
				gSpeeds[bank][i][matrix]=2;
				gClockShifts[bank][i][matrix]=0;
				gTimeShifts[bank][i][matrix]=0.0f;
				gPulseWidths[bank][i][matrix]=0.25f;
			}
		}
		page = pDiv;
		params[DIV_PARAM].setValue(1.0f);
		params[STEP_PARAM].setValue(0.0f);
		params[PROB_PARAM].setValue(0.0f);
		params[SPEED_PARAM].setValue(0.0f);
		params[CLOCKSHIFT_PARAM].setValue(0.0f);
		params[TIMESHIFT_PARAM].setValue(0.0f);
		params[PULSEWIDTH_PARAM].setValue(0.0f);
		updateControls();
	}

	void updateControls() {
		if (page == pDiv) {
			for (int i = 0; i < 16; i++) {
	      params[CONTROLS_PARAMS+i].setValue(gDivs[bank][i][pMode]/6.4f);
	  	}
		}
		else if (page == pStep) {
			for (int i = 0; i < 16; i++) {
	      params[CONTROLS_PARAMS+i].setValue(gSteps[bank][i][pMode]/6.4f);
	  	}
		}
		else if (page == pProb) {
			for (int i = 0; i < 16; i++) {
	      params[CONTROLS_PARAMS+i].setValue(gProbs[bank][i][pMode]*10.0f);
	  	}
		}
		else if (page == pSpeed) {
			for (int i = 0; i < 16; i++) {
	      params[CONTROLS_PARAMS+i].setValue(gSpeeds[bank][i][pMode]/1.5f);
	  	}
		}
		else if (page == pClockShift) {
			for (int i = 0; i < 16; i++) {
	      params[CONTROLS_PARAMS+i].setValue(gClockShifts[bank][i][pMode]/6.4f);
	  	}
		}
		else if (page == pTimeShift) {
			for (int i = 0; i < 16; i++) {
	      params[CONTROLS_PARAMS+i].setValue(rescale(gTimeShifts[bank][i][pMode],-1.0f,1.0f,0.0f,10.0f));
	  	}
		}
		else {
			for (int i = 0; i < 16; i++) {
	      params[CONTROLS_PARAMS+i].setValue(gPulseWidths[bank][i][pMode]*10.0f);
	  	}
		}
	}

	void computeMutes(int i) {
		if (muteTriggers[i].process(params[MUTE_PARAMS+i].getValue())) mutes[i]=!mutes[i];
		lights[MUTE_LIGHTS+i*3].setBrightness(mutes[i]?1:0);
		lights[MUTE_LIGHTS+i*3+1].setBrightness(mutes[i]?0:1);
		lights[MUTE_LIGHTS+i*3+2].setBrightness(0);
	}

	void computeHead(int index, float sT, float sR) {
		for (int i=0; i<3; i++) {
			if (!resetT) {
				if ((gDivs[bank][index][i]>0) && opened[index][i] && (delays[index][i]==0.0f)) {
					if (gProbs[bank][index][i] >= random::uniform()) gatePulses[index][i].trigger((gPulseWidths[bank][index][i]*gDivs[bank][index][i]/speeds[gSpeeds[bank][index][i]])*tickAccumulator);
					if (gSteps[bank][index][i] != 0) opened[index][i]=false;
					delays[index][i] = opened[index][i] ? gDivs[bank][index][i]*tickAccumulator : 0.0f;
				}
			}
			else {
				gatePulses[index][i].reset();
				opened[index][i]=true;
				delays[index][i]=(gClockShifts[bank][index][i] + gDivs[bank][index][i]*gSteps[bank][index][i] + gTimeShifts[bank][index][i])*tickAccumulator;
				if ((gDivs[bank][index][i]>0) && (delays[index][i]==0.0f)) {
					if (gProbs[bank][index][i]>=random::uniform()) gatePulses[index][i].trigger((gPulseWidths[bank][index][i]*gDivs[bank][index][i]/speeds[gSpeeds[bank][index][i]])*tickAccumulator);
				}
			}

			delays[index][i]=std::max(delays[index][i]-speeds[gSpeeds[bank][index][i]]*sT,0.0f);
			gates[index][i]=gatePulses[index][i].process(sT);
		}
	}

};

void RATEAU::process(const ProcessArgs &args) {
	int nBank = clamp(params[BANK_PARAM].getValue()+rescale(inputs[BANK_INPUT].getVoltage(),0.f,10.0f,0.0f,15.0f),0.0f,15.0f);

	if (start) {
		updateControls();
		start = false;
	}

	clockT = clockTrigger.process(inputs[CLOCK_INPUT].getVoltage());
	resetT = resetTrigger.process(inputs[RESET_INPUT].getVoltage());

	if (clockT) {
			tickAccumulator = currentTickCount;
			currentTickCount = 0.0f;
	}

	if (resetT && (nBank!=bank)) {
		bank=nBank;
		updateControls();
	}

	if (indepEditATrigger.process(params[INDEPEDITA_PARAM].getValue())) {
		previousMode = pMode != indepEditA ? pMode : previousMode;
		pMode = indepEditA;
		updateControls();
	}

	if (indepEditBTrigger.process(params[INDEPEDITB_PARAM].getValue())) {
		previousMode = pMode != indepEditB ? pMode : previousMode;
		pMode = indepEditB;
		updateControls();
	}

	if (matrixTrigger.process(params[MATRIX_PARAM].getValue())) {
		previousMode = pMode != matrix ? pMode : previousMode;
		pMode = matrix;
		updateControls();
	}

	if (modeInputTrigger.process(inputs[MODE_INPUT].getVoltage())) {
		if (pMode != matrix) {
			previousMode = pMode;
			pMode = matrix;
		}
		else {
			if (previousMode != matrix) {
				pMode = previousMode;
			}
			else {
				pMode = indepEditA;
			}
		}
		updateControls();
	}

	lights[INDEPEDITA_LIGHT].setBrightness(pMode==indepEditA?1:0);
	lights[INDEPEDITB_LIGHT].setBrightness(pMode==indepEditB?1:0);
	lights[MATRIX_LIGHT].setBrightness(pMode==matrix?1:0);

	if (divTrigger.process(params[DIV_PARAM].getValue())) {
		params[STEP_PARAM].setValue(0.0f);
		params[PROB_PARAM].setValue(0.0f);
		params[SPEED_PARAM].setValue(0.0f);
		params[CLOCKSHIFT_PARAM].setValue(0.0f);
		params[TIMESHIFT_PARAM].setValue(0.0f);
		params[PULSEWIDTH_PARAM].setValue(0.0f);
		page=pDiv;
		updateControls();
	}

	if (stepTrigger.process(params[STEP_PARAM].getValue())) {
		params[DIV_PARAM].setValue(0.0f);
		params[PROB_PARAM].setValue(0.0f);
		params[SPEED_PARAM].setValue(0.0f);
		params[CLOCKSHIFT_PARAM].setValue(0.0f);
		params[TIMESHIFT_PARAM].setValue(0.0f);
		params[PULSEWIDTH_PARAM].setValue(0.0f);
		page=pStep;
		updateControls();
	}

	if (probTrigger.process(params[PROB_PARAM].getValue())) {
		params[DIV_PARAM].setValue(0.0f);
		params[STEP_PARAM].setValue(0.0f);
		params[SPEED_PARAM].setValue(0.0f);
		params[CLOCKSHIFT_PARAM].setValue(0.0f);
		params[TIMESHIFT_PARAM].setValue(0.0f);
		params[PULSEWIDTH_PARAM].setValue(0.0f);
		page=pProb;
		updateControls();
	}

	if (speedTrigger.process(params[SPEED_PARAM].getValue())) {
		params[DIV_PARAM].setValue(0.0f);
		params[STEP_PARAM].setValue(0.0f);
		params[PROB_PARAM].setValue(0.0f);
		params[CLOCKSHIFT_PARAM].setValue(0.0f);
		params[TIMESHIFT_PARAM].setValue(0.0f);
		params[PULSEWIDTH_PARAM].setValue(0.0f);
		page=pSpeed;
		updateControls();
	}

	if (clockshiftTrigger.process(params[CLOCKSHIFT_PARAM].getValue())) {
		params[DIV_PARAM].setValue(0.0f);
		params[STEP_PARAM].setValue(0.0f);
		params[PROB_PARAM].setValue(0.0f);
		params[SPEED_PARAM].setValue(0.0f);
		params[TIMESHIFT_PARAM].setValue(0.0f);
		params[PULSEWIDTH_PARAM].setValue(0.0f);
		page=pClockShift;
		updateControls();
	}

	if (timeshiftTrigger.process(params[TIMESHIFT_PARAM].getValue())) {
		params[DIV_PARAM].setValue(0.0f);
		params[STEP_PARAM].setValue(0.0f);
		params[PROB_PARAM].setValue(0.0f);
		params[SPEED_PARAM].setValue(0.0f);
		params[CLOCKSHIFT_PARAM].setValue(0.0f);
		params[PULSEWIDTH_PARAM].setValue(0.0f);
		page=pTimeShift;
		updateControls();
	}

	if (pulsewidthTrigger.process(params[PULSEWIDTH_PARAM].getValue())) {
		params[DIV_PARAM].setValue(0.0f);
		params[STEP_PARAM].setValue(0.0f);
		params[PROB_PARAM].setValue(0.0f);
		params[SPEED_PARAM].setValue(0.0f);
		params[CLOCKSHIFT_PARAM].setValue(0.0f);
		params[TIMESHIFT_PARAM].setValue(0.0f);
		page=pPulseWidth;
		updateControls();
	}

	if (page == pDiv) {
		for (int i = 0; i < 16; i++) {
			gDivs[bank][i][pMode]=params[CONTROLS_PARAMS+i].getValue()*6.4f;
			computeHead(i,args.sampleTime,args.sampleRate);
			if (i<8) computeMutes(i);
		}
	}
	else if (page == pStep) {
		for (int i = 0; i < 16; i++) {
			gSteps[bank][i][pMode]=params[CONTROLS_PARAMS+i].getValue()*6.4f;
			computeHead(i,args.sampleTime,args.sampleRate);
			if (i<8) computeMutes(i);
		}
	}
	else if (page == pProb) {
		for (int i = 0; i < 16; i++) {
			gProbs[bank][i][pMode]=params[CONTROLS_PARAMS+i].getValue()*0.1f;
			computeHead(i,args.sampleTime,args.sampleRate);
			if (i<8) computeMutes(i);
		}
	}
	else if (page == pSpeed) {
		for (int i = 0; i < 16; i++) {
			gSpeeds[bank][i][pMode]=(int)(params[CONTROLS_PARAMS+i].getValue()*1.5f);
			computeHead(i,args.sampleTime,args.sampleRate);
			if (i<8) computeMutes(i);
		}
	}
	else if (page == pClockShift) {
		for (int i = 0; i < 16; i++) {
			gClockShifts[bank][i][pMode]=params[CONTROLS_PARAMS+i].getValue()*6.4f;
			computeHead(i,args.sampleTime,args.sampleRate);
			if (i<8) computeMutes(i);
		}
	}
	else if (page == pTimeShift) {
		for (int i = 0; i < 16; i++) {
			gTimeShifts[bank][i][pMode]=rescale(params[CONTROLS_PARAMS+i].getValue(),0.0f,10.0f,-1.0f,1.0f);
			computeHead(i,args.sampleTime,args.sampleRate);
			if (i<8) computeMutes(i);
		}
	}
	else {
		for (int i = 0; i < 16; i++) {
			gPulseWidths[bank][i][pMode]=params[CONTROLS_PARAMS+i].getValue()*0.1f;
			computeHead(i,args.sampleTime,args.sampleRate);
			if (i<8) computeMutes(i);
		}
	}

	if (pMode == matrix) {
		outputs[GATE_OUTPUTS].setVoltage(!mutes[0]&&(gates[0][matrix]||gates[1][matrix]||gates[2][matrix]||gates[3][matrix])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+1].setVoltage(!mutes[1]&&(gates[4][matrix]||gates[5][matrix]||gates[6][matrix]||gates[7][matrix])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+2].setVoltage(!mutes[2]&&(gates[8][matrix]||gates[9][matrix]||gates[10][matrix]||gates[11][matrix])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+3].setVoltage(!mutes[3]&&(gates[12][matrix]||gates[13][matrix]||gates[14][matrix]||gates[15][matrix])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+4].setVoltage(!mutes[4]&&(gates[0][matrix]||gates[4][matrix]||gates[8][matrix]||gates[12][matrix])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+5].setVoltage(!mutes[5]&&(gates[1][matrix]||gates[5][matrix]||gates[9][matrix]||gates[13][matrix])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+6].setVoltage(!mutes[6]&&(gates[2][matrix]||gates[6][matrix]||gates[10][matrix]||gates[14][matrix])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+7].setVoltage(!mutes[7]&&(gates[3][matrix]||gates[7][matrix]||gates[11][matrix]||gates[15][matrix])?10.0f:0.0f);
	}
	else {
		outputs[GATE_OUTPUTS].setVoltage(!mutes[0]&&(gates[0][indepEditA]||gates[1][indepEditA]||gates[2][indepEditA]||gates[3][indepEditA])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+1].setVoltage(!mutes[1]&&(gates[4][indepEditA]||gates[5][indepEditA]||gates[6][indepEditA]||gates[7][indepEditA])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+2].setVoltage(!mutes[2]&&(gates[8][indepEditA]||gates[9][indepEditA]||gates[10][indepEditA]||gates[11][indepEditA])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+3].setVoltage(!mutes[3]&&(gates[12][indepEditA]||gates[13][indepEditA]||gates[14][indepEditA]||gates[15][indepEditA])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+4].setVoltage(!mutes[4]&&(gates[0][indepEditB]||gates[4][indepEditB]||gates[8][indepEditB]||gates[12][indepEditB])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+5].setVoltage(!mutes[5]&&(gates[1][indepEditB]||gates[5][indepEditB]||gates[9][indepEditB]||gates[13][indepEditB])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+6].setVoltage(!mutes[6]&&(gates[2][indepEditB]||gates[6][indepEditB]||gates[10][indepEditB]||gates[14][indepEditB])?10.0f:0.0f);
		outputs[GATE_OUTPUTS+7].setVoltage(!mutes[7]&&(gates[3][indepEditB]||gates[7][indepEditB]||gates[11][indepEditB]||gates[15][indepEditB])?10.0f:0.0f);
	}

	currentTickCount += args.sampleTime;
}

struct RATEAUBlueKnob : BidooBlueKnob {
	int frame=0;
	unsigned int tFade=255;

	RATEAUBlueKnob () {
		smooth = false;
	}

	void onButton(const ButtonEvent& e) override {
		RoundKnob::onButton(e);
		RATEAU *m = dynamic_cast<RATEAU*>(this->getParamQuantity()->module);
		m->fCntrl = this->getParamQuantity()->paramId - RATEAU::RATEAU::CONTROLS_PARAMS;
	}

	void draw(const DrawArgs& args) override {
		if (getParamQuantity()) {
			RATEAU *m = dynamic_cast<RATEAU*>(this->getParamQuantity()->module);
			for (NSVGshape *shape = bg->svg->handle->shapes; shape != NULL; shape = shape->next) {
				std::string str(shape->id);
				if ((str == "bidooKnob") || (str == "bidooInterior")) {
					if (m->fCntrl == this->getParamQuantity()->paramId - RATEAU::RATEAU::CONTROLS_PARAMS) {
						shape->fill.color = (unsigned int)42 | (unsigned int)87 << 8 | (unsigned int)117 << 16;
						if (++frame <= 30) {
							tFade -= frame*3;
						}
						else if (++frame<60) {
							tFade = 255;
						}
						else {
							tFade = 255;
							frame = 0;
						}
						shape->fill.color |= (unsigned int)(tFade) << 24;
					}
					else {
						tFade = 255;
						frame = 0;
						shape->fill.color = (unsigned int)42 | (unsigned int)87 << 8 | (unsigned int)117 << 16;
						shape->fill.color |= (unsigned int)(tFade) << 24;
					}
				}
			}
		}
		BidooBlueKnob::draw(args);
	}
};

struct RATEAUDisplay : TransparentWidget {
	RATEAU *m;

	RATEAUDisplay() {

	}

	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer == 1) {
			if (m) {
	      nvgFontSize(args.vg, 16);
	  		nvgTextLetterSpacing(args.vg, 0);
	  		nvgFillColor(args.vg, YELLOW_BIDOO);
				stringstream sBank, sFControl, sValue;
				sBank << "PTRN " + to_string(m->bank + 1);
				sFControl << "# " + to_string(m->fCntrl + 1);
				switch(m->page) {
				  case pDiv:
				    //sPage << "DIV";
						sValue << " => " << fixed << setprecision(2) << m->gDivs[m->bank][m->fCntrl][m->pMode];
				    break;
					case pStep:
				    //sPage << "STEPS";
						sValue  << " => " << fixed << setprecision(2) << m->gSteps[m->bank][m->fCntrl][m->pMode];
				    break;
					case pProb:
				    //sPage << "PROB";
						sValue  << " => " << fixed << setprecision(2) << m->gProbs[m->bank][m->fCntrl][m->pMode]*100.0f << " %";
				    break;
					case pSpeed:
				    //sPage << "SPEED";
						sValue  << " => x" << fixed << setprecision(2) << m->speeds[m->gSpeeds[m->bank][m->fCntrl][m->pMode]];
				    break;
					case pClockShift:
				    //sPage << "CLK SHIFT";
						sValue  << " => " << fixed << setprecision(2) << m->gClockShifts[m->bank][m->fCntrl][m->pMode];
				    break;
					case pTimeShift:
						//sPage << "TIME SHIFT";
						sValue  << " => " << fixed << setprecision(2) << m->gTimeShifts[m->bank][m->fCntrl][m->pMode];
						break;
				  default:
						//sPage << "PW";
						sValue  << " => " << fixed << setprecision(2) << m->gPulseWidths[m->bank][m->fCntrl][m->pMode]*100.0f << " %";
				}

				//nvgText(args.vg, 0, 0, sPage.str().c_str(), NULL);
				nvgText(args.vg, 135, 0, sBank.str().c_str(), NULL);
				nvgText(args.vg, 0, 0, sFControl.str().c_str(), NULL);
				nvgText(args.vg, 35, 0, sValue.str().c_str(), NULL);
	    }
		}
		Widget::drawLayer(args, layer);
	}

};

struct divRateauBtn : app::SvgSwitch {
	divRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/divBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/divBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct stepRateauBtn : app::SvgSwitch {
	stepRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/stepBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/stepBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct probRateauBtn : app::SvgSwitch {
	probRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/probBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/probBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct speedRateauBtn : app::SvgSwitch {
	speedRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/speedBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/speedBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct clockshiftRateauBtn : app::SvgSwitch {
	clockshiftRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/clockshiftBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/clockshiftBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct timeshiftRateauBtn : app::SvgSwitch {
	timeshiftRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/timeshiftBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/timeshiftBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct pulsewidthRateauBtn : app::SvgSwitch {
	pulsewidthRateauBtn() {
		momentary = false;
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/pulsewidthBtn_0.svg")));
		addFrame(APP->window->loadSvg(asset::plugin(pluginInstance,"res/ComponentLibrary/pulsewidthBtn_1.svg")));
		shadow->opacity = 0.0f;
	}
};

struct RATEAUWidget : BidooWidget {
	ParamWidget *controls[16];
	void step() override;
  RATEAUWidget(RATEAU *module);
};


RATEAUWidget::RATEAUWidget(RATEAU *module) {
	setModule(module);
	prepareThemes(asset::plugin(pluginInstance, "res/RATEAU.svg"));

	addChild(createWidget<ScrewSilver>(Vec(15, 0)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 0)));
	addChild(createWidget<ScrewSilver>(Vec(15, 365)));
	addChild(createWidget<ScrewSilver>(Vec(box.size.x-30, 365)));

	const int controlsSpacer = 43;
	const int controlsXAnchor = 15;
	const int controlsYAnchor = 150;
	const float intputsAlign = 3.5f;

	const float btnXAnchor = 11.0f;
	const float btnOffset = 30.0f;
	const int ledOffset = 6;
	const int btnYAnchor = 116;

	addParam(createParam<divRateauBtn>(Vec(btnXAnchor, btnYAnchor), module, RATEAU::DIV_PARAM));
	addParam(createParam<stepRateauBtn>(Vec(btnXAnchor+btnOffset, btnYAnchor), module, RATEAU::STEP_PARAM));
	addParam(createParam<probRateauBtn>(Vec(btnXAnchor+2*btnOffset, btnYAnchor), module, RATEAU::PROB_PARAM));
	addParam(createParam<speedRateauBtn>(Vec(btnXAnchor+3*btnOffset, btnYAnchor), module, RATEAU::SPEED_PARAM));
	addParam(createParam<clockshiftRateauBtn>(Vec(btnXAnchor+4*btnOffset, btnYAnchor), module, RATEAU::CLOCKSHIFT_PARAM));
	addParam(createParam<timeshiftRateauBtn>(Vec(btnXAnchor+5*btnOffset, btnYAnchor), module, RATEAU::TIMESHIFT_PARAM));
	addParam(createParam<pulsewidthRateauBtn>(Vec(btnXAnchor+6*btnOffset, btnYAnchor), module, RATEAU::PULSEWIDTH_PARAM));

	addInput(createInput<PJ301MPort>(Vec(13.0f, 45.0f+intputsAlign), module, RATEAU::CLOCK_INPUT));
	addInput(createInput<PJ301MPort>(Vec(49.0f, 45.0f+intputsAlign), module, RATEAU::RESET_INPUT));

	addParam(createParam<BidooLEDButton>(Vec(80.0f,45.0f), module, RATEAU::INDEPEDITA_PARAM));
	addChild(createLight<SmallLight<BlueLight>>(Vec(80.0f+ledOffset,45.0f+ledOffset), module, RATEAU::INDEPEDITA_LIGHT));

	addParam(createParam<BidooLEDButton>(Vec(102.5f,45.0f), module, RATEAU::INDEPEDITB_PARAM));
	addChild(createLight<SmallLight<BlueLight>>(Vec(102.5f+ledOffset,45.0f+ledOffset), module, RATEAU::INDEPEDITB_LIGHT));

	addParam(createParam<BidooLEDButton>(Vec(125.0f,45.0f), module, RATEAU::MATRIX_PARAM));
	addChild(createLight<SmallLight<BlueLight>>(Vec(125.0f+ledOffset,45.0f+ledOffset), module, RATEAU::MATRIX_LIGHT));

		addInput(createInput<TinyPJ301MPort>(Vec(126.5f,67.0f), module, RATEAU::MODE_INPUT));

	addParam(createParam<BidooBlueSnapKnob>(Vec(149.0f, 45.0f), module, RATEAU::BANK_PARAM));
	addInput(createInput<PJ301MPort>(Vec(controlsXAnchor + controlsSpacer * 4, 45.0f+intputsAlign), module, RATEAU::BANK_INPUT));

	RATEAUDisplay *pDisp = new RATEAUDisplay();
	pDisp->box.pos = Vec(17,103);
	pDisp->box.size = Vec(150, 20);
	pDisp->m = module ? module : NULL;
	addChild(pDisp);

	for (int i = 0; i < 16; i++) {
		controls[i] = createParam<RATEAUBlueKnob>(Vec(controlsXAnchor + controlsSpacer *(i%4), controlsYAnchor + controlsSpacer * floor(i/4)), module, RATEAU::CONTROLS_PARAMS + i);
		addParam(controls[i]);

		if (i%4==0) {
			addOutput(createOutput<PJ301MPort>(Vec(controlsXAnchor + controlsSpacer * 4, controlsYAnchor + controlsSpacer * floor(i/4) + intputsAlign), module, RATEAU::GATE_OUTPUTS+i/4));

			addParam(createParam<MiniLEDButton>(Vec(controlsXAnchor - 11, controlsYAnchor + controlsSpacer * floor(i/4) + intputsAlign + 8), module, RATEAU::MUTE_PARAMS+i/4));
			addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(controlsXAnchor - 11 , controlsYAnchor + controlsSpacer * floor(i/4) + intputsAlign + 8), module, RATEAU::MUTE_LIGHTS+3*i/4));
		}
	}
	for (int i = 0; i < 4; i++) {
			addOutput(createOutput<PJ301MPort>(Vec(controlsXAnchor + controlsSpacer * i + intputsAlign, controlsYAnchor + controlsSpacer * 4), module, RATEAU::GATE_OUTPUTS+4+i));

			addParam(createParam<MiniLEDButton>(Vec(controlsXAnchor + controlsSpacer * i + intputsAlign + 8, controlsYAnchor - 11), module, RATEAU::MUTE_PARAMS+4+i));
			addChild(createLight<SmallLight<RedGreenBlueLight>>(Vec(controlsXAnchor + controlsSpacer * i + intputsAlign + 8, controlsYAnchor - 11 ), module, RATEAU::MUTE_LIGHTS+(4+i)*3));
	}
}

void RATEAUWidget::step() {
	for (int i = 0; i < 16; i++) {
		dynamic_cast<RATEAUBlueKnob*>(controls[i])->fb->setDirty();
	}
	BidooWidget::step();
}

Model *modelRATEAU = createModel<RATEAU, RATEAUWidget>("RATEAU");
