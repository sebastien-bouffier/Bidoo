#include "plugin.hpp"
#include <iostream>

Plugin *pluginInstance;

void init(rack::Plugin *p) {
	pluginInstance = p;

	p->addModel(modelTOCANTE);
	p->addModel(modelLATE);
	p->addModel(modelDIKTAT);
	p->addModel(modelDTROY);
	p->addModel(modelBORDL);
	p->addModel(modelZOUMAI);
	p->addModel(modelZOUMAIExpander);
	p->addModel(modelENCORE);
	p->addModel(modelENCOREExpander);
	p->addModel(modelMU);
	p->addModel(modelRATEAU);
  p->addModel(modelCHUTE);
	p->addModel(modelLOURDE);
	p->addModel(modelDILEMO);
	p->addModel(modelLAMBDA);
	p->addModel(modelBANCAU);
	p->addModel(modelACNE);
	p->addModel(modelMS);
	p->addModel(modelDUKE);
	p->addModel(modelMOIRE);
	p->addModel(modelPILOT);
	p->addModel(modelHUITRE);
	p->addModel(modelOUAIVE);
	p->addModel(modelEDSAROS);
	p->addModel(modelPOUPRE);
	p->addModel(modelMAGMA);
	p->addModel(modelOAI);
	p->addModel(modelCANARD);
	p->addModel(modelEMILE);
	p->addModel(modelFORK);
	p->addModel(modelTIARE);
	p->addModel(modelANTN);
	p->addModel(modelLIMONADE);
	p->addModel(modelLIMBO);
	p->addModel(modelPERCO);
	p->addModel(modelBAFIS);
	p->addModel(modelBAR);
	p->addModel(modelMINIBAR);
	p->addModel(modelZINC);
	p->addModel(modelFREIN);
	p->addModel(modelHCTIP);
	p->addModel(modelSPORE);
	p->addModel(modelDFUZE);
	p->addModel(modelREI);
	p->addModel(modelRABBIT);
	p->addModel(modelBISTROT);
	p->addModel(modelSIGMA);
	p->addModel(modelFLAME);
	p->addModel(modelVOID);
}

void InstantiateExpanderItem::onAction(const event::Action &e) {
	engine::Module* module = model->createModule();
	APP->engine->addModule(module);
	ModuleWidget* mw = model->createModuleWidget(module);
	if (mw) {
		APP->scene->rack->setModulePosNearest(mw, posit);
		APP->scene->rack->addModule(mw);
		history::ModuleAdd *h = new history::ModuleAdd;
		h->name = "create expander module";
		h->setModule(mw);
		APP->history->push(h);
	}
}

json_t *BidooModule::dataToJson() {
	json_t *rootJ = json_object();
	json_object_set_new(rootJ, "themeId", json_integer(themeId));
	return rootJ;
}

void BidooModule::dataFromJson(json_t *rootJ) {
	json_t *themeIdJ = json_object_get(rootJ, "themeId");
	if (themeIdJ)
		themeId = json_integer_value(themeIdJ);
}

void BidooWidget::appendContextMenu(Menu *menu) {
	ModuleWidget::appendContextMenu(menu);
	menu->addChild(new MenuSeparator());
	menu->addChild(createSubmenuItem("Theme", "", [=](ui::Menu* menu) {
		menu->addChild(construct<LightItem>(&MenuItem::text, dynamic_cast<BidooModule*>(module)->themeId == 0 ? "Light ✓" : "Light", &LightItem::module, dynamic_cast<BidooModule*>(module), &LightItem::pWidget, dynamic_cast<BidooWidget*>(this)));
		menu->addChild(construct<DarkItem>(&MenuItem::text, dynamic_cast<BidooModule*>(module)->themeId == 1 ? "Dark ✓" : "Dark", &DarkItem::module, dynamic_cast<BidooModule*>(module), &DarkItem::pWidget, dynamic_cast<BidooWidget*>(this)));
		menu->addChild(construct<BlackItem>(&MenuItem::text, dynamic_cast<BidooModule*>(module)->themeId == 2 ? "Black ✓" : "Black", &BlackItem::module, dynamic_cast<BidooModule*>(module), &BlackItem::pWidget, dynamic_cast<BidooWidget*>(this)));
		menu->addChild(construct<BlueItem>(&MenuItem::text, dynamic_cast<BidooModule*>(module)->themeId == 3 ? "Blue ✓" : "Blue", &BlueItem::module, dynamic_cast<BidooModule*>(module), &BlueItem::pWidget, dynamic_cast<BidooWidget*>(this)));
		menu->addChild(construct<GreenItem>(&MenuItem::text, dynamic_cast<BidooModule*>(module)->themeId == 4 ? "Green ✓" : "Green", &GreenItem::module, dynamic_cast<BidooModule*>(module), &GreenItem::pWidget, dynamic_cast<BidooWidget*>(this)));
	}));
}

unsigned int packedColor(int r, int g, int b, int a) {
	return r + (g << 8) + (b << 16) + (a << 24);
}

void BidooWidget::writeThemeAndContrastAsDefault() {
	json_t *settingsJ = json_object();

	// defaultPanelTheme
	json_object_set_new(settingsJ, "themeDefault", json_integer(defaultPanelTheme));

	std::string settingsFilename = asset::user("Bidoo.json");
	FILE *file = fopen(settingsFilename.c_str(), "w");
	if (file) {
		json_dumpf(settingsJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		fclose(file);
	}
	json_decref(settingsJ);
}

void BidooWidget::readThemeAndContrastFromDefault() {
	std::string settingsFilename = asset::user("Bidoo.json");
	FILE *file = fopen(settingsFilename.c_str(), "r");
	if (!file) {
		defaultPanelTheme = 0;
		writeThemeAndContrastAsDefault();
		return;
	}
	json_error_t error;
	json_t *settingsJ = json_loadf(file, 0, &error);
	if (!settingsJ) {
		fclose(file);
		defaultPanelTheme = 0;
		writeThemeAndContrastAsDefault();
		return;
	}

	// defaultPanelTheme
	json_t *themeDefaultJ = json_object_get(settingsJ, "themeDefault");
	if (themeDefaultJ) {
		defaultPanelTheme = json_integer_value(themeDefaultJ);
	}
	else {
		defaultPanelTheme = 0;
	}

	fclose(file);
	json_decref(settingsJ);
	return;
}

void BidooWidget::prepareThemes(const std::string& filename) {
	readThemeAndContrastFromDefault();

	setPanel(APP->window->loadSvg(filename));
	lightPanel = dynamic_cast<SvgPanel*>(getPanel());

	std::shared_ptr<Svg> dSvg;
	try {
		dSvg = std::make_shared<Svg>();
		dSvg->loadFile(filename);
	}
	catch (Exception& e) {
		WARN("%s", e.what());
		dSvg = NULL;
	}

	darkPanel = new SvgPanel;
	darkPanel->setBackground(dSvg);
	for (NSVGshape *shape = darkPanel->sw->svg->handle->shapes; shape != NULL; shape = shape->next) {
		std::string sId (shape->id);
		std::string sScreen ("screen");
		bool isScreen = sId.find(sScreen)!=std::string::npos;

		if ((shape->fill.color == packedColor(205, 31, 0, 255)) || (shape->stroke.color == packedColor(205, 31, 0, 255))) {
			shape->fill.color = packedColor(150, 0, 0, 255);
			shape->stroke.color = packedColor(150, 0, 0, 255);
		}
		if (((shape->fill.color == packedColor(0, 0, 0, 255)) || (shape->stroke.color == packedColor(0, 0, 0, 255))) && (!isScreen)) {
			shape->fill.color = packedColor(180, 180, 180, 255);
			shape->stroke.color = packedColor(180, 180, 180, 255);
		}
		if ((shape->fill.color == packedColor(230, 230, 230, 255)) || (shape->stroke.color == packedColor(230, 230, 230, 255))) {
			shape->fill.color = packedColor(50, 50, 50, 255);
			shape->stroke.color = packedColor(50, 50, 50, 255);
		}
		if ((shape->fill.color == packedColor(255, 255, 255, 255)) || (shape->stroke.color == packedColor(255, 255, 255, 255))) {
			shape->fill.color = packedColor(40, 40, 40, 255);
			shape->stroke.color = packedColor(40, 40, 40, 255);
		}
		if (shape->opacity<1) {
			shape->fill.color = packedColor(80, 80, 80, 255);
			shape->stroke.color = packedColor(80, 80, 80, 255);
			shape->opacity = 0.6f;
		}
	}
	darkPanel->setVisible(false);
	addChild(darkPanel);

	std::shared_ptr<Svg> bSvg;
	try {
		bSvg = std::make_shared<Svg>();
		bSvg->loadFile(filename);
	}
	catch (Exception& e) {
		WARN("%s", e.what());
		bSvg = NULL;
	}

	blackPanel = new SvgPanel;
	blackPanel->setBackground(bSvg);
	for (NSVGshape *shape = blackPanel->sw->svg->handle->shapes; shape != NULL; shape = shape->next) {
		std::string sId (shape->id);
		std::string sScreen ("screen");
		bool isScreen = sId.find(sScreen)!=std::string::npos;

		if ((shape->fill.color == packedColor(205, 31, 0, 255)) || (shape->stroke.color == packedColor(205, 31, 0, 255))) {
			shape->fill.color = packedColor(150, 0, 0, 255);
			shape->stroke.color = packedColor(150, 0, 0, 255);
		}
		if (((shape->fill.color == packedColor(0, 0, 0, 255)) || (shape->stroke.color == packedColor(0, 0, 0, 255))) && (!isScreen)) {
			shape->fill.color = packedColor(180, 180, 180, 255);
			shape->stroke.color = packedColor(180, 180, 180, 255);
		}
		if ((shape->fill.color == packedColor(230, 230, 230, 255)) || (shape->stroke.color == packedColor(230, 230, 230, 255))) {
			shape->fill.color = packedColor(0, 0, 0, 255);
			shape->stroke.color = packedColor(0, 0, 0, 255);
		}
		if ((shape->fill.color == packedColor(255, 255, 255, 255)) || (shape->stroke.color == packedColor(255, 255, 255, 255))) {
			shape->fill.color = packedColor(40, 40, 40, 255);
			shape->stroke.color = packedColor(40, 40, 40, 255);
		}
		if (shape->opacity<1) {
			shape->fill.color = packedColor(60, 60, 60, 255);
			shape->stroke.color = packedColor(60, 60, 60, 255);
			shape->opacity = 0.6f;
		}
	}
	blackPanel->setVisible(false);
	addChild(blackPanel);

	std::shared_ptr<Svg> buSvg;
	try {
		buSvg = std::make_shared<Svg>();
		buSvg->loadFile(filename);
	}
	catch (Exception& e) {
		WARN("%s", e.what());
		buSvg = NULL;
	}

	bluePanel = new SvgPanel;
	bluePanel->setBackground(buSvg);
	for (NSVGshape *shape = bluePanel->sw->svg->handle->shapes; shape != NULL; shape = shape->next) {
		std::string sId (shape->id);
		std::string sScreen ("screen");
		bool isScreen = sId.find(sScreen)!=std::string::npos;

		if ((shape->fill.color == packedColor(205, 31, 0, 255)) || (shape->stroke.color == packedColor(205, 31, 0, 255))) {
			shape->fill.color = packedColor(150, 0, 0, 255);
			shape->stroke.color = packedColor(150, 0, 0, 255);
		}
		if (((shape->fill.color == packedColor(0, 0, 0, 255)) || (shape->stroke.color == packedColor(0, 0, 0, 255))) && (!isScreen)) {
			shape->fill.color = packedColor(180, 180, 180, 255);
			shape->stroke.color = packedColor(180, 180, 180, 255);
		}
		if ((shape->fill.color == packedColor(230, 230, 230, 255)) || (shape->stroke.color == packedColor(230, 230, 230, 255))) {
			shape->fill.color = packedColor(19, 63, 84, 255);
			shape->stroke.color = packedColor(19, 63, 84, 255);
		}
		if ((shape->fill.color == packedColor(255, 255, 255, 255)) || (shape->stroke.color == packedColor(255, 255, 255, 255))) {
			shape->fill.color = packedColor(40, 40, 40, 255);
			shape->stroke.color = packedColor(40, 40, 40, 255);
		}
		if (shape->opacity<1) {
			shape->fill.color = packedColor(60, 60, 60, 255);
			shape->stroke.color = packedColor(60, 60, 60, 255);
			shape->opacity = 0.6f;
		}
	}
	bluePanel->setVisible(false);
	addChild(bluePanel);

	std::shared_ptr<Svg> gSvg;
	try {
		gSvg = std::make_shared<Svg>();
		gSvg->loadFile(filename);
	}
	catch (Exception& e) {
		WARN("%s", e.what());
		gSvg = NULL;
	}

	greenPanel = new SvgPanel;
	greenPanel->setBackground(gSvg);
	for (NSVGshape *shape = greenPanel->sw->svg->handle->shapes; shape != NULL; shape = shape->next) {
		std::string sId (shape->id);
		std::string sScreen ("screen");
		bool isScreen = sId.find(sScreen)!=std::string::npos;

		if ((shape->fill.color == packedColor(205, 31, 0, 255)) || (shape->stroke.color == packedColor(205, 31, 0, 255))) {
			shape->fill.color = packedColor(150, 0, 0, 255);
			shape->stroke.color = packedColor(150, 0, 0, 255);
		}
		if (((shape->fill.color == packedColor(0, 0, 0, 255)) || (shape->stroke.color == packedColor(0, 0, 0, 255))) && (!isScreen)) {
			shape->fill.color = packedColor(180, 180, 180, 255);
			shape->stroke.color = packedColor(180, 180, 180, 255);
		}
		if ((shape->fill.color == packedColor(230, 230, 230, 255)) || (shape->stroke.color == packedColor(230, 230, 230, 255))) {
			shape->fill.color = packedColor(19, 107, 80, 255);
			shape->stroke.color = packedColor(19, 107, 80, 255);
		}
		if ((shape->fill.color == packedColor(255, 255, 255, 255)) || (shape->stroke.color == packedColor(255, 255, 255, 255))) {
			shape->fill.color = packedColor(40, 40, 40, 255);
			shape->stroke.color = packedColor(40, 40, 40, 255);
		}
		if (shape->opacity<1) {
			shape->fill.color = packedColor(60, 60, 60, 255);
			shape->stroke.color = packedColor(60, 60, 60, 255);
			shape->opacity = 0.6f;
		}
	}
	greenPanel->setVisible(false);
	addChild(greenPanel);
}

void BidooWidget::step() {
	if (module) {
		if ((dynamic_cast<BidooModule*>(module)->loadDefault) && (dynamic_cast<BidooModule*>(module)->themeId == -1)) {
			dynamic_cast<BidooModule*>(module)->loadDefault = false;
			readThemeAndContrastFromDefault();
			dynamic_cast<BidooModule*>(module)->themeId = defaultPanelTheme;
			if (defaultPanelTheme == 0) {
				lightPanel->setVisible(true);
				darkPanel->setVisible(false);
				blackPanel->setVisible(false);
				bluePanel->setVisible(false);
				greenPanel->setVisible(false);
			}
			else if (defaultPanelTheme == 1) {
				lightPanel->setVisible(false);
				darkPanel->setVisible(true);
				blackPanel->setVisible(false);
				bluePanel->setVisible(false);
				greenPanel->setVisible(false);
			}
			else if (defaultPanelTheme == 2){
				lightPanel->setVisible(false);
				darkPanel->setVisible(false);
				blackPanel->setVisible(true);
				bluePanel->setVisible(false);
				greenPanel->setVisible(false);
			}
			else if (defaultPanelTheme == 3){
				lightPanel->setVisible(false);
				darkPanel->setVisible(false);
				blackPanel->setVisible(false);
				bluePanel->setVisible(true);
				greenPanel->setVisible(false);
			}
			else {
				lightPanel->setVisible(false);
				darkPanel->setVisible(false);
				blackPanel->setVisible(false);
				bluePanel->setVisible(false);
				greenPanel->setVisible(true);
			}
		}
		else if (dynamic_cast<BidooModule*>(module)->themeChanged) {
			dynamic_cast<BidooModule*>(module)->themeChanged = false;
			if (dynamic_cast<BidooModule*>(module)->themeId == 0) {
				lightPanel->setVisible(true);
				darkPanel->setVisible(false);
				blackPanel->setVisible(false);
				bluePanel->setVisible(false);
				greenPanel->setVisible(false);
			}
			else if (dynamic_cast<BidooModule*>(module)->themeId == 1) {
				lightPanel->setVisible(false);
				darkPanel->setVisible(true);
				blackPanel->setVisible(false);
				bluePanel->setVisible(false);
				greenPanel->setVisible(false);
			}
			else if (dynamic_cast<BidooModule*>(module)->themeId == 2){
				lightPanel->setVisible(false);
				darkPanel->setVisible(false);
				blackPanel->setVisible(true);
				bluePanel->setVisible(false);
				greenPanel->setVisible(false);
			}
			else if (dynamic_cast<BidooModule*>(module)->themeId == 3){
				lightPanel->setVisible(false);
				darkPanel->setVisible(false);
				blackPanel->setVisible(false);
				bluePanel->setVisible(true);
				greenPanel->setVisible(false);
			}
			else {
				lightPanel->setVisible(false);
				darkPanel->setVisible(false);
				blackPanel->setVisible(false);
				bluePanel->setVisible(false);
				greenPanel->setVisible(true);
			}
		}
	}
	else {
		readThemeAndContrastFromDefault();
		if (defaultPanelTheme == 0) {
			lightPanel->setVisible(true);
			darkPanel->setVisible(false);
			blackPanel->setVisible(false);
			bluePanel->setVisible(false);
			greenPanel->setVisible(false);
		}
		else if (defaultPanelTheme == 1) {
			lightPanel->setVisible(false);
			darkPanel->setVisible(true);
			blackPanel->setVisible(false);
			bluePanel->setVisible(false);
			greenPanel->setVisible(false);
		}
		else if (defaultPanelTheme == 2){
			lightPanel->setVisible(false);
			darkPanel->setVisible(false);
			blackPanel->setVisible(true);
			bluePanel->setVisible(false);
			greenPanel->setVisible(false);
		}
		else if (defaultPanelTheme == 3){
			lightPanel->setVisible(false);
			darkPanel->setVisible(false);
			blackPanel->setVisible(false);
			bluePanel->setVisible(true);
			greenPanel->setVisible(false);
		}
		else {
			lightPanel->setVisible(false);
			darkPanel->setVisible(false);
			blackPanel->setVisible(false);
			bluePanel->setVisible(false);
			greenPanel->setVisible(true);
		}
	}
	ModuleWidget::step();
}
