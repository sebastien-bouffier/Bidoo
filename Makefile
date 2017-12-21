SLUG = Bidoo
VERSION = 0.5.8

CFLAGS  += -Isrc/dep/audiofile
CXXFLAGS +=
FLAGS += \
	-DTEST \
	-Isrc/utils/audiofile

LDFLAGS +=

SOURCES += $(wildcard src/*.cpp src/dep/audiofile/*cpp)

include ../../plugin.mk

.PHONY: dist
dist: all
	mkdir -p dist/$(SLUG)
	cp LICENSE* dist/$(SLUG)/
	cp $(TARGET) dist/$(SLUG)/
	cp -R res dist/$(SLUG)/
	cd dist && zip -5 -r $(SLUG)-$(VERSION)-$(ARCH).zip $(SLUG)
