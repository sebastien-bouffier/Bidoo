# pErCO's panel (8 HP), migrated from the hand-drawn one: the same arrangement (the cutoff knob, RES and F.MOD either
# side below it, three input jacks over three outputs), now in the theme's boxes, type and colours, with the old
# picture kept (perco_parts.svg). The knob centres are the same in src/PERCO.cpp; the jacks are on the grid there too.
import os, re
import panel_kit as k

PW = 120
W = PW * k.K
CUTOFF = (PW / 2, 89.051)            # BidooHugeBlueKnob on the panel's centre, at the old corner's height (61), in src/PERCO.cpp
CMOD = (89.75, 161.75)               # BidooLargeBlueKnob at the old corner (71, 143), in src/PERCO.cpp
Q = (2 * CUTOFF[0] - CMOD[0], CMOD[1])   # mirrored about the cutoff knob, as in src/PERCO.cpp
LABEL_GAP = k.KNOB_LABEL_ABOVE - 15      # a big knob's label: 7 px above its top, the same gap for the larger knobs
HUGE, LARGE = 56.1023622, 37.5           # the knobs' sizes (theme::kHugeKnobSize, kLargeKnobSize)
INS = ('IN', 'FREQ', 'RES')              # row 1, three columns
OUTS = ('LP', 'BP', 'HP')                # row 0

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'perco_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('pErCO', PW)]
    out.append(k.text('FREQ', CUTOFF[0], CUTOFF[1] - HUGE / 2 - LABEL_GAP, k.KNOB_LABEL_SIZE, k.INK))
    for lab, (x, y) in (('RES', Q), ('F.MOD', CMOD)):
        out.append(k.text(lab, x, y - LARGE / 2 - LABEL_GAP, k.KNOB_LABEL_SIZE, k.INK))
    # the inputs on the second row and the outputs on the bottom row, one box each, the same three columns
    for r, names, colour in ((1, INS, k.INPUT), (0, OUTS, k.OUTPUT)):
        for i, lab in enumerate(names):
            out.append(k.grid_box(i, i, 3, r, PW, colour))
            out.append(k.jack_label(lab, k.jack_col(i, 3, PW), k.row(r)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('PERCO.svg', build())
