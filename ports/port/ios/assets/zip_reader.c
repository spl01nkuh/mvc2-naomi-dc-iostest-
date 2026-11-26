#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
/* Darwin hides O_NOFOLLOW when only the strict POSIX feature set is enabled. */
#define _DARWIN_C_SOURCE 1
#endif
#include "zip_reader.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define MAX_CHIP_SIZE (8u * 1024u * 1024u)
#define MAX_MANIFEST_FILES 64
#define MAX_ZIP_ENTRIES 4096
#define MAX_NAME 1024

typedef struct Entry {
    uint32_t local_offset, compressed, uncompressed, crc;
    uint16_t method, flags;
    char name[MAX_NAME + 1];
    int found, created;
} Entry;

static uint16_t le16(const unsigned char *p) {
    return (uint16_t)(p[0] | (uint16_t)p[1] << 8);
}
static uint32_t le32(const unsigned char *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static int fail(char *error, size_t size, const char *fmt, ...) {
    if (error && size) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(error, size, fmt, args);
        va_end(args);
    }
    return 0;
}
static int read_at(FILE *source, uint32_t offset, void *buffer, size_t length) {
    return fseeko(source, (off_t)offset, SEEK_SET) == 0 && fread(buffer, 1, length, source) == length;
}
static int safe_name(const char *name, size_t length) {
    if (!length || name[0] == '/' || memchr(name, '\0', length) ||
        memchr(name, '\\', length) || memchr(name, ':', length)) return 0;
    size_t start = 0;
    for (size_t i = 0; i <= length; ++i) {
        if (i == length || name[i] == '/') {
            size_t component = i - start;
            if ((!component && i != length) ||
                (component == 1 && name[start] == '.') ||
                (component == 2 && name[start] == '.' && name[start + 1] == '.')) return 0;
            start = i + 1;
        }
    }
    return 1;
}
static int empty_directory(const char *path) {
    struct stat info;
    if (!path || lstat(path, &info) || !S_ISDIR(info.st_mode)) return 0;
    DIR *directory = opendir(path);
    if (!directory) return 0;
    struct dirent *entry;
    int empty = 1;
    errno = 0;
    while ((entry = readdir(directory)) != NULL)
        if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) { empty = 0; break; }
    if (errno) empty = 0;
    closedir(directory);
    return empty;
}
static int destination_path(char *path, size_t capacity, const char *directory, const char *name) {
    int n = snprintf(path, capacity, "%s/%s", directory, name);
    return n >= 0 && (size_t)n < capacity;
}

static int extract(FILE *source, const Entry *entry, FILE *output,
                   char *error, size_t error_size) {
    unsigned char in[32768], out[32768];
    uint32_t left = entry->compressed;
    uint32_t written = 0;
    uLong crc = crc32(0, Z_NULL, 0);
    if (entry->method == 0) {
        if (entry->compressed != entry->uncompressed)
            return fail(error, error_size, "Stored ZIP entry has inconsistent sizes: %s.", entry->name);
        while (left) {
            size_t n = left < sizeof(in) ? left : sizeof(in);
            if (fread(in, 1, n, source) != n || fwrite(in, 1, n, output) != n)
                return fail(error, error_size, "Could not copy %s (storage may be full).", entry->name);
            crc = crc32(crc, in, (uInt)n);
            written += (uint32_t)n;
            left -= (uint32_t)n;
        }
    } else {
        z_stream stream;
        memset(&stream, 0, sizeof(stream));
        if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
            return fail(error, error_size, "Could not initialize ZIP decompression.");
        int result = Z_OK;
        int ok = 1;
        while (result != Z_STREAM_END) {
            if (!stream.avail_in && left) {
                uInt n = left < sizeof(in) ? left : sizeof(in);
                if (fread(in, 1, n, source) != n) { ok = 0; break; }
                stream.next_in = in; stream.avail_in = n; left -= n;
            }
            stream.next_out = out; stream.avail_out = sizeof(out);
            uLong old_in = stream.total_in;
            result = inflate(&stream, Z_NO_FLUSH);
            size_t n = sizeof(out) - stream.avail_out;
            if ((result != Z_OK && result != Z_STREAM_END) ||
                n > entry->uncompressed - written ||
                (n && fwrite(out, 1, n, output) != n)) { ok = 0; break; }
            written += (uint32_t)n;
            crc = crc32(crc, out, (uInt)n);
            if (!n && stream.total_in == old_in && result != Z_STREAM_END) { ok = 0; break; }
        }
        if (left || stream.avail_in) ok = 0;
        inflateEnd(&stream);
        if (!ok) return fail(error, error_size, "Invalid or truncated deflate stream: %s.", entry->name);
    }
    return (written == entry->uncompressed && crc == entry->crc) ||
           fail(error, error_size, "ZIP checksum or size mismatch: %s.", entry->name);
}

int mvc2_zip_extract_manifest(const char *zip_path, const char *destination,
                              const MVC2ROMFile *manifest, size_t count,
                              char *error, size_t error_size) {
    if (error && error_size) error[0] = '\0';
    if (!zip_path || !manifest || !count || count > MAX_MANIFEST_FILES)
        return fail(error, error_size, "Invalid ROM import arguments.");
    for (size_t i = 0; i < count; ++i) {
        if (!manifest[i].name || !safe_name(manifest[i].name, strlen(manifest[i].name)) ||
            strchr(manifest[i].name, '/') || strlen(manifest[i].name) > MAX_NAME ||
            manifest[i].size > MAX_CHIP_SIZE)
            return fail(error, error_size, "Invalid ROM manifest.");
        for (size_t j = 0; j < i; ++j)
            if (!strcmp(manifest[i].name, manifest[j].name))
                return fail(error, error_size, "Duplicate ROM manifest filename.");
    }
    if (!empty_directory(destination))
        return fail(error, error_size, "ROM import requires a new empty staging directory.");
    FILE *source = fopen(zip_path, "rb");
    if (!source) return fail(error, error_size, "Cannot open ROM ZIP: %s", strerror(errno));
    Entry *entries = calloc(count, sizeof(*entries));
    unsigned char *tail = NULL;
    int ok = 0;
    char output_path[4096];
    if (!entries) { fail(error, error_size, "Not enough memory to inspect ROM ZIP."); goto done; }
    if (fseeko(source, 0, SEEK_END)) { fail(error, error_size, "Cannot seek in ROM ZIP."); goto done; }
    off_t file_size = ftello(source);
    if (file_size < 22 || (uint64_t)file_size > UINT32_MAX) {
        fail(error, error_size, "ROM ZIP is truncated or exceeds classic ZIP's 4 GiB limit."); goto done;
    }
    size_t tail_size = file_size < 65557 ? (size_t)file_size : 65557;
    tail = malloc(tail_size);
    if (!tail || !read_at(source, (uint32_t)(file_size - (off_t)tail_size), tail, tail_size)) {
        fail(error, error_size, "Cannot read ROM ZIP directory."); goto done;
    }
    size_t end = tail_size - 22;
    for (;;) {
        if (le32(tail + end) == 0x06054b50 && end + 22 + le16(tail + end + 20) == tail_size) break;
        if (!end) { fail(error, error_size, "Not a supported ZIP archive (no directory record)."); goto done; }
        --end;
    }
    const unsigned char *eocd = tail + end;
    uint32_t central_size = le32(eocd + 12), central_offset = le32(eocd + 16);
    uint32_t eocd_offset = (uint32_t)(file_size - (off_t)tail_size + (off_t)end);
    uint16_t number = le16(eocd + 10);
    if (le16(eocd + 4) || le16(eocd + 6) || le16(eocd + 8) != number ||
        !number || number > MAX_ZIP_ENTRIES || central_offset > eocd_offset ||
        central_size != eocd_offset - central_offset) {
        fail(error, error_size, "ZIP64, split archives, or inconsistent ZIP directories are unsupported."); goto done;
    }
    uint32_t cursor = central_offset;
    char prefix[MAX_NAME + 1] = {0};
    int have_prefix = 0, has_disc_image = 0;
    for (unsigned number_index = 0; number_index < number; ++number_index) {
        unsigned char header[46];
        if (cursor > eocd_offset || eocd_offset - cursor < sizeof(header) ||
            !read_at(source, cursor, header, sizeof(header)) || le32(header) != 0x02014b50) {
            fail(error, error_size, "Truncated ZIP central directory."); goto done;
        }
        uint16_t name_length = le16(header + 28), extra = le16(header + 30), comment = le16(header + 32);
        uint32_t entry_size = 46u + name_length + extra + comment;
        if (!name_length || name_length > MAX_NAME || entry_size > eocd_offset - cursor ||
            le16(header + 6) >= 45 || le16(header + 34)) {
            fail(error, error_size, "Unsupported ZIP entry metadata."); goto done;
        }
        char name[MAX_NAME + 1];
        if (!read_at(source, cursor + 46, name, name_length)) {
            fail(error, error_size, "Cannot read ZIP entry name."); goto done;
        }
        name[name_length] = '\0';
        if (!safe_name(name, name_length) || ((le32(header + 38) >> 16) & 0170000) == 0120000) {
            fail(error, error_size, "Unsafe path or symbolic link in ROM ZIP."); goto done;
        }
        const char *base = strrchr(name, '/');
        base = base ? base + 1 : name;
        const char *extension = strrchr(base, '.');
        if (extension && (!strcasecmp(extension, ".gdi") ||
                          !strcasecmp(extension, ".cdi") ||
                          !strcasecmp(extension, ".chd") ||
                          !strcasecmp(extension, ".cue")))
            has_disc_image = 1;
        for (size_t i = 0; i < count; ++i) {
            if (strcmp(base, manifest[i].name)) continue;
            Entry *entry = &entries[i];
            size_t prefix_length = (size_t)(base - name);
            if (entry->found) { fail(error, error_size, "Duplicate ROM chip in ZIP: %s.", base); goto done; }
            if (have_prefix && (strlen(prefix) != prefix_length || memcmp(prefix, name, prefix_length))) {
                fail(error, error_size, "ROM chips must share one archive directory."); goto done;
            }
            memcpy(prefix, name, prefix_length); prefix[prefix_length] = '\0'; have_prefix = 1;
            entry->found = 1;
            entry->flags = le16(header + 8); entry->method = le16(header + 10);
            entry->crc = le32(header + 16); entry->compressed = le32(header + 20);
            entry->uncompressed = le32(header + 24); entry->local_offset = le32(header + 42);
            memcpy(entry->name, name, (size_t)name_length + 1);
            if ((entry->flags & ~0x080eu) || (entry->method != 0 && entry->method != 8)) {
                fail(error, error_size, "Encrypted or unsupported compression for %s; use ZIP store or deflate.", base); goto done;
            }
            if (entry->uncompressed != manifest[i].size || entry->crc != manifest[i].crc32 ||
                entry->compressed > MAX_CHIP_SIZE + 65536u) {
                fail(error, error_size, "Incorrect ROM revision or size: %s (expected CRC %08x, %u bytes).",
                     base, manifest[i].crc32, manifest[i].size); goto done;
            }
        }
        cursor += entry_size;
    }
    if (cursor != eocd_offset) { fail(error, error_size, "Unexpected bytes in ZIP directory."); goto done; }
    for (size_t i = 0; i < count; ++i)
        if (!entries[i].found) {
            if (has_disc_image)
                fail(error, error_size, "This build requires the NAOMI mvsc2 ROM ZIP. Dreamcast disc images require a Dreamcast build.");
            else
                fail(error, error_size, "Missing ROM chip: %s. Import the complete mvsc2 set.", manifest[i].name);
            goto done;
        }
    for (size_t i = 0; i < count; ++i) {
        Entry *entry = &entries[i];
        unsigned char local[30];
        if (entry->local_offset > central_offset || central_offset - entry->local_offset < sizeof(local) ||
            !read_at(source, entry->local_offset, local, sizeof(local)) || le32(local) != 0x04034b50) {
            fail(error, error_size, "Invalid local ZIP entry: %s.", entry->name); goto done;
        }
        uint16_t name_length = le16(local + 26), extra = le16(local + 28);
        uint32_t header_size = 30u + name_length + extra;
        if (le16(local + 4) >= 45 || le16(local + 6) != entry->flags || le16(local + 8) != entry->method ||
            name_length != strlen(entry->name) || header_size > central_offset - entry->local_offset ||
            entry->compressed > central_offset - entry->local_offset - header_size) {
            fail(error, error_size, "Inconsistent local ZIP metadata: %s.", entry->name); goto done;
        }
        if (!(entry->flags & 8) && (le32(local + 14) != entry->crc ||
            le32(local + 18) != entry->compressed || le32(local + 22) != entry->uncompressed)) {
            fail(error, error_size, "Inconsistent local ZIP sizes or CRC: %s.", entry->name); goto done;
        }
        char name[MAX_NAME + 1];
        if (fread(name, 1, name_length, source) != name_length || memcmp(name, entry->name, name_length) ||
            fseeko(source, (off_t)entry->local_offset + header_size, SEEK_SET)) {
            fail(error, error_size, "Inconsistent local ZIP name: %s.", entry->name); goto done;
        }
        if (!destination_path(output_path, sizeof(output_path), destination, manifest[i].name)) {
            fail(error, error_size, "ROM asset output path is too long."); goto done;
        }
        int descriptor = open(output_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600);
        if (descriptor < 0) { fail(error, error_size, "Cannot create ROM asset: %s", strerror(errno)); goto done; }
        entry->created = 1;
        FILE *output = fdopen(descriptor, "wb");
        if (!output) { close(descriptor); fail(error, error_size, "Cannot open ROM asset output."); goto done; }
        int copied = extract(source, entry, output, error, error_size);
        if (fclose(output)) { fail(error, error_size, "Could not finish ROM asset (storage may be full)."); copied = 0; }
        if (!copied) goto done;
    }
    ok = 1;
done:
    if (!ok && entries)
        for (size_t i = 0; i < count; ++i)
            if (entries[i].created && destination_path(output_path, sizeof(output_path), destination, manifest[i].name))
                unlink(output_path);
    free(tail); free(entries); fclose(source);
    return ok;
}
