/* Actual source provider -> Send -> physical index/record -> offline inspector.
 * Raw SMB/image adapters are the same independent read-only namespace fixture. */
#define BW_NATIVE_SEND_HOST_TEST 1
#define BW_NATIVE_FS_HOST_TEST 1
#define BW_NATIVE_PRELOAD 1
#define main namespace_fixture_main
#include "../../compressed/namespace_test.c"
#undef main
#include "native_send.c"
#include "native_launch.h"
#include "native_prepared_target.h"
/* Explicit aborts for unrelated historical derived/batch selection branches. */
int bw_native_derived_send_inspect(const char *a,const struct bw_provider_binding *b,struct bw_native_derived_snapshot *c){C(0);return -1;}
void bw_native_derived_snapshot_destroy(struct bw_native_derived_snapshot *a){C(0);}
int bw_native_prepared_from_derived(const struct bw_native_derived_prepared *a,struct bw_native_prepared *b){C(0);return -1;}
int bw_native_batch_inspect_exact(const char *a,const struct bw_provider_binding *b,const char *c,const uint8_t d[32],struct bw_native_batch_snapshot *e){C(0);return -1;}
void bw_native_batch_snapshot_destroy(struct bw_native_batch_snapshot *a){C(0);}
int bw_native_prepared_from_batch(const struct bw_native_batch_prepared *a,struct bw_native_prepared *b){C(0);return -1;}
int smb2_stat(struct smb2_context *s,const char *path,struct smb2_stat_64 *out){C(0);return -1;}
struct smb2fh *smb2_open(struct smb2_context *s,const char *path,int flags){C(0);return NULL;}
int smb2_fstat(struct smb2_context *s,struct smb2fh *h,struct smb2_stat_64 *out){C(0);return -1;}
int smb2_close(struct smb2_context *s,struct smb2fh *h){C(0);return -1;}
static unsigned unmapped_calls;
static char output_root[1536];static int write_fault,close_fault;static unsigned original_closes,source_fault_triggered;
int __real_close(int);ssize_t __real_write(int,const void *,size_t);
static int original_output(int fd){char link[64],path[1800];snprintf(link,sizeof link,"/proc/self/fd/%d",fd);ssize_t n=readlink(link,path,sizeof path-1);if(n<0)return 0;path[n]=0;return !strncmp(path,output_root,strlen(output_root))&&strstr(path,"/source.original.files")!=NULL;}
int __wrap_close(int fd){int tracked=original_output(fd);int result=__real_close(fd);if(tracked&&close_fault&&++original_closes==3){close_fault=0;errno=EIO;return -1;}return result;}
ssize_t __wrap_write(int fd,const void *bytes,size_t size){if(write_fault&&original_output(fd)){if(write_fault==2&&size>1){write_fault=1;return __real_write(fd,bytes,size/2);}write_fault=0;errno=EIO;return -1;}return __real_write(fd,bytes,size);}
static void change_during_copy(void *opaque,const struct bw_source_progress *progress){if(progress->phase==BW_SOURCE_COPYING&&progress->bytes_done&&!source_fault_triggered){source_fault_triggered=1;((struct smb2_context *)opaque)->changed=1;}}
static void file_write(const char *path,const void *body,size_t size){int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644);C(fd>=0&&write(fd,body,size)==(ssize_t)size&&!close(fd));}
static int unmapped(void *opaque,const char *root,const char *title){C(opaque&&root&&title&&!strcmp(title,"PPSA99001"));unmapped_calls++;return 0;}
static void mutate(const char *path){int fd=open(path,O_RDWR|O_NOFOLLOW);uint8_t b;C(fd>=0&&pread(fd,&b,1,0)==1);b^=1;C(pwrite(fd,&b,1,0)==1&&!close(fd));}
int main(int argc,char **argv){C(argc==7);int folder=!strcmp(argv[1],"folder"),projected=atoi(argv[6]);const char *scenario=argv[5];
    C(strlen(argv[4])<sizeof output_root);strcpy(output_root,argv[4]);
    struct smb2_context context={0};C(strlen(argv[2])<sizeof context.root);strcpy(context.root,argv[2]);for(unsigned i=0;i<64;i++)context.handles[i].fd=-1;
    struct bw_source_smb source={.smb=&context,.deadline=now()+600};struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");
    struct bw_provider_binding binding;C(!bw_provider_binding_init(&binding,&config,folder?"fixture":"fixture.ffpfsc","PPSA99001",folder?BW_PROVIDER_FOLDER:BW_PROVIDER_CONTAINER));
    struct bw_container_smb *image=NULL;struct image_source input={.context=&context};
    if(!folder){load_image(&input,argv[3]);image=calloc(1,sizeof *image);C(image);strcpy(image->path,"fixture.ffpfsc");image->handle=(struct smb2fh *)image;image->admitted.smb2_ino=7;image->admitted.smb2_size=input.size;image->admitted.smb2_mtime=1700000000;
        struct bw_container_ops ops={image_read,image_generation};C(!bw_container_open(&image->container,&ops,&input,input.size));}
    char log[1500];C(snprintf(log,sizeof log,"%s/game.log",argv[4])>0);int logfd=open(log,O_CREAT|O_EXCL|O_WRONLY,0600);C(logfd>=0&&!close(logfd));
    struct bw_native_prepared result={0};struct bw_source_provider provider={0};struct bw_native_send_files send={.data_root=argv[4],.game_log=log,.provider=&provider,.current=current,.opaque=&context,.upgrade_unmapped=unmapped,.upgrade_opaque=&context};
    if(!strcmp(scenario,"migrate")){
        C(!bw_source_provider_prepare(&provider,&binding,&source,image,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",32u*1024u*1024u,current,&context));
        C(!bw_native_send_files(&send,&result)&&result.state==BW_PREPARED_VALID&&result.bytes_verified&&!result.prepared_policy);
        struct bw_native_prepared old={0};C(!bw_native_send_inspect(argv[4],&binding,&old)&&old.state==BW_PREPARED_VALID&&!old.prepared_policy&&old.startup_upgrade_required);
        bw_source_provider_destroy(&provider);
    }
    C(!bw_source_provider_prepare_policy(&provider,&binding,&source,image,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",32u*1024u*1024u,current,&context,BW_SOURCE_PLAN_COMPANIONS_V1));
    uint8_t original_map[32],original_digests[32];cp_sha_bytes(provider.bundle.map.bytes,provider.bundle.map.size,original_map);cp_sha_bytes(provider.bundle.digests,provider.bundle.digest_size,original_digests);
    if(!strcmp(scenario,"partial-state"))write_fault=2;
    if(!strcmp(scenario,"close-state"))close_fault=1;
    if(!strcmp(scenario,"source-change")){source.observe=change_during_copy;source.observe_opaque=&context;}
    int sent=bw_native_send_files(&send,&result);char game[1500],state[1500],path[1700];C(snprintf(game,sizeof game,"%s/games/%s",argv[4],binding.binding.game)>0);C(snprintf(state,sizeof state,"%s/native-prepared/%s",argv[4],binding.binding.game)>0);
    if(!strcmp(scenario,"partial-state")||!strcmp(scenario,"source-change")){
        C(sent&&result.state==BW_PREPARED_UNKNOWN&&!result.bytes_verified);snprintf(path,sizeof path,"%s/prepared.bin",state);C(access(path,F_OK)<0&&errno==ENOENT);
        if(!strcmp(scenario,"partial-state"))C(!write_fault&&source.error&&strstr(source.error,"exclusively"));else C(source_fault_triggered);goto done;
    }
    if(!strcmp(scenario,"close-state")){C(sent==BW_SEND_UNCONFIRMED&&result.state==BW_PREPARED_UNKNOWN&&!result.bytes_verified&&!close_fault&&original_closes==3);snprintf(path,sizeof path,"%s/prepared.bin",state);C(!access(path,F_OK));goto done;}
    if(!strcmp(scenario,"profile-limit")){
        C(sent&&result.state==BW_PREPARED_UNKNOWN&&source.error&&strstr(source.error,"256"));snprintf(path,sizeof path,"%s/eboot.bin",game);C(access(path,F_OK)<0&&errno==ENOENT);snprintf(path,sizeof path,"%s/prepared.bin",state);C(access(path,F_OK)<0&&errno==ENOENT);goto done;
    }
    if(sent)fprintf(stderr,"send=%d error=%s retirement=%s\n",sent,prepare_v2_error?prepare_v2_error:"",result.retirement_error?result.retirement_error:"");
    C(!sent&&result.state==BW_PREPARED_VALID&&result.bytes_verified&&result.prepared_policy==(projected?3u:0u));
    if(projected)C(!strcmp(result.prepared_component,binding.binding.game)&&result.provenance_size>=384);
    if(!strcmp(scenario,"migrate"))C(unmapped_calls>0&&result.retirement==BW_RETIRE_COMPLETE);
    struct bw_native_prepared inspected={0};unsigned before=context.requests,image_before=input.fences;
    C(!bw_native_send_inspect(argv[4],&binding,&inspected)&&inspected.state==BW_PREPARED_VALID&&!inspected.bytes_verified&&inspected.prepared_policy==result.prepared_policy);
    C(!bw_native_send_verify(argv[4],&binding,current,&context,&inspected)&&inspected.state==BW_PREPARED_VALID&&inspected.bytes_verified&&inspected.prepared_policy==result.prepared_policy);
    C(context.requests==before&&input.fences==image_before);
    struct bw_reader_selection metadata_selection;C(!bw_native_launch_selection(argv[4],&binding,&result,"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee",&metadata_selection));
    C(metadata_selection.prepared_policy==result.prepared_policy&&!!bw_reader_selection_is_projection(&metadata_selection)==projected);
    C(context.requests==before&&input.fences==image_before);
    if(projected){
        struct bw_native_launch_admission *admission=NULL;const char *error=NULL;struct bw_reader_selection selected;
        C(!bw_native_launch_admission_open(&admission,argv[4],&binding,&result,"cccccccccccccccccccccccccccccccc",current,&context,&error));
        C(!bw_native_launch_admission_selection(admission,&selected)&&bw_reader_selection_is_projection(&selected)&&selected.prepared_policy==3);
        struct bw_native_projection_inputs inputs;const struct bw_preload_companions *companions=NULL;
        C(bw_native_launch_admission_projection_inputs(admission,&inputs)<0);
        C(bw_native_launch_admission_companions(admission,&companions)<0&&!companions);
        C(!bw_native_launch_admission_close(&admission)&&!admission);
        C(!bw_native_launch_admission_open_for_removal(&admission,argv[4],&binding,&result,selected.run,current,&context,&error));
        struct bw_native_prepared removal_prepared={0};C(!bw_native_launch_admission_current_for_removal(admission,argv[4],&binding,&selected,&removal_prepared)&&removal_prepared.prepared_policy==3&&
            removal_prepared.provenance_size==result.provenance_size&&!memcmp(removal_prepared.provenance_sha256,result.provenance_sha256,32));
        C(!bw_native_launch_admission_companions_for_removal(admission,&companions)&&companions);
        C(bw_native_launch_admission_projection_inputs(admission,&inputs)<0);
        companions=NULL;C(bw_native_launch_admission_companions(admission,&companions)<0&&!companions);
        C(bw_native_launch_admission_current(admission,argv[4],&binding,&selected,NULL)<0);
        C(!bw_native_launch_admission_close(&admission)&&!admission);
        C(context.requests==before&&input.fences==image_before);
    }
    C(!bw_native_send_preflight(argv[4],&binding,result.record_sha256,&provider,current,&context,&inspected)&&inspected.state==BW_PREPARED_VALID&&inspected.bytes_verified&&inspected.prepared_policy==result.prepared_policy);
    if(projected){
        snprintf(path,sizeof path,"%s/%s",state,BW_COMPANION_ORIGINAL_MAP_NAME);struct stat st;C(!lstat(path,&st)&&(st.st_mode&07777)==0600);
        snprintf(path,sizeof path,"%s/%s",state,BW_COMPANION_ORIGINAL_DIGEST_NAME);C(!lstat(path,&st)&&(st.st_mode&07777)==0600);
        /* The compiled reader is an extra slot, never source Ampr bytes copied
         * or hashed into the immutable physical prepared source map. */
        snprintf(path,sizeof path,"%s/fakelib2/libSceAmpr.sprx",game);C(lstat(path,&st)<0&&errno==ENOENT);
        if(!strcmp(scenario,"receipt-tamper")){snprintf(path,sizeof path,"%s/%s",game,BW_COMPANION_RECEIPT_PATH);mutate(path);}
        if(!strcmp(scenario,"original-tamper")){snprintf(path,sizeof path,"%s/%s",state,BW_COMPANION_ORIGINAL_MAP_NAME);mutate(path);}
    }else if(!strcmp(scenario,"stray-receipt")){snprintf(path,sizeof path,"%s/%s",game,BW_COMPANION_RECEIPT_PATH);file_write(path,"stray never selects projection",30);}
    if(strcmp(scenario,"normal")&&strcmp(scenario,"migrate")){before=context.requests;image_before=input.fences;C(!bw_native_send_inspect(argv[4],&binding,&inspected)&&inspected.state==BW_PREPARED_INVALID&&context.requests==before&&input.fences==image_before);}
done:
    {uint8_t hash[32];cp_sha_bytes(provider.bundle.map.bytes,provider.bundle.map.size,hash);C(!memcmp(hash,original_map,32));cp_sha_bytes(provider.bundle.digests,provider.bundle.digest_size,hash);C(!memcmp(hash,original_digests,32));}
    bw_source_provider_destroy(&provider);C(!context.open);free(image);free(input.bytes);
    printf("{\"passed\":true,\"checks\":%u,\"actualSendAndInspect\":true,\"offlineVerificationUsesNoSource\":true,\"sourceTablesUnchanged\":true,\"projected\":%s}\n",checks,projected?"true":"false");return 0;
}
