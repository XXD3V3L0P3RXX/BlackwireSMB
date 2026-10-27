#define _DEFAULT_SOURCE 1
#include "home_protocol.h"
#include "home_session.h"
#include "source_binding.h"
#include "startup_runtime_idle.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static unsigned cases,checks;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"case %u line %u: %s\n",cases,__LINE__,#x);exit(1);}}while(0)
static double stamp(void){struct timespec t;C(!clock_gettime(CLOCK_MONOTONIC,&t));return (double)t.tv_sec+(double)t.tv_nsec/1e9;}
static void path(char *p,size_t n,const char *root,const char *leaf){int k=snprintf(p,n,"%s/%s",root,leaf);C(k>0&&(size_t)k<n);}
static void write_file(const char *p,const void *b,size_t n,mode_t mode){int fd=open(p,O_WRONLY|O_CREAT|O_EXCL,mode);C(fd>=0);C(write(fd,b,n)==(ssize_t)n);C(!close(fd));}
struct fixture {
    struct bw_home_session owner;struct bw_session_ops ops;
    struct bw_stop_identity birth;
    struct bw_reader_selection selection;struct bw_share_config config;
    char parent[1024],root[1200],lib[1250],share[1300],selected[1300],sentinel[1300];
    int present,reuse,cancel,app_changed,idle_busy,uid_flip,unknown,close_result;unsigned closes,uid_queries;
};
static int running(void *v,uint32_t *out){(void)v;*out=23;return 0;}
static int title(void *v,uint32_t app,char out[16]){(void)v;if(app!=23)return -1;strcpy(out,"PPSA04734");return 0;}
static int proc(void *v,int32_t pid,struct bw_stop_identity *out){struct fixture *f=v;C(pid==f->birth.pid);if(f->unknown)return -1;if(!f->present)return 0;*out=f->birth;if(f->reuse)out->start_usec++;return 1;}
static int64_t wall(void *v){struct fixture *f=v;return f->birth.start_sec*1000000+f->birth.start_usec+1000000;}
static int close_game(void *v,uint32_t app){struct fixture *f=v;C(app==23);f->closes++;return f->close_result;}
static int current(void *v){return ((struct fixture*)v)->cancel?-1:0;}
static int app_current(void *v,const struct bw_native_session *s){struct fixture *f=v;return f->app_changed||s->app_id!=23||strcmp(s->title,"PPSA04734")?-1:0;}
static int uid(void *v,pid_t pid,uid_t *out){struct fixture *f=v;C(pid==f->birth.pid);f->uid_queries++;*out=geteuid();if(f->uid_flip&&f->owner.private_inputs.share.ownership_confirmed&&!f->owner.private_inputs.selection.ownership_attempted)(*out)++;return 0;}
static int idle(void *v){return ((struct fixture*)v)->idle_busy?-1:0;}
static void init(struct fixture *f,const char *base){
    memset(f,0,sizeof *f);cases++;int n=snprintf(f->parent,sizeof f->parent,"%s/session-%u",base,cases);C(n>0&&(size_t)n<sizeof f->parent);C(!mkdir(f->parent,0700));
    strcpy(f->config.server,"192.0.2.20");strcpy(f->config.share,"OwnedBackups");strcpy(f->config.user,"project-fixture");strcpy(f->config.password,"fixture-value");
    struct bw_source_binding binding;C(!bw_source_binding_resolve(&binding,&f->config,"Owned Folder","PPSA04734"));
    strcpy(f->selection.source,binding.source);strcpy(f->selection.title,binding.title);strcpy(f->selection.game,binding.game);strcpy(f->selection.binding_hex,binding.game+10);
    memset(f->selection.run,'a',32);memset(f->selection.prepared_record_hex,'b',64);f->selection.eboot_size=12345;f->selection.manifest_count=4;f->selection.manifest_hash=123456789;
    f->selection.source_kind=BW_PROVIDER_FOLDER;f->selection.source_policy=BW_PROVIDER_POLICY_V1;
    path(f->root,sizeof f->root,f->parent,binding.game);C(!mkdir(f->root,0700));path(f->lib,sizeof f->lib,f->root,"fakelib2");C(!mkdir(f->lib,0700));
    path(f->share,sizeof f->share,f->lib,BW_PRELOAD_HANDOFF_SHARE);path(f->selected,sizeof f->selected,f->lib,BW_PRELOAD_HANDOFF_SELECTION);path(f->sentinel,sizeof f->sentinel,f->lib,"reader.sprx");write_file(f->sentinel,"unchanged-reader",16,0755);
    f->birth=(struct bw_stop_identity){.pid=500,.start_sec=1700000000,.start_usec=123456,.role="eboot.bin"};f->present=1;
    f->ops=(struct bw_session_ops){f,running,title,proc,wall,close_game};
    f->owner=(struct bw_home_session){.current=current,.app_current=app_current,.consumer_uid=uid,.idle=idle,.opaque=f,.deadline=stamp()+20};
    C(!bw_home_session_bind(&f->owner,&f->birth,&f->ops,f->selection.run,f->selection.title,23));
}
static void release(struct fixture *f){f->present=0;C(!bw_home_session_release(&f->owner,stamp()+20));C(!access(f->sentinel,F_OK));C(!access(f->root,F_OK));}
static void session_tests(const char *base){
    struct fixture f;struct stat st;char text[4096];int fd;
    init(&f,base);C(!bw_home_session_publish(&f.owner,f.root,&f.selection,&f.config));C(f.owner.publication_complete);C(f.uid_queries>4);
    C(!stat(f.share,&st)&&(st.st_mode&07777)==0600&&st.st_uid==geteuid());fd=open(f.selected,O_RDONLY);C(fd>=0);ssize_t used=read(fd,text,sizeof text);C(used>0);C(!close(fd));struct bw_reader_selection selected;C(!bw_reader_selection_parse(&selected,text,(size_t)used));C(!strcmp(selected.run,f.selection.run));
    C(bw_home_session_publish(&f.owner,f.root,&f.selection,&f.config));C(bw_home_session_close(&f.owner,f.selection.run)==BW_SESSION_CLOSE_REQUESTED);C(f.closes==1);C(bw_home_session_close(&f.owner,f.selection.run)==BW_SESSION_CLOSE_UNKNOWN);C(f.closes==1);release(&f);C(access(f.share,F_OK)&&errno==ENOENT);C(access(f.selected,F_OK)&&errno==ENOENT);
    init(&f,base);write_file(f.selected,"preexisting",11,0600);C(bw_home_session_publish(&f.owner,f.root,&f.selection,&f.config));C(access(f.share,F_OK)&&errno==ENOENT);fd=open(f.selected,O_RDONLY);C(fd>=0);C(read(fd,text,sizeof text)==11&&!memcmp(text,"preexisting",11));C(!close(fd));release(&f);C(!access(f.selected,F_OK));
    init(&f,base);write_file(f.share,"wrong-mode",10,0644);C(bw_home_session_publish(&f.owner,f.root,&f.selection,&f.config));C(!access(f.share,F_OK));release(&f);C(!access(f.share,F_OK));
    init(&f,base);f.uid_flip=1;C(bw_home_session_publish(&f.owner,f.root,&f.selection,&f.config));C(f.owner.private_inputs.share.ownership_confirmed);C(!f.owner.private_inputs.selection.ownership_attempted);C(!f.owner.publication_complete);f.uid_flip=0;release(&f);
    init(&f,base);C(!bw_home_session_publish(&f.owner,f.root,&f.selection,&f.config));f.owner.game.phase=BW_SESSION_STOPPED;f.reuse=1;C(bw_home_session_exited(&f.owner));C(bw_home_session_release(&f.owner,stamp()+20));C(!f.owner.cleanup_attempted);C(!access(f.share,F_OK));f.reuse=0;release(&f);
    init(&f,base);C(!bw_home_session_publish(&f.owner,f.root,&f.selection,&f.config));f.unknown=1;C(bw_home_session_exited(&f.owner));C(bw_home_session_close(&f.owner,f.selection.run)==BW_SESSION_CLOSE_UNKNOWN);C(!f.closes);f.unknown=0;release(&f);
    init(&f,base);C(!bw_home_session_publish(&f.owner,f.root,&f.selection,&f.config));f.owner.deadline=0;f.cancel=1;C(bw_home_session_current(&f.owner));C(bw_home_session_close(&f.owner,f.selection.run)==BW_SESSION_CLOSE_REQUESTED);C(f.closes==1);release(&f);
    init(&f,base);C(!bw_home_session_publish(&f.owner,f.root,&f.selection,&f.config));f.close_result=-1;C(bw_home_session_close(&f.owner,f.selection.run)==BW_SESSION_CLOSE_UNKNOWN);C(bw_home_session_close(&f.owner,f.selection.run)==BW_SESSION_CLOSE_UNKNOWN);C(f.closes==1);release(&f);
    init(&f,base);C(!bw_home_session_publish(&f.owner,f.root,&f.selection,&f.config));f.present=0;f.idle_busy=1;C(bw_home_session_release(&f.owner,stamp()+20));C(!f.owner.cleanup_attempted);C(!access(f.share,F_OK));f.idle_busy=0;release(&f);
    init(&f,base);f.owner.deadline=DBL_MAX*2;C(bw_home_session_current(&f.owner));f.present=0;C(!bw_home_session_release(&f.owner,stamp()+20));
}
static void codec_tests(void){
    cases++;struct bw_home_claim claim={.pid=222,.deadline_us=123456789};memset(claim.nonce,'a',32);memset(claim.map_sha256,0x12,32);char body[512],ack[512],run[33],expected[33];memset(expected,'b',32);expected[32]=0;size_t used=0;
    C(!bw_home_claim_format(&claim,body,sizeof body,&used));struct bw_home_claim decoded;C(!bw_home_claim_parse(&decoded,body,used));C(!memcmp(&claim,&decoded,sizeof claim));
    C(!bw_home_ack_format(claim.nonce,expected,ack,sizeof ack,&used));C(!bw_home_ack_parse(ack,used,claim.nonce,run));C(!strcmp(run,expected));char other[33];memset(other,'c',32);other[32]=0;C(bw_home_ack_parse(ack,used,other,run));C(!*run);
    const char *bad[]={"{}","[]","{\"protocol\":\"blackwire-home/1\",\"pid\":222,\"deadlineUs\":1e5,\"nonce\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"mapSha256\":\"1212121212121212121212121212121212121212121212121212121212121212\"}","{\"protocol\":\"blackwire-home/1\",\"pid\":222,\"deadlineUs\":9223372036854775808,\"nonce\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"mapSha256\":\"1212121212121212121212121212121212121212121212121212121212121212\"}","{\"protocol\":\"blackwire-home/1\",\"pid\":222,\"deadlineUs\":123,\"nonce\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"mapSha256\":\"1212121212121212121212121212121212121212121212121212121212121212\",\"pid\":223}","{\"protocol\":\"blackwire-home/1\",\"pid\":222,\"deadlineUs\":123,\"nonce\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"mapSha256\":\"1212121212121212121212121212121212121212121212121212121212121212\",\"root\":\"/untrusted\"}"};
    for(size_t i=0;i<sizeof bad/sizeof *bad;i++)C(bw_home_claim_parse(&decoded,bad[i],strlen(bad[i])));
    memset(claim.nonce,'a',sizeof claim.nonce);C(bw_home_claim_format(&claim,body,sizeof body,&used));C(bw_home_ack_parse(ack,strlen(ack),claim.nonce,run));C(bw_home_ack_format(claim.nonce,expected,ack,sizeof ack,&used));
}
static void u32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void u64(unsigned char *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(v>>(8*i));}
struct idle_fixture {unsigned query_count;int cancel,drift,other,missing;char log[1100];struct bw_stop_identity allowed;};
static uint32_t firmware(void *v){(void)v;return BW_STOP_PROFILE_FW;}
static struct bw_src_process_result query(void *v,void *out,size_t cap){
    struct idle_fixture *f=v;size_t n=(f->other?2:1)*BW_STOP_PROFILE_RECORD;if(!out)return (struct bw_src_process_result){0,0,n};C(cap==n);memset(out,0,n);unsigned char *p=out;
    for(unsigned i=0;i<(f->other?2u:1u);i++,p+=BW_STOP_PROFILE_RECORD){u32(p,BW_STOP_PROFILE_RECORD);u32(p+72,i?501:f->missing?502:500);u64(p+336,1700000000);u64(p+344,f->drift&&f->query_count?123457:123456);memcpy(p+447,"eboot.bin",10);}f->query_count++;return (struct bw_src_process_result){0,0,n};
}
static int idle_current(void *v){return ((struct idle_fixture*)v)->cancel?-1:0;}
static void idle_case(struct idle_fixture *f,const char *base,const char *log){
    memset(f,0,sizeof *f);cases++;int n=snprintf(f->log,sizeof f->log,"%s/idle-%u.log",base,cases);C(n>0&&(size_t)n<sizeof f->log);write_file(f->log,log,strlen(log),0600);f->allowed=(struct bw_stop_identity){.pid=500,.start_sec=1700000000,.start_usec=123456,.role="eboot.bin"};
}
static void idle_tests(const char *base){
    struct idle_fixture f;struct bw_src_idle_ops ops={firmware,query,&f};struct bw_startup_idle_request r={.current=idle_current,.opaque=&f};
    const char *game="[GAME] started: PPSA04734 pid=500 app_id=0x00000017\n";
    idle_case(&f,base,game);r.game_log=f.log;r.deadline=stamp()+20;C(!bw_src_adopt_evaluate(&r,&ops,&f.allowed,"PPSA04734"));f.query_count=0;C(bw_src_idle_evaluate(&r,&ops));
    idle_case(&f,base,game);r.game_log=f.log;r.deadline=stamp()+20;f.other=1;C(bw_src_adopt_evaluate(&r,&ops,&f.allowed,"PPSA04734"));
    idle_case(&f,base,game);r.game_log=f.log;r.deadline=stamp()+20;f.missing=1;C(bw_src_adopt_evaluate(&r,&ops,&f.allowed,"PPSA04734"));
    idle_case(&f,base,game);r.game_log=f.log;r.deadline=stamp()+20;f.drift=1;C(bw_src_adopt_evaluate(&r,&ops,&f.allowed,"PPSA04734"));
    idle_case(&f,base,"[GAME] started: PPSA99999 pid=500 app_id=0x00000017\n");r.game_log=f.log;r.deadline=stamp()+20;C(bw_src_adopt_evaluate(&r,&ops,&f.allowed,"PPSA04734"));
    idle_case(&f,base,"[GAME] started: PPSA04734 pid=500 app_id=0x00000017\n[GAME] started: PPSA99999 pid=501 app_id=0x00000018\n");r.game_log=f.log;r.deadline=stamp()+20;C(bw_src_adopt_evaluate(&r,&ops,&f.allowed,"PPSA04734"));
    idle_case(&f,base,"no game log row yet\n");r.game_log=f.log;r.deadline=stamp()+20;C(!bw_src_adopt_evaluate(&r,&ops,&f.allowed,"PPSA04734"));f.cancel=1;C(bw_src_adopt_evaluate(&r,&ops,&f.allowed,"PPSA04734"));
}
int main(int argc,char **argv){C(argc==2);codec_tests();session_tests(argv[1]);idle_tests(argv[1]);printf("{\"passed\":true,\"cases\":%u,\"checks\":%u}\n",cases,checks);return 0;}

