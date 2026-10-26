#include "source_namespace.h"
#include "../streamreader/copy_sha256.h"
#include <stdlib.h>
#include <string.h>
struct ns_scan {struct bw_source_namespace_view view;uint32_t children[2];int error;};
static int ns_node(struct ns_scan *s,const char *path,int directory,int special){
    int ns=bw_source_companion_path_namespace(path);if(ns<0)return s->error=-1;
    if(!ns)return 0;unsigned id=(unsigned)ns-1u;
    if(!strchr(path,'/')){
        if(s->view.kind[id])return s->error=-1;
        s->view.kind[id]=special?BW_SOURCE_NAMESPACE_LINK_OR_SPECIAL:directory?BW_SOURCE_NAMESPACE_EMPTY_DIRECTORY:BW_SOURCE_NAMESPACE_REGULAR_FILE;
        if(special)return s->error=-1;
    }else {if(s->children[id]==UINT32_MAX)return s->error=-1;s->children[id]++;}
    return 0;
}
static int ns_snapshot_node(void *opaque,const char *path,const struct bw_source_info *info){
    return ns_node(opaque,path,!!(info->attributes&0x10u),!!(info->attributes&0x440u));
}
static int ns_container_node(void *opaque,const char *path,const struct bw_pfs_inode *node){
    unsigned type=node->mode&0xf000u;
    return ns_node(opaque,path,type==0x4000u,type!=0x4000u&&type!=0x8000u);
}
static int ns_finish(struct ns_scan *s,struct bw_source_namespace_view *out){
    if(s->error)return -1;
    for(unsigned i=0;i<2;i++){
        if(s->children[i]&&s->view.kind[i]!=BW_SOURCE_NAMESPACE_EMPTY_DIRECTORY)return -1;
        if(s->view.kind[i]==BW_SOURCE_NAMESPACE_EMPTY_DIRECTORY){if(s->children[i])s->view.kind[i]=BW_SOURCE_NAMESPACE_DIRECTORY;s->view.presence_flags|=1u<<i;}
    }
    s->view.selected=s->view.presence_flags&2u?2u:s->view.presence_flags&1u?1u:0u;*out=s->view;return 0;
}
int bw_source_namespace_snapshot(const struct bw_source_snapshot *p,struct bw_source_namespace_view *out){
    if(!out)return -1;memset(out,0,sizeof *out);struct ns_scan scan={0};
    return bw_source_snapshot_visit(p,ns_snapshot_node,&scan)||ns_finish(&scan,out)?-1:0;
}
int bw_source_namespace_container(struct bw_container *p,struct bw_source_namespace_view *out){
    if(!out)return -1;memset(out,0,sizeof *out);struct ns_scan scan={0};
    return bw_container_walk_nodes(p,ns_container_node,&scan,NULL)||ns_finish(&scan,out)?-1:0;
}
struct bw_source_companion_namespace {
    struct bw_source_provider *owner;
    struct bw_source_namespace_view view;
    struct bw_provider_binding binding;
    uint8_t map[32],digests[32],generation[32];
    uint64_t image_session;
    size_t map_bytes,digest_bytes;
    int failed;
};
static int ns_provider_current(struct bw_source_companion_namespace *p){
    if(!p||p->failed||!p->owner||p->owner->plan_policy!=BW_SOURCE_PLAN_COMPANIONS_V1||
       p->owner->bundle.plan_policy!=BW_SOURCE_PLAN_COMPANIONS_V1||bw_source_provider_current(p->owner))return -1;
    struct bw_source_provider *owner=p->owner;uint8_t hash[32];
    if(memcmp(&p->binding,&owner->identity,sizeof p->binding)||owner->bundle.map.size!=p->map_bytes||owner->bundle.digest_size!=p->digest_bytes||
       memcmp(p->generation,owner->bundle.generation_sha256,32)||
       (owner->identity.kind==BW_PROVIDER_CONTAINER&&(!owner->borrowed_image||owner->borrowed_image->container.session!=p->image_session)))return -1;
    cp_sha_bytes(owner->bundle.map.bytes,owner->bundle.map.size,hash);if(memcmp(hash,p->map,32))return -1;
    cp_sha_bytes(owner->bundle.digests,owner->bundle.digest_size,hash);return memcmp(hash,p->digests,32)?-1:0;
}
int bw_source_companion_namespace_current(struct bw_source_companion_namespace *p){
    int result=ns_provider_current(p);if(result&&p)p->failed=1;return result;
}
int bw_source_companion_namespace_matches(struct bw_source_companion_namespace *p,const struct bw_source_provider *owner){
    return !p||p->owner!=owner?-1:bw_source_companion_namespace_current(p);
}
int bw_source_companion_namespace_view(struct bw_source_companion_namespace *p,struct bw_source_namespace_view *out){
    if(out)memset(out,0,sizeof *out);if(!out||bw_source_companion_namespace_current(p))return -1;*out=p->view;return 0;
}
int bw_source_provider_companion_namespace(struct bw_source_provider *owner,struct bw_source_companion_namespace **out,const char **error){
    const char *failure="The source companion namespace is unavailable, unsupported or changed.";if(error)*error=failure;
    if(!owner||!out||*out||owner->plan_policy!=BW_SOURCE_PLAN_COMPANIONS_V1||owner->bundle.plan_policy!=BW_SOURCE_PLAN_COMPANIONS_V1||bw_source_provider_current(owner))return -1;
    struct bw_source_companion_namespace *p=calloc(1,sizeof *p);if(!p)return -1;p->owner=owner;p->binding=owner->identity;
    p->map_bytes=owner->bundle.map.size;p->digest_bytes=owner->bundle.digest_size;
    cp_sha_bytes(owner->bundle.map.bytes,p->map_bytes,p->map);cp_sha_bytes(owner->bundle.digests,p->digest_bytes,p->digests);memcpy(p->generation,owner->bundle.generation_sha256,32);
    int result=-1;owner->busy=1;
    if(owner->identity.kind==BW_PROVIDER_FOLDER)result=bw_source_namespace_snapshot(&owner->folder.snapshot,&p->view);
    else if(owner->identity.kind==BW_PROVIDER_CONTAINER&&owner->borrowed_image){p->image_session=owner->borrowed_image->container.session;result=bw_source_namespace_container(&owner->borrowed_image->container,&p->view);}
    owner->busy=0;
    for(unsigned i=0;!result&&i<2;i++)if(p->view.kind[i]==BW_SOURCE_NAMESPACE_REGULAR_FILE||p->view.kind[i]==BW_SOURCE_NAMESPACE_LINK_OR_SPECIAL)result=-1;
    if(!result)result=bw_source_companion_namespace_current(p);
    if(result){free(p);return -1;}*out=p;if(error)*error=NULL;return 0;
}
void bw_source_companion_namespace_close(struct bw_source_companion_namespace **out){if(out&&*out){memset(*out,0,sizeof **out);free(*out);*out=NULL;}}
