#ifndef BW_PRELOAD_BUNDLE_H
#define BW_PRELOAD_BUNDLE_H
#include <stddef.h>
#include <stdint.h>
struct bw_preload_image {
    const uint8_t *bytes;
    size_t size;
    uint8_t sha256[32];
    const char *scope_sha256;
    unsigned private_wait_milliseconds;
};
/* Defined by the normal app builder from one explicitly pinned reviewed scope,
 * not from a mutable default build filename. All image bytes live in the one
 * app ELF. This symbol is absent in builds without BW_NATIVE_PRELOAD. */
extern const struct bw_preload_image bw_preload_embedded_image;
struct bw_preload_bundle_request {
    const char *root;
    int (*current)(void *);
    void *opaque;
    double deadline;
};
/* First failure only. Strings name fixed stages/libraries, never paths or
 * credentials, and remain valid after return. A zero system_error means no
 * usable errno was captured; the reason still describes the refusal. */
struct bw_preload_bundle_result {
    const char *stage,*reason,*library;
    int system_error;
    unsigned mutation_started;
    /* Independent from a retained/partial per-game mutation. Set only when
     * an owned descriptor/directory stream or worker lock could not be
     * positively released. Zero never permits checkpoint replay/adoption. */
    unsigned release_unknown;
};
/* Install the embedded reader into an absent per-game fakelib2. No global
 * library folder is required or opened. Qualified optional companions already
 * present in the prepared game's fakelib are copied without changing them.
 * An existing complete legacy five-file bundle remains admitted independently
 * of global state. New bundles must match the game's qualified companion set.
 * Other backports, foreign files, stale inputs, changed game companions,
 * partial writes and unknown closes refuse. No overwrite, rename or delete.
 * Returns 0=verified, -1=no destination mutation, -2=partial/uncertain outcome.
 * Caller must retain -2 and must not automatically replay it. This verifies
 * material; ShadowMount's actual module selection still needs native evidence.
 */
int bw_preload_bundle_install(const struct bw_preload_bundle_request *);
int bw_preload_bundle_install_checked(const struct bw_preload_bundle_request *,
                                     struct bw_preload_bundle_result *);
/* Strict second check: no creation, predecessor adoption or upgrade. */
int bw_preload_bundle_verify_checked(const struct bw_preload_bundle_request *,
                                    struct bw_preload_bundle_result *);
struct bw_preload_bundle_upgrade_request {
    struct bw_preload_bundle_request bundle;
    const char *data_root,*game,*prepared_run,*launch_run;
    const uint8_t *prepared_record_sha256;
};
/* Only the joined raw-folder Play worker, BEFORE registration, may call this.
 * current must cover its borrowed live admission/receipt, settings/reservation
 * and full normal idle; this API owns only the descriptors it opens. A fixed
 * existing per-state workspace blocks even a current-image no-op. Accept only
 * the exact compiled recognized predecessors, never a caller-supplied old hash. No map/game
 * data writes, source rescan or receipt reseal. 0=current/fully upgraded,
 * -1=definite pre-mutation refusal, -2=mutation/release outcome uncertain.
 * A -2 MUST preserve the per-game checkpoint and prohibit registration;
 * retain the caller's global control owner only if release_unknown is set.
 * A known-closed interrupted mutation is terminal for this attempt, while
 * workspace presence continues to refuse this game's later Play/Send.
 * Neither this API nor its caller may resume/replay or roll back implicitly.
 * Callbacks must not reenter; all calls/checked closes run on this same worker.
 */
int bw_preload_bundle_upgrade_checked(const struct bw_preload_bundle_upgrade_request *,
                                     struct bw_preload_bundle_result *);
/* Optional same-worker CONTROL split. current remains a frequent, bounded
 * cancellation/epoch/selection lease. checkpoint must additionally verify the
 * original settings, held startup identity and full normal process/log idle.
 * The bundle calls it before mutation/publication/cleanup commits and successful
 * return, under the same immutable deadline. A NULL split is the legacy API;
 * it retains the original full current callback at every guard. No retry,
 * workspace recovery, reader policy or release-unknown rule changes. */
struct bw_preload_bundle_control {int (*checkpoint)(void *);void *opaque;};
int bw_preload_bundle_upgrade_controlled(const struct bw_preload_bundle_upgrade_request *,
    const struct bw_preload_bundle_control *,struct bw_preload_bundle_result *);
/* Presentation/refusal only; never grants launch or migration authority.
 * 0=workspace observed absent, 1=present (any type), -1=not safely observed.
 * Call only for a trusted valid prepared game key. No hashes or writes. */
int bw_preload_bundle_upgrade_workspace(const char *data_root,const char *game);
struct bw_preload_reader_pin {size_t size;uint8_t sha256[32];};
/* Read-only ordinary Uninstall selection. Uses the same trusted prepared/owner
 * tuple, but never creates a workspace or library. Missing fakelib2 selects the
 * current default; a present exact set must contain current or a compiled recognized predecessor.
 * The deletion core must independently reverify the full receipt/inventory.
 * -2 means release uncertainty, even though mutation_started remains false. */
int bw_preload_bundle_select_for_removal(const struct bw_preload_bundle_upgrade_request *,
                                       struct bw_preload_reader_pin *,struct bw_preload_bundle_result *);
#endif
