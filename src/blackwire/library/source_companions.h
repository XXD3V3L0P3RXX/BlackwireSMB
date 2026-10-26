/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_SOURCE_COMPANIONS_H
#define BW_SOURCE_COMPANIONS_H
#include "source_identity.h"
#include <stddef.h>
#include <stdint.h>

#define BW_COMPANION_NS_FAKELIB 1u
#define BW_COMPANION_NS_FAKELIB2 2u
#define BW_COMPANION_RECEIPT_PATH "blackwire.companions"
#define BW_COMPANION_ORIGINAL_MAP_NAME "source.original.files"
#define BW_COMPANION_ORIGINAL_DIGEST_NAME "source.original.digests"
#define BW_COMPANION_RECEIPT_LIMIT (1024u * 1024u)
#define BW_COMPANION_ROW_LIMIT 4096u
#define BW_COMPANION_POOL_LIMIT (512u * 1024u)
#define BW_COMPANION_HEADER_BYTES 384u
#define BW_COMPANION_ROW_BYTES 80u
enum bw_companion_role {BW_COMPANION_COPY=1,BW_COMPANION_ALIAS=2,BW_COMPANION_READER_SLOT=3};
struct bw_companion_roots {uint64_t game_dev,game_ino,state_dev,state_ino;};
struct bw_companion_binding {
    const struct bw_provider_binding *provider;
    const char *prepare_run;
    const uint8_t *source_generation_sha256;
    struct bw_companion_roots roots;
};
/* Original tables are already sealed by the held provider. They remain
 * borrowed and immutable until the resulting projection/view is cleared.
 * Presence flags come from its typed namespace observer, including EMPTY_DIR;
 * deriving presence from filenames alone would lose loader precedence. */
struct bw_companion_source_tables {const void *map;size_t map_size;const void *digests;size_t digest_size;};
/* Only copied from an independently verified parent prepared record. Computing
 * these from an arbitrary target is not ownership/admission authority. */
struct bw_companion_prepared_seal {uint8_t map_sha256[32],digests_sha256[32];};
struct bw_companion_projection;
struct bw_companion_receipt;
struct bw_companion_tables {
    const void *map;size_t map_size;const void *digests;size_t digest_size;
    const void *receipt;size_t receipt_size;
    uint32_t local_count,stream_count;
    uint64_t local_bytes,remote_bytes;
};
struct bw_companion_member {
    uint32_t source_row,destination_row,role,source_namespace;
    uint64_t bytes;
    const uint8_t *sha256;
    const char *source_path,*destination_path;
    uint32_t source_path_size,destination_path_size;
};
/* Pure deterministic data transforms: no files, network, executable bytes or
 * platform calls. Return0=projection,1=no namespaces (legacy path),-1=invalid.
 * No output grants source freshness, launch, copy or deletion authority.
 * The supplied output owner must be NULL. All material source hashes are read from the ORIGINAL digest row. Generated
 * reader slot has no mutable reader hash: consumers must use compiled exact
 * current/predecessor reader qualification. Never adopt by filename alone. */
int bw_companion_projection_create(struct bw_companion_projection **,
    const struct bw_companion_binding *,const struct bw_companion_source_tables *,uint32_t presence_flags,const char **error);
int bw_companion_projection_tables(const struct bw_companion_projection *,struct bw_companion_tables *);
/* Preparation-only data views from this owned transform, before a parent
 * record exists. These do not bypass the sealed receipt API for consumers. */
uint32_t bw_companion_projection_count(const struct bw_companion_projection *);
uint32_t bw_companion_projection_namespace(const struct bw_companion_projection *);
int bw_companion_projection_member(const struct bw_companion_projection *,uint32_t,struct bw_companion_member *);
void bw_companion_projection_clear(struct bw_companion_projection **);
/* Caller first verifies prepared.bin's full physical-map/digest pins. This
 * routine verifies receipt's LOCAL digest slot, original-table pins and exact
 * deterministic projection relation. A stray receipt without that row refuses.
 * Input tables are borrowed; source/physical/receipt regions must not overlap.
 * Files supplying these bytes must separately remain named/held, no-follow,
 * regular single-link owned files with exact modes under the admitted roots. */
int bw_companion_receipt_open(struct bw_companion_receipt **,
    const struct bw_companion_binding *,const struct bw_companion_source_tables *,
    const struct bw_companion_tables *,const struct bw_companion_prepared_seal *,const char **error);
uint32_t bw_companion_receipt_count(const struct bw_companion_receipt *);
uint32_t bw_companion_receipt_namespace(const struct bw_companion_receipt *);
int bw_companion_receipt_member(const struct bw_companion_receipt *,uint32_t,struct bw_companion_member *);
void bw_companion_receipt_clear(struct bw_companion_receipt **);
#endif
