#ifndef BW_DERIVED_LOCAL_H
#define BW_DERIVED_LOCAL_H
#include "derived_plan.h"

#define BW_DERIVED_LOCAL_MAX_ATTEMPTS BW_DERIVED_MAX_RECORDS
#define BW_DERIVED_LOCAL_MAX_CONSUME_CALLS 131072u
struct bw_derived_local_limits {
    uint32_t attempts,consume_calls;
    uint64_t attempted_bytes,delivered_bytes; /* each <=5 GiB; not allocations */
};
struct bw_derived_local_stats {
    uint32_t attempts,consume_calls,completed;
    uint64_t attempted_bytes,delivered_bytes;
};
struct bw_derived_local {void *state;const char *error;unsigned busy,failed;};
/* Zero initialize. Owns a real bw_derived_plan_open instance; borrows the exact
 * caller-owned provider/parser/source/job/grant of that request. Those inputs
 * must have been opened coherently, remain addressable and immutable, and be
 * used exclusively until destroy. Public facts cannot attest private pointers.
 * Only the existing FOLDER/1, profile-none, plain-project DATA policy is allowed.
 * Explicit per-instance work limits cannot exceed the constants above or 5 GiB.
 * Does not extend/reset the source's captured absolute deadline. */
int bw_derived_local_open(struct bw_derived_local *,const struct bw_derived_plan_request *,
                          const struct bw_derived_local_limits *);
/* Historical immutable composed metadata only. No fresh network validation,
 * copying, publication, readiness, registration or launch authority. */
const struct bw_derived_plan_view *bw_derived_local_view(struct bw_derived_local *);
/* Full actual plan/provider/parser validation before and after body dispatch.
 * ID is the COMPOSED ID; STREAM and mismatched origins refuse. Original LOCAL
 * uses actual provider_read; derived LOCAL uses actual sealed SMB read_range.
 * Whole size + composed BWDIG SHA256 must match. Zero-length original files
 * succeed without a consumer call. Every delivered chunk is provisional until
 * zero return; callers must discard all effects on failure. Failure is sticky.
 * Consumer/current/grant/prior-observer callbacks are synchronous and must not
 * reenter ANY provider/parser/composer/source/SDK or mutate borrowed/reader
 * inputs. They must not retain chunk pointers. Pure job/grant checks run while
 * actual provider_read is busy; full validation runs only outside callbacks.
 * Cancellation during original metadata waits uses a scoped observer/deadline
 * latch so existing transport cleanup drains/destroys pending callbacks before
 * owned state leaves scope. The same absolute deadline and observer are restored.
 * Limits cover this instance's body attempts/delivery/calls, not underlying
 * metadata/parser rescans, SDK allocation or cumulative work across new instances.
 */
int bw_derived_local_read(struct bw_derived_local *,uint32_t,bw_source_consume_fn,void *);
int bw_derived_local_stats(struct bw_derived_local *,struct bw_derived_local_stats *);
/* Releases only owned plan/state; never destroys borrowed sessions. Busy calls
 * refuse and poison without freeing beneath an active callback. */
int bw_derived_local_destroy(struct bw_derived_local *);
#endif
