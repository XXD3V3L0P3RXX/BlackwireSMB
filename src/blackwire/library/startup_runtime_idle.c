#define _POSIX_C_SOURCE 200809L
#include "startup_runtime_idle.h"
#include "idle_log_cache.h"
#include <pthread.h>
#include "game_stop_identity.h"
#include "copy_sha256.h"
#include "../streamreader/lifecycle.h"
#include <errno.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
/* Read-only query/fetch pattern follows ps5-payload-manager process_mgr.c.
 * Record authority remains the existing exact firmware/profile parser; no
 * guessed kernel offsets or application-info structures are introduced. */
#define SNAPSHOT_LIMIT (16u*1024u*1024u)
#define PROCESS_LIMIT 4096u
struct identity {int32_t pid;int64_t sec,usec;char name[20];};
static double now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (double)t.tv_sec+(double)t.tv_nsec/1e9;}
static int current(const struct bw_startup_idle_request *r){double t=now();return !r||!r->current||t<0||!(r->deadline>t&&r->deadline<=DBL_MAX)||r->current(r->opaque)?-1:0;}
static uint32_t u32(const unsigned char *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static uint64_t u64(const unsigned char *p){return (uint64_t)u32(p)|((uint64_t)u32(p+4)<<32);}
static int compare(const void *a,const void *b){const struct identity *x=a,*y=b;return x->pid<y->pid?-1:x->pid!=y->pid;}
static int snapshot(const struct bw_startup_idle_request *r,const struct bw_src_idle_ops *ops,struct identity **out,size_t *count,const struct bw_stop_identity *allowed){
    *out=NULL;*count=0;if(current(r)||ops->firmware(ops->opaque)!=BW_STOP_PROFILE_FW)return -1;
    struct bw_src_process_result capacity=ops->query(ops->opaque,NULL,0);
    if(current(r)||capacity.carry||capacity.value||!capacity.used||capacity.used>SNAPSHOT_LIMIT)return -1;
    unsigned char *data=malloc(capacity.used);if(!data)return -1;
    struct bw_src_process_result got=ops->query(ops->opaque,data,capacity.used);int result=-1;struct identity *rows=NULL;
    if(current(r)||ops->firmware(ops->opaque)!=BW_STOP_PROFILE_FW||got.carry||got.value||!got.used||got.used>capacity.used||got.used%BW_STOP_PROFILE_RECORD||got.used/BW_STOP_PROFILE_RECORD>PROCESS_LIMIT)goto done;
    size_t n=got.used/BW_STOP_PROFILE_RECORD;rows=calloc(n,sizeof *rows);if(!rows)goto done;unsigned busy=0,allowed_seen=0;
    for(size_t i=0;i<n;i++){
        if(current(r))goto done;const unsigned char *p=data+i*BW_STOP_PROFILE_RECORD;uint32_t pid=u32(p+72);
        if(u32(p)!=BW_STOP_PROFILE_RECORD||u32(p+4)||pid>INT32_MAX||!p[447]||!memchr(p+447,0,20)||u64(p+264)>INT64_MAX)goto done;
        rows[i].pid=(int32_t)pid;memcpy(rows[i].name,p+447,strnlen((const char *)p+447,20));rows[i].sec=(int64_t)u64(p+336);rows[i].usec=(int64_t)u64(p+344);
        if(pid>1){struct bw_stop_identity id;if(bw_stop_identity_parse(p,BW_STOP_PROFILE_RECORD,BW_STOP_PROFILE_FW,(int32_t)pid,&id))goto done;}
        else if(rows[i].sec<0||rows[i].usec<0||rows[i].usec>=1000000)goto done;
        for(size_t j=0;rows[i].name[j];j++){unsigned char c=(unsigned char)rows[i].name[j];if(c<32||c==127)goto done;}
        if(!strncmp(rows[i].name,"eboot",5)){
            if(!allowed||rows[i].pid!=allowed->pid||rows[i].sec!=allowed->start_sec||rows[i].usec!=allowed->start_usec||strcmp(rows[i].name,allowed->role))busy=1;
            else allowed_seen++;
        }
    }
    qsort(rows,n,sizeof *rows,compare);for(size_t i=1;i<n;i++)if(rows[i-1].pid==rows[i].pid)goto done;
    if(busy||(allowed&&allowed_seen!=1)||current(r))goto done;*out=rows;rows=NULL;*count=n;result=0;
done:free(rows);free(data);return result;
}
static int same(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_nlink==b->st_nlink&&a->st_size==b->st_size&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
/* An appended observation is never accepted. Keep the inode held and require
 * a fresh stable full observation, including the immutable prior byte prefix. */
#define LOG_OBSERVATIONS 3u
enum log_result { LOG_REFUSED=-1,LOG_STABLE=0,LOG_APPENDED=1 };
struct log_prefix {unsigned present;size_t bytes;unsigned char sha256[32];struct stat held;};
static int log_node(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_nlink==b->st_nlink&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid;
}
static int log_time_after(const struct timespec *a,const struct timespec *b){return a->tv_sec>b->tv_sec||(a->tv_sec==b->tv_sec&&a->tv_nsec>=b->tv_nsec);}
static int log_growth(const struct stat *before,const struct stat *after){
    return log_node(before,after)&&after->st_size>before->st_size&&after->st_blocks>=before->st_blocks&&
        log_time_after(&after->st_mtim,&before->st_mtim)&&log_time_after(&after->st_ctim,&before->st_ctim);
}
static int log_resume(const struct log_prefix *prefix,const struct stat *before){
    if(!prefix->present)return 1;
    if(!log_node(&prefix->held,before)||before->st_size<0||(uint64_t)before->st_size<prefix->bytes)return 0;
    return same(&prefix->held,before)||(log_growth(&prefix->held,before));
}
static enum log_result log_observe(const struct bw_startup_idle_request *r,const char *path,int fd,FILE *file,const struct stat *before,struct log_prefix *prefix,struct stat *stable,const struct bw_stop_identity *allowed,const char *title){
    struct bw_lifecycle_idle_fold fold;bw_lifecycle_idle_fold_init(&fold);
    size_t total=0,prefix_bytes=0;char line[1024];int bad=0;struct stat after,named;
    struct cp_sha whole,prior;cp_sha_init(&whole);cp_sha_init(&prior);
    while(fgets(line,sizeof line,file)){
        if(current(r)||!strchr(line,'\n')){bad=1;break;}size_t n=strlen(line);if(n>SNAPSHOT_LIMIT-total){bad=1;break;}
        if(total<(uint64_t)before->st_size&&(uint64_t)before->st_size-total<=n&&line[(size_t)before->st_size-total-1]!='\n'){bad=1;break;}total+=n;
        cp_sha_update(&whole,line,n);
        if(prefix->present&&prefix_bytes<prefix->bytes){size_t take=prefix->bytes-prefix_bytes;if(take>n)take=n;cp_sha_update(&prior,line,take);prefix_bytes+=take;}
        if(bw_lifecycle_idle_fold_line(&fold,line,n)<0){bad=1;break;}
    }
    if(bad||bw_lifecycle_idle_fold_finish(&fold,!ferror(file)&&feof(file),title,allowed?allowed->pid:0)||total<(uint64_t)before->st_size)return LOG_REFUSED;
    if(prefix->present){unsigned char digest[32];cp_sha_final(&prior,digest);if(prefix_bytes!=prefix->bytes||memcmp(digest,prefix->sha256,32))return LOG_REFUSED;}
    if(current(r)||fstat(fd,&after)||lstat(path,&named))return LOG_REFUSED;
    if(total==(uint64_t)before->st_size&&same(before,&after)&&same(&after,&named)&&log_node(before,&after)&&log_node(&after,&named)){if(stable)*stable=after;return LOG_STABLE;}
    if(!feof(file)||after.st_size<0||(uint64_t)after.st_size>SNAPSHOT_LIMIT||total>(uint64_t)after.st_size||!log_growth(before,&after)||!same(&after,&named)||!log_node(&after,&named))return LOG_REFUSED;
    cp_sha_final(&whole,prefix->sha256);prefix->bytes=total;prefix->held=after;prefix->present=1;return LOG_APPENDED;
}
static int log_idle_owned(const struct bw_startup_idle_request *r,int *out,struct stat *identity,unsigned *uncertain,const struct bw_stop_identity *allowed,const char *title){
    if(out)*out=-1;if(uncertain)*uncertain=0;
    const char *path=r->game_log?r->game_log:"/data/shadowmount/debug.log";if(current(r))return -1;
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;struct stat before,named;
    struct stat stable;
    if(fstat(fd,&before)||!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_size<=0||(uint64_t)before.st_size>SNAPSHOT_LIMIT||lstat(path,&named)||!same(&before,&named)){if(close(fd)&&uncertain)*uncertain=1;return -1;}
    struct log_prefix prefix={0};enum log_result result=LOG_REFUSED;
    for(unsigned observation=0;observation<LOG_OBSERVATIONS;observation++){
        if(observation&&(current(r)||fstat(fd,&before)||!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_size<=0||
            (uint64_t)before.st_size>SNAPSHOT_LIMIT||lstat(path,&named)||!same(&before,&named)||!log_resume(&prefix,&before))){result=LOG_REFUSED;break;}
        int reader=dup(fd);if(reader<0){result=LOG_REFUSED;break;}
        if(lseek(reader,0,SEEK_SET)!=0){if(close(reader)&&uncertain)*uncertain=1;result=LOG_REFUSED;break;}
        FILE *file=fdopen(reader,"r");if(!file){if(close(reader)&&uncertain)*uncertain=1;result=LOG_REFUSED;break;}
        result=log_observe(r,path,reader,file,&before,&prefix,&stable,allowed,title);
        if(fclose(file)){if(uncertain)*uncertain=1;result=LOG_REFUSED;}if(result!=LOG_APPENDED)break;
    }
    if(result==LOG_STABLE&&out){
        struct stat held;
        if(current(r)||fstat(fd,&held)||lstat(path,&named)||!same(&stable,&held)||!same(&held,&named)||
           !log_node(&stable,&held)||!log_node(&held,&named)||stable.st_blocks!=held.st_blocks||held.st_blocks!=named.st_blocks||
           held.st_mtim.tv_nsec<0||held.st_mtim.tv_nsec>=1000000000||held.st_ctim.tv_nsec<0||held.st_ctim.tv_nsec>=1000000000||held.st_blocks<0)result=LOG_REFUSED;
        else{*out=fd;*identity=held;fd=-1;}
    }
    if(fd>=0&&close(fd)){if(uncertain)*uncertain=1;result=LOG_REFUSED;}return result==LOG_STABLE?0:-1;
}
static int log_idle(const struct bw_startup_idle_request *r){return log_idle_owned(r,NULL,NULL,NULL,NULL,NULL);}

int bw_src_idle_evaluate(const struct bw_startup_idle_request *r,const struct bw_src_idle_ops *ops){
    if(!r||!ops||!ops->firmware||!ops->query||current(r))return -1;struct identity *a=NULL,*b=NULL;size_t an=0,bn=0;int result=-1;
    if(snapshot(r,ops,&a,&an,NULL)||log_idle(r)||snapshot(r,ops,&b,&bn,NULL)||an!=bn||memcmp(a,b,an*sizeof *a)||log_idle(r)||current(r))goto done;result=0;
done:free(a);free(b);return result;
}

/* Optional operation-local log proof; process snapshots are never cached. */
struct bw_idle_log_cache {
    pthread_t owner;
    int fd;
    unsigned valid,in_call,poisoned,release_unknown;
    unsigned evaluations,hits,misses,invalidations;
    double deadline,last;
    char operation[33],path[2048];
    struct stat identity;
};
static int cache_nonce(const char *s){
    if(!s||strnlen(s,33)!=32)return 0;
    for(unsigned i=0;i<32;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;
    return 1;
}
static const char *cache_path(const struct bw_startup_idle_request *r){return r->game_log?r->game_log:"/data/shadowmount/debug.log";}
static int cache_times(const struct stat *s){
    return s->st_mtim.tv_nsec>=0&&s->st_mtim.tv_nsec<1000000000&&s->st_ctim.tv_nsec>=0&&s->st_ctim.tv_nsec<1000000000;
}
static int cache_same(const struct stat *a,const struct stat *b){
    return cache_times(a)&&cache_times(b)&&same(a,b)&&log_node(a,b)&&a->st_blocks==b->st_blocks;
}
static int cache_regular(const struct stat *s){
    return S_ISREG(s->st_mode)&&s->st_nlink==1&&s->st_size>0&&(uint64_t)s->st_size<=SNAPSHOT_LIMIT&&s->st_blocks>=0&&cache_times(s);
}
static int cache_drop(struct bw_idle_log_cache *p){
    p->valid=0;
    if(p->fd>=0){int fd=p->fd;p->fd=-1;if(close(fd)){p->release_unknown=1;p->poisoned=1;}}
    return p->release_unknown?BW_IDLE_CACHE_RELEASE_UNKNOWN:0;
}
static int cache_guard(struct bw_idle_log_cache *p,const struct bw_startup_idle_request *r){
    double t=now();
    if(p->poisoned||p->release_unknown||!r||!r->current||t<0||t<p->last||
       !(p->deadline>t&&r->deadline>t&&r->deadline<=DBL_MAX)||strcmp(cache_path(r),p->path)||r->current(r->opaque)||p->poisoned){
        p->poisoned=1;return -1;
    }
    p->last=t;return 0;
}
int bw_idle_log_cache_begin(struct bw_idle_log_cache **out,const char operation[33],double operation_deadline,const struct bw_startup_idle_request *r){
    if(!out||*out||!cache_nonce(operation)||current(r)||*out)return -1;
    const char *path=cache_path(r);size_t n=strnlen(path,2048);double t=now();
    if(!n||n>=2048||path[0]!='/'||t<0||!(r->deadline>t&&r->deadline<=DBL_MAX&&operation_deadline>t&&operation_deadline<=DBL_MAX))return -1;
    struct bw_idle_log_cache *p=calloc(1,sizeof *p);if(!p)return -1;
    p->owner=pthread_self();p->fd=-1;p->deadline=operation_deadline;p->last=t;
    memcpy(p->operation,operation,33);memcpy(p->path,path,n+1);if(*out){free(p);return -1;}*out=p;return 0;
}
static int cache_log_idle(const struct bw_startup_idle_request *r,struct bw_idle_log_cache *p){
    if(cache_guard(p,r))return -1;
    if(p->valid){
        struct stat held,named;
        if(fstat(p->fd,&held)||lstat(p->path,&named))return -1;
        if(cache_same(&p->identity,&held)&&cache_same(&held,&named)&&cache_regular(&held)){
            if(cache_guard(p,r))return -1;
            /* Bracket current() too: it may observe or cause a source change. */
            if(fstat(p->fd,&held)||lstat(p->path,&named))return -1;
            if(cache_same(&p->identity,&held)&&cache_same(&held,&named)&&cache_regular(&held)){
                p->hits++;return 0;
            }
        }
        p->invalidations++;if(cache_drop(p))return BW_IDLE_CACHE_RELEASE_UNKNOWN;
    }
    p->misses++;int fd=-1;struct stat stable;unsigned uncertain=0;
    if(log_idle_owned(r,&fd,&stable,&uncertain,NULL,NULL)){
        if(uncertain){p->release_unknown=1;p->poisoned=1;}
        return p->release_unknown?BW_IDLE_CACHE_RELEASE_UNKNOWN:-1;
    }
    /* The parsed anchor transfers only after stream close + fresh full
     * metadata/current checks. No reopen can manufacture a cache certificate. */
    p->fd=fd;p->identity=stable;p->valid=1;
    if(cache_guard(p,r))return -1;
    return 0;
}
int bw_src_idle_evaluate_cached(const struct bw_startup_idle_request *r,const struct bw_src_idle_ops *ops,
    struct bw_idle_log_cache *p,const char operation[33]){
    if(!p||!pthread_equal(p->owner,pthread_self()))return -1;
    if(p->in_call){p->poisoned=1;return -1;}
    if(!cache_nonce(operation)||memcmp(operation,p->operation,33)){p->poisoned=1;cache_drop(p);return p->release_unknown?-2:-1;}
    p->in_call=1;p->evaluations++;struct identity *a=NULL,*b=NULL;size_t an=0,bn=0;int result=-1;
    if(!r||!ops||!ops->firmware||!ops->query||cache_guard(p,r))goto done;
    struct bw_startup_idle_request bounded=*r;
    if(bounded.deadline>p->deadline)bounded.deadline=p->deadline;
    if(current(&bounded))goto done;
    /* Same process/current ordering as the uncached public entry. Only the
     * two log boundaries may consume an unchanged operation-local proof. */
    if(snapshot(&bounded,ops,&a,&an,NULL)||cache_log_idle(&bounded,p)||snapshot(&bounded,ops,&b,&bn,NULL)||an!=bn||
       memcmp(a,b,an*sizeof *a)||cache_log_idle(&bounded,p)||current(&bounded)||cache_guard(p,&bounded))goto done;
    result=0;
done:
    free(a);free(b);
    if(result||p->poisoned||p->release_unknown){result=-1;cache_drop(p);}
    p->in_call=0;return p->release_unknown?BW_IDLE_CACHE_RELEASE_UNKNOWN:result;
}
int bw_idle_log_cache_stats(const struct bw_idle_log_cache *p,struct bw_idle_log_cache_stats *out){
    if(!p||!out||!pthread_equal(p->owner,pthread_self())||p->in_call)return -1;
    *out=(struct bw_idle_log_cache_stats){p->evaluations,p->hits,p->misses,p->invalidations,p->fd>=0,p->poisoned,p->release_unknown};return 0;
}
int bw_idle_log_cache_close(struct bw_idle_log_cache **owner,struct bw_idle_log_cache_release *report){
    if(!owner||!*owner||!report)return -1;struct bw_idle_log_cache *p=*owner;
    if(!pthread_equal(p->owner,pthread_self())||p->in_call)return -1;
    *report=(struct bw_idle_log_cache_release){p->fd>=0,p->release_unknown};
    cache_drop(p);report->release_unknown=p->release_unknown;*owner=NULL;free(p);
    return report->release_unknown?BW_IDLE_CACHE_RELEASE_UNKNOWN:0;
}

/* Explicit external adoption only. Legacy idle/cache entry points above still
 * reject every eboot and every active log row. No generic allowed-PID switch. */
int bw_src_adopt_evaluate(const struct bw_startup_idle_request *r,const struct bw_src_idle_ops *ops,const struct bw_stop_identity *game,const char title[10]){
    if(!r||!ops||!ops->firmware||!ops->query||!game||!title||strnlen(title,10)!=9||memcmp(title,"PPSA",4)||
       strncmp(game->role,"eboot",5)||bw_stop_identity_match(game,game)!=1||current(r))return -1;
    for(unsigned i=4;i<9;i++)if(title[i]<'0'||title[i]>'9')return -1;
    struct identity *a=NULL,*b=NULL;size_t an=0,bn=0;int result=-1;
    if(snapshot(r,ops,&a,&an,game)||log_idle_owned(r,NULL,NULL,NULL,game,title)||
       snapshot(r,ops,&b,&bn,game)||an!=bn||memcmp(a,b,an*sizeof *a)||
       log_idle_owned(r,NULL,NULL,NULL,game,title)||current(r))goto done;
    result=0;
done:free(a);free(b);return result;
}
