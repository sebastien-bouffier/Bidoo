# dUKe's panel (12 HP), migrated from the hand-drawn one: the same arrangement (MAX, MIN and the range named down the
# left, the two buttons under them between their arrows; four columns of a max and a min knob, a range switch, a slider
# joined by a line to its CV jack, and an OUT box), now in the theme's boxes, type and colours, with the old picture
# kept (duke_parts.svg). No screws, as before. The positions are the same in src/DUKE.cpp.
import os, re
import panel_kit as k

PW = 180
W = PW * k.K
# the columns: the max knobs' old corners (34 + these), every control of a column snapped onto its knob's centre (they
# were within 0.25 px); the same in src/DUKE.cpp (kColumnRight)
COLS = tuple(34 + 29.5275591 / 2 + dx for dx in (0.0, 34.0, 69.0, 104.0))
MAX_Y = 51 + 29.5275591 / 2        # the max knobs, old corner y 51 (src/DUKE.cpp)
MIN_Y = MAX_Y + 44.0               # the min knobs, 44 below (src/DUKE.cpp kMinBelow)
CKSS_HALF = 20.64111 / 2
SWITCH_Y = 139 + CKSS_HALF         # the range switches, old corner y 139 (src/DUKE.cpp)
SLIDER_BOTTOM = 184 + 76.53516     # LEDSliderGreen, old corner y 184: its line starts under it
BUTTON_X = 7 + 14.0                # the two buttons' column: the old BlueCKD6's centre (src/DUKE.cpp buttons)
BUTTON_Y = (190 + 14.0, 230 + 14.0)    # their old centres, now BidooRoundButtons (20 px), the same in src/DUKE.cpp
BUTTON_HALF = 10.0
SIDE_X = 21.0                      # the rows' names and the range's: on the buttons' column, where the old ones were
# The arrows over the first button and under the second: the old ones' size (a 20 px bar, a 10 x 7 triangle), the same
# 6 px from their button as before
ARROW_GAP, ARROW_BAR, ARROW_W, ARROW_H = 6.0, 20.0, 10.0, 7.0
BOX_W = 30.0                       # the OUT boxes: centred on their column, 4 to 5 px apart (34 and 35 px columns)
LINE = 0.26458                     # the old lines' width, in units

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'duke_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def vline(x, y0, y1):
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:{LINE}" d="M{x*k.K:.4f},{y0*k.K:.4f}V{y1*k.K:.4f}"/>'

def arrow(x, base_y, up):
    # the triangle from its base to its point, and the bar just past its point
    s = -1 if up else 1
    tip = base_y + s * ARROW_H
    bar = tip + s * 0.5
    tri = (f'M{(x - ARROW_W / 2) * k.K:.4f},{base_y * k.K:.4f}H{(x + ARROW_W / 2) * k.K:.4f}'
           f'L{x * k.K:.4f},{tip * k.K:.4f}Z')
    return (f'<path style="fill:{k.INK};stroke:{k.INK};stroke-width:{LINE};stroke-linejoin:round" d="{tri}"/>'
            f'<path style="fill:none;stroke:{k.INK};stroke-width:{LINE}" '
            f'd="M{(x - ARROW_BAR / 2) * k.K:.4f},{bar * k.K:.4f}H{(x + ARROW_BAR / 2) * k.K:.4f}"/>')

def side_label(s, y):
    # read upwards, centred on its row, as before (and as bAFIs's); not a mark for --align, it is turned
    x = SIDE_X
    return (f'<g transform="rotate(-90 {x*k.K:.4f} {y*k.K:.4f})">'
            + k._text(s, x, y + k.CAP_HEIGHT * k.BOX_LABEL_SIZE / 2, k.BOX_LABEL_SIZE, k.INK) + '</g>')

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('dUKe', PW)]
    out.append(side_label('MAX', MAX_Y))
    out.append(side_label('MIN', MIN_Y))
    # the range switches' positions, once on the left: up -5 to +5 V, down 0 to +10 V, either side of the switches' centre
    out.append(k.text('-5V/+5V', SIDE_X, SWITCH_Y - 2, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.text('0V/+10V', SIDE_X, SWITCH_Y + 2 + k.CAP_HEIGHT * k.BOX_LABEL_SIZE, k.BOX_LABEL_SIZE, k.INK))
    out.append(arrow(BUTTON_X, BUTTON_Y[0] - BUTTON_HALF - ARROW_GAP, True))
    out.append(arrow(BUTTON_X, BUTTON_Y[1] + BUTTON_HALF + ARROW_GAP, False))
    # each slider's line down to its CV jack on row 1, under them; OUT on row 0, a box each
    for x in COLS:
        out.append(vline(x, SLIDER_BOTTOM, k.row(1)))
        out.append(k.box(x - BOX_W / 2, k.box_top(0), BOX_W, k.box_bottom(0) - k.box_top(0), k.OUTPUT))
        out.append(k.jack_label('OUT', x, k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('DUKE.svg', build())
