/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_NATIVE_REGISTRATION_REMOVED_H
#define BW_NATIVE_REGISTRATION_REMOVED_H
#include "native_registration_cleanup.h"
#include "native_removed_sources.h"

/* Explicit Uninstall already removed an exact prepared generation. This is
 * only an observation of an ORIGINAL committed registration owner; it does
 * not adopt an old registration, repeat Uninstall, or create release proof.
 * The complete durable Removed record must match the owner's original raw
 * FOLDER/V1 source, game, binding and prepared receipt digest. */
struct bw_nrc_removed_observer {
    void *opaque;
    /* Shared mutation reservation and fresh full normal console idle. */
    int (*current)(void *);
    /* Complete independent source observations: no foreign same-title copy,
     * registration or unresolved operation. The exact removed path may remain
     * in an SMP cache. Incomplete or conflicting observations must refuse. */
    int (*sources)(void *,const struct bw_nrc_subject *);
    /* Complete fresh manual/list: zero only for exact path absence. */
    int (*manual_present)(void *,const struct bw_nrc_subject *);
};
/* 0 + ABSENT: original event completed; 0 + UNOWNED: no original owner, no
 * write; -1 + UNKNOWN: observation could not be established. Existing pending,
 * retiring or uninstalling automatic lifecycles refuse this completion.
 * Only a checked 0600 .absent marker may be written. At each of two snapshots,
 * require retained data/games/apps anchors, the exact complete Removed record,
 * source leaf and entire per-game prepared-state leaf ENOENT under live held
 * parents (0755 native-prepared namespace with its known 0644 marker),
 * manual-path absence, app-directory and readonly SQLite absence.
 * Safe to call after restart without any mutation/release callbacks; a lost
 * reply or uncertain marker write never authorizes another normal request.
 * Failed/partial markers remain evidence. Restore must remain unavailable
 * until this observer returns ABSENT for every outstanding same-title owner.
 * No backend/service lock may be held; callbacks must not reenter this module. */
/* Read-only lookup for one exact committed Removed generation whose ORIGINAL
 * owner already has a valid checksum-bound .absent marker. Unlike the general
 * pending-candidate list, completed owners are visible only to this narrow
 * query. No marker, layout, absence authority or platform request is created.
 * Only ops->current is called; the caller must still run observe_removed and
 * its full sources/manual/absence/probe checks. Original owner_run is returned,
 * never inferred from a preparation run. A complete bounded ledger scan and
 * stable held anchors are required. Duplicate matches, same-title outstanding
 * owners, exact-target transient markers, malformed/linked records and
 * uncertain reads refuse. Only a valid ABSENT on a nonmatching historical
 * generation permits its old consumed transient markers. Uncommitted intents
 * grant no match. 0=one owner,1=no completed match,
 * -1=unknown,-2=owned descriptor/DIR/anchor release unconfirmed. Output is
 * cleared on every nonzero result; no backend/service lock may be held. */
int bw_nrc_removed_owner(const struct bw_nrc_environment *,const struct bw_removed_source *,
    const struct bw_nrc_removed_observer *,struct bw_nrc_subject *);
int bw_nrc_observe_removed(const struct bw_nrc_environment *,const char title[10],
    const char owner_run[33],const struct bw_removed_source *,
    const struct bw_nrc_removed_observer *,struct bw_nrc_result *);
#endif
