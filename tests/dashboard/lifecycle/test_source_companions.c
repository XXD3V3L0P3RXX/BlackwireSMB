/* Actual pure codec; original fixture tables are built before projection from
 * deterministic source bytes, not learned by hashing a target directory. */
#define _DEFAULT_SOURCE 1
#include "source_companions.h"
#include "source_plan.h"
#include "../streamreader/copy_sha256.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,cases,allocation_calls,fail_allocation;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"companion case%u line%d: %s\n",cases,__LINE__,#x);exit(2);}}while(0)
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);
void *__wrap_malloc(size_t n){if(fail_allocation&&++allocation_calls==fail_allocation)return NULL;return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t z){if(fail_allocation&&++allocation_calls==fail_allocation)return NULL;return __real_calloc(n,z);}
struct file {const char *path;uint64_t size;uint32_t flags;unsigned byte;};
struct fixture {struct bw_source_map map;uint8_t *dig;size_t dig_size;struct bw_companion_source_tables source;struct bw_provider_binding provider;struct bw_companion_binding binding;uint8_t generation[32];char run[33];};
static void p32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(8*i));}
static void p64(uint8_t *p,uint64_t x){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(x>>(8*i));}
static uint32_t u32(const uint8_t *p){return(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void source_hash(uint64_t size,unsigned byte,uint8_t hash[32]){uint8_t data[64];memset(data,(int)byte,sizeof data);struct cp_sha sha;cp_sha_init(&sha);while(size){size_t n=size<sizeof data?(size_t)size:sizeof data;cp_sha_update(&sha,data,n);size-=n;}cp_sha_final(&sha,hash);}
static void init_policy(struct fixture *f,const struct file *files,size_t count,int actual_policy){
    memset(f,0,sizeof *f);struct bw_source_plan plan;C(!bw_source_plan_init_policy(&plan,"fixture",actual_policy?BW_SOURCE_PLAN_COMPANIONS_V1:BW_SOURCE_PLAN_LEGACY));C(!bw_source_plan_add(&plan,"eboot.bin",16,17));
    for(size_t i=0;i<count;i++)C(!bw_source_plan_add(&plan,files[i].path,files[i].size,17));C(!bw_source_plan_finish(&plan,&f->map));bw_source_plan_destroy(&plan);
    /* Positive fixtures exercise the actual explicit planner. Malformed-table
     * negative fixtures intentionally replace flags under the legacy planner. */
    struct bw_manifest map;C(!bw_manifest_open(&map,f->map.bytes,f->map.size));
    for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record r;C(!bw_manifest_get(&map,i,&r));for(size_t j=0;j<count;j++)if(!strcmp(r.path,files[j].path)){if(actual_policy)C(r.flags==files[j].flags);else p32(f->map.bytes+40u+32u*i+8,files[j].flags);}}
    p64(f->map.bytes+32,bw_manifest_fnv1a64(f->map.bytes+40,f->map.size-40));C(!bw_manifest_open(&map,f->map.bytes,f->map.size));
    f->dig_size=56u+32u*map.count;f->dig=calloc(f->dig_size,1);C(f->dig);memcpy(f->dig,"BWDIG001",8);p32(f->dig+8,1);p32(f->dig+12,map.count);p64(f->dig+16,map.checksum);cp_sha_bytes(f->map.bytes,f->map.size,f->dig+24);
    for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record r;C(!bw_manifest_get(&map,i,&r));if(r.flags!=BW_MANIFEST_LOCAL)continue;unsigned byte=0x31;for(size_t j=0;j<count;j++)if(!strcmp(r.path,files[j].path))byte=files[j].byte;source_hash(r.size,byte,f->dig+56u+32u*i);}
    f->source=(struct bw_companion_source_tables){f->map.bytes,f->map.size,f->dig,f->dig_size};f->provider.kind=BW_PROVIDER_FOLDER;f->provider.policy=BW_PROVIDER_POLICY_V1;
    strcpy(f->provider.binding.title,"PPSA99999");strcpy(f->provider.binding.source,"Fixture");strcpy(f->provider.binding.game,"PPSA99999_");memset(f->provider.binding.sha256,0x31,32);
    for(unsigned i=0;i<32;i++){f->provider.binding.game[10+2*i]='3';f->provider.binding.game[11+2*i]='1';}f->provider.binding.game[74]=0;
    memset(f->generation,0x3a,32);memset(f->run,'a',32);f->run[32]=0;f->binding=(struct bw_companion_binding){&f->provider,f->run,f->generation,{1,2,1,3}};
}
static void init(struct fixture *f,const struct file *files,size_t count){init_policy(f,files,count,0);}
static void init_real(struct fixture *f,const struct file *files,size_t count){init_policy(f,files,count,1);}
static void destroy(struct fixture *f){bw_source_map_destroy(&f->map);free(f->dig);memset(f,0,sizeof *f);}
static void seal(const struct bw_companion_tables *t,struct bw_companion_prepared_seal *s){cp_sha_bytes(t->map,t->map_size,s->map_sha256);cp_sha_bytes(t->digests,t->digest_size,s->digests_sha256);}
static int find(const struct bw_companion_tables *t,const char *name,struct bw_manifest_record *out){struct bw_manifest map;C(!bw_manifest_open(&map,t->map,t->map_size));for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record r;C(!bw_manifest_get(&map,i,&r));if(!strcmp(name,r.path)){if(out)*out=r;return(int)i;}}return -1;}
static void refused_create(struct fixture *f,uint32_t flags){struct bw_companion_projection *p=NULL;const char *error=NULL;cases++;int result=bw_companion_projection_create(&p,&f->binding,&f->source,flags,&error);if(result!=-1||p||!error)fprintf(stderr,"unexpected create result%d flags%u reason%s rows%u\n",result,flags,error?error:"none",u32((const uint8_t *)f->source.map+16));C(result==-1&&p==NULL&&error);}
static void refused_open(struct fixture *f,struct bw_companion_tables *t,const struct bw_companion_prepared_seal *s){struct bw_companion_receipt *view=NULL;const char *error=NULL;cases++;C(bw_companion_receipt_open(&view,&f->binding,&f->source,t,s,&error)==-1&&view==NULL&&error);}
static void reseal_receipt(struct bw_companion_tables *t){int id=find(t,BW_COMPANION_RECEIPT_PATH,NULL);C(id>=0);cp_sha_bytes(t->receipt,t->receipt_size,(uint8_t *)t->digests+56u+32u*(unsigned)id);}
static void positive(struct fixture *f,uint32_t presence,uint32_t selected,unsigned aliases,unsigned copies,int omitted){
    cases++;uint8_t before_map[32],before_dig[32],after[32];cp_sha_bytes(f->source.map,f->source.map_size,before_map);cp_sha_bytes(f->source.digests,f->source.digest_size,before_dig);
    struct bw_companion_projection *p=NULL,*again=NULL;const char *error=NULL;C(!bw_companion_projection_create(&p,&f->binding,&f->source,presence,&error)&&p&&!error);
    struct bw_companion_tables t,a;C(!bw_companion_projection_tables(p,&t));C(!bw_companion_projection_create(&again,&f->binding,&f->source,presence,&error));C(!bw_companion_projection_tables(again,&a));
    C(t.map_size==a.map_size&&!memcmp(t.map,a.map,t.map_size)&&t.digest_size==a.digest_size&&!memcmp(t.digests,a.digests,t.digest_size)&&t.receipt_size==a.receipt_size&&!memcmp(t.receipt,a.receipt,t.receipt_size));
    C(find(&t,BW_COMPANION_RECEIPT_PATH,NULL)>=0);if(omitted)C(find(&t,"fakelib2/libSceAmpr.sprx",NULL)<0);
    struct bw_companion_prepared_seal s;seal(&t,&s);struct bw_companion_receipt *view=NULL;C(!bw_companion_receipt_open(&view,&f->binding,&f->source,&t,&s,&error));C(bw_companion_receipt_namespace(view)==selected);
    C(bw_companion_projection_namespace(p)==selected&&bw_companion_projection_count(p)==bw_companion_receipt_count(view));
    unsigned slots=0,seen_alias=0,seen_copy=0;
    for(uint32_t i=0;i<bw_companion_receipt_count(view);i++){struct bw_companion_member m,preparation;C(!bw_companion_receipt_member(view,i,&m));C(!bw_companion_projection_member(p,i,&preparation));C(m.source_row==preparation.source_row&&m.destination_row==preparation.destination_row&&m.role==preparation.role&&m.source_namespace==preparation.source_namespace&&m.bytes==preparation.bytes&&!memcmp(m.sha256,preparation.sha256,32)&&!strcmp(m.source_path,preparation.source_path)&&!strcmp(m.destination_path,preparation.destination_path));C(strlen(m.source_path)==m.source_path_size&&strlen(m.destination_path)==m.destination_path_size);
        if(m.role==BW_COMPANION_READER_SLOT){slots++;C(m.destination_row==UINT32_MAX&&!m.bytes&&!memcmp(m.sha256,(uint8_t[32]){0},32)&&!strcmp(m.destination_path,"fakelib2/libSceAmpr.sprx"));}
        else{C(m.source_row<u32((const uint8_t *)f->source.map+16)&&!memcmp(m.sha256,f->dig+56u+32u*m.source_row,32));struct bw_manifest_record row;C(find(&t,m.destination_path,&row)==(int)m.destination_row&&row.flags==BW_MANIFEST_LOCAL&&row.size==m.bytes);if(m.role==BW_COMPANION_ALIAS)seen_alias++;else{C(m.role==BW_COMPANION_COPY);seen_copy++;}}
    }C(slots==1&&seen_alias==aliases&&seen_copy==copies);struct bw_companion_member m;C(bw_companion_receipt_member(view,UINT32_MAX,&m)==-1&&!m.role);
    struct bw_companion_projection *saved=p;C(bw_companion_projection_create(&p,&f->binding,&f->source,presence,&error)==-1&&p==saved);
    bw_companion_receipt_clear(&view);bw_companion_receipt_clear(&view);C(!view);bw_companion_projection_clear(&again);bw_companion_projection_clear(&p);bw_companion_projection_clear(&p);C(!p);
    cp_sha_bytes(f->source.map,f->source.map_size,after);C(!memcmp(after,before_map,32));cp_sha_bytes(f->source.digests,f->source.digest_size,after);C(!memcmp(after,before_dig,32));
}
static void mutation_cases(struct fixture *f){
    struct bw_companion_projection *p=NULL;const char *error=NULL;C(!bw_companion_projection_create(&p,&f->binding,&f->source,1,&error));struct bw_companion_tables t;C(!bw_companion_projection_tables(p,&t));struct bw_companion_prepared_seal original;seal(&t,&original);
    uint8_t *receipt=malloc(t.receipt_size),*dig=malloc(t.digest_size);C(receipt&&dig);struct bw_companion_tables changed=t;changed.receipt=receipt;changed.digests=dig;
    const size_t positions[]={0,8,12,16,20,24,32,40,44,48,52,56,88,120,152,184,216,224,232,240,244,248,256,264,272,280,354,383,384,388,392,396,400,408,440,444,448,452,456};
    for(unsigned i=0;i<sizeof positions/sizeof positions[0];i++){
        C(positions[i]<t.receipt_size);memcpy(receipt,t.receipt,t.receipt_size);memcpy(dig,t.digests,t.digest_size);receipt[positions[i]]^=1;refused_open(f,&changed,&original);
        reseal_receipt(&changed);refused_open(f,&changed,&original);struct bw_companion_prepared_seal data_only;seal(&changed,&data_only);refused_open(f,&changed,&data_only);
    }
    memcpy(receipt,t.receipt,t.receipt_size);memcpy(dig,t.digests,t.digest_size);struct bw_companion_prepared_seal zero={0};refused_open(f,&changed,&zero);refused_open(f,&changed,NULL);
    struct bw_companion_tables alias=t;alias.receipt=alias.map;refused_open(f,&alias,&original);alias=t;alias.map_size=SIZE_MAX;refused_open(f,&alias,&original);
    uint8_t old=f->dig[56];f->dig[56]^=1;refused_open(f,&t,&original);f->dig[56]=old;
    uint8_t generation=f->generation[0];f->generation[0]^=1;refused_open(f,&t,&original);f->generation[0]=generation;
    uint64_t inode=f->binding.roots.game_ino;f->binding.roots.game_ino++;refused_open(f,&t,&original);f->binding.roots.game_ino=inode;
    free(receipt);free(dig);bw_companion_projection_clear(&p);
}
int main(void){
    /* Classifier controls only the advisory reprepare hint. Exact old names
     * still require the unchanged LOCAL digest/source/bundle checks. */
    const struct {const char *path;int projection;} hints[]={
        {"fakelib/libSceAmpr.sprx",0},{"fakelib/libSceAppContent.sprx",0},
        {"fakelib/libSceGameUpdate.sprx",0},{"fakelib/libSceNpEntitlementAccess.sprx",0},
        {"fakelib/libSceAmpr.sprx.orig",0},{"fakelib/libScePlayGo.sprx",0},
        {"eboot.bin",0},{"assets/fakelib2/README",0},{"fakelib3/helper.sprx",0},
        {"fakelib",1},{"fakelib2",1},{"fakelib2/libSceAmpr.sprx",1},
        {"fakelib2/libScePlayGo.sprx",1},{"FAKELIB/libSceAmpr.sprx",1},
        {"fakelib/LIBSCEAMPR.SPRX",1},{"fakelib/libSceAmpr.sprx.orig.extra",1},
        {"fakelib/subdir/libSceAmpr.sprx",1},{"fakelib/README",1},
        {"fakelib/helper.so",1},{"fakelib/options.ini",1},{"fakelib/helper.sprx",1},
        {"fakelib/blackwire-reader.selection",1},{"fakelib/blackwire-reader.projection",1},
        {NULL,1}};
    for(unsigned i=0;i<sizeof hints/sizeof hints[0];i++){cases++;C(bw_source_legacy_companion_projection_required(hints[i].path)==hints[i].projection);}
    const struct {const char *path;uint32_t flags;int projection;} old_rows[]={
        {"fakelib/libSceAmpr.sprx",BW_MANIFEST_LOCAL,0},{"fakelib/libSceAmpr.sprx",BW_MANIFEST_STREAM,1},
        {"fakelib/libSceAmpr.sprx.orig",BW_MANIFEST_LOCAL,0},{"fakelib/libSceAmpr.sprx.orig",BW_MANIFEST_STREAM,1},
        {"fakelib/README",BW_MANIFEST_LOCAL,1},{"fakelib/README",BW_MANIFEST_STREAM,1},
        {"fakelib2/libSceAmpr.sprx",BW_MANIFEST_LOCAL,1},{"assets/data.pak",BW_MANIFEST_STREAM,0}};
    for(unsigned i=0;i<sizeof old_rows/sizeof old_rows[0];i++){struct bw_manifest_record row={.path=old_rows[i].path,.flags=old_rows[i].flags};cases++;C(bw_source_legacy_companion_row_projection_required(&row)==old_rows[i].projection);}
    struct fixture f;struct bw_companion_projection *p=NULL;const char *error=NULL;init_real(&f,NULL,0);cases++;C(bw_companion_projection_create(&p,&f.binding,&f.source,0,&error)==1&&!p&&!error);positive(&f,1,1,0,0,1);positive(&f,2,2,0,0,1);destroy(&f);
    const struct file f1[]={{"fakelib/libSceAmpr.sprx",19,1,3},{"fakelib/libScePlayGo.sprx",17,1,4},{"fakelib/README.txt",7,1,5},{"fakelib/options/config.ini",11,1,6},{"assets/data.pak",300,2,7}};
    init_real(&f,f1,5);positive(&f,1,1,3,0,1);positive(&f,3,2,0,0,1);mutation_cases(&f);
    f.provider.kind=BW_PROVIDER_CONTAINER;positive(&f,1,1,3,0,1);f.provider.kind=BW_PROVIDER_FOLDER;
    allocation_calls=0;fail_allocation=UINT_MAX;C(!bw_companion_projection_create(&p,&f.binding,&f.source,1,&error));unsigned allocations=allocation_calls;fail_allocation=0;bw_companion_projection_clear(&p);C(allocations>5);
    for(unsigned n=1;n<=allocations;n++){allocation_calls=0;fail_allocation=n;refused_create(&f,1);fail_allocation=0;}
    refused_create(&f,0);refused_create(&f,4);f.run[0]='g';refused_create(&f,1);f.run[0]='a';f.binding.roots.state_ino=f.binding.roots.game_ino;refused_create(&f,1);f.binding.roots.state_ino=3;destroy(&f);
    const struct file f2[]={{"fakelib2/libSceAmpr.sprx",19,1,3},{"fakelib2/foreign.sprx",17,1,4},{"fakelib2/README.txt",7,1,5}};init_real(&f,f2,3);positive(&f,2,2,0,2,1);positive(&f,3,2,0,2,1);destroy(&f);
    const char *bad[]={"FaKeLiB2/foreign.sprx","fakelib2","fakelib2/blackwire-reader.selection","fakelib/blackwire-reader.share","fakelib2/BLACKWIRE-READER.PROJECTION","fakelib2/LIBSCEAMPR.SPRX",BW_COMPANION_RECEIPT_PATH};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){struct file row={bad[i],17,1,4};init(&f,&row,1);refused_create(&f,3);destroy(&f);}
    struct file streamed={"fakelib2/README.txt",7,2,4};init(&f,&streamed,1);refused_create(&f,2);destroy(&f);
    init(&f,f1,5);f.dig[24]^=1;refused_create(&f,1);f.dig[24]^=1;struct bw_manifest original;C(!bw_manifest_open(&original,f.source.map,f.source.map_size));unsigned boot=UINT_MAX;for(unsigned i=0;i<original.count;i++){struct bw_manifest_record row;C(!bw_manifest_get(&original,i,&row));if(!strcmp(row.path,"eboot.bin"))boot=i;}C(boot!=UINT_MAX);memset(f.dig+56u+32u*boot,0,32);refused_create(&f,1);destroy(&f);
    for(unsigned mode=0;mode<3;mode++){
        size_t n=mode==0?4095:mode==1?4096:1500;struct file *many=calloc(n,sizeof *many);char **paths=calloc(n,sizeof *paths);C(many&&paths);
        for(size_t i=0;i<n;i++){paths[i]=malloc(256);C(paths[i]);if(mode==2){memcpy(paths[i],"fakelib/",8);memset(paths[i]+8,'x',210);snprintf(paths[i]+218,38,"%04u.sprx",(unsigned)i);}else snprintf(paths[i],256,"fakelib/lib%04u.sprx",(unsigned)i);many[i]=(struct file){paths[i],17,1,9};}
        init_real(&f,many,n);if(!mode)positive(&f,1,1,4095,0,1);else refused_create(&f,1);destroy(&f);for(size_t i=0;i<n;i++)free(paths[i]);free(paths);free(many);
    }
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualCodec\":true,\"actualExplicitPlanner\":true,\"originalInputsPreserved\":true,\"filesystemOrNetworkActions\":0,\"runtimeAuthorityProved\":false}\n",cases,checks);return 0;
}
