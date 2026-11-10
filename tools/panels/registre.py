# rEgIStRE's panel (52 HP). The picture: an open ledger, two pages of ruled lines and columns of figures with a ribbon,
# lightly mangled. An original drawing. Everything sits on the grid, as src/REGISTRE.cpp places it.
import math
import panel_kit as k

PW = 780
K = k.K
W = PW * K
N = 20                              # jack and button columns, the same in src/REGISTRE.cpp
BUTTONS = ((6, 1, 'RUN'), (7, 1, 'GO'), (8, 1, 'HOLD'), (9, 1, 'COMMIT'), (19, 0, 'REC'))   # likewise: column, row
SIZE = 7                            # captions: the columns are 37 px apart

def build():
    p = k.Picture(1910, W)
    H = k.H
    # the book, in px: two pages from x0 to x1, the spine in the middle. Larger than the panel, so that it runs off
    # every edge (the picture is clipped to the panel)
    x0, x1, y0, y1 = -70, PW + 70, -24, 404
    mid = (x0 + x1) / 2
    def P(x, y):
        return (x * K, y * K)
    def quad(a, b, c, d):
        p.add([P(*a), P(*b), P(*c), P(*d)])
    def rect(x, y, w, h):
        quad((x, y), (x + w, y), (x + w, y + h), (x, y + h))
    # page edges and the spine
    for xa, xb in ((x0, mid - 4), (mid + 4, x1)):
        rect(xa, y0, xb - xa, 2.2)
        rect(xa, y1 - 2.2, xb - xa, 2.2)
        rect(xa, y0, 2.2, y1 - y0)
        rect(xb - 2.2, y0, 2.2, y1 - y0)
    rect(mid - 1.5, y0 - 6, 3, y1 - y0 + 12)
    # the pages' stacked edges at the bottom
    for j in range(3):
        rect(x0 + 4 + 3 * j, y1 + 2 + 2.4 * j, mid - x0 - 12 - 6 * j, 0.9)
        rect(mid + 8 + 3 * j, y1 + 2 + 2.4 * j, x1 - mid - 12 - 6 * j, 0.9)
    # ruled lines, and the column rules of a ledger: index, entry, amount
    for xa, xb in ((x0 + 8, mid - 10), (mid + 10, x1 - 8)):
        y = y0 + 22
        while y < y1 - 8:
            rect(xa, y, xb - xa, 0.55)
            y += 11.5
        for f in (0.12, 0.62, 0.82):
            rect(xa + (xb - xa) * f, y0 + 10, 0.9, y1 - y0 - 18)
        rect(xa, y0 + 16, xb - xa, 1.4)          # the head rule
    # figures written in the amount columns: short strokes in groups, as digits seen from afar
    R = p.R
    for xa, xb in ((x0 + 8, mid - 10), (mid + 10, x1 - 8)):
        y = y0 + 22
        while y < y1 - 14:
            if R.random() < 0.8:
                ax = xa + (xb - xa) * 0.62 + 6
                for d in range(R.randint(2, 5)):
                    rect(ax + d * 7, y - 7.5, 4.2, 1.2)
                    rect(ax + d * 7 + 3.2, y - 7.5, 1.1, 6.2)
                ix = xa + 5
                for d in range(2):
                    rect(ix + d * 6, y - 7.5, 1.1, 6.2)
                ex = xa + (xb - xa) * 0.12 + 6
                rect(ex, y - 4.5, (xb - xa) * R.uniform(0.15, 0.4), 1.1)
            y += 11.5
    # a ribbon bookmark hanging from the spine
    quad((mid + 2, y0), (mid + 9, y0), (mid + 15, y1 + 6), (mid + 8, y1 + 6))
    p.slices(1.5, 9, [0, 0, .4, -.5, .8, -1.0, 1.3])

    def warp(X, Y):
        V = (Y - H * .5) / (H * .5)
        X2 = X + math.sin(V * 2.6) * .7
        Y2 = Y + math.sin(X / W * 6.0) * .5
        return X2 + p.shift(Y2), Y2

    out = [k.header(PW, W), k.background(W), p.path(warp, .6), k.title('rEgIStRE', PW)]
    out.append(k.screen(k.MARGIN, k.SCREEN_TOP, PW - 2 * k.MARGIN, k.screen_bottom(1) - k.SCREEN_TOP))   # RegistreScreen
    def boxed(i0, labels, k_row, colour):
        out.append(k.grid_box(i0, i0 + len(labels) - 1, N, k_row, PW, colour))
        for j, l in enumerate(labels):
            out.append(k.jack_label(l, k.jack_col(i0 + j, N, PW), k.row(k_row), SIZE))
    boxed(0, ['CLOCK', 'RESET', 'RUN', 'TRANSP', 'PART', 'GO'], 1, k.INPUT)
    boxed(10, ['X CV', 'Y CV', 'Z CV', 'X GATE', 'Y GATE', 'Z GATE'], 1, k.INPUT)
    boxed(17, ['POLY A', 'POLY B', 'POLY G'], 1, k.OUTPUT)
    for t in range(4):
        boxed(3 * t, [f'A {t + 1}', f'B {t + 1}', f'GATE {t + 1}'], 0, k.OUTPUT)
    boxed(12, ['REC A', 'REC B', 'REC G', 'REC D', 'INS', 'DEL', 'PUNCH'], 0, k.INPUT)
    for i, r, l in BUTTONS:
        # on the jack captions' baseline, so that the row keeps one (docs/THEME.md, "Alignment")
        out.append(k.text(l, k.jack_col(i, N, PW), k.row(r) - k.LABEL_ABOVE, SIZE, k.INK))   # under POLY G's column
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('REGISTRE.svg', build())
