#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifdef BW_NATIVE_DERIVED_SEND_HOST_TEST
#define BW_COPY_HOST_TEST
#endif
static const char *ds_state_path,*ds_log_path;
static char ds_games_path[2048];
#define BW_COPY_DATA ds_state_path
#define BW_GAMES_ROOT ds_games_path
#define BW_COPY_GAME_LOG ds_log_path
#ifndef BW_NATIVE_DERIVED_SEND_HOST_TEST
struct cp_plan;
static int cp_status(const struct cp_plan *,int,const char *,const char *) __attribute__((unused));
#endif
#include "native_derived_prepare_ops.c"
#include "native_json.h"
#include <pthread.h>
#include <sys/file.h>
static pthread_mutex_t ds_mutex=PTHREAD_MUTEX_INITIALIZER;
static const char ds_scope[]="Blackwire native derived preparations v1\n";
struct ds_operation {
 const struct bw_native_derived_send_files *request;
 struct prepare_v2 base_anchor,state_anchor,game_anchor;
 int base,state,root,source_file,owner_file,provenance_file;
 struct stat source_identity,owner_identity,provenance_identity;
 const char *provenance_name;
 const uint8_t *source_json,*owner_json;size_t source_size,owner_size;
 const uint8_t *prior;size_t prior_size;struct stat prior_identity;
};
static int ds_same(const struct stat *a,const struct stat *b){return cp_measure_same(a,b)&&S_ISREG(b->st_mode)&&b->st_nlink==1;}
static int ds_nonce(const char *value){
    if(!value||strnlen(value,33)!=32)return 0;
    unsigned nonzero=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",value[i]))return 0;nonzero|=value[i]!='0';}return nonzero!=0;
}
static int ds_paths(const char *base,const struct bw_source_binding *binding,char state[2048]){
    if(!base||!binding||strnlen(binding->game,sizeof binding->game)!=74||strnlen(binding->source,sizeof binding->source)>BW_MANIFEST_MAX_PATH||
        bw_manifest_validate_path(binding->source,strlen(binding->source))||strnlen(binding->title,sizeof binding->title)!=9||memcmp(binding->title,"PPSA",4)||
        memcmp(binding->game,binding->title,9)||binding->game[9]!='_'||!v2_game_component(binding->game))return -1;
    for(unsigned i=4;i<9;i++)if(binding->title[i]<'0'||binding->title[i]>'9')return -1;
    char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",binding->sha256[i]);if(strcmp(binding->game+10,hash))return -1;
    int a=snprintf(ds_games_path,sizeof ds_games_path,"%s/games",base),b=snprintf(state,2048,"%s/native-derived-prepared/%s",base,binding->game);
    return a<0||(size_t)a>=sizeof ds_games_path||b<0||b>=2048?-1:0;
}
static int ds_root(const char *path,struct prepare_v2 *anchor){
    memset(anchor,0,sizeof *anchor);if(v2_copy(anchor->root,sizeof anchor->root,path))return -1;
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd>=0&&!v2_root_current(anchor,fd)){close(fd);fd=-1;}return fd;
}
static int ds_directory(const char *path,int parent,const char *leaf,int create,int *created){
    struct prepare_v2 anchor={0};*created=0;if(v2_copy(anchor.root,sizeof anchor.root,path)||!v2_root_current(&anchor,parent)||!v2_game_component(leaf))return -1;
    int fd=v2_openat(parent,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
    if(fd<0&&errno==ENOENT&&create){if(v2_mkdirat(parent,leaf,0755))return -1;*created=1;fd=v2_openat(parent,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);if(fd>=0&&fchmod(fd,0755)){close(fd);return -1;}}
    char child[2048];int n=snprintf(child,sizeof child,"%s/%s",path,leaf);
    if(fd<0||n<0||(size_t)n>=sizeof child){if(fd>=0)close(fd);return -1;}
    if(!v2_root_current(&anchor,parent)||v2_copy(anchor.root,sizeof anchor.root,child)||!v2_root_current(&anchor,fd)||(create&&fsync(parent))){close(fd);return -1;}return fd;
}
static int ds_read(int parent,const char *leaf,size_t maximum,void **bytes,size_t *size,struct stat *identity){
    *bytes=NULL;*size=0;int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0)return errno==ENOENT?1:-1;
    struct stat before,after;int result=-1;void *body=NULL;
    if(fstat(fd,&before)||!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_size<1||(uint64_t)before.st_size>maximum||cp_regular(parent,leaf,fd,(uint64_t)before.st_size,NULL))goto done;
    body=malloc((size_t)before.st_size);if(!body||v2_read_all(fd,body,(size_t)before.st_size)||cp_regular(parent,leaf,fd,(uint64_t)before.st_size,&after)||!ds_same(&before,&after))goto done;
    if(identity)*identity=after;*bytes=body;*size=(size_t)before.st_size;body=NULL;result=0;
done:
    free(body);if(close(fd))result=-1;return result;
}
static int ds_exact(int parent,const char *leaf,const void *expected,size_t size){
    void *bytes=NULL;size_t got=0;int result=ds_read(parent,leaf,size,&bytes,&got,NULL);int bad=result||got!=size||(got==size&&memcmp(bytes,expected,size));free(bytes);return bad?-1:0;
}
static int ds_binding_json(const struct bw_source_binding *binding,struct bw_json *json){
    bw_json_text(json,"{\"version\":1,\"source\":");bw_json_string(json,binding->source);bw_json_text(json,",\"game\":");bw_json_string(json,binding->game);
    bw_json_text(json,",\"titleId\":");bw_json_string(json,binding->title);char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",binding->sha256[i]);
    bw_json_text(json,",\"bindingSha256\":");bw_json_string(json,hash);bw_json_text(json,"}\n");return json->bad?-1:0;
}
static int ds_provider_json(const struct bw_provider_binding *identity,struct bw_json *json){
    if(!identity||identity->policy!=BW_PROVIDER_POLICY_V1||
       (identity->kind!=BW_PROVIDER_FOLDER&&identity->kind!=BW_PROVIDER_CONTAINER))return -1;
    if(ds_binding_json(&identity->binding,json))return -1;
    if(identity->kind==BW_PROVIDER_CONTAINER){
        if(json->used<2)return -1;json->used-=2;
        bw_json_text(json,",\"sourceKind\":\"container\",\"sourcePolicy\":\"unsigned-contiguous-d32-pfs-v1\"}\n");
    }
    return json->bad?-1:0;
}
static int ds_blob(const char *path,int directory,const char *leaf,const void *bytes,size_t size,const char *run,double deadline){
    struct prepare_v2 anchor={0};if(v2_copy(anchor.root,sizeof anchor.root,path)||!v2_root_current(&anchor,directory))return -1;
    return cp_blob(&anchor,directory,leaf,bytes,size,run,deadline)||ds_exact(directory,leaf,bytes,size)?-1:0;
}
static int ds_lock(int directory){
    static const char marker[]="Blackwire native send lock v1\n";const char *leaf=".blackwire-send.lock";
    int fd=v2_openat(directory,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600),created=fd>=0;
    if(fd<0&&errno==EEXIST)fd=v2_openat(directory,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(fd<0)return -1;
    char bytes[sizeof marker];struct stat st;
    if((created&&(fchmod(fd,0600)||cp_write_all(fd,marker,sizeof marker-1)||fsync(fd)||fsync(directory)))||
        cp_regular(directory,leaf,fd,sizeof marker-1,&st)||pread(fd,bytes,sizeof marker-1,0)!=(ssize_t)(sizeof marker-1)||memcmp(bytes,marker,sizeof marker-1)||flock(fd,LOCK_EX|LOCK_NB)){close(fd);return -1;}return fd;
}
static int ds_games_stopped(const char *path){
    if(v2_games_stopped(path))return -1;
    /* Retain a second fresh observation; one quiet snapshot is insufficient. */
    return v2_games_stopped(path);
}
static int ds_streams_empty(struct cp_plan *plan,int root,int missing_ok){
    for(uint32_t i=0;i<plan->manifest.count;i++){
        struct bw_manifest_record row;if(bw_manifest_get(&plan->manifest,i,&row))return -1;if(row.flags!=BW_MANIFEST_STREAM)continue;
        char leaf[256],relative[1024];int parent=v2_parent(&plan->cfg,root,row.path,0,leaf,relative);if(parent==-2&&missing_ok)continue;if(parent<0)return -1;
        int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0){int absent=errno==ENOENT;close(parent);if(absent&&missing_ok)continue;return -1;}
        struct stat st;int bad=cp_regular(parent,leaf,fd,row.size,&st)||st.st_blocks||!v2_directory_current(&plan->cfg,root,parent,relative);
        if(close(fd))bad=1;if(close(parent))bad=1;if(bad)return -1;
    }return 0;
}
static void ds_hex(char text[65],const uint8_t hash[32]){for(unsigned i=0;i<32;i++)snprintf(text+2*i,3,"%02x",hash[i]);}
static int ds_identity_equal(const struct bw_provider_binding *a,const struct bw_provider_binding *b){
 return a&&b&&a->kind==b->kind&&a->policy==b->policy&&!memcmp(a->binding.sha256,b->binding.sha256,32)&&
 !strcmp(a->binding.game,b->binding.game)&&!strcmp(a->binding.source,b->binding.source)&&!strcmp(a->binding.title,b->binding.title);
}
static int ds_source_json(const struct bw_provider_binding *identity,struct bw_json *j){
 if(!identity||identity->kind!=BW_PROVIDER_FOLDER||identity->policy!=BW_PROVIDER_POLICY_V1)return -1;
 if(ds_binding_json(&identity->binding,j)||j->used<2)return -1;j->used-=2;
 bw_json_text(j,",\"preparedPolicy\":\"native-derived-plain-project-plan-v1\",\"sourceKind\":1,\"sourcePolicy\":1}\n");return j->bad?-1:0;
}
static int ds_owner_json(const struct bw_derived_provenance_facts *f,const uint8_t provenance[32],size_t size,struct bw_json *j){
 char hex[65];bw_json_text(j,"{\"version\":1,\"originalGame\":");bw_json_string(j,f->original.binding.game);
 bw_json_text(j,",\"preparedComponent\":");bw_json_string(j,f->plan.identity.component);
 bw_json_text(j,",\"preparedPolicy\":1,\"commitmentSha256\":");ds_hex(hex,f->plan.identity.commitment_sha256);bw_json_string(j,hex);
 bw_json_text(j,",\"materialSha256\":");ds_hex(hex,f->material_sha256);bw_json_string(j,hex);
 bw_json_text(j,",\"provenanceSha256\":");ds_hex(hex,provenance);bw_json_string(j,hex);
 bw_json_format(j,",\"provenanceBytes\":%zu}\n",size);return j->bad?-1:0;
}
static int ds_combined(const struct bw_json *source,const struct bw_json *owner,uint8_t bytes[4096],size_t *size){
 static const char domain[]="Blackwire native derived source/root identity v1";size_t n=sizeof domain-1;
 if(source->bad||owner->bad||n+12u+source->used+owner->used>4096)return -1;
 dpo_put32(bytes,(uint32_t)n);memcpy(bytes+4,domain,n);n+=4;dpo_put32(bytes+n,(uint32_t)source->used);n+=4;memcpy(bytes+n,source->bytes,source->used);n+=source->used;
 dpo_put32(bytes+n,(uint32_t)owner->used);n+=4;memcpy(bytes+n,owner->bytes,owner->used);n+=owner->used;*size=n;return 0;
}
static int ds_provenance_name(char leaf[96],const struct bw_derived_prepared_identity *p){
 char hash[65];ds_hex(hash,p->namespace_sha256);int n=snprintf(leaf,96,"provenance-%s.bin",hash);return n<0||n>=96?-1:0;
}
static void ds_ref(struct bw_derived_artifact_ref *r,const void *p,size_t n){r->size=n;cp_sha_bytes(p,n,r->sha256);}
static int ds_current(void *raw){
 struct ds_operation *o=raw;
 if(!o->request->current||o->request->current(o->request->opaque)||ds_games_stopped(ds_log_path)||!v2_root_current(&o->base_anchor,o->base))return -1;
 if(o->state>=0&&!v2_root_current(&o->state_anchor,o->state))return -1;
 if(o->root>=0&&!v2_root_current(&o->game_anchor,o->root))return -1;
 if(o->source_file>=0){struct stat held,named;if(fstat(o->source_file,&held)||cp_fstatat(o->state,"source.json",&named,AT_SYMLINK_NOFOLLOW)||!ds_same(&o->source_identity,&held)||!ds_same(&held,&named))return -1;}
 if(o->owner_file>=0){struct stat held,named;if(fstat(o->owner_file,&held)||cp_fstatat(o->root,"blackwire.generation.owner",&named,AT_SYMLINK_NOFOLLOW)||!ds_same(&o->owner_identity,&held)||!ds_same(&held,&named))return -1;}
 if(o->provenance_file>=0){struct stat held,named;if(fstat(o->provenance_file,&held)||cp_fstatat(o->state,o->provenance_name,&named,AT_SYMLINK_NOFOLLOW)||!ds_same(&o->provenance_identity,&held)||!ds_same(&held,&named))return -1;}
 return bw_derived_material_view(o->request->material)?0:-1;
}
static void ds_result(const struct bw_derived_terminal_record *r,const uint8_t *bytes,size_t size,unsigned verified,struct bw_native_derived_prepared *out){
 memset(out,0,sizeof *out);out->base.state=BW_PREPARED_VALID;memcpy(out->base.game,r->original.binding.game,75);memcpy(out->base.run,r->run,33);
 out->base.local_bytes=r->local_bytes;out->base.remote_bytes=r->remote_bytes;out->base.allocated_bytes=r->allocated_bytes;
 out->base.local_count=r->local_count;out->base.stream_count=r->stream_count;out->base.entries=r->entries;out->base.bytes_verified=verified;
 cp_sha_bytes(bytes,size,out->base.record_sha256);memcpy(out->terminal_sha256,out->base.record_sha256,32);out->terminal_size=size;
 out->prepared_policy=r->prepared.policy;memcpy(out->prepared_component,r->prepared.component,75);out->prepared=r->prepared;
 memcpy(out->provenance_sha256,r->provenance.sha256,32);out->provenance_size=(size_t)r->provenance.size;
}
void bw_native_derived_snapshot_destroy(struct bw_native_derived_snapshot *s){if(!s)return;bw_derived_provenance_destroy(&s->provenance);free(s->provenance_bytes);memset(s,0,sizeof *s);}

/* Called under this translation unit's mutex. No writes, source APIs or body
 * reads. A live guard optionally enables destination LOCAL hash verification. */
static int ds_inspect(const char *base,const struct bw_provider_binding *identity,const uint8_t *expected,int (*current)(void *),void *opaque,int verify,struct bw_native_derived_snapshot *out){
 if(!out||out->provenance.state||out->provenance_bytes)return -1;memset(out,0,sizeof *out);out->prepared.base.state=BW_PREPARED_INVALID;
 int data=-1,scope=-1,state=-1,root=-1,bad=1;char state_path[2048],scope_path[2048],game_path[2048],provenance_name[96],marker_name[64];
 struct prepare_v2 data_anchor={0},scope_anchor={0},state_anchor={0},game_anchor={0};struct stat gs,ss,ps;
 void *record=NULL,*provenance=NULL,*map=NULL,*digests=NULL,*index=NULL,*marker=NULL;size_t rn=0,pn=0,mn=0,dn=0,in=0,kn=0;
 struct bw_json sj={.maximum=2048},oj={.maximum=2048};struct bw_derived_provenance decoded={0};struct bw_derived_terminal_record r={0};struct cp_plan plan={0};const char *error=NULL;
 double at=cp_now();plan.deadline=at+300;cp_io_guard=current;cp_io_opaque=opaque;
 if(at<0||!base||!identity||ds_paths(base,&identity->binding,state_path)||ds_source_json(identity,&sj)||(current&&current(opaque)))goto done;
 data=ds_root(base,&data_anchor);if(data<0)goto done;
 int n=snprintf(scope_path,sizeof scope_path,"%s/native-derived-prepared",base);if(n<0||(size_t)n>=sizeof scope_path)goto done;
 scope=ds_root(scope_path,&scope_anchor);if(scope<0){struct stat st;if(lstat(scope_path,&st)<0&&errno==ENOENT)out->prepared.base.state=BW_PREPARED_ABSENT;goto done;}
 if(ds_exact(scope,".blackwire-native-derived-prepared",ds_scope,sizeof ds_scope-1))goto done;
 state=ds_root(state_path,&state_anchor);if(state<0){struct stat st;if(lstat(state_path,&st)<0&&errno==ENOENT)out->prepared.base.state=BW_PREPARED_ABSENT;goto done;}
 /* Existing state without a confirmed terminal is UNKNOWN, never adopted. */
 out->prepared.base.state=BW_PREPARED_UNKNOWN;
 if(ds_exact(state,"source.json",sj.bytes,sj.used)||ds_read(state,"prepared.bin",BW_DERIVED_TERMINAL_LIMIT,&record,&rn,NULL)||
    bw_derived_terminal_decode(&r,record,rn,&error)||!ds_identity_equal(identity,&r.original))goto done;
 uint8_t hash[32];cp_sha_bytes(record,rn,hash);if(expected&&memcmp(expected,hash,32))goto done;
 if(ds_provenance_name(provenance_name,&r.prepared)||r.provenance.size>BW_DERIVED_PROVENANCE_LIMIT||ds_read(state,provenance_name,BW_DERIVED_PROVENANCE_LIMIT,&provenance,&pn,&ps)||
    bw_derived_provenance_decode(&decoded,provenance,pn))goto done;
 const struct bw_derived_provenance_facts *f=bw_derived_provenance_view(&decoded);if(!f||!ds_identity_equal(&f->original,identity)||ds_owner_json(f,r.provenance.sha256,pn,&oj))goto done;
 n=snprintf(game_path,sizeof game_path,"%s/%s",ds_games_path,r.prepared.component);if(n<0||(size_t)n>=sizeof game_path)goto done;
 root=ds_root(game_path,&game_anchor);if(root<0||fstat(root,&gs)||fstat(state,&ss)||r.game_root.device!=(uint64_t)gs.st_dev||r.game_root.inode!=(uint64_t)gs.st_ino||
    r.state_root.device!=(uint64_t)ss.st_dev||r.state_root.inode!=(uint64_t)ss.st_ino||r.provenance_object.device!=(uint64_t)ps.st_dev||r.provenance_object.inode!=(uint64_t)ps.st_ino||
    ds_exact(root,"blackwire.generation.owner",oj.bytes,oj.used))goto done;
 if(ds_read(root,"blackwire.files",CP_MAX_MANIFEST,&map,&mn,NULL)||ds_read(root,"blackwire.generation.digests",56u+32u*BW_DERIVED_MAX_RECORDS,&digests,&dn,NULL)||
    ds_read(root,"ampr_emu.index",BW_DERIVED_TERMINAL_INDEX_LIMIT,&index,&in,NULL)||bw_generation_name(marker_name,r.run)||ds_read(root,marker_name,BW_GENERATION_SIZE,&marker,&kn,NULL))goto done;
 uint8_t combined[4096];size_t combined_size=0;if(ds_combined(&sj,&oj,combined,&combined_size))goto done;
 struct bw_derived_terminal_artifacts a={.provenance=provenance,.provenance_size=pn,.index=index,.index_size=in,.marker=marker,.marker_size=kn,.identity=combined,.identity_size=combined_size};
 if(bw_derived_terminal_validate(&r,&a,&error)||mn!=f->plan.map_size||dn!=f->plan.digest_size||memcmp(map,f->plan.map,mn)||memcmp(digests,f->plan.digests,dn))goto done;
 plan.cfg=game_anchor;memcpy(plan.cfg.run_id,r.run,33);memcpy(plan.cfg.title,r.title,10);memcpy(plan.cfg.game,r.prepared.component,75);
 plan.map=map;plan.map_size=mn;plan.digests=digests;plan.digest_size=dn;plan.index=index;plan.index_size=in;memcpy(plan.index_sha,r.index.sha256,32);strcpy(plan.phase,"index");
 if(bw_manifest_open(&plan.manifest,map,mn))goto done;plan.cfg.manifest_count=plan.manifest.count;plan.cfg.manifest_hash=plan.manifest.checksum;
 for(uint32_t i=0;i<plan.manifest.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&plan.manifest,i,&row))goto done;if(!strcmp(row.path,"eboot.bin"))plan.cfg.eboot_size=row.size;}
 if(cp_validate_map(&plan)||cp_index_rows(&plan,root)||ds_streams_empty(&plan,root,0))goto done;
 if(verify){if(!current)goto done;for(uint32_t i=0;i<plan.manifest.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&plan.manifest,i,&row))goto done;if(row.flags!=BW_MANIFEST_LOCAL)continue;
   if(!cp_alive(&plan)||current(opaque))goto done;char leaf[256],relative[1024];int parent=v2_parent(&plan.cfg,root,row.path,0,leaf,relative);if(parent<0)goto done;
   int failed=cp_existing(&plan.cfg,root,parent,relative,leaf,row.size,plan.digests+56u+32u*i);if(close(parent))failed=1;if(failed)goto done;}}
 struct stat final_provenance;
 if((current&&current(opaque))||!cp_alive(&plan)||ds_exact(state,"prepared.bin",record,rn)||ds_exact(state,provenance_name,provenance,pn)||
    cp_fstatat(state,provenance_name,&final_provenance,AT_SYMLINK_NOFOLLOW)||!ds_same(&ps,&final_provenance)||ds_exact(state,"source.json",sj.bytes,sj.used)||
    ds_exact(root,"blackwire.generation.owner",oj.bytes,oj.used)||cp_index_rows(&plan,root)||ds_streams_empty(&plan,root,0)||
    !v2_root_current(&data_anchor,data)||!v2_root_current(&scope_anchor,scope)||!v2_root_current(&state_anchor,state)||!v2_root_current(&game_anchor,root))goto done;
 ds_result(&r,record,rn,(unsigned)verify,&out->prepared);out->terminal=r;out->provenance=decoded;memset(&decoded,0,sizeof decoded);out->provenance_bytes=provenance;out->provenance_size=pn;provenance=NULL;bad=0;
 done:cp_io_guard=NULL;cp_io_opaque=NULL;free(sj.bytes);free(oj.bytes);free(record);free(provenance);free(map);free(digests);free(index);free(marker);bw_derived_provenance_destroy(&decoded);
 int closed=0;if(root>=0&&close(root))closed=1;if(state>=0&&close(state))closed=1;if(scope>=0&&close(scope))closed=1;if(data>=0&&close(data))closed=1;
 if(closed){bad=1;out->prepared.base.state=BW_PREPARED_UNKNOWN;out->prepared.base.bytes_verified=0;}
 if(bad&&out->prepared.base.state!=BW_PREPARED_ABSENT){enum bw_prepared_state state_kind=out->prepared.base.state;bw_native_derived_snapshot_destroy(out);out->prepared.base.state=state_kind;out->prepared.error="Derived preparation metadata or local verification is unconfirmed";}
 return bad?-1:0;
}
int bw_native_derived_send_inspect(const char *base,const struct bw_provider_binding *identity,struct bw_native_derived_snapshot *out){
 if(!out)return -1;if(pthread_mutex_trylock(&ds_mutex))return BW_SEND_BUSY;int r=ds_inspect(base,identity,NULL,NULL,NULL,0,out);memset(ds_games_path,0,sizeof ds_games_path);pthread_mutex_unlock(&ds_mutex);return r;
}
int bw_native_derived_send_local_verify(const char *base,const struct bw_provider_binding *identity,const uint8_t expected[32],int (*current)(void *),void *opaque,struct bw_native_derived_snapshot *out){
 if(!out||!expected||!current)return -1;if(pthread_mutex_trylock(&ds_mutex))return BW_SEND_BUSY;int r=ds_inspect(base,identity,expected,current,opaque,1,out);memset(ds_games_path,0,sizeof ds_games_path);pthread_mutex_unlock(&ds_mutex);return r;
}
static int ds_publish(struct ds_operation *o,const struct cp_plan *p,const uint8_t *record,size_t size,int *published){
 char temp[96],backup[96];int fd=-1,final=-1,result=-1,known=0;struct stat created;
 *published=0;snprintf(temp,sizeof temp,"prepared.bin.%s.new",p->cfg.run_id);snprintf(backup,sizeof backup,"prepared.bin.previous.%s",p->cfg.run_id);
 if(o->prior&&ds_blob(ds_state_path,o->state,backup,o->prior,o->prior_size,p->cfg.run_id,p->deadline))goto done;
 fd=v2_openat(o->state,temp,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600);if(fd<0)goto done;known=!fstat(fd,&created);
 if(!known||cp_regular(o->state,temp,fd,0,NULL)||fchmod(fd,0600)||cp_write_all(fd,record,size)||fsync(fd)||ds_exact(o->state,temp,record,size)||ds_current(o))goto done;
 if(o->prior){void *again=NULL;size_t n=0;struct stat after;int bad=ds_read(o->state,"prepared.bin",BW_DERIVED_TERMINAL_LIMIT,&again,&n,&after)||n!=o->prior_size||!ds_same(&o->prior_identity,&after)||(n==o->prior_size&&memcmp(again,o->prior,n));free(again);
  if(bad||cp_renameat(o->state,temp,o->state,"prepared.bin"))goto done;known=0;*published=1;
 }else{
  final=v2_openat(o->state,"prepared.bin",O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600);if(final<0)goto done;*published=1;
  if(fchmod(final,0600)||ds_current(o)||cp_regular(o->state,"prepared.bin",final,0,NULL)||cp_write_all(final,record,size)||fsync(final)||
     cp_regular(o->state,"prepared.bin",final,size,NULL)||ds_exact(o->state,"prepared.bin",record,size)||cp_regular(o->state,temp,fd,size,NULL)||
     ds_exact(o->state,temp,record,size)||cp_unlinkat(o->state,temp))goto done;known=0;
 }
 if(ds_current(o)||ds_exact(o->state,"prepared.bin",record,size)||fsync(o->state))goto done;result=0;
 done:if(result&&known)cp_cleanup_temp(&o->state_anchor,o->state,o->state,"",temp,fd,&created);
 if(fd>=0&&close(fd))result=-1;if(final>=0&&close(final))result=-1;return result&&*published?BW_SEND_UNCONFIRMED:result;
}
int bw_native_derived_send_files(const struct bw_native_derived_send_files *q,struct bw_native_derived_prepared *out){
 if(out)memset(out,0,sizeof *out);if(!out||!q||!q->data_root||!q->game_log||!q->current||!q->material||!q->original||!q->original->source||!q->project||!q->local)return BW_SEND_FAILED;
 if(pthread_mutex_trylock(&ds_mutex))return BW_SEND_BUSY;
 int result=BW_SEND_FAILED,scope=-1,games=-1,lock=-1,created=0,published=0;const char *stage="Derived source material is no longer current";
 char state_path[2048],scope_path[2048],provenance_name[96],marker_name[64];
 struct ds_operation o={.request=q,.base=-1,.state=-1,.root=-1,.source_file=-1,.owner_file=-1,.provenance_file=-1};
 struct cp_plan p={0};p.deadline=q->original->source->deadline;
 struct dpo_context c={.plan=&p,.source=q->original->source,.provider=q->original,.project=q->project,.local=q->local,.material=q->material,.selection_current=ds_current,.selection=&o};
 struct bw_json sj={.maximum=2048},oj={.maximum=2048};struct bw_derived_provenance prov={0};uint8_t *provenance=NULL;size_t provenance_size=0;const char *codec_error=NULL;
 struct bw_native_derived_snapshot prior={0};void *prior_bytes=NULL;size_t prior_size=0;uint8_t record[BW_DERIVED_TERMINAL_LIMIT],combined[4096],marker[BW_GENERATION_SIZE];size_t record_size=0,combined_size=0;
 struct bw_derived_terminal_record terminal={0};const struct bw_derived_material_view *m=bw_derived_material_view(q->material);
 const struct bw_derived_plan_view *v=bw_derived_local_view(q->local);const struct bw_pp_plan *recipe=bw_packed_project_view(q->project);
 if(!m||!v||!recipe||m->plan!=v||!ds_identity_equal(&m->original,&q->original->identity)||!ds_nonce(m->run)||
    ds_paths(q->data_root,&m->original.binding,state_path)||ds_source_json(&m->original,&sj)||q->current(q->opaque))goto done;
 struct bw_derived_provenance_input pi={.material=m,.recipe=recipe,.base_map=q->original->bundle.map.bytes,.base_digests=q->original->bundle.digests,
   .base_map_size=q->original->bundle.map.size,.base_digest_size=q->original->bundle.digest_size};
 if(bw_derived_provenance_encode(&pi,&provenance,&provenance_size,&codec_error)||bw_derived_provenance_decode(&prov,provenance,provenance_size))goto done;
 const struct bw_derived_provenance_facts *f=bw_derived_provenance_view(&prov);uint8_t provenance_sha[32];cp_sha_bytes(provenance,provenance_size,provenance_sha);
 if(!f||ds_owner_json(f,provenance_sha,provenance_size,&oj)||ds_combined(&sj,&oj,combined,&combined_size)||ds_provenance_name(provenance_name,&v->identity))goto done;
 c.view=v;ds_state_path=state_path;ds_log_path=q->game_log;o.base=ds_root(q->data_root,&o.base_anchor);
 stage="Derived destination is not an owned stopped-game preparation root";
 if(o.base<0)goto done;stage="Derived root or stopped-game controller guard refused";if(ds_current(&o))goto done;cp_io_guard=ds_current;cp_io_opaque=&o;lock=ds_lock(o.base);if(lock<0){result=BW_SEND_BUSY;goto done;}
 /* Reject prior orphan/partial state before writing anything into that scope. */
 (void)ds_inspect(q->data_root,&m->original,NULL,q->current,q->opaque,0,&prior);
 cp_io_guard=ds_current;cp_io_opaque=&o;
 if(prior.prepared.base.state!=BW_PREPARED_ABSENT&&prior.prepared.base.state!=BW_PREPARED_VALID){result=BW_SEND_UNCONFIRMED;goto done;}
 stage="Derived copier selection or current source generation refused";if(dpo_select(&c,m->run,m->original.binding.title)||dpo_generation(&c))goto done;
 stage="Derived state scope ownership or publication refused";
 scope=ds_directory(q->data_root,o.base,"native-derived-prepared",1,&created);if(scope<0)goto done;
 int n=snprintf(scope_path,sizeof scope_path,"%s/native-derived-prepared",q->data_root);if(n<0||(size_t)n>=sizeof scope_path)goto done;
 if(created){if(ds_blob(scope_path,scope,".blackwire-native-derived-prepared",ds_scope,sizeof ds_scope-1,p.cfg.run_id,p.deadline))goto done;}
 else if(ds_exact(scope,".blackwire-native-derived-prepared",ds_scope,sizeof ds_scope-1))goto done;
 o.state=ds_directory(scope_path,scope,m->original.binding.game,1,&created);if(o.state<0||v2_copy(o.state_anchor.root,sizeof o.state_anchor.root,state_path))goto done;
 if(created){if(ds_blob(state_path,o.state,"source.json",sj.bytes,sj.used,p.cfg.run_id,p.deadline))goto done;}
 else if(ds_exact(o.state,"source.json",sj.bytes,sj.used))goto done;
 o.source_file=v2_openat(o.state,"source.json",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
 if(o.source_file<0||cp_regular(o.state,"source.json",o.source_file,sj.used,&o.source_identity)||ds_current(&o))goto done;
 if(prior.prepared.base.state==BW_PREPARED_VALID){if(ds_read(o.state,"prepared.bin",BW_DERIVED_TERMINAL_LIMIT,&prior_bytes,&prior_size,&o.prior_identity))goto done;
  uint8_t hash[32];cp_sha_bytes(prior_bytes,prior_size,hash);if(memcmp(hash,prior.prepared.terminal_sha256,32))goto done;o.prior=prior_bytes;o.prior_size=prior_size;}
 stage="Derived game root ownership refused";games=ds_directory(q->data_root,o.base,"games",1,&created);if(games<0)goto done;
 o.root=ds_directory(ds_games_path,games,v->identity.component,1,&created);if(o.root<0||v2_copy(o.game_anchor.root,sizeof o.game_anchor.root,p.cfg.root))goto done;
 if(created){if(cp_blob(&p.cfg,o.root,"blackwire.generation.owner",oj.bytes,oj.used,p.cfg.run_id,p.deadline))goto done;}
 else if(ds_exact(o.root,"blackwire.generation.owner",oj.bytes,oj.used))goto done;
 o.owner_file=v2_openat(o.root,"blackwire.generation.owner",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
 if(o.owner_file<0||cp_regular(o.root,"blackwire.generation.owner",o.owner_file,oj.used,&o.owner_identity)||ds_current(&o)||cp_preflight(&p,o.root)||ds_streams_empty(&p,o.root,1))goto done;
 stage="Derived LOCAL startup copy failed; unconfirmed files were retained";
 if(cp_blob(&p.cfg,o.root,"blackwire.generation.digests",v->digests,v->digest_size,p.cfg.run_id,p.deadline)||dpo_execute(&c,o.root)||ds_streams_empty(&p,o.root,0)||dpo_generation(&c))goto done;
 stage="Derived preparation metadata publication was not confirmed";
 if(bw_generation_bytes(marker,p.cfg.run_id,m->original.binding.title,v->identity.component,v->map_sha256)||bw_generation_name(marker_name,p.cfg.run_id)||
    cp_blob(&p.cfg,o.root,marker_name,marker,sizeof marker,p.cfg.run_id,p.deadline)||ds_blob(state_path,o.state,provenance_name,provenance,provenance_size,p.cfg.run_id,p.deadline))goto done;
 struct stat gs,ss,ps;void *readback=NULL;size_t readback_size=0;
 if(ds_read(o.state,provenance_name,BW_DERIVED_PROVENANCE_LIMIT,&readback,&readback_size,&ps))goto done;
 int different=readback_size!=provenance_size||(readback_size==provenance_size&&memcmp(readback,provenance,provenance_size));free(readback);if(different||fstat(o.root,&gs)||fstat(o.state,&ss))goto done;
 o.provenance_name=provenance_name;o.provenance_file=v2_openat(o.state,provenance_name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
 if(o.provenance_file<0||cp_regular(o.state,provenance_name,o.provenance_file,provenance_size,&o.provenance_identity)||!ds_same(&ps,&o.provenance_identity)||ds_current(&o))goto done;
 terminal.original=m->original;terminal.prepared=v->identity;terminal.material_policy=m->policy;memcpy(terminal.material_policy_sha256,m->policy_sha256,32);memcpy(terminal.mapping_rule_sha256,m->mapping_rule_sha256,32);memcpy(terminal.material_sha256,m->material_sha256,32);
 memcpy(terminal.native_snapshot_sha256,v->base_snapshot_sha256,32);memcpy(terminal.base_map_sha256,v->base_map_sha256,32);memcpy(terminal.base_digests_sha256,v->base_digests_sha256,32);
 memcpy(terminal.recipe_sha256,v->recipe_sha256,32);memcpy(terminal.origins_sha256,v->origins_sha256,32);memcpy(terminal.run,p.cfg.run_id,33);memcpy(terminal.title,m->original.binding.title,10);
 ds_ref(&terminal.provenance,provenance,provenance_size);ds_ref(&terminal.map,v->map,v->map_size);ds_ref(&terminal.digests,v->digests,v->digest_size);ds_ref(&terminal.index,p.index,p.index_size);ds_ref(&terminal.marker,marker,sizeof marker);ds_ref(&terminal.identity,combined,combined_size);
 terminal.count=v->count;terminal.local_count=v->local_count;terminal.stream_count=v->stream_count;terminal.local_bytes=v->local_bytes;terminal.remote_bytes=v->remote_bytes;terminal.bootstrap_bytes=v->bootstrap_bytes;
 terminal.game_root=(struct bw_derived_object_identity){(uint64_t)gs.st_dev,(uint64_t)gs.st_ino};terminal.state_root=(struct bw_derived_object_identity){(uint64_t)ss.st_dev,(uint64_t)ss.st_ino};terminal.provenance_object=(struct bw_derived_object_identity){(uint64_t)ps.st_dev,(uint64_t)ps.st_ino};
 if(cp_measure_tree(&p,o.root,gs.st_dev,0,&terminal.allocated_bytes,&terminal.entries)||dpo_generation(&c)||ds_current(&o)||
    ds_exact(o.state,"source.json",sj.bytes,sj.used)||ds_exact(o.root,"blackwire.generation.owner",oj.bytes,oj.used)||cp_index_rows(&p,o.root)||ds_streams_empty(&p,o.root,0))goto done;
 struct bw_derived_terminal_artifacts a={.provenance=provenance,.provenance_size=provenance_size,.index=p.index,.index_size=p.index_size,.marker=marker,.marker_size=sizeof marker,.identity=combined,.identity_size=combined_size};
 if(bw_derived_terminal_validate(&terminal,&a,&codec_error)||bw_derived_terminal_encode(&terminal,record,sizeof record,&record_size,&codec_error)||fsync(o.root)||fsync(o.state)||ds_current(&o))goto done;
 result=ds_publish(&o,&p,record,record_size,&published);if(!result&&(dpo_generation(&c)||ds_current(&o)))result=BW_SEND_UNCONFIRMED;
 if(!result)ds_result(&terminal,record,record_size,1,out);
 done:cp_io_guard=NULL;cp_io_opaque=NULL;dpo_active=NULL;free(p.index);free(sj.bytes);free(oj.bytes);free(provenance);free(prior_bytes);bw_derived_provenance_destroy(&prov);bw_native_derived_snapshot_destroy(&prior);
 int closed=0;if(o.provenance_file>=0&&close(o.provenance_file))closed=1;if(o.owner_file>=0&&close(o.owner_file))closed=1;if(o.root>=0&&close(o.root))closed=1;if(games>=0&&close(games))closed=1;if(o.source_file>=0&&close(o.source_file))closed=1;
 if(o.state>=0&&close(o.state))closed=1;if(scope>=0&&close(scope))closed=1;if(lock>=0&&close(lock))closed=1;if(o.base>=0&&close(o.base))closed=1;
 if(closed&&published)result=BW_SEND_UNCONFIRMED;
 if(!result&&q->current(q->opaque))result=BW_SEND_UNCONFIRMED;
 if(result){memset(out,0,sizeof *out);out->base.state=BW_PREPARED_UNKNOWN;out->error=stage;bw_source_smb_progress(q->original->source,BW_SOURCE_ERROR,0,0,0,0);}
 else{bw_source_smb_progress(q->original->source,BW_SOURCE_PREPARED,terminal.count,terminal.count,terminal.local_bytes,terminal.local_bytes);
  if(q->current(q->opaque)){memset(out,0,sizeof *out);out->base.state=BW_PREPARED_UNKNOWN;out->error="Derived completion observer changed the selected job";result=BW_SEND_UNCONFIRMED;}}
 ds_state_path=NULL;ds_log_path=NULL;memset(ds_games_path,0,sizeof ds_games_path);pthread_mutex_unlock(&ds_mutex);return result;
}
static int ds_fresh(const struct bw_native_derived_verify *q,const struct bw_derived_terminal_record *r){
 if(q->current(q->opaque)||bw_source_provider_current(q->original)||bw_packed_project_revalidate(q->project)||bw_source_provider_current(q->original)||q->current(q->opaque))return -1;
 const struct bw_derived_plan_view *v=bw_derived_local_view(q->local);if(!v||!ds_identity_equal(&q->original->identity,&r->original)||v->identity.policy!=r->prepared.policy||
 strcmp(v->identity.component,r->prepared.component)||memcmp(v->identity.commitment_sha256,r->prepared.commitment_sha256,32)||memcmp(v->base_snapshot_sha256,r->native_snapshot_sha256,32)||
 memcmp(v->base_map_sha256,r->base_map_sha256,32)||memcmp(v->base_digests_sha256,r->base_digests_sha256,32)||memcmp(v->recipe_sha256,r->recipe_sha256,32)||memcmp(v->origins_sha256,r->origins_sha256,32)||
 memcmp(v->map_sha256,r->map.sha256,32)||memcmp(v->digests_sha256,r->digests.sha256,32))return -1;return 0;
}
int bw_native_derived_send_verify(const struct bw_native_derived_verify *q,struct bw_native_derived_snapshot *out){
 if(!q||!out||!q->original||!q->project||!q->local||!q->current||!q->expected_terminal_sha256||!q->game_log)return -1;
 if(pthread_mutex_trylock(&ds_mutex))return BW_SEND_BUSY;int result=-1;struct bw_native_derived_snapshot first={0};
 if(ds_games_stopped(q->game_log)||ds_inspect(q->data_root,&q->original->identity,q->expected_terminal_sha256,q->current,q->opaque,0,&first)||ds_fresh(q,&first.terminal))goto done;
 if(ds_inspect(q->data_root,&q->original->identity,q->expected_terminal_sha256,q->current,q->opaque,1,out)||ds_fresh(q,&out->terminal)||ds_games_stopped(q->game_log))goto done;result=0;
 done:bw_native_derived_snapshot_destroy(&first);if(result){bw_native_derived_snapshot_destroy(out);out->prepared.base.state=BW_PREPARED_UNKNOWN;out->prepared.error="Derived local copy or current source generation changed";}
 memset(ds_games_path,0,sizeof ds_games_path);pthread_mutex_unlock(&ds_mutex);return result;
}
