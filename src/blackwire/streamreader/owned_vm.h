#ifndef BW_OWNED_VM_H
#define BW_OWNED_VM_H
#include <stddef.h>
#include <stdint.h>

#define BW_OWNED_VM_MAX_NODES 128u
#define BW_OWNED_VM_MAX_BYTES UINT64_C(65536)

enum bw_owned_vm_error {
    BW_OWNED_VM_OK=0, BW_OWNED_VM_INVALID, BW_OWNED_VM_CONFIG,
    BW_OWNED_VM_PROCESS, BW_OWNED_VM_READ, BW_OWNED_VM_POINTER,
    BW_OWNED_VM_NOT_MAPPED, BW_OWNED_VM_BOUNDS, BW_OWNED_VM_RIGHTS,
    BW_OWNED_VM_CHANGED, BW_OWNED_VM_LIMIT
};
enum bw_owned_vm_stage {
    BW_OWNED_VM_VALIDATE=0, BW_OWNED_VM_PROC, BW_OWNED_VM_VMSPACE,
    BW_OWNED_VM_ROOT, BW_OWNED_VM_ENTRY_BOUNDS, BW_OWNED_VM_CHILD,
    BW_OWNED_VM_PROTECTION, BW_OWNED_VM_COMPARE
};
struct bw_owned_vm_snapshot {
    uint64_t process,vmspace,root,entry,start,end;
    uint32_t nodes;
    uint8_t protection;
};
struct bw_owned_vm_diagnostic {
    enum bw_owned_vm_error error;
    enum bw_owned_vm_stage stage;
    unsigned pass; /* 1 or 2; zero for rejected inputs */
    int callback_errno;
    uint64_t read_address;
    size_t read_size;
    struct bw_owned_vm_snapshot snapshots[2];
};
/* Callbacks return zero only on complete success, preserving the exact SDK
 * errno at that call in error_number. They may only resolve the selected PID
 * or read kernel metadata. No game/physical-memory contents establish rights. */
struct bw_owned_vm_ops {
    void *context;
    int (*process)(void *context,int32_t pid,uint64_t *address,int *error_number);
    int (*read)(void *context,uint64_t address,void *dest,size_t size,int *error_number);
};

/* Retained SDK vm_map_entry layout: left+0x10/right+0x18, bounds+0x20/+0x28,
 * protection byte+0x64; process vmspace+0x200. root_offset must be the SDK's
 * initialized firmware value 0x1c0/0x1c8/0x1d0, not a guessed offset.
 *
 * Only owned, 16KiB-aligned allocations <=64KiB and a nonempty contained user
 * range are admitted. Two independent bounded tree resolutions must identify
 * the same process/vmspace/entry/bounds/protection and full RW coverage. Tree
 * roots may rebalance while resolving the same covering entry. No next-list
 * pointer is read, no retries/writes/allocations occur, and every uncertainty
 * is a refusal. No lock is acquired; ABA and later mapping changes remain
 * caller lifetime limitations. A true map snapshot requires kernel locking. */
enum bw_owned_vm_error bw_owned_vm_check(const struct bw_owned_vm_ops *ops,
    int32_t pid,uint64_t root_offset,uint64_t owned_base,uint64_t owned_size,
    uint64_t address,uint64_t length,struct bw_owned_vm_diagnostic *diagnostic);
const char *bw_owned_vm_error_text(enum bw_owned_vm_error error);
const char *bw_owned_vm_stage_text(enum bw_owned_vm_stage stage);
#endif
