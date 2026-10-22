#ifndef BW_SOURCE_PROFILE_H
#define BW_SOURCE_PROFILE_H
#include "source_binding.h"
#include "source_plan.h"

/* Candidate-only known-build residency. Default/off delegates ordinary source
 * binding and preparation without changing a byte. All components must share
 * BW_GTA_AUDIO_BOOTSTRAP=1 before a profiled namespace can be admitted.
 * No transport, filesystem, process, registration or game-memory operations.
 */
int bw_source_profile_enabled(void);
const char *bw_source_profile_name(const char *title);
int bw_source_profile_identity(const char *title, uint8_t out[32]);
int bw_source_binding_resolve(struct bw_source_binding *, const struct bw_share_config *,
                             const char *source, const char *title);
/* Apply only to a new raw owned snapshot map before its digest pass, including
 * on every current-source rescan. Preserves original record IDs and generic
 * sidecar accounting. A recognized unsupported build never falls back thin.
 */
int bw_source_profile_map(struct bw_source_map *, const char *title,
                          uint8_t profile_sha256[32], const char **error);
/* Additional immutable-policy and full executable/archive digest gates, called
 * after the real generic hashing pass and before verified/Send. No fresh-body
 * verification claim follows from this metadata-only helper alone.
 */
int bw_source_profile_hashes(const struct bw_source_map *, const void *digests, size_t,
    const char *title, const uint8_t profile_sha256[32], const char **error);
int bw_source_profile_failure(const char *error);
#endif
