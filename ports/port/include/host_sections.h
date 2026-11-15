/* Group the native library's hot routines using each object format's syntax. */
#ifndef MVC2_HOST_SECTIONS_H
#define MVC2_HOST_SECTIONS_H

#ifdef __APPLE__
#define MVC2_NL_HOT_SECTION "__TEXT,__mvc2_nlhot,regular,pure_instructions"
#else
#define MVC2_NL_HOT_SECTION ".text.nlhot"
#endif

#endif
