#ifndef BW_PRELOAD_LAUNCH_H
#define BW_PRELOAD_LAUNCH_H
#include "preload_handoff.h"
#include <stdint.h>

/* This controller is entered only AFTER the normal current-PC/local/terminal
 * preflight. It adds no Ready authority and never replaces that verification.
 * One joined normal Play worker owns this object at its stable address. */
struct bw_preload_launch_ops {
    void *opaque;
    double (*now)(void *);
    int (*pause)(void *,unsigned milliseconds);
    int (*current)(void *);
    /* Publish or freshly verify the exact bundled module and all qualified
     * companions. 0=verified, -1=refused without mutation, -2=uncertain/partial.
     * No existing unrelated library or backport may be overwritten. */
    int (*bundle)(void *,const char *root);
    /* One normal Sony launch. 1=accepted, 0=definitely not accepted,
     * -1=unknown. Unknown never authorizes releasing a handoff. */
    int (*launch)(void *);
    /* >1 is a PID already bound to exact title/app/process birth by the
     * normal runtime; 0=not observed yet; -1=unknown. */
    int32_t (*discover)(void *);
    /* Actual SDK POSIX UID, bounded by matching bound generation/title/app
     * checks and two matching queries. No Sony user ID or guessed mapping. */
    int (*consumer)(void *,int32_t,uid_t *);
    /* 1=exact captured process alive, 0=authoritative exact-PID absence,
     * -1=unknown. Neither log-only exit nor title mismatch is absence. */
    int (*live)(void *,int32_t);
    /* Zero ONLY after independently checked exact exit (or certain no-launch)
     * AND normal idle/unmount control checks. Must ignore cancellation of the
     * old Play lease; release must still be possible after requested Close. */
    int (*exited)(void *);
    void (*event)(void *,const char *phase,const char *message,int32_t pid);
};
struct bw_preload_launch_request {
    const char *root;
    const struct bw_reader_selection *selection;
    /* Consumed and cleared before the Sony call, including failed publish. */
    struct bw_share_config *connection;
    struct bw_preload_launch_ops ops;
};
struct bw_preload_launch {
    struct bw_preload_handoff handoff;
    unsigned busy,finished,blocked,bundle_verified,bundle_uncertain;
    unsigned launch_called,assignment_attempted,assignment_confirmed,release_attempted;
    int launch_result,release_result,descriptor_result;
    int32_t pid;
    uid_t uid;
};
/* No game hooks, forced process control, fallback reader or filesystem cleanup
 * beyond the exact retained handoff. Once launch might have been accepted,
 * this does not return until exact independent exit is known. An unknown
 * owner/launch/close result is retained; no mutation is automatically replayed.
 * Return 0 means lifecycle completed/released, NOT menu/gameplay/reader proof. */
int bw_preload_launch_run(const struct bw_preload_launch_request *,struct bw_preload_launch *);
#endif
