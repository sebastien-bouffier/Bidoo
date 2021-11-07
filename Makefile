RACK_DIR ?= ../..

DISTRIBUTABLES += $(wildcard LICENSE*) res

FLAGS += -Idep/include -I./src/dep/dr_wav -I./src/dep/filters -I./src/dep/freeverb -I./src/dep/gverb/include -I./src/dep/minimp3 -I./src/dep/lodepng -I./src/dep/pffft -I./src/dep/AudioFile -I./src/dep/resampler -I./src/dep

SOURCES = $(wildcard src/*.cpp src/dep/filters/*.cpp src/dep/freeverb/*.cpp src/dep/gverb/src/*.c src/dep/lodepng/*.cpp src/dep/pffft/*.c src/dep/resampler/*.cpp src/dep/*.cpp)

include $(RACK_DIR)/plugin.mk
