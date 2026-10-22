#ifndef BW_OWNED_VM_WINDOW_H
#define BW_OWNED_VM_WINDOW_H
#include "owned_vm.h"

#define BW_VM_WINDOW_PROBES 32u
#define BW_VM_WINDOW_STABLE_US UINT64_C(5000)
#define BW_VM_WINDOW_MAX_US UINT64_C(250000)

enum bw_vm_window_error { BW_VM_WINDOW_OK=0, BW_VM_WINDOW_INVALID,
    BW_VM_WINDOW_OWNER, BW_VM_WINDOW_PROBE, BW_VM_WINDOW_MAPPING,
    BW_VM_WINDOW_CLOCK, BW_VM_WINDOW_LIMIT };
struct bw_vm_window_state {
    int anchored;
    struct bw_owned_vm_snapshot anchor;
};
struct bw_vm_window_ops {
    void *context;
    int (*owner)(void *context); /* zero only for the same unpoisoned pending request */
    enum bw_owned_vm_error (*probe)(void *context,struct bw_owned_vm_diagnostic *out);
    uint64_t (*now_us)(void *context);
    void (*pause_us)(void *context,unsigned delay);
};
struct bw_vm_window_diagnostic {
    unsigned probes,waits;
    int recovered;
    enum bw_owned_vm_error last_probe;
    struct bw_owned_vm_diagnostic last;
};
/* No reads, writes, allocation, timing or sleep are performed directly here.
 * A complete double-snapshot probe must still pass. Only NOT_MAPPED/CHANGED
 * observations may be resampled, with fresh owner checks before and after.
 * Initial admission and recovery require two identical successful mappings
 * at least5ms apart. A known anchor can never change; roots/nodes may rebalance.
 * Fast-path admission retains the existing complete double-snapshot check.
 * Failure never changes the anchor and never authorizes a protocol mutation.
 * Unlocked observations retain the underlying checker’s ABA/lifetime limits. */
enum bw_vm_window_error bw_vm_window_check(struct bw_vm_window_state *state,
    const struct bw_vm_window_ops *ops,struct bw_vm_window_diagnostic *diagnostic);
const char *bw_vm_window_error_text(enum bw_vm_window_error error);
#endif
