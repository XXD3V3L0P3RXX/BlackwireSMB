#ifndef BW_PRELOAD_HANDOFF_H
#define BW_PRELOAD_HANDOFF_H
#include "reader_selection.h"
#include "../streamreader/share_config.h"
#include <sys/types.h>
#include <sys/stat.h>

#define BW_PRELOAD_HANDOFF_ROOT_BYTES 1024u
#define BW_PRELOAD_HANDOFF_SELECTION "blackwire-reader.selection"
#define BW_PRELOAD_HANDOFF_SHARE "blackwire-reader.share"
#define BW_PRELOAD_HANDOFF_PROJECTION "blackwire-reader.projection"
enum bw_preload_handoff_status {
    BW_PRELOAD_HANDOFF_OK=0,
    BW_PRELOAD_HANDOFF_REFUSED=-1,
    BW_PRELOAD_HANDOFF_UNCONFIRMED=-2
};
struct bw_preload_handoff_request {
    const char *root;
    const struct bw_reader_selection *selection;
    const struct bw_share_config *connection;
    /* Zero means the real captured launch preparation is still current.
     * The callback must be bounded, must not reenter this object, and must
     * not log credentials. deadline is absolute CLOCK_MONOTONIC seconds. */
    int (*current)(void *);
    void *opaque;
    double deadline;
};
struct bw_preload_handoff_file {
    int created,identity_known,verified,removed;
    size_t bytes;
    struct stat identity;
    uint8_t sha256[32];
    /* Used only by explicit retained publication; never copy/close these FDs.
     * An attempted close with nonzero return is unresolved and never retried. */
    int descriptor,descriptor_held,close_attempted,close_result,close_error;
    /* Return/errno are meaningful only when attempted; skipped is no syscall.
     * errno is an immediate snapshot and can be stale on success. */
    int ownership_attempted,ownership_skipped,ownership_confirmed;
    int ownership_result,ownership_error;
    struct stat producer_identity;
};
/* Caller-zeroed, one-shot, implementation-owned bookkeeping. It contains no
 * credential text. Flags expose partial outcomes; they are not source or
 * launch authority. Keep it until the exact game has independently exited. */
struct bw_preload_handoff {
    uint32_t initialized;
    int busy,published,release_started,released,last_status;
    char root[BW_PRELOAD_HANDOFF_ROOT_BYTES];
    struct stat root_identity,fakelib_identity;
    struct bw_preload_handoff_file share,selection,projection;
    unsigned projected;
    int retained_descriptors,descriptor_close_started,descriptors_closed;
    int consumer_assignment_started,consumer_assigned;
    uid_t consumer_uid;
    pid_t producer_pid;
};
/* Caller owns a canonical qualified prepared root and existing fakelib2, has
 * freshly checked PC/LOCAL/receipt/registration, and has published the module.
 * The ordinary API supports canonical legacy selections, excluding v8. The canonical source-profile resolver
 * must share the application's BW_GTA_AUDIO_BOOTSTRAP compile policy. No
 * selection is reinterpreted or rewritten. All three reserved names must be absent. Creates
 * 0600 share first, selection last, with bounded fsync/readback checks. This is
 * cooperative owned-directory publication, not atomic complete-file publish
 * or protection against a hostile writer. Failure never overwrites or cleans
 * a partial reservation. No global config, module, map or library is touched.
 * Game-process readability of these private files still needs native proof. */
int bw_preload_handoff_publish(const struct bw_preload_handoff_request *,struct bw_preload_handoff *);
/* Two-phase variant: same bytes/modes/order, producer ownership unchanged;
 * retain verified creator FDs for later assignment/exit cleanup. At most one
 * live retained object per producer process. The exact object must remain at
 * its original address in that live process; do not copy/persist/adopt it.
 * This publishes inputs only, not game readability, Ready or launch authority. */
int bw_preload_handoff_publish_retained(const struct bw_preload_handoff_request *,struct bw_preload_handoff *);
/* Explicit policy3 variant. The envelope comes only from this launch's held
 * execution admission, is verified before publication by the controller, and
 * is independently verified by the DATA worker. Publish share, projection,
 * selection last; retain all three creator FDs under this same one-shot lease.
 * The ordinary APIs refuse v8. A foreign/stale projection input always refuses. */
int bw_preload_handoff_publish_projection_retained(const struct bw_preload_handoff_request *,
    const void *,size_t,struct bw_preload_handoff *);
/* Caller independently qualified a POSIX UID and exact current game/owner
 * policy; this function never derives it from title, Sony user ID or a guess.
 * current must cover that real policy, including the captured game identity.
 * All held bodies qualify before assignment starts. At most one fchown per
 * file, share first/selection last, GID unchanged; already-matching UID skips
 * that syscall. Full held/named/hash readback qualifies success. Partial or
 * ambiguous assignment is retained/unconfirmed and cannot be replayed.
 * UID-1 is refused. Mode0600 and all original body/identity guards remain. */
int bw_preload_handoff_assign_consumer(struct bw_preload_handoff *,uid_t,
    int (*current)(void *),void *,double deadline);
/* Convenience composition only for an already-qualified prelaunch owner;
 * request.current must include its policy. Ordinary publish is unchanged. */
int bw_preload_handoff_publish_for_consumer(const struct bw_preload_handoff_request *,uid_t,struct bw_preload_handoff *);
/* exited must return zero only while the caller's exact retained game identity
 * is independently known to have exited. It is checked before and between
 * mutations. Fresh canonical named/held identities and full hashes of ALL
 * owned files are required before the first unlink. Retained mode verifies
 * its held FDs and fresh named metadata without reopening private bodies.
 * Removes only unchanged
 * owned selection, then projection if present, then share. An unknown/partial outcome is retained and
 * must not be blindly replayed. Existing user files/folder/libs are never
 * deleted. No automatic process control or fabricated worker lease exists. */
int bw_preload_handoff_release(struct bw_preload_handoff *,int (*exited)(void *),void *,double deadline);
/* Explicit FD-only discharge after independently confirmed exact exit (or
 * known no-launch abort). Never unlinks/adopts files or requires uncertain
 * body bytes to match. Producer-instance + held dev/ino/type qualify each
 * at-most-once close. An unknown close or incomplete discharge stays retained
 * and cannot be blindly retried. Success here does NOT mean files released.
 * No descriptor receipt survives daemon restart/loss. */
int bw_preload_handoff_close_descriptors(struct bw_preload_handoff *,int (*exited)(void *),void *,double deadline);
#endif
