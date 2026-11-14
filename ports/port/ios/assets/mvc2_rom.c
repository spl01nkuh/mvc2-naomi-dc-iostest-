#define _POSIX_C_SOURCE 200809L
#include "mvc2_rom.h"
#include "zip_reader.h"
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <zlib.h>

/* Manifest provenance and the interleaving/mirror are documented in README.md. */
static const MVC2ROMFile files[] = {
    {"epr-23085a.ic11", 0x400000, 0x5d5b7ad1, 0x0000000},
    {"mpr-23048.ic17s", 0x800000, 0x93d7a63a, 0x0800000},
    {"mpr-23049.ic18",  0x800000, 0x003dcce0, 0x1000000},
    {"mpr-23050.ic19s", 0x800000, 0x1d6b88a7, 0x1800000},
    {"mpr-23051.ic20",  0x800000, 0x01226aaa, 0x2000000},
    {"mpr-23052.ic21s", 0x800000, 0x74bee120, 0x2800000},
    {"mpr-23053.ic22",  0x800000, 0xd92d4401, 0x3000000},
    {"mpr-23054.ic23s", 0x800000, 0x78ba02e8, 0x3800000},
    {"mpr-23055.ic24",  0x800000, 0x84319604, 0x4000000},
    {"mpr-23056.ic25s", 0x800000, 0xd7386034, 0x4800000},
    {"mpr-23057.ic26",  0x800000, 0xa3f087db, 0x5000000},
    {"mpr-23058.ic27s", 0x800000, 0x61a6cc5d, 0x5800000},
    {"mpr-23059.ic28",  0x800000, 0x64808024, 0x6000000},
    {"mpr-23060.ic29",  0x800000, 0x67519942, 0x6800000},
    {"mpr-23061.ic30s", 0x800000, 0xfb1844c4, 0x7000000},
    {"mpr-23083.ic31",  0x400000, 0xc61d2dfe, 0x7800000},
    {"mpr-23084.ic32s", 0x400000, 0x4ebbbdd9, 0x8000002}
};

static int fail(char *error, size_t size, const char *fmt, ...) {
    if (error && size) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(error, size, fmt, args);
        va_end(args);
    }
    return 0;
}

const MVC2ROMFile *mvc2_rom_manifest(size_t *count) {
    if (count) *count = sizeof(files) / sizeof(files[0]);
    return files;
}

int mvc2_rom_import_zip(const char *path, const char *destination,
                        char *error, size_t size) {
    return mvc2_zip_extract_manifest(path, destination, files,
                                     sizeof(files) / sizeof(files[0]), error, size);
}

static int chip_path(char *output, size_t capacity, const char *directory,
                      const char *name, char *error, size_t error_size) {
    if (!directory) return fail(error, error_size, "No ROM asset directory was supplied.");
    int n = snprintf(output, capacity, "%s/%s", directory, name);
    return (n >= 0 && (size_t)n < capacity) ||
           fail(error, error_size, "ROM asset path is too long.");
}

int mvc2_rom_validate_directory(const char *directory, char *error, size_t size) {
    unsigned char buffer[65536];
    char path[4096];
    if (error && size) error[0] = '\0';
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        if (!chip_path(path, sizeof(path), directory, files[i].name, error, size)) return 0;
        struct stat info;
        if (lstat(path, &info) || !S_ISREG(info.st_mode) ||
            info.st_size != files[i].size)
            return fail(error, size, "Missing or incorrect size: %s (expected %u bytes).",
                        files[i].name, files[i].size);
        FILE *file = fopen(path, "rb");
        if (!file) return fail(error, size, "Cannot read %s: %s", files[i].name, strerror(errno));
        uLong crc = crc32(0, Z_NULL, 0);
        size_t n;
        while ((n = fread(buffer, 1, sizeof(buffer), file)) != 0)
            crc = crc32(crc, buffer, (uInt)n);
        int read_error = ferror(file);
        fclose(file);
        if (read_error) return fail(error, size, "Could not finish reading %s.", files[i].name);
        if (crc != files[i].crc32)
            return fail(error, size, "Incorrect ROM revision or damaged file: %s (CRC %08lx, expected %08x).",
                        files[i].name, crc, files[i].crc32);
    }
    return 1;
}

static int read_chip(const char *directory, size_t index, uint32_t offset,
                       unsigned char *output, size_t length, char *error, size_t size) {
    char path[4096];
    if (offset > files[index].size || length > files[index].size - offset)
        return fail(error, size, "ROM asset read crossed a chip boundary.");
    if (!chip_path(path, sizeof(path), directory, files[index].name, error, size)) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return fail(error, size, "Cannot open %s: %s", files[index].name, strerror(errno));
    int ok = !fseeko(file, (off_t)offset, SEEK_SET) && fread(output, 1, length, file) == length;
    fclose(file);
    return ok || fail(error, size, "Could not read %s at offset %u.", files[index].name, offset);
}

int mvc2_rom_read(const char *directory, uint32_t offset, void *output, size_t length,
                  char *error, size_t size) {
    if (error && size) error[0] = '\0';
    if (offset > MVC2_ROM_BOARD_SIZE || length > MVC2_ROM_BOARD_SIZE - offset ||
        (!output && length)) return fail(error, size, "ROM board read is outside the cartridge.");
    unsigned char *bytes = output;
    while (length) {
        if (offset >= 0x8000000) {
            /* 32-bit words alternate the low/high 16-bit lane of ic31/ic32. */
            unsigned char lane[16384];
            uint32_t relative = offset - 0x8000000;
            size_t chunk = length < 32768 ? length : 32768;
            for (unsigned side = 0; side < 2; ++side) {
                uint32_t first = relative;
                while (first < relative + chunk && ((first & 2u) >> 1) != side) ++first;
                if (first == relative + chunk) continue;
                uint32_t last = relative + (uint32_t)chunk - 1;
                while (((last & 2u) >> 1) != side) --last;
                uint32_t source_start = (first / 4) * 2 + (first & 1u);
                uint32_t source_end = (last / 4) * 2 + (last & 1u) + 1;
                if (!read_chip(directory, 15 + side, source_start, lane,
                               source_end - source_start, error, size)) return 0;
                for (uint32_t pos = first; pos <= last; ++pos)
                    if (((pos & 2u) >> 1) == side)
                        bytes[pos - relative] = lane[(pos / 4) * 2 + (pos & 1u) - source_start];
            }
            bytes += chunk; offset += (uint32_t)chunk; length -= chunk;
            continue;
        }
        size_t index;
        uint32_t chip_offset, remaining;
        if (offset < 0x400000) {
            index = 0; chip_offset = offset; remaining = 0x400000 - offset;
        } else if (offset < 0x800000) {
            index = 2; chip_offset = 0x200000 + offset - 0x400000; remaining = 0x800000 - offset;
        } else if (offset < 0x7800000) {
            index = 1 + (offset - 0x800000) / 0x800000;
            chip_offset = (offset - 0x800000) % 0x800000;
            remaining = 0x800000 - chip_offset;
        } else if (offset < 0x7c00000) {
            index = 15; chip_offset = offset - 0x7800000; remaining = 0x7c00000 - offset;
        } else {
            size_t chunk = length < 0x8000000 - offset ? length : 0x8000000 - offset;
            memset(bytes, 0xff, chunk);
            bytes += chunk; offset += (uint32_t)chunk; length -= chunk;
            continue;
        }
        size_t chunk = length < remaining ? length : remaining;
        if (!read_chip(directory, index, chip_offset, bytes, chunk, error, size)) return 0;
        bytes += chunk; offset += (uint32_t)chunk; length -= chunk;
    }
    return 1;
}
