RACK_DIR ?= ../..

DISTRIBUTABLES += $(wildcard LICENSE*) res

FLAGS += -Idep/include -I./src/dep/dr_wav -I./src/dep/filters -I./src/dep/gverb/include -I./src/dep/minimp3 -I./src/dep/lodepng -I./src/dep/pffft -I./src/dep/signalsmith -I./src/dep/AudioFile -I./src/dep/resampler -I./src/dep

SOURCES = $(wildcard src/*.cpp src/dep/filters/*.cpp src/dep/gverb/src/*.c src/dep/lodepng/*.cpp src/dep/pffft/*.c src/dep/resampler/*.cpp src/dep/*.cpp src/dep/faad2/libfaad/*.c) src/dep/stb_vorbis/stb_vorbis_impl.c
SOURCES := $(filter-out src/dep/resampler/main.cpp src/dep/resampler/StopWatch.cpp src/dep/pffft/test_pffft.c src/dep/lodepng/pngdetail.cpp src/dep/lodepng/lodepng_util.cpp, $(SOURCES))

include $(RACK_DIR)/plugin.mk

# FAAD2 (antN's AAC decoder): its own config header and include paths, IEEE-exact maths,
# upstream code left as is (its warnings muted, its cfft* renamed: PFFFT's fftpack has the same names)
build/src/dep/faad2/%: CFLAGS += -DHAVE_CONFIG_H -Isrc/dep/faad2/include -Isrc/dep/faad2/libfaad -fno-unsafe-math-optimizations -w \
	-Dcfftf=faad_cfftf -Dcfftb=faad_cfftb -Dcffti=faad_cffti -Dcfftu=faad_cfftu \
	'-D__declspec(x)='
# (neaacdec.h marks its API dllexport on Windows: with one explicit export the linker stops
# exporting everything else, init() included, and Rack cannot load the plugin)
# stb_vorbis (antN's Ogg Vorbis decoder)
build/src/dep/stb_vorbis/%: CFLAGS += -fno-unsafe-math-optimizations -w

# AudioFile 1.1.x needs C++17
CXXFLAGS := $(filter-out -std=c++11,$(CXXFLAGS)) -std=c++17

# FiLTRe: Potion's Filter.h, kept unchanged, needs C++20; only this file sees it
build/src/FiltreEngine.cpp.o: CXXFLAGS += -std=c++20 -Isrc/dep/bidoo-dsp

# rICOcHEt: bidoo-dsp headers (delay/WowFlutter.h) include from the library root
build/src/RICOCHET.cpp.o: CXXFLAGS += -Isrc/dep/bidoo-dsp
