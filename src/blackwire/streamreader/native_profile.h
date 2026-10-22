#ifndef BW_NATIVE_PROFILE_H
#define BW_NATIVE_PROFILE_H

#include <stddef.h>
#include <stdint.h>

/* Only the retained Ampr binary and October 17 RE7/Village libkernel layout.
 * This is not a portable firmware ABI. No alias lookup or game call,
 * process operation, allocation, or write is performed by this module. */
#define BW_NATIVE_AMPR_SPAN UINT64_C(0x18d26000)

enum bw_native_role {
    BW_NATIVE_AIO_SINGLE,
    BW_NATIVE_AIO_MULTIPLE,
    BW_NATIVE_AIO_POLL,
    BW_NATIVE_PREAD,
    BW_NATIVE_PREADV,
    BW_NATIVE_READ,
    BW_NATIVE_READV,
    BW_NATIVE_LSEEK,
    BW_NATIVE_CLOSE,
    BW_NATIVE_FSTAT,
    BW_NATIVE_OPEN,
    BW_NATIVE_ERRNO, /* direct __error export; no Ampr slot */
    BW_NATIVE_ROLE_COUNT
};

enum bw_native_error {
    BW_NATIVE_OK = 0,
    BW_NATIVE_INVALID,
    BW_NATIVE_MODULE,
    BW_NATIVE_READ_FAILED,
    BW_NATIVE_LAYOUT,
    BW_NATIVE_SYMBOL,
    BW_NATIVE_EXPORT,
    BW_NATIVE_RANGE,
    BW_NATIVE_PREFIX,
    BW_NATIVE_JUMP,
    BW_NATIVE_NOT_READY,
    BW_NATIVE_CHANGED,
    BW_NATIVE_STATE_MISMATCH
};

/* Return zero only after filling all n bytes. The callback must read the same
 * selected game process for the complete check. It must not synthesize data. */
typedef int (*bw_native_read_fn)(void *context, uint64_t address, void *dest, size_t n);

struct bw_native_profile {
    bw_native_read_fn read;
    void *context;
    uint64_t ampr_base;
    uint64_t libkernel_base;
    /* This evidence establishes libkernel.sprx only, not libkernel_sys.sprx. */
    const char *libkernel_module;
};

const char *bw_native_symbol(enum bw_native_role role);
uint64_t bw_native_slot_offset(enum bw_native_role role); /* zero for ERRNO/invalid role */
const char *bw_native_error_text(enum bw_native_error error);

/* Check every fixed instruction window, including all role slot loads.
 * Success establishes those windows only, not a full module file identity. */
enum bw_native_error bw_native_check_ampr(const struct bw_native_profile *profile);

/* Includes check_ampr. symbol and resolved_export must be independently
 * resolved for this role in libkernel.sprx in the same selected game PID.
 * Requires the exact observed export offset, raw stub or trampoline prefix,
 * fixed branch offset and exact return entry+N. Does not read/replace a slot.
 * The caller must read the role's fixed slot and recheck it before any later
 * mutation. ERRNO has no slot and requires target == the resolved export,
 * with the complete 104-byte pure pointer-computation body matching.
 * Success is read-only evidence, not a lifetime/TOCTOU guarantee. */
enum bw_native_error bw_native_check_target(const struct bw_native_profile *profile,
    enum bw_native_role role, const char *symbol, uint64_t resolved_export,
    uint64_t candidate_target);

/* Read-only observation of the retained installer's state. Field widths and
 * offsets are proven by retained instruction bytes and its RW PT_LOAD, not a
 * newer source enum. Output is zeroed first, then filled with the latest full
 * snapshot, including on NOT_READY/CHANGED. No native function is executed. */
struct bw_native_aio_state {
    uint32_t lock;
    uint32_t hooks_installed;
    uint64_t trampoline;
    uint64_t original_slot;
    uint8_t multiple_installed;
};

/* expected_native=expected_hook=0 admits only a completed unhooked state and
 * discovers its validated native trampoline. A nonzero expected_native pins
 * that trampoline. A nonzero expected_hook requires expected_native and admits
 * only that exact caller-owned value in the original slot (post-hook monitor).
 *
 * Checks full installer/refresh windows, two equal state snapshots around
 * native validation, lock==0, both installed flags==1, and exact slot identity.
 * NOT_READY means a stable locked/incomplete/rolled-back state; CHANGED means
 * unequal snapshots; STATE_MISMATCH means a stable impossible/foreign tuple.
 * All are refusals. Never overwrite zero or use these observations as a lock.
 *
 * Caller must resolve the same current game PID/module handle/base before and
 * after this check and recheck immediately before mutation. These snapshots
 * cannot detect an ABA transition or certify another PRX's initialization.
 * This does not repair PRX_PROCESS_STARTUP_FAILURE or establish read coverage. */
enum bw_native_error bw_native_check_aio_ready(const struct bw_native_profile *profile,
    uint64_t resolved_multiple_export, uint64_t expected_native,
    uint64_t expected_hook, struct bw_native_aio_state *state);

#endif
