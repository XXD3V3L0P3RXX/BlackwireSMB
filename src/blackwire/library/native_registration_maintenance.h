/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_NATIVE_REGISTRATION_MAINTENANCE_H
#define BW_NATIVE_REGISTRATION_MAINTENANCE_H
#include "native_game_uninstall.h"
struct bw_registration_maintenance_request {
    struct bw_nrc_environment environment;
    struct bw_provider_binding identity;
    struct bw_native_prepared prepared;
    char operation[33],confirmed_title[10];
    /* Exact explicit confirmation under the caller's shared mutation
     * reservation, fresh full idle and unchanged source/settings. Every other
     * worker/console mutation must remain excluded. No release is invented. */
    int (*current)(void *);
    void *opaque;
};
enum bw_registration_maintenance_state {BW_REG_MAINT_UNKNOWN=-1,BW_REG_MAINT_REFUSED=0,BW_REG_MAINT_ABSENT=1};
struct bw_registration_maintenance_result {
    enum bw_registration_maintenance_state state;
    unsigned manual_attempted,uninstall_attempted,prepared_retained;
    const char *reason;
};
/* Explicit registration-only action. Original/source and preparation files
 * are read-only, no Removed tombstone and no automatic .owner adoption. A
 * separate typed private action journal consumes normal requests once. A
 * previous action can observe only, never advance a skipped normal request.
 * Existing committed event ownership must use its qualified lifecycle API. */
int bw_native_registration_maintain(const struct bw_registration_maintenance_request *,uint16_t,
    struct bw_registration_maintenance_result *);
#endif
