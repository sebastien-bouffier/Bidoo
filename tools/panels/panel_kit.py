# What the panel generators share: text as DejaVu outlines, boxes, the screen frame, the logo, the mangled picture.
# Colours and layout are not defined here: they are read from src/BidooTheme.hpp (and plugin.hpp for the *_BIDOO
# colours), the single source shared with the widgets. Rules in docs/THEME.md.
import os, random, re, sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RES = os.path.join(REPO, 'res')

# Output folder, for checking without touching res/. The logo and sAiSOn's panel are always read from res/.
out_dir = RES

# px -> SVG units of every panel. A unit conversion, not a matter of taste
K = 39.6875 / 150.0
H = 100.54166

# ---- The theme, read from the header
def _read_header(name):
    with open(os.path.join(REPO, 'src', name), encoding='utf-8') as f:
        return f.read()

def _hex(expr, source):
    m = re.fullmatch(r'nvgRGBA?\(\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*(?:,\s*\w+\s*)?\)', expr)
    if m:
        return '#%02x%02x%02x' % tuple(int(g, 0) for g in m.groups())
    m = re.search(r'const NVGcolor ' + expr + r'\s*=\s*([^;]+);', source)   # YELLOW_BIDOO and friends
    assert m, 'colour not understood: ' + expr
    return _hex(m.group(1), source)

def _read_theme():
    src, plugin = _read_header('BidooTheme.hpp'), _read_header('plugin.hpp')
    values = {}
    for name, expr in re.findall(r'^static constexpr float (k\w+) = ([^;]+);', src, re.M):
        expr = re.sub(r'(\d+\.?\d*)f\b', r'\1', expr)
        values[name] = eval(expr, {'__builtins__': {}}, values)
    for name, expr in re.findall(r'^const NVGcolor (k\w+) = ([^;]+);', src, re.M):
        values[name] = _hex(expr, plugin)
    # the grid's functions, the same expressions as in C++: `static constexpr float name(type a, ...) { return e; }`
    for name, args, expr in re.findall(r'^static constexpr float ([a-z]\w*)\(([^)]*)\) \{ return ([^;]+); \}', src, re.M):
        params = [a.split()[-1] for a in args.split(',')]
        expr = re.sub(r'(\d+\.?\d*)f\b', r'\1', expr)
        # the namespace is the lambdas' globals, so that they see the constants and each other
        ns = values.setdefault('_ns', {'__builtins__': {}})
        ns.update((k, v) for k, v in values.items() if k != '_ns')
        values[name] = ns[name] = eval('lambda ' + ', '.join(params) + ': ' + expr, ns)
    return values

T = _read_theme()
BOTTOM_ROW, SECOND_ROW = T['kBottomRowY'], T['kSecondRowY']
BOTTOM_TOP, BOTTOM_BOTTOM = T['kBottomBoxTop'], T['kBottomBoxBottom']
SECOND_TOP, SECOND_BOTTOM = T['kSecondBoxTop'], T['kSecondBoxBottom']
BOX_ABOVE, BOX_BELOW, LABEL_ABOVE = T['kBoxAboveJack'], T['kBoxBelowJack'], T['kBoxLabelAboveJack']
MARGIN, SCREEN_TOP, TITLE_Y, TITLE_SIZE = T['kSideMargin'], T['kScreenTop'], T['kTitleY'], T['kTitleSize']
KNOB_LABEL_ABOVE, KNOB_LABEL_SIZE, BOX_LABEL_SIZE = T['kKnobLabelAbove'], T['kKnobLabelSize'], T['kBoxLabelSize']
SMALL_LABEL_ABOVE = T['kSmallLabelAbove']
PANEL, INK, WHITE = T['kPanelGrey'], T['kPanelText'], T['kBoxText']
INPUT, OUTPUT, FRAME, SCREEN = T['kInputBox'], T['kOutputBox'], T['kScreenFrame'], T['kScreenBlack']
PICTURE_OPACITY = T['kPictureOpacity']

# ---- The grid (docs/THEME.md): row k from the bottom, column i of n across the width between the side margins, in px
row, col, col_pitch = T['row'], T['col'], T['colPitch']
box_left, box_right, box_top, box_bottom = T['boxLeft'], T['boxRight'], T['boxTop'], T['boxBottom']
jack_col = T['jackCol']
screen_bottom, screen_above_knobs = T['screenBottom'], T['screenAboveKnobs']
caption_under_jack = T['captionUnderJack']
JACK_HALF = T['kJackHalf']
CAP_HEIGHT = 1493 / 2048           # DejaVu's capitals, per unit of size
ROW_PITCH, BOX_GAP = T['kRowPitch'], T['kBoxGap']
assert row(0) == BOTTOM_ROW and row(1) == SECOND_ROW

# ---- The font: from RACK_DIR, or next to this plugin in a Rack folder, or the usual place
def _find_font():
    places = []
    if os.environ.get('RACK_DIR'):
        d = re.sub(r'^/([a-zA-Z])/', r'\1:/', os.environ['RACK_DIR'])   # an MSYS path
        places.append(os.path.join(d, 'res', 'fonts', 'DejaVuSans.ttf'))
    places.append(os.path.join(REPO, '..', '..', 'res', 'fonts', 'DejaVuSans.ttf'))
    places.append('G:/RackV2/res/fonts/DejaVuSans.ttf')
    for p in places:
        if os.path.isfile(p):
            return p
    sys.exit('DejaVuSans.ttf not found, set RACK_DIR. Tried: ' + ', '.join(places))

_font = TTFont(_find_font())
_glyphs, _cmap, _upm = _font.getGlyphSet(), _font.getBestCmap(), _font['head'].unitsPerEm

# ---- What was drawn, for make_all.py --align: labels (centre, baseline) and boxes (edges), in px
marks = []

# ---- Drawing, in px
def text(s, x, y, size, col):
    marks.append(('label', s, x, y, size))
    return _text(s, x, y, size, col)

def _text(s, x, y, size, col):
    # one path per letter: Rack tells holes from solids by counting crossings with the other outlines
    # of the same path, so letters sharing a path can come out as holes (they looked heavier)
    sc = size * K / _upm
    w = sum(_glyphs[_cmap[ord(c)]].width for c in s) * sc
    X = x * K - w / 2
    Y = y * K
    out = []
    for c in s:
        g = _glyphs[_cmap[ord(c)]]
        p = SVGPathPen(_glyphs)
        g.draw(TransformPen(p, (sc, 0, 0, -sc, X, Y)))
        d = p.getCommands()
        if d:
            out.append(f'<path style="fill:{col};stroke:none" d="{d}"/>')
        X += g.width * sc
    return ''.join(out)

def box(x, y, w, h, col):
    marks.append(('box', x, y, x + w, y + h))
    # rounded by the stroke, like the older panels' boxes. The stroke is 2.2 units = 8.3 px wide,
    # so the visible box is the nominal x..x+w, not smaller
    return (f'<path style="fill:{col};stroke:{col};stroke-width:2.2;stroke-linejoin:round" '
            f'd="M{(x+4)*K:.3f},{(y+4)*K:.3f} h{(w-8)*K:.3f} v{(h-8)*K:.3f} h{-(w-8)*K:.3f} Z"/>')

def jack_box(x, jack_y, w, col, x_right=None):
    # the box around a jack at (x, jack_y), or around a row of them up to x_right
    width = w if x_right is None else x_right - x + w
    return box(x - w / 2, jack_y - BOX_ABOVE, width, BOX_ABOVE + BOX_BELOW, col)

def grid_box(i0, i1, n, k, width_px, col, k_low=None):
    # the box around the jacks of columns i0 to i1 of n on row k, or on rows k down to k_low (theme::jackBox)
    x0, x1 = box_left(i0, n, width_px), box_right(i1, n, width_px)
    y0, y1 = box_top(k), box_bottom(k if k_low is None else k_low)
    return box(x0, y0, x1 - x0, y1 - y0, col)

def jack_label(s, x, jack_y, size=BOX_LABEL_SIZE, col=WHITE, jack_above=None):
    # centred between the box's top edge and the jack's top; in the lower row of a tall box, give the upper row's y as
    # jack_above, and the caption is centred between that jack's bottom and this one's top (theme::captionUnderJack)
    above = LABEL_ABOVE if jack_above is None else caption_under_jack(jack_y - jack_above)
    # the same for every size: 7 and 6.5 would sit under 0.4 px lower, and a row keeps one baseline (THEME.md)
    return text(s, x, jack_y - above, size, col)

def knob_label(s, x, knob_y):
    return text(s, x, knob_y - KNOB_LABEL_ABOVE, KNOB_LABEL_SIZE, INK)

def small_label(s, x, knob_y):
    # above a small knob or a round button
    return text(s, x, knob_y - SMALL_LABEL_ABOVE, BOX_LABEL_SIZE, INK)

def screen(x, y, w, h):
    marks.append(('screen', x, y, x + w, y + h))
    # id="screen" keeps its black in Dark and Black (BidooWidget::prepareThemes)
    return (f'<path id="screen" style="fill:{SCREEN};stroke:{FRAME};stroke-width:0.5" '
            f'd="M{x*K:.3f},{y*K:.3f}h{w*K:.3f}v{h*K:.3f}h{-w*K:.3f}Z"/>')

def logo(W, translate=None):
    # the logo at the bottom, taken from sAiSOn's panel (20 HP = 79.375 units) and moved to the middle of this one
    with open(os.path.join(RES, 'SAISON.svg'), encoding='utf-8') as f:
        s = f.read()
    g = re.search(r'<g\b[^>]*id="text898".*?</g>', s, re.S).group(0)
    if translate is None:
        translate = '%.6f' % (9.921876 + (W - 79.375) / 2)
    return g.replace('translate(9.921876)', f'translate({translate})')

def header(width_px, W, W_text=None):
    w = W_text if W_text is not None else f'{W:.5f}'
    return (f'<?xml version="1.0" encoding="UTF-8" standalone="no"?>\n<svg xmlns="http://www.w3.org/2000/svg" '
            f'width="{width_px:.0f}" height="380" viewBox="0 0 {w} {H}">')

def background(W):
    return f'<path style="fill:{PANEL};stroke:none" d="M0,0H{W}V{H}H0Z"/>'

def title(name, width_px, y=TITLE_Y):
    return text(name, width_px / 2, y, TITLE_SIZE, INK)

# ---- The picture: drawn in panel units, stretched over the whole panel and mangled, in one path at low opacity
class Picture:
    def __init__(self, seed, W):
        self.R = random.Random(seed)
        self.W = W
        self.polys = []
        self.cuts = []

    def add(self, p):
        # all the same way round, so that overlapping shapes don't cancel each other
        a = sum(p[i][0] * p[(i + 1) % len(p)][1] - p[(i + 1) % len(p)][0] * p[i][1] for i in range(len(p)))
        self.polys.append(p if a > 0 else p[::-1])

    def rect(self, x, y, w, h):
        self.add([(x, y), (x + w, y), (x + w, y + h), (x, y + h)])

    def slices(self, h_min, h_max, shifts):
        # cut the panel in horizontal slices that slide sideways. Call once, after the last shape
        y = 0
        while y < H:
            h = self.R.uniform(h_min, h_max)
            self.cuts.append((y, y + h, self.R.choice(shifts)))
            y += h

    def shift(self, y):
        for a, b, s in self.cuts:
            if a <= y < b:
                return s
        return 0

    def path_data(self, warp, step):
        # edges are split every `step` of height, so that the slices and warps show on them; warp is (x, y) -> (x, y)
        d = ''
        for p in self.polys:
            q = []
            for i in range(len(p)):
                a, b = p[i], p[(i + 1) % len(p)]
                m = max(1, int(abs(b[1] - a[1]) / step))
                for k in range(m):
                    X, Y = warp(a[0] + (b[0] - a[0]) * k / m, a[1] + (b[1] - a[1]) * k / m)
                    q.append((min(max(X, 0), self.W), min(max(Y, 0), H)))
            d += 'M' + ' '.join('%.2f,%.2f' % c for c in q) + 'Z'
        return d

    def path(self, warp, step):
        return f'<path id="picture" style="opacity:{PICTURE_OPACITY};fill:#000000" d="{self.path_data(warp, step)}"/>'

# ---- Output
def write(name, svg):
    # a list of lines, or the whole text. Generated panels have no newline at the end
    data = svg if isinstance(svg, str) else '\n'.join(svg)
    with open(os.path.join(out_dir, name), 'w', encoding='utf-8', newline='') as f:
        f.write(data)
