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
// A jack's caption is centred in the room between what is above it (the box's top edge, or the jack above in a tall box)
// and the jack's top: equal space above and below its capitals (docs/THEME.md, "Spacing")
static constexpr float kJackHalf = 11.25f;           // the jack's visible top above its centre: PJ301M's outer ring is 22.5 px
                                                     // across in its 23.7 px SVG (kJackSize.h / 2 = 11.85 would leave the
                                                     // caption 0.6 px closer to the box edge than to the jack, measured)
static constexpr float kCaptionCapHeight = 5.47f;    // DejaVu capitals, 1493 / 2048 x 7.5 (7 and 6.5 differ by under 0.3 px)
static constexpr float kBoxLabelAboveJack = 15.39f;  // 11.25 + (25 - 11.25 - 5.47) / 2: a caption under the box's top edge
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
static constexpr float kSmallLabelAbove = 18.f; // a label above a small knob or a round button
static constexpr float kBoxLabelSize = 7.5f;    // 7 or 6.5 where the columns are narrow
static constexpr float kPictureOpacity = 0.08f;
static constexpr float kScrewX = 15.f;          // from either side
static constexpr float kScrewBottomY = 365.f;

// ---- The grid: where controls and jacks go. Rows are counted from the bottom, columns split the width between the
// side margins (docs/THEME.md, "The grid"). panel_kit.py reads these functions from here, so each one stays on one line,
// in the form `static constexpr float name(args) { return expression; }`, with nothing C++-only in the expression.
static constexpr float kRowPitch = 46.f;        // a big knob and its label, or a jack in its box with 4 px to the next box
static constexpr float kBoxGap = 4.f;           // between two boxes, across and down
static constexpr float row(float k) { return kBottomRowY - kRowPitch * k; }
static constexpr float colPitch(int n, float widthPx) { return (widthPx - 2.f * kSideMargin) / n; }
static constexpr float col(float i, int n, float widthPx) { return kSideMargin + colPitch(n, widthPx) * (i + 0.5f); }
static constexpr float boxLeft(float i, int n, float widthPx) { return kSideMargin + colPitch(n, widthPx) * i + kBoxGap * 0.5f * (i > 0); }
static constexpr float boxRight(float i, int n, float widthPx) { return kSideMargin + colPitch(n, widthPx) * (i + 1.f) - kBoxGap * 0.5f * (i < n - 1); }
static constexpr float jackCol(float i, int n, float widthPx) { return 0.5f * (boxLeft(i, n, widthPx) + boxRight(i, n, widthPx)); }   // a boxed jack: its box's centre (outer boxes reach the screen's edge)
static constexpr float boxTop(float k) { return row(k) - kBoxAboveJack; }
static constexpr float boxBottom(float k) { return row(k) + kBoxBelowJack; }
static constexpr float screenBottom(float k) { return boxTop(k) - kBoxGap; }
static constexpr float kLabelRoom = 10.f;      // a knob label's capitals and the gap above them
static constexpr float screenAboveKnobs(float k) { return row(k) - kKnobLabelAbove - kLabelRoom; }   // a screen ending above row k's knob labels
static_assert(row(1) == kSecondRowY, "the second row is row 1 of the grid");
// a caption's baseline above its jack's centre, when what is above it (a box edge, or the bottom of the jack above) is
// roomAbove px above that centre. A box edge gives kBoxLabelAboveJack; the lower row of a tall box gives its own
static constexpr float captionAboveJack(float roomAbove) { return kJackHalf + (roomAbove - kJackHalf - kCaptionCapHeight) * 0.5f; }
static constexpr float captionUnderJack(float rowGap) { return captionAboveJack(rowGap - kJackHalf); }   // the lower row of a tall box, rowGap px under the upper row
static_assert(captionAboveJack(kBoxAboveJack) - kBoxLabelAboveJack < 0.01f && kBoxLabelAboveJack - captionAboveJack(kBoxAboveJack) < 0.01f, "kBoxLabelAboveJack is the caption under a box's top edge");


// ---- Placing controls in code (docs/THEME.md, "Placing controls in code")
// Every position is a centre. Each one comes from one of three places, and the code says which:
//   at(i, n, k, w)                    on the grid
//   a.right(dx), sameCol(a, y), ...   relative to a control already placed
//   specific(x, y), specificCorner()  off the grid on purpose: an older panel, or a module that needs it
// and goes to theme::param, input, output or light (createParamCentered and friends, see below), or to place().
// Positions are constexpr arithmetic on a few floats; nothing runs but the placing itself.

struct Pos {
	float x, y;                     // the centre
	float left = 0.f, top = 0.f;    // specificCorner's corner, kept exact (see place())
	bool hasLeft = false, hasTop = false;
	constexpr Pos right(float dx) const { Pos p = *this; p.x += dx; p.left += dx; return p; }   // a negative dx goes left
	constexpr Pos down(float dy) const { Pos p = *this; p.y += dy; p.top += dy; return p; }     // a negative dy goes up
	constexpr Pos offset(float dx, float dy) const { return right(dx).down(dy); }
	operator Vec() const { return Vec(x, y); }
};

struct Size {
	float w, h;
};

// A rectangle (a screen, a box), by its top left corner and its size
struct Area {
	float x, y, w, h;
	constexpr Pos centre() const { return {x + w * 0.5f, y + h * 0.5f}; }
	operator Rect() const { return Rect(Vec(x, y), Vec(w, h)); }
};

// On the grid: column i of n, row k. Half steps (1.5) allowed
constexpr Pos at(float i, int n, float k, float widthPx) { return {col(i, n, widthPx), row(k)}; }
constexpr Pos jackAt(float i, int n, float k, float widthPx) { return {jackCol(i, n, widthPx), row(k)}; }   // a jack in its grid box

// Relative to controls already placed
constexpr Pos colRow(Pos colOf, Pos rowOf) {   // the column of one, the row of another
	return {colOf.x, rowOf.y, colOf.left, rowOf.top, colOf.hasLeft, rowOf.hasTop};
}
constexpr Pos sameCol(Pos a, float y) { return colRow(a, Pos{0.f, y}); }
constexpr Pos sameRow(Pos a, float x) { return colRow(Pos{x, 0.f}, a); }

// Specific: a position that does not follow the grid, said so. A centre, in px from the panel's top left
constexpr Pos specific(float x, float y) { return {x, y}; }
// An older module's position, kept to the pixel: the control's top left corner as the old code had it, and its size
constexpr Pos specificCorner(float left, float top, Size s) {
	return {left + s.w * 0.5f, top + s.h * 0.5f, left, top, true, true};
}
constexpr Area specificRect(float x, float y, float w, float h) { return {x, y, w, h}; }

// The sizes Rack measures for the shared controls' SVGs, for specificCorner. Change them with the SVG
static constexpr Size kJackSize = {23.7f, 23.7f};                    // PJ301MPort
static constexpr Size kTinyJackSize = {15.f, 15.f};                  // TinyPJ301MPort
static constexpr Size kTrimpotSize = {19.1929131f, 19.1929131f};     // BidooBlueTrimpot
static constexpr Size kCKD6Size = {28.f, 27.9958782f};               // BlueCKD6
static constexpr Size kBlueKnobSize = {29.5275591f, 29.5275591f};  // BidooBlueKnob (10 mm at Rack's 75 dpi)
static constexpr Size kLargeKnobSize = {37.5f, 37.5f};               // BidooLargeBlueKnob (12.7 mm)
static constexpr Size kHugeKnobSize = {56.1023622f, 56.1023622f};   // BidooHugeBlueKnob (19 mm)

// Puts a widget's centre on p. A centre made from a corner (specificCorner, then moved) gives the corner back exactly
// when the widget is the size the corner was given for: centre - size / 2 in floats can miss it by one bit, and that
// shows as a one-level difference in the antialiasing. For a widget of another size the centre is used.
template <class TWidget>
TWidget *place(TWidget *w, Pos p) {
	const Vec half = w->box.size.div(2.f);
	w->box.pos.x = (p.hasLeft && std::fabs(p.left + half.x - p.x) < 1e-3f) ? p.left : p.x - half.x;
	w->box.pos.y = (p.hasTop && std::fabs(p.top + half.y - p.y) < 1e-3f) ? p.top : p.y - half.y;
	return w;
}

// An older module's widget kept at its old top left corner, whatever its size: for widgets whose size comes from their
// own SVG or constructor (buttons, sliders, light bezels, displays), so no k...Size is needed. Returns the widget.
template <class TWidget>
TWidget *atCorner(TWidget *w, float left, float top) {
	return place(w, specificCorner(left, top, Size{w->box.size.x, w->box.size.y}));
}

// Centred creation, the only way a widget places its controls: createParamCentered and friends, through place()
template <class T> T *param(Pos p, engine::Module *m, int id) { return place(createParamCentered<T>(p, m, id), p); }
template <class T> T *input(Pos p, engine::Module *m, int id) { return place(createInputCentered<T>(p, m, id), p); }
template <class T> T *output(Pos p, engine::Module *m, int id) { return place(createOutputCentered<T>(p, m, id), p); }
template <class T> T *light(Pos p, engine::Module *m, int id) { return place(createLightCentered<T>(p, m, id), p); }

// From grid spans: the box around the jacks of columns i0 to i1 of n, on row k, or on rows k down to kLow (one tall box)
constexpr Area jackBox(float i0, float i1, int n, float k, float widthPx, float kLow = -1.f) {
	return {boxLeft(i0, n, widthPx), boxTop(k), boxRight(i1, n, widthPx) - boxLeft(i0, n, widthPx),
		boxBottom(kLow < 0.f ? k : kLow) - boxTop(k)};
}
// The screen: from the title to 4 px above row k's boxes, between the side margins
constexpr Area screen(float k, float widthPx) {
	return {kSideMargin, kScreenTop, widthPx - 2.f * kSideMargin, screenBottom(k) - kScreenTop};
}

// The four silver screws in their usual places. Call after prepareThemes, which sets the panel's width.
inline void addScrews(ModuleWidget *w) {
	w->addChild(createWidget<ScrewSilver>(Vec(kScrewX, 0)));
	w->addChild(createWidget<ScrewSilver>(Vec(w->box.size.x - 2.f * kScrewX, 0)));
	w->addChild(createWidget<ScrewSilver>(Vec(kScrewX, kScrewBottomY)));
	w->addChild(createWidget<ScrewSilver>(Vec(w->box.size.x - 2.f * kScrewX, kScrewBottomY)));
}

} // namespace theme
