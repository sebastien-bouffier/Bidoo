# bAFIs's panel (10 HP), migrated from the hand-drawn one: the same arrangement (three frequency knobs over the filter
# shapes, then four columns of Q, GAIN, TYPE, PRE/POST and VOL named down the left side, each control with its tiny CV
# jack under it joined by a short line, IN and OUT at the bottom), now in the theme's boxes, type and colours, with the
# old picture and filter shapes kept (bafis_parts.svg). The positions are the same in src/BAFIS.cpp.
import os, re
import panel_kit as k

PW = 150
W = PW * k.K
COL_X = 22.5                    # the first column: its CV jacks' old centre, the knobs snapped onto it (0.31 px), in src/BAFIS.cpp
COL_RIGHT = 35.0                # across, between columns and between frequency knobs (src/BAFIS.cpp kColRight)
FREQ_Y = 26 + 29.5275591 / 2    # the frequency knobs (BidooBlueKnob) at the old corner's height 26 (src/BAFIS.cpp freq)
FREQ_CV_Y = 61 + 7.5            # their tiny CV jacks, old corner 61 (src/BAFIS.cpp freqCv)
ROW_Y = 82 + 23.6220472 / 2     # the Q row of small knobs, old corner 82 (src/BAFIS.cpp qTop)
ROW = 46.6                      # the columns' rows (src/BAFIS.cpp kRow)
JACK_GAP = 5.0                  # a control's bottom to its jack's top (src/BAFIS.cpp kJackGap)
SMALL_HALF, FREQ_HALF, CKSS_HALF, TINY_HALF = 23.6220472 / 2, 29.5275591 / 2, 20.64111 / 2, 7.5
ROWS = ('Q', 'GAIN', 'TYPE', 'PRE/POST', 'VOL')
SIDE_LABEL_X = 5.5              # the rows' names, turned a quarter: the centre of their capitals, where the old ones were

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bafis_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def line(x, y0, y1):
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:0.26" d="M{x*k.K:.4f},{y0*k.K:.4f}L{x*k.K:.4f},{y1*k.K:.4f}"/>'

def side_label(s, y):
    # read upwards, centred on its row; not a mark for --align, it is turned (the old panel's own way to name the rows)
    x = SIDE_LABEL_X
    return (f'<g transform="rotate(-90 {x*k.K:.4f} {y*k.K:.4f})">'
            + k._text(s, x, y + k.CAP_HEIGHT * k.BOX_LABEL_SIZE / 2, k.BOX_LABEL_SIZE, k.INK) + '</g>')

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('bAFIs', PW)]
    # the frequency knobs sit between the columns, centred on the panel; a line from each to its CV jack
    for i in range(3):
        x = COL_X + COL_RIGHT * (i + 0.5)
        out.append(line(x, FREQ_Y + FREQ_HALF, FREQ_CV_Y - TINY_HALF))
    # the columns: each control, its line, its jack; PRE/POST is a switch centred on its row
    for r, name in enumerate(ROWS):
        y = ROW_Y + ROW * r
        out.append(side_label(name, y))
        half = CKSS_HALF if name == 'PRE/POST' else SMALL_HALF
        for i in range(4):
            out.append(line(COL_X + COL_RIGHT * i, y + half, y + SMALL_HALF + JACK_GAP))
    # IN and OUT on the bottom row's outer columns of four
    for i, name, colour in ((0, 'IN', k.INPUT), (3, 'OUT', k.OUTPUT)):
        out.append(k.grid_box(i, i, 4, 0, PW, colour))
        out.append(k.jack_label(name, k.jack_col(i, 4, PW), k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('BAFIS.svg', build())
