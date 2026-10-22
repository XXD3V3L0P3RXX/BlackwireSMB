/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_HOME_IDENTITY_PS5_H
#define BW_HOME_IDENTITY_PS5_H
#include "native_session.h"
#include <sys/types.h>
/* Existing public app-info declaration, never a title/PID supplied by HTTP. */
int bw_home_identity_app(int32_t,uint32_t *,char[10]);
int bw_home_identity_current(const struct bw_native_session *);
int bw_home_identity_uid(pid_t,uid_t *);
#endif
