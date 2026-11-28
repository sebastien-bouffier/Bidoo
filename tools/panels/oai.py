# oAï's panel (8 HP), migrated from the hand-drawn one: the same arrangement (the sample light and the channel knob
# under the title, two rows of three knobs, the loop and gate switches with the kill knob, three rows of jacks), now in
# the theme's boxes, type and colours, with the old picture and the knobs' dial ticks kept (oai_parts.svg). maGma's
# panel is this one with four preset buttons (magma.py). The positions are the same in src/SliceSampler.hpp
# (sliceSamplerControls).
import os, re
import panel_kit as k

PW = 120
W = PW * k.K
N = 4                                  # the CV rows' jack columns, the same in src/SliceSampler.hpp
# the knob columns: the outer ones over the outer jacks (the old knobs were 0.26 px right of the first, 0.74 px left of
# the last), the middle one on the panel's centre (0.24 px)
KNOB_X = (k.jack_col(0, N, PW), PW / 2, k.jack_col(N - 1, N, PW))
KNOB_HALF = 29.5275591 / 2             # BidooBlueKnob: the knobs keep their old rows, from their old corners
CHANNEL_Y = 35 + KNOB_HALF             # the channel knob, old corner 35
ROW_Y = (85 + KNOB_HALF, 135 + KNOB_HALF, 185 + KNOB_HALF)   # START LEN TUNE, FLT.TYP. FREQ RES, KILL
SWITCH_Y = 190 + 20.64111 / 2          # the CKSS switches, old corner 190
SWITCH_HALF = 20.64111 / 2
# the old dials' centres (fitted to their ticks): each set of ticks moves onto its knob, with its marks
CHANNEL_DIAL = (60.0, 50.05)
FILTER_DIAL = (21.99, 149.45)
KILL_DIAL = (98.0, 200.27)
DIAL_SIZE = 4.5                        # the dial marks: small, like the old ones (4), so they stay outside the dial
# The boxes: one per jack, as on the old panel, so their rows reach 6 px from the panel's sides (the theme's margin is
# 10): four boxes holding full jacks need the room. 2 px between the CV boxes, the theme's 4 between the three below.
# The same in src/SliceSampler.hpp (kBoxLeft, kBoxRight, kCvGap).
BOX_LEFT, BOX_RIGHT, CV_GAP = 6.0, PW - 6.0, 2.0

def box_width(n, gap):
    return (BOX_RIGHT - BOX_LEFT - (n - 1) * gap) / n

def box_centre(i, n, gap):
    w = box_width(n, gap)
    return BOX_LEFT + w / 2 + i * (w + gap)

def jack_boxes(n, gap, r, colours):
    out = []
    w = box_width(n, gap)
    for i in range(n):
        out.append(k.box(box_centre(i, n, gap) - w / 2, k.box_top(r), w, k.box_bottom(r) - k.box_top(r), colours[i]))
    return out

CV_NAMES = (('STRT', 'LEN', 'V/O', 'LOOP'), ('FLTTP', 'FREQ', 'RES', 'GATE'))   # rows 2 and 1

def parts():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'oai_parts.svg'), encoding='utf-8') as f:
        return f.read()

def part(s, pid):
    # an element of the parts file by its id, with the groups around it: one a line
    m = re.search(r'^(.*id="' + pid + r'".*)$', s, re.M)
    return m.group(1)

def moved(svg, dx, dy):
    # px -> panel units
    return f'<g transform="translate({dx * k.K:.5f},{dy * k.K:.5f})">{svg}</g>'

def build(name='oAï', source=None, extra=()):
    s = source if source is not None else parts()
    out = [k.header(PW, W), k.background(W), part(s, 'picture'), k.title(name, PW)]
    # the dial ticks, each set centred on its knob
    dials = ((CHANNEL_DIAL, (KNOB_X[1], CHANNEL_Y), 'channel-ticks'), (FILTER_DIAL, (KNOB_X[0], ROW_Y[1]), 'filter-ticks'),
             (KILL_DIAL, (KNOB_X[2], ROW_Y[2]), 'kill-ticks'))
    shift = {}
    for (ox, oy), (x, y), pid in dials:
        shift[pid] = (x - ox, y - oy)
        out.append(moved(part(s, pid), x - ox, y - oy))
    # the filter type's marks and the kill knob's OFF, at their old places moved with their ticks
    dx, dy = shift['filter-ticks']
    for lab, x, y in (('LP', 4.5, 136.1), ('BP', 40.0, 136.1), ('OFF', 6.8, 169.65), ('HP', 35.9, 169.65)):
        out.append(k.text(lab, x + dx, y + dy, DIAL_SIZE, k.INK))
    dx, dy = shift['kill-ticks']
    # on the switches' lower baseline (0.08 px higher)
    out.append(k.text('OFF', 82.7 + dx, SWITCH_Y + SWITCH_HALF + 4 + k.CAP_HEIGHT * k.BOX_LABEL_SIZE, DIAL_SIZE, k.INK))
    # the knobs' labels
    for lab, i in (('START', 0), ('LEN', 1), ('TUNE', 2)):
        out.append(k.knob_label(lab, KNOB_X[i], ROW_Y[0]))
    for lab, i in (('FLT.TYP.', 0), ('FREQ', 1), ('RES', 2)):
        out.append(k.knob_label(lab, KNOB_X[i], ROW_Y[1]))
    out.append(k.knob_label('KILL', KNOB_X[2], ROW_Y[2]))
    # the switches: their two positions above and below them, as lIMbO's
    for x, up, down in ((KNOB_X[0], 'LOOP', 'OFF'), (KNOB_X[1], 'TRIG', 'GATE')):
        out.append(k.text(up, x, SWITCH_Y - SWITCH_HALF - 4, k.BOX_LABEL_SIZE, k.INK))
        out.append(k.text(down, x, SWITCH_Y + SWITCH_HALF + 4 + k.CAP_HEIGHT * k.BOX_LABEL_SIZE, k.BOX_LABEL_SIZE, k.INK))
    # the CV jacks: rows 2 and 1, a box each
    for r, names in zip((2, 1), CV_NAMES):
        out.extend(jack_boxes(N, CV_GAP, r, [k.INPUT] * N))
        for i, lab in enumerate(names):
            out.append(k.jack_label(lab, box_centre(i, N, CV_GAP), k.row(r)))
    # the bottom row: trigger and kill in, the poly out, three boxes between the same edges (TRIG and OUT 1.2 px
    # inside the knob columns)
    out.extend(jack_boxes(3, k.BOX_GAP, 0, [k.INPUT, k.INPUT, k.OUTPUT]))
    for i, lab in enumerate(('TRIG', 'KILL', 'OUT')):
        out.append(k.jack_label(lab, box_centre(i, 3, k.BOX_GAP), k.row(0)))
    out.extend(extra)
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('OAI.svg', build())
