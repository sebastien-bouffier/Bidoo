# BIsTrot's panel (8 HP), migrated from the hand-drawn one: the same arrangement (the two clock jacks in their boxes,
# two strips of eight tiny jacks, bits out on the red one under A/D and bits in on the black one under D/A, with a light
# beside each, IN and OUT), now in the theme's boxes, type and colours, with the old picture kept (bistrot_parts.svg).
# The positions are the same in src/BISTROT.cpp.
import os, re
import panel_kit as k

PW = 120
W = PW * k.K
# the A/D and D/A columns, symmetric about the centre (the jacks were 0.15 px left, the strips 0.2 and 0 px right):
# src/BISTROT.cpp kColumnOut
COLUMN_OUT = 19.0
COL = (PW / 2 - COLUMN_OUT, PW / 2 + COLUMN_OUT)
BOX_W = 28.0                           # one box per full jack (the old ones were 28.3 px)
CLK_Y = 46 + 23.7 / 2                  # the clock jacks, old corner 46 (src/BISTROT.cpp)
STRIP_W = 24.0                         # the strips of tiny jacks, the old width
STRIP_TOP, STRIP_BOTTOM = 87.5, 294.5  # the old edges (87.44, 294.44)
LABEL_X = 10.0                         # A/D and D/A from the panel's sides, rotated as before (they were at 10.1 and 8.4)
LABEL_SIZE = 8.5                       # the theme's label size (the old ones were 11)

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bistrot_parts.svg'), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def rotated(s, x, y):
    # a label reading upwards, its capitals centred on (x, y)
    return (f'<g transform="translate({x * k.K:.4f},{y * k.K:.4f}) rotate(-90)">'
            f'{k._text(s, 0, k.CAP_HEIGHT * LABEL_SIZE / 2, LABEL_SIZE, k.INK)}</g>')

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('BIsTrot', PW)]   # 56.4 px wide: 1.8 px from the screws
    for x in COL:
        out.append(k.box(x - BOX_W / 2, CLK_Y - k.BOX_ABOVE, BOX_W, k.BOX_ABOVE + k.BOX_BELOW, k.INPUT))
        out.append(k.jack_label('CLK', x, CLK_Y))
    for x, colour in zip(COL, (k.OUTPUT, k.INPUT)):
        out.append(k.box(x - STRIP_W / 2, STRIP_TOP, STRIP_W, STRIP_BOTTOM - STRIP_TOP, colour))
    mid = (STRIP_TOP + STRIP_BOTTOM) / 2
    out.append(rotated('A/D', LABEL_X, mid))
    out.append(rotated('D/A', PW - LABEL_X, mid))
    for x, lab, colour in ((COL[0], 'IN', k.INPUT), (COL[1], 'OUT', k.OUTPUT)):
        out.append(k.box(x - BOX_W / 2, k.box_top(0), BOX_W, k.BOX_ABOVE + k.BOX_BELOW, colour))
        out.append(k.jack_label(lab, x, k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('BISTROT.svg', build())
