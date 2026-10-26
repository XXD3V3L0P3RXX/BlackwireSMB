#ifndef BW_PRELOAD_COMPANIONS_INTERNAL_H
#define BW_PRELOAD_COMPANIONS_INTERNAL_H
/* Private representation: only the held startup admission mints this after
 * the sealed physical receipt/original tables have been verified. Execution
 * also requires that same admission's original-source generation proof.
 * The separate removal purpose is LOCAL-only and never grants execution. */
#include "source_companions.h"
#include "preload_source_ampr_internal.h"
#include <string.h>
struct bw_preload_companions {
    char root[1024];
    const struct bw_companion_receipt *receipt;
    unsigned purpose;
    int (*current_owner)(void *);
    void *owner;
};
/* Separate legacy-removal proof, never a projection or execution witness.
 * Each row ID belongs to the already-held physical BWNAT001 map/dig/index.
 * STREAM rows describe owned empty sparse placeholders, not executable data. */
struct bw_preload_legacy_companions {
    char root[1024];
    const struct bw_manifest *manifest;
    const uint8_t *digests;size_t digest_size;
    const uint32_t *rows;uint32_t count;
    unsigned purpose;
    int (*current_owner)(void *);void *owner;
};
static inline int bw_preload_companions_current(const struct bw_preload_companions *w,
    const char *root,unsigned purpose){
    if(!w||!root||!memchr(w->root,0,sizeof w->root)||strcmp(w->root,root)||
       !w->owner||!w->current_owner||w->purpose!=purpose||
       (purpose!=BW_SOURCE_AMPR_EXECUTION&&purpose!=BW_SOURCE_AMPR_REMOVAL)||
       !w->receipt||!bw_companion_receipt_count(w->receipt)||
       !bw_companion_receipt_namespace(w->receipt))return -1;
    return w->current_owner(w->owner);
}
static inline int bw_preload_legacy_companions_current(const struct bw_preload_legacy_companions *w,const char *root){
    if(!w||!root||!memchr(w->root,0,sizeof w->root)||strcmp(w->root,root)||w->purpose!=BW_SOURCE_AMPR_REMOVAL||
       !w->manifest||!w->digests||w->digest_size!=56u+32u*(size_t)w->manifest->count||
       !w->rows||!w->count||w->count>256u||!w->owner||!w->current_owner)return -1;
    return w->current_owner(w->owner);
}
#endif
