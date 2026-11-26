# bAFIs's panel (15 HP, 2026-11-26): the hand-drawn 10 HP panel's picture (bafis_parts.svg) stretched over the new width,
# its four-band drawing redrawn on the new columns, every control with its small CV jack on its right, joined by a level
# line, and the rows named on the left (rotated, as before). The rows' heights are the old panel's.
# The columns, rows and offsets are the same in src/BAFIS.cpp (kLeft, kRowY, kKnobDx, kJackDx...).
import os, re
import panel_kit as k

PW = 225
K = k.K
W = PW * K
LEFT = 14.0                     # the columns start after the row names (src/BAFIS.cpp kLeft)
BANDS = 4
FREQ_Y = 44.76                  # the crossover row (src/BAFIS.cpp kFreqY)
ROW_Y = (96.81, 140.81, 184.81, 228.32, 272.81)   # Q, GAIN, TYPE, PRE/POST, VOL (src/BAFIS.cpp kRowY)
ROW_NAMES = ('Q', 'GAIN', 'TYPE', 'PRE/POST', 'VOL')
KNOB_DX, JACK_DX = 11.0, 13.0   # a control left of its cell's centre, its jack right of it (src/BAFIS.cpp kKnobDx, kJackDx)
FREQ_KNOB_DX, FREQ_JACK_DX = 12.0, 14.5   # the same for the bigger crossover knobs (src/BAFIS.cpp kFreqKnobDx, kFreqJackDx)
NAME_X = 7.5                    # the row names' centre
NAME_SIZE = 7.0
DRAW_TOP, DRAW_BOTTOM, DRAW_SLOPE = 79.3, 101.0, 5.2   # the four-band drawing (the old one's height and slopes)

def band_pitch():
    return (PW - LEFT - k.MARGIN) / BANDS

def band_x(i):
    return LEFT + band_pitch() * (i + 0.5)

def cross_x(i):   # the crossover between bands i and i + 1
    return LEFT + band_pitch() * (i + 1)

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'bafis_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    d = re.search(r'<path id="picture"[^>]*\sd="([^"]*)"', s).group(1)
    return (f'<g transform="scale({W / 39.6875:.6f},1)"><path id="picture" style="opacity:{k.PICTURE_OPACITY};'
            f'fill:#000000" d="{d}"/></g>')

def stroke(d, width=0.24):
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:{width}" d="{d}"/>'

def pt(x, y):
    return f'{x*K:.3f},{y*K:.3f}'

def bands():
    # each band a flat top between its crossovers, falling across them (the slopes of two bands cross there)
    out = []
    left, right = LEFT, PW - k.MARGIN
    for i in range(BANDS):
        a = cross_x(i - 1) if i > 0 else left
        b = cross_x(i) if i < BANDS - 1 else right
        pts = []
        if i > 0:
            pts.append(pt(a - DRAW_SLOPE, DRAW_BOTTOM))
            pts.append(pt(a + DRAW_SLOPE, DRAW_TOP))
        else:
            pts.append(pt(a, DRAW_TOP))
        if i < BANDS - 1:
            pts.append(pt(b - DRAW_SLOPE, DRAW_TOP))
            pts.append(pt(b + DRAW_SLOPE, DRAW_BOTTOM))
        else:
            pts.append(pt(b, DRAW_TOP))
        out.append(stroke('M' + ' L'.join(pts)))
    return out

def link(x, y, knob_dx=KNOB_DX, jack_dx=JACK_DX):
    # level, from the control's centre to its jack's (both drawn over it, so only the gap shows)
    return stroke(f'M{pt(x - knob_dx, y)}H{(x + jack_dx)*K:.3f}', 0.32)

def rotated(s, x, y):
    # a row's name, read from the bottom up, centred on (x, y)
    return f'<g transform="rotate(-90,{x*K:.3f},{y*K:.3f})">' + k._text(s, x, y + NAME_SIZE * 0.36, NAME_SIZE, k.INK) + '</g>'

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('bAFIs', PW)]
    out += bands()
    for i in range(BANDS - 1):
        out.append(link(cross_x(i), FREQ_Y, FREQ_KNOB_DX, FREQ_JACK_DX))
    for r, y in enumerate(ROW_Y):
        out.append(rotated(ROW_NAMES[r], NAME_X, y))
        for i in range(BANDS):
            out.append(link(band_x(i), y))
    # IN and OUT, in boxes on the grid's bottom row (columns 0 and 3 of 4)
    out.append(k.grid_box(0, 0, 4, 0, PW, k.INPUT))
    out.append(k.jack_label('IN', k.jack_col(0, 4, PW), k.row(0)))
    out.append(k.grid_box(3, 3, 4, 0, PW, k.OUTPUT))
    out.append(k.jack_label('OUT', k.jack_col(3, 4, PW), k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('BAFIS.svg', build())
