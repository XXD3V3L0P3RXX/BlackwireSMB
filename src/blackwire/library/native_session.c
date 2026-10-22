#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "native_session.h"
#include <string.h>

static int ops_valid(const struct bw_session_ops *o){return o&&o->running_app&&o->title&&o->process&&o->wall_us&&o->close;}
static int run_valid(const char *run){
    if(!run||strnlen(run,33)!=32)return 0;unsigned nonzero=0;
    for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",run[i]))return 0;nonzero|=run[i]!='0';}return nonzero!=0;
}
static int title_valid(const char *title){
    if(!title||strnlen(title,10)!=9||memcmp(title,"PPSA",4))return 0;
    for(unsigned i=4;i<9;i++)if(title[i]<'0'||title[i]>'9')return 0;return 1;
}
static int title_matches(const struct bw_session_ops *o,uint32_t app,const char *title){
    char observed[16]={0};return !o->title(o->opaque,app,observed)&&memchr(observed,0,sizeof observed)&&!strcmp(observed,title);
}
int bw_native_session_bind(struct bw_native_session *s,const struct bw_session_ops *o,
    const char *run,const char *title,uint32_t expected_app,int32_t pid){
    if(!s||s->bound||s->phase!=BW_SESSION_EMPTY||!ops_valid(o)||!run_valid(run)||!title_valid(title)||!expected_app||pid<=1)return -1;
    struct bw_stop_identity first,last;uint32_t app=0,again=0;
    if(o->process(o->opaque,pid,&first)!=1||bw_stop_identity_admit(&first,o->wall_us(o->opaque))||first.pid!=pid||
       o->running_app(o->opaque,&app)||app!=expected_app||!title_matches(o,app,title)||
       o->process(o->opaque,pid,&last)!=1||bw_stop_identity_match(&first,&last)!=1||
       o->running_app(o->opaque,&again)||again!=app||!title_matches(o,again,title))return -1;
    memcpy(s->run,run,33);memcpy(s->title,title,10);s->process=last;s->app_id=app;s->bound=1;s->phase=BW_SESSION_OWNED;return 0;
}
int bw_native_session_poll(struct bw_native_session *s,const struct bw_session_ops *o){
    if(!s||!s->bound||!ops_valid(o))return -1;if(s->phase==BW_SESSION_STOPPED)return 0;
    struct bw_stop_identity now;int present=o->process(o->opaque,s->process.pid,&now);
    if(!present){s->phase=BW_SESSION_STOPPED;return 0;}
    if(present!=1||bw_stop_identity_match(&s->process,&now)!=1){s->phase=BW_SESSION_UNCONFIRMED;return -1;}
    s->phase=s->close_called?(s->native_result?BW_SESSION_UNCONFIRMED:BW_SESSION_CLOSING):BW_SESSION_OWNED;return 1;
}
int bw_native_session_close(struct bw_native_session *s,const struct bw_session_ops *o,const char *run){
    if(!s||!s->bound||!ops_valid(o)||!run_valid(run)||strcmp(run,s->run))return BW_SESSION_CLOSE_REFUSED;
    int state=bw_native_session_poll(s,o);if(!state)return BW_SESSION_CLOSE_STOPPED;
    if(state<0||s->close_called)return BW_SESSION_CLOSE_UNKNOWN;
    if(!title_matches(o,s->app_id,s->title)){
        s->phase=BW_SESSION_UNCONFIRMED;return BW_SESSION_CLOSE_UNKNOWN;
    }
    state=bw_native_session_poll(s,o);if(!state)return BW_SESSION_CLOSE_STOPPED;if(state<0)return BW_SESSION_CLOSE_UNKNOWN;
    if(!title_matches(o,s->app_id,s->title)){s->phase=BW_SESSION_UNCONFIRMED;return BW_SESSION_CLOSE_UNKNOWN;}
    /* Mark before invoking: even an error return never permits an automatic
     * second mutation. Only later read-only polling can establish actual exit. */
    s->close_called=1;s->phase=BW_SESSION_CLOSING;s->native_result=o->close(o->opaque,s->app_id);
    if(s->native_result){s->phase=BW_SESSION_UNCONFIRMED;return BW_SESSION_CLOSE_UNKNOWN;}
    return BW_SESSION_CLOSE_REQUESTED;
}
