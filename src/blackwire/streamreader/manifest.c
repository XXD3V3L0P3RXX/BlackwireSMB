#include "manifest.h"

#include <stdlib.h>
#include <string.h>

#define FNV_OFFSET UINT64_C(14695981039346656037)
#define FNV_PRIME UINT64_C(1099511628211)

struct path_slot { uint64_t hash; uint32_t id_plus_one; };

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}
uint64_t bw_manifest_fnv1a64(const void *bytes, size_t size) {
    const uint8_t *p = bytes;
    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < size; i++) hash = (hash ^ p[i]) * FNV_PRIME;
    return hash;
}
static uint8_t fold(uint8_t c) {
    return c >= 'A' && c <= 'Z' ? (uint8_t)(c + ('a' - 'A')) : c;
}
static uint64_t path_hash(const char *path, size_t size) {
    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < size; i++) hash = (hash ^ fold((uint8_t)path[i])) * FNV_PRIME;
    return hash;
}
static int path_equal(const char *a, const char *b, size_t size) {
    for (size_t i = 0; i < size; i++)
        if (fold((uint8_t)a[i]) != fold((uint8_t)b[i])) return 0;
    return 1;
}
static int valid_utf8(const uint8_t *p, size_t size) {
    for (size_t i = 0; i < size;) {
        uint32_t value, minimum;
        unsigned extra;
        uint8_t c = p[i++];
        if (c < 0x80) continue;
        if (c >= 0xc2 && c <= 0xdf) { extra = 1; value = c & 0x1f; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { extra = 2; value = c & 0x0f; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { extra = 3; value = c & 7; minimum = 0x10000; }
        else return 0;
        if (extra > size - i) return 0;
        for (unsigned j = 0; j < extra; j++) {
            c = p[i++];
            if ((c & 0xc0) != 0x80) return 0;
            value = (value << 6) | (c & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return 0;
    }
    return 1;
}
static enum bw_manifest_error valid_path(const char *path, size_t size) {
    size_t component = 0;
    if (!size || size > BW_MANIFEST_MAX_PATH || path[0] == '/' || path[size - 1] == '/') return BW_MANIFEST_PATH;
    for (size_t i = 0; i <= size; i++) {
        if (i == size || path[i] == '/') {
            size_t length = i - component;
            if (!length || length > BW_MANIFEST_MAX_COMPONENT ||
                (length == 1 && path[component] == '.') ||
                (length == 2 && path[component] == '.' && path[component + 1] == '.')) return BW_MANIFEST_PATH;
            component = i + 1;
        } else {
            uint8_t c = (uint8_t)path[i];
            if (c < 32 || c == 127 || c == '\\' || c == ':' || c == '=' || c == '|') return BW_MANIFEST_PATH;
        }
    }
    return valid_utf8((const uint8_t *)path, size) ? BW_MANIFEST_OK : BW_MANIFEST_UTF8;
}
static const char *record_path(const struct bw_manifest *view, uint32_t id, uint32_t *length) {
    const uint8_t *record = view->_records + (size_t)id * BW_MANIFEST_RECORD_SIZE;
    *length = le32(record + 4);
    return view->_paths + le32(record);
}
enum bw_manifest_error bw_manifest_validate_path(const char *path, size_t size) {
    if (!path) return BW_MANIFEST_ARGUMENT;
    return valid_path(path, size);
}
static uint32_t lookup(const struct bw_manifest *view, const struct path_slot *slots,
                       size_t mask, uint64_t hash, const char *path, uint32_t length) {
    size_t at = (size_t)hash & mask;
    while (slots[at].id_plus_one) {
        if (slots[at].hash == hash) {
            uint32_t other_length;
            const char *other = record_path(view, slots[at].id_plus_one - 1, &other_length);
            if (other_length == length && path_equal(path, other, length)) return slots[at].id_plus_one;
        }
        at = (at + 1) & mask;
    }
    return 0;
}
enum bw_manifest_error bw_manifest_open(struct bw_manifest *view,
                                        const void *bytes, size_t size) {
    const uint8_t *data = bytes;
    struct bw_manifest candidate;
    struct path_slot *slots = NULL;
    enum bw_manifest_error error = BW_MANIFEST_OK;
    if (!view) return BW_MANIFEST_ARGUMENT;
    memset(view, 0, sizeof *view);
    if (!data) return BW_MANIFEST_ARGUMENT;
    if (size < BW_MANIFEST_HEADER_SIZE) return BW_MANIFEST_LENGTH;
    if (memcmp(data, BW_MANIFEST_MAGIC, 8) || le32(data + 20)) return BW_MANIFEST_HEADER;
    if (le32(data + 8) != BW_MANIFEST_VERSION || le32(data + 12) != BW_MANIFEST_RECORD_SIZE)
        return BW_MANIFEST_VERSION_ERROR;
    uint32_t count = le32(data + 16);
    uint64_t path_bytes = le64(data + 24);
    if (!count || count > BW_MANIFEST_MAX_FILES || !path_bytes || path_bytes > BW_MANIFEST_MAX_PATH_BYTES)
        return BW_MANIFEST_LIMIT;
    uint64_t total = BW_MANIFEST_HEADER_SIZE + (uint64_t)count * BW_MANIFEST_RECORD_SIZE + path_bytes;
    if ((uint64_t)size != total) return BW_MANIFEST_LENGTH;
    uint64_t hash = bw_manifest_fnv1a64(data + BW_MANIFEST_HEADER_SIZE, size - BW_MANIFEST_HEADER_SIZE);
    if (hash != le64(data + 32)) return BW_MANIFEST_CHECKSUM;
    candidate._records = data + BW_MANIFEST_HEADER_SIZE;
    candidate._paths = (const char *)candidate._records + (size_t)count * BW_MANIFEST_RECORD_SIZE;
    candidate.count = count;
    candidate.path_bytes = (uint32_t)path_bytes;
    candidate.checksum = hash;

    /* Validate the whole pool, including bytes not reached by an offset. Each
     * string must be used exactly once; record order need not match pool order. */
    size_t strings = 0;
    for (size_t at = 0; at < candidate.path_bytes;) {
        const char *path = candidate._paths + at;
        const char *end = memchr(path, 0, candidate.path_bytes - at);
        if (!end) return BW_MANIFEST_PATH;
        error = valid_path(path, (size_t)(end - path));
        if (error) return error;
        strings++;
        at += (size_t)(end - path) + 1;
    }
    if (strings != count) return BW_MANIFEST_PATH;
    size_t capacity = 2;
    while (capacity < (size_t)count * 2) capacity *= 2;
    slots = calloc(capacity, sizeof *slots);
    if (!slots) return BW_MANIFEST_NO_MEMORY;
    for (uint32_t id = 0; id < count; id++) {
        const uint8_t *record = candidate._records + (size_t)id * BW_MANIFEST_RECORD_SIZE;
        uint32_t offset = le32(record), length = le32(record + 4), flags = le32(record + 8);
        if ((flags != BW_MANIFEST_LOCAL && flags != BW_MANIFEST_STREAM) || le32(record + 12) ||
            le64(record + 16) > INT64_MAX || le64(record + 24) > INT64_MAX) {
            error = BW_MANIFEST_RECORD; goto done;
        }
        if (offset >= path_bytes || !length || length > BW_MANIFEST_MAX_PATH ||
            length >= path_bytes - offset || (offset && candidate._paths[offset - 1] != 0) ||
            candidate._paths[offset + length] != 0 || memchr(candidate._paths + offset, 0, length)) {
            error = BW_MANIFEST_PATH; goto done;
        }
        const char *path = candidate._paths + offset;
        uint64_t key = path_hash(path, length);
        if (lookup(&candidate, slots, capacity - 1, key, path, length)) {
            error = BW_MANIFEST_DUPLICATE; goto done;
        }
        size_t at = (size_t)key & (capacity - 1);
        while (slots[at].id_plus_one) at = (at + 1) & (capacity - 1);
        slots[at].hash = key; slots[at].id_plus_one = id + 1;
    }
    /* A regular file cannot also be an ancestor directory, including under
     * ASCII case folding. Detect this with prefix lookups, not pairwise scans. */
    for (uint32_t id = 0; id < count; id++) {
        uint32_t length;
        const char *path = record_path(&candidate, id, &length);
        uint64_t key = FNV_OFFSET;
        for (uint32_t at = 0; at < length; at++) {
            if (path[at] == '/' && lookup(&candidate, slots, capacity - 1, key, path, at)) {
                error = BW_MANIFEST_PATH_CONFLICT; goto done;
            }
            key = (key ^ fold((uint8_t)path[at])) * FNV_PRIME;
        }
    }
    *view = candidate;
done:
    free(slots);
    return error;
}
enum bw_manifest_error bw_manifest_get(const struct bw_manifest *view,
                                       uint32_t file_id,
                                       struct bw_manifest_record *record) {
    if (record) memset(record, 0, sizeof *record);
    if (!view || !record || !view->_records || !view->_paths || file_id >= view->count) return BW_MANIFEST_ARGUMENT;
    const uint8_t *wire = view->_records + (size_t)file_id * BW_MANIFEST_RECORD_SIZE;
    record->path = record_path(view, file_id, &record->path_length);
    record->flags = le32(wire + 8);
    record->size = le64(wire + 16);
    record->mtime_sec = le64(wire + 24);
    return BW_MANIFEST_OK;
}
const char *bw_manifest_path(const struct bw_manifest *view, uint32_t file_id,
                             uint32_t *path_length) {
    struct bw_manifest_record record;
    if (path_length) *path_length = 0;
    if (bw_manifest_get(view, file_id, &record)) return NULL;
    if (path_length) *path_length = record.path_length;
    return record.path;
}
const char *bw_manifest_error_string(enum bw_manifest_error error) {
    static const char *const messages[] = {
        "ok", "invalid argument or record ID", "invalid manifest header",
        "unsupported manifest version or record size", "manifest limit exceeded",
        "manifest length mismatch", "manifest checksum mismatch", "invalid record fields",
        "invalid relative path or path pool", "invalid UTF-8 path", "duplicate path",
        "file/directory path conflict", "manifest validation allocation failed"
    };
    return (unsigned)error < sizeof messages / sizeof messages[0] ? messages[error] : "unknown manifest error";
}
