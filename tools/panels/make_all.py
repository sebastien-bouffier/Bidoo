# Regenerates the generated panels in res/ from src/BidooTheme.hpp.
#   python tools/panels/make_all.py            write them
#   python tools/panels/make_all.py --check    write nothing; say which differ from res/, exit 1 if any
#   python tools/panels/make_all.py --out DIR  write them to DIR instead of res/
#   python tools/panels/make_all.py --align    write nothing; list near-misses (docs/THEME.md, "Alignment")
import os, sys
sys.dont_write_bytecode = True
import panel_kit as k
import acide, copia, dose, filtre, particules, registre, ricochet, saison, sounaire

PANELS = (('SAISON.svg', saison), ('ACIDE.svg', acide), ('FILTRE.svg', filtre), ('COPIA.svg', copia), ('SOUNAIRE.svg', sounaire), ('RICOCHET.svg', ricochet), ('DOSE.svg', dose), ('REGISTRE.svg', registre), ('PARTICULES.svg', particules))

NEAR = (0.05, 6.0)   # closer than this is the same line; farther is a different one

def near_misses(marks):
    # labels: centres stacked in a column, baselines of a row; boxes and the screen: edges in a column or a row
    out = []
    labels = [m for m in marks if m[0] == 'label' and m[4] < 20]   # not the title
    rects = [m for m in marks if m[0] in ('box', 'screen')]
    def close(a, b):
        return NEAR[0] < abs(a - b) <= NEAR[1]
    for i, a in enumerate(labels):
        for b in labels[i + 1:]:
            if close(a[2], b[2]):
                out.append(f'label x  {a[1]!r} {a[2]:.2f} / {b[1]!r} {b[2]:.2f}')
            if close(a[3], b[3]):
                out.append(f'baseline {a[1]!r} {a[3]:.2f} / {b[1]!r} {b[3]:.2f}')
    names = ('left', 'top', 'right', 'bottom')
    for i, a in enumerate(rects):
        for b in rects[i + 1:]:
            for e in range(4):
                if close(a[1 + e], b[1 + e]):
                    out.append(f'{names[e]:6s}   {a[0]} ({a[1]:.1f},{a[2]:.1f}) {a[1+e]:.2f} / {b[0]} ({b[1]:.1f},{b[2]:.1f}) {b[1+e]:.2f}')
    return out

def main(args):
    if '--out' in args:
        k.out_dir = args[args.index('--out') + 1]
        os.makedirs(k.out_dir, exist_ok=True)
    differ = 0
    for name, module in PANELS:
        k.marks.clear()
        svg = module.build()
        if '--align' in args:
            found = near_misses(k.marks)
            print(f'{name}: {len(found)} near-miss' + ('es' if len(found) != 1 else ''))
            for line in found:
                print('   ', line)
            continue
        svg = svg if isinstance(svg, str) else '\n'.join(svg)
        if '--check' in args:
            with open(os.path.join(k.RES, name), encoding='utf-8', newline='') as f:
                same = f.read() == svg
            differ += not same
            print(('same    ' if same else 'DIFFERS ') + name)
        else:
            k.write(name, svg)
            print('wrote', os.path.join(k.out_dir, name))
    return 1 if differ else 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
