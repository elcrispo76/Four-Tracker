/* Four Tracker: a small config.h for building LAME's encoder (libmp3lame) as part of the program,
   on Windows (MSVC), Mac (clang) and Linux (gcc). Only the encoder is built (no mpglib decoder). */
#ifndef FOURTRACKER_LAME_CONFIG_H
#define FOURTRACKER_LAME_CONFIG_H
#define STDC_HEADERS 1
#define HAVE_ERRNO_H 1
#define HAVE_FCNTL_H 1
#define HAVE_LIMITS_H 1
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STRCHR 1
#define HAVE_MEMCPY 1
#define PROTOTYPES 1
#define USE_FAST_LOG 1
#define PACKAGE "lame"
#define VERSION "3.100"
#define LAME_LIBRARY_BUILD 1
typedef float ieee754_float32_t;
#if defined(_MSC_VER)
 #pragma warning(disable: 4244 4267 4305 4018 4996 4101 4146)
 #define _CRT_SECURE_NO_WARNINGS 1
 #if ! defined(__cplusplus) && ! defined(inline)
  #define inline __inline     /* older MSVC C compilers only know __inline */
 #endif
#endif
#endif
