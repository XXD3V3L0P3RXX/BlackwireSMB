#ifndef BW_NATIVE_PROCESS_RESULT_H
#define BW_NATIVE_PROCESS_RESULT_H
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
/* Read-only exact-PID sysctl result, before decoding a nonempty public record.
 * This console returns CF+ESRCH for a missing PID, not a zero-length success. */
static inline int bw_native_process_result(uint64_t value,unsigned carry,size_t size,size_t capacity){
    if(!capacity||carry>1||size>capacity)return -1;
    if(carry)return value==ESRCH?0:-1;
    if(value)return -1;
    return size?1:0;
}
#endif
