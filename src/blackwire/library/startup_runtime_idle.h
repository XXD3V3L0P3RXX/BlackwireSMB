#ifndef BW_STARTUP_RUNTIME_IDLE_H
#define BW_STARTUP_RUNTIME_IDLE_H
#include <stddef.h>
#include <stdint.h>
struct bw_startup_idle_request {
    double deadline;
    int (*current)(void *); /* real worker lease; must not recurse into idle */
    void *opaque;
    const char *game_log; /* NULL = installed /data/shadowmount/debug.log */
};
/* Required CONTROL-only idle callback. Native read-only KERN_PROC_PROC snapshots
 * plus complete lifecycle log; no PC/PM service, game hooks or process writes.
 * Zero means positively idle on the pinned supported profile. Unknown refuses. */
int bw_startup_idle_ps5(void *);
/* Pure record/transport boundary used by the native wrapper and host checks. */
struct bw_src_process_result {uint64_t value;unsigned carry;size_t used;};
struct bw_src_idle_ops {
    uint32_t (*firmware)(void *);
    struct bw_src_process_result (*query)(void *,void *buffer,size_t capacity);
    void *opaque;
};
int bw_src_idle_evaluate(const struct bw_startup_idle_request *,const struct bw_src_idle_ops *);
/* Separate, read-only external-start CONTROL: exactly this already-qualified
 * birth may be active; every other game or unrelated active log row refuses.
 * It neither grants publication nor weakens the ordinary idle APIs. */
struct bw_stop_identity;
int bw_src_adopt_evaluate(const struct bw_startup_idle_request *,const struct bw_src_idle_ops *,const struct bw_stop_identity *,const char[10]);
int bw_startup_adopt_ps5(const struct bw_startup_idle_request *,const struct bw_stop_identity *,const char[10]);
#endif
