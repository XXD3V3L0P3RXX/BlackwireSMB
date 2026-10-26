/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_READER_PROJECTION_H
#define BW_READER_PROJECTION_H
#include "reader_selection.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BW_READER_PROJECTION_HEADER 64u
#define BW_READER_PROJECTION_LIMIT (52u * 1024u * 1024u)
#define BW_READER_PROJECTION_LEAF "blackwire-reader.projection"
struct bw_reader_projection_parts {
    const void *prepared_record;size_t prepared_record_size;
    const void *original_map;size_t original_map_size;
    const void *original_digests;size_t original_digest_size;
    const void *physical_digests;size_t physical_digest_size;
    const void *receipt;size_t receipt_size;
};
/* Pure bounded framing, not an authority constructor. The controller must
 * obtain parts from its held execution admission after source verification.
 * The DATA worker independently verifies the complete chain below. No paths,
 * credentials, body execution, sockets, threads or cache allocation. */
int bw_reader_projection_encode(const struct bw_reader_projection_parts *,void **,size_t *);
struct bw_reader_projection;
/* Requires canonical v8 selection, complete physical map and envelope. Checks
 * selected record SHA, its full physical table pins, selected receipt SHA,
 * original source generation/root/run binding and canonical projection codec.
 * Returned numeric evidence is immutable; it is not a live file/Play lease.
 * Input files must also pass the caller's existing held/named input guards.
 * Output must be NULL. Inputs may be released after a successful call. */
int bw_reader_projection_open(struct bw_reader_projection **,const struct bw_reader_selection *,
    const void *physical_map,size_t,const void *envelope,size_t,const char **error);
int bw_reader_projection_source(const struct bw_reader_projection *,uint8_t map_sha256[32],
    uint8_t generation_sha256[32],uint64_t *image_mtime);
void bw_reader_projection_clear(struct bw_reader_projection **);
#ifdef __cplusplus
}
#endif
#endif
