/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "launch_timing.h"
#ifdef BW_LAUNCH_TIMING
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* The ordinary backend has one joined Play worker. This explicit pthread
 * owner, under a mutex, prevents HTTP/ticker/data threads entering its trace.
 * No native TLS support or implicit callback-thread attribution is assumed. */
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static struct bw_lt_scope *active;
static void (*logger)(const char *);
static const char *const stages[BW_LT_STAGE_COUNT]={
    "selection","owned_registration","registration","registration_local","registration_job",
    "smb_connect","pc_source","source_scan","preflight_local","pc_final_fence",
    "boot_samples","bundle","handoff","final_authority","sony_call","discovery","private_assignment"
};
static const char *const metrics[BW_LT_METRIC_COUNT]={
    "backend_guards","reader_guards","settings_reads","record_pins","local_hashes",
    "local_reads","local_hash_bytes","pc_hash_callbacks","pc_hash_bytes",
    "source_requests","source_entries","source_content_bytes"
};
static int valid(const char *id){
    return id&&strnlen(id,33)==32&&strspn(id,"0123456789abcdef")==32&&strspn(id,"0")!=32;
}
static int native_now(void *opaque,uint64_t *out){
    (void)opaque;struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000L||
       (uint64_t)t.tv_sec>(UINT64_MAX-(uint64_t)t.tv_nsec)/UINT64_C(1000000000))return -1;
    *out=(uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;return 0;
}
static void native_emit(void *opaque,const char *line){
    (void)opaque;void (*emit)(const char *)=NULL;
    if(!pthread_mutex_lock(&lock)){emit=logger;pthread_mutex_unlock(&lock);}if(emit)emit(line);
}
static struct bw_lt_ops native_ops(void){return(struct bw_lt_ops){NULL,native_now,native_emit};}
static struct bw_lt_scope *owned(void){return active&&pthread_equal(active->owner,pthread_self())?active:NULL;}
static int stamp(struct bw_lt_scope *s,uint64_t *out){
    if(!s->clock_valid||s->ops.now(s->ops.opaque,out)||*out<s->last){s->clock_valid=0;return -1;}
    s->last=*out;return 0;
}
static void add(struct bw_lt_scope *s,uint64_t *value,uint64_t amount){
    if(amount>UINT64_MAX-*value){*value=UINT64_MAX;s->overflow=1;}else *value+=amount;
}
void bw_lt_set_logger(void (*emit)(const char *)){
    int saved=errno;if(!pthread_mutex_lock(&lock)){logger=emit;pthread_mutex_unlock(&lock);}errno=saved;
}
void bw_lt_enter(struct bw_lt_scope *s,const char *run,const char *operation,const struct bw_lt_ops *ops){
    int saved=errno;if(!s||pthread_mutex_lock(&lock))goto done;
    if(active==s){pthread_mutex_unlock(&lock);goto done;}
    memset(s,0,sizeof *s);
    if(active||!valid(run)||(operation&&*operation&&!valid(operation))){pthread_mutex_unlock(&lock);goto done;}
    s->ops=ops?*ops:native_ops();if(!s->ops.now||!s->ops.emit){pthread_mutex_unlock(&lock);goto done;}
    memcpy(s->run,run,33);if(operation&&*operation)memcpy(s->operation,operation,33);
    s->owner=pthread_self();s->clock_valid=!s->ops.now(s->ops.opaque,&s->start);s->last=s->start;
    active=s;s->attached=1;pthread_mutex_unlock(&lock);
done:errno=saved;
}
void bw_lt_span_begin(enum bw_lt_stage id){
    int saved=errno;uint64_t at=0;if(pthread_mutex_lock(&lock))goto done;
    struct bw_lt_scope *s=owned();if(s&&!s->frozen&&id>=0&&id<BW_LT_STAGE_COUNT){
        struct bw_lt_span *span=&s->spans[id];if(span->active)s->clock_valid=0;
        else {span->active=1;if(!stamp(s,&at))span->start=at;}
    }pthread_mutex_unlock(&lock);
done:errno=saved;
}
void bw_lt_span_end(enum bw_lt_stage id,int result){
    int saved=errno;uint64_t at=0;if(pthread_mutex_lock(&lock))goto done;
    struct bw_lt_scope *s=owned();if(s&&!s->frozen&&id>=0&&id<BW_LT_STAGE_COUNT){
        struct bw_lt_span *span=&s->spans[id];if(!span->active)s->clock_valid=0;
        else {span->active=0;span->result=result;add(s,&span->calls,1);if(!stamp(s,&at))add(s,&span->total,at-span->start);}
    }pthread_mutex_unlock(&lock);
done:errno=saved;
}
void bw_lt_count(enum bw_lt_metric id,uint64_t amount){
    int saved=errno;if(!pthread_mutex_lock(&lock)){
        struct bw_lt_scope *s=owned();if(s&&!s->frozen&&id>=0&&id<BW_LT_METRIC_COUNT)add(s,&s->metrics[id],amount);
        pthread_mutex_unlock(&lock);
    }errno=saved;
}
static void report(struct bw_lt_scope *s,enum bw_lt_report kind,int result){
    char line[384];const char *operation=*s->operation?s->operation:"none";
    const char *name=kind==BW_LT_SONY_RETURN?"sony-return":kind==BW_LT_PROCESS_BOUND?"process-bound":"worker-return";
    snprintf(line,sizeof line,"launch-timing context=play-worker run=%s operation=%s event=%s result=%d resultAvailable=%u clockValid=%u counterOverflow=%u startupElapsedNs=%llu frozen=%u",
        s->run,operation,name,result,kind==BW_LT_WORKER_RETURN,s->clock_valid,s->overflow,(unsigned long long)(s->clock_valid?s->frozen_at-s->start:0),s->frozen);
    s->ops.emit(s->ops.opaque,line);
    /* A later worker return includes the raw outcome, not gameplay time in
     * startup spans. Earlier process-bound output already owns their snapshot. */
    if(kind==BW_LT_WORKER_RETURN&&s->frozen)return;
    for(unsigned i=0;i<BW_LT_STAGE_COUNT;i++)if(s->spans[i].calls||s->spans[i].active){
        const struct bw_lt_span *span=&s->spans[i];
        snprintf(line,sizeof line,"launch-timing context=play-worker run=%s event=%s span=%s calls=%llu totalNs=%llu result=%d complete=%u clockValid=%u",
            s->run,name,stages[i],(unsigned long long)span->calls,(unsigned long long)(s->clock_valid?span->total:0),span->result,!span->active,s->clock_valid);
        s->ops.emit(s->ops.opaque,line);
    }
    for(unsigned i=0;i<BW_LT_METRIC_COUNT;i++)if(s->metrics[i]){
        snprintf(line,sizeof line,"launch-timing context=play-worker run=%s event=%s metric=%s value=%llu counterOverflow=%u",
            s->run,name,metrics[i],(unsigned long long)s->metrics[i],s->overflow);s->ops.emit(s->ops.opaque,line);
    }
}
void bw_lt_report(enum bw_lt_report kind){
    int saved=errno;struct bw_lt_scope snapshot;unsigned emit=0;uint64_t at=0;
    if(pthread_mutex_lock(&lock))goto done;
    struct bw_lt_scope *s=owned();
    if(s&&!s->frozen&&kind>=0&&kind<BW_LT_WORKER_RETURN&&!(s->reports&(1u<<kind))){
        s->reports|=1u<<kind;if(!stamp(s,&at))s->frozen_at=at;
        if(kind==BW_LT_PROCESS_BOUND)s->frozen=1;snapshot=*s;emit=1;
    }pthread_mutex_unlock(&lock);if(emit)report(&snapshot,kind,0);
done:errno=saved;
}
void bw_lt_leave(struct bw_lt_scope *s,int result){
    int saved=errno;struct bw_lt_scope snapshot;unsigned emit=0;uint64_t at=0;
    if(!s||pthread_mutex_lock(&lock))goto done;
    if(owned()==s){
        if(!s->frozen&&!stamp(s,&at))s->frozen_at=at;
        if(!(s->reports&(1u<<BW_LT_WORKER_RETURN))){s->reports|=1u<<BW_LT_WORKER_RETURN;snapshot=*s;emit=1;}
        active=NULL;s->attached=0;
    }pthread_mutex_unlock(&lock);if(emit)report(&snapshot,BW_LT_WORKER_RETURN,result);
done:errno=saved;
}
void bw_lt_request_begin(struct bw_lt_request *q,const char *operation,enum bw_lt_route route,const struct bw_lt_ops *ops){
    int saved=errno;if(!q)goto done;memset(q,0,sizeof *q);
    if(route<BW_LT_START_ROUTE||route>BW_LT_PLAY_ROUTE||(operation&&*operation&&!valid(operation)))goto done;
    q->ops=ops?*ops:native_ops();if(!q->ops.now||!q->ops.emit)goto done;
    if(operation&&*operation)memcpy(q->operation,operation,33);q->route=route;q->initialized=1;
    q->clock_valid=!q->ops.now(q->ops.opaque,&q->start);
done:errno=saved;
}
void bw_lt_request_end(struct bw_lt_request *q,const char *run,int status,int result){
    int saved=errno;uint64_t at=0;char line[384];
    if(!q||!q->initialized||q->finished)goto done;q->finished=1;
    int clock_valid=q->clock_valid&&!q->ops.now(q->ops.opaque,&at)&&at>=q->start;
    int accepted=!result&&status==202&&valid(run);
    snprintf(line,sizeof line,"launch-timing context=backend-request route=%s operation=%s run=%s event=request-return result=%d status=%d accepted=%u clockValid=%u elapsedNs=%llu",
        q->route==BW_LT_START_ROUTE?"start":"play",*q->operation?q->operation:"none",valid(run)?run:"none",result,status,(unsigned)accepted,(unsigned)clock_valid,
        (unsigned long long)(clock_valid?at-q->start:0));q->ops.emit(q->ops.opaque,line);
done:errno=saved;
}
#endif
