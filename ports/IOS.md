# Native iOS port

This fork's `native-ios-port` branch is the iOS development branch. The existing
PC and 3DS game implementations are the starting point. The older iOS preview
app is not the game runtime and is not being copied over it.

**Gameplay is not ready on iOS. No IPA is provided by this branch's component
workflow.** A successful component build does not establish that the game runs.

The existing port is native game C, with its own rendering and protected M1/NRS
asset decoding. Its sound backend also interprets the sound chip's ARM7 driver;
it is not a CPU emulator wrapper around the arcade or Dreamcast game.

## Current work

- A streaming Files importer validates the complete `mvsc2.zip` and commits its
  17 ROM chips atomically on the phone. It uses bounded buffers and preserves
  the previous set after a failed import. It contains no game data. The imported
  directory is intended for the existing `naomi_rom_open` / `naomi_init` path;
  the existing M1 decoder remains responsible for protected assets.
- A same-thread ARM64 coroutine implementation supports Darwin and Linux and
  passes native ARM64 context-switch tests on both systems. This preserves the
  game/host handoff without moving UIKit or renderer callbacks onto a second
  thread.
- Experimental `HOST_NATIVE64` engine records use fixed-width words, aligned
  full-size work slots, field-boundary copies and native CPU-script pointers.
  Tests execute the original work-pool, model traversal and CPU-command routines
  for both arcade and Dreamcast layouts. With that flag disabled, preprocessing
  every game module remains identical to the upstream revision.
- Arcade loading queues keep ROM offsets separate from native buffer pointers.
  Player data and sprite/picture buffers share ordinary native RAM storage.
  The original player setup, animation decompression and palette routines are
  exercised together for all eight work slots. Throw checks return full native
  player pointers, and the game's message formatter uses the platform varargs ABI.
  Sprite counts are compiled from the reconstructed assembly tables.
- The combo and Thanos effects keep native references outside their original
  integer work overlays. Team, round, partner and system resets use native field
  boundaries while retaining entry and load metadata.
- Owned main RAM resolves serialized addresses without requiring a low process
  mapping. A separate model decoder creates native object-pointer and texture
  tables from immutable 32-bit records; its texture test uses the existing
  texture backend. File and queued stage loads register bounded raw extents;
  the scene loader resolves models into native descriptors on demand. Refreshes
  retain a stable file descriptor and preserve the prior owner after failure.
  Remaining scene and sprite paths still need integration and real-asset checks.
- Standalone font/sprite files now decode into owned native headers; the system
  texture loader retains the backing data and preserves its prior tables after
  a failed decode. The native board uses ordinary RAM storage and rejects legacy
  four-byte relocation lists. An ARM64 board fixture exercises the real coroutine,
  JVS input, rendered frames and stop/restart path. It does not run MVC2's main.
- The existing software renderer has a POSIX worker backend for iOS without
  SDL, including worker shutdown and restart. Generated-model tests run through
  the real projection, texture and model libraries into rendered ARGB pixels,
  checking depth, blending and identical output with one, two and four workers.
  A native matrix-stack stride mismatch in the projection library is corrected.
- An ARM64 audit records the game's serialized data and native layout
  differences. The existing 32-bit build guard remains in place.
- Editable multitouch controls, custom simultaneous buttons, saved options,
  screen geometry and Metal CRT presentation are reusable iOS components. They
  accept actual runtime frames; there is no generated preview or application
  entry point. Native ARGB frame words are explicitly converted to RGBA. A new
  runtime session starts empty and clears its previous frame when stopped.

## Why the existing build cannot simply target iPhone

`ports/CMakeLists.txt` requires 32-bit pointers. The game keeps serialized
addresses, callback words and fixed buffers in 32-bit RAM records. In particular,
`port/naomi/naomi_board.c` writes host callback/data addresses into four-byte
slots. An iPhone's native function pointers cannot be stored in those slots.
Changing `Sint32` or deleting the CMake guard does not solve this.

The native path uses callback tables from the reconstructed C source and rejects
the old serialized relocation lists. Remaining model references and scratch-buffer
aliases still need conversion, and the full game needs a build without GNU linker
data generation. It must then connect the game, rendered frames, input and audio
to the iOS lifecycle and interface. Existing rendering, protected asset decoding
and sound code provide the starting point for that integration.

## Component validation

On a POSIX host with CMake, Python 3, a C compiler and zlib headers:

```sh
cmake -S ports/tests -B build/ios-components -DCMAKE_BUILD_TYPE=Release
cmake --build build/ios-components --parallel 4
ctest --test-dir build/ios-components --output-on-failure
```

ARM64 hosts additionally execute the coroutine tests. The component workflow
tests native ARM64 on Linux and macOS and compiles the Apple components for
the iPhone and simulator SDKs. Those checks do not compile or link the full
game as an ARM64 iOS app.

The component workflow has passed on x86 Linux, native ARM64 Linux and ARM64
macOS. iPhone and simulator SDK compilation passes for the existing iOS
components. Saved settings, touch ownership, analog bridging and native frame
colors pass on macOS. All nine Metal presets, filter strength and presentation
geometry passed the hosted Apple Metal rendering checks. Those hosted checks
do not establish device frame rate or full-game compatibility.

The native memory/model, renderer and initial engine-routine suites also pass
on native ARM64 Linux and macOS. The additional player-loading, throw, message,
queue and pad-reader suites pass locally and are included in subsequent CI runs.
Synthetic asset fixtures verify decoding and failure handling; they do not
substitute for real gameplay data.

Before an IPA is delivered, the actual native game must load imported assets,
reach character selection, play and finish matches with working rendering,
sound and input, and support the requested custom controls, CRT effects and
screen adjustments. This needs real game data and device validation. No such
gameplay verification has been completed by this branch.
