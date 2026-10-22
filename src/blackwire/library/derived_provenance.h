#ifndef BW_DERIVED_PROVENANCE_H
#define BW_DERIVED_PROVENANCE_H
#include "derived_material.h"
#define BW_DERIVED_PROVENANCE_LIMIT (3u*1024u*1024u)
/* Immutable caller snapshots ONLY; this codec calls no live/current API. */
struct bw_derived_provenance_input {
    const struct bw_derived_material_view *material;
    const struct bw_pp_plan *recipe;
    const uint8_t *base_map,*base_digests;
    size_t base_map_size,base_digest_size;
};
/* OWNED HISTORICAL FACTS, never a provider/material/grant/job/session handle.
 * Body bytes and parser observation counters are absent. No freshness,
 * authentication, destination permission, receipt or readiness is established. */
struct bw_derived_provenance_facts {
    struct bw_provider_binding original;
    struct bw_pp_plan recipe; /* read_bytes/read_calls/current_checks == 0 */
    struct bw_derived_plan_view plan; /* historical const metadata only */
    const uint8_t *base_map,*base_digests;
    size_t base_map_size,base_digest_size;
    uint32_t material_policy;
    uint8_t material_policy_sha256[32],mapping_rule_sha256[32],material_sha256[32];
};
struct bw_derived_provenance {void *state;const char *error;};
/* Failure returns NULL/0. Caller owns/frees successful encoded bytes.
 * Caller allocations/lengths must be addressable and immutable; no concurrent
 * mutation or arbitrary-invalid-pointer safety is promised. */
int bw_derived_provenance_encode(const struct bw_derived_provenance_input *,uint8_t **,size_t *,const char **);
/* Zero initialize. Initial failure publishes no facts. Reuse refuses and hides
 * the current view; earlier borrowed pointers remain historical until destroy.
 * Owns bounded copy plus origin array, not inputs. No concurrent calls. */
int bw_derived_provenance_decode(struct bw_derived_provenance *,const void *,size_t);
const struct bw_derived_provenance_facts *bw_derived_provenance_view(const struct bw_derived_provenance *);
void bw_derived_provenance_destroy(struct bw_derived_provenance *);
#endif
