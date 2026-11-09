# cOPIA's panel (34 HP). The picture: a photocopier and its copies, lightly mangled so that it stays readable.
import math
import panel_kit as k

PW = 510
K = k.K
W = PW * K
SCREEN_H = 150                      # the same in src/CopiaWidget.hpp
LABELS1 = 194                       # labels above the small knob rows, which are at 213 and 253
LABELS2 = 235
IN_X0, OUT_X0, COL = 27, 370.2, 37.6   # the jack columns, each row centred in its box; the same in src/CopiaWidget.hpp

def build():
    p = k.Picture(1938, W)
    x0, x1 = 16.0, 101.0   # the machine's sides
    # the lid at the very top, open a little, with its document feeder and a sheet going in
    p.rect(x0 - 2, 5.2, x1 - x0 + 4, 2.2)
    p.rect(x0 + 4, 2.0, x1 - x0 - 8, 2.0)
    p.add([(x0 + 12, 2.2), (x0 + 30, 0.2), (x0 + 33, 0.2), (x0 + 16, 2.2)])
    p.add([(x1 - 40, 2.2), (x1 - 18, 0.2), (x1 - 14, 0.2), (x1 - 34, 2.2)])
    # the sheets waiting in the feeder, either side of the title
    for j in range(3):
        p.rect(x0 + 1 + j * 1.4, 0.6 + j * .3, 10, .7)
    # the frame of the machine, from the lid to the floor
    p.rect(x0, 9, 3, 82)
    p.rect(x1 - 3, 9, 3, 82)
    p.rect(x0, 9, x1 - x0, 2.5)
    # the control panel in the middle band: a display and rows of keys
    ky = 46.5
    p.rect(x0 + 6, ky, x1 - x0 - 12, 1.0)
    p.rect(x0 + 6, ky + 19, x1 - x0 - 12, 1.0)
    p.rect(x0 + 7, ky + 3, 18, 12)
    for r_ in range(3):
        for c_ in range(10):
            cx_, cy_ = x0 + 30 + c_ * 5, ky + 4 + r_ * 5
            p.add([(cx_ + 1.5 * math.cos(t * math.pi / 4), cy_ + 1.5 * math.sin(t * math.pi / 4)) for t in range(8)])
    # the big button
    p.add([(x1 - 11 + 3.4 * math.cos(t * math.pi / 6), ky + 9 + 3.4 * math.sin(t * math.pi / 6)) for t in range(12)])
    # paper drawers, seen below the jacks
    for yy in (70, 80):
        p.rect(x0 + 5, yy, x1 - x0 - 10, 8.5)
    p.rect(x0 + 5, 91, x1 - x0 - 10, 3)
    # the feet on their wheels at the very bottom
    for xx in (x0 + 6, x1 - 12):
        p.rect(xx, 92, 6, 2.5)
        p.add([(xx + 3 + 2.6 * math.cos(t * math.pi / 4), 97 + 2.6 * math.sin(t * math.pi / 4)) for t in range(8)])
    # the output: copies falling on a tray on the right, each with lines of text
    tx = x1
    p.rect(tx, 32, 19, 1.6)
    p.add([(tx + 19, 32), (tx + 30, 27), (tx + 30, 28.6), (tx + 19, 33.6)])
    for px_, py_, a in ((tx + 11, 18, -.25), (tx + 21, 44, .2), (tx + 13, 62, -.1)):
        def sheet(u, v):
            return (px_ + u * math.cos(a) - v * math.sin(a), py_ + u * math.sin(a) + v * math.cos(a))
        w, h = 8.5, 11.5
        p.add([sheet(-w, -h), sheet(w, -h), sheet(w, -h + .8), sheet(-w, -h + .8)])
        p.add([sheet(-w, h - .8), sheet(w, h - .8), sheet(w, h), sheet(-w, h)])
        p.add([sheet(-w, -h), sheet(-w + .8, -h), sheet(-w + .8, h), sheet(-w, h)])
        p.add([sheet(w - .8, -h), sheet(w, -h), sheet(w, h), sheet(w - .8, h)])
        for l_ in range(5):
            yy = -h + 3.4 + l_ * 3.6
            p.add([sheet(-w + 2.4, yy), sheet(w - 2.4, yy), sheet(w - 2.4, yy + 1.1), sheet(-w + 2.4, yy + 1.1)])
    # the same copies on the left too: it copies
    for px_, py_, a in ((7.5, 40, .08), (7.5, 64, -.08)):
        def sheet(u, v):
            return (px_ + u * math.cos(a) - v * math.sin(a), py_ + u * math.sin(a) + v * math.cos(a))
        w, h = 5.5, 10
        p.add([sheet(-w, -h), sheet(w, -h), sheet(w, h), sheet(-w, h)])

    def warp(X, Y):
        # a light mangling only: the shape has to stay readable
        return X + math.sin(Y * .13) * .9, Y

    out = [k.header(PW, W), k.background(W), p.path(warp, 1.2), k.title('cOPIA', PW)]
    out.append(k.screen(k.MARGIN, k.SCREEN_TOP, PW - 2 * k.MARGIN, SCREEN_H))
    for x, l in ((30, 'CAPTURE'), (72, 'LENGTH'), (110, 'GRID'), (146, 'RAND'), (178, 'MUTATE'), (293, 'SCENES'), (398, 'MORPH'), (436, 'MIX'), (478, 'IN/OUT')):
        out.append(k.text(l, x, LABELS1, k.BOX_LABEL_SIZE, k.INK))
    for s_ in range(8):
        cx = 10 + 61.25 * (s_ + .5)
        out.append(k.text(f'{s_+1} FX', cx - 14, LABELS2, k.BOX_LABEL_SIZE, k.INK))
        out.append(k.text('PUNCH', cx + 14, LABELS2, k.BOX_LABEL_SIZE, k.INK))
    # one tall black box and one tall red box, their outer edges on the screen's
    tall = k.BOTTOM_BOTTOM - k.SECOND_TOP
    out.append(k.box(k.MARGIN, k.SECOND_TOP, 334.8, tall, k.INPUT))
    for i, l in enumerate(['IN L', 'IN R', 'CLOCK', 'RESET', 'CAPT', 'IN/OUT', 'SCENE', 'MORPH', 'MIX']):
        out.append(k.jack_label(l, IN_X0 + COL * i, k.SECOND_ROW, 7))
    for i in range(8):
        out.append(k.jack_label(f'PUNCH {i+1}', IN_X0 + COL * i, k.BOTTOM_ROW, 6.5, jack_above=k.SECOND_ROW))
    out.append(k.box(353.2, k.SECOND_TOP, 146.8, tall, k.OUTPUT))
    for i, l in enumerate(['OUT L', 'OUT R', 'BAR', 'REC']):
        out.append(k.jack_label(l, OUT_X0 + COL * i, k.SECOND_ROW, 7))
    for i, l in enumerate(['WET L', 'WET R', 'LANES']):
        out.append(k.jack_label(l, OUT_X0 + COL * i, k.BOTTOM_ROW, 7, jack_above=k.SECOND_ROW))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('COPIA.svg', build())
