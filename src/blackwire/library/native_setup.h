#ifndef BW_NATIVE_SETUP_H
#define BW_NATIVE_SETUP_H
#include "native_request.h"
#include <sys/stat.h>
enum bw_setup_read_status {BW_SETUP_UNSAFE=-1,BW_SETUP_VALID=0,BW_SETUP_MISSING=1,BW_SETUP_INVALID=2};
enum bw_setup_save_status {BW_SETUP_SAVE_OK=0,BW_SETUP_SAVE_IO=-1,BW_SETUP_SAVE_STALE=-2,BW_SETUP_SAVE_UNSAFE=-3,BW_SETUP_SAVE_BUSY=-4,BW_SETUP_SAVE_UNCONFIRMED=-5};
struct bw_setup_generation {dev_t root_device;ino_t root_inode;int present;struct stat file;uint8_t sha256[32];};
struct bw_setup_saved {int published,verified;char backup[96];};
int bw_native_setup_config_valid(const struct bw_share_config *);
int bw_native_setup_read(const char *,struct bw_share_config *,struct bw_setup_generation *);
int bw_native_setup_generation_equal(const struct bw_setup_generation *,const struct bw_setup_generation *);
/* User-authorized private settings only. Fixed share.conf, owned nonce files,
 * no-follow anchored traversal, immutable backup and late generation recheck.
 * Returns UNCONFIRMED after publication if verification cannot be completed;
 * callers must inspect/refresh, not automatically replay this mutation. */
int bw_native_setup_store(const char *,const struct bw_share_config *,const struct bw_setup_generation *,const char *,int (*)(void *),void *,struct bw_setup_saved *);
int bw_native_setup_idle(void *);
int bw_native_setup_nonce(char[33]);
#endif
