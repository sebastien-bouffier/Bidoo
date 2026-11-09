// The shared look of the Bidoo VCV modules: what each colour means and where the common rows are. Rules in docs/THEME.md.
//
// This file is the only source of these values: tools/panels/panel_kit.py reads them from here when it draws the panels,
// so that a panel and its widget cannot disagree. Keep each constant on its own line in the form used below.
// Include it after plugin.hpp (BidooComponents.hpp does).
#pragma once

namespace theme {

// ---- Colours on screens and buttons
const NVGcolor kScreenBlack = nvgRGB(0x00, 0x00, 0x00);  // the background of a screen
const NVGcolor kScreenYellow = YELLOW_BIDOO;             // controls and values
const NVGcolor kWavePink = PINK_BIDOO;                   // waveforms and spectra
const NVGcolor kLitOrange = nvgRGB(0xF0, 0x62, 0x2F);    // lit panel buttons and states
const NVGcolor kButtonWhite = nvgRGB(0xF6, 0xF4, 0xF8);  // a panel button that is off
const NVGcolor kKeyWhite = nvgRGB(0x9A, 0x9A, 0x9A);     // keys and pads on a screen
const NVGcolor kKeyBlack = nvgRGB(0x62, 0x62, 0x62);
const NVGcolor kKeyOff = nvgRGB(0x4A, 0x4A, 0x4A);       // keys out of range, tracks

// ---- Colours of the panel SVG. BidooWidget::prepareThemes finds these exact values to make Dark and Black
const NVGcolor kPanelGrey = nvgRGB(0xE6, 0xE6, 0xE6);
const NVGcolor kPanelText = nvgRGB(0x00, 0x00, 0x00);
const NVGcolor kBoxText = nvgRGB(0xFF, 0xFF, 0xFF);
const NVGcolor kInputBox = nvgRGB(0x00, 0x00, 0x00);
const NVGcolor kOutputBox = RED_BIDOO;
const NVGcolor kScreenFrame = nvgRGB(0xA0, 0xA0, 0xA0);

// ---- Jack rows (y of the jack's centre, px)
static constexpr float kBottomRowY = 342.f;
static constexpr float kSecondRowY = 296.f;
// createInput() and createOutput() take the top left corner, not the centre: the older modules' bottom row
static constexpr float kBottomRowTop = 330.f;

// ---- A box around a jack, measured from the jack's centre; its label sits inside, above the jack
static constexpr float kBoxAboveJack = 25.f;
static constexpr float kBoxBelowJack = 17.f;
static constexpr float kBoxLabelAboveJack = 13.f;
static constexpr float kBottomBoxTop = kBottomRowY - kBoxAboveJack;      // 317
static constexpr float kBottomBoxBottom = kBottomRowY + kBoxBelowJack;   // 359
static constexpr float kSecondBoxTop = kSecondRowY - kBoxAboveJack;      // 271
static constexpr float kSecondBoxBottom = kSecondRowY + kBoxBelowJack;   // 313, or one tall box down to 359

// ---- Panel
static constexpr float kSideMargin = 10.f;      // a screen's left and right edges; the boxes line up with them
static constexpr float kScreenTop = 34.f;
static constexpr float kTitleY = 25.f;          // baseline of the title, centred on the panel
static constexpr float kTitleSize = 20.f;
static constexpr float kKnobLabelAbove = 22.f;  // a label's baseline above the centre of a normal knob
static constexpr float kKnobLabelSize = 8.5f;
static constexpr float kBoxLabelSize = 7.5f;    // 7 or 6.5 where the columns are narrow
static constexpr float kPictureOpacity = 0.08f;
static constexpr float kScrewX = 15.f;          // from either side
static constexpr float kScrewBottomY = 365.f;

// The four silver screws in their usual places. Call after prepareThemes, which sets the panel's width.
inline void addScrews(ModuleWidget *w) {
	w->addChild(createWidget<ScrewSilver>(Vec(kScrewX, 0)));
	w->addChild(createWidget<ScrewSilver>(Vec(w->box.size.x - 2.f * kScrewX, 0)));
	w->addChild(createWidget<ScrewSilver>(Vec(kScrewX, kScrewBottomY)));
	w->addChild(createWidget<ScrewSilver>(Vec(w->box.size.x - 2.f * kScrewX, kScrewBottomY)));
}

} // namespace theme
