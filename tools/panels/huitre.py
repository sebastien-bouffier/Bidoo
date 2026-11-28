# 8re's panel (10 HP), migrated from the hand-drawn one: the same arrangement (the gate mode button under the title;
# eight rows of a pattern button, the PTN, CV1 and CV2 trimpots and a tiny gate out under GATE; STEP and the three
# outputs at the bottom), now in the theme's boxes, type and colours, with the old picture kept (huitre_parts.svg).
# The positions are the same in src/HUITRE.cpp.
import os, re
import panel_kit as k

PW = 150
W = PW * k.K
N = 4                                  # the bottom row's columns, the same in src/HUITRE.cpp
TRIM_HALF = 19.1929131 / 2             # BidooBlueTrimpot
# the rows: the old ones 4 px lower, so that the mode button above them gets its label under the screw (src/HUITRE.cpp
# kRowsDown); 33 px apart (kRowBelow)
ROWS_DOWN = 4.0
ROW_Y = tuple(52 + TRIM_HALF + ROWS_DOWN + 33.0 * i for i in range(8))
TRIM_X = tuple(45 + TRIM_HALF + 27.0 * i for i in range(3))   # PTN, CV1, CV2: the old corner 45, 27 apart (kTrimpotRight)
STEP_X = k.jack_col(0, N, PW)          # the pattern buttons and the mode button over STEP (they were 3.6 px left)
GATE_X = 130 + 7.5                     # the tiny gate outs: old corner 130 (src/HUITRE.cpp)
# the mode button's label sits under the left screw (y 0 to 15): its capitals from 16.5, the button 18 below
MODE_LABEL_Y = 16.5 + k.CAP_HEIGHT * k.BOX_LABEL_SIZE
MODE_Y = MODE_LABEL_Y + k.SMALL_LABEL_ABOVE   # the same in src/HUITRE.cpp (kModeY): 7 px lower than before
STRIP_W = 20.0                         # the gate strip around the tiny jacks, 2.5 px each side
STRIP_TOP = 16.0                       # the old GATE box's top (15.8), under the right screw
STRIP_BOTTOM = ROW_Y[-1] + 7.5 + k.BOX_GAP
GATE_SIZE = 7.5

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'huitre_parts.svg'), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def rotated(s, x, y, size, col):
    # a label reading upwards, its capitals centred on (x, y), as the old GATE
    return (f'<g transform="translate({x * k.K:.4f},{y * k.K:.4f}) rotate(-90)">'
            f'{k._text(s, 0, k.CAP_HEIGHT * size / 2, size, col)}</g>')

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('8re', PW)]
    # the mode button: lit, the pattern outs give gates; off, triggers
    out.append(k.small_label('GATE', STEP_X, MODE_Y))
    # the trimpots' columns, named over the first row at a knob's offset
    for lab, x in zip(('PTN', 'CV1', 'CV2'), TRIM_X):
        out.append(k.knob_label(lab, x, ROW_Y[0]))
    # the eight gate outs: one red strip, GATE written upwards at its top as before
    out.append(k.box(GATE_X - STRIP_W / 2, STRIP_TOP, STRIP_W, STRIP_BOTTOM - STRIP_TOP, k.OUTPUT))
    out.append(rotated('GATE', GATE_X, (STRIP_TOP + ROW_Y[0] - 7.5) / 2, GATE_SIZE, k.WHITE))
    # the bottom row: STEP in, the three outputs, one box each
    for i, (lab, colour) in enumerate((('STEP', k.INPUT), ('PTN', k.OUTPUT), ('CV1', k.OUTPUT), ('CV2', k.OUTPUT))):
        out.append(k.grid_box(i, i, N, 0, PW, colour))
        out.append(k.jack_label(lab, k.jack_col(i, N, PW), k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('HUITRE.svg', build())
