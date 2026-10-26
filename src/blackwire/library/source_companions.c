/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "source_companions.h"
#include "../streamreader/copy_sha256.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct sc_row {const char *path;char *owned;uint32_t source,flags;uint64_t size,mtime;const uint8_t *hash;};
struct sc_material {const char *source_path,*destination;uint32_t source,role,space;uint64_t size;const uint8_t *hash;};
struct bw_companion_projection {uint8_t *map,*digests,*receipt;size_t map_size,digest_size,receipt_size;uint32_t locals,streams;uint64_t local_bytes,remote_bytes;};
struct bw_companion_receipt {struct bw_companion_projection *canonical;};
static const uint8_t sc_zero[32];
static uint32_t sc_u32(const uint8_t *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t sc_u64(const uint8_t *p){return sc_u32(p)|(uint64_t)sc_u32(p+4)<<32;}
static void sc_p32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static void sc_p64(uint8_t *p,uint64_t x){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(x>>(8*i));}
static int sc_fail(const char **error,const char *why){if(error)*error=why;return -1;}
static int sc_nonzero(const uint8_t *p,size_t n){unsigned value=0;while(n--)value|=*p++;return value!=0;}
static void sc_clear(void *p,size_t n){volatile uint8_t *at=p;while(n--)*at++=0;}
static int sc_nonce(const char *s){if(!s||strnlen(s,33)!=32)return 0;unsigned value=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",s[i]))return 0;value|=s[i]!='0';}return value!=0;}
static unsigned sc_fold(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int sc_compare_paths(const char *a,const char *b){while(*a&&*b&&sc_fold((unsigned char)*a)==sc_fold((unsigned char)*b)){a++;b++;}unsigned x=sc_fold((unsigned char)*a),y=sc_fold((unsigned char)*b);return x<y?-1:x>y;}
static int sc_row_order(const void *a,const void *b){return sc_compare_paths(((const struct sc_row *)a)->path,((const struct sc_row *)b)->path);}
static int sc_material_order(const void *a,const void *b){return strcmp(((const struct sc_material *)a)->destination,((const struct sc_material *)b)->destination);}
static int sc_separate(const void *a,size_t an,const void *b,size_t bn){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;if(!a||!b||an>UINTPTR_MAX-x||bn>UINTPTR_MAX-y)return 0;return x+an<=y||y+bn<=x;}
static int sc_binding(const struct bw_companion_binding *b){
    if(!b||!b->provider||!sc_nonce(b->prepare_run)||!b->source_generation_sha256||!sc_nonzero(b->source_generation_sha256,32)||
       !b->roots.game_ino||!b->roots.state_ino||(b->roots.game_dev==b->roots.state_dev&&b->roots.game_ino==b->roots.state_ino))return -1;
    const struct bw_provider_binding *p=b->provider;const struct bw_source_binding *s=&p->binding;
    if((p->kind!=BW_PROVIDER_FOLDER&&p->kind!=BW_PROVIDER_CONTAINER)||p->policy!=BW_PROVIDER_POLICY_V1||
       !memchr(s->game,0,sizeof s->game)||strlen(s->game)!=74||!memchr(s->title,0,sizeof s->title)||strlen(s->title)!=9||
       memcmp(s->title,"PPSA",4)||memcmp(s->game,s->title,9)||s->game[9]!='_'||
       !memchr(s->source,0,sizeof s->source)||bw_manifest_validate_path(s->source,strlen(s->source))||!sc_nonzero(s->sha256,32))return -1;
    for(unsigned i=4;i<9;i++)if(s->title[i]<'0'||s->title[i]>'9')return -1;
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<32;i++)if(s->game[10+2*i]!=digits[s->sha256[i]>>4]||s->game[11+2*i]!=digits[s->sha256[i]&15])return -1;
    return 0;
}
static int sc_tables(const struct bw_companion_source_tables *s,struct bw_manifest *map,const char **error){
    const size_t map_limit=BW_MANIFEST_HEADER_SIZE+(size_t)BW_MANIFEST_MAX_FILES*BW_MANIFEST_RECORD_SIZE+BW_MANIFEST_MAX_PATH_BYTES;
    if(!s||!s->map||!s->digests||s->map_size<BW_MANIFEST_HEADER_SIZE||s->map_size>map_limit||s->digest_size<56u||s->digest_size>56u+32u*(size_t)BW_MANIFEST_MAX_FILES||!sc_separate(s->map,s->map_size,s->digests,s->digest_size)||
       bw_manifest_open(map,s->map,s->map_size))return sc_fail(error,"companion_original_map_invalid");
    const uint8_t *d=s->digests;uint8_t hash[32];cp_sha_bytes(s->map,s->map_size,hash);
    if(s->digest_size!=56u+32u*(size_t)map->count||memcmp(d,"BWDIG001",8)||sc_u32(d+8)!=1||sc_u32(d+12)!=map->count||
       sc_u64(d+16)!=map->checksum||memcmp(d+24,hash,32))return sc_fail(error,"companion_original_digest_table_invalid");
    for(uint32_t i=0;i<map->count;i++){struct bw_manifest_record row;if(bw_manifest_get(map,i,&row))return sc_fail(error,"companion_original_row_invalid");
        if((row.flags==BW_MANIFEST_LOCAL)!=sc_nonzero(d+56u+32u*i,32))return sc_fail(error,"companion_original_digest_slot_invalid");}
    return 0;
}
/* Exact lowercase namespace spelling matches stock loader paths. Fold only to
 * detect an ambiguous alias, never to silently rename a source path. */
static int sc_namespace(const char *path,const char **suffix){
    const char *slash=strchr(path,'/');size_t n=slash?(size_t)(slash-path):strlen(path);char name[9];
    if(n>8)return 0;for(size_t i=0;i<n;i++)name[i]=(char)sc_fold((unsigned char)path[i]);name[n]=0;
    unsigned space=!strcmp(name,"fakelib")?BW_COMPANION_NS_FAKELIB:!strcmp(name,"fakelib2")?BW_COMPANION_NS_FAKELIB2:0;
    if(!space)return 0;if(!slash||strncmp(path,space==BW_COMPANION_NS_FAKELIB?"fakelib/":"fakelib2/",n+1))return -1;
    if(suffix)*suffix=slash+1;return(int)space;
}
static int sc_reserved(const char *suffix){return !sc_compare_paths(suffix,"blackwire-reader.selection")||!sc_compare_paths(suffix,"blackwire-reader.share")||!sc_compare_paths(suffix,"blackwire-reader.projection");}
static int sc_reader_name(const char *suffix){return !sc_compare_paths(suffix,"libSceAmpr.sprx");}
static uint32_t sc_find(struct sc_row *rows,uint32_t count,const char *path){uint32_t lo=0,hi=count;while(lo<hi){uint32_t mid=lo+(hi-lo)/2;if(sc_compare_paths(rows[mid].path,path)<0)lo=mid+1;else hi=mid;}return lo<count&&!strcmp(rows[lo].path,path)?lo:UINT32_MAX;}
static int sc_map(struct bw_companion_projection *p,struct sc_row *rows,uint32_t count,const char **error){
    qsort(rows,count,sizeof *rows,sc_row_order);size_t pool=0;
    for(uint32_t i=0;i<count;i++){size_t n=strlen(rows[i].path);if(n>BW_MANIFEST_MAX_PATH||bw_manifest_validate_path(rows[i].path,n)||
        n+1>BW_MANIFEST_MAX_PATH_BYTES-pool||(i&&!sc_compare_paths(rows[i-1].path,rows[i].path)))return sc_fail(error,"companion_projected_namespace_invalid");pool+=n+1;}
    p->map_size=BW_MANIFEST_HEADER_SIZE+(size_t)count*BW_MANIFEST_RECORD_SIZE+pool;p->map=calloc(p->map_size,1);
    p->digest_size=56u+32u*(size_t)count;p->digests=calloc(p->digest_size,1);if(!p->map||!p->digests)return sc_fail(error,"companion_memory_limit");
    memcpy(p->map,BW_MANIFEST_MAGIC,8);sc_p32(p->map+8,2);sc_p32(p->map+12,BW_MANIFEST_RECORD_SIZE);sc_p32(p->map+16,count);sc_p64(p->map+24,pool);
    uint8_t *paths=p->map+BW_MANIFEST_HEADER_SIZE+(size_t)count*BW_MANIFEST_RECORD_SIZE;size_t at=0;
    for(uint32_t i=0;i<count;i++){size_t n=strlen(rows[i].path);uint8_t *r=p->map+BW_MANIFEST_HEADER_SIZE+(size_t)i*BW_MANIFEST_RECORD_SIZE;
        sc_p32(r,(uint32_t)at);sc_p32(r+4,(uint32_t)n);sc_p32(r+8,rows[i].flags);sc_p64(r+16,rows[i].size);sc_p64(r+24,rows[i].mtime);memcpy(paths+at,rows[i].path,n+1);at+=n+1;
        if(rows[i].flags==BW_MANIFEST_LOCAL){if(rows[i].size>UINT64_MAX-p->local_bytes)return sc_fail(error,"companion_projected_byte_overflow");p->locals++;p->local_bytes+=rows[i].size;}
        else{if(rows[i].size>UINT64_MAX-p->remote_bytes)return sc_fail(error,"companion_projected_byte_overflow");p->streams++;p->remote_bytes+=rows[i].size;}
        memcpy(p->digests+56u+32u*i,rows[i].hash,32);
    }
    sc_p64(p->map+32,bw_manifest_fnv1a64(p->map+BW_MANIFEST_HEADER_SIZE,p->map_size-BW_MANIFEST_HEADER_SIZE));
    struct bw_manifest checked;if(bw_manifest_open(&checked,p->map,p->map_size))return sc_fail(error,"companion_projected_map_invalid");
    memcpy(p->digests,"BWDIG001",8);sc_p32(p->digests+8,1);sc_p32(p->digests+12,count);sc_p64(p->digests+16,checked.checksum);cp_sha_bytes(p->map,p->map_size,p->digests+24);return 0;
}
void bw_companion_projection_clear(struct bw_companion_projection **owner){
    if(!owner)return;struct bw_companion_projection *p=*owner;*owner=NULL;if(!p)return;
    if(p->map){sc_clear(p->map,p->map_size);free(p->map);}if(p->digests){sc_clear(p->digests,p->digest_size);free(p->digests);}
    if(p->receipt){sc_clear(p->receipt,p->receipt_size);free(p->receipt);}sc_clear(p,sizeof *p);free(p);
}
int bw_companion_projection_create(struct bw_companion_projection **out,const struct bw_companion_binding *b,
    const struct bw_companion_source_tables *source,uint32_t presence,const char **error){
    if(error)*error=NULL;if(!out||*out)return sc_fail(error,"companion_output_not_empty");
    if(sc_binding(b)||presence>3u)return sc_fail(error,"companion_binding_invalid");struct bw_manifest original;
    if(sc_tables(source,&original,error))return -1;
    uint32_t selected=presence&BW_COMPANION_NS_FAKELIB2?2u:presence&BW_COMPANION_NS_FAKELIB?1u:0u,material_count=1,alias_count=0,reader_source=UINT32_MAX;
    uint64_t image_mtime=0;unsigned boot=0;size_t pool=0;
    for(uint32_t i=0;i<original.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&original,i,&row))return sc_fail(error,"companion_original_row_invalid");
        if(!strcmp(row.path,"eboot.bin")&&row.flags==BW_MANIFEST_LOCAL&&row.size)boot++;
        if(b->provider->kind==BW_PROVIDER_CONTAINER){if(!i)image_mtime=row.mtime_sec;else if(image_mtime!=row.mtime_sec)return sc_fail(error,"companion_image_time_differs");}
        if(!sc_compare_paths(row.path,BW_COMPANION_RECEIPT_PATH))return sc_fail(error,"companion_reserved_source_path");
        const char *suffix=NULL;int space=sc_namespace(row.path,&suffix);if(space<0)return sc_fail(error,"companion_namespace_case_or_type_invalid");
        if(!space)continue;if(!(presence&(unsigned)space))return sc_fail(error,"companion_namespace_presence_differs");
        if(row.flags!=BW_MANIFEST_LOCAL)return sc_fail(error,"companion_material_not_local");
        if(sc_reserved(suffix))return sc_fail(error,"companion_reserved_runtime_input");
        if(sc_reader_name(suffix)&&strcmp(suffix,"libSceAmpr.sprx"))return sc_fail(error,"companion_reader_name_case_differs");
        if((unsigned)space!=selected)continue;
        if(!strcmp(suffix,"libSceAmpr.sprx")){reader_source=i;continue;}
        if(material_count==BW_COMPANION_ROW_LIMIT)return sc_fail(error,"companion_row_limit");material_count++;
        if(space==1)alias_count++;
    }
    if(boot!=1)return sc_fail(error,"companion_source_boot_invalid");if(!selected)return 1;
    uint64_t row_count=(uint64_t)original.count+alias_count+1u-(selected==2u&&reader_source!=UINT32_MAX);
    if(row_count>BW_MANIFEST_MAX_FILES)return sc_fail(error,"companion_projected_file_limit");
    struct bw_companion_projection *p=calloc(1,sizeof *p);struct sc_row *rows=calloc((size_t)row_count,sizeof *rows);
    struct sc_material *materials=calloc(material_count,sizeof *materials);int result=-1;uint32_t used=0,mu=0;
    if(!p||!rows||!materials){sc_fail(error,"companion_memory_limit");goto done;}
    for(uint32_t i=0;i<original.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&original,i,&row)){sc_fail(error,"companion_original_row_invalid");goto done;}
        const char *suffix=NULL;int space=sc_namespace(row.path,&suffix);const uint8_t *hash=(const uint8_t *)source->digests+56u+32u*i;
        if(!(selected==2u&&i==reader_source))rows[used++]=(struct sc_row){row.path,NULL,i,row.flags,row.size,row.mtime_sec,hash};
        if((uint32_t)space!=selected||!strcmp(suffix,"libSceAmpr.sprx"))continue;
        const char *dest=row.path;
        if(space==1){size_t n=strlen(suffix)+10u;char *alias=malloc(n);if(!alias){sc_fail(error,"companion_memory_limit");goto done;}
            memcpy(alias,"fakelib2/",9);memcpy(alias+9,suffix,strlen(suffix)+1);rows[used++]=(struct sc_row){alias,alias,i,BW_MANIFEST_LOCAL,row.size,row.mtime_sec,hash};dest=alias;}
        materials[mu++]=(struct sc_material){row.path,dest,i,space==1?BW_COMPANION_ALIAS:BW_COMPANION_COPY,(uint32_t)space,row.size,hash};
    }
    const char *reader_path="";if(reader_source!=UINT32_MAX){struct bw_manifest_record r;if(bw_manifest_get(&original,reader_source,&r))goto done;reader_path=r.path;}
    materials[mu++]=(struct sc_material){reader_path,"fakelib2/libSceAmpr.sprx",reader_source,BW_COMPANION_READER_SLOT,reader_source==UINT32_MAX?0u:selected,0,sc_zero};
    if(mu!=material_count){sc_fail(error,"companion_material_count_differs");goto done;}qsort(materials,mu,sizeof *materials,sc_material_order);
    for(uint32_t i=0;i<mu;i++){size_t n=strlen(materials[i].source_path)+1u+strlen(materials[i].destination)+1u;
        if(n>BW_COMPANION_POOL_LIMIT-pool){sc_fail(error,"companion_pool_limit");goto done;}pool+=n;}
    p->receipt_size=BW_COMPANION_HEADER_BYTES+(size_t)mu*BW_COMPANION_ROW_BYTES+pool;
    if(p->receipt_size>BW_COMPANION_RECEIPT_LIMIT){sc_fail(error,"companion_receipt_limit");goto done;}
    rows[used++]=(struct sc_row){BW_COMPANION_RECEIPT_PATH,NULL,UINT32_MAX,BW_MANIFEST_LOCAL,p->receipt_size,image_mtime,sc_zero};
    if(used!=row_count||sc_map(p,rows,used,error))goto done;
    p->receipt=calloc(p->receipt_size,1);if(!p->receipt){sc_fail(error,"companion_memory_limit");goto done;}uint8_t *h=p->receipt;
    memcpy(h,"BWCPR001",8);sc_p32(h+8,1);sc_p32(h+12,BW_COMPANION_HEADER_BYTES);sc_p32(h+16,BW_COMPANION_ROW_BYTES);sc_p32(h+20,mu);sc_p64(h+24,pool);sc_p64(h+32,p->receipt_size);
    sc_p32(h+40,b->provider->kind);sc_p32(h+44,b->provider->policy);sc_p32(h+48,selected);sc_p32(h+52,presence);memcpy(h+56,b->prepare_run,32);
    memcpy(h+88,b->provider->binding.sha256,32);memcpy(h+120,b->source_generation_sha256,32);cp_sha_bytes(source->map,source->map_size,h+152);cp_sha_bytes(source->digests,source->digest_size,h+184);
    sc_p64(h+216,source->map_size);sc_p64(h+224,source->digest_size);sc_p64(h+232,original.checksum);sc_p32(h+240,original.count);sc_p32(h+244,1);
    sc_p64(h+248,b->roots.game_dev);sc_p64(h+256,b->roots.game_ino);sc_p64(h+264,b->roots.state_dev);sc_p64(h+272,b->roots.state_ino);memcpy(h+280,b->provider->binding.game,74);
    uint8_t *names=h+BW_COMPANION_HEADER_BYTES+(size_t)mu*BW_COMPANION_ROW_BYTES;size_t cursor=0;
    for(uint32_t i=0;i<mu;i++){const struct sc_material *m=materials+i;uint8_t *r=h+BW_COMPANION_HEADER_BYTES+(size_t)i*BW_COMPANION_ROW_BYTES;
        uint32_t destination=m->role==BW_COMPANION_READER_SLOT?UINT32_MAX:sc_find(rows,used,m->destination);
        if(m->role!=BW_COMPANION_READER_SLOT&&destination==UINT32_MAX){sc_fail(error,"companion_destination_row_missing");goto done;}
        sc_p32(r,m->source);sc_p32(r+4,destination);sc_p32(r+8,m->role);sc_p32(r+12,m->space);sc_p64(r+16,m->size);memcpy(r+24,m->hash,32);
        size_t n=strlen(m->source_path);sc_p32(r+56,(uint32_t)cursor);sc_p32(r+60,(uint32_t)n);memcpy(names+cursor,m->source_path,n+1);cursor+=n+1;
        n=strlen(m->destination);sc_p32(r+64,(uint32_t)cursor);sc_p32(r+68,(uint32_t)n);memcpy(names+cursor,m->destination,n+1);cursor+=n+1;
    }
    uint32_t receipt_row=sc_find(rows,used,BW_COMPANION_RECEIPT_PATH);if(receipt_row==UINT32_MAX||cursor!=pool){sc_fail(error,"companion_receipt_row_missing");goto done;}
    cp_sha_bytes(p->receipt,p->receipt_size,p->digests+56u+32u*receipt_row);*out=p;p=NULL;result=0;
done:
    if(rows){for(uint32_t i=0;i<used;i++)free(rows[i].owned);free(rows);}free(materials);bw_companion_projection_clear(&p);return result;
}
int bw_companion_projection_tables(const struct bw_companion_projection *p,struct bw_companion_tables *out){
    if(!out)return -1;memset(out,0,sizeof *out);if(!p)return -1;
    *out=(struct bw_companion_tables){p->map,p->map_size,p->digests,p->digest_size,p->receipt,p->receipt_size,p->locals,p->streams,p->local_bytes,p->remote_bytes};return 0;
}
int bw_companion_receipt_open(struct bw_companion_receipt **out,const struct bw_companion_binding *binding,
    const struct bw_companion_source_tables *original,const struct bw_companion_tables *physical,
    const struct bw_companion_prepared_seal *seal,const char **error){
    if(error)*error=NULL;if(!out||*out)return sc_fail(error,"companion_output_not_empty");
    if(!seal||!sc_nonzero(seal->map_sha256,32)||!sc_nonzero(seal->digests_sha256,32))return sc_fail(error,"companion_parent_seal_missing");
    if(!physical||!original||!physical->map||!physical->digests||!physical->receipt||physical->map_size<BW_MANIFEST_HEADER_SIZE||
       physical->map_size>BW_MANIFEST_HEADER_SIZE+(size_t)BW_MANIFEST_MAX_FILES*BW_MANIFEST_RECORD_SIZE+BW_MANIFEST_MAX_PATH_BYTES||
       physical->digest_size<56u||physical->digest_size>56u+32u*(size_t)BW_MANIFEST_MAX_FILES||
       physical->receipt_size<BW_COMPANION_HEADER_BYTES||physical->receipt_size>BW_COMPANION_RECEIPT_LIMIT)return sc_fail(error,"companion_receipt_size_invalid");
    const void *regions[]={original->map,original->digests,physical->map,physical->digests,physical->receipt};
    size_t sizes[]={original->map_size,original->digest_size,physical->map_size,physical->digest_size,physical->receipt_size};
    for(unsigned i=0;i<5;i++)for(unsigned j=i+1;j<5;j++)if(!sc_separate(regions[i],sizes[i],regions[j],sizes[j]))return sc_fail(error,"companion_input_regions_alias");
    uint8_t hash[32];cp_sha_bytes(physical->map,physical->map_size,hash);if(memcmp(hash,seal->map_sha256,32))return sc_fail(error,"companion_parent_map_differs");
    cp_sha_bytes(physical->digests,physical->digest_size,hash);if(memcmp(hash,seal->digests_sha256,32))return sc_fail(error,"companion_parent_digests_differ");
    struct bw_manifest map;struct bw_companion_source_tables table={physical->map,physical->map_size,physical->digests,physical->digest_size};
    if(sc_tables(&table,&map,error))return -1;uint32_t receipt_row=UINT32_MAX;
    for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&map,i,&row))return sc_fail(error,"companion_physical_row_invalid");
        if(!strcmp(row.path,BW_COMPANION_RECEIPT_PATH)){if(row.flags!=BW_MANIFEST_LOCAL||row.size!=physical->receipt_size)return sc_fail(error,"companion_receipt_row_invalid");receipt_row=i;}}
    cp_sha_bytes(physical->receipt,physical->receipt_size,hash);
    if(receipt_row==UINT32_MAX||memcmp(hash,(const uint8_t *)physical->digests+56u+32u*receipt_row,32))return sc_fail(error,"companion_receipt_not_sealed");
    const uint8_t *h=physical->receipt;
    if(memcmp(h,"BWCPR001",8)||sc_u32(h+8)!=1||sc_u32(h+12)!=BW_COMPANION_HEADER_BYTES||sc_u32(h+16)!=BW_COMPANION_ROW_BYTES||
       !sc_u32(h+20)||sc_u32(h+20)>BW_COMPANION_ROW_LIMIT||sc_u64(h+24)>BW_COMPANION_POOL_LIMIT||sc_u64(h+32)!=physical->receipt_size||
       BW_COMPANION_HEADER_BYTES+(uint64_t)sc_u32(h+20)*BW_COMPANION_ROW_BYTES+sc_u64(h+24)!=physical->receipt_size)return sc_fail(error,"companion_receipt_format_invalid");
    struct bw_companion_projection *canonical=NULL;int made=bw_companion_projection_create(&canonical,binding,original,sc_u32(h+52),error);
    if(made||!canonical){if(!made||made==1)sc_fail(error,"companion_receipt_namespace_invalid");return -1;}
    if(canonical->map_size!=physical->map_size||canonical->digest_size!=physical->digest_size||canonical->receipt_size!=physical->receipt_size||
       memcmp(canonical->map,physical->map,physical->map_size)||memcmp(canonical->digests,physical->digests,physical->digest_size)||memcmp(canonical->receipt,h,physical->receipt_size)){
        bw_companion_projection_clear(&canonical);return sc_fail(error,"companion_projection_relation_differs");}
    struct bw_companion_receipt *view=calloc(1,sizeof *view);if(!view){bw_companion_projection_clear(&canonical);return sc_fail(error,"companion_memory_limit");}
    view->canonical=canonical;*out=view;return 0;
}
uint32_t bw_companion_projection_count(const struct bw_companion_projection *p){return p?sc_u32(p->receipt+20):0;}
uint32_t bw_companion_projection_namespace(const struct bw_companion_projection *p){return p?sc_u32(p->receipt+48):0;}
int bw_companion_projection_member(const struct bw_companion_projection *p,uint32_t id,struct bw_companion_member *out){
    if(!out)return -1;memset(out,0,sizeof *out);if(!p||id>=bw_companion_projection_count(p))return -1;
    const uint8_t *r=p->receipt+BW_COMPANION_HEADER_BYTES+(size_t)id*BW_COMPANION_ROW_BYTES;
    const char *pool=(const char *)p->receipt+BW_COMPANION_HEADER_BYTES+(size_t)bw_companion_projection_count(p)*BW_COMPANION_ROW_BYTES;
    *out=(struct bw_companion_member){sc_u32(r),sc_u32(r+4),sc_u32(r+8),sc_u32(r+12),sc_u64(r+16),r+24,pool+sc_u32(r+56),pool+sc_u32(r+64),sc_u32(r+60),sc_u32(r+68)};return 0;
}
uint32_t bw_companion_receipt_count(const struct bw_companion_receipt *p){return bw_companion_projection_count(p?p->canonical:NULL);}
uint32_t bw_companion_receipt_namespace(const struct bw_companion_receipt *p){return bw_companion_projection_namespace(p?p->canonical:NULL);}
int bw_companion_receipt_member(const struct bw_companion_receipt *p,uint32_t id,struct bw_companion_member *out){return bw_companion_projection_member(p?p->canonical:NULL,id,out);}
void bw_companion_receipt_clear(struct bw_companion_receipt **owner){if(!owner)return;struct bw_companion_receipt *p=*owner;*owner=NULL;if(!p)return;bw_companion_projection_clear(&p->canonical);sc_clear(p,sizeof *p);free(p);}
