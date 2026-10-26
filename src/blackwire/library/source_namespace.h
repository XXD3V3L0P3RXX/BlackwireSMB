#ifndef BW_SOURCE_NAMESPACE_H
#define BW_SOURCE_NAMESPACE_H
#include "source_provider.h"
enum bw_source_namespace_kind {
    BW_SOURCE_NAMESPACE_ABSENT=0,BW_SOURCE_NAMESPACE_EMPTY_DIRECTORY=1,
    BW_SOURCE_NAMESPACE_DIRECTORY=2,BW_SOURCE_NAMESPACE_REGULAR_FILE=3,
    BW_SOURCE_NAMESPACE_LINK_OR_SPECIAL=4
};
enum {BW_SOURCE_NAMESPACE_FAKELIB=1u,BW_SOURCE_NAMESPACE_FAKELIB2=2u};
/* Data only. A present empty fakelib2 still takes precedence. File roots are
 * typed rather than guessed absent, and never become presence flags. Links,
 * parser collisions and noncanonical namespace aliases refuse completely. */
struct bw_source_namespace_view {
    uint32_t kind[2],presence_flags,selected;
};
int bw_source_namespace_snapshot(const struct bw_source_snapshot *,struct bw_source_namespace_view *);
int bw_source_namespace_container(struct bw_container *,struct bw_source_namespace_view *);
/* Opaque same-worker provider observation. No source writes, selected policy,
 * destination operations or publication authority. The live provider must be
 * explicitly prepared under COMPANIONS_V1 so both original namespaces were
 * made LOCAL BEFORE their digest pass. No file-map empty-directory inference.
 * Close the observation before destroying its borrowed provider. */
struct bw_source_companion_namespace;
int bw_source_provider_companion_namespace(struct bw_source_provider *,struct bw_source_companion_namespace **,const char **);
int bw_source_companion_namespace_current(struct bw_source_companion_namespace *);
/* Projection producers must match the exact borrowed provider, not apply a
 * copied data-only flags view to another source or replacement preparation. */
int bw_source_companion_namespace_matches(struct bw_source_companion_namespace *,const struct bw_source_provider *);
int bw_source_companion_namespace_view(struct bw_source_companion_namespace *,struct bw_source_namespace_view *);
void bw_source_companion_namespace_close(struct bw_source_companion_namespace **);
#endif
