# rICOcHEt's panel (14 HP). The picture: a stone skipping on water, rings at each bounce, growing down the panel.
# Its screen is drawn by the widget, not on the panel: its margin (6) and top (36) are its own, and so is the title at 26.
import math
import panel_kit as k

PW = 210
K = k.K
W = PW * K
KX = [30, 80, 130, 180]             # the same in src/RICOCHET.cpp
KY = [166, 228]                     # likewise
JX = [20.9, 54.54, 88.18, 121.82, 155.46, 189.1]   # likewise
BW = 29.8

def build():
    p = k.Picture(1907, W)
    R = p.R
    H = k.H
    def ring(cx, cy, r0, r1, squash):
        n = max(30, int(r1 * 7))
        a = R.uniform(0, 6.28)
        i = 0
        while i < n:
            run = R.randint(3, 14)
            gap = R.randint(0, 2)
            pts = []
            for j in range(run + 1):
                t = a + 2 * math.pi * (i + j) / n
                r = r1 * (1 + R.uniform(-.012, .012))
                pts.append((cx + r * math.cos(t), cy + squash * r * math.sin(t)))
            for j in range(run, -1, -1):
                t = a + 2 * math.pi * (i + j) / n
                r = r0 * (1 + R.uniform(-.012, .012))
                pts.append((cx + r * math.cos(t), cy + squash * r * math.sin(t)))
            p.polys.append(pts)
            i += run + gap
    # bounces fall along a diagonal, growing towards the bottom of the panel
    for cx, cy, rmax, count in ((W * .20, H * .17, 10, 3), (W * .74, H * .34, 17, 4), (W * .26, H * .58, 25, 5), (W * .68, H * .93, 36, 6)):
        for j in range(count):
            r = rmax * (j + 1) / count
            th = rmax / count * R.uniform(.3, .5)
            ring(cx, cy, max(r - th, .3), r, .72)
    # the stone itself on the first bounce
    p.polys.append([(W * .20 + 2.2 * (1 + R.uniform(-.2, .2)) * math.cos(2 * math.pi * i / 11),
                     H * .17 + 1.5 * (1 + R.uniform(-.2, .2)) * math.sin(2 * math.pi * i / 11)) for i in range(11)])
    p.slices(1.2, 7, [0, 0, .8, -1.1, 1.9, -2.3, 3.1])

    def warp(X, Y):
        # the whole leans and waves, then the slices slide
        V = (Y - H * .5) / (H * .5)
        Y2 = H * .5 + V * H * .52 + math.sin(V * 2.6) * 3.0
        X2 = W * .5 + (X - W * .5) * (1.05 + .2 * V) + V * V * 3.5 + math.sin(V * 5.0) * 1.9
        return X2 + p.shift(Y2) * 1.5, Y2

    out = [k.header(PW, W), k.background(W), p.path(warp, .5), k.title('rICOcHEt', PW, 26)]
    for lab, c, r in (('TYPE', 0, 0), ('TIME', 1, 0), ('FEEDBACK', 2, 0), ('MIX', 3, 0), ('TONE', 0, 1), ('CHARACTER', 1, 1), ('SPREAD', 2, 1)):
        out.append(k.knob_label(lab, KX[c], KY[r]))
    out.append(k.text('PING', 165, KY[1] - k.KNOB_LABEL_ABOVE, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.text('STEREO', 165, KY[1] + 21, 6.5, k.INK))
    out.append(k.text('FREEZE', 191, KY[1] - k.KNOB_LABEL_ABOVE, 6.5, k.INK))
    for x, l in zip(JX, ['TIME', 'FDBK', 'TONE', 'MIX', 'CHAR', 'TYPE']):
        out.append(k.jack_box(x, k.SECOND_ROW, BW, k.INPUT))
        out.append(k.jack_label(l, x, k.SECOND_ROW))
    for x, l in zip(JX[:2], ['CLOCK', 'FREEZE']):
        out.append(k.jack_box(x, k.BOTTOM_ROW, BW, k.INPUT))
        out.append(k.jack_label(l, x, k.BOTTOM_ROW, 6.5))
    # input, one black box
    out.append(k.jack_box(JX[2], k.BOTTOM_ROW, BW, k.INPUT, JX[3]))
    for x, l in zip(JX[2:4], ['IN L', 'IN R']):
        out.append(k.jack_label(l, x, k.BOTTOM_ROW, 6.5))
    # output, one red box
    out.append(k.jack_box(JX[4], k.BOTTOM_ROW, BW, k.OUTPUT, JX[5]))
    for x, l in zip(JX[4:], ['OUT L', 'OUT R']):
        out.append(k.jack_label(l, x, k.BOTTOM_ROW, 6.5))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('RICOCHET.svg', build())
