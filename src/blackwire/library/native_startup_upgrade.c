/* Included by native_send.c only, under its mutex and interprocess file lock.
 * This is owned namespace retirement, NOT a new preparation/receipt format.
 * No delete, restoration, mount, registration or source write is provided. */
#include "source_plan.h"
#include <dirent.h>

struct ns_upgrade {
    int scope,journal,games,states,game,state;
    struct stat scope_id,journal_id,game_id,state_id;
    struct prepare_v2 scope_anchor,games_anchor,states_anchor;
    uint8_t intent_sha[32];
    char game_path[2048],journal_leaf[112];
    unsigned active,game_moved,state_moved;
    enum bw_native_retirement_state state_code;
    const char *error;
};
#define NS_UPGRADE_INIT {.scope=-1,.journal=-1,.games=-1,.states=-1,.game=-1,.state=-1}
static const char us_scope_name[]=".native-startup-rollback";
static const char us_marker_name[]=".blackwire-startup-rollback";
static const char us_marker[]="Blackwire owned startup rollback v1\n";
static int us_fail(struct ns_upgrade *u,const char *error){
    if(u->state_code!=BW_RETIRE_RECOVERY_REQUIRED)u->state_code=u->active?BW_RETIRE_RECOVERY_REQUIRED:BW_RETIRE_REFUSED;u->error=error;return v2_fail(error);
}
static int us_identity(const struct stat *a,const struct stat *b){
    return S_ISDIR(b->st_mode)&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_uid==b->st_uid&&a->st_mode==b->st_mode;
}
static int us_private(int fd,struct stat *out){
    struct stat s;if(fstat(fd,&s)||!S_ISDIR(s.st_mode)||(s.st_mode&07777)!=0700||s.st_uid!=geteuid())return -1;if(out)*out=s;return 0;
}
static int us_named(int parent,const char *leaf,int fd,const struct stat *expected){
    struct stat held,named;return fstat(fd,&held)||cp_fstatat(parent,leaf,&named,AT_SYMLINK_NOFOLLOW)||!us_identity(expected,&held)||!us_identity(&held,&named)?-1:0;
}
static int us_absent(int parent,const char *leaf){struct stat s;return cp_fstatat(parent,leaf,&s,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT?0:-1;}
static int us_file(int directory,const char *leaf,const void *bytes,size_t size){
    int fd=v2_openat(directory,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK|O_RDWR,0600),bad=1;struct stat created,after;
    if(fd<0)return -1;
    if(fstat(fd,&created)||!S_ISREG(created.st_mode)||created.st_uid!=geteuid()||created.st_nlink!=1||created.st_size||
       (created.st_mode&07777)!=0600||cp_regular(directory,leaf,fd,0,NULL))goto done;
    if(cp_write_all(fd,bytes,size)||fsync(fd)||cp_regular(directory,leaf,fd,size,&after)||!v2_identity(&created,&after)||
       after.st_uid!=created.st_uid||(after.st_mode&07777)!=0600||ns_exact(directory,leaf,bytes,size)||fsync(directory))goto done;
    bad=0;
done:if(close(fd))bad=1;return bad?-1:0; /* A partial exclusive journal is retained. */
}
static int us_read_private(int directory,const char *leaf,size_t maximum,void **body,size_t *size){
    struct stat s;if(ns_read(directory,leaf,maximum,body,size,&s)||s.st_uid!=geteuid()||(s.st_mode&07777)!=0600){free(*body);*body=NULL;return -1;}return 0;
}
static int us_complete(int directory){
    void *intent=NULL,*complete=NULL;size_t in=0,cn=0;uint8_t h[32];int bad=1;
    if(us_read_private(directory,"intent.bin",8192,&intent,&in)||in<33||us_read_private(directory,"complete.bin",72,&complete,&cn)||cn!=72)goto done;
    cp_sha_bytes(intent,in-32,h);if(memcmp(h,(uint8_t *)intent+in-32,32)||memcmp(complete,"BWUPGOK1",8))goto done;
    cp_sha_bytes(intent,in,h);if(memcmp(h,(uint8_t *)complete+8,32))goto done;bad=0;
done:free(intent);free(complete);return bad?-1:0;
}
static int us_open_scope(struct ns_operation *o,struct ns_upgrade *u,int create){
    int created=0;u->scope=v2_openat(o->base,us_scope_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
    if(u->scope<0&&errno==ENOENT&&create){
        if(v2_mkdirat(o->base,us_scope_name,0700))return -1;created=1;
        u->scope=v2_openat(o->base,us_scope_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
    }
    if(u->scope<0)return !create&&errno==ENOENT?1:-1;
    struct stat base;if(fstat(o->base,&base)||us_private(u->scope,&u->scope_id)||u->scope_id.st_dev!=base.st_dev)return -1;
    int n=snprintf(u->scope_anchor.root,sizeof u->scope_anchor.root,"%s/%s",o->request->data_root,us_scope_name);
    if(n<0||(size_t)n>=sizeof u->scope_anchor.root||!v2_root_current(&u->scope_anchor,u->scope))return -1;
    if(created){if(us_file(u->scope,us_marker_name,us_marker,sizeof us_marker-1)||fsync(o->base))return -1;}
    else if(ns_exact(u->scope,us_marker_name,us_marker,sizeof us_marker-1))return -1;
    return 0;
}
static int us_prior(struct ns_operation *o,struct ns_upgrade *u){
    const char *game=o->request->provider->identity.binding.game;int scope=us_open_scope(o,u,0);if(scope)return scope>0?0:-1;
    int duplicate=dup(u->scope);if(duplicate<0)return -1;DIR *d=fdopendir(duplicate);if(!d){close(duplicate);return -1;}int bad=0;unsigned count=0;
    for(;;){
        errno=0;struct dirent *e=readdir(d);if(!e){if(errno)bad=1;break;}
        if(++count>4096){bad=1;break;}
        if(strncmp(e->d_name,game,74)||e->d_name[74]!='.')continue;
        if(strlen(e->d_name)!=107||!ns_nonce(e->d_name+75)){bad=1;break;}
        int child=v2_openat(u->scope,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);struct stat id;
        if(child<0||us_private(child,&id)||id.st_dev!=u->scope_id.st_dev||us_complete(child))bad=1;
        if(child>=0&&close(child))bad=1;if(bad)break;
    }
    if(closedir(d))bad=1;
    if(bad){u->state_code=BW_RETIRE_RECOVERY_REQUIRED;u->error="An earlier owned startup retirement is incomplete or unconfirmed; no move was repeated";return -1;}
    return 0;
}
static int us_pure_current(void *opaque){
    struct ns_operation *o=opaque;double at=cp_now();
    return at<0||at>=o->request->provider->source->deadline||ns_current(o)?-1:0;
}
static int ns_upgrade_admit(struct ns_operation *o,struct ns_upgrade *u){
    const struct bw_native_send_files *q=o->request;
    if(us_pure_current(o)||q->provider->busy||bw_source_provider_current(q->provider)||us_pure_current(o)||
       !q->upgrade_unmapped||q->upgrade_unmapped(q->upgrade_opaque,u->game_path,q->provider->identity.binding.title)||us_pure_current(o))
        return us_fail(u,"The exact owned startup root is not proven unmapped and current; retirement stopped");
    return 0;
}
static int us_objects(struct ns_operation *o,struct ns_upgrade *u){
    const char *game=o->request->provider->identity.binding.game;
    if(!v2_root_current(&u->scope_anchor,u->scope)||!v2_root_current(&u->games_anchor,u->games)||!v2_root_current(&u->states_anchor,u->states)||
       us_private(u->journal,NULL)||us_named(u->scope,u->journal_leaf,u->journal,&u->journal_id)||
       (u->game_moved?us_absent(u->games,game)||us_named(u->journal,"game",u->game,&u->game_id):us_named(u->games,game,u->game,&u->game_id)||us_absent(u->journal,"game"))||
       (u->state_moved?us_absent(u->states,game)||us_named(u->journal,"state",u->state,&u->state_id):us_named(u->states,game,u->state,&u->state_id)||us_absent(u->journal,"state")))return -1;
    return 0;
}
static int us_move(struct ns_operation *o,struct ns_upgrade *u,int state){
    const char *game=o->request->provider->identity.binding.game,*leaf=state?"state":"game";
    int parent=state?u->states:u->games,fd=state?u->state:u->game;const struct stat *id=state?&u->state_id:&u->game_id;
    uint8_t marker[60]={0};memcpy(marker,"BWUPGMV1",8);np_put32(marker+8,state?2:1);np_put64(marker+12,(uint64_t)id->st_dev);np_put64(marker+20,(uint64_t)id->st_ino);memcpy(marker+28,u->intent_sha,32);
    char intent[32],done[32];snprintf(intent,sizeof intent,"%s.intent",leaf);snprintf(done,sizeof done,"%s.observed",leaf);
    if(ns_upgrade_admit(o,u)||us_objects(o,u)||us_file(u->journal,intent,marker,sizeof marker)||us_pure_current(o)||us_objects(o,u))return us_fail(u,"The one-shot startup move intent is unconfirmed; old copies were retained");
    int result=cp_renameat(parent,game,u->journal,leaf);
    unsigned moved=!us_absent(parent,game)&&!us_named(u->journal,leaf,fd,id);
    if(state)u->state_moved=moved;else u->game_moved=moved;
    if(result||!moved||fsync(parent)||fsync(u->journal)||us_objects(o,u)||us_file(u->journal,done,marker,sizeof marker))return us_fail(u,"An owned startup rename or its readback is unconfirmed; do not replay or restore onto a partial copy");
    return 0;
}
static int ns_upgrade_begin(struct ns_operation *o,const struct cp_plan *next,struct ns_upgrade *u){
    const struct bw_source_provider *provider=o->request->provider;const struct bw_provider_binding *identity=&provider->identity;
    struct bw_native_prepared old={0},verified={0};void *record=NULL,*map=NULL,*digests=NULL;size_t rn=0,mn=0,dn=0;int bad=-1;
    if(us_prior(o,u))return us_fail(u,u->error?u->error:"The owned startup rollback scope cannot be validated");
    if(ns_inspect_locked(o->request->data_root,identity,NULL,NULL,NULL,NULL,&old))return -1;
    char state_path[2048],games_path[2048],states_path[2048];
    if(ns_paths(o->request->data_root,&identity->binding,state_path))goto done;
    cp_io_guard=ns_current;cp_io_opaque=o;
    if(old.state!=BW_PREPARED_VALID){bad=0;goto done;}
    int n=snprintf(u->game_path,sizeof u->game_path,"%s/games/%s",o->request->data_root,identity->binding.game);
    int a=snprintf(games_path,sizeof games_path,"%s/games",o->request->data_root),b=snprintf(states_path,sizeof states_path,"%s/native-prepared",o->request->data_root);
    if(n<0||(size_t)n>=sizeof u->game_path||a<0||(size_t)a>=sizeof games_path||b<0||(size_t)b>=sizeof states_path)goto done;
    struct prepare_v2 anchor;u->games=ns_root(games_path,&u->games_anchor);u->states=ns_root(states_path,&u->states_anchor);u->game=ns_root(u->game_path,&anchor);u->state=ns_root(state_path,&anchor);
    if(u->games<0||u->states<0||u->game<0||u->state<0||fstat(u->game,&u->game_id)||fstat(u->state,&u->state_id)||
       u->game_id.st_uid!=geteuid()||u->state_id.st_uid!=geteuid()||
       ns_read(u->state,"prepared.bin",NS_RECORD_SIZE,&record,&rn,NULL)||rn!=NS_RECORD_SIZE||ns_record_valid(record,&identity->binding))goto done;
    uint8_t hash[32];cp_sha_bytes(record,rn,hash);if(memcmp(hash,old.record_sha256,32))goto done;
    const uint8_t *r=record;
    if(ns_read(u->game,"blackwire.files",CP_MAX_MANIFEST,&map,&mn,NULL)||ns_read(u->game,"blackwire.generation.digests",56u+32u*BW_MANIFEST_MAX_FILES,&digests,&dn,NULL))goto done;
    cp_sha_bytes(map,mn,hash);if(memcmp(hash,r+112,32))goto done;cp_sha_bytes(digests,dn,hash);if(memcmp(hash,r+144,32))goto done;
    if(mn==provider->bundle.map.size&&dn==provider->bundle.digest_size&&!memcmp(map,provider->bundle.map.bytes,mn)&&!memcmp(digests,provider->bundle.digests,dn)){bad=0;goto done;}
    struct bw_manifest prior;unsigned promotions=0;
    if(identity->kind!=BW_PROVIDER_FOLDER||identity->policy!=1||memcmp(provider->bundle.generation_sha256,r+80,32)||
       bw_manifest_open(&prior,map,mn)||prior.count!=next->manifest.count||dn!=provider->bundle.digest_size){bad=0;goto done;}
    for(uint32_t id=0;id<prior.count;id++){
        struct bw_manifest_record before,after;if(us_pure_current(o)||bw_manifest_get(&prior,id,&before)||bw_manifest_get(&next->manifest,id,&after))goto done;
        if(strcmp(before.path,after.path)||before.size!=after.size||before.mtime_sec!=after.mtime_sec){bad=0;goto done;}
        if(before.flags==after.flags){if(before.flags==BW_MANIFEST_LOCAL&&memcmp((uint8_t *)digests+56u+32u*id,provider->bundle.digests+56u+32u*id,32)){bad=0;goto done;}}
        else if(before.flags==BW_MANIFEST_STREAM&&after.flags==BW_MANIFEST_LOCAL&&bw_source_startup_metadata(after.path,after.size))promotions++;
        else{bad=0;goto done;}
    }
    if(!promotions){bad=0;goto done;}
    if(ns_upgrade_admit(o,u))goto done;
    if(ns_inspect_locked(o->request->data_root,identity,old.record_sha256,NULL,us_pure_current,o,&verified)||verified.state!=BW_PREPARED_VALID||!verified.bytes_verified)goto done;
    if(ns_paths(o->request->data_root,&identity->binding,state_path))goto done;cp_io_guard=ns_current;cp_io_opaque=o;
    if(ns_upgrade_admit(o,u)||us_named(u->games,identity->binding.game,u->game,&u->game_id)||us_named(u->states,identity->binding.game,u->state,&u->state_id)||ns_exact(u->state,"prepared.bin",record,rn))goto done;
    if(u->scope<0&&us_open_scope(o,u,1))goto done;
    if(u->game_id.st_dev!=u->scope_id.st_dev||u->state_id.st_dev!=u->scope_id.st_dev){us_fail(u,"Owned startup retirement requires the same filesystem; no copy fallback is allowed");goto done;}
    n=snprintf(u->journal_leaf,sizeof u->journal_leaf,"%s.%s",identity->binding.game,next->cfg.run_id);
    if(n<0||(size_t)n>=sizeof u->journal_leaf||v2_mkdirat(u->scope,u->journal_leaf,0700))goto done;
    u->active=1;u->journal=v2_openat(u->scope,u->journal_leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
    if(u->journal<0||us_private(u->journal,&u->journal_id)||u->journal_id.st_dev!=u->scope_id.st_dev||fsync(u->scope))goto done;
    struct bw_json j={.maximum=8160};bw_json_text(&j,"{\"version\":1,\"operation\":\"owned-startup-metadata-retirement\",\"original\":");
    if(ns_provider_json(identity,&j)){free(j.bytes);goto done;}if(j.used&&j.bytes[j.used-1]=='\n')j.used--;
    bw_json_text(&j,",\"acceptedRun\":");bw_json_string(&j,next->cfg.run_id);char hex[65];
    const uint8_t *values[]={old.record_sha256,r+80,r+112,provider->bundle.digests};const char *keys[]={",\"oldReceiptSha256\":",",\"sourceSeal\":",",\"oldMapSha256\":",",\"newDigestsSha256\":"};
    uint8_t new_dig[32],new_map[32];cp_sha_bytes(provider->bundle.digests,provider->bundle.digest_size,new_dig);cp_sha_bytes(provider->bundle.map.bytes,provider->bundle.map.size,new_map);values[3]=new_dig;
    for(unsigned i=0;i<4;i++){for(unsigned k=0;k<32;k++)snprintf(hex+2*k,3,"%02x",values[i][k]);bw_json_text(&j,keys[i]);bw_json_string(&j,hex);}
    for(unsigned k=0;k<32;k++)snprintf(hex+2*k,3,"%02x",new_map[k]);bw_json_text(&j,",\"newMapSha256\":");bw_json_string(&j,hex);
    bw_json_format(&j,",\"oldGameDev\":%llu,\"oldGameIno\":%llu,\"oldStateDev\":%llu,\"oldStateIno\":%llu}\n",(unsigned long long)u->game_id.st_dev,(unsigned long long)u->game_id.st_ino,(unsigned long long)u->state_id.st_dev,(unsigned long long)u->state_id.st_ino);
    if(j.bad){free(j.bytes);goto done;}uint8_t seal[32];cp_sha_bytes(j.bytes,j.used,seal);bw_json_append(&j,(const char *)seal,32);
    if(j.bad||us_file(u->journal,"intent.bin",j.bytes,j.used)){free(j.bytes);goto done;}cp_sha_bytes(j.bytes,j.used,u->intent_sha);free(j.bytes);
    if(us_move(o,u,0)||us_move(o,u,1)||ns_upgrade_admit(o,u))goto done;bad=0;
done:
    free(record);free(map);free(digests);
    if(bad)return us_fail(u,u->error?u->error:"Owned startup qualification or retirement is unconfirmed; existing copies were retained");
    /* Original handles are closed before unchanged Send creates new roots. */
    int *fds[]={&u->game,&u->state,&u->games,&u->states};
    for(unsigned i=0;i<4;i++)if(*fds[i]>=0){int fd=*fds[i];*fds[i]=-1;if(close(fd))return us_fail(u,"Closing an owned startup retirement handle failed; preserve and stop");}
    return 0;
}
static int ns_upgrade_finish(struct ns_operation *o,struct ns_upgrade *u,const struct bw_native_prepared *prepared){
    struct bw_native_prepared checked={0};const struct bw_native_send_files *q=o->request;
    if(!u->active)return 0;
    if(!prepared->bytes_verified||!v2_root_current(&u->scope_anchor,u->scope)||us_named(u->scope,u->journal_leaf,u->journal,&u->journal_id)||
       ns_upgrade_admit(o,u)||ns_inspect_locked(q->data_root,&q->provider->identity,prepared->record_sha256,q->provider,us_pure_current,o,&checked)||
       checked.state!=BW_PREPARED_VALID||!checked.bytes_verified||ns_upgrade_admit(o,u))return us_fail(u,"The recreated startup generation is not fully confirmed; rollback copies were retained");
    cp_io_guard=ns_current;cp_io_opaque=o;uint8_t complete[72];memcpy(complete,"BWUPGOK1",8);memcpy(complete+8,u->intent_sha,32);memcpy(complete+40,prepared->record_sha256,32);
    if(us_file(u->journal,"complete.bin",complete,sizeof complete)||us_named(u->scope,u->journal_leaf,u->journal,&u->journal_id))return us_fail(u,"The startup retirement completion journal is unconfirmed; do not replay it");
    u->state_code=BW_RETIRE_COMPLETE;u->error=NULL;return 0;
}
static int ns_upgrade_close(struct ns_upgrade *u){
    if(u->active&&u->state_code!=BW_RETIRE_COMPLETE){u->state_code=BW_RETIRE_RECOVERY_REQUIRED;if(!u->error)u->error="Owned startup retirement requires recovery; no move or copy was replayed";}
    int bad=0;int *fds[]={&u->game,&u->state,&u->games,&u->states,&u->journal,&u->scope};
    for(unsigned i=0;i<6;i++)if(*fds[i]>=0){int fd=*fds[i];*fds[i]=-1;if(close(fd))bad=1;}
    if(bad){u->state_code=u->active?BW_RETIRE_RECOVERY_REQUIRED:BW_RETIRE_REFUSED;u->error="Closing startup retirement metadata failed; preserve and stop";}
    return bad?-1:0;
}
