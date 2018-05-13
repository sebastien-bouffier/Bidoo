RACK_DIR ?= ../..
SLUG = Bidoo
VERSION = 0.6.6
DISTRIBUTABLES += $(wildcard LICENSE*) res

FLAGS += -DUSE_KISS_FFT -Idep/include -I./src/dep/audiofile -I./src/dep/filters -I./src/dep/freeverb \
 -I./src/dep/gist/libs/kiss_fft130 -I./src/dep/gist/src -I./src/dep/minimp3\
 -I./src/dep/gist/src/mfcc -I./src/dep/gist/src/core -I./src/dep/gist/src/fft \
 -I./src/dep/gist/src/onset-detection-functions -I./src/dep/gist/src/pitch

include $(RACK_DIR)/arch.mk

ifeq ($(ARCH), lin)
	LDFLAGS += -L$(RACK_DIR)/dep/lib $(RACK_DIR)/dep/lib/libcurl.a
endif

ifeq ($(ARCH), mac)
	LDFLAGS += -L$(RACK_DIR)/dep/lib $(RACK_DIR)/dep/lib/libcurl.a
endif

ifeq ($(ARCH), win)
	LDFLAGS += -L$(RACK_DIR)/dep/lib -lcurl
endif

SOURCES = $(wildcard src/*.cpp src/dep/audiofile/*cpp src/dep/filters/*cpp src/dep/freeverb/*cpp src/dep/gist/src/*cpp \
 src/dep/gist/libs/kiss_fft130/*c src/dep/gist/src/mfcc/*cpp src/dep/gist/src/core/*cpp src/dep/gist/src/fft/*cpp \
 src/dep/gist/src/onset-detection-functions/*cpp src/dep/gist/src/pitch/*cpp)

include $(RACK_DIR)/plugin.mk
