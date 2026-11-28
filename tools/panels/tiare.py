# TiARE's panel (12 HP), migrated from the hand-drawn one: the same arrangement (FM, FREQ and FINE across the top, the
# engine and sync switches under FM and FINE with the FM jack on its line, the XY screen the widget draws, PW, PWM and
# the PW jack down the right on their line, then the four inputs and the four outputs), now in the theme's boxes, type
# and colours, with the old picture kept (tiare_parts.svg). The positions are the same in src/TIARE.cpp.
import os, re
import panel_kit as k

PW = 180
W = PW * k.K
TRIM_HALF, LARGE_HALF, CKSS_HALF = 19.1929131 / 2, 37.5 / 2, 20.64111 / 2
FM = (41.844, 45 + TRIM_HALF)          # under its old label, at the old corner's height 45 (src/TIARE.cpp fm)
FREQ = (PW / 2, 45 + LARGE_HALF)       # BidooLargeBlueKnob on the panel's centre, old corner 45 (src/TIARE.cpp)
FINE = (PW - FM[0], FM[1])             # FM mirrored about the centre (it was 0.47 px left of that)
MODE = (30 + 7, 80 + CKSS_HALF)        # CKSS at the old corner (30, 80) (src/TIARE.cpp)
SYNC = (PW - MODE[0], MODE[1])         # MODE mirrored about the centre (it was 1.17 px left of that)
FM_IN = (53 + 7.5, 83 + 7.5)           # the FM jack's old centre: now a full jack, which fits there (src/TIARE.cpp)
FM_LINE_SLOPE = 4.1871 / 4.8998        # the old line from FM to its jack: down this much per px across, then straight down
PW_X = 162.5                           # PW, PWM and the PW jack, on the old line (src/TIARE.cpp pwCol)
PW_Y, PWM_Y = 135 + TRIM_HALF, 174 + TRIM_HALF   # trimpots at the old corners' heights (src/TIARE.cpp)
PW_IN_Y = 228 + 23.7 / 2               # the PW jack, old corner 228 (src/TIARE.cpp)
LABEL_GAP = k.KNOB_LABEL_ABOVE - 15    # 7 px above a knob's top, as for a big knob: FM, FREQ and FINE share a baseline
INS = ('V/O', 'SYNC', 'X', 'Y')
OUTS = ('SIN', 'TRI', 'SAW', 'SQR')

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tiare_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def polyline(points, width):
    d = 'L'.join('%.4f,%.4f' % (x * k.K, y * k.K) for x, y in points)
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:{width}" d="M{d}"/>'

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('TiARE', PW)]
    for lab, (x, y), half in (('FM', FM, TRIM_HALF), ('FREQ', FREQ, LARGE_HALF), ('FINE', FINE, TRIM_HALF)):
        out.append(k.text(lab, x, y - half - LABEL_GAP, k.KNOB_LABEL_SIZE, k.INK))
    # the switches: their two positions above and below them, as on lIMbO
    for (x, y), up, down in ((MODE, 'ANLG', 'DIGI'), (SYNC, 'HARD', 'SOFT')):
        out.append(k.text(up, x, y - CKSS_HALF - 4, k.BOX_LABEL_SIZE, k.INK))
        out.append(k.text(down, x, y + CKSS_HALF + 4 + k.CAP_HEIGHT * k.BOX_LABEL_SIZE, k.BOX_LABEL_SIZE, k.INK))
    # FM's amount goes to its jack: the old line, from the trimpot down to the jack
    bend = (FM_IN[0], FM[1] + (FM_IN[0] - FM[0]) * FM_LINE_SLOPE)
    out.append(polyline((FM, bend, FM_IN), 0.26458))
    # the pulse width column: PW, PWM and a line from PWM to the jack it scales
    out.append(k.text('PW', PW_X, PW_Y - TRIM_HALF - LABEL_GAP, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.text('PWM', PW_X, PWM_Y - TRIM_HALF - LABEL_GAP, k.BOX_LABEL_SIZE, k.INK))
    out.append(polyline(((PW_X, PWM_Y), (PW_X, PW_IN_Y)), 0.35))
    # the inputs on the second row, the outputs on the bottom row, one box each in four columns
    for i in range(4):
        out.append(k.grid_box(i, i, 4, 1, PW, k.INPUT))
        out.append(k.jack_label(INS[i], k.jack_col(i, 4, PW), k.row(1)))
        out.append(k.grid_box(i, i, 4, 0, PW, k.OUTPUT))
        out.append(k.jack_label(OUTS[i], k.jack_col(i, 4, PW), k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('TIARE.svg', build())
