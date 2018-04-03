RACK_DIR ?= ../..
SLUG = Bidoo
VERSION = 0.6.1
DISTRIBUTABLES += $(wildcard LICENSE*) res

FLAGS += -Idep/include -I./src/dep/audiofile -I./src/dep/filters -I./src/dep/freeverb

include $(RACK_DIR)/arch.mk

ifeq ($(ARCH), lin)
	LDFLAGS += -L$(RACK_DIR)/dep/lib $(RACK_DIR)/dep/lib/libcurl.a dep/lib/libmpg123.a
endif

ifeq ($(ARCH), mac)
	LDFLAGS += -L$(RACK_DIR)/dep/lib $(RACK_DIR)/dep/lib/libcurl.a dep/lib/libmpg123.a
endif

ifeq ($(ARCH), win)
	LDFLAGS += -L$(RACK_DIR)/dep/lib -lglfw3dll -lcurl dep/lib/libmpg123.a -lshlwapi
endif

SOURCES = $(wildcard src/*.cpp src/dep/audiofile/*cpp src/dep/pffft/*c src/dep/filters/*cpp src/dep/freeverb/*cpp)

# Static libs
mpg123 := dep/lib/libmpg123.a
OBJECTS += $(mpg123)

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

include $(RACK_DIR)/plugin.mk
