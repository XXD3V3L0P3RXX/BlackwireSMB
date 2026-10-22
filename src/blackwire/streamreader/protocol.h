#ifndef BW_STREAM_PROTOCOL_H
#define BW_STREAM_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

enum { BW_IDLE=0, BW_PENDING=1, BW_COMPLETE=2, BW_OWNED=3,
       BW_CALL=6, BW_CALLED=7 };
struct bw_request {
  uint32_t state, unused;
  uint64_t requests, count;
  int32_t error;
  uint32_t poison;
  uint64_t priority, timeout_ticks, ids;
  uint64_t function, args[6], result;
};
struct bw_aio_item {
  int64_t offset;
  uint64_t length, buffer, result;
  int32_t fd, pad;
};
struct bw_aio_result { int64_t returned; uint32_t state, pad; };
_Static_assert(offsetof(struct bw_request, requests)==8, "request pointer");
_Static_assert(offsetof(struct bw_request, count)==16, "request count");
_Static_assert(offsetof(struct bw_request, error)==24, "return code");
_Static_assert(offsetof(struct bw_request, poison)==28, "poison");
_Static_assert(offsetof(struct bw_request, priority)==32, "priority");
_Static_assert(offsetof(struct bw_request, timeout_ticks)==40, "timeout");
_Static_assert(offsetof(struct bw_request, ids)==48, "caller IDs");
_Static_assert(offsetof(struct bw_request, function)==56, "RPC function");
_Static_assert(offsetof(struct bw_request, args)==64, "RPC arguments");
_Static_assert(offsetof(struct bw_request, result)==112, "RPC result");
_Static_assert(sizeof(struct bw_aio_item)==40, "native request stride");
_Static_assert(sizeof(struct bw_aio_result)==16, "native result stride");
#endif
