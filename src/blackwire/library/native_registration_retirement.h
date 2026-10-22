/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_NATIVE_REGISTRATION_RETIREMENT_H
#define BW_NATIVE_REGISTRATION_RETIREMENT_H
#include "native_registration_cleanup.h"

/* Automatic after-exit registration retirement is separate from explicit
 * Uninstall: it NEVER removes the prepared source, its receipt, or saves.
 * Authority comes only from bw_nrc_begin/finish's real registration event.
 * A preexisting matching title is not adopted by this API. */
struct bw_nrc_retirement_ops {
    struct bw_nrc_ops registration;
    /* Zero only for this exact title/run/selection after its joined reader
     * and all private handoff/I/O ownership were positively released. Unknown
     * process/launch outcomes refuse. registration.current must additionally
     * hold a shared mutation reservation and fresh full normal console idle. */
    int (*released)(void *,const struct bw_nrc_subject *);
    /* Zero only after checking every effective current SMP scan root: this
     * literal canonical source cannot be rediscovered through any of them.
     * Do not alter global roots or auto_remove_missing to make this succeed. */
    int (*outside_scan_roots)(void *,const struct bw_nrc_subject *);
    /* Complete fresh normal manual/list observation: 0=exact path absent,
     * 1=present, -1=unknown. This does not establish registered-title absence. */
    int (*manual_present)(void *,const struct bw_nrc_subject *);
    /* One normal exact-path manual/remove. 0=accepted,1=refused,-1=unknown.
     * Any outcome consumes this attempt. A lost reply can be resolved only
     * by independent manual/list readback, never by resending. */
    int (*manual_remove)(void *,const struct bw_nrc_subject *);
};
struct bw_nrc_retirement_result {
    struct bw_nrc_result registration;
    unsigned manual_remove_attempted,manual_remove_accepted,manual_absent;
};
/* Supported initially only for the exact ordinary FOLDER/V1 owner ledger.
 * The present source/param/receipt and registered app/param/mount/readonly DB
 * witnesses must still match. Missing source uses the separate old reconciler.
 * Durable .pending + .retire precede manual removal; .uninstall precedes the
 * one title uninstall. Pending/uncertain attempts never replay, including after
 * a process restart. Completion requires exact app-directory + SQLite absence
 * twice AND manual-path absence, not an HTTP200 or cache-list disappearance.
 * Normal SMP can refuse a busy operation; this function reports it without
 * force, retry, source deletion, process control or a ShellUI cache guarantee.
 * Call without backend/service locks; callbacks must not reenter this module. */
int bw_nrc_retire_present(const struct bw_nrc_environment *,const char title[10],
    const char run[33],const struct bw_nrc_retirement_ops *,
    struct bw_nrc_retirement_result *);
/* Restart/ticker observation has no mutation callbacks or live-release grant.
 * It cannot create pending/retire/uninstall or advance a later request phase.
 * An existing retiring+pending exact owner can complete only after retained
 * source/receipt and two manual/app-directory/read-only DB absence snapshots.
 * Its only permitted write is the final absent observation marker. */
struct bw_nrc_retirement_observer {
    void *opaque;
    int (*current)(void *);
    int (*sources)(void *,const struct bw_nrc_subject *);
    int (*outside_scan_roots)(void *,const struct bw_nrc_subject *);
    int (*manual_present)(void *,const struct bw_nrc_subject *);
};
int bw_nrc_retirement_observe(const struct bw_nrc_environment *,const char title[10],
    const char run[33],const struct bw_nrc_retirement_observer *,
    struct bw_nrc_retirement_result *);
#endif
