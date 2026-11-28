# LoURdE's panel (5 HP), migrated from the hand-drawn one: the same arrangement (three groups of a value screen, a
# weight knob joined by a line to its tiny CV jack, and an input box below; the floor group above the output), now in the
# theme's screens, boxes, type and colours, with the old picture kept (lourde_parts.svg). No screws, as before. The
# positions are the same in src/LOURDE.cpp.
import os, re
import panel_kit as k

PW = 75
W = PW * k.K
COL = PW / 2                           # the knobs and jacks on the panel's centre (they were 0.24 and 0.15 px left)
BOX_W = 26.0                           # one box per jack, as póUPrè's (the old ones were 28.3 px)
CV_X = 56 + 15 / 2                     # the tiny CV jacks, old corner x 56 (src/LOURDE.cpp cvCol)
GROUP_PITCH = 80.0                     # src/LOURDE.cpp kGroupPitch
GROUP_Y = (45.312, 125.188, 205.125)   # the knob rows, on the old lines (src/LOURDE.cpp groupLine)
IN_Y = 77 + 23.7 / 2                   # the first input, old corner 77; then one a group
FLOOR_Y = 282 + 29.5275591 / 2         # the floor knob, old corner 282
FLOOR_CV_Y = 289 + 15 / 2              # its CV jack, old corner 289 (on the old line, 0.06 px lower)
# the value screens: the old rectangles around the text the widget prints, the same in src/LOURDE.cpp (kScreen...)
SCREEN_X, SCREEN_W = 2.44, 17.12
SCREEN_TOP, SCREEN_H = 27.81, 35.0     # the groups' screens, one a group (the second and third were 0.1 and 0.2 px higher)
FLOOR_SCREEN_TOP, FLOOR_SCREEN_H = 276.06, 41.0

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'lourde_parts.svg'), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def line(x0, x1, y):
    # the old rule from a knob to its CV jack
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:0.26458" d="M{x0 * k.K:.4f},{y * k.K:.4f}H{x1 * k.K:.4f}"/>'

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('LoURdE', PW)]   # 61.8 px wide at 16: fits, no screws
    for i in range(3):
        out.append(k.screen(SCREEN_X, SCREEN_TOP + GROUP_PITCH * i, SCREEN_W, SCREEN_H))
        out.append(line(COL, CV_X, GROUP_Y[i]))
        y = IN_Y + GROUP_PITCH * i
        out.append(k.box(COL - BOX_W / 2, y - k.BOX_ABOVE, BOX_W, k.BOX_ABOVE + k.BOX_BELOW, k.INPUT))
        out.append(k.jack_label('IN%d' % (i + 1), COL, y))
    out.append(k.screen(SCREEN_X, FLOOR_SCREEN_TOP, SCREEN_W, FLOOR_SCREEN_H))
    out.append(line(COL, CV_X, FLOOR_CV_Y))
    out.append(k.box(COL - BOX_W / 2, k.box_top(0), BOX_W, k.BOX_ABOVE + k.BOX_BELOW, k.OUTPUT))
    out.append(k.jack_label('OUT', COL, k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('LOURDE.svg', build())
