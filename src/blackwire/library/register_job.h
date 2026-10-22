#ifndef BW_REGISTER_JOB_H
#define BW_REGISTER_JOB_H
#include "native_registration.h"
#include "shadowmount_client.h"
struct bw_register_ops {
    void *opaque;
    int (*current)(void *);
    enum bw_registration_state (*state)(void *);
    int (*post)(void *,enum bw_sm_operation,const char *,struct bw_sm_reply *);
    double (*now)(void *);
    void (*pause)(void *,unsigned);
    void (*progress)(void *,const char *);
};
struct bw_register_result {unsigned add_attempted,add_confirmed,scan_attempted,registered;const char *message;
    /* Distinct changed-state repair, never a retry of an ambiguous install.
     * The initial acknowledged add must precede independently stable MATCH.
     * These report attempts only; neither flag grants ownership or Ready. */
    unsigned reattach_attempted,reattach_confirmed;};
/* Queue this one owned source using the public API, then independently verify
 * its actual registration. Never uninstall/switch a conflicting title. An
 * ambiguous add is checked with a read-only list, never blindly repeated.
 * One acknowledged add may be followed by ONE reattachment only after the
 * exact independent app/param/mount/DB state has remained MATCH for 2 seconds
 * and two fresh manual/list checks show the exact path absent. This repairs
 * stock SMP's publication/list race after installation completed. UNKNOWN,
 * ABSENT, PENDING, conflict or an ambiguous initial add never permit it. */
int bw_register_job(const char *,const struct bw_register_ops *,struct bw_register_result *);
#endif
