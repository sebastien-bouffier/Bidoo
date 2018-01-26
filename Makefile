SLUG = Bidoo
VERSION = 0.5.18

FLAGS = -I./pffft -DPFFFT_SIMD_DISABLE

SOURCES = $(wildcard src/*.cpp src/dep/audiofile/*cpp src/dep/pffft/*c)

DISTRIBUTABLES += $(wildcard LICENSE*) res

include ../../plugin.mk
