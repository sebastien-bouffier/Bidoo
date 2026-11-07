/* Build configuration for FAAD2 2.11.4 (https://github.com/knik0/faad2, tag 2.11.4)
   inside the Bidoo VCV Rack plugin. Replaces the CMake defines; the sources are
   unmodified. Floating-point decoder, no DRM, SBR/PS on (AAC-LC, HE-AAC v1/v2). */
#ifndef BIDOO_FAAD2_CONFIG_H
#define BIDOO_FAAD2_CONFIG_H
#define HAVE_INTTYPES_H 1
#define HAVE_MEMCPY 1
#define HAVE_STRING_H 1
#define HAVE_STRINGS_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_LRINTF 1
#define PACKAGE_VERSION "2.11.4"
#define APPLY_DRC 1
#endif
