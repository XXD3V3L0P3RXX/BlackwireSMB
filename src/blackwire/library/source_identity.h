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
/* Resolve an already pinned game binding against exactly one configured
 * share. Replaces connection with that single-share connection on success;
 * clears both outputs on failure. Does not connect or grant source authority. */
int bw_provider_binding_select(struct bw_provider_binding *,struct bw_share_config *,
    const char *source,const char *title,enum bw_provider_kind,const char *game);
struct smb2_stat_64;
/* Canonical immutable image-generation seal, shared by preparation and launch.
 * Hashing metadata does not itself verify an image or grant source authority. */
void bw_provider_image_generation(const struct bw_provider_binding *,const struct smb2_stat_64 *,const uint8_t map_sha256[32],uint8_t out[32]);
#endif
