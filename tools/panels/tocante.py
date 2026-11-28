# tOCAnTe's panel (7 HP), migrated from the hand-drawn one: the same arrangement (the screen; three columns under
# BPM / BEATS / BAR and FINE / RESET / RUN, each knob or button joined by a line to its jack, RESET and RUN forking to
# an input and an output; the nine outputs in their boxes), now in the theme's boxes, type and colours, with the old
# picture kept (tocante_parts.svg). No screws, as before. The positions are the same in src/TOCANTE.cpp.
import os, re
import panel_kit as k

PW = 105
K = k.K
W = PW * K
CENTRE = PW / 2
COLUMN = 35.0              # from one upper column to the next, about the panel's centre (src/TOCANTE.cpp kColumnRight)
COLS = tuple(CENTRE + COLUMN * (i - 1) for i in range(3))
KNOB_Y = 90 + 29.5275591 / 2    # the BPM, BEATS and BAR knobs: the old corner (90) and theme::kBlueKnobSize (src/TOCANTE.cpp)
CV_Y = 125 + 7.5                # their tiny CV jacks: the old corner (125) and theme::kTinyJackSize (src/TOCANTE.cpp)
LOW_Y = 155 + 29.5275591 / 2    # FINE, and the RESET and RUN buttons on its row (src/TOCANTE.cpp)
LOW_CV_Y = 190 + 7.5            # the lower tiny jacks (src/TOCANTE.cpp)
FORK = 8.0                      # RESET's and RUN's input and output either side of the button's column (src/TOCANTE.cpp kForkOut)
FORK_Y = 186.0                  # the old forks' height
OUT_SQUARE = 16.0               # the red square under the tiny RESET and RUN outputs, as before
# The labels sit 19 px above their knobs, not kKnobLabelAbove's 22: the screen, and the CV jacks, leave 15 px above the
# knobs, and the capitals are centred in it
LABEL_ABOVE = 19.0
SCREEN = (5.0, k.SCREEN_TOP, 95.0, 75.32 - k.SCREEN_TOP)   # the old screen at the theme's top (it was 0.6 px lower):
                                                         # src/TOCANTE.cpp's displays draw in it, so it keeps its margin of 5
LINE = 0.351                    # the old lines' width, in units
OUTS = (('M', '/4', '/8'), ('BPM', '/12', '/16'), ('/32', '/64', '/128'))   # per column, from row 2 down to row 0

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'tocante_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def line(d):
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:{LINE}" d="{d}"/>'

def vline(x, y0, y1):
    return line(f'M{x*K:.3f},{y0*K:.3f}V{y1*K:.3f}')

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('tOCAnTe', PW), k.screen(*SCREEN)]
    # the lines, drawn under the controls so only their gaps show: each knob down to its jack, RESET and RUN down to
    # their fork, the fork across to the input and output under it
    for x in COLS:
        out.append(vline(x, KNOB_Y, CV_Y))
    out.append(vline(COLS[0], LOW_Y, LOW_CV_Y))
    for x in COLS[1:]:
        out.append(vline(x, LOW_Y, FORK_Y))
        out.append(line(f'M{(x-FORK)*K:.3f},{LOW_CV_Y*K:.3f}V{FORK_Y*K:.3f}H{(x+FORK)*K:.3f}V{LOW_CV_Y*K:.3f}'))
        out.append(k.box(x + FORK - OUT_SQUARE / 2, LOW_CV_Y - OUT_SQUARE / 2, OUT_SQUARE, OUT_SQUARE, k.OUTPUT))
    for x, top, low in zip(COLS, ('BPM', 'BEATS', 'BAR'), ('FINE', 'RESET', 'RUN')):
        out.append(k.text(top, x, KNOB_Y - LABEL_ABOVE, k.KNOB_LABEL_SIZE, k.INK))
        out.append(k.text(low, x, LOW_Y - LABEL_ABOVE, k.KNOB_LABEL_SIZE, k.INK))
    # the nine outputs: one box each, three columns on rows 2 to 0
    for i, names in enumerate(OUTS):
        for j, lab in enumerate(names):
            out.append(k.grid_box(i, i, 3, 2 - j, PW, k.OUTPUT))
            out.append(k.jack_label(lab, k.jack_col(i, 3, PW), k.row(2 - j)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('TOCANTE.svg', build())
