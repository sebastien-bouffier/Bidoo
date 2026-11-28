# dIKT@T's panel (10 HP), migrated from the hand-drawn one: the same arrangement (the channel screen and knob with the
# global switch under them on the left; the root note and scale screens, each with its knob under it joined by a line
# to its CV jack, on the right; the note input over the seven chord outputs), now in the theme's boxes, type and
# colours, with the old picture kept (diktat_parts.svg). The positions are the same in src/DIKTAT.cpp.
# The boxes have no captions, as before: each jack writes its note's name there (DiktatPJ301MPort).
import os, re
import panel_kit as k

PW = 150
W = PW * k.K
N = 4                              # the jack columns, the same in src/DIKTAT.cpp
KNOB_HALF, CKSS_HALF = 29.5275591 / 2, 20.64111 / 2
LEFT_X = 22.062                    # the channel screen, knob, switch and their labels (src/DIKTAT.cpp leftCol)
RIGHT_X = 96.156                   # the root note and scale screens, knobs and labels (src/DIKTAT.cpp rightCol)
KNOB_Y = 55 + KNOB_HALF            # the channel and root note knobs, old corner y 55 (src/DIKTAT.cpp)
SCALE_BELOW = 73.0                 # from the root note group to the scale group (src/DIKTAT.cpp kScaleBelow)
CV_X = 120 + 7.5                   # the root note and scale CV jacks: the old tiny jacks' centre, now full jacks on their
                                   # knob's row (0.26 px lower), the same in src/DIKTAT.cpp
SWITCH_Y = 115 + CKSS_HALF         # the global switch, old corner y 115 (src/DIKTAT.cpp)
# The screens where they were (the widget writes in them at the same places), centred on their columns: top 33, as the
# old ones (the theme's 34 would leave their names no room under the title). The channel screen's left edge on the
# boxes' (10, it was 8.5), the root note's on the third column's boxes (77, it was 77.7); the scale screen keeps its
# 100 px for its names, so it reaches 146, past the boxes' edge (140), as before
SCREEN_TOP, SCREEN_H = 33.0, 15.0
def centred_screen(x, left, top):
    return (left, top, 2 * (x - left), SCREEN_H)
SCREENS = (centred_screen(LEFT_X, k.box_left(0, N, PW), SCREEN_TOP),
           centred_screen(RIGHT_X, k.box_left(2, N, PW), SCREEN_TOP),
           centred_screen(RIGHT_X, RIGHT_X - 50.0, SCREEN_TOP + SCALE_BELOW))
NAME_ABOVE = 6.0                   # a screen's name: its baseline this far above the screen (as before, 0.1 to 0.6 px)
LINE = 0.26458                     # the old lines' width, in units

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'diktat_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def hline(x0, x1, y):
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:{LINE}" d="M{x0*k.K:.4f},{y*k.K:.4f}H{x1*k.K:.4f}"/>'

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('dIKT@T', PW)]
    for s in SCREENS:
        out.append(k.screen(*s))
    # the screens' names, at a knob label's size: each names the knob under its screen
    for lab, x, top in (('CHANNEL', LEFT_X, SCREEN_TOP), ('ROOT NOTE', RIGHT_X, SCREEN_TOP),
                        ('SCALE', RIGHT_X, SCREEN_TOP + SCALE_BELOW)):
        out.append(k.text(lab, x, top - NAME_ABOVE, k.KNOB_LABEL_SIZE, k.INK))
    # the global switch: its name on the scale's baseline, its two positions above and below it (as lIMbO's)
    out.append(k.text('GLOBAL', LEFT_X, SCREEN_TOP + SCALE_BELOW - NAME_ABOVE, k.KNOB_LABEL_SIZE, k.INK))
    out.append(k.text('ON', LEFT_X, SWITCH_Y - CKSS_HALF - 4, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.text('OFF', LEFT_X, SWITCH_Y + CKSS_HALF + 4 + k.CAP_HEIGHT * k.BOX_LABEL_SIZE, k.BOX_LABEL_SIZE, k.INK))
    # the root note and scale knobs' lines to their CV jacks, under them
    for y in (KNOB_Y, KNOB_Y + SCALE_BELOW):
        out.append(hline(RIGHT_X, CV_X, y))
    # the note input on row 2, the outputs on rows 1 and 0, one box each in four columns
    out.append(k.grid_box(0, 0, N, 2, PW, k.INPUT))
    for i in range(7):
        out.append(k.grid_box(i % N, i % N, N, 1 - i // N, PW, k.OUTPUT))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('DIKTAT.svg', build())
