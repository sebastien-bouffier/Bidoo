# baR's panel (9 HP), migrated from the hand-drawn one: the same arrangement (the bypass button beside the title, the
# meters' screen, two rows of four trimpots under it, the stereo IN, SC and OUT at the bottom), now in the theme's
# screen, boxes, type and colours, with the old picture kept (bar_parts.svg). The positions are the same in src/BAR.cpp.
import os, re
import panel_kit as k

PW = 135
W = PW * k.K
# The screen and the boxes keep 6 px from the panel's sides (the theme's margin is 10, as oAï's boxes): BARDisplay writes
# its readouts in 14 px type centred 20 and 108 px from the panel's left, and at 10 px a usual value (10/10 for attack
# and release) would run over the screen's right edge. The same in src/BAR.cpp (kSide)
SIDE = 6.0
# the screen: from the theme's top to its old bottom, over the readouts' last line (253) and above the trimpots (265);
# the same in src/BAR.cpp (kScreenBottom)
SCREEN_BOTTOM = 259.0
# the stereo jacks: tiny (six full jacks don't fit across 9 HP), L and R 18 px apart centred in each box, at their old
# height (corner 339); the same in src/BAR.cpp (kStereoRight, kStereoY)
STEREO_RIGHT, STEREO_Y = 18.0, 339 + 15 / 2
BOXES = (('IN', k.INPUT), ('SC', k.INPUT), ('OUT', k.OUTPUT))
N = len(BOXES)

def box_width():
    return (PW - 2 * SIDE - (N - 1) * k.BOX_GAP) / N

def box_centre(i):
    w = box_width()
    return SIDE + w / 2 + i * (w + k.BOX_GAP)

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bar_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('baR', PW)]
    out.append(k.screen(SIDE, k.SCREEN_TOP, PW - 2 * SIDE, SCREEN_BOTTOM - k.SCREEN_TOP))
    # the stereo jacks: one box per pair, as before (and as lIMbO's), its name on top and L, R over the tiny jacks
    w = box_width()
    for i, (name, colour) in enumerate(BOXES):
        x = box_centre(i)
        out.append(k.box(x - w / 2, k.box_top(0), w, k.box_bottom(0) - k.box_top(0), colour))
        out.append(k.jack_label(name, x, k.row(0)))
        for j, side in enumerate('LR'):
            out.append(k.text(side, x + (j - 0.5) * STEREO_RIGHT, STEREO_Y - 7.5 - 4, 6.5, k.WHITE))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('BAR.svg', build())
