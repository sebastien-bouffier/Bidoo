# ChUTE's panel (9 HP), migrated from the hand-drawn one: the same arrangement (three framed groups down the left, each a
# CV jack joined by a line to its knob under the group's name, the ball's screen down the right, TRIG and the drop
# button on the line between them, FALL, ALT and TOP at the bottom), now in the theme's boxes, type and colours, with
# the old picture kept (chute_parts.svg). The positions are the same in src/CHUTE.cpp.
import os, re
import panel_kit as k

PW = 135
W = PW * k.K
N = 3                              # the jack columns, the same in src/CHUTE.cpp
CV_X = 20 + 23.7 / 2               # the CV jacks, old corner x 20 (src/CHUTE.cpp cvCol)
KNOB_X = 59 + 29.5275591 / 2       # the knobs, old corner x 59 (src/CHUTE.cpp knobCol)
GROUP_Y = (64.344, 128.469, 190.531)   # each group's jack, line and knob (src/CHUTE.cpp groupLine)
NAMES = ('ALTITUDE', 'GRAVITY', 'RESTITUTION')
# The frames keep their old heights; their left edge moves onto the boxes' (10, it was 4) and their right edge leaves
# 6 px before the screen. Their names at a big knob's offset over the group's knob row (0.5 px higher than before),
# centred in the frame as before
FRAME_X0, FRAME_X1 = k.box_left(0, N, PW), 102.0
FRAME_Y = ((30, 85), (92, 147), (155, 210))
FRAME_STROKE, FRAME_RADIUS = 0.3, 1.4      # in units: the old frames' line (about 1 px) and corners
# The ball's screen: the old one's height (30 to 210, the frames' span), now ending on the TOP box's right edge (it
# reached 128) and as wide as the widget drew it (17); the same in src/CHUTE.cpp (CHUTEDisplay's box)
SCREEN = (k.box_right(N - 1, N, PW) - 17, 30.0, 17.0, 180.0)
BUTTON_HALF = 10.0                 # BidooRoundButton, on row 1 in the ALT column (src/CHUTE.cpp)
LINE = 0.26458                     # the old lines' width, in units

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'chute_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def hline(x0, x1, y):
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:{LINE}" d="M{x0*k.K:.4f},{y*k.K:.4f}H{x1*k.K:.4f}"/>'

def frame(x0, y0, x1, y1):
    # x0..x1, y0..y1 are the visible outer edges: the stroke sits inside them
    h = FRAME_STROKE / 2
    X0, Y0, X1, Y1 = x0 * k.K + h, y0 * k.K + h, x1 * k.K - h, y1 * k.K - h
    return (f'<rect style="fill:none;stroke:{k.INK};stroke-width:{FRAME_STROKE}" x="{X0:.4f}" y="{Y0:.4f}" '
            f'width="{X1 - X0:.4f}" height="{Y1 - Y0:.4f}" rx="{FRAME_RADIUS}"/>')

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('ChUTE', PW), k.screen(*SCREEN)]
    # the groups: frame, name, the line from the CV jack to its knob (drawn under them, only the gap shows)
    for (y0, y1), y, name in zip(FRAME_Y, GROUP_Y, NAMES):
        out.append(frame(FRAME_X0, y0, FRAME_X1, y1))
        out.append(k.knob_label(name, (FRAME_X0 + FRAME_X1) / 2, y))
        out.append(hline(CV_X, KNOB_X, y))
    # TRIG on row 1, its line to the drop button over ALT
    out.append(k.grid_box(0, 0, N, 1, PW, k.INPUT))
    out.append(k.jack_label('TRIG', k.jack_col(0, N, PW), k.row(1)))
    out.append(hline(k.box_right(0, N, PW), k.jack_col(1, N, PW) - BUTTON_HALF, k.row(1)))
    # the outputs on the bottom row, one box each
    for i, name in enumerate(('FALL', 'ALT', 'TOP')):
        out.append(k.grid_box(i, i, N, 0, PW, k.OUTPUT))
        out.append(k.jack_label(name, k.jack_col(i, N, PW), k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('CHUTE.svg', build())
