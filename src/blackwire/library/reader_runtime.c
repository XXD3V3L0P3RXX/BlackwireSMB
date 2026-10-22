#include "reader_runtime.h"
#include <stdio.h>
#include <string.h>

static int selection_valid(const struct bw_reader_selection *s){
    char text[BW_READER_SELECTION_BYTES+1];size_t size=0;
    return !bw_reader_selection_serialize(s,text,sizeof text,&size)&&size;
}
int bw_reader_runtime_begin(struct bw_reader_runtime *r,const struct bw_native_reader_request *q,const struct bw_session_ops *ops){
    if(!r||!q||!ops||!ops->process||!ops->running_app||!ops->title||!ops->wall_us||!ops->close||!selection_valid(&q->selection)||!q->connection.present)return -1;
    pthread_mutex_lock(&r->lock);int result=-1;
    if(!r->active){r->request=*q;r->ops=*ops;r->session=(struct bw_native_session){0};r->cancelled=r->registration_verified=r->launch_requested=r->launch_accepted=0;r->active=1;result=0;}
    pthread_mutex_unlock(&r->lock);return result;
}
int bw_reader_runtime_current(struct bw_reader_runtime *r){
    pthread_mutex_lock(&r->lock);int active=r->active&&!r->cancelled;int (*current)(void *)=r->request.current;void *opaque=r->request.opaque;pthread_mutex_unlock(&r->lock);
    return !active?-1:current?current(opaque):0;
}
int bw_reader_runtime_data_current(struct bw_reader_runtime *r){
    if(!r)return -1;
    pthread_mutex_lock(&r->lock);int active=r->active&&!r->cancelled;
    int (*current)(void *)=r->request.data_current;void *opaque=r->request.opaque;pthread_mutex_unlock(&r->lock);
    return !active||!current?-1:current(opaque);
}
uint64_t bw_reader_runtime_data_epoch(struct bw_reader_runtime *r){
    if(!r)return 0;
    pthread_mutex_lock(&r->lock);uint64_t epoch=r->active&&!r->cancelled&&r->request.data_current?r->request.data_epoch:0;
    pthread_mutex_unlock(&r->lock);return epoch;
}
int bw_reader_runtime_selection(struct bw_reader_runtime *r,struct bw_reader_selection *s,struct bw_setup_generation *g){
    pthread_mutex_lock(&r->lock);int result=-1;if(r->active){*s=r->request.selection;*g=r->request.connection;result=0;}pthread_mutex_unlock(&r->lock);return result;
}
int bw_reader_runtime_before_launch(struct bw_reader_runtime *r){
    if(bw_reader_runtime_current(r))return -1;
    pthread_mutex_lock(&r->lock);struct bw_native_reader_request q=r->request;pthread_mutex_unlock(&r->lock);
    /* A caller must supply registration verification; a missing callback is
     * never interpreted as an already registered title. */
    if(!q.before_launch||q.before_launch(q.opaque,&q.selection)||bw_reader_runtime_current(r))return -1;
    pthread_mutex_lock(&r->lock);int result=-1;if(r->active&&!r->cancelled&&!r->launch_requested){r->registration_verified=1;result=0;}pthread_mutex_unlock(&r->lock);return result;
}
int bw_reader_runtime_launch_begin(struct bw_reader_runtime *r){
    if(bw_reader_runtime_current(r))return -1;
    pthread_mutex_lock(&r->lock);int result=-1;if(r->active&&!r->cancelled&&r->registration_verified&&!r->launch_requested){r->launch_requested=1;r->launch_accepted=-1;result=0;}pthread_mutex_unlock(&r->lock);return result;
}
void bw_reader_runtime_launch_result(struct bw_reader_runtime *r,int result){
    pthread_mutex_lock(&r->lock);if(r->active&&r->launch_requested)r->launch_accepted=result>=0;pthread_mutex_unlock(&r->lock);
}
int bw_reader_runtime_bind(struct bw_reader_runtime *r,uint32_t app,int32_t pid){
    pthread_mutex_lock(&r->lock);int result=-1;
    if(r->active&&r->launch_requested&&r->launch_accepted==1&&!r->session.bound){
        result=bw_native_session_bind(&r->session,&r->ops,r->request.selection.run,r->request.selection.title,app,pid);
        if(!result&&r->cancelled)(void)bw_native_session_close(&r->session,&r->ops,r->request.selection.run);
    }
    pthread_mutex_unlock(&r->lock);return result;
}
int bw_reader_runtime_poll(struct bw_reader_runtime *r,int32_t pid){
    pthread_mutex_lock(&r->lock);int result=-1;
    if(r->active&&r->session.bound&&r->session.process.pid==pid)result=bw_native_session_poll(&r->session,&r->ops);
    pthread_mutex_unlock(&r->lock);return result;
}
int bw_reader_runtime_close(struct bw_reader_runtime *r,const char *run){
    if(!run||strnlen(run,33)!=32)return BW_SESSION_CLOSE_REFUSED;
    pthread_mutex_lock(&r->lock);int result=BW_SESSION_CLOSE_REFUSED;
    if(r->active&&!strcmp(run,r->request.selection.run)){
        r->cancelled=1;
        result=r->session.bound?bw_native_session_close(&r->session,&r->ops,run):BW_SESSION_CLOSE_REQUESTED;
    }
    pthread_mutex_unlock(&r->lock);return result;
}
void bw_reader_runtime_observe(struct bw_reader_runtime *r,const struct bw_reader_event *event){
    pthread_mutex_lock(&r->lock);void (*observe)(void *,const struct bw_reader_event *)=r->active?r->request.observe:NULL;void *opaque=r->request.opaque;pthread_mutex_unlock(&r->lock);
    if(observe)observe(opaque,event);
}
int bw_reader_runtime_end(struct bw_reader_runtime *r){
    pthread_mutex_lock(&r->lock);int result=-1;
    if(r->active&&((!r->launch_requested||r->launch_accepted==0)||(r->launch_accepted==1&&r->session.bound&&!bw_native_session_poll(&r->session,&r->ops)))){
        memset(&r->request,0,sizeof r->request);memset(&r->session,0,sizeof r->session);memset(&r->ops,0,sizeof r->ops);
        r->active=r->cancelled=r->registration_verified=r->launch_requested=r->launch_accepted=0;result=0;
    }
    pthread_mutex_unlock(&r->lock);return result;
}
