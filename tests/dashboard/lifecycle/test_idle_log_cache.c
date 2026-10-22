/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _DEFAULT_SOURCE 1
#include "idle_log_cache.h"
#include "game_stop_identity.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static unsigned checks,cases,enabled,live_count,reads,lines,queries,guards,closes,streams,stream_closes;
static unsigned owned[8192],stat_field,fail_stat,fail_name,fail_close,fail_fclose,fail_open,fail_dup,fail_seek,fail_fdopen,fail_ferror,fail_calloc;
static unsigned bad_current,busy,query_error,second_birth,firmware_bad,clock_error,mutate_at_guard,hit_stats,reenter,shift_on_line;
static double clock_shift;
static char dir[1024],path[1100],other[1100];
static const char operation[]="0123456789abcdef0123456789abcdef";
static const char manual[]="[00:06:50]   [MANUAL] scanned 3 manual source(s)\n";
static struct bw_idle_log_cache *cache;
static struct bw_startup_idle_request request;
static struct bw_src_idle_ops ops;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"case=%u line=%u: %s errno=%d\n",cases,__LINE__,#x,errno);abort();}}while(0)

int __real_open(const char *,int,...);int __real_close(int);int __real_dup(int);off_t __real_lseek(int,off_t,int);
int __real_fstat(int,struct stat *);int __real_lstat(const char *,struct stat *);FILE *__real_fdopen(int,const char *);
int __real_fclose(FILE *);int __real_ferror(FILE *);char *__real_fgets(char *,int,FILE *);
int __real_clock_gettime(clockid_t,struct timespec *);void *__real_calloc(size_t,size_t);
static void own(int fd){CHECK(fd>=0&&(unsigned)fd<8192&&!owned[fd]);owned[fd]=1;live_count++;}
static void release(int fd){CHECK(fd>=0&&(unsigned)fd<8192&&owned[fd]&&live_count);owned[fd]=0;live_count--;}
static void append(const char *s){int fd=__real_open(path,O_WRONLY|O_APPEND);CHECK(fd>=0);CHECK(write(fd,s,strlen(s))==(ssize_t)strlen(s));CHECK(!__real_close(fd));}
int __wrap_open(const char *name,int flags,...){
    mode_t mode=0;if(flags&O_CREAT){va_list args;va_start(args,flags);mode=(mode_t)va_arg(args,int);va_end(args);}
    if(enabled&&fail_open&&!strcmp(name,path)){errno=EACCES;return -1;}
    int fd=__real_open(name,flags,mode);if(enabled&&fd>=0)own(fd);return fd;
}
int __wrap_close(int fd){if(enabled){closes++;release(fd);}int rc=__real_close(fd);if(enabled&&fail_close){fail_close=0;errno=EIO;return -1;}return rc;}
int __wrap_dup(int fd){if(enabled&&fail_dup){errno=EMFILE;return -1;}int n=__real_dup(fd);if(enabled&&n>=0)own(n);return n;}
off_t __wrap_lseek(int fd,off_t off,int whence){if(enabled&&fail_seek){errno=EIO;return -1;}return __real_lseek(fd,off,whence);}
FILE *__wrap_fdopen(int fd,const char *mode){if(enabled&&fail_fdopen){errno=ENOMEM;return NULL;}FILE *f=__real_fdopen(fd,mode);if(enabled&&f)streams++;return f;}
int __wrap_fclose(FILE *f){int fd=fileno(f);if(enabled){stream_closes++;release(fd);}int rc=__real_fclose(f);if(enabled&&fail_fclose){fail_fclose=0;errno=EIO;return EOF;}return rc;}
static void alter(struct stat *s){
    switch(stat_field){case 1:s->st_uid++;break;case 2:s->st_gid++;break;case 3:s->st_blocks++;break;
    case 4:s->st_ctim.tv_nsec=(s->st_ctim.tv_nsec+1)%1000000000;break;case 5:s->st_mtim.tv_nsec=(s->st_mtim.tv_nsec+1)%1000000000;break;
    case 6:s->st_mtim.tv_nsec=1000000000;break;case 7:s->st_blocks=-1;break;default:break;}
}
int __wrap_fstat(int fd,struct stat *s){if(enabled&&fail_stat){fail_stat=0;errno=EIO;return -1;}int rc=__real_fstat(fd,s);if(enabled&&!rc){alter(s);if(mutate_at_guard)hit_stats++;}return rc;}
int __wrap_lstat(const char *name,struct stat *s){if(enabled&&fail_name){fail_name=0;errno=EACCES;return -1;}int rc=__real_lstat(name,s);if(enabled&&!rc&&!strcmp(name,path))alter(s);return rc;}
int __wrap_ferror(FILE *f){if(enabled&&fail_ferror){errno=EIO;return 1;}return __real_ferror(f);}
char *__wrap_fgets(char *s,int n,FILE *f){char *v=__real_fgets(s,n,f);if(enabled){reads++;if(v){lines++;if(shift_on_line){shift_on_line=0;clock_shift=25;}}}return v;}
int __wrap_clock_gettime(clockid_t id,struct timespec *t){
    if(enabled&&clock_error){errno=EIO;return -1;}int rc=__real_clock_gettime(id,t);
    if(enabled&&!rc&&id==CLOCK_MONOTONIC){long long ns=(long long)(clock_shift*1e9);t->tv_sec+=ns/1000000000;t->tv_nsec+=ns%1000000000;
        if(t->tv_nsec<0){t->tv_nsec+=1000000000;t->tv_sec--;}if(t->tv_nsec>=1000000000){t->tv_nsec-=1000000000;t->tv_sec++;}}
    return rc;
}
void *__wrap_calloc(size_t n,size_t z){if(enabled&&fail_calloc){errno=ENOMEM;return NULL;}return __real_calloc(n,z);}
static double real_now(void){struct timespec t;CHECK(!__real_clock_gettime(CLOCK_MONOTONIC,&t));return (double)t.tv_sec+(double)t.tv_nsec/1e9;}
static uint32_t fw(void *p){(void)p;return firmware_bad?0:BW_STOP_PROFILE_FW;}
static void p32(unsigned char *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(x>>(8*i));}
static void p64(unsigned char *p,uint64_t x){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(x>>(8*i));}
static struct bw_src_process_result query(void *p,void *buffer,size_t capacity){
    (void)p;unsigned which=queries++;struct bw_src_process_result out={.used=3*BW_STOP_PROFILE_RECORD};if(query_error){out.carry=1;return out;}
    if(buffer){CHECK(capacity>=out.used);memset(buffer,0,out.used);for(unsigned i=0;i<3;i++){
        unsigned char *b=(unsigned char *)buffer+i*BW_STOP_PROFILE_RECORD;p32(b,BW_STOP_PROFILE_RECORD);p32(b+72,i==2?5:i);p64(b+336,i==2?123:0);
        strcpy((char *)b+447,i==2?"payload.elf":i==1?"init":"kernel");}
        if(busy)strcpy((char *)buffer+2*BW_STOP_PROFILE_RECORD+447,"eboot.bin");
        if(second_birth&&which%4==3)p64((unsigned char *)buffer+2*BW_STOP_PROFILE_RECORD+336,124);
    }return out;
}
static int guard(void *p){
    (void)p;guards++;
    if(reenter){reenter=0;CHECK(bw_src_idle_evaluate_cached(&request,&ops,cache,operation)!=0);}
    if(mutate_at_guard&&hit_stats){unsigned mode=mutate_at_guard;mutate_at_guard=0;append(mode==2?"[GAME] started: PPSA00001 pid=90\n":manual);}
    return bad_current?-1:0;
}
static void fixture(unsigned n){
    enabled=0;(void)unlink(path);(void)unlink(other);FILE *f=fopen(path,"w");CHECK(f);for(unsigned i=0;i<n;i++)CHECK(fputs("ordinary log line\n",f)>=0);CHECK(!fclose(f));CHECK(!chmod(path,0600));
}
static void reset(unsigned n){
    CHECK(!cache&&!live_count);memset(owned,0,sizeof owned);reads=lines=queries=guards=closes=streams=stream_closes=0;
    stat_field=fail_stat=fail_name=fail_close=fail_fclose=fail_open=fail_dup=fail_seek=fail_fdopen=fail_ferror=fail_calloc=0;
    bad_current=busy=query_error=second_birth=firmware_bad=clock_error=mutate_at_guard=hit_stats=reenter=shift_on_line=0;clock_shift=0;
    fixture(n);request=(struct bw_startup_idle_request){.deadline=real_now()+2,.current=guard,.game_log=path};ops=(struct bw_src_idle_ops){fw,query,NULL};enabled=1;
}
static int evaluate(void){return bw_src_idle_evaluate_cached(&request,&ops,cache,operation);}
static struct bw_idle_log_cache_stats stats(void){struct bw_idle_log_cache_stats s;CHECK(!bw_idle_log_cache_stats(cache,&s));return s;}
static void begin(void){CHECK(!bw_idle_log_cache_begin(&cache,operation,real_now()+20,&request));}
static void warm(void){begin();CHECK(!evaluate());CHECK(queries==4);struct bw_idle_log_cache_stats s=stats();CHECK(s.misses==1&&s.hits==1&&s.open_anchor==1&&s.evaluations==1);}
static void finish(int expect){
    struct bw_idle_log_cache_release report={0};int rc=bw_idle_log_cache_close(&cache,&report);CHECK(rc==expect&&!cache);CHECK(report.release_unknown==(unsigned)(expect==-2));CHECK(!live_count);CHECK(streams==stream_closes);enabled=0;cases++;
}
static void basic(void){
    reset(3);warm();unsigned before=reads,g=guards;CHECK(!evaluate());CHECK(reads==before&&queries==8&&guards>g);CHECK(stats().hits==3);finish(0);
    reset(3);begin();CHECK(bw_src_idle_evaluate_cached(&request,&ops,NULL,operation)!=0);CHECK(!live_count);finish(0);
    reset(3);fail_calloc=1;CHECK(bw_idle_log_cache_begin(&cache,operation,real_now()+20,&request)!=0&&!cache&&!live_count);fail_calloc=0;enabled=0;cases++;
    reset(3);CHECK(bw_idle_log_cache_begin(&cache,"BAD",real_now()+20,&request)!=0&&!cache);CHECK(bw_idle_log_cache_begin(&cache,operation,real_now()-1,&request)!=0&&!cache);enabled=0;cases++;
}
static void mutations(void){
    for(unsigned kind=0;kind<15;kind++){
        reset(4);warm();unsigned before=reads;enabled=0;
        if(kind==0)append(manual);
        else if(kind==1)append("[GAME] started: PPSA00001 pid=90\n");
        else if(kind==2)append("partial");
        else if(kind==3){int fd=__real_open(path,O_WRONLY);CHECK(fd>=0);CHECK(pwrite(fd,"X",1,0)==1);CHECK(!__real_close(fd));}
        else if(kind==4)CHECK(!truncate(path,0));
        else if(kind==5||kind==6||kind==7){CHECK(!rename(path,other));if(kind==7)CHECK(!symlink(other,path));else {FILE *f=fopen(path,"w");CHECK(f);CHECK(fputs(kind==6?"[GAME] started: PPSA00001 pid=90\n":"replacement complete\n",f)>=0);CHECK(!fclose(f));}}
        else if(kind==8)CHECK(!chmod(path,0640));
        else if(kind==9)CHECK(!link(path,other));
        else if(kind==10){struct stat s;CHECK(!__real_lstat(path,&s));int fd=__real_open(path,O_WRONLY);CHECK(fd>=0);CHECK(pwrite(fd,"Y",1,0)==1);struct timespec t[2]={s.st_atim,s.st_mtim};CHECK(!futimens(fd,t));CHECK(!__real_close(fd));}
        else if(kind==11){struct stat s;CHECK(!__real_lstat(path,&s));struct timespec t[2]={s.st_atim,s.st_mtim};t[1].tv_nsec=(t[1].tv_nsec+1)%1000000000;CHECK(!utimensat(AT_FDCWD,path,t,0));}
        else if(kind==12)CHECK(!truncate(path,18));
        else if(kind==13)append("[GAME] started: INVALID!! pid=90\n");
        else append("[GAME] started: PPSA00001 pid=90\n[GAME] finalizing stopped application: PPSA00001 pid=90\n");
        enabled=1;int rc=evaluate();unsigned success=kind==0||kind==3||kind==5||kind==8||kind==10||kind==11||kind==12||kind==14;
        CHECK((rc==0)==success);CHECK(stats().invalidations==1);if(kind!=4&&kind!=7&&kind!=9)CHECK(reads>before);finish(0);
    }
    for(unsigned field=1;field<=7;field++){
        reset(3);warm();unsigned before=reads;stat_field=field;int rc=evaluate();CHECK((rc==0)==(field<=5));CHECK(reads>before&&stats().invalidations==1);stat_field=0;finish(0);
    }
}
static void failures(void){
    for(unsigned kind=0;kind<12;kind++){
        reset(3);warm();unsigned before=reads;
        if(kind==0)bad_current=1;else if(kind==1)clock_error=1;else if(kind==2)clock_shift=-1;
        else if(kind==3)fail_stat=1;else if(kind==4)fail_name=1;else if(kind==5)busy=1;
        else if(kind==6)query_error=1;else if(kind==7)firmware_bad=1;else if(kind==8)second_birth=1;
        else if(kind==9)request.deadline=real_now()-1;else if(kind==10){clock_shift=25;request.deadline=real_now()+100;}
        else request.game_log=other;
        CHECK(evaluate()!=0);CHECK(reads==before);CHECK(!stats().open_anchor);bad_current=clock_error=0;clock_shift=0;finish(0);
    }
    reset(3);warm();CHECK(bw_src_idle_evaluate_cached(&request,&ops,cache,"1123456789abcdef0123456789abcdef")!=0);CHECK(stats().poisoned&&!stats().open_anchor);finish(0);
    reset(3);warm();reenter=1;CHECK(evaluate()!=0&&stats().poisoned);finish(0);
    for(unsigned kind=0;kind<7;kind++){
        reset(3);begin();if(kind==0)fail_open=1;else if(kind==1)fail_dup=1;else if(kind==2)fail_seek=1;
        else if(kind==3)fail_fdopen=1;else if(kind==4)fail_ferror=1;else if(kind==5)fail_fclose=1;else {fail_close=1;fail_ferror=1;}
        CHECK(evaluate()!=0);CHECK(!stats().open_anchor);fail_open=fail_dup=fail_seek=fail_fdopen=fail_ferror=0;finish(kind>=5?-2:0);
    }
    reset(3);warm();append(manual);fail_close=1;unsigned before=closes;CHECK(evaluate()==-2&&stats().release_unknown);CHECK(closes==before+1);before=closes;finish(-2);CHECK(closes==before);
    reset(3);warm();fail_close=1;finish(-2);
}
static void guard_changes(void){
    for(unsigned kind=1;kind<=2;kind++){reset(3);warm();unsigned before=reads;mutate_at_guard=kind;hit_stats=0;CHECK((evaluate()==0)==(kind==1));CHECK(reads>before&&stats().invalidations==1);finish(0);}
    reset(3);warm();clock_shift=3;request.deadline=real_now()+5;CHECK(!evaluate());CHECK(stats().hits==3);clock_shift=0;finish(0);
    reset(30);begin();request.deadline=real_now()+100;shift_on_line=1;CHECK(evaluate()!=0);CHECK(lines==1&&queries==2&&!stats().open_anchor);clock_shift=0;finish(0);
}
struct foreign {struct bw_idle_log_cache *p;int result,closed,retained;};
static void *foreign_thread(void *arg){struct foreign *f=arg;f->result=bw_src_idle_evaluate_cached(&request,&ops,f->p,operation);struct bw_idle_log_cache_release r;struct bw_idle_log_cache *local=f->p;f->closed=bw_idle_log_cache_close(&local,&r);f->retained=local==f->p;return NULL;}
static void thread_owner(void){
    reset(3);warm();struct foreign f={cache,0,0,0};pthread_t t;CHECK(!pthread_create(&t,NULL,foreign_thread,&f));CHECK(!pthread_join(t,NULL));CHECK(f.result==-1&&f.closed==-1&&f.retained);CHECK(stats().open_anchor);CHECK(!evaluate());finish(0);
}
static void costs(void){
    reset(500);unsigned baseline_queries=0,baseline_lines=0;
    for(unsigned i=0;i<40;i++)CHECK(!bw_src_idle_evaluate(&request,&ops));baseline_queries=queries;baseline_lines=lines;CHECK(!live_count&&baseline_queries==160&&baseline_lines==40000);enabled=0;
    reset(500);begin();for(unsigned i=0;i<40;i++)CHECK(!evaluate());struct bw_idle_log_cache_stats s=stats();CHECK(queries==baseline_queries&&lines==500&&s.misses==1&&s.hits==79);
    printf("{\"evaluations\":40,\"uncachedProcessQueries\":%u,\"cachedProcessQueries\":%u,\"uncachedParsedLines\":%u,\"cachedParsedLines\":%u,\"cacheHits\":%u,\"cacheMisses\":%u}\n",baseline_queries,queries,baseline_lines,lines,s.hits,s.misses);finish(0);
}
int main(void){
    strcpy(dir,"/tmp/blackwire-idle-cache-XXXXXX");CHECK(mkdtemp(dir));CHECK(snprintf(path,sizeof path,"%s/log",dir)>0);CHECK(snprintf(other,sizeof other,"%s/other",dir)>0);
    basic();mutations();failures();guard_changes();thread_owner();costs();enabled=0;CHECK(!unlink(path));(void)unlink(other);CHECK(!rmdir(dir));
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"allOwnedDescriptorsClosed\":true,\"consoleActions\":0}\n",cases,checks);return 0;
}
