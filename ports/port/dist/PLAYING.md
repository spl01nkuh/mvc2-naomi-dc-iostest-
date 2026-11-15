# Marvel vs. Capcom 2: playing

`mvc2.exe` runs the arcade game or the Dreamcast version; `mvc2dc.exe` only the Dreamcast's. No game
data comes with it.

## The game's data

- The arcade: MAME's `mvsc2.zip` in `roms/mvsc2` beside the executable (or its files, unzipped there). The first
  start unpacks the zip into `roms/mvsc2/mvsc2.bin` (136 MB).
- The Dreamcast: the files of your own USA disc (`1ST_READ.BIN` and the rest) in `disc`.

Other folders can be set in `mvc2.cfg` (`arcade = ...`, `dc = ...`) or given on the command line
(`mvc2 --dc D:\games\mvc2-disc`). With both found it asks which to play, once. If something is missing,
it says what and where; put it there and press Enter.

## Keys

|                 | Player 1        | Player 2        |
|-----------------|-----------------|-----------------|
| move            | arrows          | I J K L         |
| punch (L, H)    | A S             | R T             |
| assist 1        | D               | Y               |
| kick (L, H)     | Z X             | F G             |
| assist 2        | C               | H               |
| start           | 1               | 2               |

5 is a coin, 9 service, F2 the arcade's test menu.

- F1: the controls screen (or Back with the right shoulder held). Enter sets a control from the next key
  or button, Delete puts it back, Escape closes. It also sets the renderer, the picture and the sprites.
- F3: frame times.
- F9: switch between the arcade and the Dreamcast.
- F10: scaling (aspect, integer, stretch). F11 or Alt+Enter: full screen.
- Escape: quit.

Controllers: the first plugged in is player 1's, the second player 2's.

| Button                 |                    |
|------------------------|--------------------|
| d-pad, left stick      | move               |
| West (X), North (Y)    | light, heavy punch |
| South (A), East (B)    | light, heavy kick  |
| left or right shoulder | assist 1           |
| left or right trigger  | assist 2           |
| Start                  | start              |
| Back                   | coin               |

## Settings

`mvc2.cfg` beside the executable has the settings, each explained there.

The graphics card draws the game at the window's resolution: 3D stages, polygons and effects come out sharp, while the
characters keep their pixels (`sprites = sharp`; `pixel` for no smoothing at all, `arcade` for the
arcade's own filtering). `aa = 2` (up to 4) smooths edges by drawing larger still; `resolution = 1` is
the arcade's 640 by 480. F1 sets these too.

Without a graphics card that can, or with `renderer = soft`, the CPU draws the arcade's 640 by 480
instead; on a slow PC, `render = half` makes that much faster.

`unlock = all` lets you choose every character, colour and stage. Your saves keep only what you really
unlocked, but games played this way still count in the records.

## Sound

The AICA's own driver runs on an emulated ARM7 with its DSP; the Dreamcast's ADX music is decoded by the
port. `MVC2_SOUND=0` turns it off; `MVC2_WAV=file` records it.

## Saves

The Dreamcast's memory card is `vmu_save_A1.bin` beside the executable, the same file Flycast uses. The
arcade keeps its settings and book-keeping in `eeprom.bin` and `backup.bin` there.
