# bidoo-dsp (excerpt)

Headers copied unchanged from the author's bidoo-dsp library:

- commit 702faf7, for REI: `bidoo/dsp/reverb/Prism.h` (PrismReverb, the reverb and shimmer of FadaBox's Prism),
  with `bidoo/dsp/core/Random.h`, `bidoo/dsp/core/Inline.h` and `bidoo/dsp/shaping/TanhPade.h` (also bAFIs, lIMbO)
- commit 2f039ad, for cOPIA: `bidoo/dsp/effects/glitch/` (the glitch engine of the Mangle plugin: effects,
  pattern, slots, scenes' engine; built with BIDOO_DSP_GLITCH_LITE), `bidoo/dsp/time/PulseClock.h`, and what
  they include (core, delay, filters, oversampling, reverb, shaping, synth, time)
- commit 2f039ad, for FiLTRe: `bidoo/dsp/filters/VoiceFilters.h` (the voice filter families of Potion's filter,
  `../potion/Filter.h`), with the filters and shaping headers above

Copyright (c) 2026 Sebastien Bouffier (Bidoo). Distributed here under the GPL-3.0-or-later, as the
rest of this plugin.
