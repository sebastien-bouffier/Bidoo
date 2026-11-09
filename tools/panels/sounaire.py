# sOUNAiRe's panel (32 HP). The picture: an open accordion seen from the front, lightly mangled so that it stays readable.
import math
import panel_kit as k

PW = 480
K = k.K
W = PW * K
SCREEN_H = 106                      # display and keys together; the same in src/SounaireWidget.hpp
KNOB_ROWS = [169, 215, 261]         # likewise
UPPER_ROW = 306                     # likewise: three rows of knobs leave no room for the usual second row at 296
JX0, COL = 28, 32                   # the jack columns; likewise
KNOB_LABEL_ABOVE = 17               # small knobs

def build():
    p = k.Picture(1829, W)
    H = k.H
    px0, py0 = W / 2, H + 62        # a fan around a point below the panel
    r0, r1 = 66.0, H + 60.0         # bottom and top of the fan
    A0 = math.radians(27.5)         # the outer edge of the hand boxes
    A1 = math.radians(18.0)         # where the bellows start
    def pol(a, r):
        return (px0 + r * math.sin(a), py0 - r * math.cos(a))
    def sector(a0, a1, r_0, r_1, n=10):
        p.add([pol(a0 + (a1 - a0) * j / n, r_1) for j in range(n + 1)] + [pol(a1 + (a0 - a1) * j / n, r_0) for j in range(n + 1)])
    # the bellows: wide pleats, every other one shaded
    nb = 11
    for i in range(nb):
        sector(-A1 + 2 * A1 * i / nb, -A1 + 2 * A1 * (i + .5) / nb, r0 + 3, r1 - 3)
    # metal corners on the bellows
    for sgn in (-1, 1):
        for j in range(5):
            a = sgn * A1 * (j * 2 + 1) / 5
            sector(a - .012, a + .012, r0 - 1, r0 + 7, 2)
            sector(a - .012, a + .012, r1 - 7, r1 + 1, 2)
    # the two hand boxes: frames
    for sgn in (-1, 1):
        a_in, a_out = sgn * (A1 + .012), sgn * A0
        sector(a_in, a_out, r1 - 3, r1)
        sector(a_in, a_out, r0, r0 + 3)
        sector(a_in, a_in + sgn * .012, r0, r1)
        sector(a_out - sgn * .012, a_out, r0, r1)
    # left: the round bass buttons, in rows
    mid_l = -(A1 + A0) / 2
    for i in range(13):
        r = r0 + 9 + i * (r1 - r0 - 18) / 12
        for j in (-1, 0, 1):
            cx_, cy_ = pol(mid_l + j * .038, r)
            p.add([(cx_ + 1.25 * math.cos(t * math.pi / 4), cy_ + 1.25 * math.sin(t * math.pi / 4)) for t in range(8)])
    # right: the piano keyboard, white keys as lines and black keys in groups of two and three
    mid_r = (A1 + A0) / 2
    da = (A0 - A1) * .40
    pat = [1, 1, 0, 1, 1, 1, 0]
    r = r0 + 7
    n = 0
    step = (r1 - r0 - 14) / 24
    while r < r1 - 7:
        sector(mid_r - da, mid_r + da, r, r + .35, 2)
        if pat[n % 7]:
            sector(mid_r - da, mid_r + da * .15, r - step * .3, r + step * .3, 2)
        r += step
        n += 1

    def warp(X, Y):
        # a light mangling only: the shape has to stay readable
        return X + math.sin(Y * .16) * 1.0, Y

    out = [k.header(PW, W), k.background(W), p.path(warp, 1.2), k.title('sOUNAiRe', PW)]
    out.append(k.screen(k.MARGIN, k.SCREEN_TOP, PW - 2 * k.MARGIN, SCREEN_H))
    labels = [['START', 'END', 'LOOP IN', 'LOOP OUT', 'X-FADE', 'LOOP', 'REVERSE', 'LEGATO'],
              ['ROOT', 'LOW', 'HIGH', 'TUNE', 'FINE', 'SPEED', 'TIME', 'LEVEL'],
              ['FILTER', 'SLOPE', 'CUTOFF', 'RES', 'ATTACK', 'RELEASE', 'PLAY', 'SLICES']]
    for r_, ls in zip(KNOB_ROWS, labels):
        for c, l in enumerate(ls):
            out.append(k.text(l, 28 + 418 / 7 * c, r_ - KNOB_LABEL_ABOVE, k.BOX_LABEL_SIZE, k.INK))
    top = UPPER_ROW - k.BOX_ABOVE
    tall = k.BOTTOM_BOTTOM - top
    out.append(k.box(k.MARGIN, top, 324, tall, k.INPUT))
    for i, l in enumerate(['V/OCT', 'GATE', 'VEL', 'BEND', 'START', 'SPEED', 'CUTOFF', 'RES', 'PITCH', 'PLAY']):
        out.append(k.jack_label(l, JX0 + COL * i, UPPER_ROW, 7))
    for i, l in ((0, 'TRIG'), (1, 'SLICE'), (2, 'RESET'), (3, 'CLOCK'), (4, 'END'), (5, 'LP IN'), (6, 'LP OUT'), (7, 'REC IN')):
        out.append(k.jack_label(l, JX0 + COL * i, k.BOTTOM_ROW, 7, jack_above=UPPER_ROW))
    out.append(k.jack_label('REC', 351, k.BOTTOM_ROW, 7, k.INK, jack_above=UPPER_ROW))   # on the bottom row's baseline
    out.append(k.box(368, top, 48, tall, k.INPUT))
    out.append(k.jack_label('IN L', 392, UPPER_ROW, 7))
    out.append(k.jack_label('IN R', 392, k.BOTTOM_ROW, 7, jack_above=UPPER_ROW))
    out.append(k.box(422, top, 48, tall, k.OUTPUT))
    out.append(k.jack_label('OUT L', 446, UPPER_ROW, 7))
    out.append(k.jack_label('OUT R', 446, k.BOTTOM_ROW, 7, jack_above=UPPER_ROW))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('SOUNAIRE.svg', build())
