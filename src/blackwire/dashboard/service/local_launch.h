/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BLACKWIRE_DASHBOARD_LOCAL_LAUNCH_H
#define BLACKWIRE_DASHBOARD_LOCAL_LAUNCH_H
#include "dashboard_model.h"
#include "game_identity.h"
#include "reader_user.h"

/* Exact retained game-control/native-reader ABI. No credentials or hooks. */
struct bd_local_launch_context {uint32_t size,user,opts;uint64_t crash;uint32_t check;};
struct bd_local_platform {
    void *opaque;
    int (*foreground_user)(void *,uint32_t *,struct bw_reader_user_observation *);
    int (*launch_app)(void *,const char *,char **,struct bd_local_launch_context *);
    int (*admit)(void *,const struct rc_game_selection *,struct rc_game_identity *);
    int (*current)(void *,const struct rc_game_identity *,struct rc_game_identity *);
    int (*now_ms)(void *,uint64_t *);
};
struct bd_local_launch_config {
    /* Trusted resident configuration, copied at initialization. NULL paths
     * select /user/app, native app.db and the existing ShadowMount log. Never
     * fill these fields from an HTTP/action request. */
    const char *apps_root,*database,*game_log;
    /* Optional trusted fault-test seams. NULL functions use actual Sony,
     * unchanged reader_user and unchanged rc_game_identity adapters. */
    struct bd_local_platform platform;
};
struct bd_local_launch_status {
    char nonce[33],title_id[10],reason[128];
    int launch_result;
    unsigned attempted,accepted,identity_admitted,exact_exit,active;
    struct bw_reader_user_observation user;
    struct rc_game_identity identity;
    uint64_t log_base,log_used;
};
struct bd_local_launcher;

/* Caller serializes operations with the resident service/reservation lock.
 * No worker, socket, file writes, source repair, close API or force is added.
 * Borrowed platform opaque state must outlive this owner. */
struct bd_local_launcher *bd_local_launcher_create(const struct bd_local_launch_config *);
int bd_local_launcher_destroy(struct bd_local_launcher *); /*1 retains active/unknown.*/
int bd_local_launch_title_supported(const char *); /*PPSA only; CUSA not qualified.*/
int bd_local_launch_item_supported(const struct bd_item *); /*Pure family/kind/bounded metadata shape.*/
/* Read-only exact current source fingerprint/ID qualification. It can be used
 * to withhold CAN_PLAY when this adapter cannot qualify a catalog entry. This
 * is metadata/source binding, not a whole-game hash/completeness certificate. */
int bd_local_launch_capable(struct bd_local_launcher *,const struct bd_item *);

/* Direct service callback signatures. Single Sony call at most per nonce.
 * 1=known Sony acceptance,0=definite prelaunch/API refusal,-1=unknown retained.
 * Repeating a consumed nonce never launches again. A new nonce is refused
 * while prior lifecycle ownership remains active or unknown. This owner keeps
 * 4096 consumed nonces; when full, further launches are refused, not evicted. */
int bd_local_launch(void *,const struct bd_item *,const char nonce[33]);
/*1=exact current PID/birth+source,0=verified exact generation exit,-1=unknown.
 * No PID discovery is NEVER exit. Pending discovery uses only appended,
 * anchored SMP records for <=30s; no title-only/process-scan fallback. Once
 * admitted, current retains its original birth with no age renewal. */
int bd_local_state(void *,const char nonce[33]);
int bd_local_launcher_snapshot(const struct bd_local_launcher *,struct bd_local_launch_status *);
#endif
