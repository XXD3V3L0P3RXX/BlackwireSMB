#ifndef BW_SOURCE_BINDING_H
#define BW_SOURCE_BINDING_H
#include "../streamreader/manifest.h"
#include "../streamreader/share_config.h"
struct bw_source_binding {char source[BW_MANIFEST_MAX_PATH+1],game[75],title[10];uint8_t sha256[32];};
/* Deterministic console component from the exact server/share/source/title.
 * Never use a display title or a collection's leaf as a unique source key.
 * Credentials are neither retained nor hashed. No I/O or readiness claim. */
int bw_source_binding_init(struct bw_source_binding *,const struct bw_share_config *,const char *,const char *);
/* Release selection resolver. Candidate residency policies use a distinct
 * immutable namespace; the baseline initializer above remains unchanged. */
int bw_source_binding_resolve(struct bw_source_binding *,const struct bw_share_config *,const char *,const char *);
#endif
