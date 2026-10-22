/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef READ_CAPTURE_GAME_IDENTITY_H
#define READ_CAPTURE_GAME_IDENTITY_H
#include <stddef.h>
#include <stdint.h>
#include "diagnostics/game_stop_identity.h"

enum rc_game_identity_result {
    RC_GAME_UNKNOWN = -1,
    RC_GAME_ABSENT = 0,
    RC_GAME_QUALIFIED = 1
};
struct rc_game_selection {
    int32_t pid;
    uint32_t app_id;
    char title[10];                 /* Exactly PPSA + five digits + NUL. */
};
struct rc_game_identity {
    struct bw_stop_identity process;
    uint32_t firmware, app_id;
    char title[10];
    int64_t admitted_wall_us;        /* Preserved by current(); never renewed. */
};

/* Read-only test/integration seam. process() returns 1/present, 0/known absent,
 * -1/unknown; only a present result supplies raw public sysctl bytes. app_info
 * copies the source-established uint32 app ID and bounded 14-byte title field.
 * app_title receives a 128-byte buffer, matching native_session_ps5.c. Other
 * query callbacks return 0/success or nonzero/unknown. No callbacks may reenter,
 * modify inputs/ops/output, longjmp, or write beyond their supplied buffers.
 * They are trusted C functions; this cannot contain hostile pointers/callbacks.
 * Native API calls have no hard timeout. No callback performs a mutation. */
struct rc_game_identity_ops {
    void *opaque;
    uint32_t (*firmware)(void *);
    int32_t (*self_pid)(void *);
    int (*process)(void *, int32_t, void *, size_t, size_t *);
    int (*app_info)(void *, int32_t, uint32_t *, char[14]);
    int (*running_app)(void *, uint32_t *);
    int (*app_title)(void *, uint32_t, char *, size_t);
    int64_t (*wall_us)(void *);
};

/* Native read-only adapters; no PID discovery/default and no retries. Initial
 * admission requires age <=60s on the unchanged FW0x13000040/1096-byte profile.
 * current() requires an immutable prior qualified identity, keeps its original
 * birth/admission time, and deliberately applies no subsequent age limit.
 * Both bracket two Sony identity captures with two public PID observations.
 * ABSENT means two exact-PID absence observations, never a query/parse failure.
 * Only QUALIFIED publishes output; other results clear it. In-place current is
 * supported. These observations are not an atomic process lock, attach consent,
 * full-game/root/file-hash qualification, or debugger/credential authority. */
int rc_game_identity_admit(const struct rc_game_selection *, struct rc_game_identity *);
int rc_game_identity_current(const struct rc_game_identity *, struct rc_game_identity *);
int rc_game_identity_admit_with_ops(const struct rc_game_identity_ops *,
                                  const struct rc_game_selection *, struct rc_game_identity *);
int rc_game_identity_current_with_ops(const struct rc_game_identity_ops *,
                                    const struct rc_game_identity *, struct rc_game_identity *);
#endif
