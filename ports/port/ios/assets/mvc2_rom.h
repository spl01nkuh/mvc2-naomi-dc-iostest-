#ifndef MVC2_ROM_H
#define MVC2_ROM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MVC2_ROM_BOARD_SIZE UINT32_C(0x08800000)

typedef struct MVC2ROMFile {
    const char *name;
    uint32_t size;
    uint32_t crc32;
    uint32_t board_offset;
} MVC2ROMFile;

/* The supported set is the NAOMI mvsc2 revision whose program is epr-23085a.
 * BIOS and EEPROM files are not used by the native asset loader. */
const MVC2ROMFile *mvc2_rom_manifest(size_t *count);

/* Return 1 on success, 0 with a descriptive, NUL-terminated error on failure.
 * Destination must be an existing empty directory, private to this operation.
 * Stored and deflated classic ZIPs are supported, with optional data descriptors.
 * All required chips may be at the root or in one common enclosing directory.
 * Unknown files are ignored; unsafe archive paths and duplicate chips fail.
 * File size and both archive and canonical game CRCs are verified while streaming.
 * On failure, this call removes only files it created. The caller commits the
 * completed staging directory by renaming it into its persistent asset location.
 */
int mvc2_rom_import_zip(const char *zip_path, const char *empty_destination,
                        char *error, size_t error_size);
int mvc2_rom_validate_directory(const char *directory, char *error, size_t error_size);

/* Read the raw cartridge layout (including its mirror and interleaved area).
 * Validate the directory once before using this accessor. Reads never execute
 * game machine code. This does not implement the NRS encrypted/decompressed
 * stream used by addresses carrying bit 31 in the original game's file table.
 */
int mvc2_rom_read(const char *directory, uint32_t board_offset, void *output,
                  size_t length, char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
