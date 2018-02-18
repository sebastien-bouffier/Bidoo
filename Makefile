SLUG = Bidoo
VERSION = 0.5.21

FLAGS += -I./pffft -DPFFFT_SIMD_DISABLE
FLAGS += -I./src/dep/include

include ../../arch.mk

ifeq ($(ARCH), lin)
	LDFLAGS += -rdynamic -L./src/dep/lib -L../../dep/lib -lmpg123 -lcurl
endif

ifeq ($(ARCH), mac)
	LDFLAGS += -L./src/dep/lib -lmpg123 -lcurl
endif

ifeq ($(ARCH), win)
	LDFLAGS += -L./src/dep/lib -lmpg123 -lshlwapi -lcurl
endif

SOURCES = $(wildcard src/*.cpp src/dep/audiofile/*cpp src/dep/pffft/*c src/dep/filters/*cpp)

DISTRIBUTABLES += $(wildcard LICENSE*) res

include ../../plugin.mk

dep:
	$(MAKE) -C src/dep
