/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_HOME_SESSION_H
#define BW_HOME_SESSION_H
#include "native_session.h"
#include "preload_handoff.h"
/* One joined external owner, never a fabricated owned-launch result. The
 * controller serializes access, including Close. No fields survive restart. */
struct bw_home_session {
    struct bw_native_session game;
    struct bw_preload_handoff private_inputs;
    const struct bw_session_ops *process_ops;
    int (*current)(void *); /* cheap captured controller lease */
    int (*app_current)(void *,const struct bw_native_session *);
    int (*consumer_uid)(void *,pid_t,uid_t *);
    int (*idle)(void *); /* independent full idle, only after exact absence */
    void *opaque;
    double deadline;
    uid_t assigned_uid;
    unsigned assigning;
    unsigned publication_attempted,publication_complete,cleanup_attempted;
};
/* Bind once using the already captured claim birth, plus normal Sony/session
 * qualification. current app must additionally link this PID to app/title. */
int bw_home_session_bind(struct bw_home_session *,const struct bw_stop_identity *,
    const struct bw_session_ops *,const char run[33],const char title[10],uint32_t app_id);
int bw_home_session_current(void *);
/* Both names must be absent; retained publication/assignment is one-shot. */
int bw_home_session_publish(struct bw_home_session *,const char *,const struct bw_reader_selection *,const struct bw_share_config *);
/* One exact normal Close at most; never a foreign/system process close. */
int bw_home_session_close(struct bw_home_session *,const char run[33]);
/* Authoritative exact PID absence twice, fresh (never the stopped latch), then
 * full normal idle. PID reuse/unknown retains private inputs and the owner. */
int bw_home_session_exited(void *);
/* Removes only run-private inputs, never committed registration or game files.
 * At most one cleanup attempt. A failed FD close stays unknown, not retried. */
int bw_home_session_release(struct bw_home_session *,double);
#endif
