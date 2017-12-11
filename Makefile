
SOURCES += $(wildcard src/*.cpp src/utils/audiofile/*cpp src/utils/ffft/*cpp src/utils/lodepng/*cpp src/utils/resampler/*cpp)

CFLAGS  += -Isrc/utils/audiofile
CFLAGS  += -Isrc/utils/ffft
CFLAGS  += -Isrc/utils/lodepng
CFLAGS  += -Isrc/utils/resampler

FLAGS += \
	-DTEST \
	-Isrc/utils/audiofile

FLAGS += \
	-DTEST \
	-Isrc/utils/ffft

FLAGS += \
	-DTEST \
	-Isrc/utils/lodepng

FLAGS += \
	-DTEST \
	-Isrc/utils/resampler

include ../../plugin.mk

dist: all
	mkdir -p dist/Bidoo
	cp LICENSE* dist/Bidoo/
	cp plugin.* dist/Bidoo/
	cp -R res dist/Bidoo/
