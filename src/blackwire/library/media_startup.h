/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_MEDIA_STARTUP_H
#define BW_MEDIA_STARTUP_H
#include <float.h>

#define BW_MEDIA_STARTUP_SECONDS 90.0
#define BW_MEDIA_STARTUP_POLL_MS 250u

/* Preflight only. attempt returns zero with the exact reservation retained,
 * one only after a busy observation left no reservation, -2 when release is
 * unconfirmed, and -1 for a terminal refusal. No assets or SDK API are part of
 * this interface, so only read-only preflight can be retried. */
struct bw_media_startup_ops {
    int (*attempt)(void *);
    double (*now)(void *);
    int (*wait)(void *,unsigned);
    void *opaque;
};
static inline int bw_media_startup_acquire(const struct bw_media_startup_ops *ops,
                                         double deadline,const char **reason){
    if(reason)*reason="startup_preflight_unavailable";
    if(!ops||!ops->attempt||!ops->now||!ops->wait||!reason||!(deadline>0&&deadline<=DBL_MAX))return -1;
    double last=ops->now(ops->opaque);
    if(!(last>=0&&last<=DBL_MAX)){*reason="startup_clock_unavailable";return -1;}
    /* The observation cap also bounds a faulty/nonadvancing clock. */
    for(unsigned polls=0;polls<361u;polls++){
        double at=ops->now(ops->opaque);
        if(!(at>=last&&at<=DBL_MAX)){*reason="startup_clock_changed";return -1;}
        if(at>=deadline){*reason="startup_idle_wait_expired";return -1;}
        last=at;int accepted=ops->attempt(ops->opaque);
        if(!accepted){
            at=ops->now(ops->opaque);
            if(!(at>=last&&at<=DBL_MAX)){*reason="startup_clock_changed";return -1;}
            if(at>=deadline){*reason="startup_idle_wait_expired";return -1;}
            *reason="startup_idle_confirmed";return 0;
        }
        if(accepted!=1){*reason=accepted==-2?"startup_reservation_release_unconfirmed":"startup_preflight_refused";return accepted==-2?-2:-1;}
        at=ops->now(ops->opaque);
        if(!(at>=last&&at<=DBL_MAX)){*reason="startup_clock_changed";return -1;}
        if(at>=deadline){*reason="startup_idle_wait_expired";return -1;}
        last=at;
        if(ops->wait(ops->opaque,BW_MEDIA_STARTUP_POLL_MS)){*reason="startup_idle_wait_failed";return -1;}
    }
    *reason="startup_idle_wait_expired";return -1;
}
/* Caller serializes this flag with the resident mutex. Mark before launch so
 * even thread creation failure cannot silently schedule a second job. */
static inline int bw_media_startup_once(unsigned *requested,int (*launch)(void *),void *opaque){
    if(!requested||!launch)return -1;
    if(*requested)return 1;
    *requested=1;return launch(opaque)?-1:0;
}
#endif
