# rabBIT's panel (7 HP), migrated from the hand-drawn one: the same arrangement (eight rows of an OFF jack and button, a
# REV button and jack, under OFF and REV; the stereo in and out at the bottom), now in the theme's boxes, type and colours,
# with the old picture kept (rabbit_parts.svg). No screws, as before. The positions are the same in src/RABBIT.cpp.
import os, re
import panel_kit as k

PW = 105
W = PW * k.K
CENTRE = PW / 2
BUTTON_OUT = 15.0          # each column of buttons from the panel's centre (src/RABBIT.cpp kButtonOut)
JACK_OUT = 37.5            # each column of tiny CV jacks from the panel's centre (src/RABBIT.cpp kJackOut)
FIRST_ROW = 50 + 21.26 / 2 # the first row's centre: the old bezels' corner (50) and size (src/RABBIT.cpp)
STEREO_Y = 347.5           # the tiny stereo jacks' centre (old corner 340), the same in src/RABBIT.cpp

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rabbit_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('rabBIT', PW)]
    # OFF and REV over their first row, each centred over its pair of jack and button (as the old labels were,
    # between the two), at a round button's offset
    for lab, side in (('OFF', -1), ('REV', 1)):
        out.append(k.small_label(lab, CENTRE + side * (BUTTON_OUT + JACK_OUT) / 2, FIRST_ROW))
    # the stereo in and out: one box each over two columns, its name on top and L, R over the tiny jacks (as lIMbO)
    for i0, name, colour in ((0, 'IN', k.INPUT), (2, 'OUT', k.OUTPUT)):
        out.append(k.grid_box(i0, i0 + 1, 4, 0, PW, colour))
        mid = 0.5 * (k.box_left(i0, 4, PW) + k.box_right(i0 + 1, 4, PW))
        out.append(k.jack_label(name, mid, k.row(0)))
        for j, side in enumerate('LR'):
            out.append(k.text(side, k.col(i0 + j, 4, PW), STEREO_Y - 7.5 - 4, 6.5, k.WHITE))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('RABBIT.svg', build())
