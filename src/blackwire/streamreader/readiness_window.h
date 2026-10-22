#ifndef BW_READINESS_WINDOW_H
#define BW_READINESS_WINDOW_H
#include <stdint.h>

#define BW_READINESS_CONFIRM_US UINT64_C(5000)
struct bw_readiness_identity {
    int32_t pid;
    uint32_t ampr_handle,kernel_handle;
    uint64_t ampr_base,kernel_base,multiple_export,trampoline;
};
struct bw_readiness_window {
    struct bw_readiness_identity identity;
    uint64_t first_us,last_us;
    int present;
};
/* Pure observation bookkeeping. Pass only a completed, independently validated
 * snapshot. NULL/invalid input resets confirmation. Two equal observations at
 * least 5 ms apart are required; this is not a lock or proof of no intervening
 * ABA/other-PRX activity. No waits, allocations or process access occur here.
 */
void bw_readiness_window_reset(struct bw_readiness_window *);
int bw_readiness_window_observe(struct bw_readiness_window *,const struct bw_readiness_identity *,uint64_t);
#endif
