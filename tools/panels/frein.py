# fREIN's panel (3 HP), migrated from the hand-drawn one: the same arrangement (the state light under the title, then
# down the middle STOP and START, each a button joined by a line to its CV jack, TIME a knob joined to its CV jack, IN
# and OUT at the bottom), now in the theme's boxes, type and colours, with the old picture kept (frein_parts.svg). No
# screws, as before. The positions are the same in src/FREIN.cpp.
# The 3 HP rules (title and logo too wide for 45 px) live here and hCTIP's and lATe's scripts use them.
import os, re
import panel_kit as k
from fontTools.pens.boundsPen import BoundsPen

PW = 45
W = PW * k.K
COL = PW / 2                       # everything on the panel's centre, jack_col(0, 1, PW) (src/FREIN.cpp: stop's column)
SIDE = 3.0                         # a 3 HP title and logo leave this much to each side of the panel
LINE = 0.26458                     # the old lines' width, in units
BUTTON_Y = 50 + 27.9958782 / 2     # the STOP button at the old CKD6's centre (corner 50), now a BidooRoundButton
GROUP_PITCH = 79.0                 # from one control and its CV jack to the next (src/FREIN.cpp kGroupPitch)
CV_Y = 90 + 15 / 2                 # the first CV jack at the old tiny jack's centre (corner 90), now a full jack
KNOB_Y = 208 + 29.5275591 / 2      # the TIME knob, old corner 208 (src/FREIN.cpp speed)
LOGO_W = 41.3984                   # panel_kit.logo's outlines, px wide (Inkscape --query-all of text898)
LOGO_BOTTOM = 375.0                # and their bottom (362.602 + 12.398)

def _ink(s, size):
    # the visible left and right of a text centred on 0 as panel_kit.text centres it (on its advance)
    sc = size / k._upm
    x = -sum(k._glyphs[k._cmap[ord(c)]].width for c in s) * sc / 2
    lo, hi = 1e9, -1e9
    for c in s:
        g = k._glyphs[k._cmap[ord(c)]]
        p = BoundsPen(k._glyphs)
        g.draw(p)
        if p.bounds:
            lo, hi = min(lo, x + p.bounds[0] * sc), max(hi, x + p.bounds[2] * sc)
        x += g.width * sc
    return lo, hi

def title_size(name, width_px=PW):
    # the largest size, by 0.5 steps, whose outlines leave SIDE px each side, capped at the theme's 16
    size = k.TITLE_SIZE
    while True:
        lo, hi = _ink(name, size)
        if width_px / 2 + lo >= SIDE and width_px / 2 - hi >= SIDE:
            return size
        size -= 0.5

def title(name, width_px=PW):
    return k.text(name, width_px / 2, k.TITLE_Y, title_size(name, width_px), k.INK)

def logo(width_px=PW):
    # panel_kit.logo is 41.4 px wide: scaled about its bottom centre to leave SIDE px each side
    s = min(1.0, (width_px - 2 * SIDE) / LOGO_W)
    cx, cy = width_px / 2 * k.K, LOGO_BOTTOM * k.K
    return (f'<g transform="translate({cx:.5f},{cy:.5f}) scale({s:.5f}) translate({-cx:.5f},{-cy:.5f})">'
            f'{k.logo(width_px * k.K)}</g>')

def line(y0, y1, x=COL, width=LINE):
    # the old rule down from a control to its CV jack
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:{width}" d="M{x * k.K:.4f},{y0 * k.K:.4f}V{y1 * k.K:.4f}"/>'

def parts(name):
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), name), encoding='utf-8') as f:
        return re.search(r'<svg[^>]*>(.*)</svg>', f.read(), re.S).group(1)

def jack(lab, r, colour):
    # one box per jack on grid row r, 10 px from the sides as the theme's: 25 px for the 23.7 px jack
    return [k.grid_box(0, 0, 1, r, PW, colour), k.jack_label(lab, k.jack_col(0, 1, PW), k.row(r))]

def build():
    out = [k.header(PW, W), k.background(W), parts('frein_parts.svg'), title('fREIN')]   # 14.5
    for i, lab in enumerate(('STOP', 'START')):
        y = BUTTON_Y + GROUP_PITCH * i
        out.append(line(y, CV_Y + GROUP_PITCH * i))
        out.append(k.small_label(lab, COL, y))
    out.append(line(KNOB_Y, CV_Y + 2 * GROUP_PITCH))
    out.append(k.knob_label('TIME', COL, KNOB_Y))
    out += jack('IN', 1, k.INPUT)
    out += jack('OUT', 0, k.OUTPUT)
    out.append(logo())
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('FREIN.svg', build())
