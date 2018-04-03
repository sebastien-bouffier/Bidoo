RACK_DIR ?= ../..
SLUG = Bidoo
VERSION = 0.6.1
DISTRIBUTABLES += $(wildcard LICENSE*) res

FLAGS += -Idep/include -I./src/dep/audiofile -I./src/dep/filters -I./src/dep/freeverb

include $(RACK_DIR)/arch.mk

ifeq ($(ARCH), lin)
	LDFLAGS += -L$(RACK_DIR)/dep/lib -lglfw $(RACK_DIR)/dep/lib/libcurl.a dep/lib/libmpg123.a
endif

ifeq ($(ARCH), mac)
	LDFLAGS += -L$(RACK_DIR)/dep/lib -lglfw $(RACK_DIR)/dep/lib/libcurl.a dep/lib/libmpg123.a
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
# $(glfw):
# 	cd dep/glfw && $(CMAKE) . \
# 		-DBUILD_SHARED_LIBS=ON \
# 		-DGLFW_COCOA_CHDIR_RESOURCES=OFF -DGLFW_COCOA_MENUBAR=ON -DGLFW_COCOA_RETINA_FRAMEBUFFER=ON
# 	$(MAKE) -C dep/glfw
# 	$(MAKE) -C dep/glfw install
#
# $(openssl):
# 	cd dep && $(WGET) https://www.openssl.org/source/openssl-1.1.0g.tar.gz
# 	cd dep && $(UNTAR) openssl-1.1.0g.tar.gz
# 	cd dep/openssl-1.1.0g && ./config --prefix="$(realpath $(DEP_LOCAL))"
# 	$(MAKE) -C dep/openssl-1.1.0g
# 	$(MAKE) -C dep/openssl-1.1.0g install_sw
#
# $(curl): $(openssl)
# 	cd dep && $(WGET) https://github.com/curl/curl/releases/download/curl-7_56_0/curl-7.56.0.tar.gz
# 	cd dep && $(UNTAR) curl-7.56.0.tar.gz
# 	cd dep/curl-7.56.0 && $(CONFIGURE) --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict \
# 		 --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-manual \
# 		--without-zlib --without-libpsl --without-libmetalink --without-libssh2 --without-librtmp --without-winidn --without-libidn2 --without-nghttp2 \
# 		--without-ca-bundle --with-ca-fallback --enable-static --with-ssl="$(realpath $(DEP_LOCAL))"
# 	$(MAKE) -C dep/curl-7.56.0
# 	$(MAKE) -C dep/curl-7.56.0 install
