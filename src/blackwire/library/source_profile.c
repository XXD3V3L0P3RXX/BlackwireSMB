#include "source_profile.h"
#include "../streamreader/copy_sha256.h"
#include <stdio.h>
#include <string.h>

static const char profile_title[]="PPSA04263",profile_path[]="ps5/audio/audio_rel.rpf";
static const uint8_t profile_eboot_sha[32]={0x45,0xcd,0x4c,0x3d,0x07,0x21,0x3c,0x11,0xed,0xbf,0x7b,0xd8,0x58,0x55,0xc1,0x75,0x67,0xb3,0x12,0xa0,0x7c,0xb7,0x0b,0x99,0x85,0x27,0xbb,0x90,0x5a,0xe6,0x2a,0x89};
static const uint8_t profile_archive_sha[32]={0xc1,0xa2,0xa4,0x6c,0x80,0xab,0x20,0x63,0xf5,0x37,0xaf,0x4b,0x11,0x13,0xfe,0x22,0x12,0x48,0xa0,0xe1,0x41,0xb1,0x9c,0x70,0x5a,0xa1,0xe4,0x4f,0x7f,0x99,0xee,0x75};
#define PROFILE_BOOT_SIZE UINT64_C(62862614)
#define PROFILE_ARCHIVE_SIZE UINT64_C(3155968)
#define PROFILE_ADDED_LIMIT UINT64_C(4194304)
#define PROFILE_LOCAL_LIMIT UINT64_C(5368709120)
static uint32_t sp_u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t sp_u64(const uint8_t *p){return sp_u32(p)|(uint64_t)sp_u32(p+4)<<32;}
static void sp_put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void sp_put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static int sp_error(const char **out,const char *value){if(out)*out=value;return -1;}
int bw_source_profile_enabled(void){
#if defined(BW_GTA_AUDIO_BOOTSTRAP) && BW_GTA_AUDIO_BOOTSTRAP
    return 1;
#else
    return 0;
#endif
}
const char *bw_source_profile_name(const char *title){
    return bw_source_profile_enabled()&&title&&!strcmp(title,profile_title)?"gta-audio-bootstrap-v1":NULL;
}
int bw_source_profile_identity(const char *title,uint8_t out[32]){
    if(!out)return -1;
    memset(out,0,32);
    if(!bw_source_profile_name(title))return 0;
    static const char domain[]="Blackwire resident bootstrap profile v1";
    uint8_t numbers[28],length[4];sp_put32(numbers,1);sp_put64(numbers+4,PROFILE_ADDED_LIMIT);
    sp_put64(numbers+12,PROFILE_LOCAL_LIMIT);sp_put64(numbers+20,PROFILE_ARCHIVE_SIZE);
    sp_put32(length,sizeof profile_path-1);
    struct cp_sha sha;cp_sha_init(&sha);cp_sha_update(&sha,domain,sizeof domain);
    cp_sha_update(&sha,numbers,sizeof numbers);cp_sha_update(&sha,profile_title,9);
    cp_sha_update(&sha,profile_eboot_sha,32);cp_sha_update(&sha,profile_archive_sha,32);
    cp_sha_update(&sha,length,4);cp_sha_update(&sha,profile_path,sizeof profile_path-1);cp_sha_final(&sha,out);
    return 0;
}
int bw_source_binding_resolve(struct bw_source_binding *out,const struct bw_share_config *config,const char *source,const char *title){
    if(bw_source_binding_init(out,config,source,title))return -1;
    if(!bw_source_profile_name(title))return 0;
    uint8_t identity[32],digest[32];if(bw_source_profile_identity(title,identity))return -1;
    static const char domain[]="Blackwire bootstrap namespace v1";
    struct cp_sha sha;cp_sha_init(&sha);cp_sha_update(&sha,domain,sizeof domain);
    cp_sha_update(&sha,out->sha256,32);cp_sha_update(&sha,identity,32);cp_sha_final(&sha,digest);
    memcpy(out->sha256,digest,32);for(unsigned i=0;i<32;i++)snprintf(out->game+10+2*i,3,"%02x",digest[i]);
    return 0;
}
static int sp_view(const struct bw_source_map *map,struct bw_manifest *view,uint32_t *boot,uint32_t *archive,const char **error){
    if(!map||!map->bytes||bw_manifest_open(view,map->bytes,map->size))return sp_error(error,"source_profile_manifest_invalid");
    *boot=*archive=UINT32_MAX;uint64_t local=0,remote=0;uint32_t locals=0,streams=0;
    for(uint32_t i=0;i<view->count;i++){
        struct bw_manifest_record row;if(bw_manifest_get(view,i,&row))return sp_error(error,"source_profile_manifest_invalid");
        uint64_t *total=row.flags==BW_MANIFEST_LOCAL?&local:&remote;
        if(row.size>UINT64_C(9007199254740991)-*total)return sp_error(error,"source_profile_byte_limit");
        *total+=row.size;if(row.flags==BW_MANIFEST_LOCAL)locals++;else streams++;
        if(!strcmp(row.path,"eboot.bin")){
            if(row.flags!=BW_MANIFEST_LOCAL||row.size!=PROFILE_BOOT_SIZE)return sp_error(error,"source_profile_unsupported_executable_size");
            *boot=i;
        }
        if(!strcmp(row.path,profile_path)){
            if(row.size!=PROFILE_ARCHIVE_SIZE)return sp_error(error,"source_profile_unsupported_archive_size");
            *archive=i;
        }
    }
    if(*boot==UINT32_MAX)return sp_error(error,"source_profile_unsupported_executable");
    if(*archive==UINT32_MAX)return sp_error(error,"source_profile_required_archive_missing");
    if(local!=map->local_bytes||remote!=map->remote_bytes||locals!=map->local_count||streams!=map->stream_count||
       remote>UINT64_C(9007199254740991)-local)return sp_error(error,"source_profile_map_totals_invalid");
    return 0;
}
int bw_source_profile_map(struct bw_source_map *map,const char *title,uint8_t identity[32],const char **error){
    if(error)*error=NULL;
    if(bw_source_profile_identity(title,identity))return sp_error(error,"source_profile_identity_invalid");
    if(!bw_source_profile_name(title))return 0;
    struct bw_manifest view;uint32_t boot,archive;
    if(sp_view(map,&view,&boot,&archive,error))return -1;
    (void)boot;struct bw_manifest_record row;if(bw_manifest_get(&view,archive,&row))return sp_error(error,"source_profile_manifest_invalid");
    if(row.flags!=BW_MANIFEST_STREAM)return sp_error(error,"source_profile_required_archive_not_stream");
    if(PROFILE_ARCHIVE_SIZE>PROFILE_ADDED_LIMIT||map->local_bytes>PROFILE_LOCAL_LIMIT||
       PROFILE_ARCHIVE_SIZE>PROFILE_LOCAL_LIMIT-map->local_bytes)return sp_error(error,"source_profile_local_budget_exceeded");
    sp_put32(map->bytes+40+(size_t)archive*32+8,BW_MANIFEST_LOCAL);
    sp_put64(map->bytes+32,bw_manifest_fnv1a64(map->bytes+40,map->size-40));
    map->local_bytes+=PROFILE_ARCHIVE_SIZE;map->remote_bytes-=PROFILE_ARCHIVE_SIZE;map->local_count++;map->stream_count--;
    return 0;
}
int bw_source_profile_hashes(const struct bw_source_map *map,const void *digests,size_t size,const char *title,
                             const uint8_t expected[32],const char **error){
    if(error)*error=NULL;
    uint8_t identity[32];if(!expected||bw_source_profile_identity(title,identity)||memcmp(identity,expected,32))
        return sp_error(error,"source_profile_identity_changed");
    if(!bw_source_profile_name(title))return 0;
    struct bw_manifest view;uint32_t boot,archive;
    if(sp_view(map,&view,&boot,&archive,error))return -1;
    struct bw_manifest_record row;
    if(bw_manifest_get(&view,archive,&row)||row.flags!=BW_MANIFEST_LOCAL)return sp_error(error,"source_profile_archive_not_local");
    const uint8_t *bytes=digests;uint8_t hash[32];cp_sha_bytes(map->bytes,map->size,hash);
    if(!bytes||size!=56+(size_t)view.count*32||memcmp(bytes,"BWDIG001",8)||sp_u32(bytes+8)!=1||
       sp_u32(bytes+12)!=view.count||sp_u64(bytes+16)!=view.checksum||memcmp(bytes+24,hash,32))
        return sp_error(error,"source_profile_digest_generation_changed");
    if(memcmp(bytes+56+(size_t)boot*32,profile_eboot_sha,32))return sp_error(error,"source_profile_unsupported_executable_digest");
    if(memcmp(bytes+56+(size_t)archive*32,profile_archive_sha,32))return sp_error(error,"source_profile_unsupported_archive_digest");
    uint8_t zero[32]={0};
    for(uint32_t i=0;i<view.count;i++){
        if(bw_manifest_get(&view,i,&row))return sp_error(error,"source_profile_manifest_invalid");
        if(row.flags==BW_MANIFEST_STREAM&&memcmp(bytes+56+(size_t)i*32,zero,32))
            return sp_error(error,"source_profile_stream_digest_not_empty");
    }
    return 0;
}
int bw_source_profile_failure(const char *error){return error&&!strncmp(error,"source_profile_",15);}
