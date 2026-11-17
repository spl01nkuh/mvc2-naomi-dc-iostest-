# ARM64 coroutine backend

`coro_arm64.c` and `coro_swap_arm64.S` implement the existing `coro.h` interface on
ordinary ARM64 Linux and Apple arm64. The game stays on its caller's thread;
switching does not dispatch work to a pthread, emulate instructions or allocate
writable executable memory. The implementation uses pthread only to verify
thread ownership. Each thread can adopt its own independent coroutine family.

The switch preserves x19–x30, d8–d15, FPCR and FPSR, with a 16-byte-aligned stack.
Apple's reserved x18 is untouched. The stack is rounded to whole system pages,
at least 16 KiB, with inaccessible guard pages at both ends. A coroutine entry
must yield rather than return, matching the existing backends. Restart/delete
of the currently running coroutine or adopted caller is a no-op. Cross-thread
use aborts explicitly. Coroutine handles must remain live and owned by the
calling thread; the API does not make stale handles safe.

The arm64e pointer-authentication ABI is rejected at compile time. Normal arm64
iOS binaries can run on arm64e-capable hardware; this is a binary ABI restriction,
not a restriction to older phone hardware. Native stack unwinding across a
suspended switch and AddressSanitizer fiber integration are not implemented.
Do not enable ASan for this backend without adding its fiber-switch hooks.

The independent test uses no game data. It keeps every callee-saved integer and
floating-point register live in an assembly checker, preserves floating-point
control/status across nested yields, checks stack locals over 10,000 round trips
per family, exercises restart/delete and two additional thread-local families,
and checks guard-page, entry-return and cross-thread failures in child processes.

On a native ARM64 Linux or Mac host, from the repository root:

```sh
cc -std=c17 -O2 -UNDEBUG -Wall -Wextra -Wpedantic -Werror -pthread \
  -Iports/port/sys ports/port/sys/coro_arm64.c ports/port/sys/coro_swap_arm64.S \
  ports/tests/test_coro.c ports/tests/coro_registers_arm64.S -o /tmp/test-mvc2-coro
/tmp/test-mvc2-coro
```

This only supplies a native scheduling component. It does not resolve the game's
32-bit serialized/address ABI or establish that gameplay works on iOS.
