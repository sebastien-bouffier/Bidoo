# Bidoo's plugins for [VCVRack](https://vcvrack.com)

<!-- Version and License Badges -->
![Version](https://img.shields.io/badge/version-0.5.16-green.svg?style=flat-square)
![License](https://img.shields.io/badge/license-BSD3-blue.svg?style=flat-square)
![Language](https://img.shields.io/badge/language-C++-yellow.svg?style=flat-square)

![pack](/images/pack.png?raw=true "pack")

## How to

You can find information on that plugins pack in the [wiki](https://github.com/sebastien-bouffier/Bidoo/wiki). When doing tests it happens that I record a video so you may find some ideas on how to use those modules [here](https://www.youtube.com/bidoo).

## Last changes

0.5.17

OUAIve goes stereo and has minor fixes regarding player type and play mode information display.

baR is a new module. It is a simple stereo compressor activated when the "peak" (not RMS) signal overshoots the threshold. Is has two pairs of inputs, IN for the compressed signal and SC for the side chain one.
In terms of controls, nothing really new under the sun, T stands for threshold, R for ratio, Att and Rel for attack and release time, K for knee width, G for makeup gain, M for mix and L for lookahead (% of attack time).
By default values are set to make subtle compression except T and R (no compression by default) but you can go to mad values ... up to you.
Two pairs of bars display 10ms RMS and 300ms RMS of side chain signal, if side chain not connected they display input signal values.
WARNING be careful with makeup gain, it can be loud.
Many little things still need improvements (RMS calc vs sampling freq, lookahead and att glitches when changing ..) but it already compresses as it is.

Global GUI reworking to homogenize input and output ports design. Use of new set of colors and in particular for text displays in order to increase contrast.

## License

The license is a BSD 3-Clause with the addition that any commercial use requires explicit permission of the author. That applies for the source code.

For the image resources (all SVG files), any use requires explicit permission of the author.

## Donate

If you enjoy those modules you can support the development by making a donation. Here's the link: [DONATE](https://paypal.me/sebastienbouffier)
