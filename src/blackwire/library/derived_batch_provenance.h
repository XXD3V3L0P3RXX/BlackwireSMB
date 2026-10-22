#ifndef BW_DERIVED_BATCH_PROVENANCE_H
#define BW_DERIVED_BATCH_PROVENANCE_H
#include "derived_batch.h"
#define BW_BATCH_PROVENANCE_LIMIT (8u*1024u*1024u)
struct bw_batch_provenance {void *state;const char *error;};
/* BWDPR002 historical binary document only. V1 BWDPR001/3MiB and every legacy
 * codec stay unchanged. No body bytes, live pointer/job/grant/epoch, ready,
 * persistence or content-freshness claim enters this document.
 * Inputs must be addressable/immutable; no hostile C-pointer safety promised. */
int bw_batch_provenance_encode(const struct bw_batch_facts *,uint8_t **,size_t *,const char **);
int bw_batch_provenance_decode(struct bw_batch_provenance *,const void *,size_t);
const struct bw_batch_facts *bw_batch_provenance_view(const struct bw_batch_provenance *);
void bw_batch_provenance_destroy(struct bw_batch_provenance *);
#endif
