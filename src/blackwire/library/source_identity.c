#include "source_identity.h"
#include "../streamreader/copy_sha256.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>

void bw_provider_image_generation(const struct bw_provider_binding *identity,const struct smb2_stat_64 *stat,
    const uint8_t map_sha256[32],uint8_t out[32]){
    static const char domain[]="Blackwire immutable SMB image generation v1";
    const uint64_t fields[]={stat->smb2_type,stat->smb2_ino,stat->smb2_size,stat->smb2_mtime,
        stat->smb2_mtime_nsec,stat->smb2_ctime,stat->smb2_ctime_nsec,identity->policy};
    uint8_t row[64];for(unsigned i=0;i<8;i++)for(unsigned j=0;j<8;j++)row[8*i+j]=(uint8_t)(fields[i]>>(8*j));
    struct cp_sha sha;cp_sha_init(&sha);cp_sha_update(&sha,domain,sizeof domain);
    cp_sha_update(&sha,identity->binding.sha256,32);cp_sha_update(&sha,row,sizeof row);
    cp_sha_update(&sha,map_sha256,32);cp_sha_final(&sha,out);
}
int bw_provider_binding_init(struct bw_provider_binding *out,const struct bw_share_config *config,
    const char *source,const char *title,enum bw_provider_kind kind) {
    if (!out) return -1;
    memset(out,0,sizeof *out);
    if ((kind!=BW_PROVIDER_FOLDER && kind!=BW_PROVIDER_CONTAINER) ||
        bw_source_binding_resolve(&out->binding,config,source,title)) return -1;
    out->kind=kind;out->policy=BW_PROVIDER_POLICY_V1;
    if (kind==BW_PROVIDER_CONTAINER) {
        static const char domain[]="Blackwire logical container source v1: unsigned contiguous D32 PFS";
        struct cp_sha sha;uint8_t digest[32];cp_sha_init(&sha);cp_sha_update(&sha,domain,sizeof domain);
        cp_sha_update(&sha,out->binding.sha256,32);cp_sha_final(&sha,digest);memcpy(out->binding.sha256,digest,32);
        for (unsigned i=0;i<32;i++) snprintf(out->binding.game+10+2*i,3,"%02x",digest[i]);
    }
    return 0;
}
int bw_provider_binding_select(struct bw_provider_binding *out,struct bw_share_config *connection,
    const char *source,const char *title,enum bw_provider_kind kind,const char *game) {
    if (!out || !connection) return -1;
    struct bw_provider_binding selected={0},probe={0};
    struct bw_share_config chosen={0},candidate={0};unsigned matches=0;int result=-1;
    if (!game || strnlen(game,75)!=74 || !source || !title ||
        !memchr(connection->share2,0,sizeof connection->share2)) goto done;
    for (unsigned i=0;i<2;i++) {
        if (i && !*connection->share2) break;
        if (bw_share_select(&candidate,connection,i?connection->share2:NULL) ||
            bw_provider_binding_init(&probe,&candidate,source,title,kind)) goto done;
        if (!strcmp(probe.binding.game,game)) {selected=probe;chosen=candidate;matches++;}
        bw_share_clear(&candidate);
    }
    if(matches==1)result=0;
done:
    bw_share_clear(&candidate);bw_share_clear(connection);memset(out,0,sizeof *out);
    if (!result) {*out=selected;*connection=chosen;}
    bw_share_clear(&chosen);return result;
}
