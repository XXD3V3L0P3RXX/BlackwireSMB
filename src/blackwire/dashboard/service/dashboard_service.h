/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BLACKWIRE_DASHBOARD_SERVICE_H
#define BLACKWIRE_DASHBOARD_SERVICE_H
#include "catalog_snapshot.h"
#include "frontend_identity.h"
#include "local_http.h"
#include "native_backend.h"
#include "native_game_uninstall.h"
#include "native_setup.h"

struct bd_dashboard_service;
struct bd_service_ops {
    void *opaque;
    /* Full existing native idle check, including every eboot/frontend. Zero
     * positively idle, nonzero busy/unknown. Never exempt the dashboard. */
    int (*idle)(void *);
    /* Optional additional trusted activity guard. Zero permits scoped source
     * checks; nonzero is busy/unknown and refuses. No callback is required
     * when external observation is disabled. Never an all-assets certificate. */
    int (*copy_idle)(void *);
    /* REQUIRED shared backend reservation. reserve atomically excludes existing
     * BW mutations/workers; release matches the exact retained nonce. A local
     * accepted/unknown launch retains it until exact generation exit. */
    int (*reserve)(void *,const char[33],const char[33]);
    int (*release)(void *,const char[33]);
    int (*backend_reserved)(void *,const char[33],const char *,const char *,
                            const void *,size_t,const char *,struct bw_native_response *);
    /* One ordinary Sony launch after fresh source/registration+idle checks.
     * 1 accepted, 0 definite refusal, -1 unknown. Adapter retains its own exact
     * app/PID/birth lifecycle; no force, title-only fallback, kill or hooks.
     * local_state:1 exact present,0 verified exact exit,-1 unknown. Absence
     * before PID discovery is not an exit and MUST return -1. */
    int (*local_launch)(void *,const struct bd_item *,const char[33]);
    int (*local_state)(void *,const char[33]);
    /* Read-only qualification by the attached local launcher. Missing or
     * refused capability lists the item without advertising Play. */
    int (*local_capable)(void *,const struct bd_item *);
    /* Exact confirmed local folder/package removal. The callback only queues
     * one owned worker; all filesystem/Sony work runs outside this lock.
     * start:1 accepted,0 refusal,-1 unknown. state:1 complete,0 pending,-1
     * unknown retained. The matched backend reservation stays held until
     * state positively reports completion or a definite pre-mutation refusal. */
    int (*local_uninstall_capable)(void *,const struct bd_item *);
    int (*local_uninstall)(void *,const struct bd_item *,const char[33],const char[10]);
    int (*local_uninstall_state)(void *,const char[33],struct bw_game_uninstall_result *);
    /* Observation only for an exact retained PC Uninstall UNKNOWN. Called
     * outside this service lock with its original external nonce, after a
     * fresh full idle check. Never replay a request or release ownership.
     * Production uses the backend's durable Removed/owner observer. */
    int (*removal_observe)(void *,const char[33],struct bw_native_cleanup_observation *);
    /* Optional trusted test/platform seams. Production defaults use the real
     * backend, frontend identity, local HTTP client, monotonic clock and nonce. */
    int (*backend_read)(void *,const char *,const char *,const void *,size_t,
                         const char *,struct bw_native_response *);
    int (*http)(void *,enum bd_http_route,const struct bd_http_options *,struct bd_http_reply *);
    int (*frontend_admit)(void *,struct bd_frontend_owner *,int32_t,uint32_t);
    int (*frontend_current)(void *,const struct bd_frontend_owner *);
    int (*now_ms)(void *,uint64_t *);
    int (*nonce)(void *,char[33]);
    /* Optional progress only: copied/dequeued under the service lock, invoked
     * after unlocking. Exact accepted Play nonce; no token, paths, retry or
     * launch authority. Notification failure cannot change the operation. */
    void (*feedback)(void *,const char[33],const char *);
    /* Trusted host seam for the retained Send source witness. Production
     * leaves this NULL and uses unchanged native public identity captures. */
    const struct rc_game_identity_ops *preparation_identity_ops;
    /* Optional same-call preparation diagnostic. Replaces one idle invocation,
     * never adds a second observation or grants permission from the reason.
     * Keep last so existing positional initializers retain their meaning. */
    int (*idle_observe)(void *,struct bw_setup_idle_observation *);
};
struct bd_service_config {
    struct bw_native_backend *backend; /* Borrowed; outlives the service. */
    struct bd_frontend_package frontend_package; /* Actual trusted build tuple. */
    const char *apps_root,*database; /* NULL = /user/app, native app.db. */
    const char *settings_game_log; /* Trusted host seam; NULL=actual SMP log. */
    uint16_t smp_port,bfp_port;
    uint32_t http_timeout_ms; /* Zero=2000; at most5000. */
    unsigned observe_bfp; /* Explicit development opt-in; zero in the resident.
                            Enabled observation is strict on busy/error/timeout. */
    struct bd_service_ops ops;
    /* Trusted standalone mode: no native package or frontend admission.
     * Browser actions retain ordinary full-idle/source/ownership checks. */
    unsigned native_frontend_disabled;
};

/* No service thread or socket listener is installed here. Resident main owns
 * the listener and calls handle, plus tick at a bounded cadence after renderer
 * exit. POST token is the single X-Blackwire-Token header extracted by main;
 * duplicate headers and non-JSON Content-Type must be rejected by the listener.
 * Return0=handled HTTP response,1=route not owned,-1=internal failure.
 * Responses use the existing bw_native_response_clear ownership contract.
 * No known/unknown queued operation may be silently reset on destroy. */
struct bd_dashboard_service *bd_service_create(const struct bd_service_config *);
/* Resident installs this before starting refresh/workers. Setup alone may
 * tolerate the already admitted/current exact frontend in the lifecycle log.
 * A browser operation without a live frontend requires ordinary full idle.
 * Each operation pins that mode or the exact native frontend; it cannot switch
 * authority after a frontend disappears/appears. Token/title/PID alone never qualify.
 * Destroy first removes the callback while backend idle, and otherwise refuses
 * to free its owner. Generic idle/Play/Send/registration are unchanged. */
int bd_service_attach_settings_guard(struct bd_dashboard_service *);
/* Separate Send-only lifetime; requires an independently admitted owner for
 * its narrow frontend exemption. Direct preparation and Play stay full idle. */
int bd_service_attach_preparation_guard(struct bd_dashboard_service *);
int bd_service_handle(struct bd_dashboard_service *,const char *,const char *,
                       const void *,size_t,const char *,struct bw_native_response *);
int bd_service_tick(struct bd_dashboard_service *);
int bd_service_destroy(struct bd_dashboard_service *); /*1 if retained active.*/
#endif
