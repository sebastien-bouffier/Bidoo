==============================================================================

        Resampler - An audio resampling library in C++
        v1.02

        Copyright (c) 2003-2004 Laurent de Soras

==============================================================================



Contents:

1. Legal
2. What is Resampler ?
3. Using Resampler
4. Compilation
5. History
6. Contact



1. Legal
--------

This library is free software; you can redistribute it and/or
modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either
version 2.1 of the License, or (at your option) any later version.

This library is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public
License along with this library; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

Check the file license.txt to get full information about the license.



2. What is Resampler ?
----------------------

Resampler is a library for digital audio resampling, aka "sampling rate
conversion". Its primary goals are the following ones:

    - Very high quality and low aliasing
    - Variable resampling rate
    - Easy integration into software samplers and synthesisers
    - Real-time, low-latency resampling of pre-calculated data
    - Low and predictible CPU load
    - Low memory requirement.

Of course CPU load is not as low as cheap polynomial interpolation, but the
algorithm remains CPU-friendly regarding the achieved sound quality. Moreover,
sampler performance limitations generally come from the computer's memory
bandwidth. So it's probably a good idea to spend those lost clock cycles in
high-quality resampling...

The specs:

    - Effective bandwidth up to 90 % of Nyquist frequency
    - Aliasing rejection below -85 dB
    - 0.1 dB ripples in the passband

It is an illustration of the method described in the article
"The Quest For The Perfect Resampler", Laurent de Soras, June 2003.

Both source code and article may be downloaded from this webpage:
http://ldesoras.free.fr/prod.html



3. Using Resampler
------------------

To avoid any collision, names have been encapsulated in the namespace "rspl".
So you have to prefix every name of this library with rspl:: or put a line
"using namespace rspl;" into your code.

The main object is ResamplerFlt. It handles the concept of monophonic sound
generation, or voice. Before functionning, it requires to be connected to two
other objects, instances of MipMapFlt and InterpPack. Both can be shared
between many ResamplerFlt instances.

MipMapFlt stores sample data and handles MIP-map precalculations. Total memory
occupation won't exceed about two times the original data amount. Once
ResamplerFlt is set up, you reconnect other MipMapFlt objects on the fly. It
allows you to change the sample associated to a given voice.

InterpPack handles internal stateless operations and precalculated filters.
Generally you'll need only one instance of it per program.

For more information, check main.cpp. It contains two complete example of
sample interpolation, generating 16-bit/mono/44.1 kHz raw sample files. Don't
hesitate to dig the code documentation.

The library processes only 32-bit floating point data. I have done experiments
with 16-bit MMX instruction set (not included), it becomes very fast but with
a small penalty on quality because data is quantified to ~14 bits at some
stages.

Resampler is intended to be portable, but has little architecture-dependant
pieces of code. So far, it has been built and tested on :
     - MS Windows / MS Visual C++ 6.0
     - MS Windows / MS Visual C++ Toolkit 2003
     - MS Windows / GCC 3.3
     - MacOS 10.3 / GCC 3.3
     - MacOS 10.3 / Code Warrior 8
If you happen to have another system and tweak it
to make it run successfully, pleeeeease send me your modification so I can
include it to the main distribution. Run main.cpp in Debug mode before then
in Release mode, in order to be sure that everything is fine.



4. Compilation
--------------

Drop all the .h, .cpp and .hpp files into your project or makefile.

main.cpp and StopWatch.* are for testing purpose only, do not include them if
you just need to use the library.

Resampler may be compiled in two versions: release and debug. Debug version
has checks that could slow down the code. Define NDEBUG to set the release
mode. For example, the command line for GCC would look like:

Debug mode:
g++ -Wall -o resampler_debug.exe *.cpp

Release mode:
g++ -Wall -o resampler_release.exe -DNDEBUG -O3 *.cpp



5. History
----------

v1.02 (2004.08.25)
    - Fixed multiple definition error with GCC 3.3 and above

v1.01 (2003.06.23)
    - Support for GCC 3 / Cygwin.
    - Support for Code Warrior / MacOS.
    - Fixed std::runtime_error problem in the test suite.
    - Fixed private member problem for ResamplerFlt::_fir_mip_map_coef_arr.

v1.00 (2003.06.21)
    - Initial release, MS Windows port.



6. Contact
----------

Please address any comment, bug report or flame to:

Laurent de Soras
laurent@ohmforce.com
http://ldesoras.free.fr

Thanks to:
- Raphael Dinge for porting Resampler to MacOS.
- Richard Dobson for spotting some problems with GCC

