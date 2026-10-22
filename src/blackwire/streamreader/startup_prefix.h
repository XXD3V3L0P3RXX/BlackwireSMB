#ifndef BW_STARTUP_PREFIX_H
#define BW_STARTUP_PREFIX_H
#include "manifest.h"
#include "stage_writer.h"

#define BW_PREFIX_MAX (32u * 1024u * 1024u)
#define BW_PREFIX_BUDGET_MAX (UINT64_C(5) * 1024u * 1024u * 1024u)
#define BW_PREFIX_HEADER 64u
#define BW_PREFIX_RECORD 48u

/* BWPRE001 header: <8sII32sQQ>, version=1, count, manifest SHA-256,
 * maximum resident-data budget, total prefix bytes. Each <IIQ32s> record is
 * file ID, reserved=0, prefix length, SHA-256 of source [0,length).
 * Strictly increasing IDs, STREAM only, length<=32MiB and <=file size.
 * LOCAL bytes plus all prefixes must fit the budget. Physical allocation and
 * metadata require additional accounting by the filesystem adapter.
 */
struct bw_prefix_plan {
    const uint8_t *records;
    uint32_t count;
    uint64_t budget,total_bytes,local_bytes;
};
struct bw_prefix_entry { uint32_t file_id; uint64_t length; uint8_t sha256[32]; };
int bw_prefix_plan_open(struct bw_prefix_plan *,const void *,size_t,
                        const void *manifest_bytes,size_t manifest_length);
int bw_prefix_plan_get(const struct bw_prefix_plan *,uint32_t,struct bw_prefix_entry *);

enum bw_prefix_error {
    BW_PREFIX_OK=0, BW_PREFIX_ARGUMENT, BW_PREFIX_SOURCE_HASH,
    BW_PREFIX_IDENTITY, BW_PREFIX_JOURNAL, BW_PREFIX_WRITE,
    BW_PREFIX_SYNC, BW_PREFIX_READBACK, BW_PREFIX_READBACK_HASH
};
enum bw_prefix_guard { BW_PREFIX_EMPTY=0, BW_PREFIX_WRITING=1, BW_PREFIX_VERIFYING=2 };
enum bw_prefix_note { BW_PREFIX_INTENT=1, BW_PREFIX_COMMIT=2 };
struct bw_prefix_result {
    enum bw_prefix_error error;
    uint64_t written,verified;
    int intent_persisted,commit_persisted;
};
struct bw_prefix_ops {
    /* guard returns zero only while the selected namespace, pinned regular
     * single-link inode, exact full size, lock, idle state, generation and
     * deadline remain valid. EMPTY additionally requires zero allocated data.
     * The caller owns the complete immutable source buffer until return.
     * note must persist and fsync its append-only journal before success.
     */
    int (*guard)(void *,int,enum bw_prefix_guard);
    int (*note)(void *,enum bw_prefix_note,uint64_t,uint64_t);
    bw_stage_pwritev_fn write;
    ssize_t (*read)(void *,int,void *,size_t,uint64_t);
    int (*sync)(void *,int);
};
/* Writes only [0,length) of an already existing, independently verified empty
 * STREAM inode. Never opens, creates, resizes, renames, unlinks or launches.
 * No source read or write: buffer and expected hash are supplied by adapter.
 * All source bytes are hashed before any callback/writes. Every write is
 * guarded and bounded, then fsynced and fully read back before COMMIT.
 * Failure can leave a correct partial prefix. Retain the journal and mark the
 * preparation incomplete; never auto-launch or retry an ambiguous inode.
 */
struct bw_prefix_result bw_prefix_apply(int fd,uint64_t full_size,
        const void *source,size_t length,const uint8_t expected_sha[32],
        const struct bw_prefix_ops *,void *opaque);
#endif
