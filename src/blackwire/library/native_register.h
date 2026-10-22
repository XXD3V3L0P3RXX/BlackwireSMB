#ifndef BW_NATIVE_REGISTER_H
#define BW_NATIVE_REGISTER_H
#include "reader_runtime.h"
#include "register_job.h"
struct bw_native_launch_admission;
struct bw_native_register_request {
    const char *data_root,*apps,*database;
    const struct bw_provider_binding *identity;
    const struct bw_reader_selection *selected;
    int (*current)(void *);
    void *guard;
    void (*observe)(void *,const struct bw_reader_event *);
    void *observer;
    /* Internal endpoint, always loopback. Zero selects ShadowMount's 10101. */
    uint16_t port;
    /* Trusted normal Ready FOLDER Play only; borrowed from its joined owner.
     * NULL keeps the existing strict fresh LOCAL verification path. */
    struct bw_native_launch_admission *startup;
};
/* Strict callers freshly hash LOCAL bodies. Normal Ready FOLDER Play reuses
 * its live required-startup admission instead; no cached boolean substitutes.
 * The same public API job and independent exact registration readback remain.
 * No game launch, uninstall or conflicting-copy switch occurs here. */
int bw_native_register(const struct bw_native_register_request *,struct bw_register_result *);
#endif
