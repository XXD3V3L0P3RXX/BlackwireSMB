#ifndef BW_SOURCE_CATALOG_H
#define BW_SOURCE_CATALOG_H
#include "source_smb.h"
#include "source_identity.h"
#include "../streamreader/container_smb.h"
#define BW_CATALOG_MAX_ITEMS 4096u
#define BW_CATALOG_MAX_COLLECTIONS 512u
#define BW_CATALOG_MAX_DEPTH 3u
enum bw_catalog_state {BW_CATALOG_FOLDER=0,BW_CATALOG_BLOCKED=1,BW_CATALOG_BAD_PARAM=2,BW_CATALOG_CONTAINER=3,BW_CATALOG_CONTAINER_READY=4};
struct bw_catalog_item {
    char source[1024],name[241],title[1024],title_id[10];
    struct bw_source_info boot,param,icon;
    uint64_t container_bytes;
    /* Genuine outer SMB metadata, independent of folder boot/param metadata.
     * Logical file sizes do not become fabricated per-file SMB timestamps. */
    struct smb2_stat_64 image;
    uint8_t image_sha256[32];
    uint64_t logical_boot_size,logical_icon_size;
    int container_kind,container_error;
    enum bw_catalog_state state;
    unsigned cover;
    /* Empty in legacy single-share fixtures; discovery pins the actual
     * configured share so equal relative paths remain distinct sources. */
    char source_share[128];
};
struct bw_source_catalog {
    struct bw_catalog_item *items;
    size_t count,capacity;
    uint32_t collections,entries,skipped,unsearched;
    const char *error;
};
/* Read bounded logical title metadata without startup hashing/copying. PFSC
 * may decode bounded physical blocks containing neighboring logical bytes.
 * Container namespace policy remains 4096 admitted files / 1 MiB path bytes.
 * Root folders are
 * inspected before descending non-game collections. Never descend a game.
 * Container extensions are discovery hints only, not format/title validation.
 * Partial/failed results must be discarded by the caller. No preparation,
 * registration, game launch, source writes or console writes. */
int bw_source_catalog_scan(struct bw_source_smb *,struct bw_source_catalog *);
/* Read only the bounded icon from the exact admitted image generation. No
 * extraction or fabricated folder SMB metadata. The caller owns the result.
 * Positive means absent/over budget; -1 discards partial bytes on any failure. */
int bw_source_catalog_container_cover(struct bw_source_smb *,const struct bw_catalog_item *,size_t,void **,size_t *);
void bw_source_catalog_destroy(struct bw_source_catalog *);
int bw_source_catalog_json(const struct bw_source_catalog *,char **,size_t *);
int bw_source_catalog_image_same(const struct smb2_stat_64 *,const struct smb2_stat_64 *);
void bw_source_catalog_image_hash(const struct smb2_stat_64 *,uint8_t[32]);
int bw_source_catalog_available(const struct bw_catalog_item *);
int bw_source_catalog_binding(struct bw_provider_binding *,const struct bw_share_config *,const struct bw_catalog_item *);
enum bw_provider_kind bw_source_catalog_kind(const struct bw_catalog_item *);
uint64_t bw_source_catalog_boot_bytes(const struct bw_catalog_item *);
const char *bw_source_catalog_status(const struct bw_catalog_item *);
const char *bw_source_catalog_format(const struct bw_catalog_item *);
#endif
