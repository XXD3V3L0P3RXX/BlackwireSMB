#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifdef BW_NATIVE_SEND_HOST_TEST
#define BW_COPY_HOST_TEST
#endif
static const char *ns_state_path,*ns_log_path;
static char ns_games_path[2048];
#define BW_COPY_DATA ns_state_path
#define BW_GAMES_ROOT ns_games_path
#define BW_COPY_GAME_LOG ns_log_path
#ifndef BW_NATIVE_SEND_HOST_TEST
/* The embedded standalone status writer is unused here. The controller's job
 * cache and verified prepared.bin record own this library operation's state. */
struct cp_plan;
static int cp_status(const struct cp_plan *,int,const char *,const char *) __attribute__((unused));
#endif
#include "native_prepare_ops.c"
#include "native_send.h"
#include "native_json.h"
#include <pthread.h>
#include <sys/file.h>
#define NS_RECORD_SIZE 384u
static pthread_mutex_t ns_mutex=PTHREAD_MUTEX_INITIALIZER;
static const char ns_scope[]="Blackwire native preparations v1\n";
struct ns_operation {
    const struct bw_native_send_files *request;
    struct prepare_v2 base_anchor,state_anchor;
    int base,state,source_file;
    struct stat source_identity;
    bw_source_observer_fn observe;
    void *observe_opaque;
};
static int ns_same(const struct stat *a,const struct stat *b){return cp_measure_same(a,b)&&S_ISREG(b->st_mode)&&b->st_nlink==1;}
static int ns_nonce(const char *value){
    if(!value||strnlen(value,33)!=32)return 0;
    unsigned nonzero=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",value[i]))return 0;nonzero|=value[i]!='0';}return nonzero!=0;
}
static int ns_paths(const char *base,const struct bw_source_binding *binding,char state[2048]){
    if(!base||!binding||strnlen(binding->game,sizeof binding->game)!=74||strnlen(binding->source,sizeof binding->source)>BW_MANIFEST_MAX_PATH||
        bw_manifest_validate_path(binding->source,strlen(binding->source))||strnlen(binding->title,sizeof binding->title)!=9||memcmp(binding->title,"PPSA",4)||
        memcmp(binding->game,binding->title,9)||binding->game[9]!='_'||!v2_game_component(binding->game))return -1;
    for(unsigned i=4;i<9;i++)if(binding->title[i]<'0'||binding->title[i]>'9')return -1;
    char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",binding->sha256[i]);if(strcmp(binding->game+10,hash))return -1;
    int a=snprintf(ns_games_path,sizeof ns_games_path,"%s/games",base),b=snprintf(state,2048,"%s/native-prepared/%s",base,binding->game);
    return a<0||(size_t)a>=sizeof ns_games_path||b<0||b>=2048?-1:0;
}
static int ns_root(const char *path,struct prepare_v2 *anchor){
    memset(anchor,0,sizeof *anchor);if(v2_copy(anchor->root,sizeof anchor->root,path))return -1;
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd>=0&&!v2_root_current(anchor,fd)){close(fd);fd=-1;}return fd;
}
static int ns_directory(const char *path,int parent,const char *leaf,int create,int *created){
    struct prepare_v2 anchor={0};*created=0;if(v2_copy(anchor.root,sizeof anchor.root,path)||!v2_root_current(&anchor,parent)||!v2_game_component(leaf))return -1;
    int fd=v2_openat(parent,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
    if(fd<0&&errno==ENOENT&&create){if(v2_mkdirat(parent,leaf,0755))return -1;*created=1;fd=v2_openat(parent,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);if(fd>=0&&fchmod(fd,0755)){close(fd);return -1;}}
    char child[2048];int n=snprintf(child,sizeof child,"%s/%s",path,leaf);
    if(fd<0||n<0||(size_t)n>=sizeof child){if(fd>=0)close(fd);return -1;}
    if(!v2_root_current(&anchor,parent)||v2_copy(anchor.root,sizeof anchor.root,child)||!v2_root_current(&anchor,fd)||(create&&fsync(parent))){close(fd);return -1;}return fd;
}
static int ns_read(int parent,const char *leaf,size_t maximum,void **bytes,size_t *size,struct stat *identity){
    *bytes=NULL;*size=0;int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0)return errno==ENOENT?1:-1;
    struct stat before,after;int result=-1;void *body=NULL;
    if(fstat(fd,&before)||!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_size<1||(uint64_t)before.st_size>maximum||cp_regular(parent,leaf,fd,(uint64_t)before.st_size,NULL))goto done;
    body=malloc((size_t)before.st_size);if(!body||v2_read_all(fd,body,(size_t)before.st_size)||cp_regular(parent,leaf,fd,(uint64_t)before.st_size,&after)||!ns_same(&before,&after))goto done;
    if(identity)*identity=after;*bytes=body;*size=(size_t)before.st_size;body=NULL;result=0;
done:
    free(body);if(close(fd))result=-1;return result;
}
static int ns_exact(int parent,const char *leaf,const void *expected,size_t size){
    void *bytes=NULL;size_t got=0;int result=ns_read(parent,leaf,size,&bytes,&got,NULL);int bad=result||got!=size||(got==size&&memcmp(bytes,expected,size));free(bytes);return bad?-1:0;
}
static int ns_binding_json(const struct bw_source_binding *binding,struct bw_json *json){
    bw_json_text(json,"{\"version\":1,\"source\":");bw_json_string(json,binding->source);bw_json_text(json,",\"game\":");bw_json_string(json,binding->game);
    bw_json_text(json,",\"titleId\":");bw_json_string(json,binding->title);char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",binding->sha256[i]);
    bw_json_text(json,",\"bindingSha256\":");bw_json_string(json,hash);bw_json_text(json,"}\n");return json->bad?-1:0;
}
static int ns_provider_json(const struct bw_provider_binding *identity,struct bw_json *json){
    if(!identity||identity->policy!=BW_PROVIDER_POLICY_V1||
       (identity->kind!=BW_PROVIDER_FOLDER&&identity->kind!=BW_PROVIDER_CONTAINER))return -1;
    if(ns_binding_json(&identity->binding,json))return -1;
    if(identity->kind==BW_PROVIDER_CONTAINER){
        if(json->used<2)return -1;json->used-=2;
        bw_json_text(json,",\"sourceKind\":\"container\",\"sourcePolicy\":\"unsigned-contiguous-d32-pfs-v1\"}\n");
    }
    return json->bad?-1:0;
}
static int ns_blob(const char *path,int directory,const char *leaf,const void *bytes,size_t size,const char *run,double deadline){
    struct prepare_v2 anchor={0};if(v2_copy(anchor.root,sizeof anchor.root,path)||!v2_root_current(&anchor,directory))return -1;
    return cp_blob(&anchor,directory,leaf,bytes,size,run,deadline)||ns_exact(directory,leaf,bytes,size)?-1:0;
}
static int ns_lock(int directory){
    static const char marker[]="Blackwire native send lock v1\n";const char *leaf=".blackwire-send.lock";
    int fd=v2_openat(directory,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600),created=fd>=0;
    if(fd<0&&errno==EEXIST)fd=v2_openat(directory,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(fd<0)return -1;
    char bytes[sizeof marker];struct stat st;
    if((created&&(fchmod(fd,0600)||cp_write_all(fd,marker,sizeof marker-1)||fsync(fd)||fsync(directory)))||
        cp_regular(directory,leaf,fd,sizeof marker-1,&st)||pread(fd,bytes,sizeof marker-1,0)!=(ssize_t)(sizeof marker-1)||memcmp(bytes,marker,sizeof marker-1)||flock(fd,LOCK_EX|LOCK_NB)){close(fd);return -1;}return fd;
}
static int ns_games_stopped(const char *path){
    if(cp_idle_guard)return cp_games_stopped(path);
    if(v2_games_stopped(path))return -1;
    struct {char title[16];int pid,active;} rows[128]={0};unsigned count=0;FILE *file=fopen(path,"r");if(!file)return -1;char line[1024];int bad=0;
    while(fgets(line,sizeof line,file)){
        if(!strchr(line,'\n')&&!feof(file)){bad=1;break;}
        char *at=NULL;int active=1;
        if((at=strstr(line,"[GAME] started: PPSA")))at+=strlen("[GAME] started: ");
        else if((at=strstr(line,"[GAME] process exited, awaiting same-app handoff: PPSA")))at+=strlen("[GAME] process exited, awaiting same-app handoff: ");
        else if((at=strstr(line,"[GAME] finalizing stopped application: PPSA"))){at+=strlen("[GAME] finalizing stopped application: ");active=0;}else continue;
        char title[16]={0};int pid=0;if(sscanf(at,"%15s pid=%d",title,&pid)!=2||strlen(title)!=9||strncmp(title,"PPSA",4)||pid<=0){bad=1;break;}
        for(unsigned i=4;i<9;i++)if(title[i]<'0'||title[i]>'9')bad=1;
        unsigned i=0;for(;i<count;i++)if(!strcmp(rows[i].title,title))break;
        if(i==count){if(count==128){bad=1;break;}memcpy(rows[i].title,title,sizeof title);count++;}
        if(active){rows[i].pid=pid;rows[i].active=1;}else if(rows[i].pid==pid)rows[i].active=0;
    }
    if(ferror(file)||fclose(file))bad=1;for(unsigned i=0;i<count;i++)if(rows[i].active)bad=1;return bad?-1:0;
}
static int ns_current(void *opaque){
    struct ns_operation *p=opaque;
    if(!p->request->current||p->request->current(p->request->opaque)||ns_games_stopped(ns_log_path)||!v2_root_current(&p->base_anchor,p->base))return -1;
    if(p->state>=0&&!v2_root_current(&p->state_anchor,p->state))return -1;
    if(p->source_file>=0){struct stat held,named;if(fstat(p->source_file,&held)||cp_fstatat(p->state,"source.json",&named,AT_SYMLINK_NOFOLLOW)||!ns_same(&p->source_identity,&held)||!ns_same(&held,&named))return -1;}
    return 0;
}
static void ns_observe(void *opaque,const struct bw_source_progress *progress){
    struct ns_operation *p=opaque;if(progress->phase!=BW_SOURCE_PREPARED&&p->observe)p->observe(p->observe_opaque,progress);
}
static int ns_record_valid(const uint8_t record[NS_RECORD_SIZE],const struct bw_source_binding *binding){
    uint8_t hash[32];cp_sha_bytes(record,352,hash);char run[33];memcpy(run,record+16,32);run[32]=0;
    if(memcmp(record,"BWNAT001",8)||cp_u32(record+8)!=1||cp_u32(record+12)!=NS_RECORD_SIZE||!ns_nonce(run)||memcmp(record+48,binding->sha256,32)||memcmp(record+352,hash,32))return -1;
    for(unsigned i=336;i<352;i++)if(record[i])return -1;
    uint32_t count=cp_u32(record+268),local=cp_u32(record+272),stream=cp_u32(record+276);
    if(!count||count>BW_MANIFEST_MAX_FILES||local>count||stream!=count-local||cp_u32(record+264)>262144||cp_u64(record+280)<40||cp_u64(record+280)>CP_MAX_MANIFEST||
        cp_u64(record+288)!=56u+32u*count||cp_u64(record+296)<48||cp_u64(record+296)>CP_MAX_INDEX||cp_u64(record+240)>UINT64_C(9007199254740991)||
        cp_u64(record+248)>UINT64_C(9007199254740991)-cp_u64(record+240)||cp_u64(record+256)>UINT64_C(9007199254740991))return -1;
    return 0;
}
static int ns_streams_empty(struct cp_plan *plan,int root,int missing_ok){
    for(uint32_t i=0;i<plan->manifest.count;i++){
        struct bw_manifest_record row;if(bw_manifest_get(&plan->manifest,i,&row))return -1;if(row.flags!=BW_MANIFEST_STREAM)continue;
        char leaf[256],relative[1024];int parent=v2_parent(&plan->cfg,root,row.path,0,leaf,relative);if(parent==-2&&missing_ok)continue;if(parent<0)return -1;
        int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0){int absent=errno==ENOENT;close(parent);if(absent&&missing_ok)continue;return -1;}
        struct stat st;int bad=cp_regular(parent,leaf,fd,row.size,&st)||st.st_blocks||!v2_directory_current(&plan->cfg,root,parent,relative);
        if(close(fd))bad=1;if(close(parent))bad=1;if(bad)return -1;
    }return 0;
}
static void ns_result(const uint8_t record[NS_RECORD_SIZE],const struct bw_source_binding *binding,unsigned verified,struct bw_native_prepared *result){
    memset(result,0,sizeof *result);result->state=BW_PREPARED_VALID;memcpy(result->game,binding->game,sizeof result->game);memcpy(result->run,record+16,32);
    result->local_bytes=cp_u64(record+240);result->remote_bytes=cp_u64(record+248);result->allocated_bytes=cp_u64(record+256);result->entries=cp_u32(record+264);
    result->local_count=cp_u32(record+272);result->stream_count=cp_u32(record+276);result->bytes_verified=verified;
    cp_sha_bytes(record,NS_RECORD_SIZE,result->record_sha256);
}
static int ns_publish(struct ns_operation *operation,const struct cp_plan *plan,const uint8_t record[NS_RECORD_SIZE],int *published){
    const char *leaf="prepared.bin";char temp[96],backup[96];void *prior=NULL;size_t prior_size=0;struct stat before,after;int fd=-1,final=-1,result=BW_SEND_FAILED;
    *published=0;int loaded=ns_read(operation->state,leaf,NS_RECORD_SIZE,&prior,&prior_size,&before);
    if(loaded<0||(loaded==0&&(prior_size!=NS_RECORD_SIZE||ns_record_valid(prior,&operation->request->provider->identity.binding))))goto done;
    snprintf(temp,sizeof temp,"prepared.bin.%s.new",plan->cfg.run_id);snprintf(backup,sizeof backup,"prepared.bin.previous.%s",plan->cfg.run_id);
    if(!loaded&&ns_blob(ns_state_path,operation->state,backup,prior,prior_size,plan->cfg.run_id,plan->deadline))goto done;
    fd=v2_openat(operation->state,temp,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600);
    if(fd<0||fchmod(fd,0600)||cp_write_all(fd,record,NS_RECORD_SIZE)||fsync(fd)||ns_exact(operation->state,temp,record,NS_RECORD_SIZE)||ns_current(operation)||cp_games_stopped(ns_log_path))goto done;
    if(!loaded){
        void *again=NULL;size_t size=0;int checked=ns_read(operation->state,leaf,NS_RECORD_SIZE,&again,&size,&after);
        int bad=checked||size!=prior_size||!ns_same(&before,&after)||(size==prior_size&&memcmp(prior,again,size));free(again);if(bad)goto done;
        if(cp_renameat(operation->state,temp,operation->state,leaf))goto done;*published=1;
    }else{
        /* Fresh publication cannot depend on unsupported console hard links.
         * Reserve without replacing any existing name. A partial first copy
         * remains unconfirmed, so another Send cannot silently overwrite it. */
        final=v2_openat(operation->state,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600);
        if(final<0)goto done;*published=1;
        if(fchmod(final,0600)||ns_current(operation)||cp_regular(operation->state,leaf,final,0,NULL)||cp_write_all(final,record,NS_RECORD_SIZE)||fsync(final)||
            cp_regular(operation->state,leaf,final,NS_RECORD_SIZE,NULL)||ns_exact(operation->state,leaf,record,NS_RECORD_SIZE)||
            cp_regular(operation->state,temp,fd,NS_RECORD_SIZE,NULL)||ns_exact(operation->state,temp,record,NS_RECORD_SIZE)||cp_unlinkat(operation->state,temp))goto done;
        int old=fd;fd=final;final=-1;if(close(old))goto done;
    }
    if(ns_current(operation)||ns_exact(operation->state,leaf,record,NS_RECORD_SIZE)||fsync(operation->state))goto done;
    result=BW_SEND_OK;
done:
    if(fd>=0&&close(fd))result=BW_SEND_FAILED;if(final>=0&&close(final))result=BW_SEND_FAILED;free(prior);return result&&*published?BW_SEND_UNCONFIRMED:result;
}
static int ns_inspect_locked(const char *,const struct bw_provider_binding *,const uint8_t *,
    struct bw_source_provider *,int (*)(void *),void *,struct bw_native_prepared *);
#include "native_startup_upgrade.c"
/* A missing native state is not proof that the owned namespace is empty.
 * Reuse the existing root/owner and private retirement predicates read-only;
 * never construct a provider, repeat a move, or adopt a retained generation. */
static enum bw_prepared_state ns_absence_state(const char *base,const struct bw_provider_binding *identity,
    const struct bw_json *source_json,int data,const struct prepare_v2 *data_anchor,struct bw_native_prepared *out){
    char game_path[2048],rollback_path[2048];struct prepare_v2 game_anchor={0},rollback_anchor={0};
    int game=-1,rollback=-1;enum bw_prepared_state result=BW_PREPARED_UNKNOWN;struct stat named,data_stat;
    const char *component=identity->binding.game;int n=snprintf(game_path,sizeof game_path,"%s/%s",ns_games_path,component);
    if(n<0||(size_t)n>=sizeof game_path||fstat(data,&data_stat))goto done;
    game=ns_root(game_path,&game_anchor);
    if(game>=0){
        struct stat held;
        result=fstat(game,&held)||held.st_uid!=geteuid()||held.st_dev!=data_stat.st_dev||
            ns_exact(game,"blackwire.generation.owner",source_json->bytes,source_json->used)||
            !v2_root_current(&game_anchor,game)?BW_PREPARED_INVALID:BW_PREPARED_UNKNOWN;
        goto done;
    }
    if(lstat(game_path,&named)==0||errno!=ENOENT)goto done;
    n=snprintf(rollback_path,sizeof rollback_path,"%s/%s",base,us_scope_name);
    if(n<0||(size_t)n>=sizeof rollback_path)goto done;
    rollback=ns_root(rollback_path,&rollback_anchor);
    if(rollback<0){if(lstat(rollback_path,&named)<0&&errno==ENOENT)result=BW_PREPARED_ABSENT;goto final;}
    struct stat scope_id;
    if(us_private(rollback,&scope_id)||scope_id.st_dev!=data_stat.st_dev||
       ns_exact(rollback,us_marker_name,us_marker,sizeof us_marker-1))goto done;
    int duplicate=dup(rollback);if(duplicate<0)goto done;DIR *directory=fdopendir(duplicate);
    if(!directory){close(duplicate);goto done;}rewinddir(directory);int bad=0;unsigned count=0;
    for(;;){
        errno=0;struct dirent *entry=readdir(directory);if(!entry){if(errno)bad=1;break;}
        if(++count>4096){bad=1;break;}
        if(strncmp(entry->d_name,component,74)||entry->d_name[74]!='.')continue;
        if(strlen(entry->d_name)!=107||!ns_nonce(entry->d_name+75)){bad=1;break;}
        int child=v2_openat(rollback,entry->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);struct stat child_id;
        if(child<0||us_private(child,&child_id)||child_id.st_dev!=scope_id.st_dev||
           us_complete(child)||us_named(rollback,entry->d_name,child,&child_id))bad=1;
        if(child>=0&&close(child))bad=1;if(bad)break;
    }
    if(closedir(directory))bad=1;
    if(bad){out->retirement=BW_RETIRE_RECOVERY_REQUIRED;out->retirement_error="An owned startup retirement is incomplete or unconfirmed; inspect without replay";goto done;}
    if(!v2_root_current(&rollback_anchor,rollback))goto done;
    result=BW_PREPARED_ABSENT;
final:
    if(lstat(game_path,&named)==0||errno!=ENOENT)result=BW_PREPARED_UNKNOWN;
done:
    if(!v2_root_current(data_anchor,data))result=BW_PREPARED_UNKNOWN;
    if(game>=0&&close(game))result=BW_PREPARED_UNKNOWN;
    if(rollback>=0&&close(rollback))result=BW_PREPARED_UNKNOWN;
    return result;
}
int bw_native_send_files(const struct bw_native_send_files *request,struct bw_native_prepared *result){
    if(result)memset(result,0,sizeof *result);
    if(!request||!result||!request->provider||!request->provider->source||!request->current||!request->game_log)return BW_SEND_FAILED;
    if(pthread_mutex_trylock(&ns_mutex))return BW_SEND_BUSY;
    cp_idle_guard=request->idle;cp_idle_opaque=request->opaque;
    int outcome=BW_SEND_FAILED,lock=-1,scope=-1,games=-1,root=-1,created=0,published=0,observed=0;char state_path[2048],scope_path[2048];
    struct bw_source_smb *source=request->provider->source;
    const struct bw_provider_binding *identity=&request->provider->identity;
    const struct bw_source_binding *binding=&identity->binding;
    const struct bw_provider_bundle *bundle=&request->provider->bundle;
    struct ns_operation operation={.request=request,.base=-1,.state=-1,.source_file=-1};struct cp_plan plan={0};struct bw_json identity_json={.maximum=4096};
    struct np_context copy={.plan=&plan,.source=source,.bundle=bundle,.provider=request->provider,.selection_current=ns_current,.selection=&operation};
    struct ns_upgrade upgrade=NS_UPGRADE_INIT;
    plan.deadline=source->deadline;
    if(ns_paths(request->data_root,binding,state_path)||!bundle->ready||bw_source_provider_current(request->provider)||strcmp(bundle->source,binding->source)||
        strcmp(bundle->game,binding->game)||strcmp(bundle->title,binding->title)||ns_provider_json(identity,&identity_json))goto done;
    ns_state_path=state_path;ns_log_path=request->game_log;operation.base=ns_root(request->data_root,&operation.base_anchor);
    if(operation.base<0||ns_current(&operation)||cp_games_stopped(ns_log_path))goto done;
    cp_io_guard=ns_current;cp_io_opaque=&operation;
    lock=ns_lock(operation.base);if(lock<0){outcome=BW_SEND_BUSY;goto done;}
    if(np_select(&copy)||!ns_nonce(plan.cfg.run_id))goto done;
    if(ns_upgrade_begin(&operation,&plan,&upgrade)||ns_paths(request->data_root,binding,state_path))goto done;
    cp_io_guard=ns_current;cp_io_opaque=&operation;
    scope=ns_directory(request->data_root,operation.base,"native-prepared",1,&created);if(scope<0)goto done;
    int n=snprintf(scope_path,sizeof scope_path,"%s/native-prepared",request->data_root);if(n<0||(size_t)n>=sizeof scope_path)goto done;
    if(created){if(ns_blob(scope_path,scope,".blackwire-native-prepared",ns_scope,sizeof ns_scope-1,plan.cfg.run_id,plan.deadline))goto done;}
    else if(ns_exact(scope,".blackwire-native-prepared",ns_scope,sizeof ns_scope-1))goto done;
    if(ns_current(&operation))goto done;
    operation.state=ns_directory(scope_path,scope,binding->game,1,&created);if(operation.state<0||v2_copy(operation.state_anchor.root,sizeof operation.state_anchor.root,state_path))goto done;
    if(created){if(ns_blob(state_path,operation.state,"source.json",identity_json.bytes,identity_json.used,plan.cfg.run_id,plan.deadline))goto done;}
    else if(ns_exact(operation.state,"source.json",identity_json.bytes,identity_json.used))goto done;
    operation.source_file=v2_openat(operation.state,"source.json",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(operation.source_file<0||cp_regular(operation.state,"source.json",operation.source_file,identity_json.used,&operation.source_identity)||ns_current(&operation))goto done;
    games=ns_directory(request->data_root,operation.base,"games",1,&created);if(games<0||ns_current(&operation))goto done;
    root=ns_directory(ns_games_path,games,binding->game,1,&created);if(root<0)goto done;
    if(created){if(cp_blob(&plan.cfg,root,"blackwire.generation.owner",identity_json.bytes,identity_json.used,plan.cfg.run_id,plan.deadline))goto done;}
    else if(ns_exact(root,"blackwire.generation.owner",identity_json.bytes,identity_json.used))goto done;
    uint8_t digest_hash[32];cp_sha_bytes(bundle->digests,bundle->digest_size,digest_hash);
    if(ns_current(&operation)||cp_existing(&plan.cfg,root,root,"","blackwire.generation.digests",bundle->digest_size,digest_hash)<0||cp_preflight(&plan,root)||ns_streams_empty(&plan,root,1)||
        cp_blob(&plan.cfg,root,"blackwire.generation.digests",bundle->digests,bundle->digest_size,plan.cfg.run_id,plan.deadline))goto done;
    operation.observe=source->observe;operation.observe_opaque=source->observe_opaque;
    source->observe=ns_observe;source->observe_opaque=&operation;observed=1;
    int failed=np_execute(&copy,operation.state,root)||np_generation(&copy)||ns_current(&operation);
    if(failed)goto done;
    struct stat game_stat,state_stat;if(fstat(root,&game_stat)||fstat(operation.state,&state_stat))goto done;
    uint64_t allocated=0;uint32_t entries=0;if(cp_measure_tree(&plan,root,game_stat.st_dev,0,&allocated,&entries)||ns_current(&operation))goto done;
    uint8_t record[NS_RECORD_SIZE]={0},marker[BW_GENERATION_SIZE],map_hash[32];cp_sha_bytes(bundle->map.bytes,bundle->map.size,map_hash);
    if(bw_generation_bytes(marker,plan.cfg.run_id,bundle->title,binding->game,map_hash))goto done;
    char marker_name[64];if(bw_generation_name(marker_name,plan.cfg.run_id)||ns_exact(root,marker_name,marker,sizeof marker))goto done;
    memcpy(record,"BWNAT001",8);np_put32(record+8,1);np_put32(record+12,NS_RECORD_SIZE);memcpy(record+16,plan.cfg.run_id,32);memcpy(record+48,binding->sha256,32);
    memcpy(record+80,bundle->generation_sha256,32);memcpy(record+112,map_hash,32);memcpy(record+144,digest_hash,32);memcpy(record+176,plan.index_sha,32);cp_sha_bytes(marker,sizeof marker,record+208);
    np_put64(record+240,bundle->map.local_bytes);np_put64(record+248,bundle->map.remote_bytes);np_put64(record+256,allocated);np_put32(record+264,entries);np_put32(record+268,plan.manifest.count);
    np_put32(record+272,bundle->map.local_count);np_put32(record+276,bundle->map.stream_count);np_put64(record+280,bundle->map.size);np_put64(record+288,bundle->digest_size);np_put64(record+296,plan.index_size);
    np_put64(record+304,(uint64_t)game_stat.st_dev);np_put64(record+312,(uint64_t)game_stat.st_ino);np_put64(record+320,(uint64_t)state_stat.st_dev);np_put64(record+328,(uint64_t)state_stat.st_ino);cp_sha_bytes(record,352,record+352);
    if(ns_record_valid(record,binding))goto done;
    if(upgrade.active&&ns_upgrade_admit(&operation,&upgrade))goto done;
    if(request->publication_current&&request->publication_current(request->opaque))goto done;
    outcome=ns_publish(&operation,&plan,record,&published);
    if(!outcome&&(np_generation(&copy)||ns_current(&operation)))outcome=BW_SEND_UNCONFIRMED;
    if(!outcome)ns_result(record,binding,1,result);
done:
    cp_io_guard=NULL;cp_io_opaque=NULL;np_active=NULL;free(identity_json.bytes);free(plan.index);
    int closed=0;if(root>=0&&close(root))closed=1;if(games>=0&&close(games))closed=1;if(operation.source_file>=0&&close(operation.source_file))closed=1;
    if(operation.state>=0&&close(operation.state))closed=1;operation.state=-1;operation.source_file=-1;if(scope>=0&&close(scope))closed=1;
    if(closed&&upgrade.active)outcome=BW_SEND_UNCONFIRMED;
    if(!outcome&&upgrade.active&&ns_upgrade_finish(&operation,&upgrade,result))outcome=BW_SEND_UNCONFIRMED;
    if(ns_upgrade_close(&upgrade)){closed=1;if(upgrade.active)outcome=BW_SEND_UNCONFIRMED;}
    if(lock>=0&&close(lock))closed=1;if(operation.base>=0&&close(operation.base))closed=1;
    if(closed&&upgrade.active){upgrade.state_code=BW_RETIRE_RECOVERY_REQUIRED;upgrade.error="Closing the startup upgrade transaction was unconfirmed; preserve and stop";}
    if(closed&&published)outcome=BW_SEND_UNCONFIRMED;
    if(observed){source->observe=operation.observe;source->observe_opaque=operation.observe_opaque;}
    if(outcome){memset(result,0,sizeof *result);result->state=BW_PREPARED_UNKNOWN;bw_source_smb_progress(source,BW_SOURCE_ERROR,0,0,0,0);}
    else bw_source_smb_progress(source,BW_SOURCE_PREPARED,bundle->map.local_count+bundle->map.stream_count,bundle->map.local_count+bundle->map.stream_count,bundle->map.local_bytes,bundle->map.local_bytes);
    result->retirement=upgrade.state_code;result->retirement_error=upgrade.error;
    cp_io_guard=NULL;cp_io_opaque=NULL;cp_idle_guard=NULL;cp_idle_opaque=NULL;ns_state_path=NULL;ns_log_path=NULL;memset(ns_games_path,0,sizeof ns_games_path);pthread_mutex_unlock(&ns_mutex);return outcome;
}
static int ns_inspect_locked(const char *base,const struct bw_provider_binding *identity,const uint8_t *expected,
    struct bw_source_provider *source,int (*current)(void *),void *opaque,struct bw_native_prepared *result){
    if(!result)return -1;memset(result,0,sizeof *result);if(!base||!identity)return -1;
    const struct bw_source_binding *binding=&identity->binding;
    char state_path[2048],game_path[2048],scope_path[2048];struct prepare_v2 anchor={0},state_anchor={0},game_anchor={0},scope_anchor={0};int data=-1,state=-1,root=-1,scope=-1;void *record=NULL,*map=NULL,*digests=NULL,*index=NULL;size_t size=0;
    struct bw_json identity_json={.maximum=4096};struct cp_plan plan={0};plan.deadline=cp_now()+300;result->state=BW_PREPARED_INVALID;
    cp_io_guard=current;cp_io_opaque=opaque;
    if(current&&current(opaque)){result->state=BW_PREPARED_UNKNOWN;goto done;}
    if(ns_paths(base,binding,state_path)||ns_provider_json(identity,&identity_json))goto done;
    data=ns_root(base,&anchor);if(data<0)goto done;
    int scope_size=snprintf(scope_path,sizeof scope_path,"%s/native-prepared",base);if(scope_size<0||(size_t)scope_size>=sizeof scope_path)goto done;
    scope=ns_root(scope_path,&scope_anchor);if(scope<0){struct stat st;if(lstat(scope_path,&st)<0&&errno==ENOENT)result->state=ns_absence_state(base,identity,&identity_json,data,&anchor,result);goto done;}
    if(ns_exact(scope,".blackwire-native-prepared",ns_scope,sizeof ns_scope-1))goto done;
    state=ns_root(state_path,&state_anchor);if(state<0){struct stat st;if(lstat(state_path,&st)<0&&errno==ENOENT)result->state=ns_absence_state(base,identity,&identity_json,data,&anchor,result);goto done;}
    if(ns_exact(state,"source.json",identity_json.bytes,identity_json.used))goto done;
    int loaded=ns_read(state,"prepared.bin",NS_RECORD_SIZE,&record,&size,NULL);if(loaded==1){result->state=BW_PREPARED_UNKNOWN;goto done;}
    if(loaded||size!=NS_RECORD_SIZE||ns_record_valid(record,binding))goto done;
    const uint8_t *r=record;struct stat state_stat,game_stat;
    if(expected){uint8_t actual[32];cp_sha_bytes(record,NS_RECORD_SIZE,actual);if(memcmp(expected,actual,32))goto done;}
    if(source){
        uint8_t map_hash[32],digest_hash[32];
        if(!source->bundle.ready||bw_source_provider_current(source)||!source->bundle.map.bytes||!source->bundle.digests||
            strcmp(source->bundle.source,binding->source)||strcmp(source->bundle.game,binding->game)||strcmp(source->bundle.title,binding->title)||
            source->bundle.map.size!=cp_u64(r+280)||source->bundle.digest_size!=cp_u64(r+288)||memcmp(source->bundle.generation_sha256,r+80,32))goto done;
        cp_sha_bytes(source->bundle.map.bytes,source->bundle.map.size,map_hash);cp_sha_bytes(source->bundle.digests,source->bundle.digest_size,digest_hash);
        if(memcmp(map_hash,r+112,32)||memcmp(digest_hash,r+144,32))goto done;
    }
    int n=snprintf(game_path,sizeof game_path,"%s/%s",ns_games_path,binding->game);if(n<0||(size_t)n>=sizeof game_path)goto done;
    if(fstat(state,&state_stat)||(uint64_t)state_stat.st_dev!=cp_u64(r+320)||(uint64_t)state_stat.st_ino!=cp_u64(r+328))goto done;
    root=ns_root(game_path,&game_anchor);
    if(root<0){
        /* A user can remove the thin game folder without its separate receipt.
         * Read-only discovery may offer a fresh Send when that exact root is
         * absent. Keep the old receipt for ns_publish's verified backup; never
         * grant Play or reinterpret a present/linked root as a missing copy. */
        struct stat named;
        if(!expected&&!source&&!current&&lstat(game_path,&named)<0&&errno==ENOENT){
            result->state=ns_absence_state(base,identity,&identity_json,data,&anchor,result);
            if(ns_exact(state,"source.json",identity_json.bytes,identity_json.used)||ns_exact(state,"prepared.bin",record,NS_RECORD_SIZE)||
                !v2_root_current(&anchor,data)||!v2_root_current(&scope_anchor,scope)||!v2_root_current(&state_anchor,state))result->state=BW_PREPARED_UNKNOWN;
        }
        goto done;
    }
    if(fstat(root,&game_stat)||(uint64_t)game_stat.st_dev!=cp_u64(r+304)||
        (uint64_t)game_stat.st_ino!=cp_u64(r+312)||ns_exact(root,"blackwire.generation.owner",identity_json.bytes,identity_json.used))goto done;
    plan.cfg=game_anchor;memcpy(plan.cfg.title,binding->title,10);memcpy(plan.cfg.game,binding->game,75);memcpy(plan.cfg.run_id,r+16,32);plan.cfg.run_id[32]=0;strcpy(plan.phase,"index");
    plan.map_size=(size_t)cp_u64(r+280);plan.digest_size=(size_t)cp_u64(r+288);plan.index_size=(size_t)cp_u64(r+296);memcpy(plan.index_sha,r+176,32);
    const char *leaves[]={"blackwire.files","blackwire.generation.digests","ampr_emu.index"};void **bodies[]={&map,&digests,&index};const size_t sizes[]={plan.map_size,plan.digest_size,plan.index_size};
    for(unsigned i=0;i<3;i++){uint8_t hash[32];size_t got=0;if(ns_read(root,leaves[i],sizes[i],bodies[i],&got,NULL)||got!=sizes[i])goto done;cp_sha_bytes(*bodies[i],got,hash);if(memcmp(hash,r+112+32*i,32))goto done;}
    plan.map=map;plan.digests=digests;plan.index=index;if(bw_manifest_open(&plan.manifest,map,plan.map_size))goto done;
    plan.cfg.manifest_count=cp_u32(r+268);plan.cfg.manifest_hash=plan.manifest.checksum;
    uint64_t local=0,remote=0;
    for(uint32_t i=0;i<plan.manifest.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&plan.manifest,i,&row))goto done;if(!strcmp(row.path,"eboot.bin"))plan.cfg.eboot_size=row.size;
        uint64_t *total=row.flags==BW_MANIFEST_LOCAL?&local:&remote;if(row.size>UINT64_C(9007199254740991)-*total)goto done;*total+=row.size;}
    if(cp_validate_map(&plan)||plan.local_count!=cp_u32(r+272)||local!=cp_u64(r+240)||remote!=cp_u64(r+248)||cp_index_rows(&plan,root)||ns_streams_empty(&plan,root,0))goto done;
    if(current){
        for(uint32_t i=0;i<plan.manifest.count;i++){
            struct bw_manifest_record row;if(bw_manifest_get(&plan.manifest,i,&row))goto done;
            if(row.flags!=BW_MANIFEST_LOCAL)continue;
            double at=cp_now();if(at<0||at>=plan.deadline||current(opaque)){result->state=BW_PREPARED_UNKNOWN;goto done;}
            char leaf[256],relative[1024];int parent=v2_parent(&plan.cfg,root,row.path,0,leaf,relative);if(parent<0)goto done;
            int bad=cp_existing(&plan.cfg,root,parent,relative,leaf,row.size,plan.digests+56u+32u*i);if(close(parent))bad=-1;if(bad)goto done;
        }
        if(current(opaque)){result->state=BW_PREPARED_UNKNOWN;goto done;}
        if(cp_index_rows(&plan,root)||ns_streams_empty(&plan,root,0))goto done;
    }
    uint8_t marker[BW_GENERATION_SIZE],hash[32];char leaf[64];
    if(bw_generation_name(leaf,plan.cfg.run_id)||bw_generation_bytes(marker,plan.cfg.run_id,binding->title,binding->game,r+112)||ns_exact(root,leaf,marker,sizeof marker))goto done;
    cp_sha_bytes(marker,sizeof marker,hash);if(memcmp(hash,r+208,32)||ns_exact(state,"prepared.bin",record,NS_RECORD_SIZE)||
        !v2_root_current(&anchor,data)||!v2_root_current(&scope_anchor,scope)||!v2_root_current(&state_anchor,state)||!v2_root_current(&game_anchor,root))goto done;
    ns_result(record,binding,current!=NULL,result);
done:
    cp_io_guard=NULL;cp_io_opaque=NULL;
    free(identity_json.bytes);free(record);free(map);free(digests);free(index);if(root>=0&&close(root))result->state=BW_PREPARED_UNKNOWN;if(state>=0&&close(state))result->state=BW_PREPARED_UNKNOWN;if(scope>=0&&close(scope))result->state=BW_PREPARED_UNKNOWN;if(data>=0&&close(data))result->state=BW_PREPARED_UNKNOWN;
    memset(ns_games_path,0,sizeof ns_games_path);return 0;
}
static int ns_inspect(const char *base,const struct bw_provider_binding *identity,const uint8_t *expected,
    struct bw_source_provider *source,int (*current)(void *),void *opaque,struct bw_native_prepared *result){
    if(!result)return -1;memset(result,0,sizeof *result);if(!base||!identity)return -1;
    if(pthread_mutex_trylock(&ns_mutex)){result->state=BW_PREPARED_UNKNOWN;return 0;}
    int rc=ns_inspect_locked(base,identity,expected,source,current,opaque,result);pthread_mutex_unlock(&ns_mutex);return rc;
}
int bw_native_send_inspect(const char *base,const struct bw_provider_binding *identity,struct bw_native_prepared *result){
    return ns_inspect(base,identity,NULL,NULL,NULL,NULL,result);
}
int bw_native_send_verify(const char *base,const struct bw_provider_binding *identity,int (*current)(void *),void *opaque,struct bw_native_prepared *result){
    if(!current){if(result){memset(result,0,sizeof *result);result->state=BW_PREPARED_UNKNOWN;}return -1;}
    return ns_inspect(base,identity,NULL,NULL,current,opaque,result);
}
int bw_native_send_preflight(const char *base,const struct bw_provider_binding *identity,const uint8_t expected[32],
    struct bw_source_provider *source,int (*current)(void *),void *opaque,struct bw_native_prepared *result){
    if(!expected||!source||!current||!identity||identity->policy!=BW_PROVIDER_POLICY_V1||
       (identity->kind!=BW_PROVIDER_FOLDER&&identity->kind!=BW_PROVIDER_CONTAINER)||
       source->identity.kind!=identity->kind||source->identity.policy!=identity->policy||
       memcmp(source->identity.binding.sha256,identity->binding.sha256,32)||
       !memchr(identity->binding.source,0,sizeof identity->binding.source)||!memchr(source->identity.binding.source,0,sizeof source->identity.binding.source)||
       !memchr(identity->binding.game,0,sizeof identity->binding.game)||!memchr(source->identity.binding.game,0,sizeof source->identity.binding.game)||
       !memchr(identity->binding.title,0,sizeof identity->binding.title)||!memchr(source->identity.binding.title,0,sizeof source->identity.binding.title)||
       strcmp(source->identity.binding.source,identity->binding.source)||strcmp(source->identity.binding.game,identity->binding.game)||
       strcmp(source->identity.binding.title,identity->binding.title)){if(result){memset(result,0,sizeof *result);result->state=BW_PREPARED_UNKNOWN;}return -1;}
    return ns_inspect(base,identity,expected,source,current,opaque,result);
}
#include "native_launch_admission.c"
