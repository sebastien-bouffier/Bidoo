# Bidoo's plugins for [VCVRack](https://vcvrack.com)

<!-- Version and License Badges -->
![Version](https://img.shields.io/badge/version-0.5.12-green.svg?style=flat-square)
![License](https://img.shields.io/badge/license-BSD3-blue.svg?style=flat-square)
![Language](https://img.shields.io/badge/language-C++-yellow.svg?style=flat-square)

![pack](/images/pack.png?raw=true "pack")

## How to

You can find information on that plugins pack in the [wiki](https://github.com/sebastien-bouffier/Bidoo/wiki). When doing tests it happens that I record a video so you may find some ideas on how to use those modules [here](https://www.youtube.com/bidoo).

## Last changes

Fix clock reset issue. Change flashing blue for a more chill blue on controls. Fix root note and scale saving bug.

dTrOY has a new sensitivity control (S trimpot) to attenuate pitch buttons' range. dTrOY has, finally, 16 patterns in memory. Blue controls are saved in the pattern selected by the dedicated knob (the pattern number is white). If the pattern input is not active the pattern which is played is the same one as the one you selected (the pattern number is red). If the input is active then dTrOY plays the pattern selected by the input, you can still edit this pattern or another one at run time by selecting the target pattern with the knob (red and white pattern numbers may differ, white is current edit, red is played). Light pulses on Slide/Skip leds always follow the played pattern. With this version changing pattern resets the position to the first step. Maybe at some point I will add alternative switch modes.

## License

The license is a BSD 3-Clause with the addition that any commercial use requires explicit permission of the author. That applies for the source code.

For the image resources (all SVG files), any use requires explicit permission of the author.

## Donate

If you enjoy those modules you can support the development by making a donation. Here's the link: [DONATE](https://paypal.me/sebastienbouffier)
