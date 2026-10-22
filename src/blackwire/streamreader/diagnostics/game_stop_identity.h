#ifndef BW_GAME_STOP_IDENTITY_H
#define BW_GAME_STOP_IDENTITY_H
#include <stddef.h>
#include <stdint.h>
#define BW_STOP_PROFILE_FW UINT32_C(0x13000040)
#define BW_STOP_PROFILE_RECORD 1096u
#define BW_STOP_PROFILE_MAX_AGE_US INT64_C(60000000)
struct bw_stop_identity { int32_t pid; int64_t start_sec,start_usec; char role[20]; };
/* Parse ONLY the empirical public prefix approved for this isolated control.
 * Nonzero return is UNKNOWN; sanitized private pointer fields are not read. */
int bw_stop_identity_parse(const void *,size_t,uint32_t,int32_t,struct bw_stop_identity *);
int bw_stop_identity_admit(const struct bw_stop_identity *,int64_t wall_us);
/* 1=same admitted generation/role, -1=UNKNOWN. Never returns positive exit. */
int bw_stop_identity_match(const struct bw_stop_identity *,const struct bw_stop_identity *);
#endif
