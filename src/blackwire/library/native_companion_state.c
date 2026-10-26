/* Included once by native_send.c. Shared local held-file ownership for
 * inspection/admission; no mutex acquisition, network, mutation or adoption. */
#include "source_companions.h"
#include "source_namespace.h"
struct nc_file {int fd,parent,valid;const char *name;struct stat identity;mode_t mode;};
struct ns_companion_state {
    int game,state,failed,release_unknown;
    struct prepare_v2 game_anchor,state_anchor;
    struct stat game_identity,state_identity;
    struct nc_file files[3];
    struct bw_provider_binding identity;
    char run[33];uint8_t generation_sha256[32],original_map_sha256[32],original_digests_sha256[32],receipt_sha256[32];
    uint8_t *original_map,*original_digests,*receipt_bytes;
    size_t original_map_size,original_digest_size,receipt_size;
    struct bw_manifest original_manifest;
    struct bw_companion_receipt *receipt;
    struct bw_companion_binding binding;
    int (*guard)(void *);void *opaque;double deadline;
};
static int nc_directory(int fd,const struct prepare_v2 *anchor,const struct stat *expected){
    struct stat held;return fd<0||!v2_root_current(anchor,fd)||fstat(fd,&held)||!S_ISDIR(held.st_mode)||
        held.st_dev!=expected->st_dev||held.st_ino!=expected->st_ino||held.st_mode!=expected->st_mode||held.st_uid!=expected->st_uid||held.st_gid!=expected->st_gid?-1:0;
}
static int ns_companion_state_current(struct ns_companion_state *p){
    if(!p)return 0;
    double at=cp_now();int bad=p->failed||at<0||at>=p->deadline||(p->guard&&p->guard(p->opaque))||
        nc_directory(p->game,&p->game_anchor,&p->game_identity)||nc_directory(p->state,&p->state_anchor,&p->state_identity);
    for(unsigned i=0;!bad&&i<3;i++)if(p->files[i].fd>=0){struct nc_file *f=p->files+i;struct stat held,named;
        bad=!f->valid||fstat(f->fd,&held)||cp_fstatat(f->parent,f->name,&named,AT_SYMLINK_NOFOLLOW)||
            !ns_same(&f->identity,&held)||!ns_same(&held,&named)||held.st_uid!=geteuid()||(held.st_mode&07777)!=f->mode;
    }
    if(bad)p->failed=1;return bad?-1:0;
}
static int ns_companion_state_close(struct ns_companion_state **owner){
    if(!owner)return -1;struct ns_companion_state *p=*owner;*owner=NULL;if(!p)return 0;int bad=p->release_unknown;
    for(unsigned i=0;i<3;i++)if(p->files[i].fd>=0&&close(p->files[i].fd))bad=1;
    bw_companion_receipt_clear(&p->receipt);free(p->receipt_bytes);free(p->original_map);free(p->original_digests);free(p);return bad?-1:0;
}
static int nc_read(struct ns_companion_state *p,unsigned id,int parent,const char *name,mode_t mode,size_t size,uint8_t **out,const uint8_t expected[32]){
    if(id>=3||!out||*out||!size||ns_companion_state_current(p))return -1;
    struct nc_file *f=p->files+id;f->fd=v2_openat(parent,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);f->parent=parent;f->name=name;f->mode=mode;
    if(f->fd<0||cp_regular(parent,name,f->fd,size,&f->identity)||f->identity.st_uid!=geteuid()||(f->identity.st_mode&07777)!=mode)return -1;
    f->valid=1;uint8_t *bytes=malloc(size);if(!bytes)return -1;struct cp_sha sha;cp_sha_init(&sha);
    for(size_t at=0;at<size;){if(ns_companion_state_current(p)){free(bytes);return -1;}size_t take=size-at;if(take>65536)take=65536;
        ssize_t n=pread(f->fd,bytes+at,take,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>take){free(bytes);return -1;}cp_sha_update(&sha,bytes+at,(size_t)n);at+=(size_t)n;}
    uint8_t hash[32];cp_sha_final(&sha,hash);if(memcmp(hash,expected,32)||ns_companion_state_current(p)){free(bytes);return -1;}*out=bytes;return 0;
}
/* 1=positively verified policy3, 0=legacy with all new control names absent.
 * The physical map/digests and parent record are independently pinned first.
 * Header lengths are used only as bounded read sizes after the receipt's full
 * LOCAL parent digest passed; no policy is granted until codec relation opens. */
static int ns_companion_state_open(struct ns_companion_state **out,const struct bw_provider_binding *identity,
    const uint8_t record[NS_RECORD_SIZE],const struct cp_plan *physical,int game,const struct prepare_v2 *game_anchor,
    int state,const struct prepare_v2 *state_anchor,int (*guard)(void *),void *opaque){
    if(!out||*out||!identity||!record||!physical||game<0||state<0||!game_anchor||!state_anchor)return -1;
    struct bw_manifest map;uint32_t receipt_id=UINT32_MAX;
    if(bw_manifest_open(&map,physical->map,physical->map_size))return -1;
    for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&map,i,&row))return -1;
        if(!strcmp(row.path,BW_COMPANION_RECEIPT_PATH)){if(receipt_id!=UINT32_MAX||row.flags!=BW_MANIFEST_LOCAL||row.size<BW_COMPANION_HEADER_BYTES||row.size>BW_COMPANION_RECEIPT_LIMIT)return -1;receipt_id=i;}}
    if(receipt_id==UINT32_MAX){struct stat st;
        if(!cp_fstatat(game,BW_COMPANION_RECEIPT_PATH,&st,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT||
           !cp_fstatat(state,BW_COMPANION_ORIGINAL_MAP_NAME,&st,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT||
           !cp_fstatat(state,BW_COMPANION_ORIGINAL_DIGEST_NAME,&st,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)return -1;
        return 0;
    }
    struct ns_companion_state *p=calloc(1,sizeof *p);if(!p)return -1;for(unsigned i=0;i<3;i++)p->files[i].fd=-1;
    p->game=game;p->state=state;p->game_anchor=*game_anchor;p->state_anchor=*state_anchor;p->identity=*identity;p->guard=guard;p->opaque=opaque;p->deadline=physical->deadline;
    memcpy(p->run,record+16,32);memcpy(p->generation_sha256,record+80,32);int result=-1;
    uint8_t hash[32];cp_sha_bytes(physical->map,physical->map_size,hash);if(memcmp(hash,record+112,32))goto done;
    cp_sha_bytes(physical->digests,physical->digest_size,hash);if(memcmp(hash,record+144,32)||physical->digest_size!=56u+32u*map.count)goto done;
    if(fstat(game,&p->game_identity)||fstat(state,&p->state_identity)||
       (uint64_t)p->game_identity.st_dev!=cp_u64(record+304)||(uint64_t)p->game_identity.st_ino!=cp_u64(record+312)||
       (uint64_t)p->state_identity.st_dev!=cp_u64(record+320)||(uint64_t)p->state_identity.st_ino!=cp_u64(record+328)||ns_companion_state_current(p))goto done;
    struct bw_manifest_record receipt_row;if(bw_manifest_get(&map,receipt_id,&receipt_row))goto done;p->receipt_size=(size_t)receipt_row.size;
    if(nc_read(p,0,game,BW_COMPANION_RECEIPT_PATH,(mode_t)bw_startup_mode(BW_COMPANION_RECEIPT_PATH),p->receipt_size,&p->receipt_bytes,physical->digests+56u+32u*receipt_id))goto done;
    memcpy(p->receipt_sha256,physical->digests+56u+32u*receipt_id,32);
    uint64_t mn=cp_u64(p->receipt_bytes+216),dn=cp_u64(p->receipt_bytes+224);
    if(mn<40||mn>CP_MAX_MANIFEST||dn<56||dn>56u+32u*BW_MANIFEST_MAX_FILES)goto done;
    p->original_map_size=(size_t)mn;p->original_digest_size=(size_t)dn;
    memcpy(p->original_map_sha256,p->receipt_bytes+152,32);memcpy(p->original_digests_sha256,p->receipt_bytes+184,32);
    if(nc_read(p,1,state,BW_COMPANION_ORIGINAL_MAP_NAME,0600,p->original_map_size,&p->original_map,p->original_map_sha256)||
       nc_read(p,2,state,BW_COMPANION_ORIGINAL_DIGEST_NAME,0600,p->original_digest_size,&p->original_digests,p->original_digests_sha256)||
       bw_manifest_open(&p->original_manifest,p->original_map,p->original_map_size))goto done;
    p->binding=(struct bw_companion_binding){.provider=&p->identity,.prepare_run=p->run,.source_generation_sha256=p->generation_sha256,
        .roots={cp_u64(record+304),cp_u64(record+312),cp_u64(record+320),cp_u64(record+328)}};
    struct bw_companion_source_tables original={p->original_map,p->original_map_size,p->original_digests,p->original_digest_size};
    struct bw_companion_tables tables={.map=physical->map,.map_size=physical->map_size,.digests=physical->digests,.digest_size=physical->digest_size,.receipt=p->receipt_bytes,.receipt_size=p->receipt_size};
    struct bw_companion_prepared_seal seal;memcpy(seal.map_sha256,record+112,32);memcpy(seal.digests_sha256,record+144,32);
    if(bw_companion_receipt_open(&p->receipt,&p->binding,&original,&tables,&seal,NULL)||ns_companion_state_current(p))goto done;
    *out=p;return 1;
done:
    if(ns_companion_state_close(&p))result=-2;return result;
}
static int ns_companion_state_provider(struct ns_companion_state *p,struct bw_source_provider *source){
    if(!p||!source||source->plan_policy!=BW_SOURCE_PLAN_COMPANIONS_V1||!source->bundle.ready||
       source->identity.kind!=p->identity.kind||source->identity.policy!=p->identity.policy||memcmp(source->identity.binding.sha256,p->identity.binding.sha256,32)||
       strcmp(source->bundle.source,p->identity.binding.source)||strcmp(source->bundle.game,p->identity.binding.game)||strcmp(source->bundle.title,p->identity.binding.title)||
       source->bundle.map.size!=p->original_map_size||source->bundle.digest_size!=p->original_digest_size||
       bw_source_provider_current(source)||memcmp(source->bundle.generation_sha256,p->generation_sha256,32)||ns_companion_state_current(p))return -1;
    uint8_t hash[32];cp_sha_bytes(source->bundle.map.bytes,source->bundle.map.size,hash);if(memcmp(hash,p->original_map_sha256,32))return -1;
    cp_sha_bytes(source->bundle.digests,source->bundle.digest_size,hash);return memcmp(hash,p->original_digests_sha256,32)?-1:0;
}
static int ns_companion_state_result(struct ns_companion_state *p,struct bw_native_prepared *out){
    if(!p||!p->receipt||!out||out->state!=BW_PREPARED_VALID||ns_companion_state_current(p))return -1;
    out->prepared_policy=3;memcpy(out->prepared_component,p->identity.binding.game,sizeof out->prepared_component);
    memcpy(out->provenance_sha256,p->receipt_sha256,32);out->provenance_size=p->receipt_size;return 0;
}
