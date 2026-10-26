/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual Send/receipt/index/required-startup/register/preflight code on private
 * host fixtures. SMB bytes/metadata are read-only local fixture adapters;
 * transport/Sony runtime and unsupported derived branches are not exercised. */
#define BW_NATIVE_SEND_HOST_TEST 1
#define BW_LAUNCH_TIMING 1
#include "native_send.c"
#include "native_register.h"
#include "reader_preflight.h"
#include "native_registration.h"
#include <assert.h>
#include <stdarg.h>

static unsigned checks,cases,guard_calls,cancel_at,provider_checks,remote_lookups,remote_reads;
static unsigned measuring,tree_reads,stream_opens,required_hash_reads,required_hash_bytes,transport_posts;
static int fail_close_once;
static char source_root[2048];
static const char *scenario="initialize";
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"fast Play %s line %d: %s (%s)\n",scenario,__LINE__,#x,prepare_v2_error);exit(1);}}while(0)
void bw_lt_count(enum bw_lt_metric metric,uint64_t n){if(measuring&&metric==BW_LT_LOCAL_HASH_BYTES)required_hash_bytes+=(unsigned)n;}
void bw_lt_span_begin(enum bw_lt_stage s){(void)s;}
void bw_lt_span_end(enum bw_lt_stage s,int r){(void)s;(void)r;}
int __real_openat(int,const char *,int,...);
int __wrap_openat(int fd,const char *path,int flags,...){
    mode_t mode=0;if(flags&O_CREAT){va_list a;va_start(a,flags);mode=(mode_t)va_arg(a,int);va_end(a);}
    if(measuring&&!strncmp(path,"stream-",7))stream_opens++;
    return __real_openat(fd,path,flags,mode);
}
struct dirent *__real_readdir(DIR *);
struct dirent *__wrap_readdir(DIR *d){if(measuring)tree_reads++;return __real_readdir(d);}
int __real_close(int);
int __wrap_close(int fd){int r=__real_close(fd);if(fail_close_once){fail_close_once=0;errno=EIO;return -1;}return r;}
ssize_t __real_pread(int,void *,size_t,off_t);
ssize_t __wrap_pread(int fd,void *body,size_t size,off_t at){
    ssize_t n=__real_pread(fd,body,size,at);
    if(measuring&&n>0){char name[64],path[4096];snprintf(name,sizeof name,"/proc/self/fd/%d",fd);ssize_t length=readlink(name,path,sizeof path-1);
        if(length>0){path[length]=0;const char *leaf=strrchr(path,'/');leaf=leaf?leaf+1:path;
            if(!strcmp(leaf,"eboot.bin")||!strcmp(leaf,"param.json")||!strcmp(leaf,"libfixture.sprx"))required_hash_reads++;}}
    return n;
}
static int current(void *p){CHECK(p);guard_calls++;return cancel_at&&guard_calls>=cancel_at?-1:0;}
static void path(char out[2048],const char *a,const char *b){int n=snprintf(out,2048,"%s/%s",a,b);CHECK(n>0&&n<2048);}
static void file_write(const char *name,const void *body,size_t size,mode_t mode){
    int fd=open(name,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,mode);CHECK(fd>=0&&!fchmod(fd,mode)&&!cp_write_all(fd,body,size)&&!close(fd));
}
static void *file_read(const char *name,size_t *size){
    struct stat st;int fd=open(name,O_RDONLY|O_NOFOLLOW);CHECK(fd>=0&&!fstat(fd,&st)&&st.st_size>=0);*size=(size_t)st.st_size;
    void *p=malloc(*size?*size:1);CHECK(p&&pread(fd,p,*size,0)==(ssize_t)*size&&!close(fd));return p;
}
int bw_source_provider_current(struct bw_source_provider *p){CHECK(p&&p->bundle.ready);provider_checks++;return current(p);}
int bw_source_provider_read(struct bw_source_provider *p,const char *name,bw_source_consume_fn consume,void *o){
    CHECK(p&&consume&&!bw_source_provider_current(p));char full[2048];path(full,source_root,name);size_t size;void *body=file_read(full,&size);
    int result=consume(o,body,size);free(body);return result;
}
void bw_source_smb_progress(struct bw_source_smb *p,enum bw_source_phase phase,uint32_t a,uint32_t b,uint64_t c,uint64_t d){
    CHECK(p&&a<=b&&c<=d);p->progress=(struct bw_source_progress){.phase=phase,.files_done=a,.files_total=b,.bytes_done=c,.bytes_total=d};
}
static int remote_path(const char *name,char full[2048]){
    const char prefix[]="Fixture/";if(strncmp(name,prefix,sizeof prefix-1))return -1;path(full,source_root,name+sizeof prefix-1);return 0;
}
int bw_source_smb_file_info(struct bw_source_smb *p,const char *name,struct bw_source_info *out){
    CHECK(p&&p->smb&&name&&out);remote_lookups++;char full[2048];struct stat s;
    if(remote_path(name,full)||lstat(full,&s)||!S_ISREG(s.st_mode))return BW_SOURCE_MISSING;
    *out=(struct bw_source_info){.size=(uint64_t)s.st_size,.inode=(uint64_t)s.st_ino,
        .mtime_ticks=UINT64_C(116444736000000000)+(uint64_t)s.st_mtime*10000000u,.ctime_ticks=0,.attributes=0};
    return BW_SOURCE_FOUND;
}
int bw_source_smb_small_file(struct bw_source_smb *p,const char *name,size_t maximum,void **bytes,size_t *size,struct bw_source_info *info){
    int result=bw_source_smb_file_info(p,name,info);if(result)return result;if(info->size>maximum)return BW_SOURCE_TOO_LARGE;
    char full[2048];CHECK(!remote_path(name,full));*bytes=file_read(full,size);remote_reads++;return 0;
}
int bw_sm_post(const struct bw_sm_connection *c,enum bw_sm_operation op,const char *root,struct bw_sm_reply *reply){
    CHECK(c&&root&&!c->current(c->opaque));transport_posts++;memset(reply,0,sizeof *reply);
    if(op==BW_SM_VERSION){reply->capabilities=7;return 0;}
    if(op==BW_SM_MANUAL_LIST){reply->present=1;return 0;}
    CHECK(0);return -1; /* no mutation is needed for the exact registered fixture */
}
static void mutate_byte(const char *name){size_t size;uint8_t *p=file_read(name,&size);CHECK(size);p[0]^=1;struct stat st;CHECK(!stat(name,&st));file_write(name,p,size,st.st_mode&07777);free(p);}
static int terminal_ready(const struct bw_native_prepared *p){unsigned any=0;for(unsigned i=0;i<sizeof p->record_sha256;i++)any|=p->record_sha256[i];return p->state==BW_PREPARED_VALID&&any;}
int main(int argc,char **argv){
    struct bw_manifest_record classified_ampr={.path="fakelib/libSceAmpr.sprx",.flags=BW_MANIFEST_LOCAL};
    CHECK(bw_startup_mode(classified_ampr.path)==0755&&na_required(&classified_ampr,0));
    CHECK(argc==4);scenario=argv[2];unsigned streams=(unsigned)strtoul(argv[3],NULL,10);CHECK(streams&&streams<=10000);
    char data[2048],fixture[2048],log[2048],name[2048],apps[2048],database[2048];
    path(data,argv[1],"data");path(source_root,argv[1],"source");path(fixture,argv[1],"metadata");path(log,argv[1],"game.log");path(apps,argv[1],"apps");path(database,argv[1],"app.db");
    struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");
    struct bw_provider_binding identity;CHECK(!bw_provider_binding_init(&identity,&config,"Fixture","PPSA99999",BW_PROVIDER_FOLDER));
    struct bw_source_provider provider={0};struct bw_source_smb source={.smb=(struct smb2_context *)(uintptr_t)1,.deadline=cp_now()+60};
    provider.identity=identity;provider.source=&source;provider.bundle.ready=1;strcpy(provider.bundle.source,"Fixture");strcpy(provider.bundle.game,identity.binding.game);strcpy(provider.bundle.title,"PPSA99999");
    size_t size;path(name,fixture,"map.bin");provider.bundle.map.bytes=file_read(name,&provider.bundle.map.size);
    path(name,fixture,"digests.bin");provider.bundle.digests=file_read(name,&provider.bundle.digest_size);
    path(name,fixture,"selection.txt");void *selection=file_read(name,&size);CHECK(size<sizeof provider.bundle.selection);memcpy(provider.bundle.selection,selection,size);free(selection);
    struct bw_manifest map;CHECK(!bw_manifest_open(&map,provider.bundle.map.bytes,provider.bundle.map.size));
    for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record row;CHECK(!bw_manifest_get(&map,i,&row));
        if(row.flags==BW_MANIFEST_LOCAL){provider.bundle.map.local_count++;provider.bundle.map.local_bytes+=row.size;}else{provider.bundle.map.stream_count++;provider.bundle.map.remote_bytes+=row.size;}}
    memset(provider.bundle.generation_sha256,0x3a,32);
    struct bw_native_send_files send={.data_root=data,.game_log=log,.provider=&provider,.current=current,.opaque=&provider};struct bw_native_prepared prepared={0};
    CHECK(!bw_native_send_files(&send,&prepared)&&terminal_ready(&prepared)&&prepared.bytes_verified);cases++;
    /* Strict Send and strict verify remain actual, independent APIs. */
    struct bw_native_prepared strict={0};CHECK(!bw_native_send_verify(data,&identity,current,&provider,&strict)&&strict.state==BW_PREPARED_VALID&&strict.bytes_verified);cases++;
    char root[2048],state[2048],target[2048];snprintf(root,sizeof root,"%s/games/%s",data,identity.binding.game);snprintf(state,sizeof state,"%s/native-prepared/%s",data,identity.binding.game);
    /* Uninstall may retain an owned legacy-classification receipt. Opening
     * its local witness must not clear the execution upgrade requirement. */
    struct bw_native_prepared legacy=prepared;legacy.startup_upgrade_required=1;
    struct bw_native_launch_admission *removal_owner=NULL;const char *removal_error=NULL;
    CHECK(bw_native_launch_admission_open(&removal_owner,data,&identity,&legacy,"dddddddddddddddddddddddddddddddd",current,&provider,&removal_error)<0&&!removal_owner);
    unsigned removal_lookups=remote_lookups;const struct bw_preload_source_ampr *offline_ampr=NULL;
    CHECK(!bw_native_launch_admission_open_for_removal(&removal_owner,data,&identity,&legacy,"dddddddddddddddddddddddddddddddd",current,&provider,&removal_error));
    CHECK(!bw_native_launch_admission_source_ampr_for_removal(removal_owner,&offline_ampr)&&offline_ampr&&offline_ampr->purpose==BW_SOURCE_AMPR_REMOVAL&&remote_lookups==removal_lookups);
    struct bw_reader_selection forbidden_selection;uint8_t forbidden_map[32];
    CHECK(bw_native_launch_admission_selection(removal_owner,&forbidden_selection)<0);
    CHECK(bw_native_launch_admission_map_sha256(removal_owner,forbidden_map)<0);
    const struct bw_preload_source_ampr *forbidden_witness=NULL;
    CHECK(bw_native_launch_admission_source_ampr(removal_owner,&forbidden_witness)<0&&!forbidden_witness);
    CHECK(!bw_native_launch_admission_close(&removal_owner)&&!removal_owner);cases++;
    struct bw_native_prepared bad_removal_receipt=prepared;bad_removal_receipt.record_sha256[0]^=1;
    fail_close_once=1;
    CHECK(bw_native_launch_admission_open_for_removal(&removal_owner,data,&identity,&bad_removal_receipt,"dddddddddddddddddddddddddddddddd",current,&provider,&removal_error)==-2&&!removal_owner&&!fail_close_once);cases++;
    fail_close_once=1;
    CHECK(bw_native_launch_admission_open(&removal_owner,data,&identity,&bad_removal_receipt,"dddddddddddddddddddddddddddddddd",current,&provider,&removal_error)==-2&&!removal_owner&&!fail_close_once&&removal_error&&strstr(removal_error,"released"));cases++;
    path(target,root,"eboot.bin");
    const int source_ampr_case=!strncmp(scenario,"source-ampr",11)&&strcmp(scenario,"source-ampr-extra-local");
    if(!strcmp(scenario,"source-ampr-local-tamper")){path(target,root,"fakelib/libSceAmpr.sprx");mutate_byte(target);}
    if(!strcmp(scenario,"source-ampr-mode")){path(target,root,"fakelib/libSceAmpr.sprx");CHECK(!chmod(target,0644));}
    if(!strcmp(scenario,"source-ampr-extra-local")){char directory[2048];path(directory,root,"fakelib");CHECK(!mkdir(directory,0755));
        path(target,directory,"libSceAmpr.sprx");file_write(target,"unsealed module bytes",21,0755);}
    if(!strcmp(scenario,"missing-boot"))CHECK(!unlink(target));
    if(!strcmp(scenario,"changed-boot"))mutate_byte(target);
    if(!strcmp(scenario,"changed-module")){path(target,root,"sce_module/libfixture.sprx");mutate_byte(target);}
    if(!strcmp(scenario,"mode-module")){path(target,root,"sce_module/libfixture.sprx");CHECK(!chmod(target,0644));}
    if(!strcmp(scenario,"linked-boot")){path(target,root,"eboot.bin");CHECK(!unlink(target));char other[2048];path(other,source_root,"eboot.bin");CHECK(!symlink(other,target));}
    if(!strcmp(scenario,"index-mismatch")){path(target,root,"ampr_emu.index");mutate_byte(target);}
    if(!strcmp(scenario,"map-mismatch")){path(target,root,"blackwire.files");mutate_byte(target);}
    if(!strcmp(scenario,"receipt-mismatch"))prepared.record_sha256[0]^=1;
    if(!strcmp(scenario,"source-owner-mismatch")){path(target,state,"source.json");mutate_byte(target);}
    if(!strcmp(scenario,"settings-before"))cancel_at=guard_calls+1;
    /* Strict preparation deliberately owns full placeholders; normal Ready
     * Play must not touch one, even a missing deep one, before actual use. */
    if(!strcmp(scenario,"missing-stream")){path(target,root,"assets/stream-00000.pak");CHECK(!unlink(target));}
    if(!strcmp(scenario,"changed-nonboot-local")){path(target,root,"startup.txt");mutate_byte(target);}
    measuring=1;tree_reads=stream_opens=required_hash_reads=required_hash_bytes=provider_checks=remote_lookups=remote_reads=0;
    struct bw_native_launch_admission *admission=NULL;const char *error=NULL;
    int opened=bw_native_launch_admission_open(&admission,data,&identity,&prepared,!strcmp(scenario,"zero-run")?"00000000000000000000000000000000":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",current,&provider,&error);
    unsigned expected_open_failure=!strcmp(scenario,"missing-boot")||!strcmp(scenario,"changed-boot")||!strcmp(scenario,"changed-module")||
        !strcmp(scenario,"mode-module")||!strcmp(scenario,"index-mismatch")||!strcmp(scenario,"map-mismatch")||!strcmp(scenario,"receipt-mismatch")||
        !strcmp(scenario,"source-owner-mismatch")||!strcmp(scenario,"settings-before")||!strcmp(scenario,"linked-boot")||!strcmp(scenario,"zero-run")||!strcmp(scenario,"source-ampr-local-tamper")||!strcmp(scenario,"source-ampr-mode");
    CHECK((opened!=0)==expected_open_failure);cases++;
    if(expected_open_failure){CHECK(!admission&&error);goto finish;}
    CHECK(admission&&admission->required_count==3u+(unsigned)source_ampr_case&&!admission->prepared.bytes_verified);
    const struct bw_preload_source_ampr *source_ampr=(const struct bw_preload_source_ampr *)(uintptr_t)1;
    const struct bw_preload_source_ampr *removal_ampr=NULL;unsigned offline_lookups=remote_lookups;
    CHECK(!bw_native_launch_admission_source_ampr_for_removal(admission,&removal_ampr)&&removal_ampr&&
        removal_ampr->purpose==BW_SOURCE_AMPR_REMOVAL&&!bw_preload_source_ampr_current(removal_ampr,root)&&remote_lookups==offline_lookups);
    CHECK(bw_native_launch_admission_source_ampr(admission,&source_ampr)==-1&&!source_ampr);
    unsigned hash_bytes_once=required_hash_bytes;struct bw_reader_selection selected;
    CHECK(!bw_native_launch_admission_selection(admission,&selected));
    CHECK(selected.manifest_count==streams+4u+(unsigned)source_ampr_case&&selected.source_kind==BW_PROVIDER_FOLDER&&!selected.prepared_policy);
    if(!strcmp(scenario,"settings-after"))cancel_at=guard_calls+1;
    if(!strcmp(scenario,"required-after")){path(target,root,"sce_module/libfixture.sprx");mutate_byte(target);}
    if(!strcmp(scenario,"receipt-after")){path(target,state,"prepared.bin");mutate_byte(target);}
    int valid=bw_native_launch_admission_current(admission,data,&identity,&selected,NULL);
    unsigned after_failure=!strcmp(scenario,"settings-after")||!strcmp(scenario,"required-after")||!strcmp(scenario,"receipt-after");
    CHECK((valid!=0)==after_failure);cases++;
    if(after_failure)goto close;
    if(!strcmp(scenario,"selected-mismatch")){selected.manifest_hash^=1;CHECK(bw_native_launch_admission_current(admission,data,&identity,&selected,NULL)!=0);cases++;goto close;}
    if(!strcmp(scenario,"source-binding"))strcpy(config.share,"ChangedShare");
    if(!strcmp(scenario,"source-missing-boot")){path(target,source_root,"eboot.bin");CHECK(!unlink(target));}
    if(!strcmp(scenario,"source-title")){path(target,source_root,"sce_sys/param.json");mutate_byte(target);}
    if(!strcmp(scenario,"source-module-size")){path(target,source_root,"sce_module/libfixture.sprx");file_write(target,"other",5,0644);}
    struct bw_reader_data_admission data_admission={0};struct bw_native_prepared checked={0};
    struct bw_reader_preflight preflight={.data_root=data,.connection=&config,.selected=&selected,.source=&source,.current=current,
        .opaque=&provider,.error=&error,.data_admission=&data_admission,.data_current=current,.data_opaque=&provider,.data_epoch=1,.startup=admission};
    if(!strcmp(scenario,"missing-play-epoch"))preflight.data_epoch=0;
    int preflight_result=bw_reader_preflight_run(&preflight,&checked);
    unsigned source_failure=!strcmp(scenario,"source-binding")||!strcmp(scenario,"source-missing-boot")||!strcmp(scenario,"source-title")||!strcmp(scenario,"source-module-size")||!strcmp(scenario,"missing-play-epoch");
    CHECK((preflight_result!=0)==source_failure);cases++;
    if(source_failure){CHECK(error&&!data_admission.valid&&!checked.bytes_verified);goto close;}
    CHECK(data_admission.valid&&checked.state==BW_PREPARED_VALID&&!checked.bytes_verified&&!memcmp(data_admission.map_sha256,admission->map_sha256,32));
    CHECK(!bw_native_launch_admission_source_ampr(admission,&source_ampr)&&source_ampr&&
        source_ampr->purpose==BW_SOURCE_AMPR_EXECUTION&&source_ampr!=removal_ampr&&removal_ampr->purpose==BW_SOURCE_AMPR_REMOVAL&&
        source_ampr->present==(unsigned)source_ampr_case&&!bw_preload_source_ampr_current(source_ampr,root));
    if(source_ampr_case){unsigned found=0;for(uint32_t id=0;id<admission->manifest.count;id++){struct bw_manifest_record row;
        CHECK(!bw_manifest_get(&admission->manifest,id,&row));if(strcmp(row.path,"fakelib/libSceAmpr.sprx"))continue;
        CHECK(row.flags==BW_MANIFEST_LOCAL&&source_ampr->size==row.size&&!memcmp(source_ampr->sha256,admission->digests+56u+32u*id,32));found++;}CHECK(found==1);}
    else CHECK(!source_ampr->size);
    if(!strcmp(scenario,"source-ampr-stale")){path(target,root,"fakelib/libSceAmpr.sprx");mutate_byte(target);
        CHECK(bw_preload_source_ampr_current(source_ampr,root)!=0);source_ampr=(const struct bw_preload_source_ampr *)(uintptr_t)1;
        CHECK(bw_native_launch_admission_source_ampr(admission,&source_ampr)!=0&&!source_ampr);goto close;}
    /* Real registration metadata/SQLite fixture is created by the Python
     * driver, after Send chose the exact root. Transport is read-only here. */
    path(target,argv[1],"root.txt");file_write(target,root,strlen(root),0600);
    if(!strcmp(scenario,"registration")){
        struct bw_native_register_request registration={.data_root=data,.apps=apps,.database=database,.identity=&identity,.selected=&selected,
            .current=current,.guard=&provider,.startup=admission};struct bw_register_result registered={0};
        /* Driver supplies the app parameter/link and DB before this process;
         * its mount uses the deterministic same binding as strict Send. */
        CHECK(!bw_native_register(&registration,&registered)&&registered.registered);cases++;
    }
    if(!strcmp(scenario,"foreign-registration")){
        struct bw_native_register_request registration={.data_root=data,.apps=apps,.database=database,.identity=&identity,.selected=&selected,
            .current=current,.guard=&provider,.startup=admission};struct bw_register_result registered={0};
        CHECK(bw_native_register(&registration,&registered)!=0&&!registered.registered&&!transport_posts);cases++;
    }
    /* Required body bytes are read exactly once; all reuses above perform no
     * local content reads. The PC bounded param adapter reads a different FD. */
    CHECK(hash_bytes_once==required_hash_bytes);cases++;
close:
    fail_close_once=!strcmp(scenario,"close-error");
    CHECK((bw_native_launch_admission_close(&admission)!=0)==!strcmp(scenario,"close-error"));CHECK(!admission);cases++;
finish:
    measuring=0;CHECK(!tree_reads&&!stream_opens&&!provider_checks);cases++;
    if(!strcmp(scenario,"normal")&&streams==64){
        /* Explicit strict Send revalidation still performs its original full
         * checks and publishes a new completion. Fast Play never does this. */
        char *run=strstr(provider.bundle.selection,"run_id=");CHECK(run);memset(run+7,'c',32);
        source.deadline=cp_now()+60;struct bw_native_prepared repaired={0};
        CHECK(!bw_native_send_files(&send,&repaired)&&repaired.state==BW_PREPARED_VALID&&repaired.bytes_verified);
        CHECK(!bw_native_send_verify(data,&identity,current,&provider,&strict)&&strict.state==BW_PREPARED_VALID&&strict.bytes_verified);cases++;
    }
    if(!strcmp(scenario,"changed-boot")){
        CHECK(!bw_native_send_verify(data,&identity,current,&provider,&strict)&&strict.state==BW_PREPARED_INVALID&&!strict.bytes_verified);cases++;
    }
    printf("{\"passed\":true,\"scenario\":\"%s\",\"streams\":%u,\"cases\":%u,\"checks\":%u,\"localRequiredHashBytes\":%u,\"treeReads\":%u,\"streamOpens\":%u,\"providerRescans\":%u,\"remoteLookups\":%u,\"remoteParamReads\":%u}\n",
        scenario,streams,cases,checks,required_hash_bytes,tree_reads,stream_opens,provider_checks,remote_lookups,remote_reads);
    free(provider.bundle.map.bytes);free((void *)provider.bundle.digests);return 0;
}
