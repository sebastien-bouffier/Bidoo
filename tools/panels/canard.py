# cANARd's panel (13 HP), migrated from the hand-drawn one: the same arrangement (the screen; five columns, each a
# control joined by a line to its CV jack below, in two rows, with the EOC box and the threshold trimpot in the last
# column; the slice mode switch between its icons; the stereo in and out), now in the theme's screen, boxes, type and
# colours, with the old picture and the switch's icons kept (canard_parts.svg). The positions are the same in
# src/CANARD.cpp (CANARDWidget).
import os, re
import panel_kit as k

PW = 195
W = PW * k.K
N = 5                                  # the columns, the same in src/CANARD.cpp
# the five columns are the bottom row's grid columns: the stereo jacks under the outer two each side, the switch in the
# middle (the old columns were 2.5, 1.6, 0.5, 1.1 and 0.2 px off them)
COL = tuple(k.jack_col(i, N, PW) for i in range(N))
KNOB_Y = 170 + 29.5275591 / 2          # the upper knobs, old corner 170; REC and the GATE jack on their row (src/CANARD.cpp)
JACK_Y = 202 + 23.7 / 2                # their CV jacks, old corner 202 (src/CANARD.cpp)
LOWER = 75.0                           # from the upper row to the lower one (src/CANARD.cpp kLowerBelow)
LOW_KNOB_Y, LOW_JACK_Y = KNOB_Y + LOWER, JACK_Y + LOWER   # EOC on the knobs' row, the trimpot on the jacks'
SWITCH = (COL[2], 325 + 20.64111 / 2)  # the CKSS, old corner y 325 (it was 1.5 px left), the same in src/CANARD.cpp
# the screen where the widget draws: the theme's margin and top, the display's 110 px (two 50 px channels and 10
# between), the same in src/CANARD.cpp (CANARDDisplay's box and its width and height)
SCREEN = (k.MARGIN, k.SCREEN_TOP, PW - 2 * k.MARGIN, 110.0)
LINE = 0.26458                         # the old lines' width, in units
# the labels share one baseline per row, at a big knob's offset (REC's and CLR's buttons and the GATE jack too)
UPPER = (('REC', 0), ('G/T', 1), ('STRT', 2), ('LEN', 3), ('READ', 4))
LOWER_LABELS = (('V/O', 0), ('ENV', 1), ('IND', 2), ('CLR', 3))
# the switch's icons from the old panel (their centres there), centred on the switch's column
ICONS = (('slices', 96.25), ('loop', 96.2))

def parts():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'canard_parts.svg'), encoding='utf-8') as f:
        return f.read()

def part(s, pid):
    # an element of the parts file by its id, with the groups around it: one a line
    return re.search(r'^(.*id="' + pid + r'".*)$', s, re.M).group(1)

def moved(svg, dx, dy):
    return f'<g transform="translate({dx * k.K:.5f},{dy * k.K:.5f})">{svg}</g>'

def vline(x, y0, y1):
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:{LINE}" d="M{x * k.K:.4f},{y0 * k.K:.4f}V{y1 * k.K:.4f}"/>'

def build():
    s = parts()
    out = [k.header(PW, W), k.background(W), part(s, 'picture'), k.title('cANARd', PW), k.screen(*SCREEN)]
    # each control's line down to its jack, drawn under them so only the gap shows (G/T's two jacks have none, nor EOC)
    for i in (0, 2, 3, 4):
        out.append(vline(COL[i], KNOB_Y, JACK_Y))
    for i in (0, 1, 2, 3):
        out.append(vline(COL[i], LOW_KNOB_Y, LOW_JACK_Y))
    for lab, i in UPPER:
        out.append(k.knob_label(lab, COL[i], KNOB_Y))
    for lab, i in LOWER_LABELS:
        out.append(k.knob_label(lab, COL[i], LOW_KNOB_Y))
    # EOC: its box in the last column, on the lower knobs' row, the threshold trimpot under it
    out.append(k.box(k.box_left(4, N, PW), LOW_KNOB_Y - k.BOX_ABOVE, k.box_right(4, N, PW) - k.box_left(4, N, PW),
                     k.BOX_ABOVE + k.BOX_BELOW, k.OUTPUT))
    out.append(k.jack_label('EOC', COL[4], LOW_KNOB_Y))
    # the slice mode switch's icons: slices above (on), the loop below (off)
    for pid, cx in ICONS:
        out.append(moved(part(s, pid), SWITCH[0] - cx, 0))
    # the bottom row: the stereo in and out, one box per jack
    for i, lab, colour in ((0, 'IN L', k.INPUT), (1, 'IN R', k.INPUT), (3, 'OUT L', k.OUTPUT), (4, 'OUT R', k.OUTPUT)):
        out.append(k.grid_box(i, i, N, 0, PW, colour))
        out.append(k.jack_label(lab, COL[i], k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('CANARD.svg', build())
