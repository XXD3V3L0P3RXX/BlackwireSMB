/* Actual native Send/provider/np_copy and filesystem publication. Only source
 * transport/image bytes and outer current/idle are modeled. Linker wrappers
 * count actual body I/O or inject one bounded syscall/result fault. */
#define BW_NATIVE_SEND_HOST_TEST 1
#define BW_NATIVE_FS_HOST_TEST 1
#define BW_NATIVE_PRELOAD 1
#define main private_retained_namespace_main
#include "../../compressed/namespace_test.c"
#undef main
#include "native_send.c"
#include "native_launch.h"
#include "native_prepared_target.h"

int bw_native_derived_send_inspect(const char *a,const struct bw_provider_binding *b,struct bw_native_derived_snapshot *c){(void)a;(void)b;(void)c;C(0);return -1;}
void bw_native_derived_snapshot_destroy(struct bw_native_derived_snapshot *a){(void)a;C(0);}
int bw_native_prepared_from_derived(const struct bw_native_derived_prepared *a,struct bw_native_prepared *b){(void)a;(void)b;C(0);return -1;}
int bw_native_batch_inspect_exact(const char *a,const struct bw_provider_binding *b,const char *c,const uint8_t d[32],struct bw_native_batch_snapshot *e){(void)a;(void)b;(void)c;(void)d;(void)e;C(0);return -1;}
void bw_native_batch_snapshot_destroy(struct bw_native_batch_snapshot *a){(void)a;C(0);}
int bw_native_prepared_from_batch(const struct bw_native_batch_prepared *a,struct bw_native_prepared *b){(void)a;(void)b;C(0);return -1;}
int smb2_stat(struct smb2_context *s,const char *p,struct smb2_stat_64 *o){(void)s;(void)p;(void)o;C(0);return -1;}
struct smb2fh *smb2_open(struct smb2_context *s,const char *p,int f){(void)s;(void)p;(void)f;C(0);return NULL;}
int smb2_fstat(struct smb2_context *s,struct smb2fh *h,struct smb2_stat_64 *o){(void)s;(void)h;(void)o;C(0);return -1;}
int smb2_close(struct smb2_context *s,struct smb2fh *h){(void)s;(void)h;C(0);return -1;}


#include <stdarg.h>

enum {M_NONE,M_SOURCE,M_TEMP,M_FINAL,M_COUNT};
struct io_metric {uint64_t read_bytes,pread_bytes,write_bytes,read_calls,pread_calls,write_calls,fsync_calls;};
static struct {
    unsigned stage,fired,cleanup_fired,created_fds,closed_fds,body_unlink_calls,foreign_relation,consumer_active,current_revoked,idle_revoked;
    const char *mode,*target;struct smb2_context *context;
    int body_fds[2];
    char source[2200],temporary[2200],final[2200],game[2200],parent[2200],held[2400],foreign[2400],foreign_name[2400];
    struct io_metric io[3][M_COUNT];
    uint64_t data_current[3],control_current[3],idle_current[3],image_reads[3],image_bytes[3],provider_copy_calls,provider_copy_bytes,provider_consumer_calls;
    uint64_t consumer_current_calls,consumer_idle_calls,consumer_write_attempts,rejected_current_checks,rejected_idle_checks,attempts_at_mutation;
} measured={.body_fds={-1,-1}};
ssize_t __real_read(int,void *,size_t);
ssize_t __real_pread(int,void *,size_t,off_t);
ssize_t __real_write(int,const void *,size_t);
int __real_fsync(int);
int __real_close(int);
int __real_openat(int,const char *,int,...);
int __real_unlinkat(int,const char *,int);
int __real_bw_source_provider_read(struct bw_source_provider *,const char *,bw_source_consume_fn,void *);
static int scenario(const char *mode){return !strcmp(measured.mode,mode);}
static int fd_path(int fd,char *path,size_t capacity){
    int saved=errno;char name[64];snprintf(name,sizeof name,"/proc/self/fd/%d",fd);
    ssize_t n=readlink(name,path,capacity-1);errno=saved;
    if(n<0||(size_t)n>=capacity-1)return -1;path[n]=0;return 0;
}
static unsigned classify(int fd){
    if(fd==measured.body_fds[0]&&fd>=0)return M_TEMP;
    if(fd==measured.body_fds[1]&&fd>=0)return M_FINAL;
    char path[2400];if(fd_path(fd,path,sizeof path))return M_NONE;
    if(*measured.source&&!strcmp(path,measured.source))return M_SOURCE;
    if(*measured.temporary&&!strcmp(path,measured.temporary))return M_TEMP;
    if(*measured.final&&!strcmp(path,measured.final))return M_FINAL;return M_NONE;
}
static void raw_file(const char *path,const void *bytes,size_t size){
    int fd=__real_openat(AT_FDCWD,path,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,0644);C(fd>=0);
    C(__real_write(fd,bytes,size)==(ssize_t)size&&!__real_close(fd));
}
static void foreign_file(const char *path){static const char bytes[]="UNCHANGED FOREIGN FILE";raw_file(path,bytes,sizeof bytes-1);strcpy(measured.foreign,path);}
static int boot_temporary(const char *name){
    if(strncmp(name,".blackwire-native-",18)||!np_active)return 0;
    const char *dash=strrchr(name,'-');char *end;unsigned long id=strtoul(dash?dash+1:"",&end,10);
    if(!dash||strcmp(end,".tmp")||id>UINT32_MAX)return 0;
    struct bw_manifest_record row;return !bw_manifest_get(&np_active->plan->manifest,(uint32_t)id,&row)&&!strcmp(row.path,measured.target);
}
int __wrap_openat(int parent,const char *name,int flags,...){
    mode_t mode=0;if(flags&O_CREAT){va_list args;va_start(args,flags);mode=(mode_t)va_arg(args,int);va_end(args);}
    unsigned kind=M_NONE;char full[2400],directory[2200];
    if(measured.stage==2&&(flags&O_CREAT)&&!fd_path(parent,directory,sizeof directory)){
        C(snprintf(full,sizeof full,"%s/%s",directory,name)>0);
        if(!strcmp(directory,measured.parent)&&boot_temporary(name)){kind=M_TEMP;C(strlen(full)<sizeof measured.temporary);strcpy(measured.temporary,full);}
        else if(!strcmp(full,measured.final))kind=M_FINAL;
        if(kind&&!measured.fired&&(((scenario("temp-collision")||scenario("temp-symlink"))&&kind==M_TEMP)||((scenario("final-collision")||scenario("final-symlink")||scenario("final-hardlink"))&&kind==M_FINAL))){
            measured.fired=1;
            if(scenario("temp-symlink")||scenario("final-symlink")||scenario("final-hardlink")){
                char target[2400];C(snprintf(target,sizeof target,"%s.foreign-target",full)>0);foreign_file(target);strcpy(measured.foreign_name,full);
                if(scenario("final-hardlink")){C(!link(target,full));measured.foreign_relation=2;}
                else{C(!symlink(target,full));measured.foreign_relation=1;}
            }else foreign_file(full);
        }
    }
    int fd=__real_openat(parent,name,flags,mode);
    if(fd>=0&&kind){unsigned slot=kind==M_TEMP?0:1;C(measured.body_fds[slot]<0);measured.body_fds[slot]=fd;measured.created_fds++;}
    return fd;
}
static void after_body_write(int fd){
    if(measured.fired)return;
    if(scenario("abrupt-exit")){
        printf("{\"interrupted\":true,\"temporary\":\"%s\",\"final\":\"%s\"}\n",measured.temporary,measured.final);fflush(stdout);_exit(77);
    }else if(scenario("mode-change")){measured.fired=1;C(!fchmod(fd,0644));}
    else if(scenario("source-generation")){measured.fired=1;measured.context->changed=1;}
    else if(scenario("cancel")){measured.fired=1;measured.context->canceled=1;}
    else if(scenario("file-substitution")){
        char path[2400];C(!fd_path(fd,path,sizeof path));C(snprintf(measured.held,sizeof measured.held,"%s.held-original",path)>0);
        measured.fired=1;C(!rename(path,measured.held));foreign_file(path);
    }else if(scenario("file-hardlink")){
        char path[2400];C(!fd_path(fd,path,sizeof path));C(snprintf(measured.held,sizeof measured.held,"%s.held-link",path)>0);
        measured.fired=1;C(!link(path,measured.held));
    }else if(scenario("root-substitution")){
        C(snprintf(measured.held,sizeof measured.held,"%s.held-original",measured.game)>0);measured.fired=1;
        C(!rename(measured.game,measured.held)&&!mkdir(measured.game,0755));char mark[2400];C(snprintf(mark,sizeof mark,"%s/foreign.marker",measured.game)>0);foreign_file(mark);
    }
}
ssize_t __wrap_write(int fd,const void *bytes,size_t size){
    unsigned kind=classify(fd);ssize_t n;
    if(measured.stage==2&&(kind==M_TEMP||kind==M_FINAL)&&measured.consumer_active)measured.consumer_write_attempts++;
    if(measured.stage==2&&(kind==M_TEMP||kind==M_FINAL)&&!measured.fired){
        if(scenario("write-error")||scenario("cleanup-denied")||scenario("cleanup-uncertain")){measured.fired=1;errno=EIO;n=-1;goto counted;}
        if(scenario("write-zero")){measured.fired=1;n=0;goto counted;}
        if(scenario("write-overlong")){measured.fired=1;n=(ssize_t)size+1;goto counted;}
        if(scenario("write-eintr-current")||scenario("write-eintr-idle")){
            measured.fired=1;measured.current_revoked=scenario("write-eintr-current");measured.idle_revoked=scenario("write-eintr-idle");errno=EINTR;n=-1;goto counted;
        }
        if(scenario("write-short-current")||scenario("write-short-idle")){
            C(size>1);measured.fired=1;n=__real_write(fd,bytes,size/2);C(n==(ssize_t)(size/2));measured.current_revoked=scenario("write-short-current");measured.idle_revoked=scenario("write-short-idle");goto counted;
        }
        if(scenario("write-eintr")){measured.fired=1;errno=EINTR;n=-1;goto counted;}
        if(scenario("write-short")&&size>1){measured.fired=1;size/=2;}
    }
    n=__real_write(fd,bytes,size);
    if(n>0&&measured.stage==2&&(kind==M_TEMP||kind==M_FINAL))after_body_write(fd);
counted:;
    int saved=errno;if(kind&&measured.stage){struct io_metric *m=&measured.io[measured.stage][kind];m->write_calls++;if(n>0)m->write_bytes+=(uint64_t)n;}errno=saved;return n;
}
ssize_t __wrap_read(int fd,void *out,size_t size){unsigned kind=classify(fd);ssize_t n=__real_read(fd,out,size);int saved=errno;if(kind&&measured.stage){struct io_metric *m=&measured.io[measured.stage][kind];m->read_calls++;if(n>0)m->read_bytes+=(uint64_t)n;}errno=saved;return n;}
ssize_t __wrap_pread(int fd,void *out,size_t size,off_t at){
    unsigned kind=classify(fd);ssize_t n;
    if(measured.stage==2&&(kind==M_TEMP||kind==M_FINAL)&&!measured.fired){
        if(scenario("read-error")){measured.fired=1;errno=EIO;n=-1;goto counted;}
        if(scenario("read-eintr")){measured.fired=1;errno=EINTR;n=-1;goto counted;}
        if(scenario("read-short")&&size>1){measured.fired=1;size/=2;}
    }
    n=__real_pread(fd,out,size,at);
    if(n>0&&measured.stage==2&&(kind==M_TEMP||kind==M_FINAL)&&!measured.fired&&scenario("read-corrupt")){measured.fired=1;((uint8_t *)out)[0]^=1;}
counted:;
    int saved=errno;if(kind&&measured.stage){struct io_metric *m=&measured.io[measured.stage][kind];m->pread_calls++;if(n>0)m->pread_bytes+=(uint64_t)n;}errno=saved;return n;
}
int __wrap_fsync(int fd){
    unsigned kind=classify(fd);int n;
    if(measured.stage==2&&(kind==M_TEMP||kind==M_FINAL)&&!measured.fired&&scenario("file-fsync")){measured.fired=1;errno=EIO;n=-1;}
    else n=__real_fsync(fd);
    int saved=errno;if(kind&&measured.stage)measured.io[measured.stage][kind].fsync_calls++;errno=saved;return n;
}
int __wrap_close(int fd){
    unsigned kind=classify(fd);int body=fd>=0&&(fd==measured.body_fds[0]||fd==measured.body_fds[1]);
    int rc=__real_close(fd);int saved=errno;
    if(body){C(!rc);measured.closed_fds++;if(fd==measured.body_fds[0])measured.body_fds[0]=-1;if(fd==measured.body_fds[1])measured.body_fds[1]=-1;}
    if(measured.stage==2&&body&&kind==M_FINAL&&!measured.fired&&scenario("final-close")){measured.fired=1;errno=EIO;return -1;}
    errno=saved;return rc;
}
int __wrap_unlinkat(int parent,const char *name,int flags){
    char directory[2200],full[2400];int body=!fd_path(parent,directory,sizeof directory)&&snprintf(full,sizeof full,"%s/%s",directory,name)>0&&
        ((!strcmp(full,measured.temporary)&&*measured.temporary)||!strcmp(full,measured.final));
    if(measured.stage==2&&body)measured.body_unlink_calls++;
    if(measured.stage==2&&body&&!measured.cleanup_fired&&(scenario("cleanup-denied")||scenario("cleanup-uncertain"))){
        measured.cleanup_fired=1;if(scenario("cleanup-uncertain"))C(!__real_unlinkat(parent,name,flags));errno=EACCES;return -1;
    }return __real_unlinkat(parent,name,flags);
}
struct counted_consumer {bw_source_consume_fn consume;void *opaque;};
static int count_consumer(void *opaque,const void *bytes,size_t size){
    struct counted_consumer *p=opaque;measured.provider_consumer_calls++;measured.provider_copy_bytes+=size;const void *delivered=bytes;uint8_t copy[65536];
    if(!measured.fired&&scenario("consume-corrupt")){C(size<=sizeof copy);memcpy(copy,bytes,size);copy[0]^=1;measured.fired=1;delivered=copy;}
    C(!measured.consumer_active);measured.consumer_active=1;int rc=p->consume(p->opaque,delivered,size);measured.consumer_active=0;return rc;
}
int __wrap_bw_source_provider_read(struct bw_source_provider *provider,const char *path,bw_source_consume_fn consume,void *opaque){
    if(measured.stage==2&&!strcmp(path,measured.target)){
        C(consume==np_write&&opaque);const struct np_writer *writer=opaque;char actual[2400];C(!fd_path(writer->fd,actual,sizeof actual));
        C(!strcmp(actual,measured.temporary)||!strcmp(actual,measured.final));
        measured.provider_copy_calls++;struct counted_consumer counted={consume,opaque};return __real_bw_source_provider_read(provider,path,count_consumer,&counted);
    }return __real_bw_source_provider_read(provider,path,consume,opaque);
}
static int counted_data_current(void *opaque){if(measured.stage)measured.data_current[measured.stage]++;return current(opaque);}
static void callback_parent_mutation(unsigned idle){
    if(measured.fired||!measured.consumer_active)return;
    int requested=idle?(scenario("callback-idle-root")||scenario("callback-idle-parent")):(scenario("callback-current-root")||scenario("callback-current-parent"));
    if(!requested)return;measured.fired=1;measured.attempts_at_mutation=measured.consumer_write_attempts;
    const char *path=(scenario("callback-idle-parent")||scenario("callback-current-parent"))?measured.parent:measured.game;
    C(snprintf(measured.held,sizeof measured.held,"%s.held-original",path)>0&&!rename(path,measured.held)&&!mkdir(path,0755));char mark[2400];C(snprintf(mark,sizeof mark,"%s/foreign.marker",path)>0);foreign_file(mark);
}
static int counted_control_current(void *opaque){
    if(measured.stage)measured.control_current[measured.stage]++;if(measured.consumer_active)measured.consumer_current_calls++;
    callback_parent_mutation(0);if(measured.current_revoked){measured.rejected_current_checks++;return -1;}return current(opaque);
}
static int counted_idle_current(void *opaque){
    if(measured.stage)measured.idle_current[measured.stage]++;if(measured.consumer_active)measured.consumer_idle_calls++;
    callback_parent_mutation(1);if(measured.idle_revoked){measured.rejected_idle_checks++;return -1;}return current(opaque);
}
static int counted_image_read(void *opaque,uint64_t at,void *out,size_t size){int rc=image_read(opaque,at,out,size);measured.image_reads[measured.stage]++;if(!rc)measured.image_bytes[measured.stage]+=size;return rc;}
static unsigned fd_count(void){DIR *d=opendir("/proc/self/fd");C(d);unsigned n=0;for(struct dirent *e;(e=readdir(d));)if(e->d_name[0]!='.')n++;C(!closedir(d));return n;}
static void emit_io(const struct io_metric *p){printf("{\"readBytes\":%"PRIu64",\"preadBytes\":%"PRIu64",\"writeBytes\":%"PRIu64",\"readCalls\":%"PRIu64",\"preadCalls\":%"PRIu64",\"writeCalls\":%"PRIu64",\"fsyncCalls\":%"PRIu64"}",p->read_bytes,p->pread_bytes,p->write_bytes,p->read_calls,p->pread_calls,p->write_calls,p->fsync_calls);}
static void assert_foreign(void){if(!*measured.foreign)return;static const char expected[]="UNCHANGED FOREIGN FILE";char actual[sizeof expected];int fd=__real_openat(AT_FDCWD,measured.foreign,O_RDONLY|O_NOFOLLOW,0);C(fd>=0&&__real_read(fd,actual,sizeof actual)==sizeof expected-1&&!memcmp(actual,expected,sizeof expected-1)&&!__real_close(fd));if(measured.foreign_relation==1){char target[2400];ssize_t n=readlink(measured.foreign_name,target,sizeof target);C(n==(ssize_t)strlen(measured.foreign)&&!memcmp(target,measured.foreign,(size_t)n));}if(measured.foreign_relation==2){struct stat a,b;C(!lstat(measured.foreign,&a)&&!lstat(measured.foreign_name,&b)&&a.st_ino==b.st_ino&&a.st_dev==b.st_dev&&a.st_nlink==2&&b.st_nlink==2);}}
int main(int argc,char **argv){
    C(argc==8);int folder=!strcmp(argv[1],"folder");uint64_t length=strtoull(argv[5],NULL,10);C(length>65536&&length<8u*1024u*1024u&&strlen(argv[6])==64);measured.mode=argv[7];measured.target=(scenario("callback-current-parent")||scenario("callback-idle-parent"))?"startup/boot.sprx":"eboot.bin";
    struct smb2_context context={0};C(strlen(argv[2])<sizeof context.root);strcpy(context.root,argv[2]);for(unsigned i=0;i<64;i++)context.handles[i].fd=-1;measured.context=&context;
    struct bw_source_smb source={.smb=&context,.deadline=now()+600};struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");
    struct bw_provider_binding binding;C(!bw_provider_binding_init(&binding,&config,folder?"fixture":"fixture.ffpfsc","PPSA99001",folder?BW_PROVIDER_FOLDER:BW_PROVIDER_CONTAINER));
    C(snprintf(measured.source,sizeof measured.source,"%s/fixture/%s",argv[2],measured.target)>0);C(snprintf(measured.game,sizeof measured.game,"%s/games/%s",argv[4],binding.binding.game)>0);C(snprintf(measured.final,sizeof measured.final,"%s/%s",measured.game,measured.target)>0);strcpy(measured.parent,measured.final);char *last=strrchr(measured.parent,'/');C(last);*last=0;
    struct bw_container_smb *image=NULL;struct image_source input={.context=&context};
    if(!folder){load_image(&input,argv[3]);image=calloc(1,sizeof *image);C(image);strcpy(image->path,"fixture.ffpfsc");image->handle=(struct smb2fh *)image;image->admitted.smb2_ino=7;image->admitted.smb2_size=input.size;image->admitted.smb2_mtime=1700000000;struct bw_container_ops ops={counted_image_read,image_generation};C(!bw_container_open(&image->container,&ops,&input,input.size));}
    if(scenario("inspect-interrupted")){
        unsigned before=fd_count();struct bw_native_prepared interrupted={0};int inspected=bw_native_send_inspect(argv[4],&binding,&interrupted);C(inspected!=0||interrupted.state!=BW_PREPARED_VALID);C(fd_count()==before);
        printf("{\"passed\":true,\"checks\":%u,\"format\":\"%s\",\"mode\":\"abrupt-exit\",\"accepted\":false,\"bodyBytes\":%"PRIu64",\"actualNativeReadOnlyInspectRefused\":true,\"producerExitedWithoutCleanup\":true,\"consoleActions\":0,\"networkActions\":0}\n",checks,argv[1],length);free(image);free(input.bytes);return 0;
    }
    char log[2200];C(snprintf(log,sizeof log,"%s/game.log",argv[4])>0);raw_file(log,"",0);
    unsigned descriptors=fd_count();struct bw_source_provider provider={0};measured.stage=1;
    C(!bw_source_provider_prepare_policy(&provider,&binding,&source,image,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",32u*1024u*1024u,counted_data_current,&context,BW_SOURCE_PLAN_COMPANIONS_V1));
    C(provider.bundle.ready);struct bw_manifest map;C(!bw_manifest_open(&map,provider.bundle.map.bytes,provider.bundle.map.size));uint32_t boot_id=UINT32_MAX;
    for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record row;C(!bw_manifest_get(&map,i,&row));if(!strcmp(row.path,measured.target)){boot_id=i;C(row.flags==BW_MANIFEST_LOCAL&&row.size==length);}}
    C(boot_id!=UINT32_MAX);char planned[65];for(unsigned i=0;i<32;i++)snprintf(planned+2*i,3,"%02x",provider.bundle.digests[56u+32u*boot_id+i]);C(!strcmp(planned,argv[6]));
    struct bw_native_send_files send={.data_root=argv[4],.game_log=log,.provider=&provider,.current=counted_control_current,.opaque=&context,.idle=counted_idle_current};struct bw_native_prepared prepared={0};
    struct stat retained_before={0};if(scenario("retained-no-unmapped")){measured.stage=0;C(!bw_native_send_files(&send,&prepared)&&prepared.state==BW_PREPARED_VALID);C(!lstat(measured.final,&retained_before));}
    measured.stage=2;int rc=bw_native_send_files(&send,&prepared);measured.stage=0;
    int positive=scenario("none")||scenario("write-short")||scenario("write-eintr")||scenario("read-short")||scenario("read-eintr");
    C(measured.created_fds==measured.closed_fds&&measured.body_fds[0]<0&&measured.body_fds[1]<0&&!np_active&&!cp_io_guard);
    if(positive){if(rc)fprintf(stderr,"unexpectedcopyfailure mode=%s rc=%d error=%s\n",measured.mode,rc,prepare_v2_error);C(!rc&&prepared.state==BW_PREPARED_VALID&&prepared.bytes_verified&&prepared.prepared_policy==3);}
    else if(scenario("retained-no-unmapped")){
        C(rc!=0&&prepared.state==BW_PREPARED_UNKNOWN&&prepared.retirement==BW_RETIRE_REFUSED&&!measured.fired&&!measured.created_fds);
        struct stat st;C(!lstat(measured.final,&st)&&st.st_dev==retained_before.st_dev&&st.st_ino==retained_before.st_ino&&st.st_mtim.tv_sec==retained_before.st_mtim.tv_sec&&st.st_mtim.tv_nsec==retained_before.st_mtim.tv_nsec&&st.st_ctim.tv_sec==retained_before.st_ctim.tv_sec&&st.st_ctim.tv_nsec==retained_before.st_ctim.tv_nsec);
        struct bw_native_prepared retained={0};C(!bw_native_send_verify(argv[4],&binding,current,&context,&retained)&&retained.state==BW_PREPARED_VALID&&retained.bytes_verified);
    }
    else{C(rc!=0&&prepared.state==BW_PREPARED_UNKNOWN&&!prepared.bytes_verified&&measured.fired);char path[2400];struct stat st;
        C(snprintf(path,sizeof path,"%s/native-prepared/%s/prepared.bin",argv[4],binding.binding.game)>0&&lstat(path,&st)<0&&errno==ENOENT);
        C(snprintf(path,sizeof path,"%s/blackwire.files",measured.game)>0&&lstat(path,&st)<0&&errno==ENOENT);
        C(snprintf(path,sizeof path,"%s/ampr_emu.index",measured.game)>0&&lstat(path,&st)<0&&errno==ENOENT);
    }
    const struct io_metric *temp=&measured.io[2][M_TEMP],*final=&measured.io[2][M_FINAL];uint64_t writes=temp->write_bytes+final->write_bytes,reads=temp->read_bytes+temp->pread_bytes+final->read_bytes+final->pread_bytes;
    if(positive){C(scenario("retained-no-unmapped")?(!measured.provider_copy_calls&&!measured.provider_copy_bytes&&!writes&&reads>=length):(measured.provider_copy_calls==1&&measured.provider_copy_bytes==length));struct stat st;C(!lstat(measured.final,&st)&&S_ISREG(st.st_mode)&&(st.st_mode&07777)==0755&&st.st_nlink==1&&(uint64_t)st.st_size==length);if(scenario("retained-no-unmapped"))C(st.st_dev==retained_before.st_dev&&st.st_ino==retained_before.st_ino&&st.st_mtim.tv_sec==retained_before.st_mtim.tv_sec&&st.st_mtim.tv_nsec==retained_before.st_mtim.tv_nsec&&st.st_ctim.tv_sec==retained_before.st_ctim.tv_sec&&st.st_ctim.tv_nsec==retained_before.st_ctim.tv_nsec);struct bw_native_prepared verified={0};C(!bw_native_send_verify(argv[4],&binding,current,&context,&verified)&&verified.state==BW_PREPARED_VALID&&verified.bytes_verified);}
    else if(!scenario("retained-no-unmapped")){
        assert_foreign();struct stat st;
        if(scenario("file-hardlink")){C(!lstat(measured.temporary,&st)&&st.st_nlink==2&&!lstat(measured.held,&st)&&st.st_nlink==2);}
        if(scenario("file-substitution"))C(!lstat(measured.held,&st)&&S_ISREG(st.st_mode));
        if(scenario("root-substitution"))C(!lstat(measured.held,&st)&&S_ISDIR(st.st_mode));
        if(scenario("cleanup-denied")){C(measured.body_unlink_calls==1&&measured.cleanup_fired&&*measured.temporary&&!lstat(measured.temporary,&st));}
        if(scenario("cleanup-uncertain")){C(measured.body_unlink_calls==1&&measured.cleanup_fired&&*measured.temporary&&lstat(measured.temporary,&st)<0&&errno==ENOENT);}
        if(scenario("write-error")||scenario("write-zero")||scenario("read-error")||scenario("read-corrupt")||scenario("file-fsync")||scenario("consume-corrupt")||scenario("source-generation")||scenario("mode-change")){
            C(!*measured.temporary||(lstat(measured.temporary,&st)<0&&errno==ENOENT));C(lstat(measured.final,&st)<0&&errno==ENOENT);
        }
    }
    if(measured.current_revoked||measured.idle_revoked){C(measured.consumer_write_attempts==1);C(measured.current_revoked?measured.rejected_current_checks>0:measured.rejected_idle_checks>0);}
    if(!strncmp(measured.mode,"callback-",9))C(measured.fired&&measured.consumer_write_attempts==measured.attempts_at_mutation&&measured.consumer_write_attempts==0);
    if(positive)C(measured.consumer_current_calls>=measured.consumer_write_attempts&&measured.consumer_idle_calls>=measured.consumer_write_attempts);
    if(scenario("retained-no-unmapped"))C(!writes&&!measured.provider_copy_calls);
    C(!measured.io[1][M_SOURCE].write_bytes&&!measured.io[2][M_SOURCE].write_bytes);bw_source_provider_destroy(&provider);C(!context.open&&fd_count()==descriptors);free(image);free(input.bytes);
    printf("{\"passed\":true,\"checks\":%u,\"format\":\"%s\",\"mode\":\"%s\",\"accepted\":%s,\"bodyBytes\":%"PRIu64",\"localWriteBytes\":%"PRIu64",\"localReadBytes\":%"PRIu64",\"temporary\":",checks,argv[1],measured.mode,positive?"true":"false",length,writes,reads);emit_io(temp);printf(",\"final\":");emit_io(final);
    printf(",\"consumerFullCurrentCalls\":%"PRIu64",\"consumerIdleCalls\":%"PRIu64",\"consumerNativeWriteAttempts\":%"PRIu64",\"providerConsumerPieces\":%"PRIu64",\"sendFullCurrentCalls\":%"PRIu64",\"sendIdleCalls\":%"PRIu64",\"dataAuthorityPlanningCalls\":%"PRIu64",\"dataAuthoritySendCalls\":%"PRIu64",\"rejectedCurrentChecks\":%"PRIu64",\"rejectedIdleChecks\":%"PRIu64,measured.consumer_current_calls,measured.consumer_idle_calls,measured.consumer_write_attempts,measured.provider_consumer_calls,measured.control_current[2],measured.idle_current[2],measured.data_current[1],measured.data_current[2],measured.rejected_current_checks,measured.rejected_idle_checks);
    printf(",\"createdBodyFDs\":%u,\"closedBodyFDs\":%u,\"faultInjected\":%s,\"cleanupFaultInjected\":%s,\"actualNativeSendAndNpCopy\":true,\"independentlyPlannedDigestMatches\":true,\"noNewSuccessfulReceiptOnFault\":true,\"foreignAndSourcePreserved\":true,\"consoleActions\":0,\"networkActions\":0}\n",measured.created_fds,measured.closed_fds,measured.fired?"true":"false",measured.cleanup_fired?"true":"false");return 0;
}
