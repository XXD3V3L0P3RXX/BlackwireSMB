#ifndef BW_STARTUP_TRACE_H
#define BW_STARTUP_TRACE_H
#include "manifest.h"
#include "read_plan.h"
#include <stddef.h>
#include <stdint.h>

#define BW_STARTUP_TRACE_CAPACITY 16384u
#define BW_STARTUP_TRACE_HEADER 256u
#define BW_STARTUP_TRACE_RECORD 40u
#define BW_STARTUP_TRACE_MAX_BYTES (BW_STARTUP_TRACE_HEADER+BW_STARTUP_TRACE_CAPACITY*BW_STARTUP_TRACE_RECORD+32u)
#define BW_STARTUP_TRACE_MAX_WINDOW_US UINT64_C(600000000)
#define BW_STARTUP_TRACE_SEALED 1u
#define BW_STARTUP_TRACE_CAPACITY_END 2u
#define BW_STARTUP_TRACE_WINDOW_END 4u

struct bw_startup_trace_identity {uint8_t run[16],source[32],map[32],receipt[32];};
struct bw_startup_trace_event {uint32_t file_id,kind;uint64_t offset,length,elapsed_us,operation;};
struct bw_startup_trace {
    struct bw_startup_trace_identity identity;
    struct bw_manifest manifest;
    const void *map;size_t map_size;
    uint64_t start_us,window_us,operations,delivered_bytes;
    uint32_t count,capacity,flags,valid,active;
    struct bw_startup_trace_event events[BW_STARTUP_TRACE_CAPACITY];
};

/* Metadata only. No paths, addresses, account labels or credentials enter the
 * format. Caller holds immutable manifest bytes until seal. Source/receipt
 * inputs are the exact validated reader selection's lowercase SHA-256 hex. */
int bw_startup_trace_identity(struct bw_startup_trace_identity *,const char *run,
    const char *source_sha256,const char *receipt_sha256,const void *map,size_t map_size);
int bw_startup_trace_init(struct bw_startup_trace *,const struct bw_startup_trace_identity *,
    const void *map,size_t map_size,uint64_t start_us,uint64_t window_us,uint32_t capacity);

/* Call ONLY after the reader's native-completion, destination and post-native
 * identity checks succeed. AIO requires native return 0 and aio_verified=1,
 * meaning every staged piece has a successful exact-length completion.
 * Pread/preadv record precisely the positive returned prefix, including short
 * reads. Forwarded LOCAL/EOF/zero/error operations emit nothing. No allocations
 * or I/O occur here. Capacity/window exhaustion stops capture, not the reader.
 * Malformed ranges, changed identity or overflow invalidate the whole trace. */
int bw_startup_trace_delivered(struct bw_startup_trace *,const struct bw_startup_trace_identity *,
    const struct bw_read_plan *,int64_t returned,unsigned aio_verified,uint64_t now_us);

/* Fixed little-endian format + SHA-256 footer. Rehashes the current map and
 * refuses changed run/source/receipt/map, invalid state or undersized output.
 * Never emits a partial encoded trace; used=0 on failure. Output must not alias
 * the trace or borrowed map. Disk persistence is separate and bounded. */
int bw_startup_trace_seal(struct bw_startup_trace *,const struct bw_startup_trace_identity *,
    const void *current_map,size_t map_size,void *output,size_t capacity,size_t *used);
#endif
