# What dTrOY's and bordL's panels share (dtroy.py, bordl.py): the left section, the same on both (the clock, run, reset
# and steps row with its jacks, the key, scale, gate and slide knobs with theirs, each control joined by a line to its
# jack; play, count and the pattern jack joined to its knob; the screen; transpose, copy and the shift arrows), and a
# few drawing helpers. The positions are the same in src/MetroSeq.hpp (metroPanel and metroLeftControls).
import os, re
import panel_kit as k

# The left section's four columns: the old controls, jacks and lines were 0 to 0.5 px off them, the pattern knob 1.6
# (dTrOY) and 2.6 px (bordL) right of the last. src/MetroSeq.hpp metroPanel::kCol
COLS = (32.7, 70.1, 107.8, 147.2)
BOX_W = 29.0                           # one box per jack, the old boxes' width (29.1), src/MetroSeq.hpp kBoxWidth
KNOB_HALF = 29.5275591 / 2
KNOB_Y = 36 + KNOB_HALF                # CLOCK and STEPS, old corner 36 (src/MetroSeq.hpp)
BUTTON_ABOVE = 4.0                     # RUN and RESET above the knobs' row (they were 2.24 px lower), so that their
                                       # labels share CLOCK's baseline at a button's offset; src/MetroSeq.hpp kButtonAbove
BUTTON_Y = KNOB_Y - BUTTON_ABOVE
BUTTON_HALF = 10.0                     # BidooRoundButton
TOP_JACK_Y = 69 + 23.7 / 2             # their CV jacks, old corner 69 (src/MetroSeq.hpp)
MID_KNOB_Y = 116 + KNOB_HALF           # KEY SCALE GATE SLIDE, old corner 116
MID_JACK_Y = 149 + 23.7 / 2            # their CV jacks, old corner 149
PATTERN_Y = 210.5                      # PLAY, COUNT, the pattern jack and knob: the old line's row (210.47 and 210.56),
                                       # src/MetroSeq.hpp kPatternY
ROW_LABEL = k.KNOB_LABEL_ABOVE         # a row's labels on its knobs' baseline, the buttons' too (at 7.5)
# EXT CLK: its box reaches 4 px under the RUN button, not 25 px above its jack (20.1): the room between the button and
# the middle row's labels. 38 px wide, so that its caption fits. src/MetroSeq.hpp: the jack is on the CV jacks' row
EXT_TOP = BUTTON_Y + BUTTON_HALF + k.BOX_GAP
EXT_BOX_W = 38.0
SCREEN = (COLS[0] - BOX_W / 2, 228.75, COLS[3] - COLS[0] + BOX_W, 36.55)   # the old screen (15.2..162.1, 228.7..265.3),
                                       # its sides on the boxes' (2.9 and 0.4 px in); src/MetroSeq.hpp kScreen... and
                                       # MetroDisplay draws its text where it did
SHIFTS_X = 127.5                       # the shift arrows' centre (0.5 px right, 2 px lower than before), src/MetroSeq.hpp
LINE = 0.26458                         # the old lines' width, in units
SECTION_SIZE = k.KNOB_LABEL_SIZE       # the names of the stage rows

def parts(name):
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), name), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def width(s, size):
    # the advance panel_kit.text centres
    return sum(k._glyphs[k._cmap[ord(c)]].width for c in s) * size / k._upm

def text_right(s, x, y, size, col=k.INK):
    return k.text(s, x - width(s, size) / 2, y, size, col)

def text_left(s, x, y, size, col=k.INK):
    return k.text(s, x + width(s, size) / 2, y, size, col)

def centred_on(y, size):
    # the baseline that centres capitals on y
    return y + k.CAP_HEIGHT * size / 2

def rotated(s, x, y, size):
    # reading upwards, its capitals centred on (x, y)
    return (f'<g transform="translate({x * k.K:.4f},{y * k.K:.4f}) rotate(-90)">'
            f'{k._text(s, 0, k.CAP_HEIGHT * size / 2, size, k.INK)}</g>')

def path(d, width=LINE, fill='none'):
    return f'<path style="fill:{fill};stroke:{k.INK};stroke-width:{width}" d="{d}"/>'

def vline(x, y0, y1, width=LINE):
    return path(f'M{x * k.K:.4f},{y0 * k.K:.4f}V{y1 * k.K:.4f}', width)

def hline(x0, x1, y, width=LINE):
    return path(f'M{x0 * k.K:.4f},{y * k.K:.4f}H{x1 * k.K:.4f}', width)

def rect(x, y, w, h, filled, width=LINE):
    return path(f'M{x * k.K:.4f},{y * k.K:.4f}h{w * k.K:.4f}v{h * k.K:.4f}h{-w * k.K:.4f}Z', width,
                k.INK if filled else 'none')

def output_square(x, y, size):
    # a tiny output jack's own red box
    return k.box(x - size / 2, y - size / 2, size, size, k.OUTPUT)

def left_section():
    out = [k.screen(*SCREEN)]
    # the lines from each control down to its jack (across, for the pattern), drawn under them so only the gap shows;
    # EXT CLK has none, it is the RUN button's column but not its jack
    for i in (0, 3):
        out.append(vline(COLS[i], KNOB_Y, TOP_JACK_Y))
    out.append(vline(COLS[2], BUTTON_Y, TOP_JACK_Y))
    for x in COLS:
        out.append(vline(x, MID_KNOB_Y, MID_JACK_Y))
    out.append(hline(COLS[2], COLS[3], PATTERN_Y))
    # the top row: CLOCK and STEPS knobs, RUN and RESET buttons, one baseline
    base = KNOB_Y - ROW_LABEL
    out.append(k.knob_label('CLOCK', COLS[0], KNOB_Y))
    out.append(k.text('RUN', COLS[1], base, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.text('RESET', COLS[2], base, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.knob_label('STEPS', COLS[3], KNOB_Y))
    out.append(k.box(COLS[1] - EXT_BOX_W / 2, EXT_TOP, EXT_BOX_W, TOP_JACK_Y + k.BOX_BELOW - EXT_TOP, k.INPUT))
    out.append(k.text('EXT CLK', COLS[1], TOP_JACK_Y - k.T['captionAboveJack'](TOP_JACK_Y - EXT_TOP), k.BOX_LABEL_SIZE,
                      k.WHITE))
    for lab, x in zip(('KEY', 'SCALE', 'GATE', 'SLIDE'), COLS):
        out.append(k.knob_label(lab, x, MID_KNOB_Y))
    # PLAY and COUNT buttons, the pattern jack and knob with its name between them
    base = PATTERN_Y - ROW_LABEL
    out.append(k.text('PLAY', COLS[0], base, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.text('COUNT', COLS[1], base, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.knob_label('PATTERN', (COLS[2] + COLS[3]) / 2, PATTERN_Y))
    # transpose on row 1 (1.85 px higher, 0.85 px right), its box 4 px above the bottom row's; copy and the shift
    # arrows' names on its caption's baseline (they were 2.3 px higher)
    out.append(k.box(COLS[0] - BOX_W / 2, k.box_top(1), BOX_W, k.box_bottom(1) - k.box_top(1), k.INPUT))
    out.append(k.jack_label('TRSP', COLS[0], k.row(1)))
    base = k.row(1) - k.LABEL_ABOVE
    out.append(k.text('CPY/PST', COLS[1], base, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.text('SHIFTS', SHIFTS_X, base, k.BOX_LABEL_SIZE, k.INK))
    return out

def bottom_boxes(centres, w, names, colours):
    out = []
    for x, lab, colour in zip(centres, names, colours):
        out.append(k.box(x - w / 2, k.box_top(0), w, k.box_bottom(0) - k.box_top(0), colour))
        out.append(k.jack_label(lab, x, k.row(0)))
    return out
