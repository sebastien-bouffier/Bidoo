# πlOT's panel (30 HP), migrated from the hand-drawn one: the same arrangement (left, MORPHING with the CURVE button,
# SPEED and the curve screen over its four trimpots, BANK, then the SEQUENCER row; in the middle, the top and bottom
# scenes, each a screen, a trimpot, its buttons and their tiny CV jacks, the big morph knob between them with the go to
# top and go to bottom buttons; right, the 4 x 4 knobs with their type and voltage buttons in the white control area,
# the 16 CV outputs and POLY under it), now in the theme's screens, boxes, type and colours, with the old picture kept
# (pilot_parts.svg). The positions are the same in src/PILOT.cpp (PILOTWidget).
import os, re
import panel_kit as k

PW = 450
W = PW * k.K
OLD_W = 152.39999                      # the old panel's width in its mm units (pilot_parts.svg's viewBox)
LINE = 0.26458                         # the old lines' width, in units
TRIM_HALF = 19.1929131 / 2             # BidooBlueTrimpot
KNOB_HALF = 29.5275591 / 2             # BidooBlueKnob
CAP = k.CAP_HEIGHT * k.KNOB_LABEL_SIZE # a label's capitals at 8.5

# Left: CURVE and SPEED on the SPEED knob's row (CURVE's button and SPEED's CV jack were 0.76 and 1.26 px higher);
# SPEED and its CV jack 2 px right of their old corners (40, 80), so its label clears CURVE's by 4 px
SPEED_ROW = 60 + KNOB_HALF             # src/PILOT.cpp speed
CURVE_X = 15 + 9                       # the LED button's old centre (corner 15, 18 px)
SPEED_X = 42 + KNOB_HALF
SPEED_CV_X = 82 + 7.5
CURVE_SCREEN = (10.0, 96.0, 90.0, 90.0)   # the old screen (10.16, 96.03, 89.65 square), 6 px around the curve display
                                          # (16, 102, 78 x 78 in src/PILOT.cpp)
CURVE_TRIM_Y = 192 + TRIM_HALF         # AX, AY, BX, BY: old corners (10 + 24 i, 192), on their CV jacks' columns (0.1 px)
CURVE_CV_Y = 215 + 7.5                 # their tiny CV jacks, old corners (12 + 24 i, 215)
CURVE_COLS = tuple(12 + 7.5 + 24 * i for i in range(4))
BANK_ROW = 250 + KNOB_HALF             # the bank knob, old corner (46, 250); its CV jack on its row (0.26 px lower)
BANK_X, BANK_CV_X = 46 + KNOB_HALF, 84 + 7.5
BANK_SCREEN = (10.0, BANK_ROW - 9, 23.6, 18.0)   # the old one (10.16, 256.35, 23.4 x 17.3), centred on the knob's row
GROUP_LEFT, GROUP_RIGHT = 10.0, 100.0  # MORPHING's and BANK's brackets: the screen's edges (they were 9.6 and 100.4)
MORPHING_Y, BANK_Y = 35.2, 247.3       # the headings' baselines, as before

# The sequencer: its controls on the LENGTH knob's row (TYPE, WAIT, TRIG and REC were 0.76 px higher); TRIG and RESET's
# jacks now full jacks in boxes on the bottom row (they were tiny, 17.5 px higher); the move type and length screens
# under TYPE and LENGTH, their bottoms on the boxes' (they were 0.85 px lower)
SEQ_ROW = 310 + KNOB_HALF              # src/PILOT.cpp seq
SEQ_X = {'TYPE': 15 + 14, 'LENGTH': 55 + KNOB_HALF, 'WAIT': 99 + 9, 'TRIG': 135 + 14, 'REC': 259 + 9}
TRIG_JACK_X, RESET_JACK_X = 175 + 7.5, 215 + 7.5   # the old tiny jacks' columns
BOX_W = 28.0                           # the bottom row's boxes: RESET at 7.5 is 24 px wide
SEQ_SCREENS = ((10.0, k.BOTTOM_ROW, 38.0, 17.0), (SEQ_X['LENGTH'] - 11.75, k.BOTTOM_ROW, 23.5, 17.0))
SEQUENCER_Y = 289.0                    # the heading: its rule where it was (285.8), 2.1 px higher, clear of the labels under it
SEQUENCER_RIGHT = 278.0                # its bracket's right end (278.3), over REC

# The scenes: the trimpot's column for the screen's trimpot, its CV jack, the morph CV jack and the two go to buttons
# (the jacks were 0.1 and 1.1 px left, the buttons 0.6 px left); the buttons' columns for their jacks (0.5 px right);
# the bottom scene 161 px under the top one, as before
SCENE_X = 119 + TRIM_HALF              # src/PILOT.cpp sceneCol
SCENE_BTN_X = (150.0, 170.0, 190.0)    # SAVE / RND / FULL RND over minus / plus / RND, old corners 142, 162, 182
SCENE_TOP = 29.75                      # the top scene's screen and the white area's top (29.74 and 29.77)
SCENE_DOWN = 161.0                     # src/PILOT.cpp kSceneDown
SCENE_SCREEN = (119.72, 18.79, 15.83)  # the old screens' left, width and height (the bottom one was 0.4 px higher)
SCENE_TRIM_Y = 49 + TRIM_HALF          # the trimpot and the arrow buttons (0.4 px higher) on one row, from the top
SCENE_CV_Y = 72 + 7.5                  # the tiny CV jacks, old corner 72
SCENE_BRACKET = (29.25, 86.25)         # the SCENE brackets' ends (29.22, 86.3) and their column
SCENE_BRACKET_X = 110.84
# the morph knob at its old corner (144, 111); its CV jack on its row (0.45 px lower); the go to buttons and their CV jacks
MORPH = (144 + 56.1023622 / 2, 111 + 56.1023622 / 2)
GO_Y = (96 + 27.9958782 / 2, 155 + 27.9958782 / 2)   # the go to buttons, at the old CKD6s' rows (src/PILOT.cpp)
GO_CV = ((104 + 7.5, 121 + 7.5), (104 + 7.5, 143 + 7.5))
ARROW_BAR = (95.48, 183.58)            # the old arrows by the go to buttons: their bars, tips 0.36 px inside, bases 5.83
ARROW_X = (140.36, 155.98)

# Right: the white control area around the 16 knobs and their buttons (the old corners); its right edge on the outputs'
# (it was 0.87 px left), its bottom where it was. #FFFFFF: dark in Dark and Black
AREA = (205.0, SCENE_TOP, 440.0, 249.3)
KNOB0 = (208 + 18.75, 35 + 18.75)      # BidooLargeColoredKnob (12.7 mm), old corner (208, 35); src/PILOT.cpp
KNOB_PITCH = 57.0
LEGENDS = (('TRIG TYPE', (271.56, 84.49), (265.2, 84.49), (261.7, 88.18)),   # the old legends: text, leader's points
           ('VOLTAGE', (330.66, 193.58), (320.4, 193.58), (315.9, 189.96)))
LEGEND_SIZE = 6.5
# the outputs: the old 27 px pitch, one 25 px box each, 2 px apart; the last row at its old height (350, under the
# theme's 342: the user wants the matrix clear of the control area, 7 px under it; the boxes end 2.5 px above the
# screws), the last column's box on the white area's edge (0.35 px left). src/PILOT.cpp kOutPitch, kOutLastRow, out0
OUT_PITCH, OUT_BOX = 27.0, 25.0
OUT_LAST_ROW = 350.0
OUT_X0 = AREA[2] - OUT_BOX / 2 - 3 * OUT_PITCH
POLY_X = 288 + 23.7 / 2                # POLY at its old column, on the bottom row (7.85 px higher)
CV_OUT_X = 324.0                       # the CV OUTPUTS bracket (324.1) and its label

def text_width(s, size):
    return sum(k._glyphs[k._cmap[ord(c)]].width for c in s) * size / k._upm

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'pilot_parts.svg'), encoding='utf-8') as f:
        body = re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)
    return f'<g transform="scale({W / OLD_W:.6f})">{body}</g>'

def P(x, y):
    return f'{x * k.K:.4f},{y * k.K:.4f}'

def path(d, fill='none', stroke=k.INK):
    return f'<path style="fill:{fill};stroke:{stroke};stroke-width:{LINE}" d="{d}"/>'

def line(x0, y0, x1, y1):
    # the old lines from a control to its CV jack, drawn under them so only the gap shows
    return path(f'M{P(x0, y0)}L{P(x1, y1)}')

def heading(label, x0, x1, y, down=5.0):
    # a group's name between two rules at its capitals' middle, the rules turning down at the group's edges
    mid, half, ym = (x0 + x1) / 2, text_width(label, k.KNOB_LABEL_SIZE) / 2 + 4, y - CAP / 2
    return (k.text(label, mid, y, k.KNOB_LABEL_SIZE, k.INK)
            + path(f'M{P(mid - half, ym)}L{P(x0, ym)}L{P(x0, ym + down)}')
            + path(f'M{P(mid + half, ym)}L{P(x1, ym)}L{P(x1, ym + down)}'))

def vertical(label, x, y0, y1, right=5.9):
    # a name read upwards between two rules down the left of a group, the rules turning right at its ends
    mid, half = (y0 + y1) / 2, text_width(label, k.KNOB_LABEL_SIZE) / 2 + 4
    t = k._text(label, x, mid + CAP / 2, k.KNOB_LABEL_SIZE, k.INK)
    return (f'<g transform="rotate(-90,{x * k.K:.4f},{mid * k.K:.4f})">{t}</g>'
            + path(f'M{P(x, mid - half)}L{P(x, y0)}L{P(x + right, y0)}')
            + path(f'M{P(x, mid + half)}L{P(x, y1)}L{P(x + right, y1)}'))

def arrow(bar, up):
    # the old ▲ / ▼ by a go to button: a bar and a triangle pointing at it
    s = 1 if up else -1
    tip, base, mid = bar + 0.36 * s, bar + 5.83 * s, (ARROW_X[0] + ARROW_X[1]) / 2
    return (path(f'M{P(ARROW_X[0], bar)}L{P(ARROW_X[1], bar)}')
            + path(f'M{P(mid, tip)}L{P(mid - 3.905, base)}L{P(mid + 3.905, base)}Z', k.INK))

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('πlOT', PW)]   # 39 px wide at 16: fits
    # ---- left: MORPHING, CURVE and SPEED, the curve screen and its trimpots
    out.append(heading('MORPHING', GROUP_LEFT, GROUP_RIGHT, MORPHING_Y))
    out.append(line(SPEED_X, SPEED_ROW, SPEED_CV_X, SPEED_ROW))
    out.append(k.knob_label('CURVE', CURVE_X, SPEED_ROW))
    out.append(k.knob_label('SPEED', SPEED_X, SPEED_ROW))
    out.append(k.screen(*CURVE_SCREEN))
    for x in CURVE_COLS:
        out.append(line(x, CURVE_TRIM_Y, x, CURVE_CV_Y))
    # BANK: its screen, knob and CV jack
    out.append(heading('BANK', GROUP_LEFT, GROUP_RIGHT, BANK_Y))
    out.append(k.screen(*BANK_SCREEN))
    out.append(line(BANK_X, BANK_ROW, BANK_CV_X, BANK_ROW))
    # ---- the sequencer: its controls' names on one baseline, the move type and length screens, TRIG and RESET's boxes
    out.append(heading('SEQUENCER', GROUP_LEFT, SEQUENCER_RIGHT, SEQUENCER_Y))
    for lab, x in SEQ_X.items():
        out.append(k.knob_label(lab, x, SEQ_ROW))
    for s in SEQ_SCREENS:
        out.append(k.screen(*s))
    # the TRIG button's line to its jack's box, on the button's row, as it went to the tiny jack before
    out.append(line(SEQ_X['TRIG'], SEQ_ROW, TRIG_JACK_X - BOX_W / 2, SEQ_ROW))
    for lab, x, colour in (('TRIG', TRIG_JACK_X, k.INPUT), ('RESET', RESET_JACK_X, k.INPUT), ('POLY', POLY_X, k.OUTPUT)):
        out.append(k.box(x - BOX_W / 2, k.BOTTOM_TOP, BOX_W, k.BOTTOM_BOTTOM - k.BOTTOM_TOP, colour))
        out.append(k.jack_label(lab, x, k.BOTTOM_ROW))
    # ---- the scenes: screen, the lines from the trimpot and buttons down to their tiny CV jacks, SCENE down the left
    for dy in (0.0, SCENE_DOWN):
        x0, w, h = SCENE_SCREEN
        out.append(k.screen(x0, SCENE_TOP + dy, w, h))
        for x in (SCENE_X,) + SCENE_BTN_X:
            out.append(line(x, SCENE_TRIM_Y + dy, x, SCENE_CV_Y + dy))
        out.append(vertical('SCENE', SCENE_BRACKET_X, SCENE_BRACKET[0] + dy, SCENE_BRACKET[1] + dy))
    # the morph knob: its CV jack's line, the go to buttons' lines to their CV jacks, the arrows
    out.append(line(SCENE_X, MORPH[1], MORPH[0], MORPH[1]))
    for (x, y), go in zip(GO_CV, GO_Y):
        out.append(line(x, y, SCENE_X, go))
    out.append(arrow(ARROW_BAR[0], True))
    out.append(arrow(ARROW_BAR[1], False))
    # ---- right: the white control area and its two legends
    x0, y0, x1, y1 = AREA
    out.append(f'<path style="fill:{k.WHITE};stroke:{k.FRAME};stroke-width:0.5" '
               f'd="M{P(x0, y0)}H{x1 * k.K:.4f}V{y1 * k.K:.4f}H{x0 * k.K:.4f}Z"/>')
    for lab, a, b, c in LEGENDS:
        out.append(path(f'M{P(*a)}L{P(*b)}L{P(*c)}'))
        out.append(k.text(lab, a[0] + 2 + text_width(lab, LEGEND_SIZE) / 2, a[1] + k.CAP_HEIGHT * LEGEND_SIZE / 2,
                          LEGEND_SIZE, k.INK))
    # the 16 outputs: one box each, no caption (the 27 px pitch leaves no room); CV OUTPUTS down their left
    for j in range(4):
        for i in range(4):
            x, y = OUT_X0 + OUT_PITCH * i, OUT_LAST_ROW - OUT_PITCH * (3 - j)
            out.append(k.box(x - OUT_BOX / 2, y - OUT_BOX / 2, OUT_BOX, OUT_BOX, k.OUTPUT))
    top, bottom = OUT_LAST_ROW - 3 * OUT_PITCH - OUT_BOX / 2, OUT_LAST_ROW + OUT_BOX / 2
    out.append(vertical('CV OUTPUTS', CV_OUT_X, top, bottom, 3.6))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('PILOT.svg', build())
