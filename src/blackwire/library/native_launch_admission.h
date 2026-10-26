#ifndef BW_NATIVE_LAUNCH_ADMISSION_H
#define BW_NATIVE_LAUNCH_ADMISSION_H
#include "native_send.h"
#include "reader_selection.h"

/* Internal, single joined Play owner. This is deliberately not a persisted
 * capability or a reader file-format change. A Ready folder/container receipt seals the
 * strict Send result; normal Play checks its bounded metadata and required
 * boot/module bytes once, then retains their named/held identities. It does
 * not walk the current PC tree or inspect every STREAM placeholder. */
struct bw_native_launch_admission;
struct bw_preload_source_ampr;
struct bw_preload_companions;
struct bw_preload_legacy_companions;
/* Borrowed execution-only input set for the one transient DATA envelope.
 * Export requires this admission's ORIGINAL source generation to be verified.
 * Consumers must finish before closing the admission; clear the value only,
 * never free these pointers. None of these raw fields grants authority alone. */
struct bw_native_projection_inputs {
    const void *prepared_record;size_t prepared_record_size;
    const void *original_map;size_t original_map_size;
    const void *original_digests;size_t original_digest_size;
    const void *physical_digests;size_t physical_digest_size;
    const void *receipt;size_t receipt_size;
    /* Controller validation only; already present as app0/blackwire.files,
     * so this map is not copied again into the transient envelope. */
    const void *physical_map;size_t physical_map_size;
};
int bw_native_launch_admission_projection_inputs(struct bw_native_launch_admission *,
    struct bw_native_projection_inputs *);
int bw_native_launch_admission_companions(struct bw_native_launch_admission *,
    const struct bw_preload_companions **);
int bw_native_launch_admission_companions_for_removal(struct bw_native_launch_admission *,
    const struct bw_preload_companions **);
/* 0=held all-mapped legacy source-fakelib2 inventory,1=no source rows,
 * -1=refusal. A failure never permits falling back to another selector. */
int bw_native_launch_admission_legacy_companions_for_removal(struct bw_native_launch_admission *,
    const struct bw_preload_legacy_companions **);
/* Deterministic pre-copy profile limit using the exact retained-file predicate
 * as launch admission. A format limit is not an increased live FD budget. */
int bw_native_launch_admission_budget(const void *,size_t,unsigned projection,const char **error);
/* -2 means failed-open descriptor cleanup was unconfirmed. Both purposes
 * consume the owner; callers must retain their sticky release-unknown hold. */
int bw_native_launch_admission_open(struct bw_native_launch_admission **,
    const char *,const struct bw_provider_binding *,const struct bw_native_prepared *,
    const char *run,int (*current)(void *),void *,const char **error);
/* Local ownership only, including a valid receipt marked for startup upgrade.
 * All sealed metadata and retained required bytes are still checked. This
 * purpose-bound owner cannot supply Play selection, source checks or execution
 * witnesses; the deletion core independently validates the full inventory.
 * -2 means owned-descriptor cleanup could not be positively released; retain
 * the operation as uncertain and do not submit a mutation automatically. */
int bw_native_launch_admission_open_for_removal(struct bw_native_launch_admission **,
    const char *,const struct bw_provider_binding *,const struct bw_native_prepared *,
    const char *run,int (*current)(void *),void *,const char **error);
int bw_native_launch_admission_selection(struct bw_native_launch_admission *,struct bw_reader_selection *);
/* No body reads. Checks the original Play/settings guard, exact canonical
 * root/control names and retained required files including fine timestamps.
 * Any uncertainty poisons this one Play; a new Send/repair remains separate. */
int bw_native_launch_admission_current(struct bw_native_launch_admission *,
    const char *,const struct bw_provider_binding *,const struct bw_reader_selection *,
    struct bw_native_prepared *);
/* Exact identity getter for a LOCAL-only removal owner. This purpose cannot
 * supply execution selection, source checks, inputs or execution witnesses. */
int bw_native_launch_admission_current_for_removal(struct bw_native_launch_admission *,
    const char *,const struct bw_provider_binding *,const struct bw_reader_selection *,
    struct bw_native_prepared *);
/* Bounded source/param/boot/module checks on the already connected share.
 * Containers validate their namespace and exact sealed image generation;
 * unchanged remote startup bodies are not rehashed on every Play.
 * Durable generation is the Send seal, not a freshly reconstructed tree seal.
 * Unchanged early-reader per-request size/mtime + held/named fences remain. */
int bw_native_launch_admission_source(struct bw_native_launch_admission *,
    const struct bw_share_config *,struct bw_source_smb *,const struct bw_reader_selection *,
    uint8_t map_sha256[32],uint8_t generation_sha256[32],const char **error);
/* Read-only pinned full map digest from this held current receipt. This is
 * local identity evidence only; source() remains mandatory before handoff. */
int bw_native_launch_admission_map_sha256(struct bw_native_launch_admission *,uint8_t[32]);
/* Borrow the unique LOCAL source-Ampr row from this opaque held receipt. source()
 * must first prove the selected current folder/image generation. A witness also
 * records manifest absence and never adopts a local file's own unbound digest.
 * It remains valid only while this same admission is held/current; close only
 * after all bundle callbacks have returned. No persisted/public wire changes. */
int bw_native_launch_admission_source_ampr(struct bw_native_launch_admission *,
    const struct bw_preload_source_ampr **);
/* Same sealed LOCAL evidence, scoped solely to read-only removal selection.
 * No PC connection is required: the independent deletion core still rehashes
 * the owned manifest. This witness is refused by install/verify/upgrade APIs. */
int bw_native_launch_admission_source_ampr_for_removal(struct bw_native_launch_admission *,
    const struct bw_preload_source_ampr **);
/* Bounded same-worker connection to the exact selected configured share.
 * Checks the sealed source/image generation, releases the connection, and
 * poisons admission on uncertain release. No source writes or resealing. */
int bw_native_launch_admission_source_connect(struct bw_native_launch_admission *,
    const struct bw_share_config *,const struct bw_reader_selection *,double,const char **);
/* Consumes the owner, including on a close error. Call only after all borrowed
 * registration/preflight/reader callbacks have returned. No cross-Play cache. */
int bw_native_launch_admission_close(struct bw_native_launch_admission **);
#endif
