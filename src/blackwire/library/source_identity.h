#ifndef BW_SOURCE_IDENTITY_H
#define BW_SOURCE_IDENTITY_H
#include "source_binding.h"
enum bw_provider_kind {BW_PROVIDER_FOLDER=1,BW_PROVIDER_CONTAINER=2};
enum {BW_PROVIDER_POLICY_V1=1};
struct bw_provider_binding {
    enum bw_provider_kind kind;
    uint32_t policy;
    struct bw_source_binding binding;
};
int bw_provider_binding_init(struct bw_provider_binding *,const struct bw_share_config *,
    const char *source,const char *title,enum bw_provider_kind);
#endif
