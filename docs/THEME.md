# The Bidoo VCV look

Rules for a new module. The values live in `src/BidooTheme.hpp`; widgets use them as `theme::k...`, and
`tools/panels/panel_kit.py` reads the same file when it draws the panels, so change them only there.

The older modules have hand-drawn panels with their own positions. They move to the theme one at a time, keeping their
arrangement (see "Migrating an older module").

## Colours

| Colour | Value | Means | Constant |
|---|---|---|---|
| Yellow | `#FFE900` | controls and values on a screen | `kScreenYellow` (= `YELLOW_BIDOO`) |
| Pink | `#A4036F` | waveforms and spectra | `kWavePink` (= `PINK_BIDOO`) |
| Orange | `#F0622F` | lit panel buttons and states | `kLitOrange` |
| Off white | `#F6F4F8` | a panel button that is off | `kButtonWhite` |
| Black | `#000000` | screen background; input boxes; panel text | `kScreenBlack`, `kInputBox`, `kPanelText` |
| Red | `#CD1F00` | output boxes | `kOutputBox` (= `RED_BIDOO`) |
| White | `#FFFFFF` | labels in a box | `kBoxText` |
| Grey | `#E6E6E6` | the panel | `kPanelGrey` |
| Greys | `#9A9A9A`, `#626262`, `#4A4A4A` | keys and pads on a screen, off keys and tracks | `kKeyWhite`, `kKeyBlack`, `kKeyOff` |

A jack in a black box is an input, a jack in a red box is an output. Nothing else goes in a red box.

## Controls per size

| Control | Size | Label above its centre | Use |
|---|---|---|---|
| `BidooBlueKnob`, `BidooBlueSnapKnob` | 10 mm, 30 px | 22 px, size 8.5 | the main controls |
| `BidooSmallBlueKnob`, `BidooSmallSnapBlueKnob` | 8 mm, 24 px | 17 to 19 px, size 7.5 | dense rows: 40 to 46 px between rows |
| `BidooBlueTrimpot` | 6.5 mm, 19 px | none: it belongs to the knob above | attenuverters |
| `BidooRoundButton` | 20 px | as a small knob | on and off states; orange when lit |
| `CKSS` | | label above and below | two choices |
| `PJ301MPort` | 8 mm, 24 px | 15.39 px, size 7.5, in the box (centred, see Spacing) | every jack |

Use the shared classes; don't draw a new button or a new knob.

## Spacing

Measured in px from the top left of the panel; a panel is 380 high.

- Bottom jack row: y 342, its box 317 to 359. Same height as the older modules' bottom row.
- Second row: y 296, its box 271 to 313, or one tall box from 271 to 359 for both rows.
- A box is 25 above and 17 below its jack (`kBoxAboveJack`, `kBoxBelowJack`).
- A jack's caption sits centred in the space between the box's top edge (or, in the lower row of a tall box, the bottom of
  the jack above) and its jack's top: the same room above its capitals as below them, never squeezed against the jack.
  Under a box edge its baseline is `kBoxLabelAboveJack` (15.39) above the jack's centre: the jack's visible top is
  `kJackHalf` (11.25) above it and DejaVu's capitals at 7.5 are `kCaptionCapHeight` (5.47) tall, so
  11.25 + (25 - 11.25 - 5.47) / 2. In the lower row of a tall box use `theme::captionUnderJack(rowGap)`
  (`jack_label(..., jack_above=y)` in the scripts): 20.27 for rows 46 apart, 15.27 for sOUNAiRe's 36. Captions at 7 or 6.5
  use the same numbers (under 0.4 px off), so a row keeps one baseline.
- Screen: top 34, side margin 10, so its width is the panel's minus 20.
- Title: centred on the panel, baseline 25, size 20.
- Screws at 15 from each side, y 0 and 365: `theme::addScrews(this)`. On a 6 HP panel they would sit on the title and the logo, so dOSE puts them in the corners (x 0 and width - 15).

Boxes line up with the screen: the left edge of the first box and the right edge of the last are the screen's edges.
`panel_kit.box(x, y, w, h)` draws what it says, the stroke included, so x and x + w are the visible edges.
Spread the jack columns evenly between those edges, the same columns on both rows.

Knob columns line up with the outer jacks: the first and last knob sit over the first and last jack.

Use the full height: spread the knob rows evenly between the screen (or the title) and the second row's box, rather than
leaving a gap above the jacks. If it doesn't fit, make the screen taller or use small knobs. A row that really can't be at
296 keeps the box rule around its own height, as sOUNAiRe does with its upper row at 306.

Exceptions already there, from before these rules: aCiDe's knobs (27, 123) sit inside its outer jacks (21, 129), rICOcHEt's too
(30, 180 and 20.9, 189.1); rICOcHEt's screen is at margin 6, top 36, with the title at 26. Don't copy them.

Where a value belongs to one module (its knob rows, its screen's height, its columns), it stays in that module's widget
and its script, with a comment on the script's line saying where the other copy is.

## The grid

New modules place everything on a grid: no pixel numbers in a widget or a panel script, only grid indices. The functions
are in `src/BidooTheme.hpp` and `panel_kit.py` reads the same lines, so the widget and its panel can't disagree.

```
  y   row k = 342 - 46 k                 x   column i of n, across W - 20 px between the side margins:
  66  ── row 6                               col(i, n, W) = 10 + (W - 20) (i + 0.5) / n
 112  ── row 5
 158  ── row 4                     10                                           W - 10
 204  ── row 3                      |  col 0  |  col 1  |  col 2  |  col 3  |    (n = 4)
 250  ── row 2                      |<-------- one box: boxLeft(0) .. boxRight(1) -->|
 296  ── row 1  (second row, box 271..313)
 342  ── row 0  (bottom jacks, box 317..359)
```

- Rows count from the bottom, 46 px apart: a big knob with its label, or a jack in its box with 4 px to the next box.
  Half rows (k + 0.5) only when a module can't fit otherwise.
- Columns: a row picks its n; rows may differ. A fractional i (1.5) puts a control between two columns.
- Fixed places: title baseline 25, screen top 34, screws through `theme::addScrews`.

| C++ (`theme::`) | Python (`panel_kit.`) | Gives |
|---|---|---|
| `row(k)` | `row(k)` | the y of row k |
| `col(i, n, w)` | `col(i, n, w)` | the x of column i of n |
| `at(i, n, k, w)` | | a `theme::Pos` at column i, row k (see "Placing controls in code") |
| `jackCol(i, n, w)`, `jackAt(i, n, k, w)` | `jack_col(i, n, w)` | a jack in its grid box: the box's centre. Outer boxes reach the screen's edge, so they are wider than the inner ones and their centre is 1 px outside the column; inner columns give `col` |
| `boxLeft(i, n, w)`, `boxRight(i, n, w)` | `box_left`, `box_right` | a box's edges over column i: the screen's edge on the outside, 2 px from the column's edge inside (4 px between boxes) |
| `boxTop(k)`, `boxBottom(k)` | `box_top`, `box_bottom` | 25 above and 17 below row k |
| `captionAboveJack(room)`, `captionUnderJack(rowGap)` | `caption_under_jack`, `jack_label(..., jack_above=y)` | a jack caption's baseline above the jack's centre, centred under an edge `room` px above the jack, or under the jack `rowGap` px above (a tall box's lower row) |
| `jackBox(i0, i1, n, k, w, kLow)` | `grid_box(i0, i1, n, k, w, colour, k_low)` | the box over columns i0 to i1, row k (down to row kLow for a tall box) |
| `screenBottom(k)` | `screen_bottom(k)` | a screen ending 4 px above row k's boxes |
| `screenAboveKnobs(k)` | `screen_above_knobs(k)` | a screen ending above row k's knob labels |

Labels sit at the fixed offsets above their grid point: `kKnobLabelAbove` (22) for a big knob, `kSmallLabelAbove` (18) for a
small knob or a round button, `kBoxLabelAboveJack` (15.39) for a jack in a box (`captionUnderJack` in a tall box's lower row). In a widget:

```cpp
const int n = 4;
auto at = [&](float i, int k) { return theme::at(i, n, k, box.size.x); };
addInput(theme::input<PJ301MPort>(at(0, 0), module, NAME::IN_INPUT));
```

## Placing controls in code

Every widget places its controls, jacks, lights and screens the same way, through `theme::` in `src/BidooTheme.hpp`,
whatever its panel. A position is a centre (`theme::Pos`), and the code says where it comes from:

| Where from | Call | Use |
|---|---|---|
| the grid | `theme::at(i, n, k, w)`, `theme::jackAt(i, n, k, w)` | column i of n, row k, on a panel w px wide; half steps allowed. A jack inside a grid box uses `jackAt` (and its caption `jack_col`), so it sits in the middle of its box |
| another control | `a.right(dx)`, `a.down(dy)`, `a.offset(dx, dy)`, `theme::sameCol(a, y)`, `theme::sameRow(a, x)`, `theme::colRow(a, b)` | same column, same row, or a named step from a control already placed; `colRow` takes the column of `a` and the row of `b` |
| specific | `theme::specific(x, y)`, `theme::specificCorner(left, top, size)`, `theme::atCorner(widget, left, top)` | off the grid on purpose: a centre in px, or an older module's top left corner with the control's size (`theme::kJackSize`, `kTinyJackSize`, `kTrimpotSize`, `kCKD6Size`), or with the widget's own size (`atCorner`) |
| a rectangle | `theme::jackBox(...)`, `theme::screen(k, w)`, `theme::specificRect(x, y, w, h)` | a box or a screen from grid spans, or a specific one; assign it to `box` |

Then one of `theme::param<T>`, `theme::input<T>`, `theme::output<T>`, `theme::light<T>` (they call `createParamCentered`
and friends), or `theme::place(widget, pos)` for any other widget. Screws: `theme::addScrews(this)`.

```cpp
const theme::Pos slices = theme::specificCorner(58, 250, theme::kTrimpotSize);   // the old panel's place, to the pixel
const theme::Pos speed = slices.down(kSpeedBelow);                              // the row below
addParam(theme::param<BidooBlueTrimpot>(slices, module, OUAIVE::NB_SLICES_PARAM));
addParam(theme::param<BidooBlueTrimpot>(slices.right(kCvAmountRight), module, OUAIVE::CVSLICES_PARAM));
addParam(theme::param<BidooBlueTrimpot>(speed, module, OUAIVE::SPEED_PARAM));
addInput(theme::input<PJ301MPort>(theme::at(0, 4, 0, box.size.x), module, NAME::IN_INPUT));   // on the grid
```

Rules:

- Use the grid when a position is on it. Otherwise say it's specific: a reader must see at once which positions follow
  the system and which don't, so a pixel number appears only inside `specific`, `specificCorner` or `specificRect`,
  or as a named step.
- Place a group from one specific anchor and relative steps, and name the steps (`kCvAmountRight`, `kSpeedBelow`): one
  anchor per group, not one number per control. Use `colRow` when two controls share a column or a row on purpose.
- An older module keeps its panel to the pixel: its controls keep their old corners through `specificCorner`, even when
  one is within a pixel of a grid point. Moving it onto the grid changes the look; that is a migration, decided by the
  user, not a code change.
- In a new module, override only where the grid can't hold it (a control inside a screen, a detail of the picture, a
  panel too narrow for the columns), with a comment on the line saying why.
- `atCorner(widget, left, top)` keeps an older module's corner for a widget whose size comes from its own SVG or
  constructor (a button, a slider, a light bezel, a display), where no `k...Size` exists: create it at `Vec()`, then
  `addParam(theme::atCorner(createParam<T>(Vec(), module, id), left, top))`.
- `place()` makes a centre from `specificCorner` give back the exact corner: `centre - size / 2` in floats can miss it by
  one bit, and that shows as a one-level difference in the antialiasing. If a control's SVG changes size, change its
  `k...Size` with it.

`at()`, `jackBox()` and `screen()` use the same expressions as the panel scripts' `col`, `row` and `grid_box`, so a
widget on the grid and its panel can't disagree. OUAIve (`src/OUAIVE.cpp`) is the example of an older module.

## Alignment

For every module, new or migrated:

- Controls, labels and jacks that are close to a common line share it exactly: one x for a column (a knob, its trimpot,
  its CV jack, a box label and its jack), one y for a row (its knobs, its jacks, and its labels on one baseline, even when
  the controls differ in size).
- A label is centred over its control or jack, at the theme's offset.
- Box edges line up with each other and with the screen's edges when they share a column or a span.
- Close means within about 6 px: move them onto the shared line, unless it changes an older module's arrangement. When two
  things can't share a line, say so in the script.

`python tools/panels/make_all.py --align` lists the near-misses of the generated panels: label centres, baselines and
box or screen edges between 0.05 and 6 px apart. It sees what the scripts draw, not the widgets' controls; check those
against the labels by eye, or by placing both from the grid.

## Migrating an older module

A migration keeps the module's arrangement: its controls stay where they are, in the same order and groups, so it still
looks like itself. Only these follow the theme:

- ports, buttons and knobs: the shared classes (`PJ301MPort` wherever a full jack fits, `BidooRoundButton` for a button,
  keeping it momentary or latched as it was);
- boxes: inputs in black boxes, outputs in red ones, white labels, edges as the grid computes them;
- the bottom row on 342 (box 317 to 359), and other rows on grid rows where they fit without changing the look;
- the screen, grown into the room the new layout frees;
- colours, title, screws, themes, and the picture at opacity 0.08 (the old picture is kept when it has one).

The same width, param, input, output and light IDs, and nothing that plays sound changes. The panel becomes a script in
`tools/panels/`; parts of the old drawing it keeps (a picture, icons) go in a file next to it (`ouaive_parts.svg`).
The full grid is for new modules, and for an older one only when the user decides to rebuild it.

## The panel

- Text is outlines of DejaVu Sans, not text elements, so it looks the same everywhere.
- Behind everything, one picture linked to the module's name: aCiDe a lemon, FiLTRe a coffee filter, cOPIA a photocopier,
  sOUNAiRe an accordion, rICOcHEt a stone skipping on water, sAiSOn a tree through the seasons, dOSE a syringe. It is stretched over the
  whole panel, mangled (slices sliding sideways, warps), black at opacity 0.08 (`kPictureOpacity`). It must stay readable
  once you know what it is, but must never compete with the labels.
- The logo at the bottom comes from sAiSOn's panel: `panel_kit.logo(W)`.

## Light, Dark and Black

Every module has the three themes, through `BidooWidget`: derive from `BidooModule` and `BidooWidget`, and call
`prepareThemes(asset::plugin(pluginInstance, "res/NAME.svg"))` first in the widget. Dark and Black are made at load time by
finding exact colours in the SVG, so a panel only follows if it uses them:

- `#E6E6E6` panel, `#000000` text and input boxes, `#FFFFFF` box labels, `#CD1F00` output boxes. A near value stays as it is
  and looks wrong in Dark.
- A shape whose id contains `screen` keeps its black. Anything else black turns light grey.
- A shape with opacity under 1 is taken for the picture and repainted for the theme.
- `#FFFFFF` follows what's under the middle of the shape: on a black box, which turns light grey, it turns dark; on what stays
  dark (a red box, a screen, a near-black box of an older panel) it stays white. Draw the box before its label, or the label
  takes its colour from what's behind the box.

A screen drawn by the widget does not change with the theme: black, yellow and pink in all three.

## Adding a module

1. Copy the closest script in `tools/panels/` to `tools/panels/name.py`: its `build()` draws the picture and lays out the
   labels with `panel_kit`. Add it to `PANELS` in `make_all.py`.
2. `python tools/panels/make_all.py` writes `res/NAME.svg`. The font comes from `RACK_DIR`, or `res/fonts` of the Rack folder
   holding this plugin, or `G:/RackV2/res/fonts`. It needs `fontTools` (`pip install fonttools`).
3. In the widget, use `theme::k...` for what is shared and local constants for the rest.
4. Add the module to `plugin.json`, `plugin.hpp` and `plugin.cpp`.

`python tools/panels/make_all.py --align` lists near-misses (see "Alignment").

`python tools/panels/make_all.py --check` writes nothing and says whether `res/` still matches the scripts and the header.
Run it after changing a value in `BidooTheme.hpp`, and rebuild: scripts and widgets both follow.

## Testing

- `make dist`, then screenshots: `Rack.exe -t 2 -u <folder>`, with the `.vcvplugin` in that folder's `plugins-win-x64`. Read
  every picture you changed. The log must load the module with no warning.
- Look at it in Light, Dark and Black.
- A screenshot shows the browser preview, where `module` is null and a screen draws fake data. That is not a test of the screen.
  A screen with data needs the real load path: load a real file through the module's own menu or a saved patch, run it with
  real audio, and look at what it draws.
- Check that the jacks are inside their boxes and every label is above its control, at 100 % zoom.
