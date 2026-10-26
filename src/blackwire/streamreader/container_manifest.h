#ifndef BW_CONTAINER_MANIFEST_H
#define BW_CONTAINER_MANIFEST_H
#include "container_source.h"

#define BW_CONTAINER_MANIFEST_MISMATCH (-34)
struct bw_container_manifest {
    struct bw_container_file *files;
    struct bw_container *source;
    uint64_t session,manifest_checksum;
    uint32_t count;
    size_t allocation_bytes;
    int failed;
};
/* Map an immutable, validated BWMAP002 view to a container in one namespace
 * traversal. IDs retain manifest order, independent of image directory order.
 * All LOCAL and STREAM paths must exist at their exact sizes; unrelated image
 * files may remain. No root-pak, extension, unique-size or title restriction.
 *
 * Zero-initialize before first bind; destroy before rebinding. A new failed
 * bind discards all partial tokens. Retained token allocation <=4 MiB, with
 * <=1 MiB temporary lookup slots, in addition to the source walk's limits.
 * No source mutation, extraction, cache allocation or process operations.
 */
int bw_container_manifest_bind(struct bw_container_manifest *,struct bw_container *,const struct bw_manifest *);
/* DATA-only mapping for a separately verified companion projection. LOCAL
 * copies/aliases and generated receipt rows are deliberately not looked up in
 * the remote image and never receive a DATA token. Every STREAM row must match
 * an exact regular remote path and size. This function grants no projection or
 * source authority: its caller must first verify the sealed physical tables,
 * companion receipt and ORIGINAL source generation. Never substitute this for
 * the legacy full-map binder during source CONTROL verification. */
int bw_container_manifest_bind_streams(struct bw_container_manifest *,struct bw_container *,const struct bw_manifest *);
const struct bw_container_file *bw_container_manifest_file(struct bw_container_manifest *,uint32_t);
void bw_container_manifest_destroy(struct bw_container_manifest *);
#endif
