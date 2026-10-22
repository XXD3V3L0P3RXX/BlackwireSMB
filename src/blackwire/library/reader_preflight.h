#ifndef BW_READER_PREFLIGHT_H
#define BW_READER_PREFLIGHT_H
#include "native_send.h"
#include "reader_selection.h"
#include "reader_source.h"
#include "../tools/compressed_stream_candidate/compressed_stream.h"
struct bw_native_launch_admission;

/* Zero initialize; destroy before reuse. No provider/parser/grant transfers.
 * The reader freshly opens after preflight destroys its preparation owners.
 * Derived provenance is an owned immutable byte snapshot, not a live lease;
 * the reader must separately capture/validate the complete typed claim. */
struct bw_reader_data_admission {
    struct bw_provider_binding identity;
    struct smb2_stat_64 image;
    uint8_t map_sha256[32],generation_sha256[32];
    unsigned valid;
    uint8_t *provenance_bytes;
    size_t provenance_size;
    struct bw_derived_reader_claim derived_claim;
    uint8_t terminal_sha256[32];
};
void bw_reader_data_admission_destroy(struct bw_reader_data_admission *);

struct bw_reader_preflight {
    const char *data_root;
    const struct bw_share_config *connection;
    const struct bw_reader_selection *selected;
    struct bw_source_smb *source;
    int (*current)(void *);
    void *opaque;
    const char **error;
    struct bw_reader_data_admission *data_admission;
    /* Required for derived v6. This is the actual controller-owned pure DATA
     * lease and nonzero accepted Play epoch, never the outer I/O guard. */
    int (*data_current)(void *);
    void *data_opaque;
    uint64_t data_epoch;
    const char *game_log;
    /* Internal normal Ready FOLDER Play lease. Never decoded from a file or
     * supplied to derived/container/standalone paths. */
    struct bw_native_launch_admission *startup;
};
/* The caller owns the already-connected source and its deadline/observer.
 * Without startup, retains full current-PC hashing/namespace + LOCAL checks.
 * An explicitly borrowed normal Ready FOLDER startup admission instead reuses
 * the strict Send seal, required-startup hashes and bounded fresh PC evidence;
 * it does not claim a fresh whole-tree generation or all LOCAL bytes_verified.
 * Failure never retains a positive admission. No writes/registration/hooks. */
int bw_reader_preflight_run(const struct bw_reader_preflight *,struct bw_native_prepared *);
/* Stable-address, serialized data owner. Borrows immutable map and connected
 * SMB context; owns readonly image and metadata tokens, never another cache.
 * Close before map/SMB teardown. Never zero/reopen a live owner. Failures latch.
 * First cached use attaches the caller's freshly initialized EMPTY existing
 * cache, then validates HITS and misses. No foreign loaders while attached.
 * Every failed call discards ALL attempted output (also provisional bytes).
 * No source, console or cache-file writes; no process/game operations. */
struct bw_reader_container {
    struct bw_container_smb *image;
    struct bw_container_manifest files;
    struct bw_reader_data_admission admitted;
    struct bw_compressed_stream stream;
    const void *map_bytes;
    size_t map_size;
    int (*current)(void *);
    void *opaque;
    int ready,failed,busy;
};
int bw_reader_container_open(struct bw_reader_container *,struct smb2_context *,
    const struct bw_reader_data_admission *,const void *,size_t,int (*)(void *),void *);
int bw_reader_container_current(struct bw_reader_container *);
int bw_reader_container_boot(struct bw_reader_container *,uint64_t,uint8_t [32]);
int bw_reader_container_load(struct bw_reader_container *,struct bw_cache *,uint32_t,
    uint64_t,uint8_t *,size_t,size_t *);
int bw_reader_container_get(struct bw_reader_container *,struct bw_cache *,uint32_t,
    uint64_t,const uint8_t **,size_t *);
int bw_reader_container_close(struct bw_reader_container *);
#endif
