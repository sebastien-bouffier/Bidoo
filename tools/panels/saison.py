# The picture behind sAiSOn (20 HP): one tree through the seasons, leaves on the left, bare branches, falling leaves and
# snow on the right. This panel was drawn by hand: only the picture is generated, put into res/SAISON.svg in place of the
# path863 it had. Its labels, and the logo the other panels borrow, stay as they are.
import math, os, re
import panel_kit as k

W = 79.375
SOURCE = os.path.join(k.RES, 'SAISON.svg')

def picture():
    p = k.Picture(16, W)
    R = p.R
    H = k.H
    def limb(x0, y0, x1, y1, w0, w1):
        dx, dy = x1 - x0, y1 - y0
        L = math.hypot(dx, dy) or 1
        nx, ny = -dy / L, dx / L
        n = max(2, int(L / 2))
        left, right = [], []
        for i in range(n + 1):
            t = i / n
            w = w0 + (w1 - w0) * t
            j = R.uniform(-.14, .14)
            x, y = x0 + dx * t, y0 + dy * t
            left.append((x + nx * (w / 2 + j), y + ny * (w / 2 + j)))
            right.append((x - nx * (w / 2 - j), y - ny * (w / 2 - j)))
        p.add(left + right[::-1])
    def blob(cx, cy, r, n=11, rag=.3, sx=1.0):
        p.add([(cx + sx * r * (1 + R.uniform(-rag, rag)) * math.cos(2 * math.pi * i / n),
                cy + r * (1 + R.uniform(-rag, rag)) * math.sin(2 * math.pi * i / n)) for i in range(n)])
    tips = []
    def grow(x, y, ang, L, w, d):
        x1, y1 = x + L * math.cos(ang), y + L * math.sin(ang)
        limb(x, y, x1, y1, w, w * .68)
        if d == 0 or L < 1.6:
            tips.append((x1, y1))
            return
        for sp in ([-.55, .42] if d % 2 else [-.38, .6]):
            grow(x1, y1, ang + sp + R.uniform(-.14, .14), L * R.uniform(.68, .8), w * .68, d - 1)
    cx = W * .5
    limb(cx - 1.2, H + 1, cx - .3, H * .66, 8.5, 5.6)
    grow(cx - .3, H * .66, -math.pi / 2, 15.5, 5.6, 6)
    for a, l in ((-2.5, 9), (-.65, 9)):
        limb(cx - 1, H, cx + math.cos(a) * l, H + math.sin(a) * 2, 2.4, .5)
    # left, spring and summer: leaves on the branch tips
    for x, y in tips:
        if x < cx - 1:
            for j in range(3):
                blob(x + R.uniform(-2.6, 2.6), y + R.uniform(-2.6, 2.6), R.uniform(1.1, 2.5))
    # right, autumn and winter: bare tips, leaves falling, then snow on the ground
    for j in range(15):
        x = R.uniform(cx + 5, W - 2)
        y = R.uniform(H * .3, H * .9)
        a = R.uniform(0, 6.28)
        r = R.uniform(.9, 1.55)
        p.add([(x + r * 1.7 * math.cos(a), y + r * 1.7 * math.sin(a)), (x + r * .6 * math.cos(a + 1.57), y + r * .6 * math.sin(a + 1.57)),
               (x - r * 1.7 * math.cos(a), y - r * 1.7 * math.sin(a)), (x + r * .6 * math.cos(a - 1.57), y + r * .6 * math.sin(a - 1.57))])
    for j in range(38):
        blob(R.uniform(cx + 2, W - 1), R.uniform(4, H * .82), R.uniform(.22, .5), n=6, rag=.2)
    p.add([(cx + 1, H)] + [(cx + 1 + i * (W - cx) / 12, H - 3.5 - R.uniform(0, 1.6) - 1.5 * math.sin(i / 12 * math.pi)) for i in range(13)] + [(W, H)])
    p.slices(1.5, 8.5, [0, 0, 1.2, -1.6, 2.8, -3.4, 4.6])
    return p.path_data(lambda x, y: (x + p.shift(y), y), 1.5)

def build(source=SOURCE):
    with open(source, encoding='utf-8', newline='') as f:
        s = f.read()
    # the second half of the picture in the panel it replaced; gone once this has run
    s = re.sub(r'<path transform="translate\(39\.6875,0\)"\s+inkscape:connector-curvature="0"\s+id="path863-b".*?/>\s*', '', s, count=1, flags=re.S)
    d = picture()
    s, n = re.subn(r'(id="path863"\s+d=")[^"]*(")', lambda m: m.group(1) + d + m.group(2), s, count=1)
    assert n == 1
    return s

if __name__ == '__main__':
    k.write('SAISON.svg', build())
