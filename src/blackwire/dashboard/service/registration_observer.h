/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_REGISTRATION_OBSERVER_H
#define BD_REGISTRATION_OBSERVER_H
#include "native_backend.h"
#include <stdint.h>
struct bd_registration_observer;
struct bd_registration_observer_ops {
    void *opaque;
    int (*now_ms)(void *,uint64_t *);
    int (*busy)(void *);
    int (*idle)(void *); /* Full ordinary idle; no frontend exemption. */
    int (*read_state)(void *,struct bw_native_response *);
    int (*nonce)(void *,char[33]);
    int (*reserve)(void *,const char[33],const char[33]);
    int (*observe)(void *,const char[33],struct bw_native_cleanup_observation *);
    int (*release)(void *,const char[33]);
    void (*report)(void *,const char *); /* Static nonsecret diagnostic, once. */
};
/* Default adapters use actual backend GET/reservation/read-only observation,
 * unchanged full process/log idle, and existing private nonce generation.
 * No mutation/Uninstall API or worker reconstruction is present. */
struct bd_registration_observer *bd_registration_observer_create(struct bw_native_backend *,const struct bd_registration_observer_ops *);
/* Called outside service/backend locks. At most once per5seconds. A failed
 * exact release retains its nonce and blocks new observations; subsequent
 * calls only check/release that same controller reservation. */
int bd_registration_observer_tick(struct bd_registration_observer *);
int bd_registration_observer_destroy(struct bd_registration_observer *); /*1 if retained.*/
#endif
