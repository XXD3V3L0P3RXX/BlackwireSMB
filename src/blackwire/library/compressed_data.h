#ifndef BW_COMPRESSED_DATA_CANDIDATE_H
#define BW_COMPRESSED_DATA_CANDIDATE_H
#include "source_binding.h"
#include "source_plan.h"
#include "../streamreader/container_manifest.h"
#include "../streamreader/container_smb.h"

#define BW_COMPRESSED_DATA_CHUNK 65536u
#define BW_COMPRESSED_DATA_MAX_FILES 4096u
#define BW_COMPRESSED_DATA_MAX_PATH_BYTES (1024u * 1024u)
#define BW_COMPRESSED_DATA_MAX_LOCAL_BYTES (5ULL * 1024u * 1024u * 1024u)

enum bw_compressed_data_result {
    BW_COMPRESSED_DATA_STATE=-64, BW_COMPRESSED_DATA_BINDING=-65,
    BW_COMPRESSED_DATA_PLAN=-66, BW_COMPRESSED_DATA_BUDGET=-67,
    BW_COMPRESSED_DATA_TITLE=-68, BW_COMPRESSED_DATA_INTEGRITY=-69,
    BW_COMPRESSED_DATA_CONSUMER=-70, BW_COMPRESSED_DATA_LIMIT=-71,
    BW_COMPRESSED_DATA_CANCELLED=-72
};
typedef int (*bw_compressed_data_consume_fn)(void *,const void *,size_t);

/* DATA-ONLY CANDIDATE, deliberately not bw_source_preparation.
 * Zero-initialize, keep at a stable address and serialize all calls. The caller
 * exclusively owns the already-open readonly SMB image and its connection.
 * Its binding must come from that actual server/share/source/title selection;
 * this adapter additionally checks the exact image path and decoded title.
 * A binding is identity context, not a preparation or publication capability.
 *
 * Generic source_plan classification and the existing selected source profile
 * compose before hashing. Logical mtimes are
 * zero: PFS inode identity + the live outer SMB generation protect this view;
 * no synthetic folder snapshot, disk token, selection or receipt is produced.
 * The caller must not modify map/digests/files or reenter from a consumer.
 * All output is unusable after any failed source/data operation. A consumer
 * may have seen earlier chunks when a later check fails; discard the entire
 * attempted copy. No destination, extraction, cache or console writes occur.
 */
struct bw_compressed_data {
    struct bw_container_smb *image;
    struct bw_source_smb *progress;
    int (*current)(void *);
    void *current_opaque;
    uint64_t session,local_budget;
    struct bw_source_binding binding;
    struct bw_source_map map;
    struct bw_manifest manifest;
    struct bw_container_manifest files;
    uint8_t *digests;
    size_t digest_size;
    uint8_t map_sha256[32],digests_sha256[32];
    uint8_t profile_sha256[32];
    uint64_t hashed_bytes;
    uint32_t hashed_files;
    int ready,failed,error,busy;
    uint8_t buffer[BW_COMPRESSED_DATA_CHUNK];
};

/* Enumerate and bind supported logical files, validate bounded param.json,
 * and hash every generic LOCAL record into the existing BWDIG001 format.
 * local_budget must be nonzero and <=5 GiB. Reject an over-budget plan
 * before reading startup file bodies. Each later copy is bounded by this plan.
 * A failed prepare releases all partial outputs; destroy before trying again.
 * Admits only the existing container parser's unsigned contiguous D32 PFS
 * layouts. exFAT, signed/encrypted/scattered/64-bit-inode inputs and compressed
 * individual inner files remain unsupported. No extension-based admission.
 */
int bw_compressed_data_prepare(struct bw_compressed_data *,struct bw_container_smb *,
                             const struct bw_source_binding *,uint64_t local_budget,
                             struct bw_source_smb *,int (*)(void *),void *);
int bw_compressed_data_current(struct bw_compressed_data *);
/* Whole LOCAL file only, selected by immutable manifest ID. Synchronous borrowed
 * chunks <=64 KiB. Rehashes the consumed bytes against preparation's digest.
 * STREAM bodies are deliberately refused by this startup-copy proof.
 */
int bw_compressed_data_copy_local(struct bw_compressed_data *,uint32_t,
                                 bw_compressed_data_consume_fn,void *);
void bw_compressed_data_destroy(struct bw_compressed_data *);
#endif
