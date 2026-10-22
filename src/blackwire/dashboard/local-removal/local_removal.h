/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BLACKWIRE_LOCAL_REMOVAL_H
#define BLACKWIRE_LOCAL_REMOVAL_H
#include "dashboard_model.h"
#include "catalog_adapter.h"
#include "native_game_uninstall.h"

#define BD_LOCAL_REMOVE_ROOTS 8u
#define BD_LOCAL_REMOVE_ENTRIES 262144u
struct bd_local_removal_confirmation {
    char id[BD_ID_BYTES],title_id[10];
    enum bd_source_kind kind;
    uint64_t catalog_generation;
    unsigned confirmed;
};
struct bd_local_removal_request {
    struct bd_item selected; /* Trusted fresh daemon observation. */
    struct bd_local_removal_confirmation confirmation;
    char operation[33];
};
struct bd_local_removal_ops {
    void *opaque;
    /* Shared reservation + exact confirmed operation + full normal idle,
     * after native WAIT_EXIT. No frontend exemption. After quarantine this
     * guard must not require the deliberately retired original source name. */
    int (*current)(void *);
    /* Zero only after fresh complete effective scan-root observation proves
     * this exact destination is outside EVERY root. Never change settings. */
    int (*quarantine_allowed)(void *,const char *);
    /* Optional trusted transport seams. NULL uses existing bounded SMP APIs.
     * Uninstall/manual_remove return0 accepted,1 refused,-1 unknown. Any
     * result consumes the single durable attempt. No callback may retry. */
    int (*collect)(void *,struct bd_smp_record *,size_t,size_t *);
    int (*uninstall)(void *,const char[10]);
    int (*manual_present)(void *,const char *); /*0 absent,1 present,-1 unknown.*/
    int (*manual_remove)(void *,const char *);
    void (*observe)(void *,const struct bw_game_uninstall_progress *);
};
struct bd_local_removal_config {
    const char *data_root,*apps_root,*database;
    /* Trusted fixed roots, never fields from an HTTP request. Zero folder
     * roots selects /data/homebrew/Games. A folder must be strictly beneath
     * one root. PKG source must be <packageRoot>/<titleId>/app.pkg. */
    const char *folder_roots[BD_LOCAL_REMOVE_ROOTS];size_t folder_root_count;
    const char *package_roots[BD_LOCAL_REMOVE_ROOTS];size_t package_root_count;
    uint16_t smp_port;
    struct bd_local_removal_ops ops;
};
struct bd_local_removal_result {
    enum bw_game_uninstall_phase phase;
    unsigned mutation_started,quarantined,uninstall_attempted,
        uninstall_accepted,registration_confirmed,manual_remove_attempted,
        manual_absent,source_absent,library_removed,active;
    uint32_t files_removed,files_total;
    const char *reason; /* Static named outcome; never credentials. */
};
struct bd_local_removal;
/* Pure shape/kind/root policy. Fresh source/registration qualification occurs
 * in begin. Initial PPSA normal uninstall is independent of launch capability;
 * CUSA is withheld because the maintained uninstall transport is PPSA-only.
 * Blackwire's fixed native title and PC/image kinds are always refused. */
int bd_local_removal_capable(const struct bd_local_removal_config *,const struct bd_item *);
/* One confirmed operation. Writes a private, fsynced/readback exact source+
 * inode+inventory intent BEFORE same-device folder quarantine, and a separate
 * marker BEFORE the single normal uninstall request. It never uninstalls from
 * a title alone. Both accepted and uncertain outcomes continue via poll only.
 *0 owner created,1 definite refusal,-1 uncertain; *owner may be retained on -1.
 * Call without backend/service locks. No threads/process control are added. */
int bd_local_removal_begin(const struct bd_local_removal_config *,
    const struct bd_local_removal_request *,struct bd_local_removal **,
    struct bd_local_removal_result *);
/* Observe independent exact app-dir + read-only SQLite absence twice. Folder
 * inventory deletion starts only afterward, following exact manual-list
 * removal/readback. PKG source is NEVER manually deleted. A completed private
 * record supplies the catalog's exact-source removal event; no title merging.
 *1 complete,0 retained/pending,-1 unknown retained. No uninstall retry. */
int bd_local_removal_poll(struct bd_local_removal *,struct bd_local_removal_result *);
/* Refuses to forget an unresolved mutation. Completed/refused owners close
 * descriptors only; durable receipts are retained. Any unresolved/malformed
 * operation directory blocks new local removals, including a different nonce
 * after restart; automatic crash recovery is not added. */
int bd_local_removal_destroy(struct bd_local_removal *); /*1 retained,-1 close error.*/
#endif
