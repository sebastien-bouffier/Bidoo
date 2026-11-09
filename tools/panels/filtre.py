# FiLTRe's panel (28 HP). The picture: a pleated coffee filter, stretched and mangled.
import math
import panel_kit as k

PW = 420
K = k.K
W = PW * K
SCREEN_H = 190                      # the same in src/FiltreWidget.hpp
KNOB_ROW = 256                      # likewise
KNOB_XS = [82 + 51 * i for i in range(6)]   # likewise

def build():
    p = k.Picture(71, W)
    R = p.R
    H = k.H
    cx = W * .52
    top, bot = H * .1, H * .97
    tw, bw = W * .5, W * .1
    N = 15
    def edge(u, t):
        w = tw + (bw - tw) * t
        return (cx + (u - .5) * 2 * w, top + (bot - top) * t)
    for i in range(0, N, 2):
        u0, u1 = i / N, (i + 1) / N
        L = [edge(u0, j / 12) for j in range(13)]
        Rr = [edge(u1, j / 12) for j in range(13)]
        p.add(L + Rr[::-1])
    p.add([(cx - tw * 1.04, top - 1.4), (cx + tw * 1.04, top - 1.4), (cx + tw * 1.03, top + .8), (cx - tw * 1.03, top + .8)])
    for j in range(30):
        t = R.uniform(.8, 1.0)
        x = cx + R.uniform(-1, 1) * (tw + (bw - tw) * t) * 1.4
        y = top + (bot - top) * t + R.uniform(0, 3)
        a = R.uniform(0, 6.28)
        r = R.uniform(.3, .9)
        p.add([(x + r * math.cos(a + i * 1.57), y + r * math.sin(a + i * 1.57)) for i in range(4)])
    p.slices(1.2, 7, [0, 0, 1.6, -2.2, 3.8, -4.6, 6.4])

    def warp(X, Y):
        T = (Y - top) / (bot - top)
        X2 = X + (T - .3) * (T - .3) * 22 - math.sin(T * 5) * 5
        return X2 + p.shift(Y), Y

    out = [k.header(PW, W), k.background(W), p.path(warp, .6), k.title('FiLTRe', PW)]
    out.append(k.screen(k.MARGIN, k.SCREEN_TOP, PW - 2 * k.MARGIN, SCREEN_H))
    for x, l, c in zip(KNOB_XS, ['TYPE', 'CUTOFF', 'RES', 'DRIVE', 'MORPH', 'MIX'], ['TYPE', 'V/OCT', 'RES', 'DRIVE', 'MORPH', 'MIX']):
        # the line from the knob down through the attenuverter to its CV box
        out.append(f'<path style="fill:none;stroke:{k.INK};stroke-width:0.32" d="M{x*K:.3f},{KNOB_ROW*K:.3f}V{(k.BOTTOM_TOP+4)*K:.3f}"/>')
        out.append(k.text(l, x, KNOB_ROW - 23, k.KNOB_LABEL_SIZE, k.INK))
        out.append(k.jack_box(x, k.BOTTOM_ROW, 40, k.INPUT))
        out.append(k.jack_label(c, x, k.BOTTOM_ROW))
    for x, col, ls in ((30, k.INPUT, ('IN L', 'IN R')), (390, k.OUTPUT, ('OUT L', 'OUT R'))):
        for y, l in zip((KNOB_ROW, k.BOTTOM_ROW), ls):
            out.append(k.jack_box(x, y, 40, col))
            out.append(k.jack_label(l, x, y))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('FILTRE.svg', build())
