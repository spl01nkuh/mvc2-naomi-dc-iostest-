/* Base types, as in the Dreamcast SDK (Shinobi). */
#ifndef TYPES_H
#define TYPES_H

#if defined( HOST_NATIVE64 ) && !defined( HOST_PORT )
#error HOST_NATIVE64 requires HOST_PORT and the native host build
#endif
#ifdef HOST_NATIVE64
/* SHC's long is a 32-bit game word; Apple ARM64's long is 64-bit. Native
   pointers remain native pointers and must not be overlaid on ROM records. */
#include <stdint.h>
typedef int8_t Sint8;
typedef uint8_t Uint8;
typedef int16_t Sint16;
typedef uint16_t Uint16;
typedef int32_t Sint32;
typedef uint32_t Uint32;
#else
typedef signed char Sint8;
typedef unsigned char Uint8;
typedef signed short Sint16;
typedef unsigned short Uint16;
typedef signed long Sint32;
typedef unsigned long Uint32;
#endif
typedef float Float;

#ifndef NULL
#define NULL	( ( void * )0 )
#endif

#endif
