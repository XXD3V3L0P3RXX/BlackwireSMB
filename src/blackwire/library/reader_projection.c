/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "reader_projection.h"
#include "source_companions.h"
#include "../streamreader/copy_sha256.h"
#include <stdlib.h>
#include <string.h>
struct bw_reader_projection {uint8_t map_sha256[32],generation_sha256[32];uint64_t image_mtime;};
static uint32_t rp_u32(const uint8_t *p){uint32_t n=0;for(unsigned i=0;i<4;i++)n|=(uint32_t)p[i]<<(8*i);return n;}
static uint64_t rp_u64(const uint8_t *p){uint64_t n=0;for(unsigned i=0;i<8;i++)n|=(uint64_t)p[i]<<(8*i);return n;}
static void rp_put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void rp_put64(uint8_t *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(8*i));}
static int rp_fail(const char **error,const char *why){if(error)*error=why;return -1;}
static int rp_hex(const uint8_t hash[32],const char text[65]){static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++)if(text[2*i]!=digits[hash[i]>>4]||text[2*i+1]!=digits[hash[i]&15])return -1;return text[64]?-1:0;}
static int rp_sizes(const size_t sizes[5],size_t *total){
    const size_t limits[]={384,40u*1024u*1024u,56u+32u*(size_t)BW_MANIFEST_MAX_FILES,56u+32u*(size_t)BW_MANIFEST_MAX_FILES,BW_COMPANION_RECEIPT_LIMIT};
    size_t n=BW_READER_PROJECTION_HEADER;
    for(unsigned i=0;i<5;i++){if(!sizes[i]||sizes[i]>limits[i]||sizes[i]>BW_READER_PROJECTION_LIMIT-n)return -1;n+=sizes[i];}
    if(sizes[0]!=384||sizes[1]<BW_MANIFEST_HEADER_SIZE||sizes[2]<56||sizes[3]<56||sizes[4]<BW_COMPANION_HEADER_BYTES)return -1;
    *total=n;return 0;
}
int bw_reader_projection_encode(const struct bw_reader_projection_parts *p,void **out,size_t *size){
    if(size)*size=0;if(!out||*out||!size||!p)return -1;
    const void *data[]={p->prepared_record,p->original_map,p->original_digests,p->physical_digests,p->receipt};
    const size_t sizes[]={p->prepared_record_size,p->original_map_size,p->original_digest_size,p->physical_digest_size,p->receipt_size};size_t total;
    if(rp_sizes(sizes,&total))return -1;for(unsigned i=0;i<5;i++)if(!data[i])return -1;
    uint8_t *bytes=calloc(1,total);if(!bytes)return -1;memcpy(bytes,"BWDAT001",8);rp_put32(bytes+8,1);rp_put32(bytes+12,BW_READER_PROJECTION_HEADER);rp_put64(bytes+16,total);
    size_t at=BW_READER_PROJECTION_HEADER;for(unsigned i=0;i<5;i++){rp_put64(bytes+24+8*i,sizes[i]);memcpy(bytes+at,data[i],sizes[i]);at+=sizes[i];}
    *out=bytes;*size=total;return 0;
}
void bw_reader_projection_clear(struct bw_reader_projection **owner){if(owner&&*owner){memset(*owner,0,sizeof **owner);free(*owner);*owner=NULL;}}
int bw_reader_projection_open(struct bw_reader_projection **out,const struct bw_reader_selection *s,const void *map_bytes,size_t map_size,
    const void *input,size_t input_size,const char **error){
    if(error)*error=NULL;if(!out||*out||!s||!bw_reader_selection_is_projection(s)||!map_bytes||!map_size||!input||input_size<BW_READER_PROJECTION_HEADER||input_size>BW_READER_PROJECTION_LIMIT)
        return rp_fail(error,"projection_data_arguments_invalid");
    char canonical[BW_READER_SELECTION_BYTES+1];size_t selection_size=0;
    if(bw_reader_selection_serialize(s,canonical,sizeof canonical,&selection_size)||!selection_size)return rp_fail(error,"projection_data_selection_invalid");
    const uint8_t *bytes=input;if(memcmp(bytes,"BWDAT001",8)||rp_u32(bytes+8)!=1||rp_u32(bytes+12)!=BW_READER_PROJECTION_HEADER||rp_u64(bytes+16)!=input_size)
        return rp_fail(error,"projection_data_header_invalid");
    size_t sizes[5],total;for(unsigned i=0;i<5;i++){uint64_t n=rp_u64(bytes+24+8*i);if(n>SIZE_MAX)return rp_fail(error,"projection_data_length_invalid");sizes[i]=(size_t)n;}
    if(rp_sizes(sizes,&total)||total!=input_size)return rp_fail(error,"projection_data_length_invalid");
    const uint8_t *parts[5];size_t at=BW_READER_PROJECTION_HEADER;for(unsigned i=0;i<5;i++){parts[i]=bytes+at;at+=sizes[i];}
    const uint8_t *record=parts[0];uint8_t hash[32];cp_sha_bytes(record,384,hash);
    if(rp_hex(hash,s->prepared_record_hex)||memcmp(record,"BWNAT001",8)||rp_u32(record+8)!=1||rp_u32(record+12)!=384)return rp_fail(error,"projection_data_record_differs");
    cp_sha_bytes(record,352,hash);if(memcmp(hash,record+352,32))return rp_fail(error,"projection_data_record_invalid");
    for(unsigned i=336;i<352;i++)if(record[i])return rp_fail(error,"projection_data_record_reserved");
    if(rp_hex(record+48,s->binding_hex)||rp_u64(record+280)!=map_size||rp_u64(record+288)!=sizes[3]||rp_u32(record+268)!=s->manifest_count)
        return rp_fail(error,"projection_data_binding_differs");
    cp_sha_bytes(parts[4],sizes[4],hash);if(rp_hex(hash,s->provenance_hex))return rp_fail(error,"projection_data_receipt_differs");
    struct bw_manifest physical;if(bw_manifest_open(&physical,map_bytes,map_size)||physical.count!=s->manifest_count||physical.checksum!=s->manifest_hash)
        return rp_fail(error,"projection_data_map_invalid");
    struct bw_provider_binding provider={0};provider.kind=(enum bw_provider_kind)s->source_kind;provider.policy=s->source_policy;
    memcpy(provider.binding.sha256,record+48,32);memcpy(provider.binding.source,s->source,sizeof provider.binding.source);
    memcpy(provider.binding.game,s->game,sizeof provider.binding.game);memcpy(provider.binding.title,s->title,sizeof provider.binding.title);
    char prepare_run[33];memcpy(prepare_run,record+16,32);prepare_run[32]=0;
    struct bw_companion_binding binding={&provider,prepare_run,record+80,{rp_u64(record+304),rp_u64(record+312),rp_u64(record+320),rp_u64(record+328)}};
    struct bw_companion_source_tables original={parts[1],sizes[1],parts[2],sizes[2]};
    struct bw_companion_tables tables={.map=map_bytes,.map_size=map_size,.digests=parts[3],.digest_size=sizes[3],.receipt=parts[4],.receipt_size=sizes[4]};
    struct bw_companion_prepared_seal seal;memcpy(seal.map_sha256,record+112,32);memcpy(seal.digests_sha256,record+144,32);
    struct bw_companion_receipt *receipt=NULL;
    if(bw_companion_receipt_open(&receipt,&binding,&original,&tables,&seal,error))return -1;
    bw_companion_receipt_clear(&receipt);
    struct bw_manifest original_map;if(bw_manifest_open(&original_map,parts[1],sizes[1]))return rp_fail(error,"projection_data_original_map_invalid");
    struct bw_reader_projection *p=calloc(1,sizeof *p);if(!p)return rp_fail(error,"projection_data_allocation_failed");
    cp_sha_bytes(parts[1],sizes[1],p->map_sha256);memcpy(p->generation_sha256,record+80,32);
    if(provider.kind==BW_PROVIDER_CONTAINER){
        for(uint32_t i=0;i<original_map.count;i++){struct bw_manifest_record row;
            if(bw_manifest_get(&original_map,i,&row)||(!i?0:row.mtime_sec!=p->image_mtime)){free(p);return rp_fail(error,"projection_data_image_mtime_differs");}
            if(!i)p->image_mtime=row.mtime_sec;
        }
    }
    *out=p;return 0;
}
int bw_reader_projection_source(const struct bw_reader_projection *p,uint8_t map[32],uint8_t generation[32],uint64_t *mtime){
    if(!p||!map||!generation||!mtime)return -1;memcpy(map,p->map_sha256,32);memcpy(generation,p->generation_sha256,32);*mtime=p->image_mtime;return 0;
}
