#ifndef BW_PERF_H
#define BW_PERF_H
#include <stdint.h>

/* Diagnostic counters only: no clock, allocation, I/O or synchronization.
 * Totals/counts saturate so arbitrarily long sessions cannot wrap. */
struct bw_perf_counter {
    uint64_t count,total_us,max_us,failures,invalid_clock;
};
static inline uint64_t bw_perf_add(uint64_t a,uint64_t b) {
    return b>UINT64_MAX-a?UINT64_MAX:a+b;
}
static inline void bw_perf_record(struct bw_perf_counter *counter,uint64_t start,
                                   uint64_t end,int failed) {
    counter->count=bw_perf_add(counter->count,1);
    if (failed) counter->failures=bw_perf_add(counter->failures,1);
    if (!start || !end || end<start) {
        counter->invalid_clock=bw_perf_add(counter->invalid_clock,1); return;
    }
    uint64_t elapsed=end-start;
    counter->total_us=bw_perf_add(counter->total_us,elapsed);
    if (elapsed>counter->max_us) counter->max_us=elapsed;
}
#endif
