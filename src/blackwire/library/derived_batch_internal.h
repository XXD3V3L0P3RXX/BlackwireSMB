#ifndef BW_DERIVED_BATCH_INTERNAL_H
#define BW_DERIVED_BATCH_INTERNAL_H
#include "derived_batch.h"
#define DB_MAP_LIMIT (40u+BW_BATCH_MAX_RECORDS*(32u+241u))
#define DB_DIG_LIMIT (56u+32u*BW_BATCH_MAX_RECORDS)
struct db_product {struct bw_batch_facts f;uint8_t *map,*digests;struct bw_batch_origin *origins;};
int db_build(struct db_product *,const struct bw_provider_binding *,const void *,size_t,const void *,size_t,uint64_t,const struct bw_pbb_view *,const struct bw_pp_plan *,const char **);
int db_check(const struct bw_batch_facts *,const char **);
void db_release(struct db_product *);
#endif
