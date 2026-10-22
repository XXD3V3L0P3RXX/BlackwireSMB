/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "registration_observer.h"
#include "native_document.h"
#include "native_setup.h"
#include "startup_runtime_idle.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct bd_registration_observer {
    struct bw_native_backend *backend;struct bd_registration_observer_ops ops;
    uint64_t last,next;char retained[33];unsigned reported;
};
static int ro_nonce(const char *p){if(!p||strnlen(p,33)!=32)return 0;unsigned any=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",p[i]))return 0;any|=p[i]!='0';}return any!=0;}
static void ro_clear(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
static int ro_now(struct bd_registration_observer *r,uint64_t *out){
    if(r->ops.now_ms)return r->ops.now_ms(r->ops.opaque,out);struct timespec t;
    if(clock_gettime(CLOCK_MONOTONIC,&t)||t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000L||(uint64_t)t.tv_sec>(UINT64_MAX-999u)/1000u)return -1;
    *out=(uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;return 0;
}
static int ro_busy(void *opaque){struct bd_registration_observer *r=opaque;return r->ops.busy?r->ops.busy(r->ops.opaque):bw_native_backend_busy(r->backend);}
static int ro_idle(struct bd_registration_observer *r){
    if(r->ops.idle)return r->ops.idle(r->ops.opaque);struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
    struct bw_startup_idle_request idle={.deadline=(double)t.tv_sec+(double)t.tv_nsec/1e9+2.0,.current=ro_busy,.opaque=r};
    return bw_native_setup_idle(NULL)||bw_startup_idle_ps5(&idle)?-1:0;
}
struct ro_token {char token[33];unsigned seen;};
static int ro_setup_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n){struct ro_token *s=opaque;if(kn!=5||memcmp(key,"token",5))return 0;if(s->seen++||bw_document_string(raw,n,s->token,sizeof s->token)||!ro_nonce(s->token))return -1;return 0;}
struct ro_state {struct ro_token token;unsigned seen;};
static int ro_state_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n){struct ro_state *s=opaque;if(kn!=5||memcmp(key,"setup",5))return 0;if(s->seen++||bw_document_fields(raw,n,ro_setup_field,&s->token)||s->token.seen!=1)return -1;return 0;}
static int ro_release_refused(struct bd_registration_observer *r){
    if(!r->reported){const char *text="Source-only registration observer (hardware unverified): reservation release is unconfirmed; ownership retained.";
        r->reported=1;if(r->ops.report)r->ops.report(r->ops.opaque,text);else fprintf(stderr,"[Blackwire Dashboard] %s\n",text);}
    return -1;
}
static int ro_release(struct bd_registration_observer *r){
    int rc=r->ops.release?r->ops.release(r->ops.opaque,r->retained):bw_native_backend_external_release_idle(r->backend,r->retained);
    if(!rc){ro_clear(r->retained,sizeof r->retained);r->reported=0;return 0;}
    return ro_release_refused(r);
}
static void ro_schedule_after_completion(struct bd_registration_observer *r){
    uint64_t at;
    if(ro_now(r,&at)||at<r->last){r->next=UINT64_MAX;return;}
    r->last=at;r->next=at>UINT64_MAX-5000u?UINT64_MAX:at+5000u;
}
struct bd_registration_observer *bd_registration_observer_create(struct bw_native_backend *backend,const struct bd_registration_observer_ops *ops){
    if(!backend&&(!ops||!ops->busy||!ops->idle||!ops->read_state||!ops->nonce||!ops->reserve||!ops->observe||!ops->release))return NULL;
    struct bd_registration_observer *r=calloc(1,sizeof *r);if(!r)return NULL;r->backend=backend;if(ops)r->ops=*ops;return r;
}
int bd_registration_observer_tick(struct bd_registration_observer *r){
    uint64_t now;if(!r||ro_now(r,&now)||now<r->last)return -1;r->last=now;if(now<r->next)return 1;
    r->next=now>UINT64_MAX-5000u?UINT64_MAX:now+5000u;
    if(r->retained[0]){int rc=ro_release(r);ro_schedule_after_completion(r);return rc;}
    if(ro_busy(r)||ro_idle(r)||ro_busy(r))return 1;
    struct bw_native_response reply={0};struct ro_state state={0};char nonce[33]={0};int result=-1;
    int read=r->ops.read_state?r->ops.read_state(r->ops.opaque,&reply):bw_native_backend_handle(r->backend,"GET","/api/state",NULL,0,&reply);
    if(read||reply.status!=200||!reply.bytes||!reply.size||reply.size>BW_NATIVE_DOCUMENT_LIMIT||bw_document_fields(reply.bytes,reply.size,ro_state_field,&state)||state.seen!=1||state.token.seen!=1||ro_busy(r)||ro_idle(r)||ro_busy(r))goto done;
    if((r->ops.nonce?r->ops.nonce(r->ops.opaque,nonce):bw_native_setup_nonce(nonce))||!ro_nonce(nonce))goto done;
    if(r->ops.reserve?r->ops.reserve(r->ops.opaque,nonce,state.token.token):bw_native_backend_external_reserve(r->backend,nonce,state.token.token))goto done;
    memcpy(r->retained,nonce,33);ro_clear(state.token.token,sizeof state.token.token);
    struct bw_native_cleanup_observation observation={0};
    if(!r->ops.reserve&&!r->ops.observe&&!r->ops.release){
        unsigned released=0;
        int observed=bw_native_backend_registration_observe_idle(r->backend,nonce,&observation,&released);
        if(released){ro_clear(r->retained,sizeof r->retained);r->reported=0;result=observed;}
        else result=ro_release_refused(r);
    }else{
        int observed=r->ops.observe?r->ops.observe(r->ops.opaque,nonce,&observation):bw_native_backend_registration_observe(r->backend,nonce,&observation);
        result=ro_release(r)?-1:observed;
    }
done:ro_clear(&state,sizeof state);ro_clear(nonce,sizeof nonce);bw_native_response_clear(&reply);ro_schedule_after_completion(r);return result;
}
int bd_registration_observer_destroy(struct bd_registration_observer *r){if(!r)return -1;if(r->retained[0])return 1;free(r);return 0;}
