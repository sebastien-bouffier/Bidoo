# Bidoo's plugins for [VCVRack](https://vcvrack.com)

<!-- Version and License Badges -->
![Version](https://img.shields.io/badge/version-0.5.21-green.svg?style=flat-square)
![License](https://img.shields.io/badge/license-BSD3-blue.svg?style=flat-square)
![Language](https://img.shields.io/badge/language-C++-yellow.svg?style=flat-square)

![pack](/images/pack.png?raw=true "pack")

## How to

You can find information on that plugins pack in the [wiki](https://github.com/sebastien-bouffier/Bidoo/wiki). When doing tests it happens that I record a video so you may find some ideas on how to use those modules [here](https://www.youtube.com/bidoo).

## Last changes

0.5.21

TiARE can be switched from OSC mode to LFO via the right click menu.

bordL and dTrOY where loosing gate time and slide params of the current pattern on close. Fixed.

New module still a little "buggy" but usable .... antN. This module can read mp3 streams over http/https. Paste the url in the text field and press LISTEN.
Little/Big endianness is not currently managed but it will be soon. Changing url sometime crashes due to my poor knowledge on threads/libcurl but I hope to fix it asap.

To compile first launch a "make dep" to retrieve mpg123 lib.

## License

The license is a BSD 3-Clause with the addition that any commercial use requires explicit permission of the author. That applies for the source code.

For the image resources (all SVG files), any use requires explicit permission of the author.

## Donate

If you enjoy those modules you can support the development by making a donation. Here's the link: [DONATE](https://paypal.me/sebastienbouffier)
