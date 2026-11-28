# lambda's panel (3 HP), migrated from the hand-drawn one: the same arrangement (eight boxes down the middle, CLK and IN
# then the six outputs, the title λ above them), now in the theme's boxes, type and colours, with the old picture kept
# (lambda_parts.svg). No screws, as before. The file is lambda_.py because lambda is a Python keyword. The positions are
# the same in src/LAMBDA.cpp.
import panel_kit as k
import frein as f                     # the 3 HP title, logo, line and boxes (frein.py)

PW = 45
W = PW * k.K
COL = k.jack_col(0, 1, PW)             # the panel's centre: the jacks were 0.15 px left of it
# Eight boxes of the theme's 25 + 17 px with 2 px between them need 350 px: the top one would reach 9, over the title.
# So the boxes keep the theme's 25 px above the jack (the captions where every module has them) and 15 below (3.75 px
# under the jack), 2 px apart: from the bottom jack on 342 up to the first box's top at 23, 6 px under the title.
# The same in src/LAMBDA.cpp (kBoxBelow, kBoxGap).
BOX_BELOW = 15.0
BOX_GAP = 2.0
PITCH = k.BOX_ABOVE + BOX_BELOW + BOX_GAP   # 42 (the old boxes were 43.875 apart)
NAMES = (('CLK', k.INPUT), ('IN', k.INPUT)) + (('OUT', k.OUTPUT),) * 6

def jack_y(i):
    # box i from the top; the last on the bottom row
    return k.row(0) - PITCH * (len(NAMES) - 1 - i)

def build():
    out = [k.header(PW, W), k.background(W), f.parts('lambda_parts.svg'), f.title('λ')]   # its old title: 16, the theme's size
    x0, x1 = k.box_left(0, 1, PW), k.box_right(0, 1, PW)   # 10 to 35: a full jack (23.7) fits the theme's margins
    for i, (name, colour) in enumerate(NAMES):
        y = jack_y(i)
        out.append(k.box(x0, y - k.BOX_ABOVE, x1 - x0, k.BOX_ABOVE + BOX_BELOW, colour))
        out.append(k.jack_label(name, COL, y))
    out.append(f.logo())   # scaled to leave 3 px each side, as the other 3 HP panels
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('LAMBDA.svg', build())
