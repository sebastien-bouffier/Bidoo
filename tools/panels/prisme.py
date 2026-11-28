# PRiSME's panel (20 HP). The picture: the textbook figure of a ray refracted by a prism, redrawn as shapes (the
# user chose the figure), stretched over the panel, tilted and mangled. Everything sits on the grid, six columns; every control of a column
# shares its jack's x (jack_col), as src/PRISME.cpp.
import math
import panel_kit as k

PW = 300
K = k.K
W = PW * K
N = 6                               # columns, kCols in src/PRISME.cpp
KNOB_TOP, KNOB_LOW = 3, 2           # the knob rows, kKnobTopRow and kKnobLowRow in src/PRISME.cpp
STRIP_H, STRIP_GAP = 14, 3          # the tool strip under the screen, kStripH and kStripGap in src/PRISME.cpp
# The knobs' names 18 px over their centre, 3.2 px over the knob's top (the user found the theme's 22 too far, as on
# EMILE's buttons); the screen ends the theme's label room above them. kKnobNameAbove in src/PRISME.cpp
KNOB_NAME_ABOVE = 18

def knob_name(s, x, y):
    return k.text(s, x, y - KNOB_NAME_ABOVE, k.KNOB_LABEL_SIZE, k.INK)
# ODD/EVEN, the widest name, between the two narrowest (SWEEP, BRIGHT); BRIGHT and STRETCH over their CV jacks
TOP_KNOBS = ('MORPH', 'SWEEP', 'ODD/EVEN', 'BRIGHT', 'STRETCH', 'FORMANT')
LOW_KNOBS = ('ATTACK', 'DECAY', 'SUSTAIN', 'RELEASE', 'DAMP', 'DEPTH')
INPUTS = ('V/O', 'GATE', 'VEL', 'BRIGHT', 'STRETCH', 'MORPH')
OUTPUTS = ((3, 'L'), (4, 'R'), (5, 'POLY'))

def build():
    p = k.Picture(2711, W)
    H = k.H
    # The drawing: the textbook figure of light through a prism (the apex angle up, a ray in at J bent towards the base,
    # out at K, the normals dashed, the angles of incidence and refraction as arcs, the deviation as a small triangle
    # where the ray's two lines cross), redrawn here as shapes. It is laid out in a 245 x 141 frame, then tilted and
    # stretched so the prism covers the panel from the title to the logo, and its rays run off both sides. Thin
    # strokes, so it stays as faint as the other panels' pictures at their opacity.
    A, BL, BR = (140, 8), (58, 136), (226, 136)
    def lerp(a, b, t):
        return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
    def unit(a, b):
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        return ((b[0] - a[0]) / L, (b[1] - a[1]) / L)
    def along(a, d, t):
        return (a[0] + d[0] * t, a[1] + d[1] * t)
    J, Kp = lerp(A, BL, 0.634), lerp(A, BR, 0.45)
    SRC, OUT = (-40, 151), (300, 111)    # far out: cut at the panel's edges
    TILT = math.radians(-4)
    def M(q):
        # the frame to panel px: the apex under the title, stretched (1.6 across, 2.5 down: the base near the bottom
        # edge, its corners 15 px in from the sides once tilted)
        x, y = (q[0] - 140) * 1.6, (q[1] - 8) * 2.5
        c, s_ = math.cos(TILT), math.sin(TILT)
        return (124 + x * c - y * s_, 26 + x * s_ + y * c)
    def clip(a, b):
        # the part of a..b inside the panel (Liang-Barsky), so the rays end at the edges instead of being squashed
        t0, t1 = 0.0, 1.0
        dx, dy = b[0] - a[0], b[1] - a[1]
        for pp, q in ((-dx, a[0]), (dx, PW - a[0]), (-dy, a[1]), (dy, 380 - a[1])):
            if pp == 0:
                if q < 0:
                    return None
                continue
            r = q / pp
            if pp < 0:
                t0 = max(t0, r)
            else:
                t1 = min(t1, r)
        if t0 >= t1:
            return None
        return (a[0] + dx * t0, a[1] + dy * t0), (a[0] + dx * t1, a[1] + dy * t1)
    def band_px(a, b, t):
        # a straight band t px either side of a..b (both already in px), cut at the panel's edges
        seg = clip(a, b)
        if seg is None:
            return
        a, b = seg
        dx, dy = b[0] - a[0], b[1] - a[1]
        L = math.hypot(dx, dy) or 1
        nx, ny = -dy / L * t, dx / L * t
        p.add([((a[0] - nx) * K, (a[1] - ny) * K), ((b[0] - nx) * K, (b[1] - ny) * K),
               ((b[0] + nx) * K, (b[1] + ny) * K), ((a[0] + nx) * K, (a[1] + ny) * K)])
    def line(a, b, t):
        band_px(M(a), M(b), t)
    def dashed(a, b, t, dash=5.0, gap=3.5):
        d = unit(a, b)
        L = math.hypot(b[0] - a[0], b[1] - a[1])
        u = 0.0
        while u < L:
            line(along(a, d, u), along(a, d, min(u + dash, L)), t)
            u += dash + gap
    def arc(c, r, a0, a1, t, n=14):
        pts = [M((c[0] + r * math.cos(a0 + (a1 - a0) * i / n), c[1] + r * math.sin(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]
        for i in range(n):
            band_px(pts[i], pts[i + 1], t)
    def ang(d):
        return math.atan2(d[1], d[0])
    def solid(pts):
        p.add([(q[0] * K, q[1] * K) for q in map(M, pts)])

    # the prism's sides
    for a, b in ((A, BL), (BL, BR), (BR, A)):
        line(a, b, 2.0)
    # the apex angle, filled
    a0, a1 = ang(unit(A, BR)), ang(unit(A, BL))
    solid([A] + [(A[0] + 16 * math.cos(a0 + (a1 - a0) * i / 12), A[1] + 16 * math.sin(a0 + (a1 - a0) * i / 12)) for i in range(13)])
    # the ray: in, through, out, and its arrow
    line(SRC, J, 1.6)
    line(J, Kp, 1.6)
    line(Kp, OUT, 1.6)
    din = unit(SRC, J)
    tip = along(J, din, -38)
    nrm = (-din[1], din[0])
    solid([along(tip, din, 6), along(tip, nrm, 3.5), along(tip, nrm, -3.5)])
    # the normals at J and K, dashed
    n1 = (128 / 152.0, 82 / 152.0)                     # inward normal of the left side
    n2 = (128 / 154.3, -86 / 154.3)                    # outward normal of the right side
    dashed(along(J, n1, -48), along(J, n1, 88), 0.9)
    dashed(along(Kp, n2, -80), along(Kp, n2, 48), 0.9)
    # the ray's lines carried on, dashed: the way in past J, the way out back from K; they cross at P
    dout = unit(Kp, OUT)
    dashed(J, along(J, din, 118), 0.9)
    dashed(Kp, along(Kp, dout, -96), 0.9)
    # P: where they cross (J + u din = K - v dout)
    det = din[0] * (-dout[1]) - din[1] * (-dout[0])
    u = ((Kp[0] - J[0]) * (-dout[1]) - (Kp[1] - J[1]) * (-dout[0])) / det
    P = along(J, din, u)
    # the deviation: a small filled triangle at P, between the two lines
    solid([P, along(P, din, 18), along(P, dout, 18)])
    # the angles: i1 and r1 at J, r2 and i2 at K
    arc(J, 15, ang((-din[0], -din[1])), ang((-n1[0], -n1[1])), 0.8)
    arc(J, 11, ang(n1), ang(unit(J, Kp)), 0.8)
    arc(Kp, 12, ang(unit(Kp, J)), ang((-n2[0], -n2[1])), 0.8)
    arc(Kp, 15, ang(n2), ang(dout), 0.8)
    p.slices(1.2, 7, [0, 0, .4, -.6, .9, -1.2, 1.6])

    def warp(X, Y):
        # a slow wave across the height, then the slices slide
        V = (Y - H * .5) / (H * .5)
        X2 = X + math.sin(V * 2.4) * 1.2
        Y2 = Y + math.sin(X / W * 4.3) * 0.9
        return X2 + p.shift(Y2), Y2

    out_svg = [k.header(PW, W), k.background(W), p.path(warp, .5), k.title('PRiSME', PW)]
    sx = k.MARGIN
    strip_top = k.row(KNOB_TOP) - KNOB_NAME_ABOVE - k.T['kLabelRoom'] - STRIP_H
    # the screen and the strip, drawn again by the widget (PrismeScreen, PrismeStrip in src/PRISME.cpp)
    out_svg.append(k.screen(sx, k.SCREEN_TOP, PW - 2 * sx, strip_top - STRIP_GAP - k.SCREEN_TOP))
    out_svg.append(k.screen(sx, strip_top, PW - 2 * sx, STRIP_H))
    for i in range(N):
        x = k.jack_col(i, N, PW)
        out_svg.append(knob_name(TOP_KNOBS[i], x, k.row(KNOB_TOP)))
        out_svg.append(knob_name(LOW_KNOBS[i], x, k.row(KNOB_LOW)))
        out_svg.append(k.grid_box(i, i, N, 1, PW, k.INPUT))
        out_svg.append(k.jack_label(INPUTS[i], x, k.row(1)))
    # the bottom row: the formant's CV, TUNE and SPREAD (small knobs, no box), the three outputs
    out_svg.append(k.grid_box(0, 0, N, 0, PW, k.INPUT))
    out_svg.append(k.jack_label('FORM', k.jack_col(0, N, PW), k.row(0)))
    # the small knobs' names on the captions' baseline (the jacks' beside them), 3.4 px over the knobs' top: the theme's
    # 18 px would leave two baselines 2.6 px apart in one row
    for i, s in ((1, 'TUNE'), (2, 'SPREAD')):
        out_svg.append(k.text(s, k.jack_col(i, N, PW), k.row(0) - k.LABEL_ABOVE, k.BOX_LABEL_SIZE, k.INK))
    for i, s in OUTPUTS:
        out_svg.append(k.grid_box(i, i, N, 0, PW, k.OUTPUT))
        out_svg.append(k.jack_label(s, k.jack_col(i, N, PW), k.row(0)))
    out_svg.append(k.logo(W))
    out_svg.append('</svg>')
    return out_svg

if __name__ == '__main__':
    k.write('PRISME.svg', build())
