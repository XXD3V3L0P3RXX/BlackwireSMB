#define _POSIX_C_SOURCE 200809L
#include "startup_runtime_control_internal.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
struct src_state {
    struct bw_startup_runtime_request request;
    struct bw_startup_runtime_control *owner;
    char root[128],title[10];
    double call_deadline;
    unsigned admitted,unmount_attempted;
};
static double now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (double)t.tv_sec+(double)t.tv_nsec/1e9;}
static int fail(struct bw_startup_runtime_control *p,const char *error){p->failed=1;p->error=error;return -1;}
static int qualified(const char *root,const char *title){
    static const char prefix[]="/data/blackwire/games/";
    if(!root||!title||strnlen(title,10)!=9||memcmp(title,"PPSA",4)||strnlen(root,128)!=sizeof prefix-1+74||memcmp(root,prefix,sizeof prefix-1))return -1;
    for(unsigned i=4;i<9;i++)if(title[i]<'0'||title[i]>'9')return -1;
    const char *component=root+sizeof prefix-1;if(memcmp(component,title,9)||component[9]!='_')return -1;
    for(unsigned i=10;i<74;i++)if(!strchr("0123456789abcdef",component[i]))return -1;return 0;
}
static int current(void *opaque){
    struct src_state *s=opaque;double at=now();
    return !s||!s->owner||s->owner->failed||at<0||at>=s->request.deadline||at>=s->call_deadline||s->request.current(s->request.opaque)?-1:0;
}
static int idle(struct src_state *s){return current(s)||s->request.idle(s->request.opaque)||current(s)?-1:0;}
int bw_startup_runtime_open(struct bw_startup_runtime_control *p,const struct bw_startup_runtime_request *r){
    if(!p)return -1;if(p->state||p->busy||p->failed)return fail(p,"runtime_new_worker_session_required");
    double at=now();if(!r||!r->current||!r->idle||qualified(r->root,r->title)||at<0||!(r->deadline>at&&r->deadline<=DBL_MAX))return fail(p,"runtime_qualified_worker_required");
    struct src_state *s=calloc(1,sizeof *s);if(!s)return fail(p,"runtime_control_allocation");s->request=*r;s->owner=p;
    strcpy(s->root,r->root);strcpy(s->title,r->title);s->request.root=s->root;s->request.title=s->title;s->call_deadline=r->deadline;p->state=s;
    p->busy=1;int bad=idle(s);p->busy=0;if(bad)return fail(p,"runtime_worker_or_idle_refused");return 0;
}
int bw_startup_runtime_unmapped(void *opaque,const char *root,const char *title){
    struct bw_startup_runtime_control *p=opaque;if(!p)return -1;
    if(p->busy)return fail(p,"runtime_control_reentry");if(!p->state||p->failed)return -1;
    struct src_state *s=p->state;p->busy=1;int result=-1;double at=now();
    if(qualified(root,title)||strcmp(root,s->root)||strcmp(title,s->title)){fail(p,"runtime_selection_changed");goto done;}
    s->call_deadline=at+10;if(s->call_deadline>s->request.deadline)s->call_deadline=s->request.deadline;
    if(at<0||idle(s)){fail(p,"runtime_worker_or_idle_refused");goto done;}
    struct bw_src_connection connection={.port=s->request.port,.current=current,.opaque=s};struct bw_src_reply reply={0};
    if(bw_src_http(&connection,BW_SRC_VERSION,s->root,s->title,&reply)||reply.capabilities!=15||idle(s)){fail(p,"runtime_api_capabilities_refused");goto done;}
    if(bw_src_http(&connection,BW_SRC_SETTINGS,s->root,s->title,&reply)||bw_src_paths_excluded(&reply,1,current,s)||idle(s)){bw_src_reply_clear(&reply);fail(p,"runtime_scan_exclusion_refused");goto done;}bw_src_reply_clear(&reply);
    if(bw_src_http(&connection,BW_SRC_MANUAL,s->root,s->title,&reply)||bw_src_paths_excluded(&reply,0,current,s)||idle(s)){bw_src_reply_clear(&reply);fail(p,"runtime_manual_exclusion_refused");goto done;}bw_src_reply_clear(&reply);
    if(bw_src_http(&connection,BW_SRC_INFO,s->root,s->title,&reply)||idle(s)){fail(p,"runtime_exact_game_info_refused");goto done;}
    if(!s->admitted){
        if(!reply.source_available){fail(p,"runtime_original_source_unavailable");goto done;}
        if(reply.mounted){
            if(s->unmount_attempted||idle(s)){fail(p,"runtime_unmount_not_current");goto done;}
            /* Consume the single mutation allowance before the first byte can
             * be sent. Unknown or lost replies cannot renew it. */
            s->unmount_attempted=1;
            if(bw_src_http(&connection,BW_SRC_UNMOUNT,s->root,s->title,&reply)||idle(s)){fail(p,"runtime_unmount_outcome_unconfirmed");goto done;}
            if(bw_src_http(&connection,BW_SRC_INFO,s->root,s->title,&reply)||idle(s)||!reply.source_available||reply.mounted){fail(p,"runtime_unmount_readback_refused");goto done;}
        }
        if(reply.mounted){fail(p,"runtime_mounted_original_refused");goto done;}
        s->admitted=1;
    }else if(reply.mounted){fail(p,"runtime_mapping_changed");goto done;}
    if(idle(s)){fail(p,"runtime_final_worker_or_idle_refused");goto done;}result=0;
done:
    p->busy=0;return result;
}
int bw_startup_runtime_destroy(struct bw_startup_runtime_control *p){
    if(!p)return -1;if(p->busy)return fail(p,"runtime_control_reentry");
    if(p->state){memset(p->state,0,sizeof(struct src_state));free(p->state);}memset(p,0,sizeof *p);return 0;
}
