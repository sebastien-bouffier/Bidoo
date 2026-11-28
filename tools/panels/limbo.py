# lIMbO's panel (8 HP), migrated from the hand-drawn one: the same arrangement (the cutoff knob, RES and GAIN either
# side below it, F.MOD and the mode switch, three CV jacks, the stereo in and out), now in the theme's boxes, type and
# colours, with the old picture kept (limbo_parts.svg). The knob centres are the same in src/LIMBO.cpp.
import os, re
import panel_kit as k

PW = 120
W = PW * k.K
CUTOFF = (60.0, 89.051)            # BidooHugeBlueKnob centred, at the old corner's height (61), in src/LIMBO.cpp
Q = (30.75, 161.75)                # BidooLargeBlueKnob at (12, 143)
MUG = (2 * CUTOFF[0] - Q[0], Q[1]) # mirrored about the cutoff knob
CMOD = (30.75, 226.75)             # at (12, 208)
SWITCH = (MUG[0], 227.321)         # CKSS under GAIN, at the old corner's height (217)
LABEL_GAP = k.KNOB_LABEL_ABOVE - 15    # a big knob's label: 7 px above its top, the same gap for the larger knobs
CV_NAMES = ('FREQ', 'RES', 'GAIN')
STEREO_Y = 347.5                   # the tiny stereo jacks' centre (old corner 340), the same in src/LIMBO.cpp

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'limbo_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('lIMbO', PW)]
    out.append(k.text('FREQ', CUTOFF[0], CUTOFF[1] - 56.1023622 / 2 - LABEL_GAP, k.KNOB_LABEL_SIZE, k.INK))
    for lab, (x, y) in (('RES', Q), ('GAIN', MUG), ('F.MOD', CMOD)):
        out.append(k.text(lab, x, y - 37.5 / 2 - LABEL_GAP, k.KNOB_LABEL_SIZE, k.INK))
    # the switch: its two positions above and below it (CKSS is 20.6 px tall)
    out.append(k.text('NON LIN', SWITCH[0], SWITCH[1] - 10.32 - 4, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.text('LIN', SWITCH[0], SWITCH[1] + 10.32 + 4 + k.CAP_HEIGHT * k.BOX_LABEL_SIZE, k.BOX_LABEL_SIZE, k.INK))
    # the CV jacks on the second row, one box each
    for i, lab in enumerate(CV_NAMES):
        out.append(k.grid_box(i, i, 3, 1, PW, k.INPUT))
        out.append(k.jack_label(lab, k.jack_col(i, 3, PW), k.row(1)))
    # the stereo in and out: one box each over two columns, its name on top and L, R over the tiny jacks
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
    k.write('LIMBO.svg', build())
