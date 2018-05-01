RACK_DIR ?= ../..
SLUG = Bidoo
VERSION = 0.6.3
DISTRIBUTABLES += $(wildcard LICENSE*) res

# Static libs
mpg123 := dep/lib/libmpg123.a
# OBJECTS += $(mpg123)

# Dependencies
$(shell mkdir -p dep)
DEP_LOCAL := dep
DEPS += $(mpg123)

$(mpg123):
	cd dep && $(WGET) https://www.mpg123.de/download/mpg123-1.25.8.tar.bz2
	cd dep && $(UNTAR) mpg123-1.25.8.tar.bz2
	cd dep/mpg123-1.25.8 && ./configure --prefix="$(realpath $(DEP_LOCAL))" --with-cpu=generic --with-pic --disable-shared --enable-static
	cd dep/mpg123-1.25.8 && $(MAKE)
	cd dep/mpg123-1.25.8 && $(MAKE) install

FLAGS += -DUSE_KISS_FFT -Idep/include -I./src/dep/audiofile -I./src/dep/filters -I./src/dep/freeverb -I./src/dep/gist/libs/kiss_fft130 -I./src/dep/gist/src \
 -I./src/dep/gist/src/mfcc -I./src/dep/gist/src/core -I./src/dep/gist/src/fft \
 -I./src/dep/gist/src/onset-detection-functions -I./src/dep/gist/src/pitch

include $(RACK_DIR)/arch.mk

ifeq ($(ARCH), lin)
	LDFLAGS += -L$(RACK_DIR)/dep/lib $(RACK_DIR)/dep/lib/libcurl.a dep/lib/libmpg123.a
endif

ifeq ($(ARCH), mac)
	LDFLAGS += -L$(RACK_DIR)/dep/lib $(RACK_DIR)/dep/lib/libcurl.a dep/lib/libmpg123.a
endif

ifeq ($(ARCH), win)
	LDFLAGS += -L$(RACK_DIR)/dep/lib -lcurl dep/lib/libmpg123.a -lshlwapi
endif

SOURCES = $(wildcard src/*.cpp src/dep/audiofile/*cpp src/dep/filters/*cpp src/dep/freeverb/*cpp src/dep/gist/src/*cpp \
 src/dep/gist/libs/kiss_fft130/*c src/dep/gist/src/mfcc/*cpp src/dep/gist/src/core/*cpp src/dep/gist/src/fft/*cpp \
 src/dep/gist/src/onset-detection-functions/*cpp src/dep/gist/src/pitch/*cpp)

include $(RACK_DIR)/plugin.mk
