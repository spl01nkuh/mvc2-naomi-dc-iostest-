# iOS front-end components

The development host in `app/` boots the experimental native engine after a
validated ZIP import. It includes editable controls, saved settings, real native
video and stereo audio. Engine startup/frame failures appear in the app. This
integration still requires device testing; a successful build does not establish
that a complete match works. No preview or generated game frames are supplied.

On macOS with Xcode and CMake 3.29+, run `bash build-development-ipa.sh device`
from this directory. The unsigned artifact is
`build/ios-development-device/MVC2-Gameplay-unsigned.ipa` at the repository
root. Sign it with your sideloading tool before device installation. iOS 16 or
later is required. Import the supported complete NAOMI `mvsc2.zip` through the
app's Import ZIP button. The game starts automatically once a validated import
is available. Use the floating coin button to add credit and touch START to
begin a match. The importer supports NAOMI cartridge ZIPs.

- `NativeAssetImporter.swift` imports the complete NAOMI `mvsc2.zip` through
  Files, validates it with `assets/`, and returns a private persistent directory.
  It does not import Dreamcast disc images. See [`assets/README.md`](assets/README.md)
  for supported ZIP forms and the manifest provenance.
- `AppSettings`, `TouchOverlay`, and `ControlsEditor` supply normalized layouts,
  drag editing, exact control sizes, remapping, up to 16 custom simultaneous
  button combinations, eight-way stick/hybrid/D-pad input, opacity and tuning.
  `AppSettings` is Codable; the app persists it and calls `sanitize()` after
  loading it. The app suspends gameplay, audio and touch input for menus,
  control editing, import, background transitions and audio interruptions.
- The masks in `InputState` match `HOST_INPUT.player` in `naomi_input.h`: LP/HP/
  A1/LK/HK/A2 are JVS B1 through B6. `input/` supplies the tested native analog
  helper and independent source collection. Coin events are separate from
  player masks: `InputState.insertCoin()` queues a tap. `snapshotForFrame()`
  atomically reads the button mask and consumes one `HOST_INPUT.coin` event per
  game frame.
- `NativeFrameStore` starts empty. The runtime can publish genuine frames from
  `rd_end_frame` with `NativeCanvasFrame(width:height:argb:)`, which explicitly
  converts numeric ARGB words to RGBA bytes. `clear()` removes the stopped
  session's image. `NativeCanvasView(settings:frames:)` remains black until a
  frame is supplied, and shows Metal initialization or GPU errors.
- `display/Presentation.metal` presents real frames with ten filter modes,
  strength, aspect ratio, size, independent width/height and offsets. The named
  CRT families are original lightweight approximations, not exact third-party
  shader implementations. The app bundles it as `default.metallib`.

`NativeGameplaySession` invokes the C runtime on one permanent OS thread across
start, frame steps, stop and restart. A serial dispatch queue cannot guarantee
the thread identity required by the native game coroutines. It paces steps at
27,000,000 / (858 × 525) Hz, resets its clock on resume and limits catch-up after
slow frames. Commands are applied between native steps. ZIP replacement awaits
the owner's stop before the importer can remove the previous asset set.
Presentation/settings updates never step the game.

`NativeAudioOutput` copies native 44.1 kHz signed stereo samples into six
preallocated Float32 PCM buffers for AVAudioEngine. Pending sound is limited to
150 ms and excess samples are dropped. Pausing stops queued playback and the
engine; completion callbacks only release buffer slots. Audio session failures
are reported separately so the native video session can continue. ROM assets
live in `Documents/MVC2/GameData`; native saves use `Documents/MVC2/SaveData`.

`NativeBridge.h` exposes the native gameplay, portable C import, input and
viewport APIs. Swift sources target iOS 16 or newer and require UIKit, SwiftUI,
MetalKit and AVFoundation. The app links the native engine; no commercial game
assets are bundled.

Portable tests are `ports/tests/test_ios_input.c`, `test_ios_viewport.c` and
`test_ios_rom_import.py`. Apple host tests are `test_ios_settings.swift` (compile
with `AppSettings.swift`, `InputState.swift`, and `NativeFrames.swift`; optionally
define `MVC2_TEST_NATIVE_BRIDGE` and link `input.c`) and
`test_metal_presentation.swift` (renders the production shader on a real Metal
GPU, or reports a skip when one is unavailable). Test-generated images are
confined to those tests, never supplied as runtime game frames.
