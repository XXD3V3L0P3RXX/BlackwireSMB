#ifndef BW_NATIVE_SESSION_H
#define BW_NATIVE_SESSION_H
#include "../streamreader/diagnostics/game_stop_identity.h"

enum bw_session_phase {BW_SESSION_EMPTY,BW_SESSION_OWNED,BW_SESSION_CLOSING,BW_SESSION_STOPPED,BW_SESSION_UNCONFIRMED};
enum bw_session_close_result {BW_SESSION_CLOSE_REQUESTED=0,BW_SESSION_CLOSE_STOPPED=1,BW_SESSION_CLOSE_REFUSED=-1,BW_SESSION_CLOSE_UNKNOWN=-2};
struct bw_session_ops {
    void *opaque;
    int (*running_app)(void *,uint32_t *);
    int (*title)(void *,uint32_t,char[16]);
    /* 1=exact PID record, 0=authoritative empty exact-PID result, -1=unknown. */
    int (*process)(void *,int32_t,struct bw_stop_identity *);
    int64_t (*wall_us)(void *);
    int (*close)(void *,uint32_t);
};
struct bw_native_session {
    char run[33],title[10];uint32_t app_id;
    struct bw_stop_identity process;
    enum bw_session_phase phase;
    int bound,close_called,native_result;
};
/* Caller serializes all calls and supplies the app ID observed with the newly
 * launched PID. Bind requires two consistent process and title/app snapshots.
 * No process mutation occurs here. This module does not launch or register. */
int bw_native_session_bind(struct bw_native_session *,const struct bw_session_ops *,
    const char *,const char *,uint32_t,int32_t);
/* 1=the same process generation, 0=verified exact-PID exit, -1=unknown. */
int bw_native_session_poll(struct bw_native_session *,const struct bw_session_ops *);
/* Exact nonce + observed process birth + stored app/title, checked twice.
 * At most one native close call. Its successful return is not proof of exit.
 * Unknown/reused PIDs and changed app identity never authorize a close. */
int bw_native_session_close(struct bw_native_session *,const struct bw_session_ops *,const char *);
/* Native adapter uses read-only public process snapshots, the accepted firmware
 * profile and SystemService/LncUtil. No ptrace, credential or game-memory writes. */
void bw_native_session_ps5_ops(struct bw_session_ops *);
#endif
