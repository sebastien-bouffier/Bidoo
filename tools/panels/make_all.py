# Regenerates the generated panels in res/ from src/BidooTheme.hpp.
#   python tools/panels/make_all.py            write them
#   python tools/panels/make_all.py --check    write nothing; say which differ from res/, exit 1 if any
#   python tools/panels/make_all.py --out DIR  write them to DIR instead of res/
import os, sys
sys.dont_write_bytecode = True
import panel_kit as k
import acide, copia, filtre, ricochet, saison, sounaire

PANELS = (('SAISON.svg', saison), ('ACIDE.svg', acide), ('FILTRE.svg', filtre), ('COPIA.svg', copia), ('SOUNAIRE.svg', sounaire), ('RICOCHET.svg', ricochet))

def main(args):
    if '--out' in args:
        k.out_dir = args[args.index('--out') + 1]
        os.makedirs(k.out_dir, exist_ok=True)
    differ = 0
    for name, module in PANELS:
        svg = module.build()
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
