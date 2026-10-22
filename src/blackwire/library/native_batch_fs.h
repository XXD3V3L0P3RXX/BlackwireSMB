#ifndef BW_NATIVE_BATCH_FS_H
#define BW_NATIVE_BATCH_FS_H
#include <stdint.h>
/* Reconstructed/read-back mode5 diagnostics, not serialized receipt fields or
 * LOCAL payload accounting. Zero for historical modes1--4. The ceiling is the
 * union rounded outward to a fixed 64KiB policy unit, NOT filesystem geometry.
 * These bounds do not prove that every byte outside admitted spans is zero. */
struct bw_native_batch_metadata_accounting {
    uint64_t declared_bytes,unique_bytes,allocation_ceiling_bytes;
    uint64_t observed_allocated_bytes;
};
#endif
