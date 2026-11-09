# dOSE's panel (6 HP). The picture: a syringe, plunger up and needle down, stretched and mangled. An original drawing.
# Everything sits on the grid, two columns; every control of a column shares its jack's x (jack_col), as src/DOSE.cpp.
import math
import panel_kit as k

PW = 90
K = k.K
W = PW * K
N = 2                               # columns, the same in src/DOSE.cpp
KNOB_ROW = 4                        # likewise: CURVE and LEVEL
TRIM_ROW = 3                        # likewise: the CURVE CV amount, and the CURVE CV jack beside it

def build():
    p = k.Picture(1005, W)
    H = k.H
    cx = W * .5
    # drawn in px along the syringe's axis, a diagonal across the whole panel: plunger top left, needle bottom right,
    # so that its parts show between the screen, the knobs and the boxes. t runs along the axis, s across it
    x0, y0, x1, y1 = 8, -8, 100, 394
    L = math.hypot(x1 - x0, y1 - y0)
    dx, dy = (x1 - x0) / L, (y1 - y0) / L
    def pt(t, s):
        return ((x0 + dx * t - dy * s) * K, (y0 + dy * t + dx * s) * K)
    def quad(t0, t1, s0, s1, s0b=None, s1b=None):
        # from t0 to t1, across s0..s1 (s0b..s1b at t1, for a taper)
        s0b, s1b = (s0, s1) if s0b is None else (s0b, s1b)
        p.add([pt(t0, s0), pt(t0, s1), pt(t1, s1b), pt(t1, s0b)])
    quad(12, 26, -30, 30)               # the plunger's thumb flange, in the title's band
    quad(26, 134, -6, 6)                # its rod
    quad(96, 110, -64, 64)              # the finger grips, reaching both side margins
    quad(96, 306, -22, -15)             # the barrel's walls
    quad(96, 306, 15, 22)
    quad(134, 146, -15, 15)             # the rubber stopper, in the gap above the knob labels
    for j in range(14):                 # the graduations, every fifth one longer
        t = 150 + 11 * j
        quad(t, t + 2.6, 15 - (13 if j % 5 == 0 else 7), 15)
    quad(306, 312, -22, 22)             # the barrel's end, the shoulder, the hub
    quad(312, 324, -22, 22, -6, 6)
    quad(324, 340, -6, 6)
    quad(340, 420, -2.4, 2.4, -.5, .5)  # the needle
    p.slices(1.2, 7, [0, 0, .3, -.4, .7, -.9, 1.2])

    def warp(X, Y):
        # a slight wave (the picture already fills the panel), then the slices slide
        V = (Y - H * .5) / (H * .5)
        X2 = cx + (X - cx) * (1.0 + .04 * V) + math.sin(V * 3.4) * .5
        Y2 = Y + math.sin(V * 2.2) * .8
        return X2 + p.shift(Y2), Y2

    out = [k.header(PW, W), k.background(W), p.path(warp, .5), k.title('dOSE', PW)]
    sx = k.MARGIN
    out.append(k.screen(sx, k.SCREEN_TOP, PW - 2 * sx, k.screen_above_knobs(KNOB_ROW) - k.SCREEN_TOP))   # DoseScreen in src/DOSE.cpp
    x0, x1 = k.jack_col(0, N, PW), k.jack_col(1, N, PW)
    yk, yt = k.row(KNOB_ROW), k.row(TRIM_ROW)
    # CURVE's line: down from the knob through its CV amount, across to the CV jack's box
    out.append(f'<path style="fill:none;stroke:{k.INK};stroke-width:0.32" '
               f'd="M{x0*K:.3f},{yk*K:.3f}V{yt*K:.3f}H{k.box_left(1, N, PW)*K:.3f}"/>')
    out.append(k.knob_label('CURVE', x0, yk))
    out.append(k.knob_label('LEVEL', x1, yk))
    out.append(k.grid_box(1, 1, N, TRIM_ROW, PW, k.INPUT))
    out.append(k.jack_label('CV', x1, yt))
    for i, l in ((0, 'IN'), (1, 'ENV')):
        out.append(k.grid_box(i, i, N, 2, PW, k.INPUT))
        out.append(k.jack_label(l, k.jack_col(i, N, PW), k.row(2)))
    # the outputs, one red box over the two lower rows
    out.append(k.grid_box(0, 1, N, 1, PW, k.OUTPUT, 0))
    out.append(k.jack_label('OUT', x0, k.row(1)))
    out.append(k.jack_label('MIX', x1, k.row(1)))
    out.append(k.jack_label('ENV OUT', k.jack_col(.5, N, PW), k.row(0), jack_above=k.row(1)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('DOSE.svg', build())
