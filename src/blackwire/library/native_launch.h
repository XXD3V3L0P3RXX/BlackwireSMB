#ifndef BW_NATIVE_LAUNCH_H
#define BW_NATIVE_LAUNCH_H
#include "native_send.h"
#include "reader_selection.h"
/* Construct an in-memory launch selection from a pinned durable preparation
 * and its checked file map. This is not the fresh PC/local launch preflight. */
int bw_native_launch_selection(const char *,const struct bw_provider_binding *,const struct bw_native_prepared *,const char *,struct bw_reader_selection *);
struct bw_native_batch_snapshot;
/* Pure metadata conversion of an actual already-validated owned or borrowed
 * policy2 snapshot. All inputs/backing bytes must remain addressable, immutable
 * and distinct from output for this call. No filesystem/source/current check,
 * LOCAL hashing, readiness or launch authority is supplied. The launch run is
 * independent of the preparation run committed by the terminal. Failure clears
 * output. The caller retains fresh LOCAL/source/registration and pin checks. */
int bw_native_launch_batch_snapshot(const struct bw_provider_binding *,const struct bw_native_batch_snapshot *,const char *,struct bw_reader_selection *);
#endif
