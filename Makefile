
SOURCES += $(wildcard src/*.cpp src/dep/audiofile/*cpp)

CFLAGS  += -Isrc/dep/audiofile

FLAGS += \
	-DTEST \
	-Isrc/utils/audiofile

include ../../plugin.mk

dist: all
	mkdir -p dist/Bidoo
	cp LICENSE* dist/Bidoo/
	cp plugin.* dist/Bidoo/
	cp -R res dist/Bidoo/
