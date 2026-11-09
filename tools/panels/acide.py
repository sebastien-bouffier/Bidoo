# aCiDe's panel (10 HP). The picture: a lemon slice, pulled tall and mangled.
import math
import panel_kit as k

PW = 150
W = 39.6875
KX = [27, 75, 123]                  # the same in src/ACIDE.cpp
KY = [72, 142, 212]                 # likewise
JX = [21, 57, 93, 129]              # likewise

def build():
    p = k.Picture(303, W)
    R = p.R
    H = k.H
    cx, cy, rad = W * .5, H * .40, 19.4
    # the rind: a ring cut in small arcs
    n = 72
    for i in range(n):
        a0, a1 = 2 * math.pi * i / n, 2 * math.pi * (i + 1.04) / n
        r0 = rad * .9 + R.uniform(-.12, .12)
        r1 = rad * (1 + R.uniform(-.015, .03))
        p.add([(cx + r1 * math.cos(a0), cy + r1 * math.sin(a0)), (cx + r1 * math.cos(a1), cy + r1 * math.sin(a1)),
               (cx + r0 * math.cos(a1), cy + r0 * math.sin(a1)), (cx + r0 * math.cos(a0), cy + r0 * math.sin(a0))])
    # nine segments with ragged pulp
    for s in range(9):
        a = 2 * math.pi * s / 9
        da = 2 * math.pi / 9 * .43
        pts = [(cx + 1.0 * math.cos(a), cy + 1.0 * math.sin(a))]
        for j in range(13):
            t = -da + 2 * da * j / 12
            r = rad * .82 * (1 + R.uniform(-.035, .035))
            pts.append((cx + r * math.cos(a + t), cy + r * math.sin(a + t)))
        p.add(pts)
    # drips running down from the slice
    for x0 in (6.5, 13.5, 27.5, 33.5):
        y0 = cy + math.sqrt(max(rad * rad - (x0 - cx) ** 2, 0)) - 1
        L = R.uniform(4, 15)
        w = R.uniform(.7, 1.3)
        p.add([(x0 - w, y0), (x0 + w, y0), (x0 + w * .6, y0 + L), (x0, y0 + L + 1.6), (x0 - w * .6, y0 + L)])
    p.slices(1.2, 7, [0, 0, .8, -1.1, 1.9, -2.3, 3.1])

    def warp(X, Y):
        # stretched: pulled tall and leaning, wider at the bottom, then the slices slide
        V = (Y - cy) / rad
        Y2 = H * .47 + V * 30.0 + math.sin(V * 2.1) * 7.0
        X2 = cx + (X - cx) * (1.32 + .28 * V) + V * V * 5.5 + math.sin(V * 4.2) * 2.4
        return X2 + p.shift(Y2) * 1.5, Y2

    out = [k.header(PW, W, '39.6875'), k.background(W), p.path(warp, .5), k.title('aCiDe', PW)]
    for lab, c, r in (('TUNE', 0, 0), ('VOLUME', 2, 0), ('CUTOFF', 0, 1), ('RES', 1, 1), ('ENV MOD', 2, 1), ('DECAY', 0, 2), ('ACCENT', 1, 2)):
        out.append(k.knob_label(lab, KX[c], KY[r]))
    out.append(k.knob_label('SQR', KX[1], KY[0]))
    out.append(k.text('SAW', KX[1], KY[0] + 26, k.KNOB_LABEL_SIZE, k.INK))
    # the output, in a red box where the ninth knob would be
    out.append(k.box(KX[2] - 20, KY[2] - 32, 40, 53, k.OUTPUT))
    # its caption at size 9, centred between the box's top (32 above the jack) and the jack's top
    cap9 = k.CAP_HEIGHT * 9
    out.append(k.text('OUT', KX[2], KY[2] - (k.JACK_HALF + (32 - k.JACK_HALF - cap9) / 2), 9, k.WHITE))
    for y, labs in ((k.SECOND_ROW, ['V/OCT', 'GATE', 'ACC', 'SLIDE']), (k.BOTTOM_ROW, ['CUT', 'RES', 'ENV', 'DEC'])):
        for x, l in zip(JX, labs):
            out.append(k.jack_box(x, y, 34, k.INPUT))
            out.append(k.jack_label(l, x, y))
    # placed by hand when this panel was made: 2 millionths of a unit from the computed place
    out.append(k.logo(W, '-9.921876'))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('ACIDE.svg', build())
