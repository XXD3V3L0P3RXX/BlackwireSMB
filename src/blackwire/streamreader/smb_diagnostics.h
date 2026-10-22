#ifndef BW_SMB_DIAGNOSTICS_H
#define BW_SMB_DIAGNOSTICS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Diagnostic-only private-ABI adapter. Link ONLY with the archive below,
 * compiled from /tmp/libsmb2-libsmb2-6.2 using its build/config.h. The apparent
 * public library version is insufficient to identify this build. Verify the
 * archive and header hashes before compiling; never use guessed offsets.
 */
#define BW_SMB_DIAGNOSTICS_LIB_SHA256 \
    "095af775130eeba4d2fa5f411abaf90d4d884f53fcd4d000f8586b64c1fae711"
#define BW_SMB_DIAGNOSTICS_PRIVATE_SHA256 \
    "7f8d463441ffcddf12c51ed3961d54a3b83c6938ec06ae8dfdd61d5662dc0c88"
#define BW_SMB_DIAGNOSTICS_CONFIG_SHA256 \
    "46f748a8241f84b20c03fa4d799b2a7d020583786dbee3877dca71b167b57a0f"

struct smb2_context;

/* result/errno_value are the raw return and captured errno for this individual
 * query. A failed query may return a positive SDK error with errno_value == 0.
 * Value is meaningful ONLY when known == 1. No SO_ERROR query is made, because
 * reading that option would clear a pending socket error.
 */
struct bw_smb_diag_option {
    int known;
    int value;
    int result;
    int errno_value;
};

struct bw_smb_diag_snapshot {
    int context_known;
    int signing_flag;
    int encryption_flag;
    int credits;
    uint16_t dialect;
    uint16_t cipher;
    struct bw_smb_diag_option receive_buffer_bytes;
    struct bw_smb_diag_option send_buffer_bytes;
    struct bw_smb_diag_option tcp_nodelay;
};

/* Call on the owning thread after a successful connection, between SMB calls.
 * It neither connects nor sends an SMB request, and never changes security,
 * socket options, credits, or the context. It does not read/copy credentials or
 * key fields. Flags describe the library context, not a packet capture. Cipher
 * may be negotiated even when encryption_flag == 0.
 *
 * Returns 0 when connected-context fields were read, -1 for a NULL argument or
 * a context without a connected fd/recognized negotiated dialect. Each socket
 * query has independent validity. Caller errno is preserved. A NULL/invalid
 * context yields context_known == 0, never a false claim of unsigned traffic.
 * Do not call concurrently with another user of, or destruction of, the context.
 */
int bw_smb_diag_read(const struct smb2_context *smb,
                     struct bw_smb_diag_snapshot *out);

struct bw_smb_diag_clock {
    int known;
    int result;
    int errno_value;
    uint64_t nanoseconds;
};

struct bw_smb_diag_clocks {
    struct bw_smb_diag_clock monotonic;
    struct bw_smb_diag_clock process_cpu;
};

/* Read CLOCK_MONOTONIC and the calling process's CLOCK_PROCESS_CPUTIME_ID.
 * CPU support is probed at runtime: failure, malformed output, or an absent SDK
 * clock constant remains unknown; wall time is NEVER substituted for CPU time.
 * Header support and successful syntax validation do not establish console
 * clock accuracy. CPU time includes all threads of this process. Compute a
 * delta only when both endpoint samples are known and nondecreasing; a zero
 * CPU delta may reflect coarse accounting. Calls preserve caller errno.
 */
void bw_smb_diag_sample_clocks(struct bw_smb_diag_clocks *out);

#ifdef __cplusplus
}
#endif
#endif
