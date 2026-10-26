#ifndef BW_CONTAINER_SMB_H
#define BW_CONTAINER_SMB_H
#include "container_source.h"
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

/* Borrows one already connected SMB context; owns one O_RDONLY handle.
 * Keep at a stable address and serialize calls. The remote file must remain
 * unchanged. Named/held inode, size, type, mtime and ctime are checked before
 * and after every public container operation. These metadata checks are not
 * cryptographic authentication against a malicious or non-coherent server.
 * No share, source, mount, game or console-cache mutation is performed.
 */
enum bw_container_smb_failure_operation {
    BW_IMAGE_FAILURE_NONE=0,BW_IMAGE_FAILURE_ADMIT_STAT,BW_IMAGE_FAILURE_ADMIT_METADATA,
    BW_IMAGE_FAILURE_OPEN,BW_IMAGE_FAILURE_FSTAT,BW_IMAGE_FAILURE_GENERATION_STAT,
    BW_IMAGE_FAILURE_GENERATION_METADATA,BW_IMAGE_FAILURE_READ_BOUNDS,
    BW_IMAGE_FAILURE_MAX_READ,BW_IMAGE_FAILURE_PREAD,BW_IMAGE_FAILURE_CLOSE
};
struct bw_container_smb {
    struct bw_container container;
    struct smb2_context *smb;
    struct smb2_context **transport_owner;
    struct smb2fh *handle;
    struct smb2_stat_64 admitted;
    char path[1024];
    uint64_t read_calls,read_bytes,generation_checks;
    int failed,last_transport_error;
    /* Observation only: retain the FIRST refused operation independently of
     * last_transport_error's existing context-drain/close behavior. A NULL
     * open has a synthesized -1, not a known integer -EPERM result. */
    int first_failure_result;
    enum bw_container_smb_failure_operation first_failure_operation;
    unsigned first_failure_seen,first_failure_result_known;
};
int bw_container_smb_open(struct bw_container_smb *,struct smb2_context *,const char *);
/* Same worker owns *transport_owner through close. A failed synchronous SMB
 * call can retain a destination: destroy/drain that context and clear the
 * caller's pointer before returning failure or freeing any decoder workspace.
 * The pointer itself must remain alive and exclusively owned through close. */
int bw_container_smb_open_owned(struct bw_container_smb *,struct smb2_context **transport_owner,const char *);
int bw_container_smb_close(struct bw_container_smb *);
#endif
