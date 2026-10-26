#ifndef BW_PRELOAD_SOURCE_AMPR_INTERNAL_H
#define BW_PRELOAD_SOURCE_AMPR_INTERNAL_H
/* Private representation, not a client/persisted hash-input interface. Only
 * native_launch_admission.c mints production instances from its already-held
 * sealed LOCAL map/digest row and verified source generation. Borrowed instances
 * must never outlive that admission. The callback rechecks that exact owner;
 * it does not select a path or compute/reseal a hash from source material. */
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#define BW_PRELOAD_SOURCE_AMPR_MAX (4u * 1024u * 1024u)
enum {BW_SOURCE_AMPR_EXECUTION=1,BW_SOURCE_AMPR_REMOVAL=2};
struct bw_preload_source_ampr {
    char root[1024];
    uint64_t size;
    uint8_t sha256[32];
    unsigned present;
    unsigned purpose;
    int (*current_owner)(void *);
    void *owner;
};
static inline int bw_preload_source_ampr_current(const struct bw_preload_source_ampr *w,const char *root){
    if(!w||!root||!memchr(w->root,0,sizeof w->root)||strcmp(w->root,root)||
       !w->owner||!w->current_owner||w->present>1||
       (w->purpose!=BW_SOURCE_AMPR_EXECUTION&&w->purpose!=BW_SOURCE_AMPR_REMOVAL)||
       (w->present?(!w->size||w->size>BW_PRELOAD_SOURCE_AMPR_MAX):w->size!=0))return -1;
    return w->current_owner(w->owner);
}
#endif
