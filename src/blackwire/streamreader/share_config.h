#ifndef BW_SHARE_CONFIG_H
#define BW_SHARE_CONFIG_H
#include <stddef.h>
struct bw_share_config {
    char server[128],share[128],user[128],password[256];
};
void bw_share_clear(struct bw_share_config *);
int bw_share_parse(struct bw_share_config *,const char *,size_t);
/* Read only the existing canonical /data/blackwire/share.conf. Credentials
 * must never enter public status/logs. The caller clears them after connecting.
 */
int bw_share_load(struct bw_share_config *);
#endif
