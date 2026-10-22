/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_PRELOAD_FIRST_READ_H
#define BW_PRELOAD_FIRST_READ_H
#include "source.h"
#ifdef __cplusplus
extern "C" {
#endif
struct bp_first_read {unsigned emitted;}; /* one source worker only */
/* Pure formatter. Caller supplies a positive completion snapshot, captured
 * while its job was still held. No logging/target/transport calls here. A
 * successful format consumes the sole emission attempt, even if logger fails. */
int bp_first_read_format(struct bp_first_read *,const struct bp_read_witness *,int64_t completed,int canceled,char *,size_t);
#ifdef __cplusplus
}
#endif
#endif
