/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_NATIVE_CLEANUP_REMOTE_H
#define BW_NATIVE_CLEANUP_REMOTE_H
#include "native_registration_cleanup.h"
#include "shadowmount_client.h"
#include <stdint.h>
struct bw_nrc_remote {
    struct bw_sm_connection connection;
    const char *reason; /* static nonsecret diagnostic */
    /* Observation diagnostics only, never ownership or absence evidence. */
    const char *substage,*path_class;
    unsigned row_index,missing_fields,unknown_game_rows,unknown_manual_paths;
    unsigned preparation_only,observation_release_unknown;
    int observation_error;
    uint64_t local_observation_bytes; /* bounded read-only classification work */
#ifdef BW_COPY_HOST_TEST
    const char *fixture_manual_status_path; /* host-only real-filesystem seam */
#endif
};
/* These are the sources/uninstall adapters for a trusted cleanup reservation.
 * The connection guard must exclude every frontend/game and conflicting worker.
 * They do not acquire authority from a browser or from an HTTP response. */
int bw_nrc_remote_sources(void *,const struct bw_nrc_subject *);
int bw_nrc_remote_uninstall(void *,const char[10]);
/* Registration only: SMP's normal install mounts the exact source before its
 * reply. mounted is mount presence, not a live-process observation. This
 * permits that flag only for the single exact same-title/content/folder/source
 * row; the caller still proves full idle, source/receipt and real app+DB identity.
 * Retirement/uninstall must keep using the strict adapters above. */
int bw_nrc_remote_registration_sources(void *,const struct bw_nrc_subject *);
/* Post-registration absence only. The trusted callback must independently
 * prove exact app-directory AND read-only database absence beneath retained
 * named parents. It is required twice before and after every snapshot; zero
 * is not inferred from cache flags. Empty cached content is permitted only
 * for the exact uninstalled/unmanaged/unmounted folder, never a PKG/image.
 * These functions do not submit or replay any mutation. */
typedef int (*bw_nrc_remote_absent_fn)(void *,const struct bw_nrc_subject *);
int bw_nrc_remote_absent_sources(void *,const struct bw_nrc_subject *,bw_nrc_remote_absent_fn,void *);
/* A trusted live lifecycle transition or explicit confirmed maintenance may
 * positively bind a still-mounted exact registration. Return1 only for that
 * captured app/source/DB MATCH with its qualified authority,0 for independent
 * app+DB absence, or-1 unknown. Two checks before/after every snapshot must
 * agree; no state transition is accepted within one collection call. */
typedef int (*bw_nrc_remote_registered_fn)(void *,const struct bw_nrc_subject *);
int bw_nrc_remote_confirmed_sources(void *,const struct bw_nrc_subject *,bw_nrc_remote_registered_fn,void *);
/* Only inside an already-consumed original mutation unit. The callback must
 * still return positive exact ownership/confirmed app+source+DB identity.
 * One supported normal unmount, no retry/force; strict fresh unmounted state
 * and the same positive witness are required before a later unregister. */
int bw_nrc_remote_release_runtime(struct bw_nrc_remote *,const struct bw_nrc_subject *,bw_nrc_remote_registered_fn,void *);
const char *bw_nrc_remote_status_message(const struct bw_nrc_remote *);
/* Parser/real-filesystem boundary, also used by host fixtures. Both documents
 * must be complete current SMP responses. An unknown manual source refuses;
 * cache/list absence never establishes that a registered title is absent. */
int bw_nrc_remote_documents(struct bw_nrc_remote *,const struct bw_nrc_subject *,
    const void *,size_t,const void *,size_t);
int bw_nrc_remote_registration_documents(struct bw_nrc_remote *,const struct bw_nrc_subject *,
    const void *,size_t,const void *,size_t);
int bw_nrc_remote_absent_documents(struct bw_nrc_remote *,const struct bw_nrc_subject *,
    const void *,size_t,const void *,size_t,bw_nrc_remote_absent_fn,void *);
int bw_nrc_remote_confirmed_documents(struct bw_nrc_remote *,const struct bw_nrc_subject *,
    const void *,size_t,const void *,size_t,bw_nrc_remote_registered_fn,void *);
/* Non-destructive registration preparation only. The callback must repeatedly
 * prove independent app+DB absence (0), or exact app/param/mount/source/DB MATCH
 * (1), or return -1. Disjoint unknown unrelated entries are counted, never
 * called absent/owned. Target rows and overlapping/aliased paths remain strict.
 * A successful result is NOT a destructive source proof. Every later removal
 * must call the original strict adapters with its positive ownership witness. */
int bw_nrc_remote_prepare_sources(void *,const struct bw_nrc_subject *,bw_nrc_remote_registered_fn,void *);
int bw_nrc_remote_prepare_documents(struct bw_nrc_remote *,const struct bw_nrc_subject *,
    const void *,size_t,const void *,size_t,bw_nrc_remote_registered_fn,void *);
#endif
