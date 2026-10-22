/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_IDLE_LOG_CACHE_H
#define BW_IDLE_LOG_CACHE_H
#include "startup_runtime_idle.h"

struct bw_idle_log_cache;
enum bw_idle_log_cache_result {
    BW_IDLE_CACHE_OK=0,
    BW_IDLE_CACHE_REFUSED=-1,
    BW_IDLE_CACHE_RELEASE_UNKNOWN=-2
};
struct bw_idle_log_cache_stats {
    unsigned evaluations,hits,misses,invalidations,open_anchor,poisoned,release_unknown;
};
struct bw_idle_log_cache_release {
    unsigned anchor_close_attempted,release_unknown;
};
/* Optional one-operation owner, created/evaluated/closed by the same joined
 * worker. Copy only the nonce, path and absolute deadline; never retain a
 * callback/opaque pointer. Each evaluation clips its unchanged caller deadline
 * to the immutable operation ceiling; a fresh short call does not renew it.
 * No cross-operation, restart or global reuse.
 * Concurrent destruction/access is forbidden: join the owner before disposal.
 */
int bw_idle_log_cache_begin(struct bw_idle_log_cache **,const char operation[33],
    double operation_deadline,const struct bw_startup_idle_request *);
int bw_src_idle_evaluate_cached(const struct bw_startup_idle_request *,
    const struct bw_src_idle_ops *,struct bw_idle_log_cache *,const char operation[33]);
int bw_startup_idle_ps5_cached(const struct bw_startup_idle_request *,
    struct bw_idle_log_cache *,const char operation[33]);
int bw_idle_log_cache_stats(const struct bw_idle_log_cache *,struct bw_idle_log_cache_stats *);
/* Owner-thread close consumes each numeric descriptor at most once. Always
 * retain the mandatory release report in the outer operation. -2 means some
 * close/fclose was ambiguous, even if this object is freed; it NEVER proves
 * release or permits another operation/mutation. Do not retry that numeric FD.
 * Wrong-thread/reentrant close refuses and leaves *cache owned by its worker.
 */
int bw_idle_log_cache_close(struct bw_idle_log_cache **,struct bw_idle_log_cache_release *);
#endif
