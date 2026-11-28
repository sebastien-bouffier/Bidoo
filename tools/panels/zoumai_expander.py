# zOù MAï's expander (17 HP): the panel of zouseq_panel.py (expander) with its title and its old picture
# (zoumai_expander_parts.svg). enCORE's (encore_expander.py) is the same with its own. The widget is
# src/ZouSeqExpander.hpp (ZouSeqExpanderWidget).
import panel_kit as k
import zouseq_panel

def build():
    return zouseq_panel.expander('zOù MAï +', 'zoumai_expander_parts.svg')

if __name__ == '__main__':
    k.write('ZOUMAIExpander.svg', build())
