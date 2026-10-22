#ifndef BW_SOURCE_PROVIDER_H
#define BW_SOURCE_PROVIDER_H
#include "source_prepare.h"
#include "compressed_data.h"

#include "source_identity.h"
#define BW_PROVIDER_MAX_LOCAL_BYTES (5ULL * 1024u * 1024u * 1024u)
/* Borrowed immutable view backed by a typed live provider, never a fabricated
 * folder snapshot. The folder generation is its real sealed snapshot digest;
 * the image generation has a separate domain and explicit SMB file metadata.
 * Ready grants this data/copy operation only, never registration or Play. */
struct bw_provider_bundle {
    struct bw_source_map map;
    const uint8_t *digests;
    size_t digest_size;
    char source[BW_MANIFEST_MAX_PATH+1],game[75],title[10],selection[2048];
    uint8_t generation_sha256[32];
    unsigned ready;
};
struct bw_source_provider {
    struct bw_provider_binding identity;
    struct bw_provider_bundle bundle;
    struct bw_source_smb *source;
    struct bw_source_preparation folder;
    struct bw_compressed_data container;
    struct bw_container_ops image_ops;
    void *image_source;
    struct bw_container_smb *borrowed_image;
    struct smb2_stat_64 image_generation;
    uint64_t budget;
    int (*current)(void *);
    void *opaque;
    int failed,error,busy;
};
/* Folder identity delegates source_binding_resolve unchanged, including its
 * existing profile namespace. Container identity adds an explicit stable
 * source-kind/policy domain, so it cannot reuse an old folder receipt. */
/* Zero-initialize; stable addresses, exclusive borrowed connection/image,
 * no concurrency or callback reentry. The provider temporarily wraps image I/O
 * for current/deadline checks and restores it at destroy. Do not bind a STREAM
 * adapter while this preparation provider owns the image. After preparation,
 * destroy the provider and close its borrowed image; the reader must open a
 * fresh selected session and build its own manifest tokens. No simultaneous
 * preparation/reader view or live-token handoff is supported in this overlay.
 * Required current checks enforce job and
 * selection lifetime. No source writes. Only LOCAL startup bodies are read.
 * The caller supplies a budget <=5 GiB; allocations remain metadata + chunks.
 * Folder preparation/snapshot authority is retained unchanged and owned here.
 * Progress observers only copy/coalesce borrowed numeric progress and never
 * reenter or retain pointers. They may race a selection cancellation: prepare
 * checks the required current guard again after its final VERIFIED callback.
 * bundle.ready alone never grants authority; every data/copy use validates the
 * typed live provider and the current selected job.
 */
int bw_source_provider_prepare(struct bw_source_provider *,const struct bw_provider_binding *,
    struct bw_source_smb *,struct bw_container_smb *,const char *run,uint64_t budget,
    int (*current)(void *),void *);
int bw_source_provider_current(struct bw_source_provider *);
int bw_source_provider_read(struct bw_source_provider *,const char *,bw_source_consume_fn,void *);
void bw_source_provider_destroy(struct bw_source_provider *);
#endif
