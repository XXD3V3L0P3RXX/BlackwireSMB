#include "register_job.h"
#include <string.h>
static int current(const struct bw_register_ops *o,struct bw_register_result *r,double deadline){
    double t=o->now(o->opaque);int changed=o->current(o->opaque);
    if(changed){r->cause="registration_current_changed";r->message="Registration stopped because the current setup changed. Startup files were kept.";return -1;}
    if(!__builtin_isfinite(t)||t<0){r->cause="registration_clock_invalid";r->message="Registration timing could not be checked safely. Startup files were kept.";return -1;}
    if(t>=deadline){r->cause="registration_deadline_expired";r->message="ShadowMount did not finish confirming this game before the setup timeout. Startup files were kept.";return -1;}
    return 0;
}
static int acceptable(struct bw_register_result *r,enum bw_registration_state state,int publication_wait){
    if(state==BW_REG_CONFLICT){r->cause="registration_conflict";r->message="This title points to another console copy. That copy was preserved.";return -1;}
    if(state==BW_REG_UNKNOWN&&publication_wait)return 0;
    if(state!=BW_REG_ABSENT&&state!=BW_REG_PENDING&&state!=BW_REG_MATCH){r->cause="registration_state_unknown";r->message="The current console registration could not be checked safely.";return -1;}
    return 0;
}
static int manual_presence(const char *root,const struct bw_register_ops *o,struct bw_register_result *r,double deadline,unsigned *present){
    struct bw_sm_reply reply={0};
    r->message="ShadowMount's exact manual source could not be checked safely.";
    if(current(o,r,deadline))return -1;
    r->last_post_result=o->post(o->opaque,BW_SM_MANUAL_LIST,root,&reply);
    if(r->last_post_result){r->cause="registration_manual_observation_failed";return -1;}
    if(current(o,r,deadline))return -1;
    *present=reply.present;return 0;
}
static int reattach_completed(const char *root,const struct bw_register_ops *o,struct bw_register_result *r,double deadline){
    unsigned present=0;
    /* o->state is native_registration_state's independent exact source,
     * staged app/param/mount and read-only DB observation. Recheck immediately
     * around the second list snapshot. A changed current guard aborts. */
    if(!r->add_confirmed||r->reattach_attempted||current(o,r,deadline)||o->state(o->opaque)!=BW_REG_MATCH||
       manual_presence(root,o,r,deadline,&present)||o->state(o->opaque)!=BW_REG_MATCH||current(o,r,deadline))return -1;
    if(present)return 1; /* A fresh read won the race; no mutation is needed. */
    r->reattach_attempted=1;
    struct bw_sm_reply reply={0};int added=o->post(o->opaque,BW_SM_MANUAL_ADD,root,&reply);
    if(!added&&reply.present)r->reattach_confirmed=1;
    /* Every result consumes the sole reattachment. A lost reply can only be
     * resolved by independent exact MATCH/list readback, never a third add. */
    r->message="The completed registration's manual source was reattached; checking that it stays retained.";
    return current(o,r,deadline)?-1:0;
}
int bw_register_job(const char *root,const struct bw_register_ops *o,struct bw_register_result *r){
    if(!r)return -1;memset(r,0,sizeof *r);r->last_state=BW_REG_UNKNOWN;r->message="Registration request is invalid or cancelled.";
    if(!root||root[0]!='/'||strnlen(root,2048)>=2048||!o||!o->current||!o->state||!o->post||!o->now||!o->pause||!o->progress)return -1;
    double start=o->now(o->opaque),deadline=start+BW_REGISTER_WAIT_SECONDS;
    if(!__builtin_isfinite(start)||!__builtin_isfinite(deadline)||start<0){r->cause="registration_clock_invalid";return -1;}
    if(current(o,r,deadline))return -1;
    enum bw_registration_state state=o->state(o->opaque);if(acceptable(r,state,0)||current(o,r,deadline))return -1;
    int publication_wait=0;
    o->progress(o->opaque,"Checking the prepared copy's retained registration in the PS5 library.");struct bw_sm_reply reply={0};
    r->message="ShadowMount's local registration API is unavailable or incompatible.";
    if(current(o,r,deadline))return -1;
    r->last_post_result=o->post(o->opaque,BW_SM_VERSION,root,&reply);r->capabilities=reply.capabilities;
    if(r->last_post_result||(reply.capabilities&7u)!=7u){r->cause="registration_api_incompatible";return -1;}
    r->message="ShadowMount's registered source list could not be checked.";
    unsigned initially_present=0;if(manual_presence(root,o,r,deadline,&initially_present))return -1;
    if(!initially_present){
        if(current(o,r,deadline))return -1;r->add_attempted=1;
        int added=o->post(o->opaque,BW_SM_MANUAL_ADD,root,&reply);
        if(!added&&reply.present)r->add_confirmed=1;
        /* manual/add queues its own asynchronous scan. A complete positive
         * reply permits bounded read-only publication waits. A lost/negative
         * reply never authorizes the changed-state reattachment below. */
        unsigned present=0;if(manual_presence(root,o,r,deadline,&present))return -1;
        if(!present&&!r->add_confirmed){r->message="The initial source add was unconfirmed and its path is absent; no add was repeated.";return -1;}
        /* Our acknowledged add queues an asynchronous scan. Its exact retained
         * source may be visible before all registration files exist. Only this
         * job's confirmed add/readback permits bounded read-only UNKNOWN waits;
         * it never grants MATCH or reattachment authority by itself. */
        publication_wait=r->add_confirmed;
    }
    if(current(o,r,deadline))return -1;state=o->state(o->opaque);if(acceptable(r,state,publication_wait))return -1;
    if(state!=BW_REG_MATCH&&!r->add_attempted){
        r->scan_attempted=1;r->message="The registration scan could not be confirmed; no scan was repeated.";
        /* Only a source that was already listed before this job can need an
         * explicit scan. A lost response never causes another mutation. */
        (void)o->post(o->opaque,BW_SM_SCAN,root,&reply);
    }
    double matched_since=-1,retained_since=-1;
    while(!current(o,r,deadline)){
        state=o->state(o->opaque);if(acceptable(r,state,publication_wait))return -1;
        unsigned present=0;if(manual_presence(root,o,r,deadline,&present))return -1;
        enum bw_registration_state after=o->state(o->opaque);if(acceptable(r,after,publication_wait)||current(o,r,deadline))return -1;
        double checked=o->now(o->opaque);r->observations++;r->last_state=(int)after;r->last_manual=present;
        if(state==BW_REG_MATCH&&after==BW_REG_MATCH){
            if(matched_since<0)matched_since=checked;
            if(!present){
                retained_since=-1;
                if(!r->add_confirmed){r->message="The source path is absent and the initial add was not positively acknowledged; no reattachment was requested.";return -1;}
                if(r->reattach_attempted){r->message="The source was not retained after its single qualified reattachment; no add was repeated.";return -1;}
                if(checked-matched_since>=2){
                    int attached=reattach_completed(root,o,r,deadline);
                    if(attached<0)return -1;
                    /* A second ADD or a concurrent positive list observation
                     * must earn its own fresh 2-second persistence window. */
                    matched_since=retained_since=-1;
                }
            }else{
                if(retained_since<0)retained_since=checked;
                if(checked-retained_since>=2){
                    /* This is a bounded persistence observation, not a scanner
                     * idle transaction: the public API has no such primitive. */
                    unsigned final_present=0;if(manual_presence(root,o,r,deadline,&final_present))return -1;
                    state=o->state(o->opaque);if(acceptable(r,state,publication_wait)||current(o,r,deadline))return -1;
                    if(state==BW_REG_MATCH&&final_present){r->registered=1;r->message="Prepared copy and exact manual source remain registered.";return 0;}
                    if(state!=BW_REG_MATCH)matched_since=-1;
                    retained_since=-1;
                }
            }
        }else{
            matched_since=retained_since=-1;
            if(!present&&!r->add_confirmed){r->message="The unconfirmed registration lost its source path; no add was repeated.";return -1;}
            if(!present&&r->reattach_attempted){r->message="The source remains absent after one reattachment; no add was repeated.";return -1;}
        }
        o->pause(o->opaque,250);
    }
    return -1; /* current() records timeout, cancellation or invalid timing separately. */
}
