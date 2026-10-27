/* Shared native preparation operations. The installed copier keeps its PC
 * transport; this translation-unit include reuses its filesystem checks and
 * publication code with an in-memory, verified SMB bundle instead. */
#define BW_COPY_EMBEDDED
#include "../streamreader/prepare_copy.c"
#include "source_provider.h"
#include "source_companions.h"
#include "source_namespace.h"
struct np_context {
    struct cp_plan *plan;
    struct bw_source_smb *source;
    const struct bw_provider_bundle *bundle;
    struct bw_source_provider *provider;
    int (*selection_current)(void *);
    void *selection;
    uint32_t sparse_created;
    uint64_t local_completed_bytes;
    struct bw_companion_projection *projection;
    struct bw_source_companion_namespace *source_namespace;
};
/* One explicit operation owns this translation unit; no detached workers. */
static struct np_context *np_active;
static int cp_refresh(const struct cp_plan *p){
    return !np_active||np_active->plan!=p||!np_active->bundle->ready||!cp_alive(p)||
        strcmp(p->selection,np_active->bundle->selection)||!np_active->selection_current||np_active->selection_current(np_active->selection)?
        v2_fail("Native preparation selection changed or expired"):0;
}
static int np_generation(struct np_context *ctx){
    return cp_refresh(ctx->plan)||(ctx->source_namespace?
        bw_source_companion_namespace_matches(ctx->source_namespace,ctx->provider):bw_source_provider_current(ctx->provider))||cp_refresh(ctx->plan)?
        v2_fail("Native preparation source generation changed or expired"):0;
}
static int np_read(struct np_context *ctx,uint32_t id,const char *path,bw_source_consume_fn consume,void *opaque){
    if(!ctx->projection)return bw_source_provider_read(ctx->provider,path,consume,opaque);
    if(!ctx->source_namespace||cp_refresh(ctx->plan))return -1;
    struct bw_companion_tables tables;if(bw_companion_projection_tables(ctx->projection,&tables))return -1;
    if(!strcmp(path,BW_COMPANION_RECEIPT_PATH)){
        for(size_t at=0;at<tables.receipt_size;){if(cp_refresh(ctx->plan))return -1;size_t n=tables.receipt_size-at;if(n>65536)n=65536;
            if(consume(opaque,(const uint8_t *)tables.receipt+at,n))return -1;at+=n;}
        return cp_refresh(ctx->plan);
    }
    for(uint32_t i=0;i<bw_companion_projection_count(ctx->projection);i++){
        struct bw_companion_member member;if(bw_companion_projection_member(ctx->projection,i,&member))return -1;
        if(member.destination_row!=id)continue;
        if(member.role!=BW_COMPANION_COPY&&member.role!=BW_COMPANION_ALIAS)return -1;
        if(member.source_path_size>BW_MANIFEST_MAX_PATH||member.destination_path_size!=strlen(path)||memcmp(member.destination_path,path,member.destination_path_size))return -1;
        char original[BW_MANIFEST_MAX_PATH+1];memcpy(original,member.source_path,member.source_path_size);original[member.source_path_size]=0;
        return bw_source_provider_read(ctx->provider,original,consume,opaque);
    }
    /* These copied bytes are provisional until np_generation rechecks the
     * complete typed source namespace before index/record publication. Original
     * reads still use each provider's held/named per-file/image fences.
     * Ordinary noncompanion rows retain their original path and digest. The
     * authoritative provider only supplies its exact original LOCAL members. */
    return bw_source_provider_read(ctx->provider,path,consume,opaque);
}
struct np_writer {struct cp_plan *plan;struct np_context *context;int root,parent,fd;const char *relative;uint64_t size;struct cp_sha hash;};
static int np_write(void *opaque,const void *bytes,size_t size){
    struct np_writer *w=opaque;
    /* Native Send binds selection_current to ns_current, which freshly checks
     * settings, the full idle authority and held state/source identities.
     * Qualify once before EACH actual write attempt, then recheck the named
     * parent after those callbacks. Short writes and EINTR acquire no grant
     * for their continuation; shared publication/control writes are unchanged. */
    size_t at=0;
    do{
        if(cp_refresh(w->plan)||!v2_directory_current(&w->plan->cfg,w->root,w->parent,w->relative))return -1;
        if(at==size)break; /* An empty consumer piece still checks its scope. */
        size_t want=size-at<65536?size-at:65536;
        ssize_t n=BW_COPY_WRITE(w->fd,(const uint8_t *)bytes+at,want);
        if(n<0&&errno==EINTR)continue;
        if(n<=0||(size_t)n>want)return -1;
        at+=(size_t)n;
    }while(at<size);
    cp_sha_update(&w->hash,bytes,size);w->size+=size;w->plan->copied_bytes+=size;
    bw_source_smb_progress(w->context->source,BW_SOURCE_COPYING,w->plan->verified_count,w->context->bundle->map.local_count,
        w->context->local_completed_bytes+w->size,w->context->bundle->map.local_bytes);return 0;
}
static int np_copy(struct np_context *ctx,int root,uint32_t id){
    struct cp_plan *p=ctx->plan;struct bw_manifest_record r;char leaf[256],parent_rel[1024],temp[128];
    cp_diagnostic_reset();cp_diagnostic.file_id=id;
    if(bw_manifest_get(&p->manifest,id,&r)||r.flags!=BW_MANIFEST_LOCAL||cp_refresh(p)||cp_games_stopped(BW_COPY_GAME_LOG))return -1;
    int parent=v2_parent(&p->cfg,root,r.path,1,leaf,parent_rel);if(parent<0)return -1;
    const uint8_t *expected=p->digests+56u+32u*id;
    int exists=cp_existing(&p->cfg,root,parent,parent_rel,leaf,r.size,expected);
    if(exists<=0){if(!exists)exists=cp_repair_execute(p,root,parent,parent_rel,leaf,&r,expected,id);close(parent);return exists;}
    int n=snprintf(temp,sizeof temp,".blackwire-native-%s-%u.tmp",p->cfg.run_id,id),fd=-1,bad=1,known=0;struct stat created;
    if(n<0||(size_t)n>=sizeof temp||!v2_directory_current(&p->cfg,root,parent,parent_rel))goto done;
    mode_t mode=(mode_t)bw_startup_mode(r.path);fd=v2_openat(parent,temp,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,mode);
    if(fd<0)goto done;
    known=!fstat(fd,&created);if(!known||cp_regular(parent,temp,fd,0,NULL)||fchmod(fd,mode))goto done;
    struct np_writer writer={.plan=p,.context=ctx,.root=root,.parent=parent,.fd=fd,.relative=parent_rel};cp_sha_init(&writer.hash);
    if(np_read(ctx,id,r.path,np_write,&writer)||writer.size!=r.size)goto done;
    uint8_t actual[32];cp_sha_final(&writer.hash,actual);memcpy(cp_diagnostic.source_sha,actual,32);cp_diagnostic.hash_flags|=2;
    if(memcmp(actual,expected,32)||cp_refresh(p)||fsync(fd)||cp_regular(parent,temp,fd,r.size,NULL)||
        cp_file_hash(fd,r.size,actual)||memcmp(actual,expected,32)||cp_publish(&p->cfg,root,parent,parent_rel,temp,leaf,fd,r.size,mode,expected,p->deadline))goto done;
    bad=0;
done:
    if(bad&&known)cp_diagnostic.cleanup_result=cp_cleanup_temp(&p->cfg,root,parent,parent_rel,temp,fd,&created);
    if(fd>=0&&close(fd))bad=1;
    close(parent);return bad?v2_fail("Native startup copy failed verification; existing files were preserved"):0;
}
static int np_sparse(struct np_context *ctx,int root,uint32_t id){
    struct cp_plan *p=ctx->plan;struct bw_manifest_record r;char leaf[256],relative[1024];
    if(bw_manifest_get(&p->manifest,id,&r)||r.flags!=BW_MANIFEST_STREAM||cp_refresh(p)||cp_games_stopped(BW_COPY_GAME_LOG))return -1;
    int parent=v2_parent(&p->cfg,root,r.path,1,leaf,relative);if(parent<0)return -1;
    int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0),result=-1;
    if(fd>=0){result=cp_regular(parent,leaf,fd,r.size,NULL)||!v2_directory_current(&p->cfg,root,parent,relative)?-1:0;close(fd);close(parent);return result;}
    if(errno!=ENOENT){close(parent);return v2_fail("A streamed destination is linked or unavailable");}
    fd=v2_openat(parent,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0644);struct stat created,after;int known=fd>=0&&!fstat(fd,&created);
    if(!known||cp_regular(parent,leaf,fd,0,NULL)||fchmod(fd,0644)||cp_refresh(p)||cp_games_stopped(BW_COPY_GAME_LOG)||
        ftruncate(fd,(off_t)r.size)||fsync(fd)||cp_regular(parent,leaf,fd,r.size,&after)||!v2_identity(&created,&after)||after.st_blocks||
        !v2_directory_current(&p->cfg,root,parent,relative)||fsync(parent))goto done;
    ctx->sparse_created++;result=0;
done:
    if(result&&known)cp_cleanup_temp(&p->cfg,root,parent,relative,leaf,fd,&created);
    if(fd>=0&&close(fd))result=-1;
    close(parent);return result?v2_fail("Sparse placeholder creation failed; existing files were preserved"):0;
}
static void np_put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void np_put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static int np_index(struct np_context *ctx,int root){
    struct cp_plan *p=ctx->plan;bw_source_smb_progress(ctx->source,BW_SOURCE_INDEXING,0,p->manifest.count,0,0);
    uint32_t slots=2;uint64_t pool=0;
    for(uint32_t i=0;i<p->manifest.count;i++){struct bw_manifest_record r;if(bw_manifest_get(&p->manifest,i,&r))return -1;pool+=7u+r.path_length;}
    while(slots<2*p->manifest.count)slots*=2;
    uint64_t paths_at=48u+24u*p->manifest.count,slots_at=(paths_at+pool+15)&~UINT64_C(15),size=slots_at+16u*slots;
    if(pool>32u*1024u*1024u||size>CP_MAX_INDEX)return v2_fail("Native index exceeds its bounded size");
    uint8_t *bytes=calloc(1,(size_t)size);if(!bytes)return -1;
    memcpy(bytes,"AMPRIDX3",8);np_put32(bytes+8,3);np_put32(bytes+12,24);np_put64(bytes+16,p->manifest.count);np_put64(bytes+24,pool);np_put64(bytes+32,slots_at);np_put32(bytes+40,16);np_put32(bytes+44,slots);
    size_t cursor=0;int bad=1;
    for(uint32_t i=0;i<p->manifest.count;i++){
        struct bw_manifest_record r;char leaf[256],relative[1024];if(bw_manifest_get(&p->manifest,i,&r)||cp_refresh(p))goto done;
        int parent=v2_parent(&p->cfg,root,r.path,0,leaf,relative);if(parent<0)goto done;
        int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);struct stat st;
        int invalid=fd<0||cp_regular(parent,leaf,fd,r.size,&st)||!v2_directory_current(&p->cfg,root,parent,relative);
        if(fd>=0&&close(fd))invalid=1;
        close(parent);if(invalid)goto done;
        uint8_t *row=bytes+48u+24u*i,*path=bytes+paths_at+cursor;uint32_t length=6u+r.path_length;
        memcpy(path,"/app0/",6);memcpy(path+6,r.path,r.path_length);np_put32(row,(uint32_t)cursor);np_put32(row+4,length);np_put64(row+8,r.size);np_put64(row+16,(uint64_t)st.st_mtime);cursor+=length+1;
        uint64_t hash=UINT64_C(1469598103934665603);
        for(uint32_t k=0;k<length;k++){uint8_t c=path[k];if(c>='A'&&c<='Z')c+=32;hash=(hash^c)*UINT64_C(1099511628211);}if(!hash)hash=1;
        uint32_t slot=(uint32_t)hash&(slots-1),duplicate=0;
        while(cp_u32(bytes+slots_at+16u*slot+8)){
            uint8_t *prior=bytes+slots_at+16u*slot;
            if(cp_u64(prior)==hash){np_put32(prior+12,cp_u32(prior+12)|1u);duplicate=1;}
            slot=(slot+1)&(slots-1);
        }
        uint8_t *entry=bytes+slots_at+16u*slot;np_put64(entry,hash);np_put32(entry+8,i+1);np_put32(entry+12,duplicate);
        bw_source_smb_progress(ctx->source,BW_SOURCE_INDEXING,i+1,p->manifest.count,0,0);
    }
    p->index=bytes;p->index_size=(size_t)size;cp_sha_bytes(bytes,p->index_size,p->index_sha);bad=0;
done:
    if(bad)free(bytes);return bad?-1:0;
}
static int np_select(struct np_context *ctx){
    struct cp_plan *p=ctx->plan;const struct bw_provider_bundle *b=ctx->bundle;
    if(np_active||!b->ready)return -1;
    char active[4097];int n=snprintf(active,sizeof active,"%sphase=copy\nmanifest_bytes=%zu\ndigests_bytes=%zu\nindex_bytes=0\nindex_sha256=%064u\n",b->selection,b->map.size,b->digest_size,0u);
    if(n<0||(size_t)n>=sizeof active||cp_parse(p,(uint8_t *)active,(size_t)n))return -1;
    /* Borrowed immutable bytes stay owned by the source bundle for this call. */
    p->map=b->map.bytes;p->digests=(uint8_t *)b->digests;np_active=ctx;
    if(cp_validate_map(p)||cp_refresh(p)){np_active=NULL;return -1;}
    return 0;
}
static int np_execute(struct np_context *ctx,int data,int root){
    struct cp_plan *p=ctx->plan;
    if(np_active!=ctx||np_generation(ctx)||cp_games_stopped(BW_COPY_GAME_LOG)||!v2_root_current(&p->cfg,root))return -1;
    bw_source_smb_progress(ctx->source,BW_SOURCE_PREFLIGHT,0,p->manifest.count,0,0);
    if(cp_preflight(p,root))return -1;
    bw_source_smb_progress(ctx->source,BW_SOURCE_PREFLIGHT,p->manifest.count,p->manifest.count,0,0);
    bw_source_smb_progress(ctx->source,BW_SOURCE_COPYING,0,ctx->bundle->map.local_count,0,ctx->bundle->map.local_bytes);
    for(uint32_t i=0;i<p->manifest.count;i++){
        struct bw_manifest_record r;if(bw_manifest_get(&p->manifest,i,&r))return -1;
        if(r.flags==BW_MANIFEST_LOCAL){if(np_copy(ctx,root,i))return -1;p->verified_count++;ctx->local_completed_bytes+=r.size;
            bw_source_smb_progress(ctx->source,BW_SOURCE_COPYING,p->verified_count,ctx->bundle->map.local_count,ctx->local_completed_bytes,ctx->bundle->map.local_bytes);}
    }
    if(np_generation(ctx)||cp_blob(&p->cfg,root,"blackwire.files",p->map,p->map_size,p->cfg.run_id,p->deadline))return -1;
    uint32_t sparse_verified=0;bw_source_smb_progress(ctx->source,BW_SOURCE_SPARSE,0,ctx->bundle->map.stream_count,0,0);
    for(uint32_t i=0;i<p->manifest.count;i++){
        struct bw_manifest_record r;if(bw_manifest_get(&p->manifest,i,&r))return -1;
        if(r.flags==BW_MANIFEST_STREAM){if(np_sparse(ctx,root,i))return -1;
            bw_source_smb_progress(ctx->source,BW_SOURCE_SPARSE,++sparse_verified,ctx->bundle->map.stream_count,0,0);}
    }
    if(np_generation(ctx)||v2_copy(p->phase,sizeof p->phase,"index")||np_index(ctx,root))return -1;
    bw_source_smb_progress(ctx->source,BW_SOURCE_INDEX_VERIFY,0,p->manifest.count,0,0);
    if(cp_index_rows(p,root))return -1;
    bw_source_smb_progress(ctx->source,BW_SOURCE_INDEX_VERIFY,p->manifest.count,p->manifest.count,0,0);
    if(cp_blob(&p->cfg,root,"ampr_emu.index",p->index,p->index_size,p->cfg.run_id,p->deadline)||np_generation(ctx)||cp_control(p,data,"stream.conf",p->selection))return -1;
    char verify[2100];int n=snprintf(verify,sizeof verify,"%smode=verify\n",p->selection);
    if(n<0||(size_t)n>=sizeof verify||cp_control(p,data,"register-v2.conf",verify)||np_generation(ctx))return -1;
    bw_source_smb_progress(ctx->source,BW_SOURCE_MEASURING,0,0,0,0);
    if(cp_receipt(p,data,root))return -1;
    bw_source_smb_progress(ctx->source,BW_SOURCE_PREPARED,p->manifest.count,p->manifest.count,ctx->bundle->map.local_bytes,ctx->bundle->map.local_bytes);
    return 0;
}
