#ifndef BW_PRELOAD_BUNDLE_H
#define BW_PRELOAD_BUNDLE_H
#include <stddef.h>
#include <stdint.h>
/* Optional source-provided PlayGo emulator pins, never embedded module bytes.
 * Provenance: strongt1me/PS5-Dump-Image-Converter commit
 * 1942dd01a6a0ab5532ba51af10444d5f776f23c0, PlayGo & AMPR_EMU/
 * PlayGo_v0.5/{log,nolog} and PlayGo_v0.6/{log,nolog}/libScePlayGo.sprx.
 * The same commit includes GPL-3.0 source in each Quellcode/ directory.
 * These are exact distributed artifact identities, not a reproducible-build
 * claim or authority to accept another build with a similar filename. */
struct bw_preload_playgo_pin {size_t size;const char *hex;};
static inline const struct bw_preload_playgo_pin *bw_preload_playgo_pin(uint64_t size){
    static const struct bw_preload_playgo_pin pins[]={
        {20582,"305e6e117ca3a16cb6062007e3ddee7eb184ca19c3e353d5b12a6161bc92a3bd"},
        {14390,"645caa464a7d28fab7019ba547b204ecc007f87ccec072d330eb2b72a3e9ddac"},
        {23702,"7595924889d67c33149dc152903d2f2492abbc3e846196d536c4f16a7b5560f6"},
        {18150,"f5fdb0ed6cc7413e6dcac79a62e0efd8cfbae04cca0bf932e7a741aed850d213"}
    };
    for(unsigned i=0;i<sizeof pins/sizeof pins[0];i++)if(size==pins[i].size)return pins+i;
    return NULL;
}
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
    /* A rejected entry name is copied and byte-escaped before readdir storage
     * is released. Truncation is explicit; no path or dirent pointer escapes.
     * Ignored source entries are never selected, opened, copied or adopted. */
    const char *entry_class;
    char entry_name[257];
    unsigned entry_name_truncated,source_entries_ignored;
};
/* Install the embedded reader into an absent per-game fakelib2. No global
 * library folder is required or opened. Qualified optional companions already
 * present in the prepared game's fakelib are copied without changing them.
 * Optional PlayGo is copied only when its size and complete hash match one
 * of the fixed source-backed emulator artifacts above. Unknown source modules
 * refuse rather than silently losing functionality under an exclusive bundle.
 * Original metadata entries are left untouched and are not opened, followed,
 * copied or granted execution/ownership authority. ShadowMount's
 * exclusive fakelib2 selection is distinct from its composable source fakelib.
 * The owned private fakelib2 always retains its exact qualified inventory.
 * An existing complete legacy five-file bundle remains admitted independently
 * of global state; any present game source is still scanned/qualified, and
 * source PlayGo requires its matching private companion. New bundles must
 * match the game's qualified companion set.
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
