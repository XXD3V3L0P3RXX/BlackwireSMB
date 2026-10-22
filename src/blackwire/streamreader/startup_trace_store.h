#ifndef BW_STARTUP_TRACE_STORE_H
#define BW_STARTUP_TRACE_STORE_H
#include <stddef.h>
/* One fixed exclusive leaf, never append/overwrite/rotate/delete. Existing or
 * partial captures require separate operator review. Max disk length is the
 * bounded trace size. Directory must be ordinary, canonical and held stable.
 * 0=stored and readback/fsync verified, 1=existing file preserved, -1=failure.
 * Caller invokes after the game/reader releases its runtime, never per read. */
int bw_startup_trace_store(const char *directory,const void *encoded,size_t size);
#endif
