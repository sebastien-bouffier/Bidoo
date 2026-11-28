# The panels of the two ZouSeq sequencers (zOù MAï and enCORE, 31 HP) and of their expanders (17 HP), migrated from the
# hand-drawn ones. Each pair differs only in its title and its picture, so both come from here and stay the same by
# construction: zoumai.py, encore.py, zoumai_expander.py and encore_expander.py give the title and the parts file.
# The positions are the same in src/ZouSeq.hpp (ZouSeqWidget) and src/ZouSeqExpander.hpp (ZouSeqExpanderWidget).
#
# The sequencer keeps its arrangement: the five global inputs down the left with the pattern knob under them, the eight
# track rows (reset and on/off inputs, on/off and select buttons), the two screens the widget draws with their knobs,
# the octave buttons and the keyboard, the 8 x 4 outputs and the poly row on the right, the page buttons, the 16 steps,
# FILL and RUN, and the record section at the bottom. The full jacks get the theme's boxes; the tiny jacks (too close
# for full jacks: 20 px apart) one small box each, their column named above them, as the old tags did.
import os, re
import panel_kit as k

HERE = os.path.dirname(os.path.abspath(__file__))
LINE = 0.26458                     # the old lines' width, in units
JACK_HALF, TINY_HALF = 23.7 / 2, 7.5
TINY_BOX = 18.0                    # a tiny jack's box: 1.5 px around it, 2 px between boxes 20 px apart (only drawn here)

def parts(name):
    with open(os.path.join(HERE, name), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def width(s, size):
    return sum(k._glyphs[k._cmap[ord(c)]].width for c in s) * size / k._upm

def rotated(s, x, y, size=k.BOX_LABEL_SIZE, col=k.INK):
    # a label reading upwards, its capitals centred on (x, y), as the old tags' names
    return (f'<g transform="translate({x * k.K:.4f},{y * k.K:.4f}) rotate(-90)">'
            f'{k._text(s, 0, k.CAP_HEIGHT * size / 2, size, col)}</g>')

def centred(s, x, y, size=k.BOX_LABEL_SIZE, col=k.INK):
    # a label whose capitals are centred on y
    return k.text(s, x, y + k.CAP_HEIGHT * size / 2, size, col)

def hline(x0, x1, y):
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:{LINE}" d="M{x0*k.K:.4f},{y*k.K:.4f}H{x1*k.K:.4f}"/>'

def vline(x, y0, y1):
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:{LINE}" d="M{x*k.K:.4f},{y0*k.K:.4f}V{y1*k.K:.4f}"/>'

def tiny_box(x, y, colour):
    return k.box(x - TINY_BOX / 2, y - TINY_BOX / 2, TINY_BOX, TINY_BOX, colour)

def header(s, x0, x1, y):
    # a group's name, a rule under it from x0 to x1 (as the old TRACKS IN & CTRL, TRACKS OUT and PAGE)
    return [k.text(s, (x0 + x1) / 2, y, k.BOX_LABEL_SIZE, k.INK), hline(x0, x1, y + 3)]

# ---- The sequencer, 31 HP. Every value below is a centre in px, the same in src/ZouSeq.hpp (ZouSeqWidget)
PW = 465
W = PW * k.K
LEFT_X = 10 + JACK_HALF            # CLK, RST, GTE1, GTE2, PTRN and FILL: old corner x 10 (src/ZouSeq.hpp left)
LEFT_Y0, LEFT_PITCH = 30 + JACK_HALF, 45.0   # old corners 30, 75, 120, 165, 210 (left, kLeftPitch): 45 px apart,
                                   # so their boxes are 3 px apart, not the theme's 4
LEFT_BOX_W = 28.0                  # one box per full jack, as the old ones (28)
LEFT_NAMES = ('CLK', 'RST', 'GTE1', 'GTE2', 'PTRN')
FILL_Y = 298 + JACK_HALF           # FILL and RUN, old corners y 298 (fill, run)
RUN_X = 74 + JACK_HALF             # old corner x 74 (run)
FILL_BUTTON_X, RUN_BUTTON_X = 54.0, 118.0   # the old CKD6s' centres, now BidooRoundButtons on their jack's row (0.15 px up):
                                   # kFillButtonX, kRunButtonX
TRACK_Y0, TRACK_PITCH = 74.5, 28.0 # the tracks' rows: the tiny jacks' centres (old corner 67), the on/off buttons
                                   # (0.13 px up) and the select buttons (0.3 px up) on them (rst, kTrackPitch)
RST_X, ACT_X = 57.5, 77.5          # the reset and on/off inputs, old corners x 50 and 70 (rst, act)
ONOFF_X = 90 + 21.26028 / 2        # the on/off buttons (LEDLightBezel, kept for their green and blue), old corner x 90
SEL_X = 125.0                      # the select buttons, now 20 px BidooRoundButtons: 2.2 px right of the old ones, to
                                   # stay 3.7 px from the on/off buttons and 3 px from the screen's frame (kSelX)
TRACK_NUMBER_X = (LEFT_X + LEFT_BOX_W / 2 + RST_X - TINY_BOX / 2) / 2   # between the left boxes and the reset boxes
COLUMN_LABEL_Y = 56.5              # the columns' names over the first row: 18 above the tracks' row (kSmallLabelAbove)
GROUP_Y = 42.0                     # the groups' names (TRACKS IN & CTRL, TRACKS OUT), their rule 3 px under
OUT_X0, OUT_PITCH = 375 + TINY_HALF, 20.0    # the outputs' columns, old corner x 375 (out, kOutPitch)
OUT_Y0 = 65 + TINY_HALF            # their rows, 20 px apart, the poly row as a ninth (out)
OUT_NAMES = ('GATE', 'V/O', 'CV1', 'CV2')
OUT_NUMBER_X = (OUT_X0 + 3 * OUT_PITCH + TINY_BOX / 2 + PW) / 2   # between the last boxes and the side (1.1 px right)
OCTAVE_X0, OCTAVE_PITCH = 175.8, 22.0  # the octave buttons, now 20 px BidooRoundButtons: 22 px apart (they were 19,
                                   # 10.6 px wide), centred where the old row was (241.8) (octave, kOctavePitch)
OCTAVE_Y = 262.8                   # 1 px lower than before, so their numbers clear the knobs above (octave)
OCTAVE_LABEL_ABOVE = 17.0          # the numbers' baseline above the buttons (the theme's 17 to 19 for small controls)
PAGE_X0, PAGE_PITCH = 378.5 + 10.63 / 2, 19.0   # the page buttons (SmallLEDLightBezel, kept for their red and blue)
PAGE_Y = 314.5 + 10.63 / 2
STEP_X0, STEP_PITCH, STEP_TOP = 12.0, 28.0, 330.0   # the 16 steps (LEDLightBezel, kept: their colours are the steps'
STEP_SIZE = 21.26                  # states), unchanged
REC_Y = 360 + TINY_HALF            # the record section: its inputs' centres, old corner y 360; the two buttons, now
REC_X, GATE_X, VO_X, QNT_X = 305.5, 334.5, 363.5, 393.5   # BidooRoundButtons, at the old ones' centres, 1 px lower (recGate, kRecX, kQntX)
REC_LABELS = (('REC', 290.8), ('GATE', 320.5), ('V/O', 349.0), ('QNT', 378.2))   # rotated, left of each, as before
REC_FRAME = (282.0, 352.0, 410.0)  # the old frame around them: left, top, right; open at the bottom

def sequencer(title, parts_file):
    out = [k.header(PW, W), k.background(W), parts(parts_file), k.title(title, PW)]
    # the global inputs down the left, one box each, and the pattern knob under them (no label, as before)
    for i, lab in enumerate(LEFT_NAMES):
        y = LEFT_Y0 + LEFT_PITCH * i
        out.append(k.jack_box(LEFT_X, y, LEFT_BOX_W, k.INPUT))
        out.append(k.jack_label(lab, LEFT_X, y))
    # FILL and RUN, each joined by a line to its button
    for x, bx, lab in ((LEFT_X, FILL_BUTTON_X, 'FILL'), (RUN_X, RUN_BUTTON_X, 'RUN')):
        out.append(k.jack_box(x, FILL_Y, LEFT_BOX_W, k.INPUT))
        out.append(k.jack_label(lab, x, FILL_Y))
        out.append(hline(x + LEFT_BOX_W / 2, bx - 10, FILL_Y))
    # the tracks: their number, the reset and on/off inputs, the on/off and select buttons
    out += header('TRACKS IN & CTRL', RST_X - TINY_BOX / 2, SEL_X + 10, GROUP_Y)
    out.append(k.small_label('RST', RST_X, TRACK_Y0))
    out.append(k.small_label('ON/OFF', (ACT_X + ONOFF_X) / 2, TRACK_Y0))
    out.append(k.small_label('SEL', SEL_X, TRACK_Y0))
    for i in range(8):
        y = TRACK_Y0 + TRACK_PITCH * i
        out.append(centred(str(i + 1), TRACK_NUMBER_X, y, k.KNOB_LABEL_SIZE))
        out.append(tiny_box(RST_X, y, k.INPUT))
        out.append(tiny_box(ACT_X, y, k.INPUT))
    # the outputs: 8 rows and the poly row, one box per jack, the tracks' numbers on the right
    out += header('TRACKS OUT', OUT_X0 - TINY_BOX / 2, OUT_X0 + 3 * OUT_PITCH + TINY_BOX / 2, GROUP_Y)
    for j, lab in enumerate(OUT_NAMES):
        out.append(k.text(lab, OUT_X0 + OUT_PITCH * j, COLUMN_LABEL_Y, k.BOX_LABEL_SIZE, k.INK))
    for i in range(9):
        y = OUT_Y0 + OUT_PITCH * i
        for j in range(4):
            out.append(tiny_box(OUT_X0 + OUT_PITCH * j, y, k.OUTPUT))
        if i < 8:
            out.append(centred(str(i + 1), OUT_NUMBER_X, y))
    poly_bottom = OUT_Y0 + OUT_PITCH * 8 + TINY_BOX / 2
    out.append(k.text('POLY', OUT_X0 + 1.5 * OUT_PITCH, poly_bottom + 4 + k.CAP_HEIGHT * k.BOX_LABEL_SIZE,
                      k.BOX_LABEL_SIZE, k.INK))
    # the octaves: their numbers above, OCT on their left
    for i in range(7):
        out.append(k.text(str(i + 1), OCTAVE_X0 + OCTAVE_PITCH * i, OCTAVE_Y - OCTAVE_LABEL_ABOVE, k.BOX_LABEL_SIZE, k.INK))
    oct_x = OCTAVE_X0 - 10 - 3 - width('OCT', k.BOX_LABEL_SIZE) / 2
    out.append(centred('OCT', oct_x, OCTAVE_Y))
    # the pages: their names above, PAGE over them
    page_top = PAGE_Y - 10.63 / 2
    # PAGE centred on the outputs' columns (its buttons' middle is 0.19 px left of it), its rule as wide as the buttons
    out += header('PAGE', OUT_X0 + 1.5 * OUT_PITCH - 37, OUT_X0 + 1.5 * OUT_PITCH + 37, page_top - 14.5)
    for i in range(4):
        out.append(k.text('%d:4' % (i + 1), PAGE_X0 + PAGE_PITCH * i, page_top - 4, k.BOX_LABEL_SIZE, k.INK))
    # the steps, in groups of four (the old separators)
    for g in (1, 2, 3):
        x = STEP_X0 + STEP_PITCH * 4 * g - (STEP_PITCH - STEP_SIZE) / 2
        out.append(vline(x, STEP_TOP, STEP_TOP + STEP_SIZE))
    # the record section: REC, the gate and V/O inputs, QNT, each named on its left, in the old frame
    x0, y0, x1 = REC_FRAME
    out.append(f'<path style="fill:none;stroke:{k.INK};stroke-width:{LINE}" '
               f'd="M{x0*k.K:.4f},{k.H:.4f}V{(y0+4)*k.K:.4f}Q{x0*k.K:.4f},{y0*k.K:.4f} {(x0+4)*k.K:.4f},{y0*k.K:.4f}'
               f'H{(x1-4)*k.K:.4f}Q{x1*k.K:.4f},{y0*k.K:.4f} {x1*k.K:.4f},{(y0+4)*k.K:.4f}V{k.H:.4f}"/>')
    for x in (GATE_X, VO_X):
        out.append(tiny_box(x, REC_Y, k.INPUT))
    for lab, x in REC_LABELS:
        out.append(rotated(lab, x, REC_Y))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

# ---- The expander, 17 HP: eight rows, the same in src/ZouSeqExpander.hpp (ZouSeqExpanderWidget)
EPW = 255
EW = EPW * k.K
ROW_PITCH = 38.0                   # the rows (src/ZouSeqExpander.hpp kRowPitch)
BUTTON_Y0 = 64.5                   # the FILL, FORCE, KILL and TRSP buttons: 0.8 px higher than before, so that 16 px
                                   # buttons and the boxes under them keep 2 px between them (button)
JACK_Y0 = 76 + TINY_HALF           # their inputs under them, old corner 76 (jack)
MID_Y0 = 69 + TINY_HALF            # the dice knob, its input, the rotation trimpots and inputs: old corner 69 for the
                                   # jacks, the knob and trimpots 0.3 and 0.1 px higher (mid)
BUTTON_SIZE = 16.0                 # BidooRoundButton at 16 px: at 20 a button and the box under it don't fit 38 px apart
FILL_X, FORCE_X, KILL_X = 13.5, 39.5, 62.0   # FILL 1.5 px left, FORCE 1 px right of before: room for the track numbers
DICE_KNOB_X, DICE_X, TRSP_X = 73.5 + 23.622 / 2, 109.0, 132.5   # old corners 73.5, 101.5, 125 (the TRSP button 0.3 px
                                   # left, onto its input's column)
SHIFT_X, LEN_X = 151 + 19.1929131 / 2, 179.2 + 19.1929131 / 2   # the rotation trimpots, old corners 151 and 179.2
LEFT_IN_X, RIGHT_IN_X = 207.25 + TINY_HALF, 230.75 + TINY_HALF
COLUMN_BOTTOM = 52.0               # the columns' names, rotated as before, ending 4.5 px over the buttons
NUMBER_X = (FILL_X + FORCE_X) / 2  # the tracks' numbers, between the FILL and FORCE buttons, as before

def expander(title, parts_file):
    out = [k.header(EPW, EW), k.background(EW), parts(parts_file), k.title(title, EPW)]
    columns = (('FILL', FILL_X), ('FORCE', FORCE_X), ('KILL', KILL_X), ('DICE', (DICE_KNOB_X + DICE_X) / 2),
               ('TRSP', TRSP_X), ('SHIFT', SHIFT_X), ('LEN', LEN_X), ('LEFT', LEFT_IN_X), ('RIGHT', RIGHT_IN_X))
    for lab, x in columns:
        out.append(rotated(lab, x, COLUMN_BOTTOM - width(lab, k.BOX_LABEL_SIZE) / 2))
    # ROTATION over its four columns, from the shift trimpot to the right input's box
    tallest = max(width(lab, k.BOX_LABEL_SIZE) for lab, _ in columns[5:])
    out += header('ROTATION', SHIFT_X - 19.1929131 / 2, RIGHT_IN_X + TINY_BOX / 2, COLUMN_BOTTOM - tallest - 7)
    for i in range(8):
        dy = ROW_PITCH * i
        out.append(centred(str(i + 1), NUMBER_X, BUTTON_Y0 + dy))
        for x in (FILL_X, FORCE_X, KILL_X, TRSP_X):
            out.append(tiny_box(x, JACK_Y0 + dy, k.INPUT))
        for x in (DICE_X, LEFT_IN_X, RIGHT_IN_X):
            out.append(tiny_box(x, MID_Y0 + dy, k.INPUT))
    out.append(k.logo(EW))
    out.append('</svg>')
    return out
