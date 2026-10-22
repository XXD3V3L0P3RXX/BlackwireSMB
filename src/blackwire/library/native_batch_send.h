#ifndef BW_NATIVE_BATCH_SEND_H
#define BW_NATIVE_BATCH_SEND_H
#include "native_send.h"
#include "native_batch_fs.h"
#include "native_batch_terminal.h"
struct bw_native_batch_prepared {
    struct bw_native_prepared base; /* original game; prepared_policy exactly2 */
    struct bw_derived_prepared_identity prepared;
    uint8_t terminal_sha256[32];size_t terminal_size;
    const char *error;
    struct bw_native_batch_metadata_accounting metadata;
};
struct bw_native_batch_send_files {
    const char *data_root,*game_log;
    struct bw_batch_plan *plan;
    const struct bw_batch_plan_request *plan_request;
    struct bw_batch_material *material;
    int (*current)(void *);void *opaque; /* CONTROL-only, never in consumers */
};
struct bw_native_batch_snapshot {
    struct bw_native_batch_prepared prepared;
    struct bw_batch_terminal_record terminal;
    struct bw_batch_provenance provenance;
    uint8_t *provenance_bytes;size_t provenance_size;
};
/* Fresh-generation-only publication. Refuses every existing final component or
 * generation-keyed batch state, including owned ones. Existing good state is
 * reused through inspect/preflight, never silently replaced by Send.
 * Borrows actual accepted material/plan/request/provider/cache/optional LOCAL
 * lifetimes exclusively. Emits original LOCAL via real provider, optional
 * project via real V1 LOCAL, selected binary bodies via the owned cache; all effects provisional
 * until cache finish plus final source/material/job checks.
 * Mode5 alone additionally emits original declared Pak metadata spans into
 * held exclusive STREAM files before finish. Inspection hashes every span
 * and enforces the fixed64KiB rounded-union allocation ceiling; unlisted
 * STREAM files and all modes1--4 still require zero blocks. metadata reports
 * declared/unique/ceiling/observed bytes separately, not extra LOCAL payload.
 * Builds private hidden game/state siblings. The game is renamed to an absent
 * distinct policy2 name only after files/hashes/index/mode-specific STREAM verify.
 * BWNDR002 is written last in the private state, then completed state cutover
 * publishes <originalBinding>.<preparedNamespaceHex>. Retained original-keyed
 * historical state is readable and is never moved or overwritten. Same-FS ordinary renameat is used under
 * the owned-parent/cooperative-writer assumption; there is no atomic NOREPLACE
 * or hostile-writer guarantee. Uncertain outcomes are retained, never deleted,
 * restored, recovered or automatically retried. No registration/hooks/launch. */
int bw_native_batch_send_files(const struct bw_native_batch_send_files *,struct bw_native_batch_prepared *);
int bw_native_batch_send_inspect(const char *,const struct bw_provider_binding *,struct bw_native_batch_snapshot *);
/* Exact terminal lookup over bounded historical original-keyed and new
 * generation-keyed states. These structural reads grant no source freshness.
 * Every matching state must be complete and valid; corrupt/incomplete/ambiguous
 * namespaces refuse. At most 4096 scope names and 32 matching generations. */
int bw_native_batch_inspect_record(const char *,const struct bw_provider_binding *,const uint8_t[32],struct bw_native_batch_snapshot *);
int bw_native_batch_record_path(const char *,const struct bw_provider_binding *,const uint8_t[32],char *,size_t);
/* Known-target reads bind the exact original source identity, 74-character
 * prepared component and full terminal SHA256. They inspect only the direct
 * generation-keyed state or matching historical original-keyed state, with a
 * bounded scope-name alias/selected-stage check and stable shallow legacy
 * terminal collision check. Unrelated generation bodies/provenance are not
 * inspected and grant no readiness authority; generic discovery above retains
 * its stricter all-matching-history qualification. An unreadable/incomplete
 * shared legacy key conservatively refuses. No state/path is adopted from
 * metadata alone. inspect_exact fully verifies selected structural artifacts;
 * local_verify_exact additionally hashes selected LOCAL files under current.
 * record_path_exact only qualifies the pinned decoded terminal, exact original
 * source/component/source.json and held/named state/game-root/scope metadata.
 * It does not decode provenance, verify LOCAL bodies, set bytes_verified, or
 * supply readiness/admission; its caller holds an independently validated
 * snapshot and owns the surrounding current/readback checks.
 * current is pure/nonreentrant and owns the caller's actual lease/deadline.
 * Every input remains alive and immutable throughout the synchronous call. */
int bw_native_batch_inspect_exact(const char *,const struct bw_provider_binding *,const char *,const uint8_t[32],struct bw_native_batch_snapshot *);
int bw_native_batch_local_verify_exact(const char *,const struct bw_provider_binding *,const char *,const uint8_t[32],int (*)(void *),void *,struct bw_native_batch_snapshot *);
int bw_native_batch_record_path_exact(const char *,const struct bw_provider_binding *,const char *,const uint8_t[32],char *,size_t);
/* Borrowed read-only snapshot, valid only until callback returns. Zero means
 * continue; nonzero refuses. All states qualify before any callback; callbacks
 * run with no batch mutex, held filesystem handle or copy-global guard/path
 * operation, so nested read-only lookup is permitted. Exact pins are freshly
 * reinspected after callbacks and the complete collection is checked again.
 * Caller owns its real current lease and must not mutate until visit succeeds. */
typedef int (*bw_native_batch_visit_fn)(const struct bw_native_batch_snapshot *,void *);
int bw_native_batch_visit(const char *,const struct bw_provider_binding *,bw_native_batch_visit_fn,void *);
int bw_native_batch_send_local_verify(const char *,const struct bw_provider_binding *,const uint8_t[32],int (*)(void *),void *,struct bw_native_batch_snapshot *);
void bw_native_batch_snapshot_destroy(struct bw_native_batch_snapshot *);
/* Optional fresh owner comparison for normal Play: actual policy2 plan/request
 * must have been independently reconstructed under that real reader job/grant.
 * No PREPARE material/job/token is invented; this does no body emission/write. */
struct bw_native_batch_verify {
    const char *data_root,*game_log;const uint8_t *expected_terminal_sha256;
    struct bw_batch_plan *plan;const struct bw_batch_plan_request *plan_request;
    int (*current)(void *);void *opaque;
};
int bw_native_batch_send_verify(const struct bw_native_batch_verify *,struct bw_native_batch_snapshot *);
#endif
