/* Actual projected producer/witness -> preparation-only cleanup. Remote idle
 * and complete manual/scan exclusion are modeled, not a Sony/SMP transport. */
#define BW_PROJECTED_REMOVE_FIXTURE_ONLY 1
#include "test_projected_game_uninstall_e2e.c"
#include "native_registration_cleanup.h"
/* The optional real NRC-owner creation below is folder-only. Offline image
 * producer/removal cases never ask execution admission to connect an image. */
int bw_container_smb_open_owned_metadata(struct bw_container_smb *p,struct smb2_context **owner,const char *name){(void)p;(void)owner;(void)name;abort();}
int bw_container_smb_close(struct bw_container_smb *p){(void)p;abort();}
struct cleanup_fixture {struct smb2_context *context;const char *mode;char game[1800],moved[1850];unsigned exclusions,renamed;};
static int cleanup_current(void *opaque){struct cleanup_fixture *p=opaque;return current(p->context);}
static int cleanup_excluded(void *opaque,const struct bw_removed_source *removed){
    struct cleanup_fixture *p=opaque;C(removed&&!strcmp(removed->identity.binding.title,"PPSA99001"));p->exclusions++;
    if(!strcmp(p->mode,"manual")||!strcmp(p->mode,"scan"))return -1;
    if(!strcmp(p->mode,"consumed-before-unlink")&&p->exclusions==2)return -1;
    if(!strcmp(p->mode,"root-rename")&&!p->renamed){C(!rename(p->game,p->moved));p->renamed=1;}
    return 0;
}
static void sql_execute(const char *path,const char *sql){sqlite3 *db=NULL;C(sqlite3_open(path,&db)==SQLITE_OK&&sqlite3_exec(db,sql,NULL,NULL,NULL)==SQLITE_OK&&sqlite3_close(db)==SQLITE_OK);}
static void dir_hash_at(const char *path,const char *relative,struct cp_sha *sha){
    char full[2200];C(snprintf(full,sizeof full,"%s%s%s",path,*relative?"/":"",relative)>0);struct stat st;C(!lstat(full,&st));
    cp_sha_update(sha,relative,strlen(relative)+1);mode_t type=st.st_mode&S_IFMT;cp_sha_update(sha,&type,sizeof type);
    if(S_ISDIR(st.st_mode)){struct dirent **entries=NULL;int n=scandir(full,&entries,NULL,alphasort);C(n>=0);
        for(int i=0;i<n;i++){const char *name=entries[i]->d_name;if(strcmp(name,".")&&strcmp(name,"..")){char child[1700];C(snprintf(child,sizeof child,"%s%s%s",relative,*relative?"/":"",name)>0);dir_hash_at(path,child,sha);}free(entries[i]);}free(entries);
    }else{C(S_ISREG(st.st_mode));uint8_t h[32];fingerprint(full,h);cp_sha_update(sha,h,32);}
}
static void tree_pin(const char *path,uint8_t hash[32]){struct cp_sha sha;cp_sha_init(&sha);dir_hash_at(path,"",&sha);cp_sha_final(&sha,hash);}
static int same_stat(const struct stat *a,const struct stat *b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static int owner_sources(void *opaque,const struct bw_nrc_subject *s){struct cleanup_fixture *p=opaque;C(s&&!strcmp(s->title,"PPSA99001")&&!strcmp(s->source,p->game));return cleanup_current(opaque);}
static int owner_uninstall_forbidden(void *opaque,const char title[10]){(void)opaque;(void)title;C(0);return -1;}
static void make_app(const char *apps,const char *content,char app[1800],char param[2000]){
    C(snprintf(app,1800,"%s/PPSA99001",apps)>0&&!mkdir(app,0755));char sys[2000];C(snprintf(sys,sizeof sys,"%s/sce_sys",app)>0&&!mkdir(sys,0755));
    C(snprintf(param,2000,"%s/param.json",sys)>0);char json[512];int n=snprintf(json,sizeof json,"{\"titleId\":\"PPSA99001\",\"contentId\":\"%s\"}",content);C(n>0&&(size_t)n<sizeof json);file_write(param,json,(size_t)n);
    C(snprintf(sys,sizeof sys,"%s/sce_sys/icon0.png",app)>0);file_write(sys,"FOREIGN UNCHANGED ICON",22);
}
int main(int argc,char **argv){C(argc==6);int folder=!strcmp(argv[1],"folder");const char *mode=argv[5];
    struct smb2_context context={0};C(strlen(argv[2])<sizeof context.root);strcpy(context.root,argv[2]);for(unsigned i=0;i<64;i++)context.handles[i].fd=-1;
    struct bw_source_smb source={.smb=&context,.deadline=now()+600};struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");
    struct bw_provider_binding binding;C(!bw_provider_binding_init(&binding,&config,folder?"fixture":"fixture.ffpfsc","PPSA99001",folder?BW_PROVIDER_FOLDER:BW_PROVIDER_CONTAINER));
    struct bw_container_smb *image=NULL;struct image_source input={.context=&context};if(!folder){load_image(&input,argv[3]);image=calloc(1,sizeof *image);C(image);strcpy(image->path,"fixture.ffpfsc");image->handle=(struct smb2fh *)image;image->admitted.smb2_ino=7;image->admitted.smb2_size=input.size;image->admitted.smb2_mtime=1700000000;struct bw_container_ops ops={image_read,image_generation};C(!bw_container_open(&image->container,&ops,&input,input.size));}
    char data[1600],apps[1600],db[1600],save[1600],log[1600],state[1800],path[2200],app[1800]={0},param[2000]={0};
    C(snprintf(data,sizeof data,"%s/data",argv[4])>0&&!mkdir(data,0755));C(snprintf(apps,sizeof apps,"%s/apps",argv[4])>0&&!mkdir(apps,0770));C(snprintf(db,sizeof db,"%s/app.db",argv[4])>0);empty_db(db);
    C(snprintf(save,sizeof save,"%s/saved-game.bin",argv[4])>0);file_write(save,"untouched save",14);uint8_t save_hash[32];fingerprint(save,save_hash);
    C(snprintf(log,sizeof log,"%s/game.log",argv[4])>0);file_write(log,"",0);
    struct bw_source_provider provider={0};C(!bw_source_provider_prepare_policy(&provider,&binding,&source,image,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",32u*1024u*1024u,current,&context,BW_SOURCE_PLAN_COMPANIONS_V1));
    struct bw_native_send_files send={.data_root=data,.game_log=log,.provider=&provider,.current=current,.opaque=&context};struct bw_native_prepared prepared={0};C(!bw_native_send_files(&send,&prepared)&&prepared.state==BW_PREPARED_VALID&&prepared.bytes_verified&&prepared.prepared_policy==3);
    struct cleanup_fixture f={.context=&context,.mode=mode};C(snprintf(f.game,sizeof f.game,"%s/games/%s",data,binding.binding.game)>0);C(snprintf(f.moved,sizeof f.moved,"%s-moved",f.game)>0);C(snprintf(state,sizeof state,"%s/native-prepared/%s",data,binding.binding.game)>0);
    bw_source_provider_destroy(&provider);C(!context.open);unsigned requests=context.requests,fences=input.fences;
    C(snprintf(path,sizeof path,"%s/fakelib2",f.game)>0);C(!mkdir(path,0755)||errno==EEXIST);C(snprintf(path,sizeof path,"%s/fakelib2/libSceAmpr.sprx",f.game)>0);file_write(path,bw_preload_embedded_image.bytes,bw_preload_embedded_image.size);C(!chmod(path,0755));
    struct bw_native_launch_admission *admission=NULL;C(!bw_native_launch_admission_open_for_removal(&admission,data,&binding,&prepared,"cccccccccccccccccccccccccccccccc",current,&context,NULL));const struct bw_preload_companions *companions=NULL;C(!bw_native_launch_admission_companions_for_removal(admission,&companions));
    struct bw_preload_bundle_upgrade_request selection={.bundle={f.game,current,&context,now()+20},.data_root=data,.game=binding.binding.game,.prepared_run=prepared.run,.launch_run="cccccccccccccccccccccccccccccccc",.prepared_record_sha256=prepared.record_sha256};struct bw_preload_reader_pin pin={0};struct bw_preload_bundle_result diagnostic={0};C(!bw_preload_bundle_select_projection_for_removal(&selection,companions,&pin,&diagnostic));C(!bw_native_launch_admission_close(&admission));
    static const char matching[]="UP0000-PPSA99001_00-BLACKWIREFIXTURE0",foreign[]="UP0000-PPSA99001_00-OTHERFIXTURE0000";
    struct bw_nrc_environment env={data,apps,db};struct bw_nrc_ticket *ticket=NULL;struct bw_reader_selection execution={0};
    if(!strcmp(mode,"owned-ledger")){
        C(folder&&!bw_native_launch_admission_open(&admission,data,&binding,&prepared,"dddddddddddddddddddddddddddddddd",current,&context,NULL));C(!bw_native_launch_admission_selection(admission,&execution));uint8_t map[32],generation[32];C(!bw_native_launch_admission_source(admission,&config,&source,&execution,map,generation,NULL));
        struct bw_nrc_ops ops={.opaque=&f,.current=cleanup_current,.sources=owner_sources,.uninstall=owner_uninstall_forbidden};struct bw_nrc_result nr={0};C(!bw_nrc_begin_projected(&env,&execution,admission,&ops,&ticket,&nr)&&ticket);
    }
    if(strcmp(mode,"normal-empty")){
        const char *content=!strcmp(mode,"foreign-content")?foreign:matching;make_app(apps,content,app,param);
        if(strcmp(mode,"db-absent")){char sql[512];C(snprintf(sql,sizeof sql,"INSERT INTO tbl_contentinfo VALUES('PPSA99001','%s')",content)>0);sql_execute(db,sql);}
    }
    if(ticket){C(snprintf(path,sizeof path,"%s/mount.lnk",app)>0);file_write(path,f.game,strlen(f.game));struct bw_register_result registered={.add_attempted=1,.add_confirmed=1,.registered=1};struct bw_nrc_result nr={0};C(!bw_nrc_finish(ticket,0,&registered,&nr)&&nr.state==BW_NRC_OWNED);C(!bw_nrc_ticket_destroy(ticket));C(!bw_native_launch_admission_close(&admission));C(!unlink(path));requests=context.requests;fences=input.fences;}
    if(!strcmp(mode,"malformed-ledger")){C(snprintf(path,sizeof path,"%s/registration-owners",data)>0&&!mkdir(path,0700));char marker[2200];C(snprintf(marker,sizeof marker,"%s/.blackwire-registration-owners",path)>0);static const char scope[]="Blackwire registration ownership ledger v1\n";file_write(marker,scope,sizeof scope-1);C(!chmod(marker,0600));C(snprintf(marker,sizeof marker,"%s/invalid.owner",path)>0);file_write(marker,"not a valid event",17);C(!chmod(marker,0600));}
    if(!strcmp(mode,"receipt-tamper")){C(snprintf(path,sizeof path,"%s/blackwire.companions",f.game)>0);mutate(path);}
    if(!strcmp(mode,"original-tamper")){C(snprintf(path,sizeof path,"%s/source.original.files",state)>0);mutate(path);}
    if(!strcmp(mode,"foreign-reader")){C(snprintf(path,sizeof path,"%s/fakelib2/libSceAmpr.sprx",f.game)>0);mutate(path);}
    if(!strcmp(mode,"active"))context.canceled=1;
    uint8_t app_hash[32]={0},db_hash[32],game_hash[32],state_hash[32];struct stat app_before={0},param_before={0},db_before;fingerprint(db,db_hash);C(!lstat(db,&db_before));tree_pin(f.game,game_hash);tree_pin(state,state_hash);if(*app){tree_pin(app,app_hash);C(!lstat(app,&app_before)&&!lstat(param,&param_before));C(snprintf(path,sizeof path,"%s/mount.lnk",app)>0);struct stat no_mount;C(lstat(path,&no_mount)<0&&errno==ENOENT);}
    struct bw_preparation_remove_request request={.environment=env,.identity=binding,.prepared=prepared,.ops={.opaque=&f,.current=cleanup_current,.source_excluded=cleanup_excluded},.reader_size=pin.size};memcpy(request.reader_sha256,pin.sha256,32);strcpy(request.operation,"cccccccccccccccccccccccccccccccc");strcpy(request.confirmed_title,binding.binding.title);
    struct bw_game_uninstall_result result={0};int normal=!strcmp(mode,"normal-empty")||!strcmp(mode,"normal-foreign"),rc;
    if(normal){struct bw_game_uninstall_request r={.environment=env,.identity=binding,.prepared=prepared,.ops={.opaque=&context,.current=current,.registration_absent=registration_absent,.source_excluded=excluded},.reader_size=pin.size};memcpy(r.reader_sha256,pin.sha256,32);strcpy(r.operation,request.operation);strcpy(r.confirmed_title,request.confirmed_title);rc=bw_native_game_uninstall(&r,&result);}
    else rc=bw_native_preparation_remove(&request,&result);
    int positive=!strcmp(mode,"matching")||!strcmp(mode,"foreign-content")||!strcmp(mode,"db-absent")||!strcmp(mode,"normal-empty");
    if(positive){if(rc)fprintf(stderr,"cleanup rc%d reason%s files%u/%u\n",rc,result.reason?result.reason:"",result.files_removed,result.files_total);C(!rc&&result.phase==BW_GAME_REMOVE_DONE&&result.source_absent&&result.prepared_record_absent&&result.files_removed==result.files_total&&result.files_total>0&&!result.release_unknown);C(result.preparation_only==(unsigned)!normal&&result.registration_confirmed==(unsigned)normal&&result.library_removed==(unsigned)normal);struct stat gone;C(lstat(f.game,&gone)<0&&errno==ENOENT&&lstat(state,&gone)<0&&errno==ENOENT);
        if(!normal){unsigned calls=f.exclusions;struct bw_game_uninstall_result replay={0};C(bw_native_preparation_remove(&request,&replay)<0&&!replay.files_removed&&f.exclusions==calls);}
    }else{C(rc<0&&!result.files_removed&&!result.preparation_only);uint8_t after[32];tree_pin(f.renamed?f.moved:f.game,after);C(!memcmp(game_hash,after,32));tree_pin(state,after);C(!memcmp(state_hash,after,32));
        if(!strcmp(mode,"consumed-before-unlink")){C(result.mutation_started&&result.phase==BW_GAME_REMOVE_UNKNOWN&&f.exclusions==2);uint8_t journal[32];C(snprintf(path,sizeof path,"%s/preparation-removals",data)>0);tree_pin(path,journal);unsigned calls=f.exclusions;struct bw_game_uninstall_result replay={0};C(bw_native_preparation_remove(&request,&replay)<0&&!replay.files_removed&&replay.mutation_started&&replay.phase==BW_GAME_REMOVE_UNKNOWN&&f.exclusions==calls+1);tree_pin(path,after);C(!memcmp(journal,after,32));tree_pin(f.game,after);C(!memcmp(game_hash,after,32));tree_pin(state,after);C(!memcmp(state_hash,after,32));}
    }
    if(!normal)C(!registration_calls);
    struct bw_removed_source removed={0};struct bw_removed_result hidden={0};C(!bw_removed_lookup(data,&binding,&removed,&hidden)&&hidden.state==(normal&&positive?BW_REMOVED_COMMITTED:BW_REMOVED_VISIBLE));
    uint8_t after[32];struct stat st;fingerprint(db,after);C(!memcmp(db_hash,after,32)&&!lstat(db,&st)&&same_stat(&db_before,&st));fingerprint(save,after);C(!memcmp(save_hash,after,32));if(*app){tree_pin(app,after);C(!memcmp(app_hash,after,32)&&!lstat(app,&st)&&same_stat(&app_before,&st)&&!lstat(param,&st)&&same_stat(&param_before,&st));}
    C(context.requests==requests&&input.fences==fences&&!context.open);free(image);free(input.bytes);
    printf("{\"passed\":true,\"checks\":%u,\"mode\":\"%s\",\"actualProviderSendAndV8Witness\":true,\"actualNrcCandidateCollector\":true,\"actualPreparationCleanup\":%s,\"removedPreparation\":%s,\"normalPath\":%s,\"externalAppAndDbUnchanged\":true,\"saveAndOriginalSourcePreserved\":true,\"postPreparationSourceRequests\":0,\"exclusionAndIdleModeled\":true,\"consoleActions\":0}\n",checks,mode,normal?"false":"true",positive?"true":"false",normal?"true":"false");return 0;
}
