# Native iOS gameplay test build

Branch `native-ios-gameplay` integrates upstream release **0.2.1**
(`6d9e0928395f5e8cc97935408909eea5e7d36c87`, from upstream `main`). The separate
`ports/port/native64/engine` build compiles the reconstructed arcade game as
native ARM64 C. The iOS app connects its actual game coroutine, rendering,
input and audio; the earlier import-only app did not include this engine.

**This is an experimental device-test build.** Commercial ROM assets are
absent from this repository and CI. Compilation, synthetic asset tests and a
simulator host launch do not verify booting a real cartridge, character
selection, a complete match, or device frame rate. Those checks require the
user's imported ROM and an iPhone/iPad. Modifier-volume shadows are currently
unsupported by the software renderer and are explicitly logged.

## Build and install

On macOS with the full Xcode app and CMake 3.29 or later:

```sh
bash ports/port/ios/build-development-ipa.sh device
python3 ports/port/ios/verify-development-ipa.py \
  build/ios-development-device/MVC2-Gameplay-unsigned.ipa
```

Sign the unsigned IPA with your sideloading tool before installation. iOS 16
or later and Metal are required. The `iOS native gameplay IPA` Actions workflow
builds both the device and simulator apps and uploads the unsigned IPA.
The existing development bundle identifier is preserved so replacing the old
test app can retain its imported ROM and settings.

In the app:

1. Use **Import ZIP** to select your complete NAOMI `mvsc2.zip` through Files.
   The importer streams and validates all 17 required ROM chips, then commits
   them atomically. A failed import preserves the previous set. Dreamcast disc
   images are not supported by this importer.
2. Choose **Start Game**. After the native game boots, use **Insert Coin** and
   the touch **START** control. Startup failures appear in the app.
3. Use **Layout** for control position, size, labels and custom simultaneous
   buttons. D-pad, analog and hybrid styles have scalable vector artwork,
   direction feedback and a moving stick grip.
4. Use **Settings** for native widescreen, touch tuning, CRT presets, filter
   strength, scale, independent width/height and offsets. The CRT presets are
   original lightweight approximations of their named shader families.

Native widescreen expands the rendered world horizontally at the same pixel
scale, from 640×480 to 854×480. The original 640-pixel HUD safe area stays
centered; it does not change game physics or the original camera's movement
limits. Some stage edges and effects whose lifetimes use the original logical
screen may become visible and still require real-asset testing. Turn the option
off to return to the original viewport. Geometry/Fill controls remain separate.

Imported assets live in `Documents/MVC2/GameData`, settings in
`Documents/MVC2/settings.json`, and game backups in `Documents/MVC2/SaveData`.

## Runtime and validation

`NativeGameplaySession` owns one permanent OS thread for native start, frame
steps, stop and restart. It copies the actual renderer's tightly packed ARGB
frames and 44.1 kHz stereo PCM before advancing another frame. The host
suspends game, touch input and queued audio for menus, control editing,
backgrounding and audio interruptions. Replacing a ZIP waits for the old
engine to release its ROM handles.

The `HOST_NATIVE64` path retains fixed-width serialized records while using
native pointers for linked tables, work slots, model/texture owners and
character scratch records. Owned RAM resolves cartridge addresses without
requiring a low process mapping. Native program startup rejects old four-byte
callback relocation lists. Clang section checkpoints restore native game and
backend globals after resources and renderer workers have been released.

The original 32-bit desktop/3DS CMake build remains separate. With
`HOST_NATIVE64` disabled, the guard checker compares every arcade/DC C module
against upstream 0.2.1. The iOS app executes linked native game C. The upstream
sound implementation interprets the AICA sound driver's ARM7 code.

Run the regression suite with:

```sh
cmake -S ports/tests -B build/ios-components -DCMAKE_BUILD_TYPE=Release
cmake --build build/ios-components --parallel 4
ctest --test-dir build/ios-components --output-on-failure
```

ARM64 hosts additionally link the complete native engine and exercise the
coroutine/board and runtime error paths. Tests cover streaming ZIP validation,
native work/character layouts, actual player-loading and backup routines,
projection/model/texture rendering, input ownership, checkpoint restoration,
and shader presentation. Widescreen tests use synthetic geometry through the
production renderer to check extra scene visibility, pixel scale, HUD position,
clipping, fades and worker bounds. The Apple workflow also compiles the iPhone
and simulator SDKs. These checks do not establish a verified playable match
with real assets.
