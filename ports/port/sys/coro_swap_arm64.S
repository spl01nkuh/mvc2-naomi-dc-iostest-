/* Same-thread stack switch for the ordinary AAPCS64 / Apple arm64 ABI.
 * x18 is reserved by Apple and is never read or written here.
 * arm64e pointer-authenticated function pointers require their own adapter.
 */
#include "coro_arm64_context.h"

#if !defined(__aarch64__) && !defined(__arm64__)
#error "coro_swap_arm64.S requires ARM64"
#endif
#if defined(__arm64e__)
#error "coro_swap_arm64.S does not implement the arm64e pointer-authentication ABI"
#endif

#if defined(__APPLE__)
#define SWAP _coro_arm64_swap
#else
#define SWAP coro_arm64_swap
#endif

.text
.p2align 2
.globl SWAP
#if !defined(__APPLE__)
.type SWAP, %function
#endif
SWAP:
    sub sp, sp, #CORO_ARM64_CONTEXT_BYTES
    stp x19, x20, [sp, #CORO_ARM64_X19]
    stp x21, x22, [sp, #CORO_ARM64_X21]
    stp x23, x24, [sp, #CORO_ARM64_X23]
    stp x25, x26, [sp, #CORO_ARM64_X25]
    stp x27, x28, [sp, #CORO_ARM64_X27]
    stp x29, x30, [sp, #CORO_ARM64_X29]
    stp d8, d9, [sp, #CORO_ARM64_D8]
    stp d10, d11, [sp, #CORO_ARM64_D10]
    stp d12, d13, [sp, #CORO_ARM64_D12]
    stp d14, d15, [sp, #CORO_ARM64_D14]
    mrs x9, fpcr
    str x9, [sp, #CORO_ARM64_FPCR]
    mrs x9, fpsr
    str x9, [sp, #CORO_ARM64_FPSR]
    mov x9, sp
    str x9, [x0]
    mov sp, x1
    ldr x9, [sp, #CORO_ARM64_FPCR]
    msr fpcr, x9
    ldr x9, [sp, #CORO_ARM64_FPSR]
    msr fpsr, x9
    ldp d8, d9, [sp, #CORO_ARM64_D8]
    ldp d10, d11, [sp, #CORO_ARM64_D10]
    ldp d12, d13, [sp, #CORO_ARM64_D12]
    ldp d14, d15, [sp, #CORO_ARM64_D14]
    ldp x19, x20, [sp, #CORO_ARM64_X19]
    ldp x21, x22, [sp, #CORO_ARM64_X21]
    ldp x23, x24, [sp, #CORO_ARM64_X23]
    ldp x25, x26, [sp, #CORO_ARM64_X25]
    ldp x27, x28, [sp, #CORO_ARM64_X27]
    ldp x29, x30, [sp, #CORO_ARM64_X29]
    add sp, sp, #CORO_ARM64_CONTEXT_BYTES
    ret
#if !defined(__APPLE__)
.size SWAP, .-SWAP
.section .note.GNU-stack,"",%progbits
#endif
