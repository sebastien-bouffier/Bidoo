#pragma once

// A stand-in for Potion's src/dsp/Params.h: only what Filter.h reads. The enum, its order, kNumFilterTypes
// and usesMorph are copied unchanged from Potion 74f83d5 (presets store the index there).

namespace potion
{
enum class FilterType { Low12, Low24, High12, High24, Band12, Band24, Notch12, Notch24, Peak, Ladder24, CombPlus, CombMinus,
                        Morph12, Morph24, Formant, Phaser, Flanger,
                        // 0.56: new types go at the end (presets store the index)
                        Diode24, Ota24, SkLow12, SkHigh12, BandReject, LowShelf, HighShelf, DistComb, Diffuser, Downsample,
                        // 0.59: the bidoo-dsp voice filter families (filters/VoiceFilters.h)
                        Low6, Low18, High6, High18, Peak24,
                        DualLH, DualLB, DualLP, DualLN, DualHB, DualHP, DualHN, DualBP, DualPP, DualPN, DualNN,
                        MorphLph12, MorphLph24, MorphLnh12, MorphLnh24, MorphBpn12, MorphBpn24,
                        CombLowPlus, CombLowMinus, CombHighPlus, CombHighMinus, CombBandPlus, CombBandMinus,
                        Phaser4Plus, Phaser4Minus, Phaser8Plus, Phaser8Minus, Phaser16Plus, Phaser16Minus,
                        BandEq, RingMod, ChordComb, Reverb, AddBass, ScreamLow, ScreamBand, Cmos12, DjFilter, FormantF };
constexpr int kNumFilterTypes = (int) FilterType::FormantF + 1;
// Whether the filter type uses the Morph setting (the 0.59 slopes have none).
constexpr bool usesMorph (FilterType t) { return t >= FilterType::Morph12 && ! (t >= FilterType::Low6 && t <= FilterType::Peak24); }
} // namespace potion
