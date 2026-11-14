/* Internal stack layout shared by the ARM64 C and assembler units. */
#ifndef CORO_ARM64_CONTEXT_H
#define CORO_ARM64_CONTEXT_H

#define CORO_ARM64_X19 0
#define CORO_ARM64_X21 16
#define CORO_ARM64_X23 32
#define CORO_ARM64_X25 48
#define CORO_ARM64_X27 64
#define CORO_ARM64_X29 80
#define CORO_ARM64_D8 96
#define CORO_ARM64_D10 112
#define CORO_ARM64_D12 128
#define CORO_ARM64_D14 144
#define CORO_ARM64_FPCR 160
#define CORO_ARM64_FPSR 168
#define CORO_ARM64_CONTEXT_BYTES 176

#endif
