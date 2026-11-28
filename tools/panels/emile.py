# EMILE's panel (9 HP), migrated from the hand-drawn one: the same arrangement (the picture screen under the title,
# four colour buttons each joined by a line to its CV jack, four knobs, four boxed jacks), now in the theme's boxes, type
# and colours, with the old picture kept (emile_parts.svg). The buttons are BidooRoundButtons (orange when their colour
# is on), so they get their colour's name above them. The positions are the same in src/EMILE.cpp.
import os, re
import panel_kit as k

PW = 135
W = PW * k.K
SCREEN = (5, 30, 125, 130)          # the widget's display, which draws the image (src/EMILE.cpp)
# The four columns: one box per jack between the screen's edges (5 px from the panel's sides, as the old boxes), the
# theme's 4 px between them; the buttons, their CV jacks and the knobs share them (src/EMILE.cpp kBoxLeft, kBoxRight)
BOX_LEFT, BOX_RIGHT = SCREEN[0], SCREEN[0] + SCREEN[2]
N = 4
STRIP = (5, 163, 125, 14)           # the drawing strip under the screen, drawn by the widget (src/EMILE.cpp)
BUTTON_Y = 203.0                    # BidooRoundButton, 24 px under the old LEDButtons: room for the strip and its name
CV_Y = 229.0                        # the full CV jacks, 16 px under the buttons as before (src/EMILE.cpp)
KNOB_ROW = 1                        # the knobs on row 1 (296), 1.2 px under their old centre
BUTTON_NAMES = ('RED', 'GREEN', 'BLUE', 'ALPHA')
NAME_ABOVE = 10 + 3                  # a name's baseline above its button's centre: 3 px over its 20 px button
KNOB_NAMES = ('TUNE', 'POS', 'CURVE', 'GAIN')
JACK_NAMES = ('VO', 'POS', 'CRV', 'OUT')

def box_width():
    return (BOX_RIGHT - BOX_LEFT - (N - 1) * k.BOX_GAP) / N

def box_centre(i):
    w = box_width()
    return BOX_LEFT + w / 2 + i * (w + k.BOX_GAP)

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'emile_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def line(x, y0, y1):
    # a button's line down to its CV jack, drawn under both, so only the gap between them shows (the old line's width)
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:0.264583" d="M{x*k.K:.4f},{y0*k.K:.4f}V{y1*k.K:.4f}"/>'

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('EMILE', PW)]
    out.append(k.screen(*SCREEN))
    for i in range(N):
        x = box_centre(i)
        out.append(line(x, BUTTON_Y, CV_Y))
        # 3 px above the button's top (the user found the theme's 18 above the centre too far)
        out.append(k.text(BUTTON_NAMES[i], x, BUTTON_Y - NAME_ABOVE, k.BOX_LABEL_SIZE, k.INK))
        out.append(k.knob_label(KNOB_NAMES[i], x, k.row(KNOB_ROW)))
        out.append(k.box(x - box_width() / 2, k.box_top(0), box_width(), k.box_bottom(0) - k.box_top(0),
                         k.OUTPUT if i == N - 1 else k.INPUT))
        out.append(k.jack_label(JACK_NAMES[i], x, k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('EMILE.svg', build())
