# bidoo-dsp (excerpt)

Headers copied unchanged from the author's bidoo-dsp library. Every header here is identical to the
library's file at commit e8d3e02 (2026-11-26): no local edits. Brought up to date by bidoo-dsp's
`tools/sync-vcv.py` (the headers the modules include, followed through their includes; `--check` lists
any difference).

- REI: `bidoo/dsp/reverb/Prism.h` (PrismReverb, the reverb and shimmer of FadaBox's Prism)
- PARTICULES: `bidoo/dsp/delay/GranularPitchShifter.h`, `bidoo/dsp/reverb/FdnReverb.h`, `bidoo/dsp/time/PulseClock.h`
- fLAME: `bidoo/dsp/filters/Filters.h`
- cOPIA: `bidoo/dsp/effects/glitch/` (the glitch engine of the Mangle plugin, built with BIDOO_DSP_GLITCH_LITE),
  `bidoo/dsp/time/PulseClock.h`, and the reverb, delay, filter and core headers it includes
- FiLTRe: `bidoo/dsp/filters/VoiceFilters.h` (the voice filter families of Potion's filter, `../potion/Filter.h`),
  with `filters/TptSvf.h`, `shaping/AdaaTanhTable.h` and `shaping/TanhPade.h`
- zOù MAï and enCORE: `bidoo/dsp/time/Euclid.h` (the euclidean fill)
- aCiDe: `bidoo/dsp/filters/DiodeLadder.h` and `bidoo/dsp/synth/AcidVoice.h`, with `synth/KaiserBlep.h`,
  `oversampling/IirHalfBand.h` and `shaping/TanhPade.h`
- rICOcHEt: `bidoo/dsp/delay/CharacterDelay.h` and `bidoo/dsp/delay/WowFlutter.h` (`core/Trig.h`: fast sines on MinGW)
- bAFIs, lIMbO: `bidoo/dsp/shaping/TanhPade.h`, `bidoo/dsp/core/Random.h`, `bidoo/dsp/core/Inline.h`
- baR: `bidoo/dsp/dynamics/Dynamics.h` (SlidingMax, the peak catcher's lookahead maximum)

Copyright (c) 2026 Sebastien Bouffier (Bidoo). Distributed here under the GPL-3.0-or-later, as the
rest of this plugin.
