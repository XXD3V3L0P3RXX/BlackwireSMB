/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_HOME_PROTOCOL_H
#define BW_HOME_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BW_HOME_PROTOCOL "blackwire-home/1"
#define BW_HOME_REQUEST_LIMIT 512u
struct bw_home_claim {int32_t pid;uint64_t deadline_us;char nonce[33];uint8_t map_sha256[32];};
/* This is a cooperative local startup request, not peer-to-PID authentication.
 * No path/title/config/credential supplied by the caller can select a source. */
int bw_home_claim_parse(struct bw_home_claim *,const void *,size_t);
int bw_home_claim_format(const struct bw_home_claim *,char *,size_t,size_t *);
int bw_home_ack_parse(const void *,size_t,const char nonce[33],char run[33]);
int bw_home_ack_format(const char nonce[33],const char run[33],char *,size_t,size_t *);
#ifdef __cplusplus
}
#endif
#endif
