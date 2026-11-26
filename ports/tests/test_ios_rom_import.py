#!/usr/bin/env python3
"""Exercise the production ZIP importer with synthetic assets, never game ROMs.

Pass a compiled shared library, or let this script build one using the host CC.
"""
import ctypes
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "ports/port/ios/assets"
LIBRARY = sys.argv.pop(1) if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else None


class ROMFile(ctypes.Structure):
    _fields_ = [("name", ctypes.c_char_p), ("size", ctypes.c_uint32),
                ("crc32", ctypes.c_uint32), ("board_offset", ctypes.c_uint32)]


class ROMImportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="mvc2-rom-tests-")
        path = LIBRARY or str(Path(cls.build.name) / "libmvc2rom.so")
        if not LIBRARY:
            subprocess.run([os.environ.get("CC", "cc"), "-std=c17", "-Wall", "-Wextra", "-Werror",
                            "-fPIC", "-shared", str(ASSETS / "mvc2_rom.c"),
                            str(ASSETS / "zip_reader.c"), "-lz", "-o", path], check=True)
        cls.library = ctypes.CDLL(path)
        cls.library.mvc2_zip_extract_manifest.argtypes = [ctypes.c_char_p, ctypes.c_char_p,
                                                        ctypes.POINTER(ROMFile), ctypes.c_size_t,
                                                        ctypes.c_char_p, ctypes.c_size_t]
        cls.library.mvc2_zip_extract_manifest.restype = ctypes.c_int
        cls.library.mvc2_rom_read.argtypes = [ctypes.c_char_p, ctypes.c_uint32, ctypes.c_void_p,
                                            ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t]
        cls.library.mvc2_rom_read.restype = ctypes.c_int
        cls.library.mvc2_rom_manifest.argtypes = [ctypes.POINTER(ctypes.c_size_t)]
        cls.library.mvc2_rom_manifest.restype = ctypes.POINTER(ROMFile)

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="mvc2-import-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.archive = self.root / "assets.zip"
        self.destination = self.root / "out"
        self.destination.mkdir()
        self.assets = {"a.ic1": b"abcdefgh" * 7,
                       "b.ic2": bytes(range(256)) * 4000}
        self.manifest = (ROMFile * len(self.assets))(*[
            ROMFile(name.encode(), len(data), zlib.crc32(data), 0)
            for name, data in self.assets.items()])

    def write_zip(self, names=None, compression=zipfile.ZIP_DEFLATED):
        with zipfile.ZipFile(self.archive, "w", compression) as archive:
            for name, data in self.assets.items():
                archive.writestr((names or {}).get(name, name), data)

    def invoke(self, expect=True):
        error = ctypes.create_string_buffer(512)
        result = self.library.mvc2_zip_extract_manifest(
            os.fsencode(self.archive), os.fsencode(self.destination), self.manifest,
            len(self.manifest), error, len(error))
        self.assertEqual(bool(result), expect, error.value.decode())
        if expect:
            self.assertEqual(error.value, b"")
            self.assertEqual({p.name: p.read_bytes() for p in self.destination.iterdir()}, self.assets)
        else:
            self.assertTrue(error.value)
            self.assertEqual(list(self.destination.iterdir()), [])
        return error.value.decode()

    def test_deflate_import_and_safe_enclosing_folder(self):
        self.write_zip({n: "my-game/mvsc2/" + n for n in self.assets})
        self.invoke()

    def test_stored_import(self):
        self.write_zip(compression=zipfile.ZIP_STORED)
        self.invoke()

    def test_classic_zip_over_old_size_limit(self):
        # Sparse prefix keeps the fixture small while testing large ZIP offsets.
        with self.archive.open("wb") as stream:
            stream.seek(161 * 1024 * 1024)
            with zipfile.ZipFile(stream, "w", zipfile.ZIP_STORED) as archive:
                for name, data in self.assets.items():
                    archive.writestr(name, data)
        self.invoke()

    def test_unknown_safe_files_are_ignored(self):
        self.write_zip()
        with zipfile.ZipFile(self.archive, "a") as archive:
            archive.writestr("readme.txt", "Not an asset")
        self.invoke()

    def test_missing_chip(self):
        self.write_zip({"b.ic2": "other.ic2"})
        self.assertIn("Missing ROM chip", self.invoke(False))

    def test_disc_image_explains_platform_requirement(self):
        with zipfile.ZipFile(self.archive, "w") as archive:
            archive.writestr("disc.GDI", b"test descriptor")
        self.assertIn("Dreamcast disc images require a Dreamcast build", self.invoke(False))

    def test_wrong_chip_revision(self):
        self.write_zip()
        self.manifest[0].crc32 ^= 1
        self.assertIn("Incorrect ROM revision", self.invoke(False))

    def test_duplicate_chip(self):
        self.write_zip()
        with zipfile.ZipFile(self.archive, "a") as archive:
            archive.writestr("nested/a.ic1", self.assets["a.ic1"])
        self.assertIn("Duplicate ROM", self.invoke(False))

    def test_mixed_asset_directories(self):
        self.write_zip({"b.ic2": "nested/b.ic2"})
        self.assertIn("share one", self.invoke(False))

    def test_traversal_path_in_ignored_file_is_rejected(self):
        self.write_zip()
        with zipfile.ZipFile(self.archive, "a") as archive:
            archive.writestr("../escape", b"x")
        self.assertIn("Unsafe path", self.invoke(False))
        self.assertFalse((self.root / "escape").exists())

    def test_symlink_is_rejected(self):
        self.write_zip()
        with zipfile.ZipFile(self.archive, "a") as archive:
            link = zipfile.ZipInfo("symlink")
            link.create_system = 3
            link.external_attr = 0o120777 << 16
            archive.writestr(link, "a.ic1")
        self.assertIn("symbolic link", self.invoke(False))

    def test_truncated_directory(self):
        self.write_zip()
        self.archive.write_bytes(self.archive.read_bytes()[:-5])
        self.invoke(False)

    def test_inconsistent_local_filename(self):
        self.write_zip()
        raw = bytearray(self.archive.read_bytes())
        raw[30] = ord("z")
        self.archive.write_bytes(raw)
        self.assertIn("local ZIP name", self.invoke(False))

    def test_corrupt_payload_rolls_back_earlier_chips(self):
        self.write_zip(compression=zipfile.ZIP_STORED)
        with zipfile.ZipFile(self.archive) as archive:
            offset = archive.getinfo("b.ic2").header_offset
        raw = bytearray(self.archive.read_bytes())
        raw[offset + 30 + len("b.ic2") + 7] ^= 1
        self.archive.write_bytes(raw)
        self.assertIn("checksum", self.invoke(False))

    def test_unknown_compression_is_rejected(self):
        self.write_zip(compression=zipfile.ZIP_BZIP2)
        self.assertIn("Unsupported ZIP entry metadata", self.invoke(False))

    def test_data_descriptors(self):
        # Nonseekable output forces ZIP data descriptors instead of local sizes.
        class NonSeekable:
            def __init__(self, file): self.file = file
            def write(self, data): return self.file.write(data)
            def flush(self): return self.file.flush()
        with self.archive.open("wb") as output:
            with zipfile.ZipFile(NonSeekable(output), "w", zipfile.ZIP_DEFLATED) as archive:
                for name, data in self.assets.items(): archive.writestr(name, data)
        self.invoke()

    def test_zip64_rejected(self):
        self.write_zip()
        raw = bytearray(self.archive.read_bytes())
        struct.pack_into("I", raw, len(raw) - 22 + 16, 0xFFFFFFFF)
        self.archive.write_bytes(raw)
        self.invoke(False)

    def test_nonempty_destination_is_not_changed(self):
        self.write_zip()
        existing = self.destination / "keep"
        existing.write_bytes(b"retain")
        error = ctypes.create_string_buffer(512)
        result = self.library.mvc2_zip_extract_manifest(
            os.fsencode(self.archive), os.fsencode(self.destination), self.manifest,
            len(self.manifest), error, len(error))
        self.assertEqual(result, 0)
        self.assertEqual(existing.read_bytes(), b"retain")

    def test_raw_board_mirror_and_interleaved_lanes(self):
        # Sparse synthetic chip files are enough to test the raw address mapping.
        def chip(name, size, updates):
            with (self.destination / name).open("wb") as output:
                output.truncate(size)
                for offset, data in updates:
                    output.seek(offset)
                    output.write(data)
        chip("epr-23085a.ic11", 0x400000, [(0x3FFFFE, b"AB")])
        chip("mpr-23049.ic18", 0x800000, [(0x200000, b"mirror")])
        lane0 = b"abcdefgh" + bytes(range(256)) * 512
        lane1 = b"12345678" + bytes(reversed(range(256))) * 512
        chip("mpr-23083.ic31", 0x400000, [(0, lane0), (0x3FFFFC, b"WXYZ")])
        chip("mpr-23084.ic32s", 0x400000, [(0, lane1), (0x3FFFFC, b"6789")])

        def read(offset, length):
            output = ctypes.create_string_buffer(length)
            error = ctypes.create_string_buffer(256)
            result = self.library.mvc2_rom_read(os.fsencode(self.destination), offset,
                                               output, length, error, len(error))
            self.assertEqual(result, 1, error.value.decode())
            return output.raw
        self.assertEqual(read(0x3FFFFE, 8), b"ABmirror")
        self.assertEqual(read(0x7BFFFFC, 8), b"WXYZ\xff\xff\xff\xff")
        self.assertEqual(read(0x8000000, 16), b"ab12cd34ef56gh78")
        self.assertEqual(read(0x8000001, 6), b"b12cd3")
        self.assertEqual(read(0x87FFFF8, 8), b"WX67YZ89")
        expected = b"".join(lane0[i:i + 2] + lane1[i:i + 2] for i in range(0, len(lane0), 2))
        for offset in (0, 1, 2, 3, 32765, 32766, 32767, 32768):
            self.assertEqual(read(0x8000000 + offset, 65539), expected[offset:offset + 65539])
        error = ctypes.create_string_buffer(256)
        self.assertEqual(self.library.mvc2_rom_read(os.fsencode(self.destination), 0x8800000,
                                                  None, 1, error, len(error)), 0)

    def test_production_manifest_shape(self):
        count = ctypes.c_size_t()
        manifest = self.library.mvc2_rom_manifest(ctypes.byref(count))
        self.assertEqual(count.value, 17)
        self.assertEqual(manifest[0].name, b"epr-23085a.ic11")
        self.assertEqual(manifest[0].crc32, 0x5D5B7AD1)
        self.assertEqual(sum(manifest[i].size for i in range(count.value)), 124 * 1024 * 1024)

    def test_imported_filenames_and_sizes_match_existing_runtime(self):
        # An imported set must be consumable by this fork's existing loader;
        # this catches a mismatched revision or filename during future changes.
        source = (ROOT / "ports/port/naomi/naomi_rom.c").read_text()
        table = re.search(r"static const ROM_FILE files\[\]\s*=\s*\{(.*?)\n\};",
                          source, re.S)
        self.assertIsNotNone(table)
        runtime = re.findall(r'\{\s*"([^"]+)"\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\}',
                             table.group(1))
        count = ctypes.c_size_t()
        manifest = self.library.mvc2_rom_manifest(ctypes.byref(count))
        self.assertEqual(len(runtime), count.value - 1)
        for i, (name, offset, size) in enumerate(runtime):
            self.assertEqual(manifest[i].name.decode(), name)
            self.assertEqual(manifest[i].board_offset, int(offset, 16))
            self.assertEqual(manifest[i].size, int(size, 16))
        self.assertEqual(manifest[count.value - 1].name, b"mpr-23084.ic32s")
        self.assertIn('"mpr-23084.ic32s"', source)


if __name__ == "__main__":
    unittest.main()
