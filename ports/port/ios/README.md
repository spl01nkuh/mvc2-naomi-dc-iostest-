# iOS front-end components

These are library components for the native port, with no application entry
point or generated game preview. They are not a playable iOS build. The existing
32-bit runtime still needs a native ARM64 integration before the components can
be connected to gameplay or packaged for the user.

- `NativeAssetImporter.swift` imports the complete NAOMI `mvsc2.zip` through
  Files, validates it with `assets/`, and returns a private persistent directory.
  It does not import Dreamcast disc images. See [`assets/README.md`](assets/README.md)
  for supported ZIP forms and the manifest provenance.
- `AppSettings`, `TouchOverlay`, and `ControlsEditor` supply normalized layouts,
  drag editing, exact control sizes, remapping, up to 16 custom simultaneous
  button combinations, eight-way stick/hybrid/D-pad input, opacity and tuning.
  `AppSettings` is Codable; the eventual application owns persistence and calls
  `sanitize()` after loading it. The host must suspend the overlay and clear
  `InputState` for menus, editing sheets, import and background interruptions.
- The masks in `InputState` match `HOST_INPUT.player` in `naomi_input.h`: LP/HP/
  A1/LK/HK/A2 are JVS B1 through B6. `input/` supplies the tested native analog
  helper and independent source collection. Coin events are separate from
  player masks: `InputState.insertCoin()` queues a tap and `takeCoin()` consumes
  one `HOST_INPUT.coin` event per frame. An arcade host supplies its own Insert
  Coin button alongside the editable player controls.
- `NativeFrameStore` starts empty. The runtime can publish genuine frames from
  `rd_end_frame` with `NativeCanvasFrame(width:height:argb:)`, which explicitly
  converts numeric ARGB words to RGBA bytes. `clear()` removes the stopped
  session's image. `NativeCanvasView(settings:frames:)` remains black until a
  frame is supplied, and shows Metal initialization or GPU errors.
- `display/Presentation.metal` presents real frames with ten filter modes,
  strength, aspect ratio, size, independent width/height and offsets. The named
  CRT families are original lightweight approximations, not exact third-party
  shader implementations. An eventual app bundles it as `default.metallib`.

The runtime owner calls `naomi_run_frame` at the board's 59.94 Hz cadence and
provides `host_input`/`host_present`. Presentation and settings changes must not
advance the simulation. Pausing stops simulation and drains/suspends audio;
resuming resets its pacing clock. Those runtime hooks are not implemented by
these UI components.

`NativeBridge.h` exposes the portable C import, input and viewport APIs. Its
include search paths are `ports/port/ios` and `ports/port/include`. Swift sources
target iOS 16 or newer and require UIKit, SwiftUI and MetalKit. The entire set
can be typechecked as a library without an application or IPA.

Portable tests are `ports/tests/test_ios_input.c`, `test_ios_viewport.c` and
`test_ios_rom_import.py`. Apple host tests are `test_ios_settings.swift` (compile
with `AppSettings.swift`, `InputState.swift`, and `NativeFrames.swift`; optionally
define `MVC2_TEST_NATIVE_BRIDGE` and link `input.c`) and
`test_metal_presentation.swift` (renders the production shader on a real Metal
GPU, or reports a skip when one is unavailable). Test-generated images are
confined to those tests, never supplied as runtime game frames.
