#ifndef BW_SHADOWMOUNT_CLIENT_H
#define BW_SHADOWMOUNT_CLIENT_H
#include <stddef.h>
#include <stdint.h>
enum bw_sm_operation {BW_SM_VERSION,BW_SM_MANUAL_LIST,BW_SM_MANUAL_ADD,BW_SM_SCAN,BW_SM_MANUAL_REMOVE};
struct bw_sm_reply {unsigned present,changed,queued;uint32_t count,capabilities;unsigned remove_supported,uninstall_supported,settings_supported,unmount_supported;};
/* These five registration operations expose no generic URL, setting update,
 * game launch, source delete or private bridge command. Owned cleanup has a
 * separate narrow API below; it is never a registration fallback. */
int bw_sm_reply_parse(enum bw_sm_operation,const char *,const void *,size_t,struct bw_sm_reply *);
struct bw_sm_connection {uint16_t port;int (*current)(void *);void *opaque;};
/* Synchronous bounded HTTP/1.0 client to 127.0.0.1 only. port=0 uses 10101.
 * One request per call; no mutation retry after any partial send/unknown result.
 * Host tests can supply a temporary loopback fixture port. */
int bw_sm_post(const struct bw_sm_connection *,enum bw_sm_operation,const char *,struct bw_sm_reply *);
/* Fixed observations used by the owned-registration reconciler. These return
 * an owned exact JSON document, not a success/ownership/absence assertion. */
enum bw_sm_observation { BW_SM_OBSERVE_GAMES, BW_SM_OBSERVE_MANUAL_PATHS, BW_SM_OBSERVE_SETTINGS };
struct bw_sm_document {unsigned char *bytes;size_t size;};
int bw_sm_observe(const struct bw_sm_connection *,enum bw_sm_observation,struct bw_sm_document *);
void bw_sm_document_clear(struct bw_sm_document *);
/* Narrow normal title uninstall. The cleanup ledger/idle reservation is the
 * authority; transport completion only acknowledges the request. It never
 * proves removal. Exactly one request; callers must persist pending first and
 * must not retry after an incomplete or refused response. No source deletion,
 * arbitrary URL, database write or settings mutation is exposed. */
struct bw_sm_uninstall_reply {unsigned requested,scan_queued,scan_deferred;
    unsigned response_complete,http_status,api_status,refused_before_platform;
    char error_reason[64],error[256];};
int bw_sm_uninstall_reply_parse(const char[10],const void *,size_t,struct bw_sm_uninstall_reply *);
int bw_sm_uninstall(const struct bw_sm_connection *,const char[10],struct bw_sm_uninstall_reply *);
/* Supported normal runtime release, never a raw syscall or force operation.
 * Authority/one-shot consumption belongs to the caller. A complete failure
 * may follow partial layer release and never permits an automatic retry. */
struct bw_sm_unmount_reply {unsigned released,response_complete,http_status,api_status;
    char error_reason[64],error[256];};
int bw_sm_unmount_reply_parse(const char[10],unsigned,const void *,size_t,struct bw_sm_unmount_reply *);
int bw_sm_uninstall_error_parse(unsigned,const void *,size_t,struct bw_sm_uninstall_reply *);
int bw_sm_unmount(const struct bw_sm_connection *,const char[10],struct bw_sm_unmount_reply *);
/* Paired dependency: fixed Blackwire Media registration only. Capability is
 * observed before owned file preparation. Registration is one request and
 * must never be automatically replayed after any response or transport error. */
struct bw_sm_media_reply {unsigned registered,registration_attempted,platform_status_present;
    int32_t platform_status,api_status;unsigned response_complete,http_status;char reason[64],error[128];};
int bw_sm_media_capability(const struct bw_sm_connection *);
int bw_sm_media_reply_parse(const void *,size_t,struct bw_sm_media_reply *);
int bw_sm_media_register(const struct bw_sm_connection *,struct bw_sm_media_reply *);
#endif
