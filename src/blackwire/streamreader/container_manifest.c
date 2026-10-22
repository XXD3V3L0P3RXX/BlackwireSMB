#include "container_manifest.h"
#include <stdlib.h>
#include <string.h>
_Static_assert(sizeof(struct bw_container_file)<=32,"Token population stays within4MiB");

struct mapping {
    struct bw_container_manifest *output;
    const struct bw_manifest *manifest;
    uint32_t *slots,capacity,matched;
    int error;
};
static uint32_t slot_for(const char *path,uint32_t capacity) {
    return (uint32_t)bw_manifest_fnv1a64(path,strlen(path))&(capacity-1);
}
static int map_file(void *opaque,const char *path,const struct bw_pfs_inode *node) {
    struct mapping *p=opaque;
    uint32_t at=slot_for(path,p->capacity);
    while (p->slots[at]) {
        uint32_t id=p->slots[at]-1;
        struct bw_manifest_record record;
        if (bw_manifest_get(p->manifest,id,&record)) {p->error=BW_PFS_ARGUMENT;return -1;}
        if (!strcmp(path,record.path)) {
            struct bw_container_file *token=&p->output->files[id];
            if (token->owner || record.size!=node->logical_size || (node->flags&1u)) {
                p->error=BW_CONTAINER_MANIFEST_MISMATCH;return -1;
            }
            *token=(struct bw_container_file){p->output->source,p->output->session,node->logical_size,node->id};
            p->matched++;return 0;
        }
        at=(at+1)&(p->capacity-1);
    }
    return 0;
}
void bw_container_manifest_destroy(struct bw_container_manifest *p) {
    if (p) {free(p->files);memset(p,0,sizeof *p);}
}
int bw_container_manifest_bind(struct bw_container_manifest *p,struct bw_container *source,const struct bw_manifest *manifest) {
    if (!p || p->files || !source || !manifest || !manifest->count || manifest->count>BW_MANIFEST_MAX_FILES)
        return BW_PFS_ARGUMENT;
    memset(p,0,sizeof *p);
    int result=bw_container_validate(source);if (result) return result;
    uint32_t capacity=2;
    while (capacity<manifest->count*2) capacity*=2;
    struct mapping map={p,manifest,NULL,capacity,0,0};
    map.slots=calloc(capacity,sizeof *map.slots);
    p->files=calloc(manifest->count,sizeof *p->files);
    if (!map.slots || !p->files) {result=BW_PFS_MEMORY;goto done;}
    p->source=source;p->session=source->session;p->count=manifest->count;
    p->manifest_checksum=manifest->checksum;p->allocation_bytes=manifest->count*sizeof *p->files;
    for (uint32_t id=0;id<manifest->count;id++) {
        struct bw_manifest_record record;
        if (bw_manifest_get(manifest,id,&record)) {result=BW_PFS_ARGUMENT;goto done;}
        uint32_t at=slot_for(record.path,capacity);
        while (map.slots[at]) at=(at+1)&(capacity-1);
        map.slots[at]=id+1;
    }
    result=bw_container_walk(source,map_file,&map,NULL);
    if (map.error) result=map.error;
    if (!result && map.matched!=manifest->count) result=BW_CONTAINER_MANIFEST_MISMATCH;
    if (!result && source->session!=p->session) result=BW_CONTAINER_STALE;
    if (!result) result=bw_container_validate(source);
done:
    free(map.slots);
    if (result) bw_container_manifest_destroy(p);
    return result;
}
const struct bw_container_file *bw_container_manifest_file(struct bw_container_manifest *p,uint32_t id) {
    if (!p || !p->files || id>=p->count || p->failed) return NULL;
    if (p->source->session!=p->session) {p->failed=1;return NULL;}
    int current=bw_container_validate(p->source);
    if (current) {if(current!=BW_CONTAINER_CANCELED)p->failed=1;return NULL;}
    return &p->files[id];
}
