RACK_DIR ?= ../..
SLUG = Bidoo
VERSION = 0.6.1

FLAGS += -Idep/include -I./src/dep/include -I./src/dep/audiofile -I./src/dep/filters -I./src/dep/freeverb
SOURCES += $(wildcard src/*.cpp src/dep/audiofile/*cpp src/dep/pffft/*c src/dep/filters/*cpp src/dep/freeverb/*cpp)
DISTRIBUTABLES += $(wildcard LICENSE*) res

include $(RACK_DIR)/arch.mk

ifeq ($(ARCH), lin)
	LDFLAGS += 	-Ldep/lib -lcurl -lssl
endif

ifeq ($(ARCH), mac)
	LDFLAGS += 	-Ldep/lib -lcurl -lssl
endif

ifeq ($(ARCH), win)
	LDFLAGS += 	-Ldep/lib -lcurl -lssl -lshlwapi
endif

# Static libs
mpg123 := dep/lib/libmpg123.a
curl := dep/lib/libcurl.a
openssl := dep/lib/ssl.a

ifeq ($(ARCH), win)
	glfw = dep/lib/libglfw3dll.a
endif

OBJECTS += $(mpg123) $(curl) $(glfw)

# Dependencies
$(shell mkdir -p dep)
DEP_LOCAL := dep
DEPS += $(mpg123) $(libcurl) $(glfw)

$(mpg123):
	cd dep && $(WGET) https://www.mpg123.de/download/mpg123-1.25.8.tar.bz2
	cd dep && $(UNTAR) mpg123-1.25.8.tar.bz2
	cd dep/mpg123-1.25.8 && ./configure --prefix="$(realpath $(DEP_LOCAL))" --with-cpu=generic --with-pic --disable-shared --enable-static
	cd dep/mpg123-1.25.8 && $(MAKE)
	cd dep/mpg123-1.25.8 && $(MAKE) install

$(glfw):
	cd dep/glfw && $(CMAKE) . \
		-DBUILD_SHARED_LIBS=ON \
		-DGLFW_COCOA_CHDIR_RESOURCES=OFF -DGLFW_COCOA_MENUBAR=ON -DGLFW_COCOA_RETINA_FRAMEBUFFER=ON
	$(MAKE) -C dep/glfw
	$(MAKE) -C dep/glfw install

$(openssl):
	cd dep && $(WGET) https://www.openssl.org/source/openssl-1.1.0g.tar.gz
	cd dep && $(UNTAR) openssl-1.1.0g.tar.gz
	cd dep/openssl-1.1.0g && ./config --prefix="$(realpath $(DEP_LOCAL))"
	$(MAKE) -C dep/openssl-1.1.0g
	$(MAKE) -C dep/openssl-1.1.0g install_sw

$(curl): $(openssl)
	cd dep && $(WGET) https://github.com/curl/curl/releases/download/curl-7_56_0/curl-7.56.0.tar.gz
	cd dep && $(UNTAR) curl-7.56.0.tar.gz
	cd dep/curl-7.56.0 && $(CONFIGURE) --disable-ftp --disable-file --disable-ldap --disable-ldaps --disable-rtsp --disable-proxy --disable-dict \
		 --disable-telnet --disable-tftp --disable-pop3 --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-manual \
		--without-zlib --without-libpsl --without-libmetalink --without-libssh2 --without-librtmp --without-winidn --without-libidn2 --without-nghttp2 \
		--without-ca-bundle --with-ca-fallback --with-ssl="$(realpath $(DEP_LOCAL))"
	$(MAKE) -C dep/curl-7.56.0
	$(MAKE) -C dep/curl-7.56.0 install

include $(RACK_DIR)/plugin.mk
