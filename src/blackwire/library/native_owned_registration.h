#ifndef BW_NATIVE_OWNED_REGISTRATION_H
#define BW_NATIVE_OWNED_REGISTRATION_H
#include "native_register.h"
struct bw_native_owned_registration_result {
    unsigned remove_attempted,remove_confirmed,add_attempted,add_confirmed;
    unsigned scan_attempted,scan_confirmed,switched,unconfirmed;
    const char *message;
};
/* Optional, narrow normal-Play upgrade before bw_native_register. Legacy v4/v5
 * is a no-op. Policy3 retains the original source and is a no-op ONLY with its
 * live source-qualified execution admission; it never enters a derived switch.
 * A v6 conflict may move ONLY the exact verified original legacy
 * root to this selection's exact verified derived root. An explicit v7/policy2
 * conflict may instead refine the exact owned original legacy OR V1-derived
 * registration of the SAME original identity to its verified batch root.
 * Both sides require full LOCAL verification and complete bounded receipt pins.
 * It uses public exact-path
 * manual/remove + manual/add and bounded readback. No uninstall, source/file
 * deletion, general source switch, database writes or grant/source fabrication.
 * At most one attempt per mutation in this invocation; lost replies are followed
 * by readback, never retry. Unrelated manual paths are preserved by the pinned
 * public operation scope, not a global/atomic list snapshot claim. A failed
 * switch can leave the old path excluded or the new path listed: no rollback or
 * replay is promised. The helper retains both startup copies and exposes no
 * save operation; public registration behavior is not a global save guarantee.
 * current must independently fence the accepted selection and idle console;
 * it may do control/filesystem I/O and is never used inside DATA callbacks. */
int bw_native_owned_registration(const struct bw_native_register_request *,
                                struct bw_native_owned_registration_result *);
#endif
