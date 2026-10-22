#include "source_identity.h"
#include "../streamreader/copy_sha256.h"
#include <stdio.h>
#include <string.h>
int bw_provider_binding_init(struct bw_provider_binding *out,const struct bw_share_config *config,
    const char *source,const char *title,enum bw_provider_kind kind) {
    if (!out) return -1;
    memset(out,0,sizeof *out);
    if ((kind!=BW_PROVIDER_FOLDER && kind!=BW_PROVIDER_CONTAINER) ||
        bw_source_binding_resolve(&out->binding,config,source,title)) return -1;
    out->kind=kind;out->policy=BW_PROVIDER_POLICY_V1;
    if (kind==BW_PROVIDER_CONTAINER) {
        static const char domain[]="Blackwire logical container source v1: unsigned contiguous D32 PFS";
        struct cp_sha sha;uint8_t digest[32];cp_sha_init(&sha);cp_sha_update(&sha,domain,sizeof domain);
        cp_sha_update(&sha,out->binding.sha256,32);cp_sha_final(&sha,digest);memcpy(out->binding.sha256,digest,32);
        for (unsigned i=0;i<32;i++) snprintf(out->binding.game+10+2*i,3,"%02x",digest[i]);
    }
    return 0;
}
