#ifndef BW_NATIVE_BATCH_CONTEXT_H
#define BW_NATIVE_BATCH_CONTEXT_H
#include "derived_batch.h"
#include "derived_batch_provenance.h"
#include "native_derived_context.h"

/* Stable-address, zeroed owner borrowed by one actual joined native worker.
 * Owns its cache, optional existing V1 project/LOCAL owner, batch plan and
 * prepare-only material. The original provider/source/snapshot remain borrowed
 * and unchanged; destroy this context before destroying that provider. */
struct bw_native_batch_context {void *state;const char *error;unsigned busy,failed;};

/* Explicit controller acceptance of literal Engine/Content/Internationalization
 * placement. 1=batch ready for this DATA operation, 0=not applicable, -1=refusal.
 * Only an actual accepted selection + unique RUNNING jobs/cache/token may open
 * material. No fake job/cache/token or retained ready bit provides authority.
 * No source mutation, copy, receipt, registration or publication occurs. */
int bw_native_batch_context_prepare(struct bw_native_batch_context *,
    struct bw_source_provider *,int (*)(void *),void *,
    const struct bw_native_prepare_selection *,struct bw_native_jobs *,
    const struct bw_native_job_token *);

/* Fresh actual owners under the real Play guard/epoch. Historical facts must
 * come from the owned strict BWDPR002 decoder and stay addressable/immutable
 * through this synchronous call. No preparation material or jobs are created.
 * Returns 0 only when the reconstructed live plan exactly matches history. */
int bw_native_batch_context_reopen(struct bw_native_batch_context *,
    struct bw_source_provider *,int (*)(void *),void *,uint64_t,
    const struct bw_batch_facts *);

/* Outer orchestration only. Never call these from a DATA consumer, source
 * observer or current/grant callback. Borrowed handles/pointers remain owned
 * by this context. Current/grant callbacks are pure nonreentrant job checks;
 * only the cache's outer fence performs actual provider/project revalidation.
 * No accessor is current-source/publication/readiness authority. */
const struct bw_batch_plan_request *bw_native_batch_context_request(struct bw_native_batch_context *);
struct bw_batch_plan *bw_native_batch_context_plan(struct bw_native_batch_context *);
struct bw_batch_material *bw_native_batch_context_material(struct bw_native_batch_context *);
struct bw_packed_bootstrap *bw_native_batch_context_cache(struct bw_native_batch_context *);
struct bw_native_derived_context *bw_native_batch_context_project(struct bw_native_batch_context *);
const struct bw_batch_facts *bw_native_batch_context_view(struct bw_native_batch_context *);

/* Fresh provider/project fences are outer-only, never per ICU body. Emitted
 * cached bytes remain provisional until cache finish and caller publication
 * checks. The normal batch copier owns actual cache finish exactly once;
 * after its finish/cutovers use revalidate(), NEVER this finish() wrapper.
 * The wrapper is for an outer caller that itself owns all cache emission and
 * calls cache finish nowhere else, once after every ICU entry was emitted.
 * Any failure is sticky; discard all provisional results. */
int bw_native_batch_context_revalidate(struct bw_native_batch_context *);
int bw_native_batch_context_finish(struct bw_native_batch_context *);
int bw_native_batch_context_destroy(struct bw_native_batch_context *);
#endif
