/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_LAUNCH_TIMING_H
#define BW_LAUNCH_TIMING_H
#include <stdint.h>

/* Observations only: no title, path, credential, provider or policy pointer. */
enum bw_lt_stage {
    BW_LT_SELECT,BW_LT_OWNED_REGISTER,BW_LT_REGISTER,BW_LT_LOCAL_REGISTER,BW_LT_REGISTER_JOB,
    BW_LT_SMB_CONNECT,BW_LT_PC_SOURCE,BW_LT_SOURCE_SCAN,BW_LT_LOCAL_PREFLIGHT,
    BW_LT_PC_FINAL_FENCE,BW_LT_BOOT_SAMPLES,BW_LT_BUNDLE,BW_LT_HANDOFF,
    BW_LT_FINAL_AUTH,BW_LT_SONY,BW_LT_DISCOVER,BW_LT_ASSIGN,BW_LT_STAGE_COUNT
};
enum bw_lt_metric {
    BW_LT_BACKEND_GUARDS,BW_LT_READER_GUARDS,BW_LT_SETTINGS_READS,BW_LT_RECORD_PINS,
    BW_LT_LOCAL_HASH_CALLS,BW_LT_LOCAL_READ_CALLS,BW_LT_LOCAL_HASH_BYTES,
    BW_LT_PC_HASH_CALLBACKS,BW_LT_PC_HASH_BYTES,BW_LT_SOURCE_REQUESTS,
    BW_LT_SOURCE_ENTRIES,BW_LT_SOURCE_CONTENT_BYTES,
    BW_LT_HOME_TIME_REFUSALS,BW_LT_HOME_STATE_REFUSALS,BW_LT_METRIC_COUNT
};
enum bw_lt_report {BW_LT_SONY_RETURN,BW_LT_PROCESS_BOUND,BW_LT_WORKER_RETURN};
enum bw_lt_route {BW_LT_START_ROUTE,BW_LT_PLAY_ROUTE};

#ifdef BW_LAUNCH_TIMING
#include <pthread.h>
struct bw_lt_ops {
    void *opaque;
    int (*now)(void *,uint64_t *nanoseconds);
    void (*emit)(void *,const char *line);
};
struct bw_lt_span {uint64_t start,total,calls;int result;unsigned active;};
struct bw_lt_scope {
    struct bw_lt_ops ops;
    pthread_t owner;
    char run[33],operation[33];
    uint64_t start,last,frozen_at;
    struct bw_lt_span spans[BW_LT_STAGE_COUNT];
    uint64_t metrics[BW_LT_METRIC_COUNT];
    unsigned attached,clock_valid,overflow,frozen,reports;
};
struct bw_lt_request {
    struct bw_lt_ops ops;
    char operation[33];
    uint64_t start;
    enum bw_lt_route route;
    unsigned initialized,clock_valid,finished;
};
/* Set once before creating workers. The logger is called outside our mutex. */
void bw_lt_set_logger(void (*)(const char *));
void bw_lt_enter(struct bw_lt_scope *,const char *run,const char *operation,const struct bw_lt_ops *);
void bw_lt_span_begin(enum bw_lt_stage);
void bw_lt_span_end(enum bw_lt_stage,int);
void bw_lt_count(enum bw_lt_metric,uint64_t);
void bw_lt_report(enum bw_lt_report);
void bw_lt_leave(struct bw_lt_scope *,int result);
void bw_lt_request_begin(struct bw_lt_request *,const char *,enum bw_lt_route,const struct bw_lt_ops *);
void bw_lt_request_end(struct bw_lt_request *,const char *run,int http_status,int result);
#define BW_LT_SCOPE(name,run,operation) struct bw_lt_scope name;bw_lt_enter(&(name),(run),(operation),0)
#define BW_LT_LEAVE(name,result) bw_lt_leave(&(name),(result))
#define BW_LT_COUNT(metric,n) bw_lt_count((metric),(n))
#define BW_LT_REPORT(kind) bw_lt_report((kind))
#define BW_LT_LOGGER(callback) bw_lt_set_logger((callback))
/* Preserve original order, single evaluation, raw result and errno. */
#define BW_LT_CALL(stage,expression) __extension__ ({ \
    enum bw_lt_stage bw_lt_observed_stage=(stage); \
    bw_lt_span_begin(bw_lt_observed_stage); \
    int bw_lt_observed_result=(expression); \
    bw_lt_span_end(bw_lt_observed_stage,bw_lt_observed_result); \
    bw_lt_observed_result; })
#else
#define BW_LT_SCOPE(name,run,operation) ((void)0)
#define BW_LT_LEAVE(name,result) ((void)0)
#define BW_LT_COUNT(metric,n) ((void)0)
#define BW_LT_REPORT(kind) ((void)0)
#define BW_LT_LOGGER(callback) ((void)0)
#define BW_LT_CALL(stage,expression) (expression)
#endif
#endif
