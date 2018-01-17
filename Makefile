SLUG = Bidoo
VERSION = 0.5.17

SOURCES = $(wildcard src/*.cpp src/dep/audiofile/*cpp)

DISTRIBUTABLES += $(wildcard LICENSE*) res

include ../../plugin.mk
