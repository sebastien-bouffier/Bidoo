# ziNC's panel (13 HP), migrated from the hand-drawn one: the same arrangement (the 4 x 4 band knobs with their
# frequencies, which the widget prints, then ATTACK, DECAY and the four Q trimpots, the MOD, CAR and GAIN knobs, and the
# MOD, CAR and OUT jacks), now in the theme's boxes, type and colours, with the old picture kept (zinc_parts.svg). The
# positions are the same in src/ZINC.cpp.
import os, re
import panel_kit as k

PW = 195
W = PW * k.K
TRIM_HALF = 19.1929131 / 2
TRIM_RIGHT = 32.0                  # between trimpots (src/ZINC.cpp kTrimpotRight)
TRIM_X0 = PW / 2 - 2.5 * TRIM_RIGHT    # the six centred on the panel (they were 0.1 px right of it), in src/ZINC.cpp
TRIM_Y = 230 + TRIM_HALF           # at the old corner's height 230 (src/ZINC.cpp attack)
TRIMS = ('ATTACK', 'DECAY', 'Q1', 'Q2', 'Q3', 'Q4')
LABEL_GAP = k.KNOB_LABEL_ABOVE - 15    # 7 px above a knob's top, as for a big knob
GAIN_RIGHT = 41.667                # CAR on the panel's centre, MOD and GAIN either side (src/ZINC.cpp kGainRight)
GAIN_Y = 268 + 29.5275591 / 2      # BidooBlueKnob at the old corner's height 268 (src/ZINC.cpp gainRow)
GAINS = ('MOD', 'CAR', 'GAIN')
JACKS = ((0, 'MOD', k.INPUT), (2, 'CAR', k.INPUT), (4, 'OUT', k.OUTPUT))   # columns of five (src/ZINC.cpp)

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'zinc_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('ziNC', PW)]
    # the trimpots: small, so their names at 7.5 (ATTACK and DECAY would touch at 8.5)
    for i, lab in enumerate(TRIMS):
        out.append(k.text(lab, TRIM_X0 + TRIM_RIGHT * i, TRIM_Y - TRIM_HALF - LABEL_GAP, k.BOX_LABEL_SIZE, k.INK))
    for i, lab in enumerate(GAINS):
        out.append(k.knob_label(lab, PW / 2 + GAIN_RIGHT * (i - 1), GAIN_Y))
    # the jacks: MOD, CAR and OUT on the bottom row's first, middle and last of five columns
    for i, name, colour in JACKS:
        out.append(k.grid_box(i, i, 5, 0, PW, colour))
        out.append(k.jack_label(name, k.jack_col(i, 5, PW), k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('ZINC.svg', build())
