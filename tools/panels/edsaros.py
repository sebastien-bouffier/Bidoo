# eDsaroS's panel (9 HP), migrated from the hand-drawn one: the same arrangement (the screen; a grid of small knobs,
# five across, each with its tiny CV jack under it, the three mode screens in the last column in place of CV jacks;
# GATE, V/O, the zero crossing button and OUT), now in the theme's screens, boxes, type and colours, with the old
# picture kept (edsaros_parts.svg). The positions are the same in src/EDSAROS.cpp (EDSAROSWidget).
import os, re
import panel_kit as k

PW = 135
W = PW * k.K
N = 4                                  # the bottom row's columns, the same in src/EDSAROS.cpp
KNOB_HALF = 23.6220472 / 2             # BidooSmallBlueKnob (8 mm)
ACROSS, DOWN = 26.0, 28.0              # from one knob column to the next, from a knob row to its CV row (src/EDSAROS.cpp)
KNOB_X = tuple(3.5 + KNOB_HALF + ACROSS * c for c in range(5))   # old corner x 3.5; the CV jacks on these columns
                                                                 # (they were 0.19 px right)
def knob_y(r):
    return 97 + KNOB_HALF + DOWN * r   # old corner y 97; knobs on the even rows

def cv_y(r):
    return 97 + DOWN + 7.5 + DOWN * r  # the tiny CV jacks under them, old corner 125

# The screen where the widget draws the sample: the theme's margin, the display's old top and height (the theme's top
# would take 9 of its 60 px: the knob labels under it leave no room), the same in src/EDSAROS.cpp (EDSAROSDisplay's box)
SCREEN = (k.MARGIN, 25.0, PW - 2 * k.MARGIN, 60.0)
# the mode screens in the last column, in place of the CV jacks under L MODE, R MODE and LINK, as before (22 x 11.9 at
# x 108.4), the same in src/EDSAROS.cpp (kModeScreen)
MODE_SCREEN = (22.0, 12.0)
# The labels: 6.5 (five across 26 px columns: at 7.5 S STRT and S END would almost touch), their capitals centred
# between the CV jack above and the knob, the old place (0.05 px lower); every row the same
LABEL_SIZE = 6.5
LABELS = {0: ('S STRT', 'S END', 'L STRT', 'L END', 'R STRT'), 2: ('ATT', 'DEC', 'REL', None, 'L MODE'),
          4: ('SLOPE', 'SLOPE', 'SLOPE', None, 'R MODE'), 6: ('INIT', 'PEAK', 'SUS', 'GAIN', 'LINK')}
BUTTON_X = k.col(2, N, PW)             # the zero crossing button in the third column (2.1 px left), on the jacks' row

def label_above():
    # between the CV jack's bottom (7.5 under its centre) and the next knob's top
    gap = knob_y(2) - KNOB_HALF - (cv_y(0) + 7.5)
    return KNOB_HALF + (gap - k.CAP_HEIGHT * LABEL_SIZE) / 2

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'edsaros_parts.svg'), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('eDsaroS', PW), k.screen(*SCREEN)]
    for r in (2, 4, 6):
        w, h = MODE_SCREEN
        out.append(k.screen(KNOB_X[4] - w / 2, cv_y(r) - h / 2, w, h))
    above = label_above()
    for r, names in LABELS.items():
        for c, lab in enumerate(names):
            if lab:
                out.append(k.text(lab, KNOB_X[c], knob_y(r) - above, LABEL_SIZE, k.INK))
    out.append(k.small_label('Z+', BUTTON_X, k.row(0)))
    for i, lab, colour in ((0, 'GATE', k.INPUT), (1, 'V/O', k.INPUT), (3, 'OUT', k.OUTPUT)):
        out.append(k.grid_box(i, i, N, 0, PW, colour))
        out.append(k.jack_label(lab, k.jack_col(i, N, PW), k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('EDSAROS.svg', build())
