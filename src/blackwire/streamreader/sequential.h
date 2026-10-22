#ifndef BW_SEQUENTIAL_H
#define BW_SEQUENTIAL_H
#include <stddef.h>
#include <stdint.h>
#include "read_plan.h"

/* Isolated candidate. The normal reader deliberately does not call this module.
 * A global in-process gate is insufficient unless every cursor/lifetime path
 * and every holder of the open description is controlled. Imports do not prove
 * that condition. A fixture with private descriptors can establish it. */
enum bw_seq_kind { BW_SEQ_READ=0, BW_SEQ_READV=1 };
enum bw_seq_error { BW_SEQ_OK=0, BW_SEQ_INVALID, BW_SEQ_UNADMITTED,
    BW_SEQ_IDENTITY, BW_SEQ_LIMIT, BW_SEQ_STAGE, BW_SEQ_NATIVE,
    BW_SEQ_CURSOR_CHANGED, BW_SEQ_COMMIT };
struct bw_seq_identity { uint64_t device,inode,size; uint32_t file_id; int stream,regular; };
struct bw_seq_admission {
    int gate_held,all_cursor_paths_gated,all_lifetime_paths_gated;
    int description_private_to_controlled_threads,native_same_calling_thread;
};
struct bw_seq_request {
    enum bw_seq_kind kind;
    int32_t fd;
    uint64_t buffer,length;
    const struct bw_read_iovec *iov;
    int32_t count;
};
struct bw_seq_plan {
    enum bw_seq_kind kind;
    int32_t original_fd,native_fd;
    int forward_unchanged;
    uint64_t source_offset,length,buffer;
    int32_t count;
    struct bw_read_iovec iov[BW_READ_MAX_IOV];
};
struct bw_seq_outcome {
    int64_t returned;
    int native_errno,native_read_calls,committed;
    uint64_t before_offset,after_offset;
};
struct bw_seq_ops {
    void *context;
    /* begin/end retain one gate through tell, staging, read and offset commit.
     * Native helpers must be saved originals, never a detoured public export.
     * This contract must be independently admitted before game integration. */
    int (*begin)(void *,int32_t,struct bw_seq_admission *);
    void (*end)(void *);
    int (*identify)(void *,int32_t,struct bw_seq_identity *);
    int64_t (*seek)(void *,int32_t,int64_t,int,int *);
    int64_t (*read)(void *,enum bw_seq_kind,int32_t,uint64_t,uint64_t,
                    const struct bw_read_iovec *,int32_t,int *);
    int (*stage)(void *,const struct bw_seq_identity *,uint64_t,uint64_t);
    int32_t stage_fd;
};

enum bw_seq_error bw_seq_plan(struct bw_seq_plan *,const struct bw_seq_request *,
    const struct bw_seq_identity *,const struct bw_seq_admission *,int64_t,int32_t);
enum bw_seq_error bw_seq_execute(const struct bw_seq_ops *,const struct bw_seq_request *,struct bw_seq_outcome *);
/* All explicit lseek operations use the same gate as execute. Non-admission
 * forwards no stream operation; failure never returns sparse placeholder data. */
enum bw_seq_error bw_seq_seek(const struct bw_seq_ops *,int32_t,int64_t,int,struct bw_seq_outcome *);
const char *bw_seq_error_string(enum bw_seq_error);
#endif
