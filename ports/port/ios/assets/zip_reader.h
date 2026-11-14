#ifndef MVC2_ZIP_READER_H
#define MVC2_ZIP_READER_H

#include "mvc2_rom.h"

#ifdef __cplusplus
extern "C" {
#endif

/* General form used by the importer and synthetic fixtures. Names must be
 * simple unique basenames. No archive-provided name is used as an output path. */
int mvc2_zip_extract_manifest(const char *zip_path, const char *empty_destination,
                              const MVC2ROMFile *manifest, size_t count,
                              char *error, size_t error_size);

#ifdef __cplusplus
}
#endif
#endif
