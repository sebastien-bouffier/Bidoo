# OUAIve's panel (9 HP), migrated from the hand-drawn one: the same arrangement (the position reset in and end of cycle
# out beside the title, the screen, the TYPE and PLAY MODE buttons, the slices and speed trimpots with their CV, the
# jacks below), now in the theme's screen, boxes, type and colours, with the old picture and icons kept
# (ouaive_parts.svg). The positions are the same in src/OUAIVE.cpp (OUAIVEWidget).
import os, re
import panel_kit as k

PW = 135
W = PW * k.K
N = 4                                  # the bottom row's columns, the same in src/OUAIVE.cpp
# the controls keep their old rows (src/OUAIVE.cpp): the slices and speed trimpots and the PLAY MODE button over them
# 8 px left, the TYPE button at its old centre
TRIM_HALF = 19.1929131 / 2             # BidooBlueTrimpot
# the slices trimpot 8 px left of its old corner (58, 250): its CV amount then clears the CV box by 5 px, and PLAY MODE
# above it stays 7 px from TYPE's label (in the POS column it would touch it). TRIM_LEFT in src/OUAIVE.cpp
TRIM_LEFT = 8
SLICES = (58 - TRIM_LEFT + TRIM_HALF, 250 + TRIM_HALF)
SPEED_Y = SLICES[1] + 25               # kSpeedBelow
# TYPE over the TRIG jack (the old CKD6's centre was 0.375 px left), PLAY MODE over the trimpots
BUTTON_X = (k.jack_col(0, N, PW), SLICES[0])
CV_X = k.jack_col(N - 1, N, PW)        # the tiny CV jacks' column: over OUT R, in its column's box (1.9 px left)
CV_Y = 222 + 7.5                       # the read mode CV jack, old corner 222; the buttons on its row (0.5 px lower)
CV_BOX = (k.box_left(N - 1, N, PW), k.box_right(N - 1, N, PW))   # the CV box's edges, OUT R's column's
SCREEN_BOTTOM = CV_Y - k.BOX_ABOVE - k.BOX_GAP   # 4 px above the CV box, the same in src/OUAIVE.cpp
ICON_X = BUTTON_X[0]                   # the slices and speed icons, in the TYPE button's column (they were 1 px right)

def parts():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ouaive_parts.svg'), encoding='utf-8') as f:
        return f.read()

def part(s, pid):
    # an element of the parts file by its id, with the groups around it: one a line
    return re.search(r'^(.*id="' + pid + r'".*)$', s, re.M).group(1)

def moved(svg, dx, dy):
    return f'<g transform="translate({dx * k.K:.5f},{dy * k.K:.5f})">{svg}</g>'

def line(x0, x1, y):
    # the old thin rules joining a row's controls to its CV jack, drawn under them
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:0.26458" d="M{x0 * k.K:.4f},{y * k.K:.4f}H{x1 * k.K:.4f}"/>'

def build():
    s = parts()
    out = [k.header(PW, W), k.background(W), part(s, 'picture'), k.title('OUAIve', PW)]
    # the little icons of the reset in and end of cycle out, beside their tiny jacks (see src/OUAIVE.cpp)
    out += [part(s, 'reset'), part(s, 'eoc')]
    out.append(k.screen(k.MARGIN, k.SCREEN_TOP, PW - 2 * k.MARGIN, SCREEN_BOTTOM - k.SCREEN_TOP))
    # the buttons' labels
    out.append(k.small_label('TYPE', BUTTON_X[0], CV_Y))
    out.append(k.small_label('PLAY MODE', BUTTON_X[1], CV_Y))
    # each row's rule from its first control to the CV box
    for y in (CV_Y, SLICES[1], SPEED_Y):
        out.append(line(BUTTON_X[1], CV_BOX[0], y))
    # the slices (a saw) and speed (a keyboard) icons, on their rows, in the panel's ink
    for pid, (cx, cy), y in (('saw', (23.95, 258.75), SLICES[1]), ('keyboard', (24.0, 283.5), SPEED_Y)):
        out.append(moved(part(s, pid).replace('#2a5775', k.INK), ICON_X - cx, y - cy))
    # the CV jacks: one box down the right, under the screen's edge
    out.append(k.box(CV_BOX[0], CV_Y - k.BOX_ABOVE, CV_BOX[1] - CV_BOX[0], SPEED_Y + k.BOX_BELOW - CV_Y + k.BOX_ABOVE, k.INPUT))
    out.append(k.jack_label('CV', CV_X, CV_Y))   # 2.6 px under TYPE and PLAY MODE: a caption in a box, they label buttons
    # the bottom row: gate and position in, the stereo out over two columns
    out.append(k.grid_box(0, 0, N, 0, PW, k.INPUT))
    out.append(k.grid_box(1, 1, N, 0, PW, k.INPUT))
    out.append(k.grid_box(2, 3, N, 0, PW, k.OUTPUT))
    for i, lab in enumerate(('TRIG', 'POS', 'OUT L', 'OUT R')):
        out.append(k.jack_label(lab, k.jack_col(i, N, PW), k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('OUAIVE.svg', build())
