#ifndef BW_READER_SOURCE_H
#define BW_READER_SOURCE_H
#include "source_binding.h"
#include "../streamreader/filemap.h"

/* Stable-address, single-worker adapter. The existing file map still binds to
 * the exact console root leaf and owns its bounded handle pool. Only paths
 * passed to the read-only source operations are translated to the full PC key.
 * No credentials are retained. Destroy the file map before clearing this object
 * or destroying the borrowed connection. Zero-initialize before open. */
struct bw_reader_source {
    struct bw_source_binding _binding;
    struct bw_files *_files;
    struct bw_files_remote_ops _ops;
    void *_context;
    int _ready,_failed;
};
int bw_reader_source_open(struct bw_reader_source *,struct bw_files *,
    const struct bw_share_config *,const struct bw_source_binding *,
    const struct bw_files_remote_ops *,void *);
void *bw_reader_source_get(struct bw_reader_source *,uint32_t);
/* The production operations have no create/write capability. This is path and
 * existing file-map identity validation, not a complete launch preflight or
 * per-read content authentication. */
int bw_reader_source_smb_open(struct bw_reader_source *,struct bw_files *,
    const struct bw_share_config *,const struct bw_source_binding *,struct smb2_context *);

#include "reader_selection.h"
struct bw_derived_reader_claim {
    struct bw_provider_binding original;
    char prepared_game[75],run[33];
    uint32_t prepared_policy,manifest_count;
    uint64_t manifest_hash,eboot_size;
    uint8_t provenance_sha256[32],receipt_sha256[32],map_sha256[32],commitment_sha256[32];
};
/* Caller preflight owns actual terminal/source/LOCAL/destination admission.
 * This pure synchronous lease must compare the complete captured claim and
 * check cancellation/lifetime without source/SDK/filemap/reader reentry or
 * mutation. A hash/callback alone cannot establish trusted native ownership. */
typedef int (*bw_derived_reader_current_fn)(void *,const struct bw_derived_reader_claim *);
struct bw_derived_reader_request {
    const struct bw_reader_selection *selection;
    const void *provenance;size_t provenance_size;
    const uint8_t *admitted_receipt_sha256; /* 32 complete-terminal-byte SHA */
    bw_derived_reader_current_fn current;void *opaque;
};
struct bw_derived_reader_source {void *state;const char *error;unsigned busy,failed;};
/* Zero-init, stable address, single joined worker. Decode/copy bounded
 * historical provenance; recompute original binding from actual config.
 * Requires exact loaded composed map/root and complete admitted v6 claim.
 * Borrowed file map/ops/context must outlive all handles; callbacks must not
 * reenter or mutate reader/file-map/source state. No raw body/terminal/source
 * freshness validation is performed here. */
int bw_derived_reader_source_open(struct bw_derived_reader_source *,struct bw_files *,
    const struct bw_share_config *,const struct bw_derived_reader_request *,
    const struct bw_files_remote_ops *,void *);
void *bw_derived_reader_source_get(struct bw_derived_reader_source *,uint32_t);
/* Destroy bw_files first so all cached handles consume their exact borrowed
 * ops/context; keep this object/context alive until then. Refuses busy/bound
 * maps, never frees callback-live storage or silently rebinds. After a drained
 * map, releases/clears owned state but returns -1 if any close reported error;
 * close consumes its handle even on error. Busy/bound refusal retains state. */
int bw_derived_reader_source_destroy(struct bw_derived_reader_source *);
int bw_derived_reader_source_smb_open(struct bw_derived_reader_source *,struct bw_files *,
    const struct bw_share_config *,const struct bw_derived_reader_request *,struct smb2_context *);

#endif
