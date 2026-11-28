# liMonADe's panel (30 HP), migrated from the hand-drawn one: the same arrangement (the spectrum screen across the top,
# the wave screen at the bottom left, the frame size display, the edited frame knob and the keypad of edit buttons
# beside it, all drawn by the widget; right, five columns UNI, FREQ, FINE, FM and SCAN, each a knob over its CV jack
# joined by a line, with the unison range and scan amount trimpots, the SYNC box under FINE, the sync mode switch
# between H and S and its tiny CV jack, IN and OUT), now in the theme's screens, boxes, type and colours, with the old
# picture kept (limonade_parts.svg). The positions are the same in src/LIMONADE.cpp (LIMONADEWidget).
import os, re
import panel_kit as k

PW = 450
W = PW * k.K
LINE = 0.26458                         # the old lines' width, in units
# the screens where the widget draws: the spectrum (its display 15, 33, 420 x 162) and the wave (11, 235), the old
# rectangles (10.41, 27.35 to 439.59, 200.15 and 10.29, 209.7 to 161.26, 360.98) with their left edges on the theme's
# margin and the spectrum's right edge on the other one
SPECTRUM = (k.MARGIN, 27.0, PW - 2 * k.MARGIN, 173.0)
WAVE = (k.MARGIN, 209.7, 151.3, 151.3)
# the five columns: the old lines and boxes (src/LIMONADE.cpp col)
COL = (289.156, 324.562, 359.094, 394.062, 429.375)
KNOB_Y = 219 + 29.5275591 / 2          # the knobs, old corner 219 (src/LIMONADE.cpp knobRow)
RANGE_Y = 262 + 19.1929131 / 2         # the unison range trimpot under UNI, old corner 262
SCAN_AMT_Y = 257 + 19.1929131 / 2      # the scan amount trimpot under SCAN, old corner 257
CV_Y = k.SECOND_ROW                    # the CV jacks and SYNC on the second row (1.15 px lower than before)
IO_Y = k.BOTTOM_ROW                    # IN and OUT on the bottom row (0.15 px lower)
BOX_W = 26.0                           # one box per jack (the old ones were 28.3 px); OUT's is 7.6 px from the side
SWITCH = (COL[2], 327 + 20.64111 / 2)  # the sync mode CKSS under SYNC, old corner (352, 327): 0.09 px right, on FINE's
CKSS_HALF = 20.64111 / 2               # column. Its tiny CV jack on its row in FREQ's column (0.06 px right, 0.18 higher)

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'limonade_parts.svg'), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def line(x0, y0, x1, y1):
    # the old lines from a knob or trimpot down to its CV jack, drawn under them so only the gap shows
    return (f'<path style="fill:none;stroke:{k.INK};stroke-width:{LINE}" '
            f'd="M{x0 * k.K:.4f},{y0 * k.K:.4f}L{x1 * k.K:.4f},{y1 * k.K:.4f}"/>')

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('liMonADe', PW)]   # 70 px wide at 16: fits
    out.append(k.screen(*SPECTRUM))
    out.append(k.screen(*WAVE))
    # the lines: UNI from its range trimpot (RNG sits between it and the knob), FREQ, FM and SCAN from the knob
    out.append(line(COL[0], RANGE_Y, COL[0], CV_Y))
    for i in (1, 3, 4):
        out.append(line(COL[i], KNOB_Y, COL[i], CV_Y))
    out.append(line(COL[1], SWITCH[1], SWITCH[0], SWITCH[1]))   # the sync mode switch to its tiny CV jack
    for lab, x in zip(('UNI', 'FREQ', 'FINE', 'FM', 'SCAN'), COL):
        out.append(k.knob_label(lab, x, KNOB_Y))
    # RNG: centred between UNI's knob and its trimpot (a small label 18 px above would touch the knob)
    room_top = KNOB_Y + 29.5275591 / 2
    room = RANGE_Y - 19.1929131 / 2 - room_top
    out.append(k.text('RNG', COL[0], room_top + (room + k.CAP_HEIGHT * k.BOX_LABEL_SIZE) / 2, k.BOX_LABEL_SIZE, k.INK))
    # the sync mode switch: its two positions above and below it, as lIMbO's
    out.append(k.text('H', SWITCH[0], SWITCH[1] - CKSS_HALF - 4, k.BOX_LABEL_SIZE, k.INK))
    out.append(k.text('S', SWITCH[0], SWITCH[1] + CKSS_HALF + 4 + k.CAP_HEIGHT * k.BOX_LABEL_SIZE, k.BOX_LABEL_SIZE, k.INK))
    # the boxes: SYNC under FINE, IN under UNI, OUT under SCAN
    for lab, x, y, colour in (('SYNC', COL[2], CV_Y, k.INPUT), ('IN', COL[0], IO_Y, k.INPUT), ('OUT', COL[4], IO_Y, k.OUTPUT)):
        out.append(k.box(x - BOX_W / 2, y - k.BOX_ABOVE, BOX_W, k.BOX_ABOVE + k.BOX_BELOW, colour))
        out.append(k.jack_label(lab, x, y))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('LIMONADE.svg', build())
