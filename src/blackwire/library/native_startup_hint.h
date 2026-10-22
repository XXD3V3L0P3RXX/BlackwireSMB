#ifndef BW_NATIVE_STARTUP_HINT_H
#define BW_NATIVE_STARTUP_HINT_H
#include "native_send.h"
/* Read-only scheduling hint from an owned legacy map and small LOCAL command
 * files. It never grants preparation or launch; the actual accepted worker
 * must reconstruct and verify the PC source and recipe independently. */
int bw_native_legacy_startup_upgrade(const char *,const struct bw_provider_binding *,
    const struct bw_native_prepared *,unsigned *);
#endif
