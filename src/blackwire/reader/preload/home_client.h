/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_HOME_CLIENT_H
#define BW_HOME_CLIENT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* One fixed127.0.0.1:5918 POST, no redirects/retries/credentials/loader. All
 * socket I/O consumes the caller's one absolute monotonic startup deadline. */
int bw_home_client_claim(int32_t,const uint8_t[32],uint64_t,char[33]);
/* Numeric first-failure snapshot for this module's one startup claim. No
 * request/response bytes, nonce, digest, credentials, or new I/O are retained.
 * Zero stage means success/no failure. Reset only by the next claim call. */
enum bw_home_client_stage {
    BW_HC_NONE=0,BW_HC_ARGUMENT,BW_HC_CLOCK,BW_HC_DEADLINE,BW_HC_NONCE,
    BW_HC_SOCKET,BW_HC_FCNTL_GET,BW_HC_FCNTL_SET,BW_HC_NOSIGPIPE,
    BW_HC_CONNECT,BW_HC_CONNECT_RESULT,BW_HC_POLL,BW_HC_SEND,BW_HC_RECEIVE,
    BW_HC_REQUEST_FORMAT,BW_HC_HTTP_STATUS,BW_HC_HTTP_HEADER,
    BW_HC_CONTENT_TYPE,BW_HC_CONTENT_LENGTH,BW_HC_BODY,BW_HC_EOF,
    BW_HC_CLOSE,BW_HC_CLAIM_FORMAT,BW_HC_ACK,BW_HC_NONBLOCK
};
enum bw_home_client_io {BW_HC_IO_CONNECT=1,BW_HC_IO_SEND_HEADER,BW_HC_IO_SEND_BODY,
    BW_HC_IO_HEADER,BW_HC_IO_BODY,BW_HC_IO_EOF};
struct bw_home_client_failure {unsigned stage,detail;int64_t raw;int error;unsigned http_status;};
void bw_home_client_diagnostic(struct bw_home_client_failure *);
/* Full stable manifest bytes, not its noncryptographic wire checksum. */
void bw_home_client_map_sha256(const void *,size_t,uint8_t[32]);
#ifdef __cplusplus
}
#endif
#endif
