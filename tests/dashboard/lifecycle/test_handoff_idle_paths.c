/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual lifecycle consumers. The runner generates byte-exact retained fixture
 * helper prefixes in its output directory; no event/parser implementation is
 * copied here. Source transport and process snapshots are explicit host seams. */
#define _GNU_SOURCE 1
#define _DEFAULT_SOURCE 1
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static const char started[]="[00:00:01]   [GAME] started: PPSA99001 pid=77 app_id=0x00000023\n";
static const char awaiting[]="[00:00:02]   [GAME] process exited, awaiting same-app handoff: PPSA99001 pid=77 app_id=0x00000023\n";
static const char handoff[]="[00:00:03]   [GAME] process handoff: PPSA99001 app_id=0x00000023 pid=77 -> pid=88\n";
static const char awaiting_new[]="[00:00:04]   [GAME] process exited, awaiting same-app handoff: PPSA99001 pid=88 app_id=0x00000023\n";
static const char final_old[]="[00:00:05]   [GAME] finalizing stopped application: PPSA99001 pid=77\n";
static const char final_new[]="[00:00:06]   [GAME] finalizing stopped application: PPSA99001 pid=88\n";
static const char ordinary[]="[00:00:00]   [MANUAL] scanned 3 manual source(s)\n";
static char observed_log[2200];
static unsigned log_opens,append_on_open,append_fired;
FILE *__real_fopen(const char *,const char *);
static void hf_store(const char *path,const char *data,int append){
    int fd=open(path,O_WRONLY|O_CREAT|(append?O_APPEND:O_TRUNC),0600);
    if(fd<0||write(fd,data,strlen(data))!=(ssize_t)strlen(data)||close(fd)){perror("fixture log write");exit(3);}
}
FILE *__wrap_fopen(const char *path,const char *mode){
    if(*observed_log&&!strcmp(path,observed_log)){
        log_opens++;
        if(append_on_open&&log_opens==append_on_open){append_fired=1;hf_store(path,started,1);hf_store(path,awaiting,1);}
    }
    return __real_fopen(path,mode);
}
static int hf_history(const char *mode){
    hf_store(observed_log,ordinary,0);append_on_open=append_fired=log_opens=0;
    if(!strcmp(mode,"quiet"))return 1;
    if(!strcmp(mode,"second-pending")){append_on_open=2;return 0;}
    hf_store(observed_log,started,1);hf_store(observed_log,awaiting,1);
    if(!strcmp(mode,"pending"))return 0;
    hf_store(observed_log,handoff,1);
    if(!strcmp(mode,"handoff-live"))return 0;
    if(!strcmp(mode,"stale-old")){hf_store(observed_log,final_old,1);return 0;}
    hf_store(observed_log,awaiting_new,1);hf_store(observed_log,final_new,1);
    if(!strcmp(mode,"complete"))return 1;
    fprintf(stderr,"unknown lifecycle fixture mode\n");exit(3);
}

#if defined(HANDOFF_SEND)
#include "native_copy_helpers.inc"
int main(int argc,char **argv){
    C(argc==8);int folder=!strcmp(argv[1],"folder");uint64_t length=strtoull(argv[5],NULL,10);C(length>65536&&strlen(argv[6])==64);
    measured.mode="none";measured.target="eboot.bin";
    struct smb2_context context={0};C(strlen(argv[2])<sizeof context.root);strcpy(context.root,argv[2]);for(unsigned i=0;i<64;i++)context.handles[i].fd=-1;measured.context=&context;
    struct bw_source_smb source={.smb=&context,.deadline=now()+600};struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");
    struct bw_provider_binding binding;C(!bw_provider_binding_init(&binding,&config,folder?"fixture":"fixture.ffpfsc","PPSA99001",folder?BW_PROVIDER_FOLDER:BW_PROVIDER_CONTAINER));
    C(snprintf(measured.source,sizeof measured.source,"%s/fixture/eboot.bin",argv[2])>0);C(snprintf(measured.game,sizeof measured.game,"%s/games/%s",argv[4],binding.binding.game)>0);C(snprintf(measured.final,sizeof measured.final,"%s/eboot.bin",measured.game)>0);strcpy(measured.parent,measured.game);
    struct bw_container_smb *image=NULL;struct image_source input={.context=&context};
    if(!folder){load_image(&input,argv[3]);image=calloc(1,sizeof *image);C(image);strcpy(image->path,"fixture.ffpfsc");image->handle=(struct smb2fh *)image;image->admitted.smb2_ino=7;image->admitted.smb2_size=input.size;image->admitted.smb2_mtime=1700000000;struct bw_container_ops ops={counted_image_read,image_generation};C(!bw_container_open(&image->container,&ops,&input,input.size));}
    C(snprintf(observed_log,sizeof observed_log,"%s/game.log",argv[4])>0);int expected=hf_history(argv[7]);
    unsigned descriptors=fd_count();struct bw_source_provider provider={0};measured.stage=1;
    C(!bw_source_provider_prepare_policy(&provider,&binding,&source,image,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",32u*1024u*1024u,counted_data_current,&context,BW_SOURCE_PLAN_COMPANIONS_V1));C(provider.bundle.ready);
    struct bw_native_send_files send={.data_root=argv[4],.game_log=observed_log,.provider=&provider,.current=counted_control_current,.opaque=&context,.idle=NULL};struct bw_native_prepared prepared={0};
    measured.stage=2;int rc=bw_native_send_files(&send,&prepared);measured.stage=0;
    C(!np_active&&!cp_io_guard&&!cp_idle_guard);C(measured.created_fds==measured.closed_fds);
    if(expected){C(!rc&&prepared.state==BW_PREPARED_VALID&&prepared.bytes_verified&&prepared.prepared_policy==3);C(log_opens>=2&&!(log_opens%2));C(measured.provider_copy_calls==1&&measured.provider_copy_bytes==length);struct bw_native_prepared verified={0};C(!bw_native_send_verify(argv[4],&binding,current,&context,&verified)&&verified.state==BW_PREPARED_VALID&&verified.bytes_verified);}
    else {C(rc!=0&&prepared.state==BW_PREPARED_UNKNOWN&&!prepared.bytes_verified);C(!measured.provider_copy_calls&&!measured.provider_copy_bytes&&!measured.created_fds);char receipt[2400];struct stat st;C(snprintf(receipt,sizeof receipt,"%s/native-prepared/%s/prepared.bin",argv[4],binding.binding.game)>0&&lstat(receipt,&st)<0&&errno==ENOENT);C(log_opens==(!strcmp(argv[7],"second-pending")?2u:1u));}
    if(!strcmp(argv[7],"second-pending"))C(append_fired);
    unsigned opens=log_opens;bw_source_provider_destroy(&provider);C(!context.open&&fd_count()==descriptors);free(image);free(input.bytes);
    printf("{\"passed\":true,\"checks\":%u,\"kind\":\"actual-default-send\",\"format\":\"%s\",\"mode\":\"%s\",\"accepted\":%s,\"logOpens\":%u,\"suppliedIdleCallback\":false,\"bodyCopied\":%s,\"noReceiptOnRefusal\":true}\n",checks,argv[1],argv[7],expected?"true":"false",opens,expected?"true":"false");return 0;
}
#elif defined(HANDOFF_DERIVED) || defined(HANDOFF_BATCH)
#define BW_NATIVE_FS_HOST_TEST 1
#if defined(HANDOFF_DERIVED)
#define BW_NATIVE_DERIVED_SEND_HOST_TEST 1
#include "native_derived_send.c"
#define hf_guard ds_games_stopped
#define hf_kind "actual-derived-guard"
#else
#define BW_NATIVE_BATCH_SEND_HOST_TEST 1
#include "native_batch_send.c"
#define hf_guard bs_games_stopped
#define hf_kind "actual-batch-guard"
#endif
static unsigned checks;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(2);}}while(0)
/* Directed tag contract checks: priority precedes byte position; within the
 * winning kind, the first occurrence wins. Run once in derived/quiet only. */
static void hf_tag_edges(void){
    static const char bracket[]="[x] [GAME] started";
    enum bw_lifecycle_idle_event_kind kind=BW_LIFECYCLE_IDLE_FINAL;
    const char *at=bw_lifecycle_idle_tag(bracket,sizeof bracket-1,&kind);
    C(kind==BW_LIFECYCLE_IDLE_START);C(at==bracket+sizeof bracket-1);
    static const char priority[]="[GAME] finalizing stopped application [GAME] started [GAME] started";
    kind=BW_LIFECYCLE_IDLE_FINAL;
    at=bw_lifecycle_idle_tag(priority,sizeof priority-1,&kind);
    C(kind==BW_LIFECYCLE_IDLE_START);
    C(at==priority+sizeof "[GAME] finalizing stopped application [GAME] started"-1);
    static const char boundary[]="[GAME] process handoff";
    kind=BW_LIFECYCLE_IDLE_FINAL;
    at=bw_lifecycle_idle_tag(boundary,sizeof boundary-1,&kind);
    C(kind==BW_LIFECYCLE_IDLE_HANDOFF);C(at==boundary+sizeof boundary-1);
}
int main(int argc,char **argv){C(argc==3);if(!strcmp(hf_kind,"actual-derived-guard")&&!strcmp(argv[2],"quiet"))hf_tag_edges();C(strlen(argv[1])<sizeof observed_log);strcpy(observed_log,argv[1]);int expected=hf_history(argv[2]);int rc=hf_guard(observed_log);C((rc==0)==expected);C(log_opens==(expected||!strcmp(argv[2],"second-pending")?2u:1u));if(!strcmp(argv[2],"second-pending"))C(append_fired);printf("{\"passed\":true,\"checks\":%u,\"kind\":\"%s\",\"mode\":\"%s\",\"accepted\":%s,\"logOpens\":%u}\n",checks,hf_kind,argv[2],expected?"true":"false",log_opens);return 0;}
#elif defined(HANDOFF_STARTUP)
#include "idle_log_cache_helpers.inc"
static void hf_replace(const char *text){enabled=0;hf_store(path,text,0);enabled=1;}
static void hf_append(const char *text){enabled=0;hf_store(path,text,1);enabled=1;}
static void hf_log_mode(const char *mode){strcpy(observed_log,path);enabled=0;(void)hf_history(mode);enabled=1;}
/* Exact firmware-shaped process records reuse the retained real parser recipe.
 * Only this supplied process-query boundary is modeled. */
static unsigned hf_owned_pid=77,hf_owned_usec=123456,hf_actual_pid=77,hf_actual_usec=123456,hf_owned_queries;
static struct bw_src_process_result hf_owned_query(void *p,void *buffer,size_t capacity){
    (void)p;hf_owned_queries++;struct bw_src_process_result result={.used=BW_STOP_PROFILE_RECORD};
    if(buffer){CHECK(capacity==result.used);memset(buffer,0,result.used);unsigned char *b=buffer;p32(b,BW_STOP_PROFILE_RECORD);p32(b+72,hf_actual_pid);p64(b+336,1700000000);p64(b+344,hf_actual_usec);strcpy((char *)b+447,"eboot.bin");}return result;
}
static void hf_cold(void){
    for(unsigned i=0;i<4;i++){reset(1);hf_log_mode(i==0?"complete":i==1?"pending":i==2?"handoff-live":"stale-old");CHECK((bw_src_idle_evaluate(&request,&ops)==0)==(i==0));CHECK(!live_count);enabled=0;cases++;}
}
static void hf_cached(void){
    reset(1);hf_log_mode("complete");warm();unsigned before=reads;CHECK(!evaluate()&&reads==before&&queries==8);finish(0);
    reset(1);warm();unsigned before_lines=lines;hf_append(started);hf_append(awaiting);hf_append(handoff);CHECK(evaluate()!=0&&lines>before_lines&&stats().invalidations==1);finish(0);
    reset(1);warm();hf_append(started);hf_append(awaiting);hf_append(handoff);hf_append(awaiting_new);hf_append(final_new);CHECK(!evaluate()&&stats().invalidations==1&&queries==8);finish(0);
    reset(1);warm();hf_append(started);hf_append(awaiting);hf_append(handoff);hf_append(final_old);CHECK(evaluate()!=0&&stats().invalidations==1);finish(0);
}
static void hf_adopt(void){
    for(unsigned i=0;i<6;i++){
        reset(1);hf_owned_pid=77;hf_owned_usec=123456;hf_actual_pid=77;hf_actual_usec=123456;hf_owned_queries=0;
        hf_replace(started);if(i==1)hf_append(awaiting);if(i>=2)hf_append(awaiting);if(i>=2)hf_append(handoff);
        if(i==3)hf_actual_pid=88;
        if(i==4){hf_actual_pid=88;hf_owned_pid=88;}
        if(i==5){hf_actual_pid=88;hf_owned_pid=88;hf_actual_usec++;}
        struct bw_stop_identity allowed={.pid=(int32_t)hf_owned_pid,.start_sec=1700000000,.start_usec=(int64_t)hf_owned_usec,.role="eboot.bin"};struct bw_stop_identity saved=allowed;
        struct bw_src_idle_ops ownops={fw,hf_owned_query,NULL};int rc=bw_src_adopt_evaluate(&request,&ownops,&allowed,"PPSA99001");CHECK((rc==0)==(i==0||i==4));CHECK(!memcmp(&allowed,&saved,sizeof allowed));CHECK(hf_owned_queries>=2&&!live_count);enabled=0;cases++;
    }
}
int main(void){strcpy(dir,"/tmp/blackwire-handoff-idle-XXXXXX");CHECK(mkdtemp(dir));CHECK(snprintf(path,sizeof path,"%s/log",dir)>0);CHECK(snprintf(other,sizeof other,"%s/other",dir)>0);hf_cold();hf_cached();hf_adopt();enabled=0;CHECK(!unlink(path));(void)unlink(other);CHECK(!rmdir(dir));printf("{\"passed\":true,\"checks\":%u,\"cases\":%u,\"kind\":\"actual-startup-idle\",\"coldCases\":4,\"cacheCases\":4,\"ownedCases\":6,\"allOwnedDescriptorsClosed\":true,\"allowedIdentityUnchanged\":true}\n",checks,cases);return 0;}
#else
#error Select exactly one actual consumer fixture
#endif
