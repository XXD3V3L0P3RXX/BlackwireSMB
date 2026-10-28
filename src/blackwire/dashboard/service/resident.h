/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BD_DASHBOARD_RESIDENT_H
#define BD_DASHBOARD_RESIDENT_H
#include "native_backend.h"
struct bd_resident;
struct bd_resident *bd_resident_create(struct bw_native_backend *);
struct bd_resident *bd_resident_create_with_feedback(struct bw_native_backend *,void (*)(const char *));
/* App startup only: one discovery request after guards and before observers.
 * Generic constructors above keep their existing no-initial-refresh behavior. */
struct bd_resident *bd_resident_create_with_startup_refresh(struct bw_native_backend *,void (*)(const char *));
int bd_resident_handle(struct bd_resident *,const char *,const char *,const void *,size_t,const char *,struct bw_native_response *);
#endif
