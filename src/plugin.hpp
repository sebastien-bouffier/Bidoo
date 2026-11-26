#pragma once
#include "rack.hpp"

using namespace rack;

const NVGcolor BLUE_BIDOO = nvgRGBA(42, 87, 117, 255);
const NVGcolor LIGHTBLUE_BIDOO = nvgRGBA(45, 114, 143, 255);
const NVGcolor RED_BIDOO = nvgRGBA(205, 31, 0, 255);
const NVGcolor YELLOW_BIDOO = nvgRGBA(255, 233, 0, 255);
const NVGcolor YELLOW_BIDOO_LIGHT = nvgRGBA(255, 233, 0, 25);
const NVGcolor SAND_BIDOO = nvgRGBA(230, 220, 191, 255);
const NVGcolor ORANGE_BIDOO = nvgRGBA(228, 87, 46, 255);
const NVGcolor PINK_BIDOO = nvgRGBA(164, 3, 111, 255);
const NVGcolor GREEN_BIDOO = nvgRGBA(2, 195, 154, 255);

extern Plugin *pluginInstance;

extern Model *modelDTROY;
extern Model *modelBORDL;
extern Model *modelZOUMAI;
extern Model *modelZOUMAIExpander;
extern Model *modelTOCANTE;
extern Model *modelCHUTE;
extern Model *modelRATEAU;
extern Model *modelLATE;
extern Model *modelACNE;
extern Model *modelMS;
extern Model *modelOUAIVE;
extern Model *modelEDSAROS;
extern Model *modelPOUPRE;
extern Model *modelMAGMA;
extern Model *modelOAI;
extern Model *modelEMILE;
extern Model *modelFORK;
extern Model *modelFLAME;
extern Model *modelTIARE;
extern Model *modelANTN;
extern Model *modelLIMONADE;
extern Model *modelLIMBO;
extern Model *modelPERCO;
extern Model *modelBAFIS;
extern Model *modelBAR;
extern Model *modelMINIBAR;
extern Model *modelZINC;
extern Model *modelDUKE;
extern Model *modelMOIRE;
extern Model *modelPILOT;
extern Model *modelHUITRE;
extern Model *modelSAISON;
extern Model *modelCANARD;
extern Model *modelFREIN;
extern Model *modelLOURDE;
extern Model *modelDILEMO;
extern Model *modelLAMBDA;
extern Model *modelBANCAU;
extern Model *modelHCTIP;
extern Model *modelSPORE;
extern Model *modelREI;
extern Model *modelSOUNAIRE;
extern Model *modelCOPIA;
extern Model *modelFILTRE;
extern Model *modelACIDE;
extern Model *modelRICOCHET;
extern Model *modelDOSE;
extern Model *modelREGISTRE;
extern Model *modelPARTICULES;
extern Model *modelSIGMA;
extern Model *modelRABBIT;
extern Model *modelBISTROT;
extern Model *modelDIKTAT;
extern Model *modelVOID;

struct InstantiateExpanderItem : MenuItem {
	Module* module;
	Model* model;
	Vec posit;
	void onAction(const event::Action &e) override;
};

// Theme ids saved in patches and in <user>/Bidoo.json. 3 and 4 were the removed blue and green (shown as light).
enum BidooTheme { THEME_LIGHT = 0, THEME_DARK = 1, THEME_BLACK = 2, THEME_FOLLOW_RACK = 5 };

struct BidooModule : Module {
	int themeId = -1; // -1 until the widget sets the user's default
	json_t *dataToJson() override;
	void dataFromJson(json_t *rootJ) override;
};

struct BidooWidget : ModuleWidget {
	SvgPanel* lightPanel;
	SvgPanel* darkPanel;
	SvgPanel* blackPanel;
	int defaultPanelTheme = THEME_FOLLOW_RACK;
	int shownTheme = -1; // panel currently visible, -1 before the first showTheme
	engine::Module *themeModule = nullptr; // module the cast below was made for
	BidooModule *bidooModule = nullptr;

	BidooWidget() {
		readThemeAndContrastFromDefault();
	}

	void setTheme(int theme);
	void writeThemeAndContrastAsDefault();
	void readThemeAndContrastFromDefault();
	void prepareThemes(const std::string& filename);
	void appendContextMenu(Menu *menu) override;
	void showTheme(int theme);
	void step() override;
};
