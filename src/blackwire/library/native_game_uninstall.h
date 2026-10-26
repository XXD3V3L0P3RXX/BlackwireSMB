/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_NATIVE_GAME_UNINSTALL_H
#define BW_NATIVE_GAME_UNINSTALL_H
#include "native_removed_sources.h"
#include "native_send.h"
#include "native_registration_cleanup.h"
enum bw_game_uninstall_phase {BW_GAME_REMOVE_CHECKING,
    BW_GAME_REMOVE_REGISTRATION,BW_GAME_REMOVE_FILES,BW_GAME_REMOVE_RECORD,
    BW_GAME_REMOVE_COMMITTING,BW_GAME_REMOVE_DONE,BW_GAME_REMOVE_ERROR,
    BW_GAME_REMOVE_UNKNOWN};
struct bw_game_uninstall_progress {
    enum bw_game_uninstall_phase phase;
    uint32_t files_done,files_total;
    const char *message;
};
struct bw_game_uninstall_ops {
    void *opaque;
    /* Required shared resident mutation reservation and full normal game/app
     * idle after native WAIT_EXIT; exact selection/settings/cancel epoch. */
    int (*current)(void *);
    /* Remove only a positively owned registration through the normal ledger
     * retirement path, or prove Send-only registered absence. A title-only
     * request is never authority. This callback may perform one qualified
     * normal request; the component independently rechecks app-dir+DB absence.
     * 0=qualified complete,1=pending/refused,-1=unknown; never blindly retry. */
    int (*registration_absent)(void *,const struct bw_removed_source *);
    /* Complete manual-list and effective scan-root guard for this exact copy.
     * No global roots/settings may be modified to satisfy this callback. */
    int (*source_excluded)(void *,const struct bw_removed_source *);
    void (*observe)(void *,const struct bw_game_uninstall_progress *);
    /* Optional pre-registration collector: all effective scan roots exclude
     * the copy and complete lists contain no alternative/foreign source; the
     * exact selected manual association may still be present for the single
     * normal unregister adapter. If absent, source_excluded is required even
     * before registration. source_excluded remains mandatory afterward. */
    int (*source_safe)(void *,const struct bw_removed_source *);
};
struct bw_game_uninstall_request {
    struct bw_nrc_environment environment;
    struct bw_provider_binding identity;
    struct bw_native_prepared prepared;
    char operation[33];
    /* Explicit human confirmation of this exact selected title, after the
     * service has bound id/kind/catalogGeneration. Never inferred from a path. */
    char confirmed_title[10];
    struct bw_game_uninstall_ops ops;
    /* Trusted daemon's exact reader pin for an optional per-game fakelib2;
     * no client-supplied hash or arbitrary extra-file allowlist. */
    unsigned char reader_sha256[32];
    uint64_t reader_size;
};
struct bw_game_uninstall_result {
    enum bw_game_uninstall_phase phase;
    unsigned mutation_started,registration_confirmed,source_absent,
        prepared_record_absent,library_removed;
    /* Distinct from mutation: an owned fd/DIR/helper could not be released.
     * The resident must keep all mutating controls blocked; never retry. */
    unsigned release_unknown;
    uint32_t files_removed,files_total;
    const char *reason;
};
/* First candidate is exact raw FOLDER/V1 policy0 only. Send-only copies are
 * eligible through their durable preparation ownership and independent title
 * absence; no registration-event ledger is required when no title exists.
 * Inventory must match receipt/map/root identity before the first deletion.
 * Unknown entries, links, foreign titles/PKGs, active or uncertain processes,
 * replaced nodes and partial prior attempts refuse. PC original and saved
 * data roots are never an input or deletion target. Manual full-folder and
 * installed-PKG uninstall require a later separate named-source confirmation
 * path; neither is implemented by this API. */
int bw_native_game_uninstall(const struct bw_game_uninstall_request *,
    struct bw_game_uninstall_result *);
struct bw_game_uninstall_files_ops {
    void *opaque;
    /* The retained original operation under its shared mutation reservation,
     * unchanged settings/selection and fresh full normal process/game idle. */
    int (*current)(void *);
    /* Complete post-registration manual/scan-root exclusion. This is checked
     * alongside independent exact title-directory and read-only DB absence. */
    int (*source_excluded)(void *,const struct bw_removed_source *);
    void (*observe)(void *,const struct bw_game_uninstall_progress *);
};
struct bw_game_uninstall_files_request {
    struct bw_nrc_environment environment;
    struct bw_provider_binding identity;
    struct bw_native_prepared prepared;
    char operation[33],confirmed_title[10];
    struct bw_game_uninstall_files_ops ops;
    unsigned char reader_sha256[32];
    uint64_t reader_size;
};
/* Explicit continuation of ONE authenticated original operation. A valid
 * durable .intent and .registration must already exist; this API accepts NO
 * registration/manual-remove/uninstall callback. Only an absent .files/.done
 * pair permits the never-started file phase, after rebuilding the complete
 * identical original inventory and independent twice-confirmed title absence.
 * Once .files exists, every later call is observation-only. No new lifecycle,
 * relaxed inventory, resumed partial unlink, or inferred registration result.
 * Any drift/unknown close/current/exclusion retains UNKNOWN without a retry. */
int bw_native_game_uninstall_continue_files(const struct bw_game_uninstall_files_request *,
    struct bw_game_uninstall_result *);
struct bw_game_uninstall_capability {
    unsigned can_uninstall,removed,can_restore;
    const char *reason;
};
/* Presentation-only, bounded metadata probe. No manifest/tree/STREAM walk,
 * LOCAL body digest pass, layout creation, registration action or deletion.
 * The supplied prepared tuple is the backend's cached completion. A positive
 * result is not destructive admission: Uninstall still performs its complete
 * fresh inventory, selection/current, confirmation and absence checks. */
int bw_native_game_uninstall_probe(const struct bw_nrc_environment *,
    const struct bw_provider_binding *,const struct bw_native_prepared *,
    struct bw_game_uninstall_capability *);
#endif
