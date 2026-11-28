#include "plugin.hpp"
#include <iostream>
#include <set>

Plugin *pluginInstance;

// antN's station list (res/stations.json)
void antnLoadStations();

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
	p->addModel(modelSAISON);
	p->addModel(modelOUAIVE);
	p->addModel(modelEDSAROS);
	p->addModel(modelPOUPRE);
	p->addModel(modelMAGMA);
	p->addModel(modelOAI);
	p->addModel(modelCANARD);
	p->addModel(modelEMILE);
	p->addModel(modelPRISME);
	p->addModel(modelFORK);
	p->addModel(modelTIARE);
	p->addModel(modelANTN);
	antnLoadStations();
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
	p->addModel(modelREI);
	p->addModel(modelSOUNAIRE);
	p->addModel(modelCOPIA);
	p->addModel(modelFILTRE);
	p->addModel(modelACIDE);
	p->addModel(modelRICOCHET);
	p->addModel(modelDOSE);
	p->addModel(modelREGISTRE);
	p->addModel(modelPARTICULES);
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
	json_object_set_new(rootJ, "panelTheme", json_integer(themeId));
	return rootJ;
}

void BidooModule::dataFromJson(json_t *rootJ) {
	// the old "themeId" key is ignored: modules from older patches follow Rack
	json_t *themeIdJ = json_object_get(rootJ, "panelTheme");
	if (themeIdJ)
		themeId = json_integer_value(themeIdJ);
}

void BidooWidget::appendContextMenu(Menu *menu) {
	ModuleWidget::appendContextMenu(menu);
	BidooModule *m = dynamic_cast<BidooModule*>(module);
	if (!m)
		return;
	menu->addChild(new MenuSeparator());
	menu->addChild(createSubmenuItem("Theme", "", [=](ui::Menu* menu) {
		static const std::pair<int, const char*> themes[] = {
			{THEME_FOLLOW_RACK, "Follow Rack"}, {THEME_LIGHT, "Light"}, {THEME_DARK, "Dark"}, {THEME_BLACK, "Black"}};
		for (const auto& t : themes) {
			const int id = t.first;
			menu->addChild(createCheckMenuItem(t.second, "",
				[=]() { return m->themeId == id; },
				[=]() { setTheme(id); }));
		}
	}));
}

// Chosen from the menu: this module's theme and the default for new ones.
void BidooWidget::setTheme(int theme) {
	if (bidooModule)
		bidooModule->themeId = theme;
	defaultPanelTheme = theme;
	writeThemeAndContrastAsDefault();
}

unsigned int packedColor(int r, int g, int b, int a) {
	return r + (g << 8) + (b << 16) + (a << 24);
}

// The default theme lives in <user>/Bidoo.json. It is read from disk once and kept in this
// cache, shared by every Bidoo widget (module browser previews included), and refreshed
// whenever a theme is chosen from a context menu, so no widget touches the file per frame.
static bool defaultThemeCached = false;
static int defaultThemeCache = THEME_FOLLOW_RACK;

static void writeDefaultThemeFile(int theme) {
	json_t *settingsJ = json_object();
	json_object_set_new(settingsJ, "panelThemeDefault", json_integer(theme));
	std::string settingsFilename = asset::user("Bidoo.json");
	FILE *file = fopen(settingsFilename.c_str(), "w");
	if (file) {
		json_dumpf(settingsJ, file, JSON_INDENT(2) | JSON_REAL_PRECISION(9));
		fclose(file);
	}
	json_decref(settingsJ);
}

static int loadDefaultTheme() {
	if (defaultThemeCached)
		return defaultThemeCache;
	defaultThemeCached = true;
	// no file or no new key yet (old "themeDefault" ignored): follow Rack
	defaultThemeCache = THEME_FOLLOW_RACK;
	std::string settingsFilename = asset::user("Bidoo.json");
	FILE *file = fopen(settingsFilename.c_str(), "r");
	if (!file) {
		writeDefaultThemeFile(defaultThemeCache);
		return defaultThemeCache;
	}
	json_error_t error;
	json_t *settingsJ = json_loadf(file, 0, &error);
	fclose(file);
	if (!settingsJ) {
		writeDefaultThemeFile(defaultThemeCache);
		return defaultThemeCache;
	}
	json_t *themeDefaultJ = json_object_get(settingsJ, "panelThemeDefault");
	if (themeDefaultJ)
		defaultThemeCache = (int)json_integer_value(themeDefaultJ);
	json_decref(settingsJ);
	return defaultThemeCache;
}

void BidooWidget::writeThemeAndContrastAsDefault() {
	defaultThemeCached = true;
	defaultThemeCache = defaultPanelTheme;
	writeDefaultThemeFile(defaultPanelTheme);
}

void BidooWidget::readThemeAndContrastFromDefault() {
	defaultPanelTheme = loadDefaultTheme();
}

// Is the point inside the shape? Enough for a box: the corners of its curves, even-odd.
static bool pointInShape(const NSVGshape *shape, float x, float y) {
	if (x < shape->bounds[0] || x > shape->bounds[2] || y < shape->bounds[1] || y > shape->bounds[3])
		return false;
	bool in = false;
	for (const NSVGpath *path = shape->paths; path; path = path->next) {
		for (int i = 0; i < path->npts; i += 3) {
			const float *a = &path->pts[i * 2];
			const float *b = i + 3 < path->npts ? &path->pts[(i + 3) * 2] : &path->pts[0];
			if ((a[1] > y) != (b[1] > y) && x < a[0] + (y - a[1]) * (b[0] - a[0]) / (b[1] - a[1]))
				in = !in;
		}
	}
	return in;
}

// A white shape is a label on a box. Black boxes turn light grey in Dark and Black, so a label on one turns dark. What
// stays dark under a label keeps it white: red output boxes, screens, and the near-black boxes of the older panels.
static bool labelStaysWhite(const NSVGimage *image, const NSVGshape *label) {
	const float x = 0.5f * (label->bounds[0] + label->bounds[2]);
	const float y = 0.5f * (label->bounds[1] + label->bounds[3]);
	bool stays = false;
	// the shapes run from the back to the front: the last one under the label's centre is what it sits on
	for (const NSVGshape *shape = image->shapes; shape != label; shape = shape->next) {
		if (shape->fill.type != NSVG_PAINT_COLOR || shape->opacity < 1 || !pointInShape(shape, x, y))
			continue;
		// the same order as in themedPanel: a black or red stroke repaints the fill too
		const unsigned int c = shape->fill.color;
		const unsigned int red = packedColor(205, 31, 0, 255), black = packedColor(0, 0, 0, 255);
		const int r = c & 255, g = (c >> 8) & 255, b = (c >> 16) & 255, a = (c >> 24) & 255;
		if (c == red || shape->stroke.color == red)
			stays = true;
		else if (std::string(shape->id).find("screen") != std::string::npos)
			stays = r < 64 && g < 64 && b < 64;
		else if (c == black || shape->stroke.color == black)
			stays = false;
		else
			stays = r < 64 && g < 64 && b < 64 && a >= 200;
	}
	return stays;
}

// A panel for Dark or Black: the SVG again, with its colours swapped (see docs/THEME.md).
static SvgPanel *themedPanel(const std::string& filename, unsigned int panelColor, unsigned int pictureColor) {
	std::shared_ptr<Svg> svg;
	try {
		svg = std::make_shared<Svg>();
		svg->loadFile(filename);
	}
	catch (Exception& e) {
		WARN("%s", e.what());
		svg = NULL;
	}

	SvgPanel *panel = new SvgPanel;
	panel->setBackground(svg);
	NSVGimage *image = panel->sw->svg->handle;

	// decided first: the loop below repaints what the labels sit on
	std::set<const NSVGshape*> whiteLabels;
	for (const NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next) {
		if (shape->fill.color == packedColor(255, 255, 255, 255) && labelStaysWhite(image, shape))
			whiteLabels.insert(shape);
	}

	for (NSVGshape *shape = image->shapes; shape != NULL; shape = shape->next) {
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
			shape->fill.color = panelColor;
			shape->stroke.color = panelColor;
		}
		if (((shape->fill.color == packedColor(255, 255, 255, 255)) || (shape->stroke.color == packedColor(255, 255, 255, 255))) && !whiteLabels.count(shape)) {
			shape->fill.color = packedColor(40, 40, 40, 255);
			shape->stroke.color = packedColor(40, 40, 40, 255);
		}
		if (shape->opacity<1) {
			shape->fill.color = pictureColor;
			shape->stroke.color = pictureColor;
			shape->opacity = 0.6f;
		}
	}
	panel->setVisible(false);
	return panel;
}

void BidooWidget::prepareThemes(const std::string& filename) {
	readThemeAndContrastFromDefault();

	setPanel(APP->window->loadSvg(filename));
	lightPanel = dynamic_cast<SvgPanel*>(getPanel());

	darkPanel = themedPanel(filename, packedColor(50, 50, 50, 255), packedColor(80, 80, 80, 255));
	addChild(darkPanel);

	blackPanel = themedPanel(filename, packedColor(0, 0, 0, 255), packedColor(60, 60, 60, 255));
	addChild(blackPanel);
}

void BidooWidget::showTheme(int theme) {
	// Follow Rack: dark when Rack prefers dark panels; unknown ids (the removed blue and green) are light
	if (theme == THEME_FOLLOW_RACK)
		theme = settings::preferDarkPanels ? THEME_DARK : THEME_LIGHT;
	else if (theme < THEME_LIGHT || theme > THEME_BLACK)
		theme = THEME_LIGHT;
	if (theme == shownTheme)
		return;
	shownTheme = theme;
	lightPanel->setVisible(theme == THEME_LIGHT);
	darkPanel->setVisible(theme == THEME_DARK);
	blackPanel->setVisible(theme == THEME_BLACK);
}

void BidooWidget::step() {
	if (module != themeModule) {
		themeModule = module;
		bidooModule = dynamic_cast<BidooModule*>(module);
	}
	// the cached default, no file access per frame; previews (no module) show it too
	readThemeAndContrastFromDefault();
	if (bidooModule) {
		if (bidooModule->themeId == -1)
			bidooModule->themeId = defaultPanelTheme;
		showTheme(bidooModule->themeId);
	}
	else
		showTheme(defaultPanelTheme);
	ModuleWidget::step();
}

// The last sample folder: read once from <user>/Bidoo/folders.json, written when it changes (UI thread only).
static bool sampleFolderRead = false;
static std::string sampleFolder;

static std::string foldersFile() { return asset::user("Bidoo/folders.json"); }

std::string sampleDialogDir(const std::string &current) {
	if (!sampleFolderRead) {
		sampleFolderRead = true;
		if (json_t *j = json_load_file(foldersFile().c_str(), 0, NULL)) {
			json_t *s = json_object_get(j, "samples");
			if (json_is_string(s))
				sampleFolder = json_string_value(s);
			json_decref(j);
		}
	}
	if (!sampleFolder.empty() && system::isDirectory(sampleFolder))
		return sampleFolder;
	if (!current.empty()) {
		std::string d = system::getDirectory(current);
		if (system::isDirectory(d))
			return d;
	}
	return asset::user("");
}

void rememberSampleFolder(const std::string &path) {
	if (path.empty())
		return;
	std::string d = system::isDirectory(path) ? path : system::getDirectory(path);
	if (d.empty() || !system::isDirectory(d))
		return;
	sampleDialogDir();   // the file read first
	if (d == sampleFolder)
		return;
	sampleFolder = d;
	// the file's other keys kept
	json_t *j = json_load_file(foldersFile().c_str(), 0, NULL);
	if (!json_is_object(j)) {
		if (j)
			json_decref(j);
		j = json_object();
	}
	json_object_set_new(j, "samples", json_string(d.c_str()));
	system::createDirectories(asset::user("Bidoo"));
	json_dump_file(j, foldersFile().c_str(), JSON_INDENT(2));
	json_decref(j);
}
