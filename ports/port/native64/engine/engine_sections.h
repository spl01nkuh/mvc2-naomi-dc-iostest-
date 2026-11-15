/* Forced into game/backend translation units only. The native host and
 * checkpoint controller are separate objects and never use these sections. */
#ifndef MVC2_ENGINE_SECTIONS_H
#define MVC2_ENGINE_SECTIONS_H
#if defined(__clang__) && defined(__APPLE__)
#pragma clang section data="__DATA,__mvc2_data" bss="__DATA,__mvc2_bss"
#elif defined(__clang__) && defined(__ELF__)
#pragma clang section data="mvc2_data" bss="mvc2_bss"
#endif
#endif
