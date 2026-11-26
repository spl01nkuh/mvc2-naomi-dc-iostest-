# iOS cartridge asset import

`mvc2_rom_import_zip` streams the 17 program/data chips of the supported NAOMI
`mvsc2` set into a private, empty staging directory. The caller owns the final
directory rename. Extraction uses at most two 32 KiB decompression buffers,
bounded ZIP directory metadata, and zlib state; it does not load the 124 MiB set
into memory. Successful extraction checks actual byte counts and CRC32s against
both ZIP metadata and the canonical manifest. CRC32 identifies damaged or wrong
ROM revisions; it is not a cryptographic authenticity guarantee.

Supported archives use ordinary ZIP store/deflate, optional data descriptors,
and either root filenames or one shared enclosing directory. Nested folders are
accepted when all required chips share that folder. ZIP64, split ZIPs,
encryption, symbolic links, absolute paths, traversal components, duplicate
required basenames, mixed asset directories, ROM chips over 8 MiB, and archives
larger than the classic ZIP limit are rejected. Safe unrelated files are ignored. Archive paths never
become output paths; output names come only from the fixed manifest. Existing
destinations are never overwritten. All newly written files are removed if any
chip fails, including a late checksum failure. No BIOS or EEPROM is required by
this native asset import layer.

`mvc2_rom_read` presents the raw 0x08800000-byte cartridge layout with:

- Program at 0x0000000, fourteen 8 MiB data chips at 0x0800000–0x77fffff.
- 0x0400000–0x07fffff copied from 0x1200000–0x15fffff.
- A 4 MiB chip at 0x7800000, then an erased 0xff gap to 0x8000000.
- The same 4 MiB chip interleaved in 16-bit lanes with the final chip at
  0x8000000–0x87fffff.

These are raw asset bytes. The original game also requests NRS-protected reads
when bit 31 is set in `src/am_load.c`'s file table. The existing runtime implements
those through `ports/port/naomi/naomi_m1.c`; this helper must not replace that path
or execute the cartridge program. Hand the validated directory to the existing
`naomi_rom_open`/`naomi_init` path once the native iOS runtime is ready.

Manifest and layout provenance: the `ROM_START( mvsc2 )` declaration in
[MAME naomi.cpp at b9252e738e04140fe8ebf58a8b283a818dc5b497](https://github.com/mamedev/mame/blob/b9252e738e04140fe8ebf58a8b283a818dc5b497/src/mame/sega/naomi.cpp).
Only declarative chip names, sizes, checksums and layout facts are retained;
no MAME emulator implementation or game data is included. The program SHA-1
`f58c31b245fc33fa541f9f074548402a63f7c3d3` agrees with this repository's README.

Run synthetic importer/address-map tests on a POSIX host with a C compiler,
Python 3 and zlib headers:

```sh
python3 ports/tests/test_ios_rom_import.py
```

`ports/port/ios/NativeAssetImporter.swift` owns Files security-scoped access,
coordinates the streamed read on a worker task, and commits the completed
directory through an atomic `active.json` pointer. A failed import preserves the
previous set. It is an import component; it does not start a game or provide a
demo app. The Objective-C bridging header for a future iOS target needs only
`#include "assets/mvc2_rom.h"` with `ports/port/ios` in its include search path.
Swift/UIKit execution still requires an Apple build and device validation.
