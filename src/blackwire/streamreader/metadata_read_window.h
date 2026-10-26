/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_METADATA_READ_WINDOW_H
#define BW_METADATA_READ_WINDOW_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BW_METADATA_WINDOW_PAGE 16384u
#define BW_METADATA_WINDOW_SLOTS 8u
#define BW_METADATA_WINDOW_BYPASS 65536u
#define BW_METADATA_WINDOW_MAX_READ (1024u*1024u)
/* Data-only, serialized read coalescing. The owner supplies lifetime and source
 * generation fences; this object grants no identity or transport authority.
 * Reset BEFORE every generation check, even a refused check. Never share an
 * instance between images. No output or page is authoritative after a refusal.
 * Storage is 128 KiB of data plus fixed bookkeeping, with no internal allocation.
 */
struct bw_metadata_read_window {
    uint8_t bytes[BW_METADATA_WINDOW_SLOTS][BW_METADATA_WINDOW_PAGE];
    uint64_t page[BW_METADATA_WINDOW_SLOTS];
    size_t size[BW_METADATA_WINDOW_SLOTS];
    uint8_t rank[BW_METADATA_WINDOW_SLOTS],valid;
};
typedef int (*bw_metadata_window_fetch_fn)(void *,uint64_t,void *,size_t);

static inline void bw_metadata_window_reset(struct bw_metadata_read_window *p){
    if(p)p->valid=0;
}
static inline void bw_metadata_window_touch(struct bw_metadata_read_window *p,unsigned slot,unsigned previous){
    for(unsigned i=0;i<BW_METADATA_WINDOW_SLOTS;i++)
        if(i!=slot&&(p->valid&(1u<<i))&&p->rank[i]<previous)p->rank[i]++;
    p->rank[slot]=0;
}
static inline int bw_metadata_window_read(struct bw_metadata_read_window *p,uint64_t image_size,
    uint64_t at,void *out,size_t size,bw_metadata_window_fetch_fn fetch,void *opaque){
    if(!fetch||(!out&&size)||size>BW_METADATA_WINDOW_MAX_READ||at>image_size||size>image_size-at){
        bw_metadata_window_reset(p);return -1;
    }
    if(!size)return 0;
    /* Whole large requests retain the previous exact transport chunking. */
    if(!p||size>=BW_METADATA_WINDOW_BYPASS){
        int result=fetch(opaque,at,out,size);if(result)bw_metadata_window_reset(p);return result;
    }
    for(size_t used=0;used<size;){
        uint64_t position=at+used,page=position-(position%BW_METADATA_WINDOW_PAGE);
        size_t within=(size_t)(position-page);unsigned slot=BW_METADATA_WINDOW_SLOTS;
        for(unsigned i=0;i<BW_METADATA_WINDOW_SLOTS;i++)
            if((p->valid&(1u<<i))&&p->page[i]==page){slot=i;break;}
        if(slot==BW_METADATA_WINDOW_SLOTS){
            for(unsigned i=0;i<BW_METADATA_WINDOW_SLOTS;i++)if(!(p->valid&(1u<<i))){slot=i;break;}
            if(slot==BW_METADATA_WINDOW_SLOTS){
                slot=0;for(unsigned i=1;i<BW_METADATA_WINDOW_SLOTS;i++)if(p->rank[i]>p->rank[slot])slot=i;
            }
            p->valid&=(uint8_t)~(1u<<slot);
            size_t length=image_size-page<BW_METADATA_WINDOW_PAGE?(size_t)(image_size-page):BW_METADATA_WINDOW_PAGE;
            int result=fetch(opaque,page,p->bytes[slot],length);
            if(result){bw_metadata_window_reset(p);return result;}
            /* Publish a page only after its entire exact fetch succeeded. */
            p->page[slot]=page;p->size[slot]=length;
            bw_metadata_window_touch(p,slot,BW_METADATA_WINDOW_SLOTS);
            p->valid|=(uint8_t)(1u<<slot);
        }else bw_metadata_window_touch(p,slot,p->rank[slot]);
        size_t take=p->size[slot]-within;if(take>size-used)take=size-used;
        memcpy((uint8_t *)out+used,p->bytes[slot]+within,take);used+=take;
    }
    return 0;
}
#endif
