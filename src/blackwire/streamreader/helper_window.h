#ifndef BW_HELPER_WINDOW_H
#define BW_HELPER_WINDOW_H
#include <stdint.h>
#define BW_HELPER_CONFIRM_US UINT64_C(5000)
#define BW_HELPER_MAX_OBSERVATIONS 32u
struct bw_helper_observation {
    uint64_t fstat_export,open_export;
    int fstat_protection,open_protection;
};
struct bw_helper_window {
    uint64_t fstat_export,open_export,first_us,last_us;
    int confirming;
};
/* Pure state only; caller must validate the same live PID/module cohort
 * around each probe. Negative protection queries reset confirmation and wait;
 * missing/changed addresses or non-executable results refuse immediately.
 * READY requires equal positive observations separated by at least 5 ms.
 * This does not establish a lifetime, lock, or absence of intervening changes.
 */
enum bw_helper_result { BW_HELPER_REFUSE=-1,BW_HELPER_WAIT=0,BW_HELPER_READY=1 };
enum bw_helper_result bw_helper_observe(struct bw_helper_window *,
    const struct bw_helper_observation *,uint64_t now_us);
#endif
