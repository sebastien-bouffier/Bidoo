# Bidoo's plugins for [VCVRack](https://vcvrack.com)

<!-- Version and License Badges -->
![Version](https://img.shields.io/badge/version-0.5.19-green.svg?style=flat-square)
![License](https://img.shields.io/badge/license-BSD3-blue.svg?style=flat-square)
![Language](https://img.shields.io/badge/language-C++-yellow.svg?style=flat-square)

![pack](/images/pack.png?raw=true "pack")

## How to

You can find information on that plugins pack in the [wiki](https://github.com/sebastien-bouffier/Bidoo/wiki). When doing tests it happens that I record a video so you may find some ideas on how to use those modules [here](https://www.youtube.com/bidoo).

## Last changes

0.5.19

lIMbO the 4th order ladder filter has a second mode i.e. "non linear". In that mode the GAIN control is no more a gain on the output but a gain on the tanh() that introduces non linearity.

pErCO is a step in my journey across filters. To achieve the next plugin I needed a BP filter so I tried this approach. The result is a very simple 2nd order LP, BP, HP filter.

ziNC is the plugin I wanted to do with filters. It is a 16 band vocoder. I started this plugin with pErCO's BP but I needed more selective filters so I finally used 4 poles BP.
If you are interested in filter design and more generally in dsp I recommend that website http://www.earlevel.com.
Controls are gains for the 16 bands. Attack and Decay for the envelop follower and gain stages for MODulator, CARRier and OUTput.


## License

The license is a BSD 3-Clause with the addition that any commercial use requires explicit permission of the author. That applies for the source code.

For the image resources (all SVG files), any use requires explicit permission of the author.

## Donate

If you enjoy those modules you can support the development by making a donation. Here's the link: [DONATE](https://paypal.me/sebastienbouffier)
