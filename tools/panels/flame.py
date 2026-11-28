# fLAME's panel (10 HP), migrated from the hand-drawn one: the same arrangement (the flame screen under the title, the
# FFT sizes and the colours as six buttons in one row under their group names, the stereo jacks in three tall boxes, L
# above R), now in the theme's boxes, type and colours, with the old picture kept (flame_parts.svg). The positions are
# the same in src/FLAME.cpp.
import os, re
import panel_kit as k

PW = 150
W = PW * k.K
SCREEN = (10, 28, 130, 200)          # the widget's display: the old corner, FLAME::H rows tall (src/FLAME.cpp)
N_BUTTONS = 6
BUTTON_Y = k.box_top(1) - k.BOX_GAP - 9   # the buttons' row, where the old 18 px LEDButtons were centred (src/FLAME.cpp kButtonRowY)
# The buttons' names: six in 130 px, so smaller than the theme's 7.5 (CuSO4 would touch CuCl2), 3 px over the buttons'
# top (13 above their centre, as EMILE's): near their buttons, with 5.4 px under the screen for the group names above
NAME_SIZE, NAME_ABOVE = 6.0, 10 + 3
GROUP_SIZE, GROUP_GAP = 6.5, 2.5     # a group's name, 2.5 px above the buttons' names
BUTTON_NAMES = (('512', ''), ('1024', ''), ('2048', ''), ('NaCl', ''), ('CuCl', '2'), ('CuSO', '4'))   # name, subscript
GROUPS = (('FFT SIZE', 1), ('COLOR', 4))   # over their middle button
JACKS = (('IN', k.INPUT), ('BAND', k.OUTPUT), ('AMP', k.OUTPUT))   # three columns, L on row 1 above R on row 0
SUB = 0.65                            # a subscript: its size, and its drop below the baseline, per unit of size
SUB_DROP = 0.2

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flame_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    return re.search(r'<svg[^>]*>(.*)</svg>', s, re.S).group(1)

def width(s, size):
    return sum(k._glyphs[k._cmap[ord(c)]].width for c in s) * size / k._upm

def name(s, sub, x, y, size):
    # a chemical formula with its subscript, the whole centred on x
    if not sub:
        return k.text(s, x, y, size, k.INK)
    a, b = width(s, size), width(sub, size * SUB)
    left = x - (a + b) / 2
    k.marks.append(('label', s + sub, x, y, size))
    return k._text(s, left + a / 2, y, size, k.INK) + k._text(sub, left + a + b / 2, y + SUB_DROP * size, size * SUB, k.INK)

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('fLAME', PW)]
    out.append(k.screen(*SCREEN))
    name_y = BUTTON_Y - NAME_ABOVE
    for i, (s, sub) in enumerate(BUTTON_NAMES):
        out.append(name(s, sub, k.col(i, N_BUTTONS, PW), name_y, NAME_SIZE))
    for s, i in GROUPS:
        out.append(k.text(s, k.col(i, N_BUTTONS, PW), name_y - k.CAP_HEIGHT * NAME_SIZE - GROUP_GAP, GROUP_SIZE, k.INK))
    # the jacks: one tall box per column, rows 1 and 0
    for i, (s, colour) in enumerate(JACKS):
        out.append(k.grid_box(i, i, 3, 1, PW, colour, k_low=0))
        x = k.jack_col(i, 3, PW)
        out.append(k.jack_label(s + ' L', x, k.row(1)))
        out.append(k.jack_label(s + ' R', x, k.row(0), jack_above=k.row(1)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('FLAME.svg', build())
