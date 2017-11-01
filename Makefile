
SOURCES = $(wildcard src/*.cpp)

include ../../plugin.mk

dist: all
	mkdir -p dist/Bidoo
	cp LICENSE* dist/Bidoo/
	cp plugin.* dist/Bidoo/
	cp -R res dist/Bidoo/
