SLUG = Bidoo
VERSION = 0.5.23

FLAGS += -I./src/dep/include -I./src/dep/audiofile -I./src/dep/filters

include ../../arch.mk


ifeq ($(ARCH), lin)
	LDFLAGS += ../../dep/lib/libcurl.a src/dep/lib/libmpg123.a
endif

ifeq ($(ARCH), mac)
	LDFLAGS += ../../dep/lib/libcurl.a src/dep/lib/libmpg123.a
endif

ifeq ($(ARCH), win)
	LDFLAGS += -lcurl src/dep/lib/libmpg123.a -lshlwapi
endif

SOURCES = $(wildcard src/*.cpp src/dep/audiofile/*cpp src/dep/pffft/*c src/dep/filters/*cpp)

DISTRIBUTABLES += $(wildcard LICENSE*) res

include ../../plugin.mk

dep:
	$(MAKE) -C src/dep

dist: all
	mkdir -p dist/$(SLUG)
	cp LICENSE* dist/$(SLUG)/
	cp $(TARGET) dist/$(SLUG)/
	cp -R res dist/$(SLUG)/
	cd dist && zip -5 -r $(SLUG)-$(VERSION)-$(ARCH).zip $(SLUG)
