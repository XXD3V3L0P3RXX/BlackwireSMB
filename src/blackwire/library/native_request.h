#ifndef BW_NATIVE_REQUEST_H
#define BW_NATIVE_REQUEST_H
#include "../streamreader/share_config.h"
#include <stdint.h>
#include "../streamreader/manifest.h"
struct bw_native_setup_request {
    struct bw_share_config values;
    uint32_t revision;
    unsigned keep_credentials;
};
/* Strict bounded flat JSON. No duplicate/unknown keys, coercion, NUL/control
 * values, invalid UTF-8, ambiguous credentials or nested objects. Output is
 * cleared on failure; caller clears it after the asynchronous worker finishes. */
int bw_native_setup_request_parse(const void *,size_t,struct bw_native_setup_request *);
struct bw_native_send_request {char source[BW_MANIFEST_MAX_PATH+1];uint64_t library_generation;uint32_t settings_revision;};
int bw_native_send_request_parse(const void *,size_t,struct bw_native_send_request *);
void bw_native_sensitive_clear(void *,size_t);
#endif
