# REI's panel (26 HP, 2026-11-26): Grotte's controls on Prism, grouped by theme. The picture is the old hand-drawn
# panel's (rei_parts.svg), stretched over the new width. Three rows of seven cells: a knob (or a button) on the left of
# its cell and its small CV jack on the right at the same height, joined by a level line, the label above the cell; a
# group's name above its cells, between two rules; then the small controls and the jacks in boxes. The cells, rows and offsets are the same in src/REI.cpp (kCells, kRowY, kKnobDx, kJackDx).
import os, re
import panel_kit as k

PW = 390
K = k.K
W = PW * K
N = 7
ROW_Y = (74.0, 146.0, 218.0)   # the knobs' centres (src/REI.cpp kRowY)
KNOB_DX, JACK_DX = 12.0, 15.0   # a cell's knob left of its column, its jack right of it (src/REI.cpp kKnobDx, kJackDx)
LABEL_SIZE = 7.5                # the columns are 52.9 px
LABEL_ABOVE = 22.0              # kKnobLabelAbove
GROUP_ABOVE = 38.0              # a group's name, above its row's knobs
GROUP_SIZE = 7.0

# (column, row, label, has a jack); src/REI.cpp kCells in the same order
CELLS = (
    (0, 0, 'STYLE', True), (1, 0, 'SIZE', True), (2, 0, 'STRETCH', True), (3, 0, 'PREDELAY', True),
    (4, 0, 'SYNC', False), (5, 0, 'OFFSET', False), (6, 0, 'FREEZE', True),
    (0, 1, 'MOTION', True), (1, 1, 'DENSITY', True), (2, 1, 'DEPTH', True), (3, 1, 'AIR', True),
    (4, 1, 'WIDTH', True), (5, 1, 'BUILD', True), (6, 1, 'MIX', True),
    (0, 2, 'SHIMMER', True), (1, 2, 'TONE', True), (2, 2, 'INTERVAL', True), (3, 2, 'DUCK', True),
    (4, 2, 'GATE', True), (5, 2, 'HOLD', True), (6, 2, 'SYNC', False),
)
# (first column, last column, row, name or None): the groups
GROUPS = (
    (0, 5, 0, 'SPACE'), (6, 6, 0, None),
    (0, 5, 1, 'CHARACTER'), (6, 6, 1, None),
    (0, 2, 2, 'SHIMMER'), (3, 6, 2, 'DYNAMICS'),
)

def text_width(s, size):
    return sum(k._glyphs[k._cmap[ord(c)]].width for c in s) * size / k._upm

def line(x0, y0, x1, y1):
    return f'<path style="fill:none;stroke:{k.INK};stroke-width:0.26" d="M{x0*K:.3f},{y0*K:.3f}L{x1*K:.3f},{y1*K:.3f}"/>'

def picture():
    with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'rei_parts.svg'), encoding='utf-8') as f:
        s = f.read()
    d = re.search(r'<path id="picture"[^>]*\sd="([^"]*)"', s).group(1)
    return (f'<g transform="scale({W / 27.78125:.6f},1)"><path id="picture" style="opacity:{k.PICTURE_OPACITY};'
            f'fill:#000000" d="{d}"/></g>')

def build():
    out = [k.header(PW, W), k.background(W), picture(), k.title('REI', PW)]
    pitch = k.col_pitch(N, PW)
    for i0, i1, r, name in GROUPS:
        y = ROW_Y[r] - GROUP_ABOVE
        left, right = k.col(i0, N, PW) - pitch / 2 + 3, k.col(i1, N, PW) + pitch / 2 - 3
        if name:
            # the name in the middle of the group, a rule on each side of it at the capitals' half height
            out.append(k.text(name, (left + right) / 2, y, GROUP_SIZE, k.INK))
            half = text_width(name, GROUP_SIZE) / 2 + 4
            mid, ym = (left + right) / 2, y - 2.5
            out.append(line(left, ym, mid - half, ym))
            out.append(line(mid + half, ym, right, ym))
    for i, r, label, jack in CELLS:
        x, y = k.col(i, N, PW), ROW_Y[r]
        if jack:
            # the link: level, from the knob's centre to the jack's (both drawn over it, so only the gap shows)
            out.append(f'<path style="fill:none;stroke:{k.INK};stroke-width:0.32" '
                       f'd="M{(x-KNOB_DX)*K:.3f},{y*K:.3f}H{(x+JACK_DX)*K:.3f}"/>')
        out.append(k.text(label, x, y - LABEL_ABOVE, LABEL_SIZE, k.INK))
    # the second row: input level and pan, the clipping switch, output level and pan (small knobs)
    for i, label in ((0, 'INPUT'), (1, 'IN PAN'), (3, 'CLIP'), (5, 'OUTPUT'), (6, 'OUT PAN')):
        out.append(k.small_label(label, k.col(i, N, PW), k.row(1)))
    # the jacks: the stereo input, the clock (for the two SYNCs), the stereo output
    out.append(k.grid_box(0, 1, N, 0, PW, k.INPUT))
    out.append(k.grid_box(3, 3, N, 0, PW, k.INPUT))
    out.append(k.grid_box(5, 6, N, 0, PW, k.OUTPUT))
    for i, label in ((0, 'IN L'), (1, 'IN R'), (3, 'CLOCK'), (5, 'OUT L'), (6, 'OUT R')):
        out.append(k.jack_label(label, k.jack_col(i, N, PW), k.row(0)))
    out.append(k.logo(W))
    out.append('</svg>')
    return out

if __name__ == '__main__':
    k.write('REI.svg', build())
