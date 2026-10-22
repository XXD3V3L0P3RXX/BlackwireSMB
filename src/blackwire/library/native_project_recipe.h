#ifndef BW_NATIVE_PROJECT_RECIPE_H
#define BW_NATIVE_PROJECT_RECIPE_H
#include "source_provider.h"
#include "packed_project.h"
#define BW_NATIVE_PROJECT_COMMAND_LIMIT 8192u
#define BW_NATIVE_PROJECT_ARCHIVE_LIMIT 32u
struct bw_native_project_recipe {
    struct bw_packed_project project;
    char command_path[241],requested[250],destination[241];
    uint8_t command_sha256[32];
    const char *error;
    unsigned found;
};
/* 1 exact narrow plain-project request, 0 no project argument, -1 malformed,
 * ambiguous, unsupported or out of bounds. Exact ASCII bytes are preserved;
 * only a literal ../../../ prefix is admitted by this named placement policy.
 * This function does not grant destination or filesystem authority. */
int bw_native_project_command(const void *,size_t,char[250],char[241]);
/* Caller owns a live immutable FOLDER/V1/profile-none provider with a PURE
 * current callback. Synchronous metadata/LOCAL reads only; current/job must
 * remain alive through parser destroy. Returns1 unique verified project body,
 * 0 no supported command-line file/project argument, -1 explicit refusal.
 * Known command filenames are matched by ASCII case; actual original paths
 * and requested/indexed project paths are never case-normalized or aliased.
 * Every candidate Pak is checked within fixed archive/metadata work bounds;
 * conflicting duplicate project bodies refuse instead of guessing load order.
 */
int bw_native_project_recipe_open(struct bw_native_project_recipe *,struct bw_source_provider *,
                                  int (*)(void *),void *);
int bw_native_project_recipe_destroy(struct bw_native_project_recipe *);
#endif
