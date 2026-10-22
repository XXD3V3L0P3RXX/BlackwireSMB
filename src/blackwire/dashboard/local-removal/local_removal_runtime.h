/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BLACKWIRE_LOCAL_REMOVAL_RUNTIME_H
#define BLACKWIRE_LOCAL_REMOVAL_RUNTIME_H
#include "local_removal.h"
#include "native_backend.h"
#include "shadowmount_client.h"

struct bd_local_removal_runtime;
struct bd_local_removal_runtime_config {
    struct bw_native_backend *backend; /* Borrowed resident lifetime. */
    struct bd_local_removal_config removal; /* Trusted fixed path roots. */
    const char *game_log; /* NULL=installed ShadowMount lifecycle log. */
    /* Optional trusted host/platform seams; production leaves these NULL.
     * Callback opaque objects must outlive this runtime.
     * They cannot be populated by an HTTP action. */
    void *opaque;
    int (*reservation_current)(void *,const char[33]);
    int (*idle)(void *); /* FULL ordinary idle, no frontend exemption. */
    int (*settings)(void *,struct bw_sm_document *);
};
/* One worker, never network/filesystem work under a service/backend mutex.
 * This adapter acquires no reservation: service must retain its exact external
 * nonce from WAIT_EXIT through independently confirmed completion. */
struct bd_local_removal_runtime *bd_local_removal_runtime_create(const struct bd_local_removal_runtime_config *);
int bd_local_removal_runtime_destroy(struct bd_local_removal_runtime *); /*1 retained active/unknown.*/
int bd_local_removal_runtime_capable(void *,const struct bd_item *);
/* Service callbacks: enqueue once, never replay a nonce. 1 queued/already
 * accepted same request,0 definite refusal,-1 unknown. Confirmation must be
 * exact selected title; service also binds id/kind/catalogGeneration. */
int bd_local_removal_runtime_start(void *,const struct bd_item *,const char[33],const char[10]);
/*1 terminal done/error,0 pending,-1 unknown. Result is a copied snapshot; no
 * component polling or Sony calls occur on the caller's thread. */
int bd_local_removal_runtime_state(void *,const char[33],struct bw_game_uninstall_result *);
/* Pure parser + real root-alias observation used by the worker and fixtures.
 * Qualified for SMP1.7-beta5fix1 settings: custom scan_paths plus its known
 * defaults and managed roots (a conservative superset). Unknown refuses. */
int bd_local_removal_scan_excluded(const void *,size_t,const char *);
#endif
