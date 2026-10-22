#ifndef BW_PATH_PROTOCOL_H
#define BW_PATH_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

enum { BW_PATH_IDLE=0, BW_PATH_PENDING=1, BW_PATH_COMPLETE=2,
       BW_PATH_OWNED=3, BW_PATH_CALL=6, BW_PATH_CALLED=7 };
enum { BW_PATH_AIO_MULTIPLE=0, BW_PATH_AIO_SINGLE=1,
       BW_PATH_PREAD=2, BW_PATH_PREADV=3,
       BW_PATH_READ=4, BW_PATH_READV=5, BW_PATH_LSEEK=6, BW_PATH_ROUTE_COUNT=7 };

/* First 120 bytes retain the original bw_request RPC layout. The controller
 * must finish the complete native operation before publishing COMPLETE.
 * Error poisons the slot permanently; only a new game/slot starts fresh. */
struct bw_path_request {
    uint32_t state, kind;
    uint64_t requests, count;
    int32_t error;
    uint32_t poison;
    uint64_t priority, timeout_ticks, ids;
    uint64_t function, args[6], result;
    uint64_t final_return, original_args[6];
};
_Static_assert(offsetof(struct bw_path_request,state)==0,"state");
_Static_assert(offsetof(struct bw_path_request,kind)==4,"kind");
_Static_assert(offsetof(struct bw_path_request,requests)==8,"requests alias");
_Static_assert(offsetof(struct bw_path_request,count)==16,"count alias");
_Static_assert(offsetof(struct bw_path_request,error)==24,"old error");
_Static_assert(offsetof(struct bw_path_request,poison)==28,"poison");
_Static_assert(offsetof(struct bw_path_request,priority)==32,"priority alias");
_Static_assert(offsetof(struct bw_path_request,timeout_ticks)==40,"deadline");
_Static_assert(offsetof(struct bw_path_request,ids)==48,"IDs alias");
_Static_assert(offsetof(struct bw_path_request,function)==56,"RPC function");
_Static_assert(offsetof(struct bw_path_request,args)==64,"RPC arguments");
_Static_assert(offsetof(struct bw_path_request,result)==112,"RPC result");
_Static_assert(offsetof(struct bw_path_request,final_return)==120,"full return");
_Static_assert(offsetof(struct bw_path_request,original_args)==128,"original arguments");
_Static_assert(sizeof(struct bw_path_request)==176,"request size");

/* Relocation-free blob literals to patch before copying/protecting code:
 * bw_path_slot: u64 slot address; bw_path_kind: u32 kind;
 * bw_path_failure_return: u64 (-1 for POSIX, SCE EIO for AIO);
 * bw_path_errno_fn: optional validated int *(*)(void) returning this thread's
 * errno cell. Failure writes EIO=5; AIO should leave this literal zero.
 */
extern const unsigned char bw_path_hook_start[],bw_path_hook_end[];
extern const unsigned char bw_path_slot[],bw_path_kind[];
extern const unsigned char bw_path_failure_return[],bw_path_errno_fn[];
#endif
