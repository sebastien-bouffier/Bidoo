# Bidoo's plugins for [VCVRack](https://vcvrack.com)

<!-- Version and License Badges -->
![Version](https://img.shields.io/badge/version-0.6.14-green.svg?style=flat-square)
![License](https://img.shields.io/badge/license-BSD3-blue.svg?style=flat-square)
![Language](https://img.shields.io/badge/language-C++-yellow.svg?style=flat-square)

![pack](/images/pack.png?raw=true "pack")

## How to

You can find information on that plugins pack in the [wiki](https://github.com/sebastien-bouffier/Bidoo/wiki). When doing tests it happens that I record a video so you may find some ideas on how to use those modules [here](https://www.youtube.com/bidoo).

## Last changes

03/09/2018 => 0.6.14

*NEW* GarçOn is still under development. First attempt to use pffft library. Needs to log scale vertical freqs.

31/08/2018 => 0.6.13

*BUGFIX* **dTrOY** and **bordL** crash when the number of steps is greater than 8 => FIXED.

*UPDATE* **ACnE** input channels can be linked in order to create stereo busses for SOLO/MUTE (hit le little lights between SOLO/MUTE). All SOLO and MUTE can be turned off simultaneously by clicking on the buttons in the bottom left corner of the UI.

*UPDATE* **lATe** Swing's CV input has a trim control.

*UPDATE* **OUAIve** Speed and Slices's CV input have trim controls.

*NEW* **MS** Mid/Side decoder/encoder.

Minor changes on plugins to save some CPU.

30/08/2018 => 0.6.12

*REBORN* **dTrOY** and **bordL** have now a gate output on each step that fires on all step's pulses or just on the first pulse (configuration via menu => right click). Pattern management has been optimized to save many CPU cycles. **bordL**'s layout has been revamped to make it less .... "bordéLique".

*NEW* **EMILE** turns your images into sound. *WARNING* : Each line of your image is an oscillator so the taller is the image the higher is the CPU load. F. CURVE changes the way frequencies are distributed vertically. Low freqs are on top and high freqs at the bottom. To load an image => right click. Why **EMILE** ? ... because of "Emile et Images" :) I plan to put in place an auto rescaling feature (image resampling) to limit the height of the images.

Minor changes on all plugins to save some CPU.

24/08/2018 => 0.6.11

*NEW* BIsTrot is a 8bit ADC<=>DAC converter. Do not hesitate to patch bit ports and mangle them. Can be used as a sample rate reducer with the help of clocks, bit reducer, random gate generation etc ...

*REBORN* OUAIve has been rewritten, now it is as robust as cANARd.

Minor changes on all plugins to save some CPU.

## License

The license is a BSD 3-Clause with the addition that any commercial use requires explicit permission of the author. That applies for the source code.

For the image resources (all SVG files), any use requires explicit permission of the author.

## Donate

If you enjoy those modules you can support the development by making a donation. Here's the link: [DONATE](https://paypal.me/sebastienbouffier)
