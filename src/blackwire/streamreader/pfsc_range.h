#ifndef BW_PFSC_RANGE_H
#define BW_PFSC_RANGE_H

#include <stddef.h>
#include <stdint.h>

#define BW_PFSC_BLOCK 65536u
#define BW_PFSC_TABLE_PAGE 4096u
#define BW_PFSC_MAX_READ (1024u * 1024u)
#ifndef BW_PFSC_READAHEAD
#define BW_PFSC_READAHEAD (256u * 1024u)
#endif
#if BW_PFSC_READAHEAD > BW_PFSC_MAX_READ
#error "PFSC read-ahead must fit one bounded source operation"
#endif
#define BW_PFSC_MAX_LOGICAL (8ULL << 40)

/* Exact read-only range I/O. The caller owns a stable source generation and
 * serializes calls. No callback may return success for a short read.
 * The codec must accept a zlib-wrapped stream, bound output to output_size,
 * verify its checksum/end marker and consume the entire input exactly.
 * Failed reads may have modified output; callers must not publish that data.
 */
typedef int (*bw_pfsc_pread_fn)(void *, uint64_t, void *, size_t);
typedef int (*bw_pfsc_inflate_fn)(void *, const void *, size_t, void *, size_t);

enum bw_pfsc_result {
    BW_PFSC_OK = 0, BW_PFSC_ARGUMENT = -1, BW_PFSC_FORMAT = -2,
    BW_PFSC_IO = -3, BW_PFSC_CODEC = -4, BW_PFSC_RANGE = -5
};

/* Fixed workspace: two 64 KiB block buffers, one 4 KiB metadata page and
 * a bounded physical batching window. Only consecutive decoded blocks within
 * the current requested range grow a fetch. No unrelated blocks are fetched.
 * No allocation, mounts, writes, process operations or console dependencies.
 * Codec state and the caller's result buffer are external, additional storage.
 */
struct bw_pfsc {
    bw_pfsc_pread_fn pread;
    bw_pfsc_inflate_fn inflate;
    void *source, *codec;
    uint64_t image_size, base, stored_size, logical_size, block_count;
    uint64_t table_bytes, data_start, table_page;
    uint64_t decoded_index;
    size_t page_size;
    int ready, page_valid, decoded_valid;
#if BW_PFSC_READAHEAD
    uint64_t window_at;size_t window_size;
    uint8_t window[BW_PFSC_READAHEAD];
#endif
    uint8_t packed[BW_PFSC_BLOCK];
    uint8_t decoded[BW_PFSC_BLOCK];
    uint8_t page[BW_PFSC_TABLE_PAGE];
};

int bw_pfsc_open(struct bw_pfsc *, bw_pfsc_pread_fn, void *,
                 bw_pfsc_inflate_fn, void *, uint64_t image_size,
                 uint64_t payload_base, uint64_t stored_size,
                 uint64_t inode_logical_size);
int bw_pfsc_read(struct bw_pfsc *, uint64_t offset, void *, size_t length);
int bw_pfsc_inflate_miniz(void *, const void *, size_t, void *, size_t);

#endif
