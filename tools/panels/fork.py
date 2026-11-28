# ForK's panel (6 HP), migrated from the hand-drawn one: the same arrangement (the pitch knob, the four formants as F
# and G rows of a trimpot, its letter and its CV jack, joined by a bracket, the presets button, V/O and OUT), the names
# written down the left side as before, now in the theme's boxes, type and colours, with the old picture kept
# (fork_parts.svg). The positions are the same in src/FORK.cpp.
import os, re
import panel_kit as k

PW = 90
W = PW * k.K
PITCH = (PW / 2, 58.75)              # BidooLargeBlueKnob at its old height (corner 40), on the panel's centre (0.25 px)
PRESET = (PW / 2, 288.0)             # BidooRoundButton at the old CKD6's centre, on the knob's column (1 px)
ROWS = (93.469, 113.688, 142.156, 162.406, 190.344, 210.562, 238.219, 258.438)   # F, G, F, G...: the old letters' middles (src/FORK.cpp)
LETTER_X = 57.7                      # the letters, between the trimpots and the tiny CV jacks
# the names down the left side, read from below: their capitals' middle on one x
SIDE_X, SIDE_SIZE = 8.8, 7.5
# the bracket joining the four formants: a vertical rule with a tick to each formant's middle, and one to its name
BRACKET_X, TICK_RIGHT, TICK_LEFT = 20.4, 26.25, 15.0
BRACKET_WIDTH = 0.5
# the bottom row: one box each, 5 px from the panel's sides as the old ones (the 3 columns of 6 HP are too narrow for a
# full jack), the same in src/FORK.cpp (kBoxLeft, kBoxRight)
BOX_W = 29.0
BOXES = ((5.0, 'V/O', k.INPUT), (PW - 5.0 - BOX_W, 'OUT', k.OUTPUT))

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fork_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def side_text(s, y):
    # rotated a quarter turn, its capitals' middle at (SIDE_X, y)
    c = k.CAP_HEIGHT * SIDE_SIZE / 2
    return (f'<g transform="rotate(-90,{SIDE_X * k.K:.4f},{y * k.K:.4f})">'
            f'{k._text(s, SIDE_X, y + c, SIDE_SIZE, k.INK)}</g>')

def bracket():
    mids = [(ROWS[2 * i] + ROWS[2 * i + 1]) / 2 for i in range(4)]
    centre = (mids[0] + mids[-1]) / 2
    d = f'M{TICK_RIGHT * k.K:.4f},{mids[0] * k.K:.4f}H{BRACKET_X * k.K:.4f}V{mids[-1] * k.K:.4f}H{TICK_RIGHT * k.K:.4f}'
    for y in mids[1:-1]:
        d += f'M{BRACKET_X * k.K:.4f},{y * k.K:.4f}H{TICK_RIGHT * k.K:.4f}'
    d += f'M{TICK_LEFT * k.K:.4f},{centre * k.K:.4f}H{BRACKET_X * k.K:.4f}'
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:{BRACKET_WIDTH}" d="{d}"/>', centre

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('ForK', PW)]
    b, centre = bracket()
    out.append(b)
    out.append(side_text('PITCH', PITCH[1]))
    out.append(side_text('FORMANTS', centre))
    out.append(side_text('PRESETS', PRESET[1]))
    for j, y in enumerate(ROWS):
        out.append(k.text('FG'[j % 2], LETTER_X, y + k.CAP_HEIGHT * k.BOX_LABEL_SIZE / 2, k.BOX_LABEL_SIZE, k.INK))
    for x, s, colour in BOXES:
        out.append(k.box(x, k.box_top(0), BOX_W, k.box_bottom(0) - k.box_top(0), colour))
        out.append(k.jack_label(s, x + BOX_W / 2, k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('FORK.svg', build())
