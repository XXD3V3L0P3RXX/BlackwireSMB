#ifndef BW_NATIVE_BACKEND_H
#define BW_NATIVE_BACKEND_H
#include "source_catalog.h"
#include "native_job.h"
#include "native_setup.h"
#include "native_send.h"
#include "reader_runtime.h"
#include "native_game_uninstall.h"
#include "../streamreader/share_config.h"
#define BW_NATIVE_COVER_LIMIT (16u*1024u*1024u)
struct bw_native_cover {uint8_t *bytes;size_t size;};
/* Public-safe discovery diagnostics only. Never retain a config, account,
 * password or libsmb2's free-form error text in this record. */
struct bw_native_check_failure {
    char share[128],stage[32],reason[129],source_reason[129];
    char source[1024],source_stage[32],source_operation[32];
    int container_result,transport_result;
    unsigned transport_result_known;
    int result;uint32_t nt_status;
};
struct bw_native_library {
    struct bw_source_catalog catalog;
    struct bw_native_cover *covers;
    size_t cover_bytes,cover_count;
    struct bw_native_prepared *prepared;
    struct bw_game_uninstall_capability *removal;
    uint32_t requests,entries;
    uint64_t content_bytes;
    struct bw_native_check_failure failure;
};
struct bw_native_prepare_selection {
    /* Controller-owned typed selection. Actual outer image metadata is a
     * catalog observation that must match the freshly opened image; it is not
     * a folder snapshot, byte-verification flag or publication capability. */
    struct bw_provider_binding identity;
    struct bw_source_info boot,param;
    struct smb2_stat_64 image;
    uint64_t logical_boot_size;
    int container_kind;
    char run[33];
};
struct bw_native_play_selection {struct bw_provider_binding identity;struct bw_native_prepared prepared;struct bw_setup_generation connection;char run[33];};
struct bw_native_removal_selection {
    struct bw_provider_binding identity;
    struct bw_native_prepared prepared;
    struct bw_setup_generation connection;
    char operation[33],confirmed_title[10];
    unsigned startup_only; /* Trusted reserved purpose, never client authority. */
};
enum {BW_BACKEND_SOURCE_CHANGED=-3};
/* A worker owns the temporary configuration and result. The production loader
 * reads existing share.conf; discover uses SMB only and may clear credentials.
 * Injected operations are for host tests, not remote/client-supplied callbacks. */
struct bw_native_backend_ops {
    unsigned native_preparation;
    int (*load)(void *,struct bw_share_config *);
    int (*discover)(void *,struct bw_share_config *,struct bw_native_library *);
    int (*read_settings)(void *,struct bw_share_config *,struct bw_setup_generation *);
    int (*save_settings)(void *,const struct bw_share_config *,const struct bw_setup_generation *,const char *,int (*)(void *),void *,struct bw_setup_saved *);
    int (*idle)(void *);
    int (*nonce)(void *,char[33]);
    int (*prepare)(void *,struct bw_share_config *,const struct bw_native_prepare_selection *,int (*)(void *),void *,bw_source_observer_fn,void *,struct bw_native_prepared *);
    int (*inspect)(void *,const struct bw_provider_binding *,struct bw_native_prepared *);
    /* The joined worker retains reader/cache ownership until this returns.
     * observe(error) may occur while the worker still holds an active game. */
    int (*play)(void *,const struct bw_native_play_selection *,int (*)(void *),void *,void (*)(void *,const struct bw_reader_event *),void *);
    int (*close)(void *,const char *);
    /* Explicit reserved removal only. Probe is bounded metadata and is cached
     * at scan/Send/action completion; state polling never calls it. */
    int (*removal_probe)(void *,const struct bw_provider_binding *,const struct bw_native_prepared *,struct bw_game_uninstall_capability *);
    int (*uninstall)(void *,const struct bw_native_removal_selection *,int (*)(void *),void *,void (*)(void *,const struct bw_game_uninstall_progress *),void *,struct bw_game_uninstall_result *);
    int (*restore)(void *,const struct bw_native_removal_selection *,int (*)(void *),void *,struct bw_game_uninstall_result *);
    int (*remove_startup)(void *,const struct bw_native_removal_selection *,int (*)(void *),void *,void (*)(void *,const struct bw_game_uninstall_progress *),void *,struct bw_game_uninstall_result *);
};
#ifdef BW_NATIVE_BACKEND_HOST_TEST
void bw_native_backend_host_paths(const char *,const char *);
#endif
struct bw_native_backend;
struct bw_native_response {int status;const char *type;void *bytes;size_t size;};
struct bw_native_backend *bw_native_backend_create(const struct bw_native_backend_ops *,void *);
/* Refresh is an explicit read-only request. Returns 1 while the retained
 * worker is active. A completed prior worker is joined before replacement. */
int bw_native_backend_refresh(struct bw_native_backend *);
int bw_native_backend_busy(struct bw_native_backend *);
/* Trusted resident wiring, never request input. This callback is used ONLY by
 * connection setup, not preparation/Play/registration/general idle. Install or
 * remove only while the backend has no worker/reservation. The owner must stay
 * alive until matching removal succeeds; removal refuses active setup. Native
 * workers copy the pointer under the backend lock, then release that lock
 * before invoking it, so the callback may take the service's owner lock.
 * The operation argument is the unchanged backend setup-operation counter;
 * the callback must retain one frontend generation across that whole save. */
int bw_native_backend_set_settings_idle(struct bw_native_backend *,int (*)(void *,uint32_t),void *);
int bw_native_backend_clear_settings_idle(struct bw_native_backend *,int (*)(void *,uint32_t),void *);
enum bw_prepare_guard_point {BW_PREPARE_IO,BW_PREPARE_CONTROL,BW_PREPARE_RELEASE};
/* Trusted resident Send only, separate from setup and ordinary full idle.
 * The reserved operation nonce and exact backend job run bind each callback.
 * Only explicit reserved /api/send copies this authority into its joined
 * worker; direct Send and Play/automatic preparation never receive it.
 * Callbacks run outside the backend mutex. Matching removal/destroy refuses
 * while a worker/reservation borrows the owner. RELEASE consumes held source
 * descriptors after every result and never grants mutation permission. */
int bw_native_backend_set_preparation_idle(struct bw_native_backend *,int (*)(void *,const char[33],const char[33],enum bw_prepare_guard_point),void *);
int bw_native_backend_clear_preparation_idle(struct bw_native_backend *,int (*)(void *,const char[33],const char[33],enum bw_prepare_guard_point),void *);
/* Returns 1 while a worker is active; never detach/free a running worker. */
int bw_native_backend_destroy(struct bw_native_backend *);
void bw_native_library_destroy(struct bw_native_library *);
int bw_native_backend_handle(struct bw_native_backend *,const char *,const char *,const void *,size_t,struct bw_native_response *);
int bw_native_backend_handle_authenticated(struct bw_native_backend *,const char *,const char *,const void *,size_t,const char *,struct bw_native_response *);
/* Trusted resident Dashboard only. A fresh nonce reserves mutations until
 * explicit matched release; unknown/local-active ownership must be retained.
 * Every ordinary POST and direct refresh shares the same admission gate. */
int bw_native_backend_external_reserve(struct bw_native_backend *,const char[33],const char[33]);
int bw_native_backend_external_release(struct bw_native_backend *,const char[33]);
/* Background-only release: the exact retained owner must be known, inactive
 * and free of control-release uncertainty. Does not release an active action. */
int bw_native_backend_external_release_idle(struct bw_native_backend *,const char[33]);
/* Zero only for the same retained external reservation with no active worker,
 * unchanged backend/settings epoch and fresh equal private settings. This is
 * a controller lease, NOT a Sony/process/log idle assertion: local mutation
 * adapters must also perform their existing independent full normal idle.
 * Read callbacks execute outside the backend mutex; no action is started. */
int bw_native_backend_external_current(struct bw_native_backend *,const char[33]);
/* Trusted reserved idle ticker only, never an HTTP action. Observes at most
 * one durable pending retirement or exact committed Removed owner and cannot
 * submit or advance a normal SMP mutation. Exact Removed observation can also
 * resolve a retained same-operation removal UNKNOWN, never issue it again.
 * The caller must already hold this exact external reservation.
 * 0=one observed, 1=no eligible candidate, -1=unknown/refused. */
struct bw_native_cleanup_observation {char title[10],run[33];unsigned pending,absent;const char *reason;};
int bw_native_backend_registration_observe(struct bw_native_backend *,const char[33],struct bw_native_cleanup_observation *);
/* Independent idle observer only. Observes under the normal guards, then
 * consumes its exact inactive reservation under the same dispatch gate.
 * released=1 confirms that consume; otherwise caller retains ownership.
 * Existing active-Uninstall observers use the non-consuming API above. */
int bw_native_backend_registration_observe_idle(struct bw_native_backend *,const char[33],struct bw_native_cleanup_observation *,unsigned *released);
/* Cached reporting only; mutation callers must retain their own reservation
 * and fresh ledger checks. 1=pending exact title, 0=no cached pending, -1=unknown. */
int bw_native_backend_registration_pending(struct bw_native_backend *,const char[10]);
int bw_native_backend_handle_reserved(struct bw_native_backend *,const char[33],const char *,const char *,const void *,size_t,const char *,struct bw_native_response *);
/* Trusted accepted-socket wiring only. peer_loopback is derived from actual
 * getpeername/accept sockaddr, never Host/body/proxy fields. Require the exact
 * X-Blackwire-Reader:home/1 plus JSON content type and no Origin/Referer.
 * Returns an ordinary native response (0 handled); no credential response,
 * SMB/hash/publication or blocking process wait occurs on this HTTP thread. */
int bw_native_backend_home_claim(struct bw_native_backend *,const void *,size_t,int peer_loopback,int reader_header,int browser_origin,struct bw_native_response *);
void bw_native_response_clear(struct bw_native_response *);
#endif
