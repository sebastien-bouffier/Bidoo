SLUG = Bidoo
VERSION = 0.6.0
DISTRIBUTABLES += $(wildcard LICENSE*) res
RACK_DIR ?= ../..

FLAGS += -I./src/dep/include -I./src/dep/audiofile -I./src/dep/filters

include $(RACK_DIR)/arch.mk

ifeq ($(ARCH), lin)
	LDFLAGS += -L$(RACK_DIR)/dep/lib -lglfw $(RACK_DIR)/dep/lib/libcurl.a src/dep/lib/libmpg123.a
endif

ifeq ($(ARCH), mac)
	LDFLAGS += -L$(RACK_DIR)/dep/lib -lglfw $(RACK_DIR)/dep/lib/libcurl.a src/dep/lib/libmpg123.a
endif

ifeq ($(ARCH), win)
	LDFLAGS += -L$(RACK_DIR)/dep/lib -lglfw3dll -lcurl src/dep/lib/libmpg123.a -lshlwapi
endif

SOURCES = $(wildcard src/*.cpp src/dep/audiofile/*cpp src/dep/pffft/*c src/dep/filters/*cpp)

include $(RACK_DIR)/plugin.mk

dep:
	$(MAKE) -C src/dep
