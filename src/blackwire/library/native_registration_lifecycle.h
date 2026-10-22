/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_NATIVE_REGISTRATION_LIFECYCLE_H
#define BW_NATIVE_REGISTRATION_LIFECYCLE_H
#include "native_registration_retirement.h"
#include "reader_runtime.h"
#include "shadowmount_client.h"

enum bw_nrl_control { BW_NRL_REGISTER, BW_NRL_RETIRE };
struct bw_nrl_request {
    struct bw_nrc_environment environment;
    const struct bw_reader_selection *selection;
    uint16_t port;
    /* Trusted joined worker CONTROL reservation, not cancelled Play DATA.
     * RETIRE must additionally prove exact joined I/O/private-input release.
     * Both modes require fresh full normal idle and unchanged settings/epoch.
     * Never called with the backend/service mutex held. */
    int (*current)(void *,enum bw_nrl_control);
    void *opaque;
};
struct bw_nrl_owner;
/* New ABSENT registrations require a durable intent before any add/scan.
 * A twice-matching existing registration is explicitly left unowned.
 * begin/registered return BW_NRC_RELEASE_UNCONFIRMED for an ambiguous internal
 * close. Caller must retain CONTROL as Unknown, even if startup remains valid.
 * registered remembers this outcome until destroy; no close is retried. */
int bw_nrl_begin(const struct bw_nrl_request *,struct bw_nrl_owner **,struct bw_nrc_result *);
int bw_nrl_registered(struct bw_nrl_owner *,int,const struct bw_register_result *,struct bw_nrc_result *);
/* Shared Send/explicit-Uninstall admission only. Complete one older committed
 * same-origin event by observed app-dir/DB/manual absence while a verified new
 * prepared receipt is held current. No add/remove/uninstall request, source
 * deletion, unknown-phase reset or ownership adoption. current must retain
 * full native idle/settings/selection CONTROL for the joined caller. */
int bw_nrl_reconcile_prepared_generation(const struct bw_nrc_environment *,
    const struct bw_reader_selection *,uint16_t,int (*)(void *),void *,struct bw_nrc_result *);
/* Called only after reader_run and its borrowed startup owner have joined and
 * checked closes succeeded. A matching positive release output is mandatory.
 * Durable pending markers consume each normal request even after lost reply. */
int bw_nrl_retire(struct bw_nrl_owner *,const struct bw_native_reader_release *,struct bw_nrc_retirement_result *);
/* Consumes the owner. Zero only if all present AND previously attempted owned
 * releases were confirmed; nonzero must retain the caller's Unknown control. */
int bw_nrl_destroy(struct bw_nrl_owner **);
/* Reserved idle restart/tick observation only. The durable pending marker is
 * required; this never invents the old release certificate or submits/advances
 * any manual-remove/uninstall request. */
int bw_nrl_observe(const struct bw_nrc_environment *,const char[10],const char[33],
    uint16_t,int (*)(void *,enum bw_nrl_control),void *,struct bw_nrc_retirement_result *);
struct bw_nrc_remote;
/* Read-only post-unregister collector. Hold the canonical shared apps parent
 * through both remote snapshots; independently require exact title ENOENT and
 * no read-only database row twice before/after each snapshot. Caller supplies
 * the retained full-idle mutation reservation. This cannot authorize requests. */
int bw_nrl_absent_sources(const struct bw_nrc_environment *,struct bw_nrc_remote *,const struct bw_nrc_subject *);
/* Actual settings parser + no-follow root collector boundary for fault tests.
 * The supplied complete document must be from the fixed SMP settings route.
 * Custom roots override the source-pinned defaults; managed roots always apply.
 * This is cooperative snapshot evidence, not an upstream uninstall CAS. */
int bw_nrl_settings_exclude(const struct bw_sm_connection *,const struct bw_nrc_subject *,const void *,size_t);
/* Explicit Uninstall only: borrowed present-before-delete witnesses. After
 * deletion, only ENOENT for the exact leaf beneath the same canonical held
 * parent AND a captured positive link count becoming zero on that same inode
 * is accepted. A filesystem's initial zero link count is not removal proof. Rename,
 * replacement, parent change or unknown close/metadata never means absence. */
struct bw_nrl_retained_source {int parent,root;struct stat parent_identity,root_identity;};
int bw_nrl_settings_exclude_retained(const struct bw_sm_connection *,const struct bw_nrc_subject *,
    const void *,size_t,const struct bw_nrl_retained_source *);
#endif
