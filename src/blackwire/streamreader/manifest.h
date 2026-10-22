#ifndef BW_MANIFEST_H
#define BW_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#define BW_MANIFEST_MAGIC "BWMAP002"
#define BW_MANIFEST_VERSION 2u
#define BW_MANIFEST_HEADER_SIZE 40u
#define BW_MANIFEST_RECORD_SIZE 32u
#define BW_MANIFEST_MAX_FILES 131072u
#define BW_MANIFEST_MAX_PATH_BYTES (32u * 1024u * 1024u)
#define BW_MANIFEST_MAX_PATH 1017u
#define BW_MANIFEST_MAX_COMPONENT 255u
#define BW_MANIFEST_LOCAL 1u
#define BW_MANIFEST_STREAM 2u

/* Little-endian wire format; never cast untrusted bytes to native structs.
 * Header <8sIIIIQQ>: magic, version, recordSize, count, flags(0), pathBytes,
 * standard FNV-1a64(records + path pool). Record <IIIIQQ>: pathOffset,
 * pathLength excluding NUL, flags(LOCAL or STREAM), reserved(0), size, mtimeSec.
 * The path pool consists of NUL-terminated UTF-8 strings, each referenced once.
 * Offsets may reorder those strings. File IDs are zero-based record indices.
 */
enum bw_manifest_error {
    BW_MANIFEST_OK = 0,
    BW_MANIFEST_ARGUMENT,
    BW_MANIFEST_HEADER,
    BW_MANIFEST_VERSION_ERROR,
    BW_MANIFEST_LIMIT,
    BW_MANIFEST_LENGTH,
    BW_MANIFEST_CHECKSUM,
    BW_MANIFEST_RECORD,
    BW_MANIFEST_PATH,
    BW_MANIFEST_UTF8,
    BW_MANIFEST_DUPLICATE,
    BW_MANIFEST_PATH_CONFLICT,
    BW_MANIFEST_NO_MEMORY
};

struct bw_manifest {
    /* Validated view; caller must not modify these fields or the backing bytes. */
    const uint8_t *_records;
    const char *_paths;
    uint32_t count;
    uint32_t path_bytes;
    uint64_t checksum;
};

struct bw_manifest_record {
    const char *path;
    uint32_t path_length, flags;
    uint64_t size, mtime_sec;
};

/* No retained allocation. Backing bytes must remain alive and immutable while
 * using the view. Validation uses a temporary hash table of at most 4 MiB.
 * Failure clears view. Maximum input size is 40 + 131072*32 + 32MiB bytes.
 */
enum bw_manifest_error bw_manifest_open(struct bw_manifest *view,
                                        const void *bytes, size_t size);
enum bw_manifest_error bw_manifest_get(const struct bw_manifest *view,
                                       uint32_t file_id,
                                       struct bw_manifest_record *record);
const char *bw_manifest_path(const struct bw_manifest *view, uint32_t file_id,
                             uint32_t *path_length);
uint64_t bw_manifest_fnv1a64(const void *bytes, size_t size);
/* Shared source-path validation; identical to the manifest namespace rules. */
enum bw_manifest_error bw_manifest_validate_path(const char *, size_t);
const char *bw_manifest_error_string(enum bw_manifest_error error);

#endif
