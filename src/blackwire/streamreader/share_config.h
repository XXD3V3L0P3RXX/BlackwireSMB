#ifndef BW_SHARE_CONFIG_H
#define BW_SHARE_CONFIG_H
#include <stddef.h>
struct bw_share_config {
    char server[128],share[128],user[128],password[256];
    char share2[128];
};
void bw_share_clear(struct bw_share_config *);
int bw_share_parse(struct bw_share_config *,const char *,size_t);
/* Select one configured share on the same server/account. Empty selects the
 * primary share. The returned connection contains only that share; callers
 * clear it after use. Supports out == configured. */
int bw_share_select(struct bw_share_config *out,const struct bw_share_config *configured,const char *share);
/* Read only the existing canonical /data/blackwire/share.conf. Credentials
 * must never enter public status/logs. The caller clears them after connecting.
 */
int bw_share_load(struct bw_share_config *);
#endif
