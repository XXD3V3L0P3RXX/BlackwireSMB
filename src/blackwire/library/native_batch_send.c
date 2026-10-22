#ifndef BW_NATIVE_BATCH_SEND_HOST_TEST
/* The frozen public flock interface uses the SDK's BSD namespace. */
#ifdef _XOPEN_SOURCE
#undef _XOPEN_SOURCE
#endif
#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif
#endif
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifdef BW_NATIVE_BATCH_SEND_HOST_TEST
#define BW_COPY_HOST_TEST
#endif
static const char *bs_state_path,*bs_log_path;static char bs_games_path[2048];
#define BW_COPY_DATA bs_state_path
#define BW_GAMES_ROOT bs_games_path
#define BW_COPY_GAME_LOG bs_log_path
#define BW_COPY_EMBEDDED
#ifndef BW_NATIVE_BATCH_SEND_HOST_TEST
struct cp_plan;
static int cp_status(const struct cp_plan *,int,const char *,const char *) __attribute__((unused));
#endif
#include "../streamreader/prepare_copy.c"
#include "native_batch_send.h"
#include "native_json.h"
#include "batch_metadata.h"
#include "packed_sha1.h"
#include <pthread.h>
static pthread_mutex_t bs_mutex=PTHREAD_MUTEX_INITIALIZER;
static const char bs_scope[]="Blackwire native batch preparations v2\n";
struct bs_operation {
    const struct bw_native_batch_send_files *request;
    struct prepare_v2 base_anchor,scope_anchor,games_anchor,state_anchor,game_anchor;
    int base,scope,games,state,root,source_file,owner_file,provenance_file,log_file;
    struct stat source_identity,owner_identity,provenance_identity;
    struct stat log_identity;unsigned log_checked;
    const char *provenance_name;
    mode_t root_mode;
};
struct bs_seed;
struct bpo_context {struct cp_plan *plan;struct bw_source_smb *source;const struct bw_batch_facts *facts;struct bs_operation *operation;struct bs_seed *seed;};
static struct bpo_context *bpo_active;
static int bs_current(void *);
static int cp_refresh(const struct cp_plan *p){return !bpo_active||bpo_active->plan!=p||!cp_alive(p)||bs_current(bpo_active->operation)?-1:0;}
#include "native_batch_index.c"
#include "native_batch_fs.c"
static int bs_owned(const struct prepare_v2 *anchor,int fd,mode_t mode){struct stat s;return !v2_root_current(anchor,fd)||fstat(fd,&s)||s.st_uid!=geteuid()||(s.st_mode&07777)!=mode?-1:0;}
static int bs_absent(int parent,const char *leaf){struct stat s;return cp_fstatat(parent,leaf,&s,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT?0:-1;}
static int bs_current(void *raw){
    struct bs_operation *o=raw;const struct bw_native_batch_send_files *q=o->request;
    if(!q->current||q->current(q->opaque)||bs_log_current(o)||!v2_root_current(&o->base_anchor,o->base))return -1;
    if(o->scope>=0&&bs_owned(&o->scope_anchor,o->scope,0700))return -1;
    if(o->games>=0&&!v2_root_current(&o->games_anchor,o->games))return -1;
    if(o->state>=0&&bs_owned(&o->state_anchor,o->state,0700))return -1;
    if(o->root>=0&&bs_owned(&o->game_anchor,o->root,o->root_mode))return -1;
    struct {int fd,parent;const char *leaf;const struct stat *identity;} files[]={
        {o->source_file,o->state,"source.json",&o->source_identity},{o->owner_file,o->root,"blackwire.generation.owner",&o->owner_identity},{o->provenance_file,o->state,o->provenance_name,&o->provenance_identity}};
    for(unsigned i=0;i<3;i++)if(files[i].fd>=0){struct stat a,b;if(fstat(files[i].fd,&a)||cp_fstatat(files[i].parent,files[i].leaf,&b,AT_SYMLINK_NOFOLLOW)||!bs_same(files[i].identity,&a)||!bs_same(&a,&b)||a.st_uid!=geteuid())return -1;}
    return 0;
}
/* Full DATA/source/material checks occur only at these outer boundaries. */
static int bs_generation(struct bpo_context *c){
    const struct bw_native_batch_send_files *q=c->operation->request;
    if(cp_refresh(c->plan)||bw_batch_plan_request_matches(q->plan,q->plan_request)||bw_batch_plan_revalidate(q->plan))return -1;
    const struct bw_batch_material_view *m=bw_batch_material_view(q->material);const struct bw_batch_facts *f=bw_batch_plan_view(q->plan);
    return !m||!f||f!=c->facts||m->facts!=f||strcmp(m->run,c->plan->cfg.run_id)||!m->job_generation||cp_refresh(c->plan)?-1:0;
}
static int bs_select(struct bpo_context *c,const char *run,const char *title){
    struct cp_plan *p=c->plan;const struct bw_batch_facts *f=c->facts;struct bw_manifest map;if(bpo_active||bw_manifest_open(&map,f->map,f->map_size))return -1;
    uint64_t boot=0;for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record r;if(bw_manifest_get(&map,i,&r))return -1;if(!strcmp(r.path,"eboot.bin"))boot=r.size;}
    char active[4097];int n=snprintf(active,sizeof active,"version=2\nrun_id=%s\ntitle_id=%s\ngame=%s\neboot_size=%"PRIu64"\nmanifest_count=%u\nmanifest_hash=%016"PRIx64"\nphase=copy\nmanifest_bytes=%zu\ndigests_bytes=%zu\nindex_bytes=0\nindex_sha256=%064u\n",run,title,f->prepared.component,boot,map.count,map.checksum,f->map_size,f->digest_size,0u);
    if(n<0||(size_t)n>=sizeof active||cp_parse(p,(uint8_t *)active,(size_t)n))return -1;p->map=(uint8_t *)f->map;p->digests=(uint8_t *)f->digests;bpo_active=c;
    if(cp_validate_map(p)||cp_refresh(p)){bpo_active=NULL;return -1;}return 0;
}
struct bs_writer {int fd;uint64_t size,expected;struct cp_sha hash;};
static int bs_write(void *opaque,const void *bytes,size_t size){
    struct bs_writer *w=opaque;if(!bytes||!size||size>65536||w->size>w->expected||size>w->expected-w->size)return -1;
    /* No guard/provider/view/SDK/progress/namespace call in this consumer. */
    size_t at=0;while(at<size){ssize_t n=write(w->fd,(const uint8_t *)bytes+at,size-at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>size-at)return -1;at+=(size_t)n;}
    cp_sha_update(&w->hash,bytes,size);w->size+=size;return 0;
}
static int bs_copy(struct bpo_context *c,uint32_t id){
    struct cp_plan *p=c->plan;struct bs_operation *o=c->operation;const struct bw_native_batch_send_files *q=o->request;struct bw_manifest_record row;
    if(bw_manifest_get(&p->manifest,id,&row)||row.flags!=BW_MANIFEST_LOCAL||cp_refresh(p))return -1;
    const struct bw_batch_origin *origin=&c->facts->origins[id];if(origin->file_id!=id)return -1;
    char leaf[256],relative[1024];int parent=v2_parent(&p->cfg,o->root,row.path,1,leaf,relative);if(parent<0)return -1;int result=-1;
    mode_t mode=(mode_t)bw_startup_mode(row.path);int fd=v2_openat(parent,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,mode);if(fd<0){close(parent);return -1;}
    struct stat before,after;if(fchmod(fd,mode)||cp_regular(parent,leaf,fd,0,&before)||before.st_uid!=geteuid())goto done;
    struct bs_writer writer={.fd=fd,.expected=row.size};cp_sha_init(&writer.hash);int read_result=-1;
    if(origin->kind==BW_BATCH_ORIGINAL)read_result=bw_source_provider_read(q->plan_request->original,row.path,bs_write,&writer);
    else if(origin->kind==BW_BATCH_PROJECT){const struct bw_derived_plan_view *v=bw_derived_local_view(q->plan_request->project_local);if(v)read_result=bw_derived_local_read(q->plan_request->project_local,v->derived_id,bs_write,&writer);}
    else if(origin->kind==BW_BATCH_ICU)read_result=bw_packed_bootstrap_read(q->plan_request->bootstrap,origin->entry_index,bs_write,&writer);
    uint8_t digest[32];cp_sha_final(&writer.hash,digest);const uint8_t *expected=p->digests+56u+32u*id;
    if(read_result||writer.size!=row.size||memcmp(digest,expected,32)||cp_refresh(p)||fsync(fd)||cp_regular(parent,leaf,fd,row.size,&after)||!v2_identity(&before,&after)||after.st_uid!=geteuid()||(after.st_mode&07777)!=mode||cp_file_hash(fd,row.size,digest)||memcmp(digest,expected,32)||!v2_directory_current(&p->cfg,o->root,parent,relative)||fsync(parent))goto done;
    result=0;
done:if(close(fd))result=-1;if(close(parent))result=-1;return result;
}
static int bs_sparse(struct bpo_context *c,uint32_t id){
    struct cp_plan *p=c->plan;struct bs_operation *o=c->operation;struct bw_manifest_record row;if(bw_manifest_get(&p->manifest,id,&row)||row.flags!=BW_MANIFEST_STREAM||cp_refresh(p))return -1;
    char leaf[256],relative[1024];int parent=v2_parent(&p->cfg,o->root,row.path,1,leaf,relative);if(parent<0)return -1;
    int fd=v2_openat(parent,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0644),result=-1;struct stat before,after;
    if(fd<0){close(parent);return -1;}if(fchmod(fd,0644)||cp_regular(parent,leaf,fd,0,&before)||before.st_uid!=geteuid()||cp_refresh(p)||ftruncate(fd,(off_t)row.size)||fsync(fd)||cp_regular(parent,leaf,fd,row.size,&after)||!v2_identity(&before,&after)||after.st_blocks||!v2_directory_current(&p->cfg,o->root,parent,relative)||fsync(parent))goto done;
    int held=bs_seed_hold(c,&row,fd,parent,leaf,relative,&after);if(held<0)goto done;if(held){fd=-1;parent=-1;}result=0;
done:if(fd>=0&&close(fd))result=-1;if(parent>=0&&close(parent))result=-1;return result;
}
static int bs_all_local(struct cp_plan *p,int root){
    for(uint32_t i=0;i<p->manifest.count;i++){struct bw_manifest_record r;if(bw_manifest_get(&p->manifest,i,&r))return -1;if(r.flags!=BW_MANIFEST_LOCAL)continue;
        char leaf[256],relative[1024];int parent=v2_parent(&p->cfg,root,r.path,0,leaf,relative);if(parent<0)return -1;
        int bad=bs_existing(p,root,parent,relative,leaf,r.size,p->digests+56u+32u*i);if(close(parent))bad=1;if(bad)return -1;
    }return 0;
}
static void bs_result(const struct bw_batch_terminal_record *r,const uint8_t *bytes,size_t size,unsigned verified,struct bw_native_batch_prepared *out){
    memset(out,0,sizeof *out);out->base.state=BW_PREPARED_VALID;memcpy(out->base.game,r->original.binding.game,75);memcpy(out->base.run,r->run,33);
    out->base.local_bytes=r->local_bytes;out->base.remote_bytes=r->remote_bytes;out->base.allocated_bytes=r->allocated_bytes;out->base.local_count=r->local_count;out->base.stream_count=r->stream_count;out->base.entries=r->entries;out->base.bytes_verified=verified;
    cp_sha_bytes(bytes,size,out->base.record_sha256);memcpy(out->terminal_sha256,out->base.record_sha256,32);out->terminal_size=size;out->prepared=r->prepared;
    out->base.prepared_policy=BW_BATCH_POLICY;memcpy(out->base.prepared_component,r->prepared.component,75);memcpy(out->base.provenance_sha256,r->provenance.sha256,32);out->base.provenance_size=r->provenance.size;
}
void bw_native_batch_snapshot_destroy(struct bw_native_batch_snapshot *s){if(s){bw_batch_provenance_destroy(&s->provenance);free(s->provenance_bytes);memset(s,0,sizeof *s);}}
#include "native_batch_inspect.c"
static int bs_private_create(const char *parent_path,int parent,const char *leaf,struct prepare_v2 *anchor){
    struct prepare_v2 base={0};if(v2_copy(base.root,sizeof base.root,parent_path)||!v2_root_current(&base,parent)||!v2_game_component(leaf)||v2_mkdirat(parent,leaf,0700))return -1;
    int fd=v2_openat(parent,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);int n=snprintf(anchor->root,sizeof anchor->root,"%s/%s",parent_path,leaf);
    if(fd<0||n<0||(size_t)n>=sizeof anchor->root||fchmod(fd,0700)||bs_owned(anchor,fd,0700)||!v2_root_current(&base,parent)||fsync(parent)){if(fd>=0)close(fd);return -1;}return fd;
}
static int bs_no_stages(int parent,const char *prefix){
    int duplicate=dup(parent);if(duplicate<0)return -1;DIR *directory=fdopendir(duplicate);if(!directory){close(duplicate);return -1;}
    rewinddir(directory);int bad=0;unsigned count=0;size_t n=strlen(prefix);
    for(;;){errno=0;struct dirent *entry=readdir(directory);if(!entry){if(errno)bad=1;break;}if(++count>4096||!strncmp(entry->d_name,prefix,n)){bad=1;break;}}
    if(closedir(directory))bad=1;return bad?-1:0;
}
static int bs_cutover(const struct prepare_v2 *parent_anchor,int parent,const char *stage,const char *final,int held,struct prepare_v2 *anchor){
    struct stat before,after,named;if(!v2_root_current(parent_anchor,parent)||!v2_root_current(anchor,held)||fstat(held,&before)||before.st_uid!=geteuid()||bs_absent(parent,final)||cp_fstatat(parent,stage,&named,AT_SYMLINK_NOFOLLOW)||!v2_identity(&before,&named))return -1;
    /* Ordinary renameat under the owned-parent/cooperative-writer boundary.
     * This is deliberately not represented as atomic NOREPLACE. */
    int result=cp_renameat(parent,stage,parent,final);int moved=!bs_absent(parent,stage)&&!cp_fstatat(parent,final,&named,AT_SYMLINK_NOFOLLOW)&&!fstat(held,&after)&&v2_identity(&before,&after)&&v2_identity(&after,&named)&&after.st_uid==geteuid();
    if(moved){int n=snprintf(anchor->root,sizeof anchor->root,"%s/%s",parent_anchor->root,final);if(n<0||(size_t)n>=sizeof anchor->root)return -1;}
    if(result||!moved||!v2_root_current(parent_anchor,parent)||!v2_root_current(anchor,held)||fsync(parent))return -1;return 0;
}
int bw_native_batch_send_files(const struct bw_native_batch_send_files *q,struct bw_native_batch_prepared *out){
    if(out){memset(out,0,sizeof *out);out->base.state=BW_PREPARED_UNKNOWN;}if(!q||!out||!q->data_root||!q->game_log||!q->current||!q->plan||!q->plan_request||!q->material||!q->plan_request->original||!q->plan_request->original->source)return BW_SEND_FAILED;
    if(pthread_mutex_trylock(&bs_mutex))return BW_SEND_BUSY;int result=BW_SEND_FAILED,lock=-1,created=0,published=0,cutover=0;const char *error="Batch accepted source/material is not current";
    struct bs_operation o={.request=q,.base=-1,.scope=-1,.games=-1,.state=-1,.root=-1,.source_file=-1,.owner_file=-1,.provenance_file=-1,.log_file=-1,.root_mode=0700};
    struct cp_plan p={.deadline=q->plan_request->original->source->deadline};struct bpo_context c={.plan=&p,.source=q->plan_request->original->source,.operation=&o};
    struct bw_json sj={.maximum=2048},oj={.maximum=2048};uint8_t *provenance=NULL;size_t pn=0;uint8_t record[BW_BATCH_TERMINAL_LIMIT],marker[BW_GENERATION_SIZE],combined[4096];size_t rn=0,cn=0;
    char final_state[2048],scope_path[2048],state_leaf[140],stage_game[160],stage_state[192],provenance_name[96],marker_name[64];const char *codec_error=NULL;struct bw_batch_terminal_record terminal={0};
    if(q->current(q->opaque)||bw_batch_plan_request_matches(q->plan,q->plan_request))goto done;const struct bw_batch_material_view *m=bw_batch_material_view(q->material);const struct bw_batch_facts *f=bw_batch_plan_view(q->plan);
    if(!m||!f||m->facts!=f||!bs_nonce(m->run)||!bs_identity_equal(&f->original,&q->plan_request->original->identity)||bs_paths(q->data_root,&f->original.binding,final_state)||bs_state_leaf(state_leaf,&f->original.binding,&f->prepared)||bs_source_json(&f->original,&sj)||bw_batch_provenance_encode(f,&provenance,&pn,&codec_error))goto done;c.facts=f;
    if(bs_seed_open(f,&c.seed))goto done;
    uint8_t ph[32];cp_sha_bytes(provenance,pn,ph);if(bs_owner_json(f,ph,pn,&oj)||bs_combined(&sj,&oj,combined,&cn)||bs_provenance_name(provenance_name,&f->prepared))goto done;
    bs_log_path=q->game_log;o.base=bs_root(q->data_root,&o.base_anchor);if(o.base<0||bs_current(&o))goto done;lock=bs_lock(o.base);if(lock<0){result=BW_SEND_BUSY;goto done;}
    /* Read-only history qualification holds the same cooperative writer lock,
     * and finishes before the copy operation owns global paths/guards. */
    error="Existing batch generation metadata or orphan staging is unconfirmed";
    if(bs_history_valid(q->data_root,&f->original,q->current,q->opaque)||bs_paths(q->data_root,&f->original.binding,final_state))goto done;
    cp_io_guard=bs_current;cp_io_opaque=&o;
    if(bs_select(&c,m->run,f->original.binding.title)||bs_generation(&c))goto done;
    error="An existing batch final/state or unowned scope was preserved";
    o.games=bs_directory(q->data_root,o.base,"games",1,&created);if(o.games<0||v2_copy(o.games_anchor.root,sizeof o.games_anchor.root,bs_games_path)||bs_absent(o.games,f->prepared.component))goto done;
    o.scope=bs_directory(q->data_root,o.base,"native-batch-prepared",1,&created);if(o.scope<0)goto done;int n=snprintf(scope_path,sizeof scope_path,"%s/native-batch-prepared",q->data_root);
    if(n<0||(size_t)n>=sizeof scope_path||v2_copy(o.scope_anchor.root,sizeof o.scope_anchor.root,scope_path))goto done;
    if(created){if(fchmod(o.scope,0700)||bs_blob(scope_path,o.scope,".blackwire-native-batch-prepared",bs_scope,sizeof bs_scope-1,p.cfg.run_id,p.deadline))goto done;}
    else if(bs_owned(&o.scope_anchor,o.scope,0700)||bs_exact(o.scope,".blackwire-native-batch-prepared",bs_scope,sizeof bs_scope-1))goto done;
    if(bs_absent(o.scope,state_leaf))goto done;
    char game_prefix[128],state_prefix[160];int gp=snprintf(game_prefix,sizeof game_prefix,".blackwire-batch-%s.",f->prepared.component),sp=snprintf(state_prefix,sizeof state_prefix,".%s.",state_leaf);
    if(gp<0||(size_t)gp>=sizeof game_prefix||sp<0||(size_t)sp>=sizeof state_prefix||bs_no_stages(o.games,game_prefix)||bs_no_stages(o.scope,state_prefix)||bs_current(&o))goto done;
    n=snprintf(stage_game,sizeof stage_game,".blackwire-batch-%s.%s",f->prepared.component,p.cfg.run_id);int k=snprintf(stage_state,sizeof stage_state,".%s.%s",state_leaf,p.cfg.run_id);
    if(n<0||(size_t)n>=sizeof stage_game||k<0||(size_t)k>=sizeof stage_state)goto done;
    o.state=bs_private_create(scope_path,o.scope,stage_state,&o.state_anchor);if(o.state<0)goto done;bs_state_path=o.state_anchor.root;
    o.root=bs_private_create(bs_games_path,o.games,stage_game,&o.game_anchor);if(o.root<0||v2_copy(p.cfg.root,sizeof p.cfg.root,o.game_anchor.root))goto done;
    if(bs_blob(bs_state_path,o.state,"source.json",sj.bytes,sj.used,p.cfg.run_id,p.deadline)||bs_blob(p.cfg.root,o.root,"blackwire.generation.owner",oj.bytes,oj.used,p.cfg.run_id,p.deadline))goto done;
    o.source_file=v2_openat(o.state,"source.json",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);o.owner_file=v2_openat(o.root,"blackwire.generation.owner",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(o.source_file<0||o.owner_file<0||cp_regular(o.state,"source.json",o.source_file,sj.used,&o.source_identity)||cp_regular(o.root,"blackwire.generation.owner",o.owner_file,oj.used,&o.owner_identity)||bs_current(&o))goto done;
    error="Batch LOCAL copy or cached ICU emission failed; private staging was retained";
    uint64_t completed=0;for(uint32_t i=0;i<p.manifest.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&p.manifest,i,&row))goto done;if(row.flags==BW_MANIFEST_LOCAL){if(bs_copy(&c,i))goto done;p.verified_count++;completed+=row.size;bw_source_smb_progress(c.source,BW_SOURCE_COPYING,p.verified_count,f->local_count,completed,f->local_bytes);if(cp_refresh(&p))goto done;}}
    if(c.seed){
        error="Batch provisional STREAM metadata emission failed; private staging was retained";
        for(uint32_t i=0;i<p.manifest.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&p.manifest,i,&row))goto done;if(row.flags==BW_MANIFEST_STREAM&&bs_sparse(&c,i))goto done;}
        if(bs_seed_emit(&c))goto done;
    }
    if(bw_packed_bootstrap_finish(q->plan_request->bootstrap)||bs_generation(&c)||bs_seed_finish(&c))goto done;
    error="Batch map/index/sparse or final source fence failed; private staging was retained";
    if(bs_blob(p.cfg.root,o.root,"blackwire.files",f->map,f->map_size,p.cfg.run_id,p.deadline)||bs_blob(p.cfg.root,o.root,"blackwire.generation.digests",f->digests,f->digest_size,p.cfg.run_id,p.deadline))goto done;
    if(!c.seed)for(uint32_t i=0;i<p.manifest.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&p.manifest,i,&row))goto done;if(row.flags==BW_MANIFEST_STREAM&&bs_sparse(&c,i))goto done;}
    if(bpo_index(&c,o.root)||bs_blob(p.cfg.root,o.root,"ampr_emu.index",p.index,p.index_size,p.cfg.run_id,p.deadline)||bw_generation_bytes(marker,p.cfg.run_id,f->original.binding.title,f->prepared.component,f->map_sha256)||bw_generation_name(marker_name,p.cfg.run_id)||bs_blob(p.cfg.root,o.root,marker_name,marker,sizeof marker,p.cfg.run_id,p.deadline)||bs_blob(bs_state_path,o.state,provenance_name,provenance,pn,p.cfg.run_id,p.deadline)||bs_all_local(&p,o.root)||bs_index_rows(&p,o.root)||bs_streams_verify(&p,o.root,0,c.seed,NULL)||bs_generation(&c))goto done;
    o.provenance_name=provenance_name;o.provenance_file=v2_openat(o.state,provenance_name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(o.provenance_file<0||cp_regular(o.state,provenance_name,o.provenance_file,pn,&o.provenance_identity)||bs_current(&o)||fsync(o.root)||fsync(o.state))goto done;
    error="Batch game cutover was unconfirmed; retain staging/final evidence";
    if(fchmod(o.root,0755))goto done;o.root_mode=0755;if(bs_current(&o)||bs_absent(o.scope,state_leaf))goto done;cutover=1;
    if(bs_cutover(&o.games_anchor,o.games,stage_game,f->prepared.component,o.root,&o.game_anchor)||v2_copy(p.cfg.root,sizeof p.cfg.root,o.game_anchor.root)){result=BW_SEND_UNCONFIRMED;goto done;}
    error="Batch terminal/state publication was unconfirmed; retain completed game evidence";
    struct stat gs,ss,ps;if(fstat(o.root,&gs)||fstat(o.state,&ss)||fstat(o.provenance_file,&ps)||bs_generation(&c)||bs_all_local(&p,o.root)||bs_index_rows(&p,o.root)||bs_streams_verify(&p,o.root,0,c.seed,NULL))goto done;
    terminal.original=f->original;terminal.prepared=f->prepared;terminal.material_policy=BW_BATCH_POLICY;memcpy(terminal.material_policy_sha256,f->material_policy_sha256,32);memcpy(terminal.mapping_rule_sha256,f->mapping_rule_sha256,32);memcpy(terminal.material_sha256,f->material_sha256,32);
    memcpy(terminal.native_snapshot_sha256,f->bootstrap.native_snapshot_sha256,32);memcpy(terminal.base_map_sha256,f->base_map_sha256,32);memcpy(terminal.base_digests_sha256,f->base_digests_sha256,32);memcpy(terminal.facts_sha256,f->facts_sha256,32);memcpy(terminal.origins_sha256,f->origins_sha256,32);memcpy(terminal.run,p.cfg.run_id,33);memcpy(terminal.title,f->original.binding.title,10);
    bs_ref(&terminal.provenance,provenance,pn);bs_ref(&terminal.map,f->map,f->map_size);bs_ref(&terminal.digests,f->digests,f->digest_size);bs_ref(&terminal.index,p.index,p.index_size);bs_ref(&terminal.marker,marker,sizeof marker);bs_ref(&terminal.identity,combined,cn);
    terminal.count=f->count;terminal.local_count=f->local_count;terminal.stream_count=f->stream_count;terminal.local_bytes=f->local_bytes;terminal.remote_bytes=f->remote_bytes;terminal.bootstrap_bytes=f->bootstrap_bytes;
    terminal.game_root=(struct bw_derived_object_identity){(uint64_t)gs.st_dev,(uint64_t)gs.st_ino};terminal.state_root=(struct bw_derived_object_identity){(uint64_t)ss.st_dev,(uint64_t)ss.st_ino};terminal.provenance_object=(struct bw_derived_object_identity){(uint64_t)ps.st_dev,(uint64_t)ps.st_ino};
    struct bw_batch_terminal_artifacts a={.provenance=provenance,.provenance_size=pn,.index=p.index,.index_size=p.index_size,.marker=marker,.marker_size=sizeof marker,.identity=combined,.identity_size=cn};
    if(cp_measure_tree(&p,o.root,gs.st_dev,0,&terminal.allocated_bytes,&terminal.entries)||bs_current(&o)||bw_batch_terminal_validate(&terminal,&a,&codec_error)||bw_batch_terminal_encode(&terminal,record,sizeof record,&rn,&codec_error)||bs_absent(o.state,"prepared.bin")||bs_blob(bs_state_path,o.state,"prepared.bin",record,rn,p.cfg.run_id,p.deadline)||bs_generation(&c)||fsync(o.state))goto done;
    if(bs_cutover(&o.scope_anchor,o.scope,stage_state,state_leaf,o.state,&o.state_anchor)){result=BW_SEND_UNCONFIRMED;goto done;}published=1;bs_state_path=o.state_anchor.root;
    struct bw_native_batch_metadata_accounting accounting={0};
    if(bs_current(&o)||bs_exact(o.state,"prepared.bin",record,rn)||bs_exact(o.state,provenance_name,provenance,pn)||bs_exact(o.root,"blackwire.generation.owner",oj.bytes,oj.used)||bs_all_local(&p,o.root)||bs_index_rows(&p,o.root)||bs_streams_verify(&p,o.root,0,c.seed,&accounting)||bs_generation(&c)){result=BW_SEND_UNCONFIRMED;goto done;}
    bs_result(&terminal,record,rn,1,out);out->metadata=accounting;result=0;
done:
    if(bs_seed_destroy(c.seed)){result=BW_SEND_UNCONFIRMED;}c.seed=NULL;
    cp_io_guard=NULL;cp_io_opaque=NULL;bpo_active=NULL;free(p.index);free(provenance);free(sj.bytes);free(oj.bytes);int closed=0;
    int fds[]={o.provenance_file,o.owner_file,o.source_file,o.root,o.state,o.games,o.scope,lock,o.base,o.log_file};for(unsigned i=0;i<sizeof fds/sizeof *fds;i++)if(fds[i]>=0&&close(fds[i]))closed=1;
    if((closed||result)&&cutover)result=BW_SEND_UNCONFIRMED;if(!result&&q->current(q->opaque))result=BW_SEND_UNCONFIRMED;
    if(result){memset(out,0,sizeof *out);out->base.state=BW_PREPARED_UNKNOWN;out->error=error;bw_source_smb_progress(q->plan_request->original->source,BW_SOURCE_ERROR,0,0,0,0);}
    else{bw_source_smb_progress(q->plan_request->original->source,BW_SOURCE_PREPARED,terminal.count,terminal.count,terminal.local_bytes,terminal.local_bytes);if(q->current(q->opaque)){out->base.state=BW_PREPARED_UNKNOWN;out->base.bytes_verified=0;out->error="Batch completion observer changed the accepted job";result=BW_SEND_UNCONFIRMED;}}
    (void)published;bs_state_path=NULL;bs_log_path=NULL;memset(bs_games_path,0,sizeof bs_games_path);pthread_mutex_unlock(&bs_mutex);return result;
}
int bw_native_batch_send_verify(const struct bw_native_batch_verify *q,struct bw_native_batch_snapshot *out){
    if(!q||!out||!q->data_root||!q->game_log||!q->expected_terminal_sha256||!q->plan||!q->plan_request||!q->current)return -1;
    if(q->current(q->opaque)||bw_batch_plan_request_matches(q->plan,q->plan_request)||bw_batch_plan_revalidate(q->plan))return -1;const struct bw_batch_facts *f=bw_batch_plan_view(q->plan);if(!f)return -1;
    if(bw_native_batch_local_verify_exact(q->data_root,&f->original,f->prepared.component,q->expected_terminal_sha256,q->current,q->opaque,out))return -1;
    const struct bw_batch_facts *history=bw_batch_provenance_view(&out->provenance);
    if(!history||memcmp(f->prepared.commitment_sha256,history->prepared.commitment_sha256,32)||memcmp(f->material_sha256,history->material_sha256,32)||memcmp(f->map_sha256,history->map_sha256,32)||memcmp(f->digests_sha256,history->digests_sha256,32)||memcmp(f->origins_sha256,history->origins_sha256,32)||bs_games_stopped(q->game_log)||bw_batch_plan_revalidate(q->plan)||q->current(q->opaque)){bw_native_batch_snapshot_destroy(out);out->prepared.base.state=BW_PREPARED_UNKNOWN;return -1;}return 0;
}
