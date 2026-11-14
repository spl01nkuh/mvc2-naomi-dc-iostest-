#!/usr/bin/env python3
"""Verify the actual device bundle; does not assert gameplay or device signing."""
import plistlib
from pathlib import PurePosixPath
import struct
import sys
import zipfile

with zipfile.ZipFile(sys.argv[1]) as archive:
    names = archive.namelist()
    assert all(not PurePosixPath(name).is_absolute() and ".." not in PurePosixPath(name).parts
               for name in names), "unsafe archive path"
    base = "Payload/MVC2Development.app/"
    info = plistlib.loads(archive.read(base + "Info.plist"))
    assert info["CFBundlePackageType"] == "APPL"
    assert info["CFBundleIdentifier"] == "com.spl01nkuh.mvc2.native.dev"
    assert info["CFBundleExecutable"] == "MVC2Development"
    assert info["UIFileSharingEnabled"] and info["LSSupportsOpeningDocumentsInPlace"]
    assert info["MinimumOSVersion"] == "16.0"
    executable = archive.read(base + info["CFBundleExecutable"])
    magic, cpu, _, kind, commands, _, _, _ = struct.unpack_from("<IiiIIIII", executable)
    assert magic == 0xfeedfacf and cpu == 0x100000c and kind == 2, "expected ARM64 Mach-O executable"
    cursor, platforms = 32, []
    for _ in range(commands):
        command, length = struct.unpack_from("<II", executable, cursor)
        assert length >= 8 and cursor + length <= len(executable)
        if command == 0x32:
            platforms.append(struct.unpack_from("<I", executable, cursor + 8)[0])
        cursor += length
    assert platforms == [2], "expected iOS device platform"
    assert len(archive.read(base + "default.metallib")) > 100
    assert not any(name.endswith((".ic11", ".ic17s", ".ic18", ".zip")) for name in names), "game data must not be bundled"
    assert not any("embedded.mobileprovision" in name for name in names), "this artifact is unsigned"
    print("Development IPA verified: iOS ARM64 executable, bundled Metal library, Files support; unsigned, no gameplay claim.")
