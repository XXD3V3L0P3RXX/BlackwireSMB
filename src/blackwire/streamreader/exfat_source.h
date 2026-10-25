#ifndef BW_EXFAT_SOURCE_H
#define BW_EXFAT_SOURCE_H
#include "pfs_source.h"
/* Fixed read-only workspace. The Unicode up-case map is decoded once, not on
 * every asset read. File tokens retain their existing 32-byte representation. */
struct bw_exfat {
    bw_pfs_pread_fn pread;
    void *source;
    uint64_t size,fat,heap,bitmap,bitmap_size,root_size,cached_page,upcase_base,upcase_size;
    uint32_t cluster_size,clusters,root;
    uint16_t upcase[65536];
    uint8_t cache[4096];
    size_t cached_bytes;
    int ready,cache_valid,namespace_validated;
};
#endif
