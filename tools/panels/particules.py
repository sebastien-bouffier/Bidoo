# pARtiCuLeS's panel (32 HP). The picture: a cloud of particles swirling out of a bright core, specks and streaks that
# run off every edge, lightly mangled. An original drawing. Everything sits on the grid, as src/PARTICULES.cpp places it.
import math
import panel_kit as k

PW = 480
K = k.K
W = PW * K
N = 10                              # jack columns, the same in src/PARTICULES.cpp
KNOBS = (                           # likewise: row, columns, labels ('' = not a big knob)
    (4, 8, ['POSITION', 'SPRAY', 'SCAN', 'SIZE', 'RATE', 'CHAOS', 'COUNT', '']),
    (3, 8, ['SHAPE', 'SOFT', 'TUNE', 'DETUNE', 'WIDTH', 'DIRECTION', '', '']),
    (2, 8, ['DECAY', 'ROOM', 'TONE', 'SHIMMER', 'INTERVAL', 'MIX', 'LEVEL', '']),
)
SIZE = 7                            # jack captions: the columns are 46 px apart
LABEL_ABOVE = 20                    # knob and button labels: centred between their knob's top and the bottom of
                                    # the knob above (three rows 46 apart; the theme's 22 sat on the knob above)

def build():
    p = k.Picture(1911, W)
    R = p.R
    H = k.H
    def P(x, y):
        return (x * K, y * K)
    def speck(x, y, r, n=7):
        a = R.uniform(0, 6.28)
        p.add([P(x + r * (1 + R.uniform(-.25, .25)) * math.cos(a + 2 * math.pi * i / n),
                 y + r * (1 + R.uniform(-.25, .25)) * math.sin(a + 2 * math.pi * i / n)) for i in range(n)])
    def streak(x, y, ang, length, w):
        dx, dy = math.cos(ang), math.sin(ang)
        p.add([P(x - dy * w, y + dx * w), P(x + dx * length - dy * w * .2, y + dy * length + dx * w * .2),
               P(x + dx * length + dy * w * .2, y + dy * length - dx * w * .2), P(x + dy * w, y - dx * w)])
    # a spiral cloud in px around a core left of centre, larger than the panel so that it runs off every edge
    cx, cy = PW * .38, 190
    for j in range(900):
        t = R.uniform(0, 1) ** .7
        arm = R.randint(0, 2)
        ang = arm * 2.094 + t * 7.5 + R.gauss(0, .35)
        rad = 6 + t * 520
        x, y = cx + rad * math.cos(ang), cy + .62 * rad * math.sin(ang)
        if -20 < x < PW + 20 and -20 < y < 400:
            r = max(.5, R.gauss(1.5 - t, .5))
            if R.random() < .14:
                streak(x, y, ang + 1.57, 4 + 10 * t * R.random(), r * .6)
            else:
                speck(x, y, r)
    # the core: a few rings of larger specks
    for j in range(40):
        a = R.uniform(0, 6.28)
        rr = R.uniform(2, 14)
        speck(cx + rr * math.cos(a), cy + .7 * rr * math.sin(a), R.uniform(1.2, 2.6))
    p.slices(1.3, 8, [0, 0, .4, -.6, .9, -1.2, 1.6])

    def warp(X, Y):
        V = (Y - H * .5) / (H * .5)
        X2 = X + math.sin(V * 3.1) * .8
        Y2 = Y + math.sin(X / W * 5.0) * .6
        return X2 + p.shift(Y2), Y2

    out = [k.header(PW, W), k.background(W), p.path(warp, .5), k.title('pARtiCuLeS', PW)]
    out.append(k.screen(k.MARGIN, k.SCREEN_TOP, PW - 2 * k.MARGIN, k.screen_above_knobs(4) - k.SCREEN_TOP))   # ParticulesScreen
    for r, n, labels in KNOBS:
        for i, l in enumerate(labels):
            if l:
                out.append(k.text(l, k.col(i, n, PW), k.row(r) - LABEL_ABOVE, k.KNOB_LABEL_SIZE, k.INK))
    # CLOCK, REC and FREEZE: round buttons (CLOCK lit: on the clock; off: free running), their labels on the knob
    # labels' baseline so that each row keeps one (docs/THEME.md, "Alignment")
    for l, i, r in (('CLOCK', 7, 4), ('REC', 6, 3), ('FREEZE', 7, 3)):
        out.append(k.text(l, k.col(i, 8, PW), k.row(r) - LABEL_ABOVE, k.BOX_LABEL_SIZE, k.INK))
    def boxed(i0, labels, k_row, colour):
        out.append(k.grid_box(i0, i0 + len(labels) - 1, N, k_row, PW, colour))
        for j, l in enumerate(labels):
            out.append(k.jack_label(l, k.jack_col(i0 + j, N, PW), k.row(k_row), SIZE))
    boxed(0, ['POS', 'SPRAY', 'SIZE', 'RATE', 'SHAPE', 'DETUNE', 'MIX', 'TRIG', 'REC', 'FREEZE'], 1, k.INPUT)
    boxed(0, ['IN L', 'IN R', 'V/O', 'CLOCK', 'RESET', 'SCAN'], 0, k.INPUT)
    boxed(6, ['GRAIN', 'ENV', 'OUT L', 'OUT R'], 0, k.OUTPUT)
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('PARTICULES.svg', build())
