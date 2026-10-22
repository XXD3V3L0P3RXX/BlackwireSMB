#ifndef BW_READER_USER_H
#define BW_READER_USER_H
#include <stdint.h>
enum bw_reader_user_result {BW_READER_USER_OK=0,BW_READER_USER_INIT_FAILED=-1,BW_READER_USER_FOREGROUND_FAILED=-2};
struct bw_reader_user_observation {
    int foreground_before,initialize_result,foreground_after;
    unsigned initialize_called,foreground_after_called;
};
/* Query a fresh foreground user for each launch. An already usable service
 * needs no initialization. If the query fails, require a successful initialize
 * AND a successful fresh query. No init error is treated as success, no service
 * is terminated, and no user/account is selected or changed by this function.
 * Serialize calls on the app's existing single owned playback worker. */
int bw_reader_foreground_user(uint32_t *,struct bw_reader_user_observation *);
#endif
