/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_NATIVE_REMOVED_SOURCES_H
#define BW_NATIVE_REMOVED_SOURCES_H
#include "source_identity.h"
#include <stddef.h>
#define BW_REMOVED_LIMIT 256u
struct bw_removed_source {
    struct bw_provider_binding identity;
    unsigned char prepared_sha256[32];
    char operation[33];
};
enum bw_removed_state {BW_REMOVED_UNKNOWN=-1,BW_REMOVED_VISIBLE=0,
    BW_REMOVED_COMMITTED=1,BW_REMOVED_RESTORED=2};
struct bw_removed_ops {
    void *opaque;
    /* Trusted shared reservation/settings/selection guard, never UI input. */
    int (*current)(void *);
    /* Twice independently qualify exact prepared-root/private-record/title
     * absence. commit is called only after the actual uninstall completes;
     * Restore also requires no partial/replaced copy to be silently adopted. */
    int (*absent)(void *,const struct bw_removed_source *);
};
struct bw_removed_result {enum bw_removed_state state;const char *reason;};
/* Read-only private durable library state, not browser storage. No ledger is
 * created by lookup/list. Missing whole ledger means visible; malformed or
 * linked evidence is UNKNOWN, never a hidden/removed claim. Credentials are
 * not stored: identity's binding is the existing server/share/source/title
 * domain and does not include account/password material. */
int bw_removed_lookup(const char *data_root,const struct bw_provider_binding *,
    struct bw_removed_source *,struct bw_removed_result *);
int bw_removed_list(const char *data_root,struct bw_removed_source *,size_t,size_t *);
/* Creates only one complete 0600 record after exact absence. Partial writes,
 * existing contradictory records and uncertain closes refuse without replay.
 * PC files, PS5 prepared files, registrations and saved data are not touched. */
int bw_removed_commit(const char *data_root,const struct bw_removed_source *,
    const struct bw_removed_ops *,struct bw_removed_result *);
/* Explicit Restore clears only a positively held exact library tombstone.
 * It does not copy files or assert Ready. A durable restore intent precedes
 * one attempt per held record/commit unlink; after uncertainty the same
 * operation is observation-only. */
int bw_removed_restore(const char *data_root,const struct bw_provider_binding *,
    const char operation[33],const struct bw_removed_ops *,struct bw_removed_result *);
#endif
