/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
#include "source.h"
#include <cstdint>
#include <atomic>
/* Two-phase source-owned initialization. Original-op preparation uses the
 * unchanged hook resolver/table, then required metadata loads before patches. */
int bp_prepare_originals_and_metadata();
int bp_release_unpatched_metadata(); /* failed-start owner; no residual detour */
enum : unsigned {BP_INIT_ENTER=1,BP_INIT_PREPASS=2,BP_INIT_METADATA=3,BP_INIT_INDEX=4,BP_INIT_TARGETS=5,BP_INIT_INSTALL=6,BP_INIT_RELEASE=7};
void bp_init_report(unsigned,int64_t); /* bounded numeric snapshot; outside HookLock */
bool bp_metadata_prepare();
bool bp_runtime_metadata_ready();
/* Source-owned Ampr worker calls. No SMB or allocation in open/match. */
bool bp_worker_initialize();
int bp_worker_priority(int upstream);
void bp_worker_priority_report(int upstream,int requested,int result);
void bp_worker_finish();
bool bp_runtime_ready();
bool bp_runtime_failed();
int bp_runtime_match(const char*,size_t,uint64_t,uint32_t*);
int64_t bp_runtime_read(uint32_t,uint64_t,void*,size_t,const std::atomic<bool>*,struct bp_read_witness*);
void bp_runtime_report_read(const struct bp_read_witness*,int64_t); /* copied snapshot; outside pack lock */
void bp_runtime_cancel();
int bp_runtime_release(); /* joined worker and no metadata users required */
/* Only a BOTH-absent home failure; release must already prove no detour or
 * source owner remains. Uses existing libkernel lookup, never SystemService. */
int bp_home_startup_failure(int release_result);
