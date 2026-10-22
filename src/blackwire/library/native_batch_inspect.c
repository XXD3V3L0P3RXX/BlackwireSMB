#include "packed_bootstrap_rules.h"
#define BS_SCOPE_NAMES 4096u
#define BS_GENERATIONS 32u
struct bs_member {char leaf[140];struct stat identity;uint8_t pin[32];};
struct bs_collection {struct bs_member members[BS_GENERATIONS];unsigned count,absent;struct stat data,scope;};
struct bs_read_lease {int (*current)(void *);void *opaque;double deadline;};
static int bs_read_current(void *raw){struct bs_read_lease *q=raw;double at=cp_now();return at<0||at>=q->deadline||(q->current&&q->current(q->opaque))?-1:0;}
static int bs_member_compare(const void *a,const void *b){return strcmp(((const struct bs_member *)a)->leaf,((const struct bs_member *)b)->leaf);}
/* -1=matching incomplete/malformed name, 0=unrelated, 1=final old/new leaf. */
static int bs_matching_leaf(const char *leaf,const char *game){
    size_t n=strnlen(leaf,256);if(n==256)return -1;
    if(leaf[0]=='.'&&!strncmp(leaf+1,game,74)&&(leaf[75]=='.'||!leaf[75]))return -1;
    if(strcmp(leaf,game)&&strncmp(leaf,game,74))return 0;
    if(n==74)return 1;if(n<75||leaf[74]!='.')return 0;if(n!=139)return -1;
    for(size_t i=75;i<139;i++)if(!strchr("0123456789abcdef",leaf[i]))return -1;return 1;
}
static int bs_collect(const char *base,const struct bw_provider_binding *identity,struct bs_read_lease *lease,struct bs_collection *out){
    memset(out,0,sizeof *out);char legacy[2048],scope_path[2048];struct prepare_v2 data_anchor={0},scope_anchor={0};int data=-1,scope=-1,bad=1;DIR *directory=NULL;
    if(bs_read_current(lease)||!base||!identity||bs_paths(base,&identity->binding,legacy))goto done;
    data=bs_root(base,&data_anchor);if(data<0||fstat(data,&out->data))goto done;
    int n=snprintf(scope_path,sizeof scope_path,"%s/native-batch-prepared",base);if(n<0||(size_t)n>=sizeof scope_path)goto done;
    scope=bs_root(scope_path,&scope_anchor);if(scope<0){struct stat s;if(lstat(scope_path,&s)<0&&errno==ENOENT&&v2_root_current(&data_anchor,data)&&!bs_read_current(lease)){out->absent=1;bad=0;}goto done;}
    if(bs_owned(&scope_anchor,scope,0700)||bs_exact(scope,".blackwire-native-batch-prepared",bs_scope,sizeof bs_scope-1)||fstat(scope,&out->scope))goto done;
    int duplicate=dup(scope);if(duplicate<0)goto done;directory=fdopendir(duplicate);if(!directory){close(duplicate);goto done;}rewinddir(directory);unsigned names=0;
    for(;;){
        if(bs_read_current(lease))goto done;errno=0;struct dirent *entry=readdir(directory);if(!entry){if(errno)goto done;break;}
        if(++names>BS_SCOPE_NAMES)goto done;int matching=bs_matching_leaf(entry->d_name,identity->binding.game);if(matching<0)goto done;if(!matching)continue;
        if(out->count==BS_GENERATIONS)goto done;struct bs_member *m=&out->members[out->count];size_t length=strlen(entry->d_name);memcpy(m->leaf,entry->d_name,length+1);
        if(cp_fstatat(scope,m->leaf,&m->identity,AT_SYMLINK_NOFOLLOW)||!S_ISDIR(m->identity.st_mode)||(m->identity.st_mode&07777)!=0700||m->identity.st_uid!=geteuid())goto done;out->count++;
    }
    struct stat final_data,final_scope;
    if(fstat(data,&final_data)||fstat(scope,&final_scope)||!cp_measure_same(&out->data,&final_data)||!cp_measure_same(&out->scope,&final_scope)||
       !v2_root_current(&data_anchor,data)||!v2_root_current(&scope_anchor,scope)||bs_read_current(lease))goto done;
    qsort(out->members,out->count,sizeof out->members[0],bs_member_compare);bad=0;
done:if(directory&&closedir(directory))bad=1;if(scope>=0&&close(scope))bad=1;if(data>=0&&close(data))bad=1;return bad?-1:0;
}
static int bs_collection_equal(const struct bs_collection *a,const struct bs_collection *b){
    if(a->absent!=b->absent||a->count!=b->count||!cp_measure_same(&a->data,&b->data)||(!a->absent&&!cp_measure_same(&a->scope,&b->scope)))return 0;
    for(unsigned i=0;i<a->count;i++)if(strcmp(a->members[i].leaf,b->members[i].leaf)||!cp_measure_same(&a->members[i].identity,&b->members[i].identity))return 0;return 1;
}
static int bs_inspect_leaf(const char *base,const struct bw_provider_binding *identity,const char *state_leaf,const uint8_t *expected,int (*current)(void *),void *opaque,int verify,struct bw_native_batch_snapshot *out){
 if(!out||out->provenance.state||out->provenance_bytes)return -1;memset(out,0,sizeof *out);out->prepared.base.state=BW_PREPARED_INVALID;
 int data=-1,scope=-1,state=-1,root=-1,bad=1;char state_path[2048],scope_path[2048],game_path[2048],provenance_name[96],marker_name[64];
 struct prepare_v2 data_anchor={0},scope_anchor={0},state_anchor={0},game_anchor={0};struct stat gs,ss,ps;
 void *record=NULL,*provenance=NULL,*map=NULL,*digests=NULL,*index=NULL,*marker=NULL;size_t rn=0,pn=0,mn=0,dn=0,in=0,kn=0;
 struct bw_json sj={.maximum=2048},oj={.maximum=2048};struct bw_batch_provenance decoded={0};struct bw_batch_terminal_record r={0};struct cp_plan plan={0};const char *error=NULL;
 struct bs_seed *seed=NULL;struct bw_native_batch_metadata_accounting accounting={0};
 double at=cp_now();plan.deadline=at+300;cp_io_guard=current;cp_io_opaque=opaque;
 if(at<0||!base||!identity||!state_leaf||bs_matching_leaf(state_leaf,identity->binding.game)!=1||bs_paths(base,&identity->binding,state_path)||bs_source_json(identity,&sj)||(current&&current(opaque)))goto done;
 int state_length=snprintf(state_path,sizeof state_path,"%s/native-batch-prepared/%s",base,state_leaf);if(state_length<0||(size_t)state_length>=sizeof state_path)goto done;
 data=bs_root(base,&data_anchor);if(data<0)goto done;
 int n=snprintf(scope_path,sizeof scope_path,"%s/native-batch-prepared",base);if(n<0||(size_t)n>=sizeof scope_path)goto done;
 scope=bs_root(scope_path,&scope_anchor);if(scope<0){struct stat st;if(lstat(scope_path,&st)<0&&errno==ENOENT)out->prepared.base.state=BW_PREPARED_ABSENT;goto done;}
 if(bs_owned(&scope_anchor,scope,0700)||bs_exact(scope,".blackwire-native-batch-prepared",bs_scope,sizeof bs_scope-1))goto done;
 state=bs_root(state_path,&state_anchor);if(state<0){struct stat st;if(lstat(state_path,&st)<0&&errno==ENOENT)out->prepared.base.state=BW_PREPARED_ABSENT;goto done;}
 /* Existing state without a confirmed terminal is UNKNOWN, never adopted. */
 out->prepared.base.state=BW_PREPARED_UNKNOWN;
 if(bs_owned(&state_anchor,state,0700)||bs_exact(state,"source.json",sj.bytes,sj.used)||bs_read(state,"prepared.bin",BW_BATCH_TERMINAL_LIMIT,&record,&rn,NULL)||
    bw_batch_terminal_decode(&r,record,rn,&error)||!bs_identity_equal(identity,&r.original))goto done;
 char generated_leaf[140];if(strcmp(state_leaf,identity->binding.game)&&(bs_state_leaf(generated_leaf,&identity->binding,&r.prepared)||strcmp(state_leaf,generated_leaf)))goto done;
 uint8_t hash[32];cp_sha_bytes(record,rn,hash);if(expected&&memcmp(expected,hash,32))goto done;
 if(bs_provenance_name(provenance_name,&r.prepared)||r.provenance.size>BW_BATCH_PROVENANCE_LIMIT||bs_read(state,provenance_name,BW_BATCH_PROVENANCE_LIMIT,&provenance,&pn,&ps)||
    bw_batch_provenance_decode(&decoded,provenance,pn))goto done;
 const struct bw_batch_facts *f=bw_batch_provenance_view(&decoded);if(!f||!bs_identity_equal(&f->original,identity)||bs_owner_json(f,r.provenance.sha256,pn,&oj)||bs_seed_open(f,&seed))goto done;
 n=snprintf(game_path,sizeof game_path,"%s/%s",bs_games_path,r.prepared.component);if(n<0||(size_t)n>=sizeof game_path)goto done;
 root=bs_root(game_path,&game_anchor);if(root<0||bs_owned(&game_anchor,root,0755)||fstat(root,&gs)||fstat(state,&ss)||r.game_root.device!=(uint64_t)gs.st_dev||r.game_root.inode!=(uint64_t)gs.st_ino||
    r.state_root.device!=(uint64_t)ss.st_dev||r.state_root.inode!=(uint64_t)ss.st_ino||r.provenance_object.device!=(uint64_t)ps.st_dev||r.provenance_object.inode!=(uint64_t)ps.st_ino||
    bs_exact(root,"blackwire.generation.owner",oj.bytes,oj.used))goto done;
 if(bs_read(root,"blackwire.files",CP_MAX_MANIFEST,&map,&mn,NULL)||bs_read(root,"blackwire.generation.digests",56u+32u*BW_BATCH_MAX_RECORDS,&digests,&dn,NULL)||
    bs_read(root,"ampr_emu.index",BW_BATCH_TERMINAL_INDEX_LIMIT,&index,&in,NULL)||bw_generation_name(marker_name,r.run)||bs_read(root,marker_name,BW_GENERATION_SIZE,&marker,&kn,NULL))goto done;
 uint8_t combined[4096];size_t combined_size=0;if(bs_combined(&sj,&oj,combined,&combined_size))goto done;
 struct bw_batch_terminal_artifacts a={.provenance=provenance,.provenance_size=pn,.index=index,.index_size=in,.marker=marker,.marker_size=kn,.identity=combined,.identity_size=combined_size};
 if(bw_batch_terminal_validate(&r,&a,&error)||mn!=f->map_size||dn!=f->digest_size||memcmp(map,f->map,mn)||memcmp(digests,f->digests,dn))goto done;
 plan.cfg=game_anchor;memcpy(plan.cfg.run_id,r.run,33);memcpy(plan.cfg.title,r.title,10);memcpy(plan.cfg.game,r.prepared.component,75);
 plan.map=map;plan.map_size=mn;plan.digests=digests;plan.digest_size=dn;plan.index=index;plan.index_size=in;memcpy(plan.index_sha,r.index.sha256,32);strcpy(plan.phase,"index");
 if(bw_manifest_open(&plan.manifest,map,mn))goto done;plan.cfg.manifest_count=plan.manifest.count;plan.cfg.manifest_hash=plan.manifest.checksum;
 for(uint32_t i=0;i<plan.manifest.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&plan.manifest,i,&row))goto done;if(!strcmp(row.path,"eboot.bin"))plan.cfg.eboot_size=row.size;}
 if(cp_validate_map(&plan)||bs_index_rows(&plan,root)||bs_streams_verify(&plan,root,0,seed,NULL))goto done;
 if(verify){if(!current)goto done;for(uint32_t i=0;i<plan.manifest.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&plan.manifest,i,&row))goto done;if(row.flags!=BW_MANIFEST_LOCAL)continue;
   if(!cp_alive(&plan)||current(opaque))goto done;char leaf[256],relative[1024];int parent=v2_parent(&plan.cfg,root,row.path,0,leaf,relative);if(parent<0)goto done;
   int failed=bs_existing(&plan,root,parent,relative,leaf,row.size,plan.digests+56u+32u*i);if(close(parent))failed=1;if(failed)goto done;}}
 struct stat final_provenance;
 if((current&&current(opaque))||!cp_alive(&plan)||bs_exact(state,"prepared.bin",record,rn)||bs_exact(state,provenance_name,provenance,pn)||
    cp_fstatat(state,provenance_name,&final_provenance,AT_SYMLINK_NOFOLLOW)||!bs_same(&ps,&final_provenance)||bs_exact(state,"source.json",sj.bytes,sj.used)||
    bs_exact(root,"blackwire.generation.owner",oj.bytes,oj.used)||bs_index_rows(&plan,root)||bs_streams_verify(&plan,root,0,seed,&accounting)||
    !v2_root_current(&data_anchor,data)||!v2_root_current(&scope_anchor,scope)||!v2_root_current(&state_anchor,state)||!v2_root_current(&game_anchor,root))goto done;
 bs_result(&r,record,rn,(unsigned)verify,&out->prepared);out->prepared.metadata=accounting;out->terminal=r;out->provenance=decoded;memset(&decoded,0,sizeof decoded);out->provenance_bytes=provenance;out->provenance_size=pn;provenance=NULL;bad=0;
 done:if(bs_seed_destroy(seed))bad=1;cp_io_guard=NULL;cp_io_opaque=NULL;free(sj.bytes);free(oj.bytes);free(record);free(provenance);free(map);free(digests);free(index);free(marker);bw_batch_provenance_destroy(&decoded);
 int closed=0;if(root>=0&&close(root))closed=1;if(state>=0&&close(state))closed=1;if(scope>=0&&close(scope))closed=1;if(data>=0&&close(data))closed=1;
 if(closed){bad=1;out->prepared.base.state=BW_PREPARED_UNKNOWN;out->prepared.base.bytes_verified=0;}
 if(bad&&out->prepared.base.state!=BW_PREPARED_ABSENT){enum bw_prepared_state state_kind=out->prepared.base.state;bw_native_batch_snapshot_destroy(out);out->prepared.base.state=state_kind;out->prepared.error="Batch preparation metadata or local verification is unconfirmed";}
 return bad?-1:0;
}
static void bs_read_globals_clear(void){cp_io_guard=NULL;cp_io_opaque=NULL;memset(bs_games_path,0,sizeof bs_games_path);}
static int bs_find(const char *base,const struct bw_provider_binding *identity,const uint8_t *expected,int (*current)(void *),void *opaque,int verify,struct bw_native_batch_snapshot *out,char leaf[140]){
    struct bs_collection before,after;struct bw_native_batch_snapshot chosen={0};struct bs_read_lease lease={current,opaque,cp_now()+300};unsigned highest=0,highest_count=0,matches=0;char selected[140]={0};int bad=1;
    if(bs_collect(base,identity,&lease,&before))goto done;if(!before.count){out->prepared.base.state=BW_PREPARED_ABSENT;goto done;}
    for(unsigned i=0;i<before.count;i++){
        struct bw_native_batch_snapshot s={0};if(bs_inspect_leaf(base,identity,before.members[i].leaf,NULL,bs_read_current,&lease,0,&s)){bw_native_batch_snapshot_destroy(&s);goto done;}
        const struct bw_batch_facts *facts=bw_batch_provenance_view(&s.provenance);unsigned mode=facts?bw_pbb_prefix_mode(facts->bootstrap.prefix):0;int wanted=0;
        if(!mode){bw_native_batch_snapshot_destroy(&s);goto done;}
        if(expected){wanted=!memcmp(expected,s.prepared.terminal_sha256,32);if(wanted)matches++;}
        else if(mode>highest){highest=mode;highest_count=1;wanted=1;}else if(mode==highest)highest_count++;
        if(wanted){bw_native_batch_snapshot_destroy(&chosen);chosen=s;memset(&s,0,sizeof s);memcpy(selected,before.members[i].leaf,sizeof selected);}
        bw_native_batch_snapshot_destroy(&s);if(bs_read_current(&lease))goto done;
    }
    if(expected?matches!=1:highest_count!=1)goto done;
    if(bs_collect(base,identity,&lease,&after)||!bs_collection_equal(&before,&after))goto done;
    if(verify){bw_native_batch_snapshot_destroy(&chosen);if(bs_inspect_leaf(base,identity,selected,expected,bs_read_current,&lease,1,&chosen))goto done;}
    if(bs_read_current(&lease)||bs_collect(base,identity,&lease,&after)||!bs_collection_equal(&before,&after))goto done;
    if(leaf)memcpy(leaf,selected,140);*out=chosen;memset(&chosen,0,sizeof chosen);bad=0;
done:bw_native_batch_snapshot_destroy(&chosen);if(bad&&out->prepared.base.state!=BW_PREPARED_ABSENT){bw_native_batch_snapshot_destroy(out);out->prepared.base.state=BW_PREPARED_UNKNOWN;out->prepared.error="Batch generation collection is incomplete, ambiguous or changed";}return bad?-1:0;
}
static int bs_history_valid(const char *base,const struct bw_provider_binding *identity,int (*current)(void *),void *opaque){
    struct bs_collection before,after;struct bs_read_lease lease={current,opaque,cp_now()+300};int bad=1;
    if(bs_collect(base,identity,&lease,&before))goto done;
    for(unsigned i=0;i<before.count;i++){struct bw_native_batch_snapshot s={0};int r=bs_inspect_leaf(base,identity,before.members[i].leaf,NULL,bs_read_current,&lease,0,&s);const struct bw_batch_facts *f=!r?bw_batch_provenance_view(&s.provenance):NULL;int valid=f&&bw_pbb_prefix_mode(f->bootstrap.prefix);bw_native_batch_snapshot_destroy(&s);if(!valid)goto done;}
    if(bs_collect(base,identity,&lease,&after)||!bs_collection_equal(&before,&after)||bs_read_current(&lease))goto done;bad=0;
done:bs_read_globals_clear();return bad?-1:0;
}
int bw_native_batch_send_inspect(const char *base,const struct bw_provider_binding *identity,struct bw_native_batch_snapshot *out){
 if(!out||out->provenance.state||out->provenance_bytes)return -1;memset(out,0,sizeof *out);out->prepared.base.state=BW_PREPARED_UNKNOWN;if(pthread_mutex_trylock(&bs_mutex))return BW_SEND_BUSY;int r=bs_find(base,identity,NULL,NULL,NULL,0,out,NULL);bs_read_globals_clear();pthread_mutex_unlock(&bs_mutex);return r;
}
int bw_native_batch_inspect_record(const char *base,const struct bw_provider_binding *identity,const uint8_t expected[32],struct bw_native_batch_snapshot *out){
 if(!out||out->provenance.state||out->provenance_bytes||!expected)return -1;memset(out,0,sizeof *out);out->prepared.base.state=BW_PREPARED_UNKNOWN;if(pthread_mutex_trylock(&bs_mutex))return BW_SEND_BUSY;int r=bs_find(base,identity,expected,NULL,NULL,0,out,NULL);bs_read_globals_clear();pthread_mutex_unlock(&bs_mutex);return r;
}
int bw_native_batch_record_path(const char *base,const struct bw_provider_binding *identity,const uint8_t expected[32],char *out,size_t capacity){
 if(out&&capacity)out[0]=0;if(!out||!capacity||!expected)return -1;if(pthread_mutex_trylock(&bs_mutex))return BW_SEND_BUSY;struct bw_native_batch_snapshot s={0};char leaf[140];int r=bs_find(base,identity,expected,NULL,NULL,0,&s,leaf);
 if(!r){int n=snprintf(out,capacity,"%s/native-batch-prepared/%s/prepared.bin",base,leaf);if(n<0||(size_t)n>=capacity){out[0]=0;r=-1;}}
 bw_native_batch_snapshot_destroy(&s);bs_read_globals_clear();pthread_mutex_unlock(&bs_mutex);return r;
}
int bw_native_batch_send_local_verify(const char *base,const struct bw_provider_binding *identity,const uint8_t expected[32],int (*current)(void *),void *opaque,struct bw_native_batch_snapshot *out){
 if(!out||out->provenance.state||out->provenance_bytes||!expected||!current)return -1;memset(out,0,sizeof *out);out->prepared.base.state=BW_PREPARED_UNKNOWN;if(pthread_mutex_trylock(&bs_mutex))return BW_SEND_BUSY;int r=bs_find(base,identity,expected,current,opaque,1,out,NULL);bs_read_globals_clear();pthread_mutex_unlock(&bs_mutex);return r;
}
int bw_native_batch_visit(const char *base,const struct bw_provider_binding *identity,bw_native_batch_visit_fn callback,void *opaque){
    if(!callback)return -1;struct bs_collection before,after;struct bs_read_lease lease={NULL,NULL,cp_now()+300};int bad=1;
    if(pthread_mutex_trylock(&bs_mutex))return BW_SEND_BUSY;
    if(bs_collect(base,identity,&lease,&before))goto locked_done;
    /* Complete structural qualification before invoking any callback. */
    for(unsigned i=0;i<before.count;i++){struct bw_native_batch_snapshot s={0};int r=bs_inspect_leaf(base,identity,before.members[i].leaf,NULL,bs_read_current,&lease,0,&s);const struct bw_batch_facts *f=!r?bw_batch_provenance_view(&s.provenance):NULL;int valid=f&&bw_pbb_prefix_mode(f->bootstrap.prefix);if(valid)memcpy(before.members[i].pin,s.prepared.terminal_sha256,32);bw_native_batch_snapshot_destroy(&s);if(!valid)goto locked_done;}
    if(bs_collect(base,identity,&lease,&after)||!bs_collection_equal(&before,&after))goto locked_done;
    bs_read_globals_clear();pthread_mutex_unlock(&bs_mutex);
    for(unsigned i=0;i<before.count;i++){
        struct bw_native_batch_snapshot s={0};if(pthread_mutex_trylock(&bs_mutex))return BW_SEND_BUSY;
        int r=bs_inspect_leaf(base,identity,before.members[i].leaf,before.members[i].pin,bs_read_current,&lease,0,&s);bs_read_globals_clear();pthread_mutex_unlock(&bs_mutex);
        if(r){bw_native_batch_snapshot_destroy(&s);return -1;}
        r=callback(&s,opaque);bw_native_batch_snapshot_destroy(&s);if(r||bs_read_current(&lease))return -1;
        if(pthread_mutex_trylock(&bs_mutex))return BW_SEND_BUSY;
        r=bs_inspect_leaf(base,identity,before.members[i].leaf,before.members[i].pin,bs_read_current,&lease,0,&s);bw_native_batch_snapshot_destroy(&s);bs_read_globals_clear();pthread_mutex_unlock(&bs_mutex);if(r)return -1;
    }
    if(pthread_mutex_trylock(&bs_mutex))return BW_SEND_BUSY;
    /* A later callback may have changed an earlier member; recheck every pin. */
    for(unsigned i=0;i<before.count;i++){struct bw_native_batch_snapshot s={0};int r=bs_inspect_leaf(base,identity,before.members[i].leaf,before.members[i].pin,bs_read_current,&lease,0,&s);bw_native_batch_snapshot_destroy(&s);if(r)goto locked_done;}
    if(bs_collect(base,identity,&lease,&after)||!bs_collection_equal(&before,&after)||bs_read_current(&lease))goto locked_done;bad=0;
locked_done:bs_read_globals_clear();pthread_mutex_unlock(&bs_mutex);return bad?-1:0;
}
/* Known-target operations deliberately do not call bs_collect/bs_find. Scope
 * names are bounded once for selected aliases/stages; unrelated histories are
 * neither decoded nor a source of readiness authority. */
struct bs_exact_member {char leaf[140];int fd;unsigned present;struct prepare_v2 anchor;struct stat directory,record;void *bytes;size_t size;uint8_t pin[32];};
struct bs_exact_namespace {int data,scope;unsigned absent;struct prepare_v2 data_anchor,scope_anchor;struct stat data_info,scope_info;struct bs_exact_member member[2];};
static int bs_exact_prefix_fold(const char *a,const char *b){while(*b){if(!*a||bw_pbb_selector_fold((unsigned char)*a++)!=bw_pbb_selector_fold((unsigned char)*b++))return 0;}return 1;}
static int bs_exact_names(const char *component,const struct bw_provider_binding *id,char leaf[140]){
    if(!component||strnlen(component,75)!=74||memcmp(component,id->binding.title,9)||component[9]!='_')return -1;
    for(unsigned i=10;i<74;i++)if(!strchr("0123456789abcdef",component[i]))return -1;
    return snprintf(leaf,140,"%s.%s",id->binding.game,component+10)!=139?-1:0;
}
static int bs_exact_namespace_current(struct bs_exact_namespace *n,struct bs_read_lease *lease){
    struct stat now;if(bs_read_current(lease)||fstat(n->data,&now)||!cp_measure_same(&n->data_info,&now)||!v2_root_current(&n->data_anchor,n->data))return -1;
    if(n->absent){struct stat named;return lstat(n->scope_anchor.root,&named)<0&&errno==ENOENT?0:-1;}
    if(fstat(n->scope,&now)||!cp_measure_same(&n->scope_info,&now)||bs_owned(&n->scope_anchor,n->scope,0700)||bs_exact(n->scope,".blackwire-native-batch-prepared",bs_scope,sizeof bs_scope-1))return -1;
    for(unsigned i=0;i<2;i++){
        struct bs_exact_member *m=&n->member[i];struct stat named;
        if(!m->present){if(cp_fstatat(n->scope,m->leaf,&named,AT_SYMLINK_NOFOLLOW)>=0||errno!=ENOENT)return -1;continue;}
        if(fstat(m->fd,&now)||!cp_measure_same(&m->directory,&now)||bs_owned(&m->anchor,m->fd,0700)||cp_fstatat(n->scope,m->leaf,&named,AT_SYMLINK_NOFOLLOW)||!cp_measure_same(&now,&named))return -1;
        void *bytes=NULL;size_t size=0;struct stat record;int r=bs_read(m->fd,"prepared.bin",BW_BATCH_TERMINAL_LIMIT,&bytes,&size,&record);
        int bad=r||size!=m->size||(size==m->size&&memcmp(bytes,m->bytes,size))||(!r&&((record.st_mode&07777)!=0644||!bs_same(&m->record,&record)));free(bytes);if(bad)return -1;
    }return bs_read_current(lease);
}
static int bs_exact_namespace_open(const char *base,const struct bw_provider_binding *id,const char *component,struct bs_read_lease *lease,struct bs_exact_namespace *n){
    memset(n,0,sizeof *n);n->data=n->scope=-1;for(unsigned i=0;i<2;i++)n->member[i].fd=-1;
    char legacy[2048],scope_path[2048];if(bs_read_current(lease)||bs_paths(base,&id->binding,legacy)||bs_exact_names(component,id,n->member[0].leaf))return -1;
    memcpy(n->member[1].leaf,id->binding.game,75);n->data=bs_root(base,&n->data_anchor);if(n->data<0||fstat(n->data,&n->data_info))return -1;
    int length=snprintf(scope_path,sizeof scope_path,"%s/native-batch-prepared",base);if(length<0||(size_t)length>=sizeof scope_path)return -1;
    n->scope=bs_root(scope_path,&n->scope_anchor);if(n->scope<0){struct stat s;if(lstat(scope_path,&s)<0&&errno==ENOENT){n->absent=1;return bs_exact_namespace_current(n,lease);}return -1;}
    if(bs_owned(&n->scope_anchor,n->scope,0700)||bs_exact(n->scope,".blackwire-native-batch-prepared",bs_scope,sizeof bs_scope-1)||fstat(n->scope,&n->scope_info))return -1;
    int duplicate=dup(n->scope);if(duplicate<0)return -1;DIR *d=fdopendir(duplicate);if(!d){close(duplicate);return -1;}rewinddir(d);unsigned names=0;int bad=1;
    for(;;){
        if(bs_read_current(lease))goto scanned;errno=0;struct dirent *e=readdir(d);if(!e){if(errno)goto scanned;break;}
        if(++names>BS_SCOPE_NAMES||strnlen(e->d_name,256)==256)goto scanned;const char *name=e->d_name;
        /* Reject any canonical selected-name alias or malformed selected tail. */
        if(bs_exact_prefix_fold(name,n->member[0].leaf)&&strcmp(name,n->member[0].leaf))goto scanned;
        if(bs_exact_prefix_fold(name,n->member[1].leaf)&&strlen(name)==74&&strcmp(name,n->member[1].leaf))goto scanned;
        if(name[0]=='.'){
            if(bs_exact_prefix_fold(name+1,n->member[0].leaf))goto scanned;
            /* Shared legacy-key staging has no independently known component.
             * Only a recognizable different 64-digit generation prefix is
             * unrelated; missing/malformed legacy staging refuses. */
            if(bs_exact_prefix_fold(name+1,n->member[1].leaf)&&(!name[75]||name[75]=='.')){
                size_t name_length=strlen(name);int other=name_length>=140&&(!name[140]||name[140]=='.');
                for(size_t j=76;other&&j<140;j++)if(!strchr("0123456789abcdef",name[j]))other=0;
                if(!other)goto scanned;
            }
        }
        for(unsigned i=0;i<2;i++)if(!strcmp(name,n->member[i].leaf)){if(n->member[i].present)goto scanned;n->member[i].present=1;}
    }bad=0;
scanned:if(closedir(d))bad=1;if(bad)return -1;
    for(unsigned i=0;i<2;i++){
        struct bs_exact_member *m=&n->member[i];if(!m->present)continue;
        int used=snprintf(m->anchor.root,sizeof m->anchor.root,"%s/%s",scope_path,m->leaf);if(used<0||(size_t)used>=sizeof m->anchor.root)return -1;
        m->fd=v2_openat(n->scope,m->leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
        struct stat named;if(m->fd<0||bs_owned(&m->anchor,m->fd,0700)||fstat(m->fd,&m->directory)||cp_fstatat(n->scope,m->leaf,&named,AT_SYMLINK_NOFOLLOW)||!cp_measure_same(&m->directory,&named)||
            bs_read(m->fd,"prepared.bin",BW_BATCH_TERMINAL_LIMIT,&m->bytes,&m->size,&m->record)||(m->record.st_mode&07777)!=0644)return -1;
        cp_sha_bytes(m->bytes,m->size,m->pin);
    }return bs_exact_namespace_current(n,lease);
}
static int bs_exact_namespace_close(struct bs_exact_namespace *n){
    int bad=0;for(unsigned i=0;i<2;i++){free(n->member[i].bytes);if(n->member[i].fd>=0&&close(n->member[i].fd))bad=1;}if(n->scope>=0&&close(n->scope))bad=1;if(n->data>=0&&close(n->data))bad=1;return bad?-1:0;
}
static int bs_exact_path_qualified(const char *base,const struct bw_provider_binding *identity,const char *component,const struct bs_exact_member *member,struct bs_read_lease *lease){
    struct bw_batch_terminal_record r={0};struct bw_json source={.maximum=2048};struct prepare_v2 anchor={0};struct stat before,after;const char *error=NULL;char game[2048],unused[2048];int fd=-1,bad=1;
    if(bs_read_current(lease)||bw_batch_terminal_decode(&r,member->bytes,member->size,&error)||!bs_identity_equal(identity,&r.original)||strcmp(component,r.prepared.component)||
        r.state_root.device!=(uint64_t)member->directory.st_dev||r.state_root.inode!=(uint64_t)member->directory.st_ino||bs_source_json(identity,&source)||bs_exact(member->fd,"source.json",source.bytes,source.used)||bs_paths(base,&identity->binding,unused))goto done;
    int n=snprintf(game,sizeof game,"%s/%s",bs_games_path,component);if(n<0||(size_t)n>=sizeof game)goto done;
    fd=bs_root(game,&anchor);if(fd<0||bs_owned(&anchor,fd,0755)||fstat(fd,&before)||r.game_root.device!=(uint64_t)before.st_dev||r.game_root.inode!=(uint64_t)before.st_ino||
        bs_exact(member->fd,"source.json",source.bytes,source.used)||fstat(fd,&after)||!cp_measure_same(&before,&after)||bs_owned(&anchor,fd,0755)||bs_read_current(lease))goto done;bad=0;
done:free(source.bytes);if(fd>=0&&close(fd))bad=1;return bad?-1:0;
}
static int bs_find_exact(const char *base,const struct bw_provider_binding *identity,const char *component,const uint8_t expected[32],int (*current)(void *),void *opaque,int verify,struct bw_native_batch_snapshot *out,char leaf[140]){
    struct bs_exact_namespace space={.data=-1,.scope=-1,.member={{.fd=-1},{.fd=-1}}};struct bw_native_batch_snapshot chosen={0};struct bs_read_lease lease={current,opaque,cp_now()+300};
    struct bw_provider_binding captured;char root[2048],prepared[75];uint8_t pin[32];int bad=1;unsigned selected=0;
    if(!base||!identity||!component||!expected||v2_copy(root,sizeof root,base)||strnlen(component,sizeof prepared)!=74)goto done;
    captured=*identity;memcpy(prepared,component,sizeof prepared);memcpy(pin,expected,32);cp_io_guard=bs_read_current;cp_io_opaque=&lease;
    if(bs_exact_namespace_open(root,&captured,prepared,&lease,&space))goto done;
    if(space.absent||(!space.member[0].present&&!space.member[1].present)){out->prepared.base.state=BW_PREPARED_ABSENT;goto done;}
    if(space.member[0].present){
        if(memcmp(space.member[0].pin,pin,32)||(space.member[1].present&&!memcmp(space.member[1].pin,pin,32)))goto done;
    }else{selected=1;if(!space.member[1].present||memcmp(space.member[1].pin,pin,32))goto done;}
    if(verify<0){if(bs_exact_path_qualified(root,&captured,prepared,&space.member[selected],&lease))goto done;}
    else{
        if(bs_inspect_leaf(root,&captured,space.member[selected].leaf,pin,bs_read_current,&lease,verify,&chosen))goto done;
        cp_io_guard=bs_read_current;cp_io_opaque=&lease;const struct bw_batch_facts *facts=bw_batch_provenance_view(&chosen.provenance);
        if(!facts||!bw_pbb_prefix_mode(facts->bootstrap.prefix)||strcmp(chosen.prepared.prepared.component,prepared)||strcmp(facts->prepared.component,prepared)||memcmp(chosen.prepared.terminal_sha256,pin,32)||
            chosen.terminal.state_root.device!=(uint64_t)space.member[selected].directory.st_dev||chosen.terminal.state_root.inode!=(uint64_t)space.member[selected].directory.st_ino)goto done;
    }
    if(bs_exact_namespace_current(&space,&lease)||memcmp(identity,&captured,sizeof captured)||strnlen(base,sizeof root)!=strlen(root)||strcmp(base,root)||strnlen(component,sizeof prepared)!=74||memcmp(component,prepared,sizeof prepared)||memcmp(expected,pin,32)||bs_read_current(&lease))goto done;
    if(leaf)memcpy(leaf,space.member[selected].leaf,140);bad=0;
done:if(bs_exact_namespace_close(&space))bad=1;
    if(!bad){*out=chosen;memset(&chosen,0,sizeof chosen);}else if(out->prepared.base.state!=BW_PREPARED_ABSENT){bw_native_batch_snapshot_destroy(out);out->prepared.base.state=BW_PREPARED_UNKNOWN;out->prepared.error="Exact batch component, terminal or selected namespace is unconfirmed";}
    bw_native_batch_snapshot_destroy(&chosen);return bad?-1:0;
}
int bw_native_batch_inspect_exact(const char *base,const struct bw_provider_binding *identity,const char *component,const uint8_t expected[32],struct bw_native_batch_snapshot *out){
    if(!out||out->provenance.state||out->provenance_bytes)return -1;memset(out,0,sizeof *out);out->prepared.base.state=BW_PREPARED_UNKNOWN;if(pthread_mutex_trylock(&bs_mutex))return BW_SEND_BUSY;
    int r=bs_find_exact(base,identity,component,expected,NULL,NULL,0,out,NULL);bs_read_globals_clear();pthread_mutex_unlock(&bs_mutex);return r;
}
int bw_native_batch_local_verify_exact(const char *base,const struct bw_provider_binding *identity,const char *component,const uint8_t expected[32],int (*current)(void *),void *opaque,struct bw_native_batch_snapshot *out){
    if(!out||out->provenance.state||out->provenance_bytes||!current)return -1;memset(out,0,sizeof *out);out->prepared.base.state=BW_PREPARED_UNKNOWN;if(pthread_mutex_trylock(&bs_mutex))return BW_SEND_BUSY;
    int r=bs_find_exact(base,identity,component,expected,current,opaque,1,out,NULL);bs_read_globals_clear();pthread_mutex_unlock(&bs_mutex);return r;
}
int bw_native_batch_record_path_exact(const char *base,const struct bw_provider_binding *identity,const char *component,const uint8_t expected[32],char *out,size_t capacity){
    if(out&&capacity)out[0]=0;if(!out||!capacity)return -1;if(pthread_mutex_trylock(&bs_mutex))return BW_SEND_BUSY;struct bw_native_batch_snapshot s={0};char leaf[140];int r=bs_find_exact(base,identity,component,expected,NULL,NULL,-1,&s,leaf);
    if(!r){int n=snprintf(out,capacity,"%s/native-batch-prepared/%s/prepared.bin",base,leaf);if(n<0||(size_t)n>=capacity){out[0]=0;r=-1;}}
    bw_native_batch_snapshot_destroy(&s);bs_read_globals_clear();pthread_mutex_unlock(&bs_mutex);return r;
}
