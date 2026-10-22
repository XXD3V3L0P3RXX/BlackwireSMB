/* DATA-only native copier operations. The original filesystem implementation
 * is included unchanged; consume callbacks deliberately avoid its I/O guard. */
#define BW_COPY_EMBEDDED
#include "../streamreader/prepare_copy.c"
#include "native_derived_send.h"
struct dpo_context {
 struct cp_plan *plan;struct bw_source_smb *source;
 struct bw_source_provider *provider;struct bw_packed_project *project;
 struct bw_derived_local *local;struct bw_derived_material *material;
 const struct bw_derived_plan_view *view;
 int (*selection_current)(void *);void *selection;
 uint32_t sparse_created;uint64_t local_completed_bytes;
};
static struct dpo_context *dpo_active;
static int cp_refresh(const struct cp_plan *p){
 return !dpo_active||dpo_active->plan!=p||!cp_alive(p)||!dpo_active->selection_current||dpo_active->selection_current(dpo_active->selection)?-1:0;
}
static int dpo_generation(struct dpo_context *c){
 if(cp_refresh(c->plan)||bw_source_provider_current(c->provider)||bw_packed_project_revalidate(c->project)||bw_source_provider_current(c->provider)||cp_refresh(c->plan))return -1;
 const struct bw_derived_plan_view *v=bw_derived_local_view(c->local);
 if(!v||memcmp(v->identity.commitment_sha256,c->view->identity.commitment_sha256,32))return -1;
 if(c->material){const struct bw_derived_material_view *m=bw_derived_material_view(c->material);if(!m||m->plan!=v)return -1;}
 return 0;
}
struct dpo_writer {int fd;uint64_t size,expected;struct cp_sha hash;};
static int dpo_write(void *raw,const void *bytes,size_t size){
 struct dpo_writer *w=raw;if(w->size>w->expected||size>w->expected-w->size)return -1;
 /* No settings, observers, source/SDK APIs, path checks or progress here. */
 if(w->fd>=0){size_t at=0;while(at<size){size_t want=size-at<65536?size-at:65536;ssize_t n=write(w->fd,(const uint8_t *)bytes+at,want);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>want)return -1;at+=(size_t)n;}}
 cp_sha_update(&w->hash,bytes,size);w->size+=size;return 0;
}
static int dpo_copy(struct dpo_context *c,int root,uint32_t id){
 struct cp_plan *p=c->plan;struct bw_manifest_record r;char leaf[256],relative[1024],temp[128];
 if(bw_manifest_get(&p->manifest,id,&r)||r.flags!=BW_MANIFEST_LOCAL||cp_refresh(p)||v2_games_stopped(BW_COPY_GAME_LOG))return -1;
 int parent=v2_parent(&p->cfg,root,r.path,1,leaf,relative);if(parent<0)return -1;
 const uint8_t *expected=p->digests+56u+32u*id;int exists=cp_existing(&p->cfg,root,parent,relative,leaf,r.size,expected),fd=-1,bad=1,known=0;struct stat created;
 temp[0]=0;if(exists<0)goto done;
 if(exists){int n=snprintf(temp,sizeof temp,".blackwire-native-%s-%u.tmp",p->cfg.run_id,id);if(n<0||(size_t)n>=sizeof temp||!v2_directory_current(&p->cfg,root,parent,relative))goto done;
  mode_t mode=(mode_t)bw_startup_mode(r.path);fd=v2_openat(parent,temp,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,mode);if(fd<0)goto done;
  known=!fstat(fd,&created);if(!known||cp_regular(parent,temp,fd,0,NULL)||fchmod(fd,mode))goto done;}
 struct dpo_writer w={.fd=fd,.expected=r.size};cp_sha_init(&w.hash);
 /* Existing verified bytes still require an actual fresh source read/hash. */
 if(bw_derived_local_read(c->local,id,dpo_write,&w)||w.size!=r.size)goto done;
 uint8_t actual[32];cp_sha_final(&w.hash,actual);if(memcmp(actual,expected,32)||cp_refresh(p))goto done;
 if(exists){mode_t mode=(mode_t)bw_startup_mode(r.path);
  if(fsync(fd)||cp_regular(parent,temp,fd,r.size,NULL)||cp_file_hash(fd,r.size,actual)||memcmp(actual,expected,32)||cp_publish(&p->cfg,root,parent,relative,temp,leaf,fd,r.size,mode,expected,p->deadline))goto done;known=0;
 }else if(cp_existing(&p->cfg,root,parent,relative,leaf,r.size,expected)||cp_repair_execute(p,root,parent,relative,leaf,&r,expected,id))goto done;
 p->copied_bytes+=w.size;bad=0;
 done:if(bad&&known)cp_cleanup_temp(&p->cfg,root,parent,relative,temp,fd,&created);
 if(fd>=0&&close(fd))bad=1;if(close(parent))bad=1;return bad?-1:0;
}
static int dpo_sparse(struct dpo_context *ctx,int root,uint32_t id){
    struct cp_plan *p=ctx->plan;struct bw_manifest_record r;char leaf[256],relative[1024];
    if(bw_manifest_get(&p->manifest,id,&r)||r.flags!=BW_MANIFEST_STREAM||cp_refresh(p)||v2_games_stopped(BW_COPY_GAME_LOG))return -1;
    int parent=v2_parent(&p->cfg,root,r.path,1,leaf,relative);if(parent<0)return -1;
    int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0),result=-1;
    if(fd>=0){result=cp_regular(parent,leaf,fd,r.size,NULL)||!v2_directory_current(&p->cfg,root,parent,relative)?-1:0;if(close(fd))result=-1;if(close(parent))result=-1;return result;}
    if(errno!=ENOENT){close(parent);return v2_fail("A streamed destination is linked or unavailable");}
    fd=v2_openat(parent,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0644);struct stat created,after;int known=fd>=0&&!fstat(fd,&created);
    if(!known||cp_regular(parent,leaf,fd,0,NULL)||fchmod(fd,0644)||cp_refresh(p)||v2_games_stopped(BW_COPY_GAME_LOG)||
        ftruncate(fd,(off_t)r.size)||fsync(fd)||cp_regular(parent,leaf,fd,r.size,&after)||!v2_identity(&created,&after)||after.st_blocks||
        !v2_directory_current(&p->cfg,root,parent,relative)||fsync(parent))goto done;
    ctx->sparse_created++;result=0;
done:
    if(result&&known)cp_cleanup_temp(&p->cfg,root,parent,relative,leaf,fd,&created);
    if(fd>=0&&close(fd))result=-1;
    if(close(parent))result=-1;return result?v2_fail("Sparse placeholder creation failed; existing files were preserved"):0;
}
static void dpo_put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void dpo_put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static int dpo_index(struct dpo_context *ctx,int root){
    struct cp_plan *p=ctx->plan;bw_source_smb_progress(ctx->source,BW_SOURCE_INDEXING,0,p->manifest.count,0,0);
    uint32_t slots=2;uint64_t pool=0;
    for(uint32_t i=0;i<p->manifest.count;i++){struct bw_manifest_record r;if(bw_manifest_get(&p->manifest,i,&r))return -1;pool+=7u+r.path_length;}
    while(slots<2*p->manifest.count)slots*=2;
    uint64_t paths_at=48u+24u*p->manifest.count,slots_at=(paths_at+pool+15)&~UINT64_C(15),size=slots_at+16u*slots;
    if(pool>32u*1024u*1024u||size>BW_DERIVED_TERMINAL_INDEX_LIMIT)return v2_fail("Native index exceeds its bounded size");
    uint8_t *bytes=calloc(1,(size_t)size);if(!bytes)return -1;
    memcpy(bytes,"AMPRIDX3",8);dpo_put32(bytes+8,3);dpo_put32(bytes+12,24);dpo_put64(bytes+16,p->manifest.count);dpo_put64(bytes+24,pool);dpo_put64(bytes+32,slots_at);dpo_put32(bytes+40,16);dpo_put32(bytes+44,slots);
    size_t cursor=0;int bad=1;
    for(uint32_t i=0;i<p->manifest.count;i++){
        struct bw_manifest_record r;char leaf[256],relative[1024];if(bw_manifest_get(&p->manifest,i,&r)||cp_refresh(p))goto done;
        int parent=v2_parent(&p->cfg,root,r.path,0,leaf,relative);if(parent<0)goto done;
        int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);struct stat st;
        int invalid=fd<0||cp_regular(parent,leaf,fd,r.size,&st)||!v2_directory_current(&p->cfg,root,parent,relative);
        if(fd>=0&&close(fd))invalid=1;
        if(close(parent))invalid=1;if(invalid)goto done;
        uint8_t *row=bytes+48u+24u*i,*path=bytes+paths_at+cursor;uint32_t length=6u+r.path_length;
        memcpy(path,"/app0/",6);memcpy(path+6,r.path,r.path_length);dpo_put32(row,(uint32_t)cursor);dpo_put32(row+4,length);dpo_put64(row+8,r.size);dpo_put64(row+16,(uint64_t)st.st_mtime);cursor+=length+1;
        uint64_t hash=UINT64_C(1469598103934665603);
        for(uint32_t k=0;k<length;k++){uint8_t c=path[k];if(c>='A'&&c<='Z')c+=32;hash=(hash^c)*UINT64_C(1099511628211);}if(!hash)hash=1;
        uint32_t slot=(uint32_t)hash&(slots-1),duplicate=0;
        while(cp_u32(bytes+slots_at+16u*slot+8)){
            uint8_t *prior=bytes+slots_at+16u*slot;
            if(cp_u64(prior)==hash){dpo_put32(prior+12,cp_u32(prior+12)|1u);duplicate=1;}
            slot=(slot+1)&(slots-1);
        }
        uint8_t *entry=bytes+slots_at+16u*slot;dpo_put64(entry,hash);dpo_put32(entry+8,i+1);dpo_put32(entry+12,duplicate);
        bw_source_smb_progress(ctx->source,BW_SOURCE_INDEXING,i+1,p->manifest.count,0,0);
    }
    p->index=bytes;p->index_size=(size_t)size;cp_sha_bytes(bytes,p->index_size,p->index_sha);bad=0;
done:
    if(bad)free(bytes);return bad?-1:0;
}

static int dpo_select(struct dpo_context *c,const char *run,const char *title){
 struct cp_plan *p=c->plan;const struct bw_derived_plan_view *v=c->view;struct bw_manifest m;
 if(dpo_active||!v||bw_manifest_open(&m,v->map,v->map_size))return -1;
 uint64_t boot=0;for(uint32_t i=0;i<m.count;i++){struct bw_manifest_record r;if(bw_manifest_get(&m,i,&r))return -1;if(!strcmp(r.path,"eboot.bin"))boot=r.size;}
 char active[4097];int n=snprintf(active,sizeof active,"version=2\nrun_id=%s\ntitle_id=%s\ngame=%s\neboot_size=%"PRIu64"\nmanifest_count=%u\nmanifest_hash=%016"PRIx64"\nphase=copy\nmanifest_bytes=%zu\ndigests_bytes=%zu\nindex_bytes=0\nindex_sha256=%064u\n",run,title,v->identity.component,boot,m.count,m.checksum,v->map_size,v->digest_size,0u);
 if(n<0||(size_t)n>=sizeof active||cp_parse(p,(uint8_t *)active,(size_t)n))return -1;
 p->map=(uint8_t *)v->map;p->digests=(uint8_t *)v->digests;dpo_active=c;
 if(cp_validate_map(p)||cp_refresh(p)){dpo_active=NULL;return -1;}return 0;
}
static int dpo_execute(struct dpo_context *c,int root){
 struct cp_plan *p=c->plan;const struct bw_derived_plan_view *v=c->view;
 if(dpo_generation(c)||v2_games_stopped(BW_COPY_GAME_LOG)||!v2_root_current(&p->cfg,root)||cp_preflight(p,root))return -1;
 for(uint32_t i=0;i<p->manifest.count;i++){struct bw_manifest_record r;if(bw_manifest_get(&p->manifest,i,&r))return -1;
  if(r.flags==BW_MANIFEST_LOCAL){if(dpo_copy(c,root,i))return -1;p->verified_count++;c->local_completed_bytes+=r.size;
   bw_source_smb_progress(c->source,BW_SOURCE_COPYING,p->verified_count,v->local_count,c->local_completed_bytes,v->local_bytes);if(cp_refresh(p))return -1;}}
 if(dpo_generation(c)||cp_blob(&p->cfg,root,"blackwire.files",p->map,p->map_size,p->cfg.run_id,p->deadline))return -1;
 for(uint32_t i=0;i<p->manifest.count;i++){struct bw_manifest_record r;if(bw_manifest_get(&p->manifest,i,&r))return -1;if(r.flags==BW_MANIFEST_STREAM&&dpo_sparse(c,root,i))return -1;}
 if(dpo_generation(c)||v2_copy(p->phase,sizeof p->phase,"index")||dpo_index(c,root)||cp_index_rows(p,root)||cp_blob(&p->cfg,root,"ampr_emu.index",p->index,p->index_size,p->cfg.run_id,p->deadline))return -1;
 return dpo_generation(c);
}
