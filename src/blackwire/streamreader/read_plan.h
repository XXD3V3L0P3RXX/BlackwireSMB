#ifndef BW_READ_PLAN_H
#define BW_READ_PLAN_H
#include <stddef.h>
#include <stdint.h>

#define BW_READ_MAX_ITEMS 128u
#define BW_READ_MAX_IOV 1024u
#define BW_READ_MAX_BYTES (64ULL * 1024 * 1024)
#define BW_READ_STAGE_BYTES (256ULL * 1024 * 1024)
#define BW_READ_STAGE_ALIGNMENT 65536ULL
enum bw_read_kind { BW_READ_AIO_MULTIPLE=0, BW_READ_AIO_SINGLE=1,
                    BW_READ_PREAD=2, BW_READ_PREADV=3 };
enum bw_read_error { BW_READ_OK=0, BW_READ_INVALID, BW_READ_LIMIT,
                     BW_READ_OVERFLOW, BW_READ_IDENTITY, BW_READ_RESULT_ALIAS };
struct bw_read_aio {
    int64_t offset;
    uint64_t length, buffer, result;
    int32_t fd, pad;
};
struct bw_read_iovec { uint64_t buffer, length; };
struct bw_read_file { uint32_t file_id; uint64_t size; };
/* Resolve returns 1 for an identified streamed regular file, 0 for an ordinary
 * local file, -1 on lookup/identity failure. No persistent integer-FD cache. */
typedef int (*bw_read_resolve)(void *,int32_t,struct bw_read_file *);
struct bw_read_piece {
    uint32_t file_id, index;
    uint64_t source_offset, length, stage_offset;
};
struct bw_read_plan {
    enum bw_read_kind kind;
    uint32_t count, native_id_count, piece_count;
    int forward_unchanged;
    int32_t native_fd;
    int64_t native_offset;
    uint64_t native_buffer, native_length, stage_bytes;
    struct bw_read_piece pieces[BW_READ_MAX_ITEMS];
    struct bw_read_aio aio[BW_READ_MAX_ITEMS];
    struct bw_read_iovec iov[BW_READ_MAX_IOV];
};
_Static_assert(sizeof(struct bw_read_aio)==40,"native AIO request stride");
_Static_assert(offsetof(struct bw_read_aio,fd)==32,"native AIO FD");
_Static_assert(sizeof(struct bw_read_iovec)==16,"native iovec stride");

/* Pure planning only; no memory at buffer/result addresses is accessed. All
 * inputs have already been copied from the game. Caller validates mappings,
 * readable/writable pointers, real stage FD identity and native ABI provenance.
 * Any error zeroes out; no partial plan can be staged/submitted.
 * Stream requests are limited to 64MiB declared bytes, even when EOF clamps the
 * effective length. Local operations retain their native arguments unchanged.
 * Zero-length/EOF stream operations retain the real FD and native EOF handling.
 */
enum bw_read_error bw_read_plan_aio(struct bw_read_plan *out,enum bw_read_kind kind,
                                    const struct bw_read_aio *input,uint32_t count,
                                    int32_t stage_fd,bw_read_resolve resolve,void *context);
enum bw_read_error bw_read_plan_pread(struct bw_read_plan *out,int32_t fd,uint64_t buffer,
                                      uint64_t length,int64_t offset,int32_t stage_fd,
                                      bw_read_resolve resolve,void *context);
enum bw_read_error bw_read_plan_preadv(struct bw_read_plan *out,int32_t fd,
                                       const struct bw_read_iovec *input,uint32_t count,
                                       int64_t offset,int32_t stage_fd,
                                       bw_read_resolve resolve,void *context);
const char *bw_read_error_string(enum bw_read_error error);

/* Execute each plan as ONE saved native call of its original kind, preserving
 * caller priority, destination/result pointers and output ID storage. Multiple
 * yields count genuine IDs; Single yields ONE genuine group ID. Poll/wait that
 * native ID set, never manufacture IDs or delete caller-owned IDs. Pin staging
 * until native completion; a failed/partial submission may still own it.
 * For preadv use native_fd/native_offset and the copied, EOF-clamped iov array:
 * one source piece forms a contiguous byte stream, with scatter order intact.
 * Up to 1,024 vectors need 16 KiB of copied game-side array storage by themselves;
 * ensure the request page also fits its protocol header and scratch region.
 * Check each staged AIO result for its piece's effective length after completion.
 * Result-slot aliasing is refused when staging: native semantics for shared
 * result structs are unproven, and one final struct cannot validate every item.
 */
#endif
