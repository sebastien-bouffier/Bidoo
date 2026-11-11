# pARtiCuLeS's panel (32 HP). The picture: a particle collision as a detector shows it: tracks curling out of the vertex
# (tight spirals, wide arcs, a few straight jets), the detector's segmented rings with the hits on them and calorimeter
# towers outside, larger than the panel so that it runs off every edge. An original drawing, lightly mangled.
# Everything sits on the grid, as src/PARTICULES.cpp places it.
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
    p = k.Picture(1912, W)
    R = p.R
    H = k.H
    def P(x, y):
        return (x * K, y * K)
    def line(pts, w):
        # a polyline in px as a ribbon w px wide
        left, right = [], []
        for i, (x, y) in enumerate(pts):
            a = pts[max(0, i - 1)]
            b = pts[min(len(pts) - 1, i + 1)]
            dx, dy = b[0] - a[0], b[1] - a[1]
            n = math.hypot(dx, dy) or 1.0
            nx, ny = -dy / n * w / 2, dx / n * w / 2
            left.append(P(x + nx, y + ny))
            right.append(P(x - nx, y - ny))
        p.add(left + right[::-1])
    def dot(x, y, r, n=8):
        p.add([P(x + r * math.cos(2 * math.pi * i / n), y + r * math.sin(2 * math.pi * i / n)) for i in range(n)])
    cx, cy = PW * .42, 172           # the vertex, in px
    # the detector: rings in segments, larger than the panel
    rings = [38, 74, 118, 176, 250, 340]
    for rr in rings:
        n = int(rr * .9)
        i = 0
        a0 = R.uniform(0, 6.28)
        while i < n:
            run = R.randint(4, 10)
            pts = [(cx + rr * math.cos(a0 + 2 * math.pi * (i + j) / n), cy + rr * math.sin(a0 + 2 * math.pi * (i + j) / n)) for j in range(run + 1)]
            line(pts, .55 if rr < 200 else .8)
            i += run + 1
    # calorimeter towers outside the last ring
    for j in range(140):
        a = 2 * math.pi * j / 140
        e = R.random() ** 3 * 60
        if e < 3:
            continue
        r0 = 352
        w = 2 * math.pi * r0 / 140 * .7
        c, s_ = math.cos(a), math.sin(a)
        p.add([P(cx + r0 * c - s_ * w / 2, cy + r0 * s_ + c * w / 2), P(cx + (r0 + e) * c - s_ * w / 2, cy + (r0 + e) * s_ + c * w / 2),
               P(cx + (r0 + e) * c + s_ * w / 2, cy + (r0 + e) * s_ - c * w / 2), P(cx + r0 * c + s_ * w / 2, cy + r0 * s_ - c * w / 2)])
    # tracks: arcs of a circle through the vertex (charge, momentum), some curling in a tightening spiral
    def track(a0, radius, sign, length, curl):
        pts = []
        x, y, a = cx, cy, a0
        step = 3.0
        rad = radius
        dist = 0.0
        hit = 0
        while dist < length:
            x += step * math.cos(a)
            y += step * math.sin(a)
            a += sign * step / rad
            if curl:
                rad = max(4.0, rad * .985)
            dist += step
            pts.append((x, y))
            d = math.hypot(x - cx, y - cy)
            if hit < len(rings) and d >= rings[hit]:
                dot(x, y, 1.3)
                hit += 1
        line([(cx, cy)] + pts, .7)
    for j in range(46):
        a0 = R.uniform(0, 2 * math.pi)
        kind = R.random()
        if kind < .2:
            track(a0, R.uniform(20, 60), R.choice((-1, 1)), R.uniform(150, 420), True)
        elif kind < .85:
            track(a0, R.uniform(90, 900), R.choice((-1, 1)), R.uniform(160, 520), False)
        else:
            track(a0, 1e5, 1, 640, False)       # a straight, hard track: out through every ring and the panel
    # two jets: bunches of near-straight tracks
    for ja in (R.uniform(0, 3.14), R.uniform(3.14, 6.28)):
        for j in range(9):
            track(ja + R.gauss(0, .06), R.uniform(1500, 6000), R.choice((-1, 1)), R.uniform(420, 600), False)
    dot(cx, cy, 3.2, 12)
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
