#ifndef BW_READER_RUNTIME_H
#define BW_READER_RUNTIME_H
#include "native_session.h"
#include "native_setup.h"
#include "reader_selection.h"
#include <pthread.h>

struct bw_reader_event {
    const char *phase,*message;
    int32_t pid;
    uint64_t network_bytes,cache_hit_bytes,stage_high_water;
};
struct bw_native_launch_admission;
/* Internal synchronous worker output, zero until all controller-owned I/O and
 * the exact early private handoff have been discharged. This is not a durable
 * process/launch receipt and must never be reconstructed after restart. */
struct bw_native_reader_release {
    char run[33];
    unsigned runtime_released,io_released,private_released;
};
struct bw_native_reader_request {
    struct bw_reader_selection selection;
    struct bw_setup_generation connection;
    int (*current)(void *);
    /* Called after current PC/local startup verification and before launch.
     * Must independently check the exact title's registered mount/metadata. */
    int (*before_launch)(void *,const struct bw_reader_selection *);
    void (*observe)(void *,const struct bw_reader_event *);
    void *opaque;
    /* Pure controller-owned in-memory cancel/selection lease for DATA
     * callbacks. Never settings/filesystem/SDK/source reentry. */
    int (*data_current)(void *);
    uint64_t data_epoch;
    /* Borrowed only until this synchronous joined reader returns. The runtime
     * never destroys or renews the Play owner's required-startup proof. */
    struct bw_native_launch_admission *startup;
    struct bw_native_reader_release *released;
};
struct bw_reader_runtime {
    pthread_mutex_t lock;
    int active,cancelled,registration_verified,launch_requested,launch_accepted;
    struct bw_native_reader_request request;
    struct bw_native_session session;
    struct bw_session_ops ops;
};
#define BW_READER_RUNTIME_INITIALIZER {.lock=PTHREAD_MUTEX_INITIALIZER}
int bw_reader_runtime_begin(struct bw_reader_runtime *,const struct bw_native_reader_request *,const struct bw_session_ops *);
int bw_reader_runtime_current(struct bw_reader_runtime *);
int bw_reader_runtime_data_current(struct bw_reader_runtime *);
uint64_t bw_reader_runtime_data_epoch(struct bw_reader_runtime *);
int bw_reader_runtime_selection(struct bw_reader_runtime *,struct bw_reader_selection *,struct bw_setup_generation *);
int bw_reader_runtime_before_launch(struct bw_reader_runtime *);
int bw_reader_runtime_launch_begin(struct bw_reader_runtime *);
void bw_reader_runtime_launch_result(struct bw_reader_runtime *,int);
int bw_reader_runtime_bind(struct bw_reader_runtime *,uint32_t,int32_t);
int bw_reader_runtime_poll(struct bw_reader_runtime *,int32_t);
int bw_reader_runtime_close(struct bw_reader_runtime *,const char *);
void bw_reader_runtime_observe(struct bw_reader_runtime *,const struct bw_reader_event *);
/* Refuses to recycle the reader after an accepted launch until the bound exact
 * PID has an authoritative empty process query. Unknown retains ownership. */
int bw_reader_runtime_end(struct bw_reader_runtime *);

/* Implemented by the generated reader in the normal app's joined worker. */
int bw_native_reader_run(const struct bw_native_reader_request *);
int bw_native_reader_close(const char *);
#endif
