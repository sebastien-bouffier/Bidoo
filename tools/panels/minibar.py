# mINIBar's panel (5 HP), migrated from the hand-drawn one: the same arrangement (the bypass button under the title, eight
# trimpots down the left side, each with its name written upwards beside it, the meter screen, IN, SC and OUT), now in
# the theme's screen, boxes, type and colours, with the old picture kept (minibar_parts.svg). No screws, as before. The
# positions are the same in src/MINIBAR.cpp.
import os, re
import panel_kit as k

PW = 75
W = PW * k.K
BYPASS_Y = 29.5                        # the bypass button's centre, 2.5 px under the title, 2.5 px over the screen
                                       # (src/MINIBAR.cpp kBypassY); on the panel's centre as before
TRIM_HALF = 19.1929131 / 2
TRIM_X = 2 + TRIM_HALF                 # the trimpots at their old corners (2, 38 + 35 i), src/MINIBAR.cpp
TRIM_Y0, TRIM_PITCH = 38 + TRIM_HALF, 35.0
NAMES = ('TRSH', 'RATIO', 'ATTACK', 'RELEASE', 'KNEE', 'GAIN', 'MIX', 'AHEAD')
# the screen: the meters and the value the widget writes (src/MINIBAR.cpp screen), its right edge on OUT's box's
# (it was 0.7 px left), its top under the bypass button (it was 7 px higher)
SCREEN_X, SCREEN_Y, SCREEN_W, SCREEN_H = 30.0, 42.0, 43.0, 261.0
LABEL_X = (2 + 2 * TRIM_HALF + SCREEN_X - 0.95) / 2   # the names' capitals centred between the trimpots and the screen's frame
LABEL_SIZE = k.BOX_LABEL_SIZE
# the jacks: one box each, 2 px from the panel's sides and from each other, as the old ones (the old boxes were within
# 0.35 px of these). Tiny jacks: a full jack's ring (22.5 px) does not fit a 22.3 px box (src/MINIBAR.cpp kBox...)
BOX_SIDE, BOX_GAP = 2.0, 2.0
BOX_W = (PW - 2 * BOX_SIDE - 2 * BOX_GAP) / 3
JACKS = (('IN', k.INPUT), ('SC', k.INPUT), ('OUT', k.OUTPUT))

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'minibar_parts.svg'), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def upright(s, y):
    # a name reading upwards, its capitals centred on (LABEL_X, y), as the old ones
    c = k.CAP_HEIGHT * LABEL_SIZE / 2
    return (f'<g transform="rotate(-90,{LABEL_X * k.K:.4f},{y * k.K:.4f})">'
            f'{k._text(s, LABEL_X, y + c, LABEL_SIZE, k.INK)}</g>')

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('mINIBar', PW)]   # 64 px wide at 16: fits, no screws
    for i, s in enumerate(NAMES):
        out.append(upright(s, TRIM_Y0 + TRIM_PITCH * i))
    out.append(k.screen(SCREEN_X, SCREEN_Y, SCREEN_W, SCREEN_H))
    for i, (s, colour) in enumerate(JACKS):
        x = BOX_SIDE + (BOX_W + BOX_GAP) * i
        out.append(k.box(x, k.box_top(0), BOX_W, k.box_bottom(0) - k.box_top(0), colour))
        out.append(k.jack_label(s, x + BOX_W / 2, k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('MINIBAR.svg', build())
