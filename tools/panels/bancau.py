# BanCoù's panel (15 HP), migrated from the hand-drawn one: the same arrangement (two envelopes side by side, each three
# columns: RISE and FALL knobs over their CV amounts, joined by a line down to the three CV jacks, the EXP, CYC, EXP
# switches, a row of inputs over a row of outputs), now in the theme's boxes, type and colours, with the old picture
# kept (bancau_parts.svg). No screws, as before. The positions are the same in src/BANCAU.cpp.
import os, re
import panel_kit as k

PW = 225
W = PW * k.K
# The columns: the old ones (knobs, jacks and switches within 0.25 px of these), now symmetric about the panel's centre,
# 35 px apart in an envelope and 40 between the two. The same in src/BANCAU.cpp (kColumn)
COLUMNS = (22.5, 57.5, 92.5, 132.5, 167.5, 202.5)
KNOB_HALF = 29.5275591 / 2         # BidooBlueKnob
RISE_Y = 60 + KNOB_HALF            # the rise and fall knobs, old corner 60 (src/BANCAU.cpp riseRow)
CV_LINE = 175.562                  # the line through the CV jacks, their centres (src/BANCAU.cpp cvLine)
SWITCH_HALF = 20.64111 / 2         # CKSS
SWITCH_Y = 213 + SWITCH_HALF       # the switches, old corner 213 (src/BANCAU.cpp switchRow)
LINE = 0.26458                     # the old lines' width (1 px), in units
# The jacks: the inputs on the second row and the outputs on the bottom row (they were 21 and 7 px higher), one box each
# centred on its column, the theme's 4 px between boxes in an envelope (9 between the two)
BOX_W = 31.0
NAMES = ((('VO', 'IN', 'TRIG'), ('OUT', 'GATE', 'EOC')), (('VO', 'IN', 'TRIG'), ('OUT', 'NORM', 'EOC')))
SWITCHES = ('EXP', 'CYC', 'EXP')

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bancau_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def line(d):
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:{LINE}" d="{d}"/>'

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('BanCoù', PW)]
    for e in range(2):
        x0, x1, x2 = COLUMNS[3 * e: 3 * e + 3]
        # the line from the rise and fall knobs down to the CV jacks, drawn under the controls
        out.append(line(f'M{x0 * k.K:.4f},{RISE_Y * k.K:.4f}V{CV_LINE * k.K:.4f}H{x2 * k.K:.4f}V{RISE_Y * k.K:.4f}'))
        out.append(k.knob_label('RISE', x0, RISE_Y))
        out.append(k.knob_label('FALL', x2, RISE_Y))
        # the switches: their name above them, as before (no second position was written)
        for x, name in zip((x0, x1, x2), SWITCHES):
            out.append(k.text(name, x, SWITCH_Y - SWITCH_HALF - 4, k.BOX_LABEL_SIZE, k.INK))
        ins, outs = NAMES[e]
        for r, names, colour in ((1, ins, k.INPUT), (0, outs, k.OUTPUT)):
            for x, name in zip((x0, x1, x2), names):
                out.append(k.box(x - BOX_W / 2, k.box_top(r), BOX_W, k.box_bottom(r) - k.box_top(r), colour))
                out.append(k.jack_label(name, x, k.row(r)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('BANCAU.svg', build())
