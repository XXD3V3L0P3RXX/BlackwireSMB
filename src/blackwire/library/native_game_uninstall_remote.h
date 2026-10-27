/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_NATIVE_GAME_UNINSTALL_REMOTE_H
#define BW_NATIVE_GAME_UNINSTALL_REMOTE_H
#include "native_game_uninstall.h"
/* Trusted synchronous joined worker only. Copies the exact confirmed request,
 * preserving its current/observe callbacks and reader pin. Supplies fixed
 * normal SMP source/registration adapters; never accepts a URL or credentials.
 * The core's durable registration intent consumes the whole normal unregister
 * unit before its at-most-once callback. Unknown outcomes never replay. */
int bw_native_game_uninstall_normal(const struct bw_game_uninstall_request *,
    uint16_t port,struct bw_game_uninstall_result *);
/* Same original confirmed operation and inventory only. Provides NO normal
 * registration/manual-remove/uninstall callback to the file-only core. Uses
 * repeated actual app+read-only DB absence around post-uninstall snapshots. */
int bw_native_game_uninstall_files_normal(const struct bw_game_uninstall_files_request *,
    uint16_t port,struct bw_game_uninstall_result *);
/* Strict complete source/manual/scan exclusion only. This adapter has no
 * registration/manual-remove/platform-uninstall callback and never writes
 * external app metadata, database state, settings or the PC source. */
int bw_native_preparation_remove_normal(const struct bw_preparation_remove_request *,
    uint16_t port,struct bw_game_uninstall_result *);
#endif
