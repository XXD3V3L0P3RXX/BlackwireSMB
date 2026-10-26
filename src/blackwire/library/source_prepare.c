#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "source_prepare.h"
#include "launch_timing.h"
#include "source_param.h"
#include "source_profile.h"
#include "../streamreader/copy_sha256.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fail(struct bw_source_smb *p,const char *error){if(!p->error)p->error=error;return -1;}
struct collect {uint8_t *bytes;size_t size,used;};
static int collect(void *opaque,const void *bytes,size_t size){
    struct collect *p=opaque;if(size>p->size-p->used)return -1;memcpy(p->bytes+p->used,bytes,size);p->used+=size;return 0;
}
struct hashing {struct cp_sha sha;struct bw_source_smb *source;struct bw_source_preparation *preparation;};
static int hash(void *opaque,const void *bytes,size_t size){
    struct hashing *p=opaque;struct bw_source_preparation *b=p->preparation;cp_sha_update(&p->sha,bytes,size);b->hashed_bytes+=size;
    BW_LT_COUNT(BW_LT_PC_HASH_CALLBACKS,1);BW_LT_COUNT(BW_LT_PC_HASH_BYTES,size);
    bw_source_smb_progress(p->source,BW_SOURCE_HASHING,b->hashed_files,b->map.local_count,b->hashed_bytes,b->map.local_bytes);return 0;
}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static int run_valid(const char *run){
    if(!run||strlen(run)!=32)return 0;
    for(unsigned i=0;i<32;i++)if(!((run[i]>='0'&&run[i]<='9')||(run[i]>='a'&&run[i]<='f')))return 0;
    return 1;
}
int bw_source_prepare_current(struct bw_source_smb *source,const struct bw_source_preparation *p){
    if(!source||!p||!p->snapshot.sealed||source->error)return -1;
    const char *profile_error=NULL;
    if(bw_source_profile_hashes(&p->map,p->digests,p->digest_size,p->title,p->profile_sha256,&profile_error))return fail(source,profile_error);
    struct bw_source_progress prior=source->progress;
    bw_source_smb_progress(source,BW_SOURCE_RECHECKING,0,0,0,0);
    struct bw_source_map map={0};struct bw_source_snapshot snapshot={0};
    int bad=BW_LT_CALL(BW_LT_SOURCE_SCAN,(p->plan_policy==BW_SOURCE_PLAN_COMPANIONS_V1?
        bw_source_smb_snapshot_bound_policy(source,p->snapshot.source,p->snapshot.game,&map,&snapshot,p->plan_policy):
        p->snapshot.parent_count?bw_source_smb_snapshot_bound(source,p->snapshot.source,p->snapshot.game,&map,&snapshot):bw_source_smb_snapshot(source,p->snapshot.game,&map,&snapshot)));
    uint8_t profile_sha256[32];
    if(!bad&&bw_source_profile_map(&map,p->title,profile_sha256,&profile_error))bad=fail(source,profile_error);
    if(!bad&&memcmp(profile_sha256,p->profile_sha256,32))bad=fail(source,"source_profile_identity_changed");
    if(!bad&&(map.size!=p->map.size||memcmp(map.bytes,p->map.bytes,map.size)||!bw_source_snapshot_equal(&snapshot,&p->snapshot)))bad=fail(source,"source_preparation_generation_changed");
    bw_source_map_destroy(&map);bw_source_snapshot_destroy(&snapshot);
    if(!bad)bw_source_smb_progress(source,prior.phase,prior.files_done,prior.files_total,prior.bytes_done,prior.bytes_total);
    return bad;
}
static int prepare(struct bw_source_smb *source,const char *remote,const char *game,const char *title,const char *run,struct bw_source_preparation *p,enum bw_source_plan_policy policy){
    if(!source||!p)return -1;
    memset(p,0,sizeof *p);
    if(policy!=BW_SOURCE_PLAN_LEGACY&&policy!=BW_SOURCE_PLAN_COMPANIONS_V1)return fail(source,"invalid_source_plan_policy");
    p->plan_policy=policy;
    if(!run_valid(run))return fail(source,"invalid_source_preparation_run");
    bw_source_smb_progress(source,BW_SOURCE_SCANNING,0,0,0,0);
    if(BW_LT_CALL(BW_LT_SOURCE_SCAN,(policy==BW_SOURCE_PLAN_COMPANIONS_V1?
        bw_source_smb_snapshot_bound_policy(source,remote,game,&p->map,&p->snapshot,policy):
        title?bw_source_smb_snapshot_bound(source,remote,game,&p->map,&p->snapshot):bw_source_smb_snapshot(source,game,&p->map,&p->snapshot))))goto failed;
    const struct bw_source_info *param=bw_source_snapshot_find(&p->snapshot,"sce_sys/param.json");
    if(!param||!param->size||param->size>BW_SOURCE_PARAM_LIMIT){fail(source,"source_param_missing_or_too_large");goto failed;}
    struct collect json={malloc((size_t)param->size),(size_t)param->size,0};
    if(!json.bytes){fail(source,"source_param_allocation_failed");goto failed;}
    bw_source_smb_progress(source,BW_SOURCE_METADATA,0,1,0,param->size);
    int bad=bw_source_smb_read(source,&p->snapshot,"sce_sys/param.json",collect,&json)||json.used!=json.size||bw_source_param_title(json.bytes,json.size,p->title);
    free(json.bytes);if(bad){fail(source,"source_param_title_invalid_or_changed");goto failed;}
    if(title&&strcmp(title,p->title)){fail(source,"source_bound_title_changed");goto failed;}
    if(!title&&bw_source_profile_name(p->title)){fail(source,"source_profile_requires_bound_namespace");goto failed;}
    const char *profile_error=NULL;
    if(bw_source_profile_map(&p->map,p->title,p->profile_sha256,&profile_error)){fail(source,profile_error);goto failed;}
    bw_source_smb_progress(source,BW_SOURCE_METADATA,1,1,param->size,param->size);
    struct bw_manifest manifest;
    if(bw_manifest_open(&manifest,p->map.bytes,p->map.size)){fail(source,"source_preparation_manifest_invalid");goto failed;}
    p->digest_size=56u+32u*manifest.count;p->digests=calloc(1,p->digest_size);
    if(!p->digests){fail(source,"source_digest_allocation_failed");goto failed;}
    memcpy(p->digests,"BWDIG001",8);put32(p->digests+8,1);put32(p->digests+12,manifest.count);put64(p->digests+16,manifest.checksum);cp_sha_bytes(p->map.bytes,p->map.size,p->digests+24);
    uint64_t boot=0;
    bw_source_smb_progress(source,BW_SOURCE_HASHING,0,p->map.local_count,0,p->map.local_bytes);
    for(uint32_t i=0;i<manifest.count;i++){
        struct bw_manifest_record record;if(bw_manifest_get(&manifest,i,&record)){fail(source,"source_preparation_record_invalid");goto failed;}
        if(!strcmp(record.path,"eboot.bin"))boot=record.size;
        if(record.flags!=BW_MANIFEST_LOCAL)continue;
        struct hashing hashing={.source=source,.preparation=p};cp_sha_init(&hashing.sha);
        if(bw_source_smb_read(source,&p->snapshot,record.path,hash,&hashing))goto failed;
        cp_sha_final(&hashing.sha,p->digests+56u+32u*i);p->hashed_files++;
        bw_source_smb_progress(source,BW_SOURCE_HASHING,p->hashed_files,p->map.local_count,p->hashed_bytes,p->map.local_bytes);
    }
    if(!boot||boot>UINT64_C(2147483648)){fail(source,"source_startup_executable_size_invalid");goto failed;}
    if(bw_source_prepare_current(source,p))goto failed;
    int n=snprintf(p->selection,sizeof p->selection,"version=2\nrun_id=%s\ntitle_id=%s\ngame=%s\neboot_size=%"PRIu64"\nmanifest_count=%u\nmanifest_hash=%016"PRIx64"\n",run,p->title,game,boot,manifest.count,manifest.checksum);
    if(n<0||(size_t)n>=sizeof p->selection){fail(source,"source_preparation_selection_limit");goto failed;}
    p->verified=1;bw_source_smb_progress(source,BW_SOURCE_VERIFIED,p->hashed_files,p->map.local_count,p->hashed_bytes,p->map.local_bytes);return 0;
failed:
    bw_source_prepare_destroy(p);return -1;
}
int bw_source_prepare(struct bw_source_smb *source,const char *game,const char *run,struct bw_source_preparation *p){return prepare(source,game,game,NULL,run,p,BW_SOURCE_PLAN_LEGACY);}
int bw_source_prepare_bound_policy(struct bw_source_smb *source,const char *remote,const char *game,const char *title,const char *run,struct bw_source_preparation *p,enum bw_source_plan_policy policy){
    if(!source||!p)return -1;
    memset(p,0,sizeof *p);
    if(!title||strnlen(title,10)!=9||memcmp(title,"PPSA",4))return fail(source,"invalid_bound_title");
    for(unsigned i=4;i<9;i++)if(title[i]<'0'||title[i]>'9')return fail(source,"invalid_bound_title");
    return prepare(source,remote,game,title,run,p,policy);
}
int bw_source_prepare_bound(struct bw_source_smb *source,const char *remote,const char *game,const char *title,const char *run,struct bw_source_preparation *p){
    return bw_source_prepare_bound_policy(source,remote,game,title,run,p,BW_SOURCE_PLAN_LEGACY);
}
void bw_source_prepare_destroy(struct bw_source_preparation *p){if(p){bw_source_map_destroy(&p->map);bw_source_snapshot_destroy(&p->snapshot);free(p->digests);memset(p,0,sizeof *p);}}
