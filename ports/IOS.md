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
- A same-thread ARM64 coroutine implementation is being added for Darwin and
  Linux. This preserves the game/host handoff without moving UIKit or renderer
  callbacks onto a second thread. It is a platform component, not a replacement
  for the remaining native game port.
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

The native adaptation must separate serialized 32-bit records from host records,
resolve callbacks through typed native tables, replace fixed RAM aliases and
GNU linker-dependent data generation, and provide the Darwin frame/lifecycle
integration. It must then connect real rendered frames, input and audio to the
iOS interface. Existing game rendering, texture handling, protected assets and
sound code can be reused after those boundaries are addressed.

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

Before an IPA is delivered, the actual native game must load imported assets,
reach character selection, play and finish matches with working rendering,
sound and input, and support the requested custom controls, CRT effects and
screen adjustments. This needs real game data and device validation. No such
gameplay verification has been completed by this branch.
