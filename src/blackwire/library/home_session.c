/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "home_session.h"
#include <string.h>
#include <float.h>
#include <time.h>
static double now(void){struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)?-1:(double)t.tv_sec+(double)t.tv_nsec/1e9;}
static int lease(struct bw_home_session *s){double t=now();return !s||!s->current||t<0||!(s->deadline>t&&s->deadline<=DBL_MAX)||s->current(s->opaque)?-1:0;}
int bw_home_session_bind(struct bw_home_session *s,const struct bw_stop_identity *birth,const struct bw_session_ops *ops,const char run[33],const char title[10],uint32_t app){
    if(!s||!birth||!ops||!s->app_current||!s->consumer_uid||!s->idle||lease(s)||s->game.bound)return -1;
    s->process_ops=ops;
    if(bw_native_session_bind(&s->game,ops,run,title,app,birth->pid)||bw_stop_identity_match(birth,&s->game.process)!=1||bw_home_session_current(s))return -1;
    return 0;
}
int bw_home_session_current(void *opaque){
    struct bw_home_session *s=opaque;
    if(lease(s)||!s->game.bound||!s->process_ops||bw_native_session_poll(&s->game,s->process_ops)!=1||s->app_current(s->opaque,&s->game))return -1;
    if(s->assigning||s->publication_complete){uid_t uid=(uid_t)-1;if(s->consumer_uid(s->opaque,s->game.process.pid,&uid)||uid!=s->assigned_uid)return -1;}
    return bw_native_session_poll(&s->game,s->process_ops)!=1||lease(s)?-1:0;
}
int bw_home_session_publish(struct bw_home_session *s,const char *root,const struct bw_reader_selection *selection,const struct bw_share_config *config){
    if(!s||s->publication_attempted||!selection||strcmp(selection->run,s->game.run)||strcmp(selection->title,s->game.title)||bw_home_session_current(s))return -1;
    s->publication_attempted=1;
    struct bw_preload_handoff_request request={root,selection,config,bw_home_session_current,s,s->deadline};
    int result=bw_preload_handoff_publish_retained(&request,&s->private_inputs);if(result)return result;
    uid_t first=(uid_t)-1,second=(uid_t)-1;
    if(bw_home_session_current(s)||s->consumer_uid(s->opaque,s->game.process.pid,&first)||first==(uid_t)-1||bw_home_session_current(s)||
       s->consumer_uid(s->opaque,s->game.process.pid,&second)||second!=first||bw_home_session_current(s))return -1;
    s->assigned_uid=first;s->assigning=1;
    result=bw_preload_handoff_assign_consumer(&s->private_inputs,first,bw_home_session_current,s,s->deadline);
    s->assigning=0;
    if(!result)s->publication_complete=1;return result;
}
int bw_home_session_close(struct bw_home_session *s,const char run[33]){
    if(!s||!s->game.bound||!s->process_ops||!run||strcmp(run,s->game.run)||s->app_current(s->opaque,&s->game))return BW_SESSION_CLOSE_REFUSED;
    return bw_native_session_close(&s->game,s->process_ops,run);
}
int bw_home_session_exited(void *opaque){
    struct bw_home_session *s=opaque;struct bw_stop_identity observed;
    if(!s||!s->game.bound||!s->process_ops||!s->idle)return -1;
    if(s->process_ops->process(s->process_ops->opaque,s->game.process.pid,&observed)!=0||
       s->process_ops->process(s->process_ops->opaque,s->game.process.pid,&observed)!=0||s->idle(s->opaque)||
       s->process_ops->process(s->process_ops->opaque,s->game.process.pid,&observed)!=0)return -1;
    return 0;
}
int bw_home_session_release(struct bw_home_session *s,double deadline){
    if(!s||s->cleanup_attempted||bw_home_session_exited(s))return -1;
    s->cleanup_attempted=1;
    if(!s->private_inputs.initialized)return 0;
    /* An exclusive-publication refusal before either create owns no pathname
     * and the handoff has already discharged its empty consumer reservation.
     * Preserve those preexisting files without converting a definite refusal
     * into a global unknown release. An ambiguous directory close remains -2. */
    if(!s->private_inputs.share.created&&!s->private_inputs.selection.created&&
       s->private_inputs.descriptors_closed&&s->private_inputs.last_status==BW_PRELOAD_HANDOFF_REFUSED)return 0;
    int result=bw_preload_handoff_release(&s->private_inputs,bw_home_session_exited,s,deadline);
    if(result){(void)bw_preload_handoff_close_descriptors(&s->private_inputs,bw_home_session_exited,s,deadline);return -1;}
    return 0;
}
