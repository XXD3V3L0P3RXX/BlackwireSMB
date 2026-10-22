#ifndef BW_NATIVE_JOB_H
#define BW_NATIVE_JOB_H
#include "source_smb.h"
#define BW_NATIVE_JOB_LIMIT 16u
enum bw_native_job_state {BW_JOB_QUEUED,BW_JOB_RUNNING,BW_JOB_DONE,BW_JOB_ERROR};
enum bw_native_job_failure {BW_JOB_SUCCESS,BW_JOB_SOURCE_CHANGED,BW_JOB_CONNECTION_LOST,BW_JOB_DESTINATION_CONFLICT,BW_JOB_CANCELLED,BW_JOB_FAILED};
struct bw_native_jobs;
struct bw_native_job_token {struct bw_native_jobs *owner;uint64_t generation;char id[33];};
struct bw_native_job_record {
    char id[33],source[BW_MANIFEST_MAX_PATH+1],message[384];
    uint64_t generation;
    enum bw_native_job_state state;
    enum bw_native_job_failure failure;
    struct bw_source_progress progress;
    unsigned files_prepared,registered;
};
struct bw_native_job_view {size_t count;struct bw_native_job_record jobs[BW_NATIVE_JOB_LIMIT];};
/* In-memory controller state only. No workers, network, filesystem, registration
 * or game actions are started here. One queued/running mutation at a time.
 * Callers must join their worker and stop readers before destroying the cache.
 * Source names are the catalog's full share-relative keys, never leaf/title IDs. */
struct bw_native_jobs *bw_native_jobs_create(void);
int bw_native_jobs_destroy(struct bw_native_jobs *);
/* 0 accepted, 1 busy/retained-ID reuse, -1 invalid. A rejected begin leaves the
 * output token unchanged. Tokens bind both a nonce and an internal generation. */
int bw_native_job_begin(struct bw_native_jobs *,const char *,const char *,struct bw_native_job_token *);
int bw_native_job_start(const struct bw_native_job_token *);
int bw_native_job_update(const struct bw_native_job_token *,const struct bw_source_progress *);
void bw_native_job_observe(void *,const struct bw_source_progress *);
/* Worker completion only, after its actions/handles have stopped. Reporting a
 * cancellation here does not request or interrupt one. Successful completion
 * requires the native PREPARED event after its receipt. This means file
 * preparation only, never registration or playable status. */
int bw_native_job_finish(const struct bw_native_job_token *,enum bw_native_job_failure);
/* Trusted joined Send owner only. Progress is presentation, not a new source
 * capability. Reuse describes already held/verified startup data; it never
 * requests a copy. setup finishes only after registration and bundle checks
 * have returned and their owned descriptors have closed. */
int bw_native_job_setup_message(const struct bw_native_job_token *,const char *);
int bw_native_job_reuse_prepared(const struct bw_native_job_token *,uint32_t,uint64_t);
int bw_native_job_finish_setup(const struct bw_native_job_token *,int,int,const char *);
int bw_native_jobs_snapshot(struct bw_native_jobs *,struct bw_native_job_view *);
/* Owned bounded JSON array matching existing jobs id/action/name/state/message
 * fields. Caller frees it. No borrowed mutable state escapes the cache. */
int bw_native_jobs_json(struct bw_native_jobs *,char **,size_t *);
#endif
