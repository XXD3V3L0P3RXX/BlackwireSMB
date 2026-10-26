/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_NATIVE_REGISTRATION_CLEANUP_H
#define BW_NATIVE_REGISTRATION_CLEANUP_H
#include "native_register.h"

#define BW_NRC_LEDGER_LEAF "registration-owners"
#define BW_NRC_PATH_BYTES 2048u
#define BW_NRC_CANDIDATE_LIMIT 256u
/* Trusted fixed resident roots, never request input. Production uses
 * /data/blackwire, /user/app and the ordinary read-only app.db pathname. */
struct bw_nrc_environment {const char *data_root,*apps_root,*database;};
struct bw_nrc_subject {
    char title[10],run[33],source[BW_NRC_PATH_BYTES],content[128];
    char game[75];
    unsigned char prepared_record_sha256[32],selection_sha256[32];
};
struct bw_nrc_ops {
    void *opaque;
    /* Zero only while a shared resident mutation reservation and full native
     * idle/cancel/settings epoch are positively current. No Dashboard/title
     * exemption. No calls back into a lock held by this module. */
    int (*current)(void *);
    /* Fresh complete collector: zero only with NO same-title alternative,
     * full/manual/image/PKG source and no unresolved external operation.
     * A row for this exact owned missing source may remain cached. Unknown or
     * incomplete lists refuse. Called repeatedly; no title-only authority. */
    int (*sources)(void *,const struct bw_nrc_subject *);
    /* One normal SMP /api/v1/games/uninstall request, literal selected title.
     * 0=request accepted,1=definite refusal,-1=unknown. ANY result consumes
     * this ledger lifecycle: persistent pending permits observation only.
     * No filesystem deletion, direct database mutation, force or retry. */
    int (*uninstall)(void *,const char[10]);
};
enum bw_nrc_state {BW_NRC_UNKNOWN=-1,BW_NRC_UNOWNED=0,BW_NRC_INTENT=1,
    BW_NRC_OWNED=2,BW_NRC_BLOCKED=3,BW_NRC_PENDING=4,BW_NRC_ABSENT=5};
struct bw_nrc_result {
    enum bw_nrc_state state;
    unsigned uninstall_attempted,uninstall_accepted,missing_mount_link;
    const char *reason; /* Static nonsecret diagnostic; no copied credentials. */
    /* First failed existing call/predicate. error_snapshot may be zero/stale
     * for a predicate or a library failure; it is never a guessed cause. */
    const char *stage;
    int raw_result,error_snapshot;
    const char *detail; /* Existing collector/control failure, no new query. */
};
struct bw_nrc_ticket;
/* A failed operation could not confirm descriptor release. No descriptor is
 * retried by number; the joined caller must retain its CONTROL reservation. */
#define BW_NRC_RELEASE_UNCONFIRMED (-2)
/* Call immediately before the REAL registration mutation, after existing
 * prepared/source admission. FOLDER or CONTAINER/V1 with policy0 is supported.
 * Require two actual app-dir+DB ABSENT observations and a fresh selection run;
 * preexisting MATCH/PENDING/unknown is never adopted. On success, fsynced
 * private intent exists and ticket owns held anchors. 0=ticket,1=inert/refused,
 * -1=uncertain/error,-2=release unconfirmed during failed-begin cleanup.
 * Failed intents are retained, never blindly resumed. */
int bw_nrc_begin(const struct bw_nrc_environment *,const struct bw_reader_selection *,
    const struct bw_nrc_ops *,struct bw_nrc_ticket **,struct bw_nrc_result *);
/* Call only with that same registration job's successful result/current
 * guard, before launch. Requires registered AND add/scan attempted, exact fresh
 * normal registration verification and captured source/app/param/mount witness.
 * No ownership arises from a matching pathname or a fabricated reload. */
int bw_nrc_finish(struct bw_nrc_ticket *,int registration_status,const struct bw_register_result *,
    struct bw_nrc_result *);
int bw_nrc_ticket_destroy(struct bw_nrc_ticket *); /* Checked closes only; no deletion. */
/* Per owned title+run; root may enumerate bounded .owner names in the private
 * ledger. Never creates layout when ledger/evidence is absent. Missing source
 * is leaf ENOENT under the captured, still-live games parent; EACCES/ENOTDIR,
 * symlinks, changed ancestors and lost whole data root are UNKNOWN/refused.
 * Recheck registered app inode, exact param stat/body/content+SQLite and mount
 * witness. A now-absent mount.lnk is permitted only with all other independent
 * captured witnesses intact and a fresh no-alternative collector result.
 * Persist and read back pending BEFORE the single callback; after pending,
 * even a crash/refusal/lost reply never sends again. Completion needs two
 * independent exact app-dir ENOENT + no title row observations under live
 * /user/app. Public API cache or an HTTP200 never proves removal. */
int bw_nrc_reconcile(const struct bw_nrc_environment *,const char title[10],
    const char run[33],const struct bw_nrc_ops *,struct bw_nrc_result *);
struct bw_nrc_candidate {struct bw_nrc_subject subject;unsigned pending,retiring;};
/* Read-only bounded listing, no collector/uninstall and no layout creation.
 * Committed owner leaves are PPSAxxxxx.<32-lowerhex-run>.owner. Pending/retiring first,
 * then title/run; completed .absent records are omitted. retiring requires the
 * separate present-source observer; the missing-source observer never advances
 * those new lifecycles. Unknown filenames,
 * partial/linked committed records or more than capacity fail the complete
 * listing (count=0). Uncommitted intents grant no candidate or authority.
 * Root schedules at most one candidate per idle tick with its own fair cursor;
 * this list is NOT cleanup admission. Missing whole ledger/reset => count0.
 * -1 is an ordinary incomplete/refused listing; BW_NRC_RELEASE_UNCONFIRMED
 * means an owned file/DIR/anchor close was uncertain. Both clear count/output;
 * the joined caller must latch the latter before allowing any new control. */
int bw_nrc_candidates(const struct bw_nrc_environment *,struct bw_nrc_candidate *,
    size_t capacity,size_t *count);
/* An older committed event may be completed by observation when a freshly
 * verified preparation replaces its receipt at the SAME source/binding.
 * This appends only that old event's checksum-bound .absent marker. It never
 * deletes a journal, adopts a registration or sends a platform request.
 * Pending/retire/uninstall history, foreign origin, unknown app/DB/manual
 * state or lost native-idle CONTROL refuses. Sources must independently prove
 * absent registration/no alternative; manual_present must return exactly0.
 * The current preparation's source/param/receipt witnesses stay held/current
 * through two observations, marker publication and two final observations. */
struct bw_nrc_generation_observer {
    void *opaque;
    int (*current)(void *);
    int (*sources)(void *,const struct bw_nrc_subject *);
    int (*manual_present)(void *,const struct bw_nrc_subject *);
};
int bw_nrc_observe_superseded(const struct bw_nrc_environment *,
    const struct bw_nrc_subject *,const struct bw_reader_selection *,
    const struct bw_nrc_generation_observer *,struct bw_nrc_result *);
/* Synchronous calls are serialized internally. Call without holding the
 * backend/service mutex; callbacks must not reenter this module. Keep callback
 * ownership alive through finish/destroy. Root's collector/transport must own
 * the shared mutation reservation; upstream title-only uninstall has no atomic
 * expected-source parameter against an unrelated external SMP mutator. */
#endif
