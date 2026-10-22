/* Data-only extension of the existing sparse preparer. No process access.
 * The only new payload write is a genuine 64-byte header into a descriptor
 * just created O_EXCL by the existing preparer. Never seed an existing file.
 */
#ifndef BW_ASSET_PREPARE_H
#define BW_ASSET_PREPARE_H
#include "copy_sha256.h"
static int bw_asset_seed(int fd,int newly_created,uint64_t size,const uint8_t header[64]) {
    struct stat before,after;uint8_t observed[64];
    if (!newly_created || size<65536 || size>INT64_MAX || fstat(fd,&before) ||
        !S_ISREG(before.st_mode) || before.st_nlink!=1 || (uint64_t)before.st_size!=size ||
        before.st_blocks<0 || (uint64_t)before.st_blocks>2048 ||
        pread(fd,observed,sizeof observed,0)!=sizeof observed) return -1;
    for (unsigned i=0;i<sizeof observed;i++) if (observed[i]) return -1;
    if (pwrite(fd,header,64,0)!=64 || fsync(fd) || fstat(fd,&after) ||
        before.st_dev!=after.st_dev || before.st_ino!=after.st_ino ||
        !S_ISREG(after.st_mode) || after.st_nlink!=1 || (uint64_t)after.st_size!=size ||
        after.st_blocks<0 || (uint64_t)after.st_blocks>2048 ||
        pread(fd,observed,sizeof observed,0)!=sizeof observed || memcmp(observed,header,64)) return -1;
    return 0;
}
#ifndef BW_ASSET_SEED_UNIT_ONLY
/* Reuse the reader's exact bounded snapshots and selection validation. Its
 * full owner remains linked but is never opened by this data-only helper. */
#include "asset_reader.c"
static uint8_t asset_prepare_header[64];
static uint64_t asset_prepare_size;
static int asset_prepare_admit(const struct prepare_v2 *cfg,int rootfd) {
    (void)rootfd;
    void *selection=NULL,*source=NULL,*apr=NULL,*mapping=NULL,*index=NULL,*header=NULL;
    size_t ss=0,ms=0,as=0,vs=0,is=0,hs=0,total=0;int result=-1;
    struct bw_asset_reader_selection s={cfg->run_id,cfg->title,cfg->game,cfg->eboot_size,
                                       cfg->manifest_hash,cfg->manifest_count};
    if (ar_snapshot(cfg->root,BW_ASSET_SELECTION_NAME,312,312,0,&selection,&ss,&total) ||
        ar_selection(selection,&s,cfg->manifest_bytes,(size_t)cfg->manifest_stat.st_size)) goto done;
    const uint8_t *pins=selection;
    if (ar_snapshot(cfg->root,"blackwire.files",BW_FILES_MAX_MANIFEST_BYTES,0,0,&source,&ms,&total) || ar_hash(source,ms,pins+120) ||
        ar_snapshot(cfg->root,"ampr_emu.index",BW_ASSET_PACK_MAX_METADATA,0,0,&apr,&as,&total) || ar_hash(apr,as,pins+152) ||
        ar_snapshot(cfg->root,"blackwire-volume.map",BW_ASSET_PACK_MAX_METADATA,0,0,&mapping,&vs,&total) || ar_hash(mapping,vs,pins+184) ||
        ar_snapshot(cfg->root,BW_ASSET_PACK_INDEX,BW_ASSET_PACK_MAX_METADATA,0,0,&index,&is,&total) || ar_hash(index,is,pins+216) ||
        ar_snapshot(cfg->root,"blackwire-pack.header",64,64,0,&header,&hs,&total) || ar_hash(header,hs,pins+280)) goto done;
    struct bw_asset_volume volume;
    if (bw_asset_volume_open(&volume,mapping,vs,pins+184,source,ms,pins+152,header,hs) ||
        volume.source.count+2!=cfg->manifest.count) goto done;
    for (uint32_t i=0;i<volume.source.count;i++) {
        struct bw_manifest_record a,b;
        if (bw_manifest_get(&volume.source,i,&a) || bw_manifest_get(&cfg->manifest,i,&b) ||
            strcmp(a.path,b.path) || a.flags!=b.flags || a.size!=b.size || a.mtime_sec!=b.mtime_sec) goto done;
    }
    struct bw_manifest_record pack,physical;
    if (bw_manifest_get(&cfg->manifest,volume.source.count,&pack) || strcmp(pack.path,BW_ASSET_PACK_INDEX) ||
        pack.flags!=BW_MANIFEST_LOCAL || pack.size!=is ||
        bw_manifest_get(&cfg->manifest,volume.source.count+1,&physical) || strcmp(physical.path,BW_ASSET_PACK_VOLUME) ||
        physical.flags!=BW_MANIFEST_STREAM || physical.size!=volume.size) goto done;
    memcpy(asset_prepare_header,header,64);asset_prepare_size=volume.size;result=0;
done:
    free(selection);free(source);free(apr);free(mapping);free(index);free(header);
    return result?v2_fail("Asset generation, selected map or genuine header does not match"):0;
}
static int asset_prepare_missing(const struct prepare_v2 *cfg,int rootfd) {
    if (!asset_prepare_size || cfg->manifest.count<3) return -1;
    for (uint32_t i=0;i+1<cfg->manifest.count;i++)
        if (cfg->destinations[i].missing) return v2_fail("Complete ordinary preparation before creating the virtual volume");
    if (!cfg->destinations[cfg->manifest.count-1].missing) {
        int fd=v2_openat(rootfd,BW_ASSET_PACK_VOLUME,O_RDONLY|O_NOFOLLOW,0);uint8_t header[64];struct stat st;
        int bad=fd<0 || fstat(fd,&st) || st.st_blocks<0 || (uint64_t)st.st_blocks>2048 ||
            pread(fd,header,64,0)!=64 || memcmp(header,asset_prepare_header,64);
        if (fd>=0) close(fd);
        if (bad) return v2_fail("Existing volume header differs; existing bytes were preserved");
    }
    return 0;
}
#endif
#endif
