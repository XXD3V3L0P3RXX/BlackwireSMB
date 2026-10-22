#ifndef BW_STARTUP_RUNTIME_CONTROL_H
#define BW_STARTUP_RUNTIME_CONTROL_H
#include <stdint.h>
struct bw_startup_runtime_control {void *state;const char *error;unsigned busy,failed;};
struct bw_startup_runtime_request {
    const char *root,*title;
    uint16_t port; /*0=installed loopback10101; test fixture ports only otherwise*/
    double deadline; /*real accepted preparation worker's monotonic deadline*/
    int (*current)(void *);
    /* Required independent, read-only CONTROL observation of game/process and
     * log slots. Zero means positively idle; unknown/conflicting is nonzero.
     * Log-only or path/DB-equal registration is not sufficient authority. */
    int (*idle)(void *);
    void *opaque;
};
/* One zeroed session per real joined preparation worker. Caller owns/excludes
 * concurrent access and retains the accepted selection/current/idle lease.
 * No job token is created or inferred. No network action occurs in open.
 * All callbacks must be CONTROL-only and must not reenter provider/SMB work. */
int bw_startup_runtime_open(struct bw_startup_runtime_control *,const struct bw_startup_runtime_request *);
/* Exact native_send upgrade_unmapped signature. First structurally qualified
 * call may attempt ONE normal exact-title unmount, then independently read
 * mounted:false. All later calls are read-only and allow temporary source
 * absence only after initial exact source_available:true admission. Any error
 * poisons this worker session; no mutation or request is automatically retried.
 * Invoke only outside SMB/provider/consumer callbacks with all borrowed DATA
 * handles inactive. Success is a fresh observation, never retained authority. */
int bw_startup_runtime_unmapped(void *,const char *root,const char *title);
int bw_startup_runtime_destroy(struct bw_startup_runtime_control *);
#endif
