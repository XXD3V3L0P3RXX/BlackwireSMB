/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_NATIVE_PROJECTION_HANDOFF_H
#define BW_NATIVE_PROJECTION_HANDOFF_H
#include "native_launch_admission.h"
#include "reader_projection.h"
#include <stdlib.h>
/* Private controller composition. Only the held execution admission exports
 * these source-verified tables. Validate the actual DATA decoder before making
 * any private files, then reobserve the owner; no arbitrary digest arguments. */
static inline int bw_native_projection_handoff_bytes(struct bw_native_launch_admission *owner,
    const struct bw_reader_selection *selected,void **bytes,size_t *size){
    if(size)*size=0;if(!bytes||*bytes||!size||!selected||!bw_reader_selection_is_projection(selected))return -1;
    struct bw_native_projection_inputs in={0};
    if(bw_native_launch_admission_projection_inputs(owner,&in))return -1;
    struct bw_reader_projection_parts parts={in.prepared_record,in.prepared_record_size,in.original_map,in.original_map_size,
        in.original_digests,in.original_digest_size,in.physical_digests,in.physical_digest_size,in.receipt,in.receipt_size};
    if(bw_reader_projection_encode(&parts,bytes,size))return -1;
    struct bw_reader_projection *proof=NULL;
    int result=bw_reader_projection_open(&proof,selected,in.physical_map,in.physical_map_size,*bytes,*size,NULL);
    bw_reader_projection_clear(&proof);
    struct bw_native_projection_inputs after={0};
    if(!result)result=bw_native_launch_admission_projection_inputs(owner,&after);
    if(result){free(*bytes);*bytes=NULL;*size=0;return -1;}return 0;
}
#endif
