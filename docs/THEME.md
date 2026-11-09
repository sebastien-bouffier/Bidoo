# The Bidoo VCV look

Rules for a new module. The values live in `src/BidooTheme.hpp`; widgets use them as `theme::k...`, and
`tools/panels/panel_kit.py` reads the same file when it draws the panels, so change them only there.

The older modules have hand-drawn panels with their own positions. These rules don't apply to them until someone redraws them.

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
| `PJ301MPort` | 8 mm, 24 px | 13 px, size 7.5, in the box | every jack |

Use the shared classes; don't draw a new button or a new knob.

## Spacing

Measured in px from the top left of the panel; a panel is 380 high.

- Bottom jack row: y 342, its box 317 to 359. Same height as the older modules' bottom row.
- Second row: y 296, its box 271 to 313, or one tall box from 271 to 359 for both rows.
- A box is 25 above and 17 below its jack (`kBoxAboveJack`, `kBoxBelowJack`); its label's baseline is 13 above the jack.
- Screen: top 34, side margin 10, so its width is the panel's minus 20.
- Title: centred on the panel, baseline 25, size 20.
- Screws at 15 from each side, y 0 and 365: `theme::addScrews(this)`.

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

## The panel

- Text is outlines of DejaVu Sans, not text elements, so it looks the same everywhere.
- Behind everything, one picture linked to the module's name: aCiDe a lemon, FiLTRe a coffee filter, cOPIA a photocopier,
  sOUNAiRe an accordion, rICOcHEt a stone skipping on water, sAiSOn a tree through the seasons. It is stretched over the
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
