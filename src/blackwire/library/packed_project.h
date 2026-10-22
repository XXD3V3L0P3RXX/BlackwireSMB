#ifndef BW_PACKED_PROJECT_H
#define BW_PACKED_PROJECT_H
#include "source_smb.h"
#define BW_PP_MAX_PATH 240u
#define BW_PP_MAX_INDEX (8u*1024u*1024u)
#define BW_PP_MAX_BODY 65536u
#define BW_PP_MAX_READ_BYTES (16u*1024u*1024u)
#define BW_PP_MAX_READ_CALLS 4096u
#define BW_PP_MAX_INVENTORY 4096u
#define BW_PP_MAX_ENTRIES 262144u
#define BW_PP_MAX_MODULES 32u
enum bw_pp_span_kind { BW_PP_FOOTER, BW_PP_PRIMARY, BW_PP_NAMES, BW_PP_HEADER, BW_PP_BODY, BW_PP_SPAN_COUNT };
struct bw_pp_span { uint64_t offset,size;uint8_t sha256[32],sha1[20]; };
struct bw_pp_request {
    /* Owned, exclusive, real native objects held immutable for this session.
     * The required callback is a pure current-job/cancel guard, zero=current.
     * Caller separately checks whole-provider generation/lifetime. These tags
     * REFUSE unsupported inputs; they do not establish native ownership. */
    struct bw_source_smb *source;
    const struct bw_source_snapshot *snapshot;
    bw_source_range_current_fn current;
    void *job;
    uint32_t source_kind,source_policy; /* exactly FOLDER=1, policy=1 */
    const char *profile;               /* exactly "none" */
    const char *archive,*requested,*listed,*proposed_destination;
    uint64_t base_local_bytes,startup_budget; /* <=5 GiB, never allocation */
};
struct bw_pp_plan {
    /* Observations ONLY. No publication/registration/Play/ready capability.
     * Proposed destination does NOT establish CWD or early visibility. */
    char archive[241],requested[250],listed[250],proposed_destination[241];
    char source[BW_MANIFEST_MAX_PATH+1],game[241];
    uint8_t native_snapshot_sha256[32];
    struct bw_source_info archive_info;
    struct bw_pp_span spans[BW_PP_SPAN_COUNT];
    uint32_t entry_location,module_count;
    char modules[BW_PP_MAX_MODULES][65];
    uint64_t read_bytes;
    uint32_t read_calls,current_checks;
};
struct bw_packed_project {void *state;const char *error;unsigned busy,failed;};
/* Zero initialize; no callback/source/provider reentry or mutation. Outputs are
 * cleared on failure. No arbitrary invalid-pointer/hostile callback safety.
 * Buffers/plan view borrow this session until destroy or failed revalidation.
 * Source/snapshot/job must remain alive. No source writes or file creation. */
int bw_packed_project_open(struct bw_packed_project *,const struct bw_pp_request *);
/* Checks captured in-memory native seal and current-job guard before returning.
 * Does not reread archive content/siblings. A previously returned pointer is a
 * historical observation, never renewed authority; revalidate before reuse. */
const struct bw_pp_plan *bw_packed_project_view(struct bw_packed_project *);
/* Rederive using the SAME captured native seal/archive/job and compare original
 * spans/digests. A changed generation/content poisons the session, no retries. */
int bw_packed_project_revalidate(struct bw_packed_project *);
int bw_packed_project_destroy(struct bw_packed_project *);
#endif
