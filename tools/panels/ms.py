# MS's panel (3 HP), migrated from the hand-drawn one: the same arrangement (four tall boxes down the middle, two jacks
# each: L R in, M S out, M S in, L R out; each input box joined to its output box by a bar), now in the theme's boxes,
# type and colours, with the old picture kept (ms_parts.svg). Each jack is named in its box (L, R, M, S) where the old
# panel wrote one name (LR, MS) between the two. No screws, as before. The positions are the same in src/MS.cpp.
import panel_kit as k
import frein as f                     # the 3 HP title, logo, line and boxes (frein.py)

PW = 45
W = PW * k.K
COL = k.jack_col(0, 1, PW)             # the panel's centre, as before
ROW_GAP = 40.0                         # the two jacks of a box, as the old ones: src/MS.cpp kJackPitch
BOX_GAP = 3.0                          # between the boxes: src/MS.cpp kBoxGap
BOX_H = k.BOX_ABOVE + ROW_GAP + k.BOX_BELOW   # 82
PAIR_PITCH = BOX_H + BOX_GAP           # 85: the last box on the bottom row, the first's top at 22, 5 px under the title
BAR_W = 7.3                            # the old bars joining an input box to its output box, centred
BOXES = ((('L', 'R'), k.INPUT), (('M', 'S'), k.OUTPUT), (('M', 'S'), k.INPUT), (('L', 'R'), k.OUTPUT))

def upper_y(p):
    # the upper jack of box p from the top; the last box's lower jack on the bottom row
    return k.row(0) - ROW_GAP - PAIR_PITCH * (len(BOXES) - 1 - p)

def bar(p):
    # from the middle of box p to the middle of box p + 1, under both
    y0, y1 = upper_y(p) + ROW_GAP / 2, upper_y(p + 1) + ROW_GAP / 2
    x = COL - BAR_W / 2
    return f'<path style="fill:{k.INPUT};stroke:none" d="M{x * k.K:.4f},{y0 * k.K:.4f}h{BAR_W * k.K:.4f}V{y1 * k.K:.4f}h{-BAR_W * k.K:.4f}Z"/>'

def build():
    out = [k.header(PW, W), k.background(W), f.parts('ms_parts.svg'), f.title('MS')]   # 16, the theme's size
    out += [bar(0), bar(2)]
    x0, x1 = k.box_left(0, 1, PW), k.box_right(0, 1, PW)   # 10 to 35, as grid_box: a full jack (23.7) fits the theme's margins
    for p, (names, colour) in enumerate(BOXES):
        y = upper_y(p)
        out.append(k.box(x0, y - k.BOX_ABOVE, x1 - x0, BOX_H, colour))
        out.append(k.jack_label(names[0], COL, y))
        out.append(k.jack_label(names[1], COL, y + ROW_GAP, jack_above=y))
    out.append(f.logo())   # scaled to leave 3 px each side, as the other 3 HP panels
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('MS.svg', build())
