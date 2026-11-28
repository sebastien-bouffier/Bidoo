# zOù MAï's panel (31 HP): the ZouSeq sequencer's panel of zouseq_panel.py with its title and its old picture
# (zoumai_parts.svg). enCORE's (encore.py) is the same with its own. The widget is src/ZouSeq.hpp (ZouSeqWidget).
import panel_kit as k
import zouseq_panel

def build():
    return zouseq_panel.sequencer('zOù MAï', 'zoumai_parts.svg')

if __name__ == '__main__':
    k.write('ZOUMAI.svg', build())
