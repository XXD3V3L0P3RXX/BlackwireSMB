/* Actual DATA/cache/source code; explicitly modeled SMB transport, not a server. */
#include "reader/preload/source.h"
#include "library/reader_selection.h"
#include "library/source_binding.h"
#include "library/source_profile.h"
#include "streamreader/manifest.h"
#include "streamreader/cache.h"
#include "streamreader/copy_sha256.h"
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
static unsigned checks,cases,contexts,destroys,opens,closes,reads,connects,pending_drained;
static int canceled,fail_read,fail_close,changed,change_after_read,cancel_after_read;
static unsigned stat_calls,fstat_calls,final_named_at,fail_at_read;
static int change_on_final,cancel_on_final,fail_final_stat,zero_read,oversized_read;
static unsigned char *alter_output;
static unsigned short_limit=8191;
static uint32_t negotiated_limit=BP_COALESCE_BYTES;
static unsigned trace_count,clock_fault,clock_reverse;
static uint64_t fake_ns=1000000000u;
struct read_trace {uint64_t at;uint32_t requested,returned;};
static struct read_trace trace[4096];
static uint64_t stream_size=131079;
static unsigned char borrowed_storage[BW_CACHE_BYTES];
static unsigned char large_out[1024u*1024u],large_expected[sizeof large_out];
struct smb2fh {unsigned id;struct smb2fh *next;};
struct smb2_context {struct smb2fh *handles;unsigned char *pending;int signing;uint16_t security_mode;};
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"check failed %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static unsigned char body(unsigned id,uint64_t at){return (unsigned char)((at*17+id*29)%251);}
int __wrap_clock_gettime(clockid_t clock,struct timespec *stamp){
    CHECK(clock==CLOCK_MONOTONIC&&stamp);errno=ERANGE;if(clock_fault)return -1;
    fake_ns=clock_reverse?fake_ns-1000:fake_ns+1000;
    stamp->tv_sec=(time_t)(fake_ns/1000000000u);stamp->tv_nsec=(long)(fake_ns%1000000000u);return 0;
}
struct smb2_context *smb2_init_context(void){contexts++;return calloc(1,sizeof(struct smb2_context));}
void smb2_destroy_context(struct smb2_context *c){
    CHECK(c);if(c->pending){*c->pending=0x57;pending_drained++;}
    while(c->handles){struct smb2fh *h=c->handles;c->handles=h->next;free(h);}
    destroys++;free(c);
}
void smb2_set_timeout(struct smb2_context *c,int n){CHECK(c&&n==10);}
void smb2_set_user(struct smb2_context *c,const char *u){CHECK(c&&u&&!strcmp(u,"fixture"));}
void smb2_set_password(struct smb2_context *c,const char *p){CHECK(c&&p&&!strcmp(p,""));}
void smb2_set_sign(struct smb2_context *c,int required){CHECK(c&&required==1);c->signing=required;}
void smb2_set_security_mode(struct smb2_context *c,uint16_t m){CHECK(c&&m==(SMB2_NEGOTIATE_SIGNING_ENABLED|SMB2_NEGOTIATE_SIGNING_REQUIRED));c->security_mode=m;}
int smb2_connect_share(struct smb2_context *c,const char *s,const char *h,const char *u){connects++;CHECK(c&&c->signing==1&&c->security_mode==(SMB2_NEGOTIATE_SIGNING_ENABLED|SMB2_NEGOTIATE_SIGNING_REQUIRED)&&!strcmp(s,"192.0.2.1")&&!strcmp(h,"fixture")&&!strcmp(u,"fixture"));return 0;}
int smb2_get_nterror(struct smb2_context *c){CHECK(c);return 0;}
uint32_t smb2_get_max_read_size(struct smb2_context *c){CHECK(c);return negotiated_limit;}
static int path_id(const char *p){unsigned id;char tail;return sscanf(p,"Fixture Title/stream%02u.pak%c",&id,&tail)==1&&id<18?(int)id:-1;}
static void stat_of(unsigned id,struct smb2_stat_64 *s){memset(s,0,sizeof *s);s->smb2_type=SMB2_TYPE_FILE;s->smb2_size=stream_size+id;s->smb2_ino=100+id+(unsigned)changed;s->smb2_mtime=100;s->smb2_mtime_nsec=7;s->smb2_ctime=200;s->smb2_ctime_nsec=9;}
int smb2_stat(struct smb2_context *c,const char *p,struct smb2_stat_64 *s){
    int id=path_id(p);CHECK(c&&id>=0);stat_calls++;
    /* One named fence at entry, one after the complete caller copy, for any
     * number of blocks. Tests may select a later cached request's final fence. */
    if(stat_calls==final_named_at){
        if(change_on_final)changed=1;if(cancel_on_final)canceled=1;if(alter_output)*alter_output=0xee;
        if(fail_final_stat){fail_final_stat=0;c->pending=(unsigned char*)s;return -EIO;}
    }
    stat_of((unsigned)id,s);return 0;
}
int smb2_fstat(struct smb2_context *c,struct smb2fh *h,struct smb2_stat_64 *s){CHECK(c&&h);fstat_calls++;stat_of(h->id,s);return 0;}
struct smb2fh *smb2_open(struct smb2_context *c,const char *p,int flags){int id=path_id(p);CHECK(c&&id>=0&&flags==O_RDONLY);struct smb2fh *h=calloc(1,sizeof *h);h->id=(unsigned)id;h->next=c->handles;c->handles=h;opens++;return h;}
int smb2_close(struct smb2_context *c,struct smb2fh *h){struct smb2fh **at=&c->handles;while(*at&&*at!=h)at=&(*at)->next;CHECK(*at==h);*at=h->next;free(h);closes++;if(fail_close){fail_close=0;return -EIO;}return 0;}
int smb2_pread(struct smb2_context *c,struct smb2fh *h,uint8_t *out,uint32_t count,uint64_t at){
    CHECK(c&&h&&out&&count&&count<=BP_COALESCE_BYTES&&count<=negotiated_limit);CHECK(trace_count<4096);
    struct read_trace *sample=&trace[trace_count++];*sample=(struct read_trace){at,count,0};
    reads++;if(fail_read||reads==fail_at_read){fail_read=0;fail_at_read=0;c->pending=out;return -EIO;}
    if(zero_read){zero_read=0;return 0;}
    if(oversized_read){oversized_read=0;return (int)count+1;}
    if(count>short_limit)count=short_limit;sample->returned=count;for(uint32_t i=0;i<count;i++)out[i]=body(h->id,at+i);
    if(change_after_read)changed=1;if(cancel_after_read)canceled=1;return (int)count;
}
static int guard(void *p){CHECK(p==&canceled);return canceled;}
static void put32(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(i*8));}
static void put64(unsigned char *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(n>>(i*8));}
static unsigned char map[8192];static size_t map_size,selection_size;static char selection[4096];
static struct bw_reader_selection selected;
static const char share[]="server=192.0.2.1\nshare=fixture\nuser=fixture\npassword=\n";
static void fixture(void){
    canceled=fail_read=fail_close=changed=change_after_read=cancel_after_read=0;
    stat_calls=fstat_calls=fail_at_read=0;final_named_at=2;
    change_on_final=cancel_on_final=fail_final_stat=zero_read=oversized_read=0;alter_output=NULL;
    short_limit=8191;stream_size=131079;
    negotiated_limit=BP_COALESCE_BYTES;trace_count=clock_fault=clock_reverse=0;
    memset(map,0,sizeof map);memcpy(map,"BWMAP002",8);put32(map+8,2);put32(map+12,32);put32(map+16,20);
    size_t pool=40+20*32,offset=0;
    for(unsigned i=0;i<20;i++){
        char path[64];if(i==0)strcpy(path,"eboot.bin");else if(i==1)strcpy(path,"local.bin");else snprintf(path,sizeof path,"stream%02u.pak",i-2);
        size_t n=strlen(path);unsigned char *row=map+40+i*32;
        put32(row,(uint32_t)offset);put32(row+4,(uint32_t)n);put32(row+8,i<2?1:2);put64(row+16,i==0?11:i==1?17:131079+i-2);put64(row+24,100);
        memcpy(map+pool+offset,path,n+1);offset+=n+1;
    }
    map_size=pool+offset;put64(map+24,offset);put64(map+32,bw_manifest_fnv1a64(map+40,map_size-40));
    struct bw_share_config cfg;CHECK(!bw_share_parse(&cfg,share,sizeof share-1));struct bw_source_binding b;
    CHECK(!bw_source_binding_resolve(&b,&cfg,"Fixture Title","PPSA09999"));memset(&selected,0,sizeof selected);
    strcpy(selected.source,b.source);strcpy(selected.title,b.title);strcpy(selected.game,b.game);strcpy(selected.binding_hex,b.game+10);
    memset(selected.run,'1',32);memset(selected.prepared_record_hex,'2',64);selected.eboot_size=11;selected.manifest_count=20;
    selected.manifest_hash=bw_manifest_fnv1a64(map+40,map_size-40);selected.source_kind=1;selected.source_policy=1;
    CHECK(!bw_reader_selection_serialize(&selected,selection,sizeof selection,&selection_size));bw_share_clear(&cfg);
}
static struct bp_source *open_source(void){struct bp_source *p=NULL;CHECK(!bp_source_open(&p,map,map_size,selection,selection_size,share,sizeof share-1,guard,&canceled));CHECK(p);return p;}
static void set_stream_size(uint64_t size){
    stream_size=size;for(unsigned i=2;i<20;i++)put64(map+40+i*32+16,size+i-2);
    selected.manifest_hash=bw_manifest_fnv1a64(map+40,map_size-40);put64(map+32,selected.manifest_hash);
    CHECK(!bw_reader_selection_serialize(&selected,selection,sizeof selection,&selection_size));
}
static uint32_t match(struct bp_source *p,unsigned id){char path[64];snprintf(path,sizeof path,"/app0/stream%02u.pak",id);uint32_t out=99;CHECK(bp_source_match(p,path,strlen(path),stream_size+id,&out)==1);CHECK(out==id+2);return out;}
static int empty_witness(const struct bp_read_witness *w){struct bp_read_witness zero;memset(&zero,0,sizeof zero);return !memcmp(w,&zero,sizeof zero);}
int main(void){
    unsigned char out[140000];struct bp_source *p;uint32_t id=99;unsigned before,cold_reads;
    fixture();p=open_source();cases++;struct bp_connect_diagnostic connect_diagnostic;
    memset(&connect_diagnostic,0xa5,sizeof connect_diagnostic);bp_source_connect_diagnostic(p,&connect_diagnostic);
    CHECK(connect_diagnostic.stage==BP_CONNECT_NONE&&connect_diagnostic.options_returned==5&&!connect_diagnostic.ntstatus_valid);
    CHECK(bp_source_match(p,"/app0/local.bin",15,17,&id)==0);CHECK(bp_source_match(p,"/app0/missing",13,1,&id)==0);
    CHECK(bp_source_match(p,"/app0/stream00.pak",18,1,&id)==-1);CHECK(bp_source_match(p,"/app0/../file",13,1,&id)==-1);
    CHECK(bp_source_match(p,"/app0/Stream00.pak",18,131079,&id)==0);CHECK(bp_source_match(p,"/other/stream00.pak",19,131079,&id)==-1);
    id=match(p,0);cold_reads=reads;CHECK(bp_source_read(p,id,65530,out,65550)==65549);
    CHECK(stat_calls==2&&fstat_calls==2&&reads-cold_reads==17);
    for(size_t i=0;i<65549;i++)CHECK(out[i]==body(0,65530+i));
    before=reads;CHECK(bp_source_read(p,id,65530,out,9)==9);CHECK(reads==before);CHECK(stat_calls==4&&fstat_calls==4);CHECK(bp_source_cache_hits(p)>0);CHECK(bp_source_network_bytes(p)==131079);
    CHECK(bp_source_read(p,id,131079,out,8)==0);CHECK(bp_source_read(p,id,UINT64_MAX,out,8)==0);CHECK(bp_source_read(p,id,0,NULL,0)==0);
    CHECK(bp_source_read(p,id,0,NULL,1)==-EFAULT);CHECK(bp_source_read(p,0,0,out,1)==-EINVAL);CHECK(!bp_source_close(p));
    fixture();p=open_source();cases++;for(unsigned i=0;i<18;i++){id=match(p,i);CHECK(bp_source_read(p,id,0,out,1)==1);CHECK(out[0]==body(i,0));}CHECK(!bp_source_close(p));
    fixture();p=open_source();cases++;id=match(p,0);fail_read=1;before=destroys;CHECK(bp_source_read(p,id,0,out,1)==-EIO);CHECK(destroys==before+1&&pending_drained==1);before=reads;CHECK(bp_source_read(p,id,0,out,1)==-EIO);CHECK(reads==before);CHECK(!bp_source_close(p));
    fixture();p=open_source();cases++;id=match(p,0);CHECK(bp_source_read(p,id,0,out,1)==1);changed=1;CHECK(bp_source_read(p,id,0,out,1)==-EIO);CHECK(!bp_source_close(p));
    fixture();p=open_source();cases++;id=match(p,0);change_after_read=1;CHECK(bp_source_read(p,id,0,out,1)==-EIO);CHECK(!bp_source_close(p));
    fixture();p=open_source();cases++;id=match(p,0);canceled=1;CHECK(bp_source_read(p,id,0,out,1)==-ECANCELED);CHECK(!bp_source_close(p));
    fixture();p=open_source();cases++;id=match(p,0);cancel_after_read=1;CHECK(bp_source_read(p,id,0,out,1)==-ECANCELED);CHECK(!bp_source_close(p));
    fixture();p=open_source();cases++;id=match(p,0);CHECK(bp_source_read(p,id,0,out,1)==1);fail_close=1;CHECK(bp_source_close(p)==-1);
    fixture();cases++;unsigned calls=connects;map[42]^=1;p=NULL;CHECK(bp_source_open(&p,map,map_size,selection,selection_size,share,sizeof share-1,guard,&canceled)<0&&!p);CHECK(calls==connects);
    fixture();cases++;selected.eboot_size=12;CHECK(!bw_reader_selection_serialize(&selected,selection,sizeof selection,&selection_size));p=NULL;CHECK(bp_source_open(&p,map,map_size,selection,selection_size,share,sizeof share-1,guard,&canceled)<0&&!p);CHECK(calls==connects);
    fixture();cases++;const char dns[]="server=example.invalid\nshare=fixture\nuser=fixture\npassword=\n";p=NULL;CHECK(bp_source_open(&p,map,map_size,selection,selection_size,dns,sizeof dns-1,guard,&canceled)<0&&!p);CHECK(calls==connects);
    fixture();cases++;p=open_source();id=match(p,0);CHECK(bp_source_read(p,id,0,out,1)==1);CHECK(!bp_source_disconnect(p));CHECK(bp_source_match(p,"/app0/local.bin",15,17,&id)==0);CHECK(bp_source_read(p,2,0,out,1)==-EIO);CHECK(!bp_source_close(p));
    for(unsigned policy=1;policy<=2;policy++){
        fixture();cases++;selected.prepared_policy=policy;memcpy(selected.prepared_game,selected.title,9);selected.prepared_game[9]='_';memset(selected.prepared_game+10,'3',64);memset(selected.provenance_hex,'4',64);
        CHECK(!bw_reader_selection_serialize(&selected,selection,sizeof selection,&selection_size));p=open_source();id=match(p,0);CHECK(bp_source_read(p,id,9,out,5)==5);CHECK(out[0]==body(0,9));CHECK(!bp_source_close(p));
    }
    fixture();cases++;calls=connects;p=NULL;CHECK(!bp_source_admit(&p,map,map_size,selection,selection_size,share,sizeof share-1,guard,&canceled));CHECK(connects==calls);
    id=match(p,0);CHECK(bp_source_read(p,id,0,out,1)==-EIO);CHECK(connects==calls);CHECK(!bp_source_connect(p));CHECK(connects==calls+1);CHECK(bp_source_read(p,id,0,out,1)==1);CHECK(!bp_source_close(p));
    fixture();cases++;calls=connects;p=NULL;CHECK(!bp_source_admit(&p,map,map_size,selection,selection_size,share,sizeof share-1,guard,&canceled));CHECK(!bp_source_close(p));CHECK(calls==connects);
    fixture();cases++;struct bw_share_config cfg;struct bw_source_binding raw,profile;CHECK(bw_source_profile_enabled()==1);CHECK(!bw_share_parse(&cfg,share,sizeof share-1));
    CHECK(!bw_source_binding_init(&raw,&cfg,"Fixture Title","PPSA04263"));CHECK(!bw_source_binding_resolve(&profile,&cfg,"Fixture Title","PPSA04263"));CHECK(strcmp(raw.game,profile.game)!=0);
    strcpy(selected.title,profile.title);strcpy(selected.game,profile.game);strcpy(selected.binding_hex,profile.game+10);CHECK(!bw_reader_selection_serialize(&selected,selection,sizeof selection,&selection_size));p=open_source();CHECK(!bp_source_close(p));
    cases++;strcpy(selected.game,raw.game);strcpy(selected.binding_hex,raw.game+10);CHECK(!bw_reader_selection_serialize(&selected,selection,sizeof selection,&selection_size));calls=connects;p=NULL;
    CHECK(bp_source_admit(&p,map,map_size,selection,selection_size,share,sizeof share-1,guard,&canceled)<0&&!p);CHECK(connects==calls);bw_share_clear(&cfg);
    fixture();cases++;p=open_source();id=match(p,0);struct bp_read_witness witness;unsigned char digest[32],expected[65536];
    memset(&witness,0xa5,sizeof witness);CHECK(bp_source_read_observed(p,id,65530,out,65550,&witness)==65549);
    for(size_t i=0;i<sizeof expected;i++)expected[i]=body(0,65530+i);cp_sha_bytes(expected,sizeof expected,digest);
    CHECK(witness.valid==1&&witness.map_id==id&&witness.offset==65530&&witness.requested==65550&&witness.returned==65549);
    CHECK(witness.digest_offset==65530&&witness.digest_size==65536&&!memcmp(digest,witness.digest_sha256,sizeof digest));
    CHECK(witness.fetched_payload_bytes==131079);CHECK(!bp_source_close(p));
    fixture();cases++;p=open_source();id=match(p,0);alter_output=out;CHECK(bp_source_read_observed(p,id,7,out,5,&witness)==5);
    CHECK(stat_calls==2&&fstat_calls==2&&out[0]==0xee);for(size_t i=0;i<5;i++)expected[i]=body(0,7+i);cp_sha_bytes(expected,5,digest);
    CHECK(witness.valid&&witness.digest_size==5&&!memcmp(digest,witness.digest_sha256,sizeof digest));CHECK(!bp_source_close(p));
    fixture();cases++;p=open_source();id=match(p,0);fail_read=1;before=destroys;memset(&witness,0xa5,sizeof witness);
    CHECK(bp_source_read_observed(p,id,0,out,5,&witness)==-EIO);CHECK(empty_witness(&witness)&&destroys==before+1);CHECK(!bp_source_close(p));
    fixture();cases++;p=open_source();id=match(p,0);cancel_on_final=1;memset(&witness,0xa5,sizeof witness);
    CHECK(bp_source_read_observed(p,id,7,out,5,&witness)==-ECANCELED);CHECK(stat_calls==2&&fstat_calls==2&&empty_witness(&witness));CHECK(!bp_source_close(p));
    fixture();cases++;p=open_source();id=match(p,0);change_on_final=1;memset(&witness,0xa5,sizeof witness);
    CHECK(bp_source_read_observed(p,id,7,out,5,&witness)==-EIO);CHECK(stat_calls==2&&fstat_calls==2&&empty_witness(&witness));CHECK(!bp_source_close(p));
    fixture();cases++;p=open_source();id=match(p,0);memset(&witness,0xa5,sizeof witness);before=reads;
    CHECK(bp_source_read_observed(p,id,131079,out,5,&witness)==0);CHECK(empty_witness(&witness)&&reads==before);CHECK(!bp_source_close(p));
    fixture();cases++;p=NULL;borrowed_storage[0]=0x31;borrowed_storage[sizeof borrowed_storage-1]=0x79;
    CHECK(!bp_source_admit(&p,map,map_size,selection,selection_size,share,sizeof share-1,guard,&canceled));
    CHECK(!bp_source_connect_with_storage(p,borrowed_storage,sizeof borrowed_storage));
    CHECK(borrowed_storage[0]==0x31&&borrowed_storage[sizeof borrowed_storage-1]==0x79);
    id=match(p,0);CHECK(bp_source_read_observed(p,id,7,out,5,&witness)==5&&witness.valid&&witness.returned==5);
    before=reads;CHECK(bp_source_read(p,id,7,out,5)==5&&reads==before&&out[0]==body(0,7));CHECK(!bp_source_close(p));
    CHECK(borrowed_storage[sizeof borrowed_storage-1]==0x79);
    fixture();cases++;p=NULL;CHECK(!bp_source_admit(&p,map,map_size,selection,selection_size,share,sizeof share-1,guard,&canceled));
    CHECK(!bp_source_connect_with_storage(p,borrowed_storage,sizeof borrowed_storage));id=match(p,0);fail_read=1;before=pending_drained;
    borrowed_storage[0]=0x31;
    CHECK(bp_source_read_observed(p,id,0,out,5,&witness)==-EIO&&empty_witness(&witness));CHECK(pending_drained==before+1&&borrowed_storage[0]==0x31);CHECK(!bp_source_close(p));
    fixture();cases++;p=NULL;CHECK(!bp_source_admit(&p,map,map_size,selection,selection_size,share,sizeof share-1,guard,&canceled));
    CHECK(!bp_source_connect_with_storage(p,borrowed_storage,sizeof borrowed_storage));id=match(p,0);cancel_on_final=1;
    CHECK(bp_source_read_observed(p,id,7,out,5,&witness)==-ECANCELED&&empty_witness(&witness));CHECK(!bp_source_close(p));
    /* A full1MiB cold run uses one payload call and the original four fences.
     * Warm replay keeps the fences and issues no payload calls. */
    fixture();cases++;set_stream_size(2u*1024u*1024u+7u);short_limit=BP_COALESCE_BYTES;
    p=open_source();id=match(p,0);before=reads;unsigned opens_before=opens;
    CHECK(bp_source_read_observed(p,id,0,large_out,sizeof large_out,&witness)==(int64_t)sizeof large_out);
    CHECK(stat_calls==2&&fstat_calls==2&&reads-before==1&&opens==opens_before+1);
    CHECK(trace_count==1&&trace[0].at==0&&trace[0].requested==BP_COALESCE_BYTES);
    for(size_t i=0;i<sizeof large_expected;i++)large_expected[i]=body(0,i);
    CHECK(!memcmp(large_out,large_expected,sizeof large_out));
    CHECK(witness.valid&&witness.returned==sizeof large_out&&witness.digest_size==65536);
    cp_sha_bytes(large_expected,65536,digest);CHECK(!memcmp(digest,witness.digest_sha256,sizeof digest));
    before=reads;CHECK(bp_source_read(p,id,0,large_out,sizeof large_out)==(int64_t)sizeof large_out);
    CHECK(stat_calls==4&&fstat_calls==4&&reads==before&&opens==opens_before+1);
    CHECK(bp_source_network_bytes(p)==sizeof large_out);
    struct bp_source_performance perf;bp_source_performance(p,&perf);
    CHECK(perf.reads==2&&perf.completed_reads==2&&!perf.failed_reads&&perf.completed_bytes==2*sizeof large_out);
    CHECK(perf.cache_hit_bytes==sizeof large_out&&perf.payload_bytes==sizeof large_out&&perf.cache_hits==16&&perf.cache_misses==16);
    CHECK(perf.coalesced_runs==1&&perf.coalesced_blocks==16&&perf.scratch_bytes==BP_COALESCE_BYTES&&perf.largest_pread_request==BP_COALESCE_BYTES);
    CHECK(perf.named_stat.calls==4&&perf.held_stat.calls==4&&perf.open.calls==1&&perf.pread.calls==1&&perf.read.calls==2);
    CHECK(perf.named_stat.nanoseconds==4000&&perf.held_stat.nanoseconds==4000&&perf.open.nanoseconds==1000&&perf.pread.nanoseconds==1000&&!perf.clock_failures);
    CHECK(!bp_source_close(p));
    /* Source mutation after payload still fails the final fence and poisons
     * any cache entries already populated by the provisional logical read. */
    fixture();cases++;p=open_source();id=match(p,0);change_on_final=1;before=destroys;
    CHECK(bp_source_read_observed(p,id,0,out,100000,&witness)==-EIO&&empty_witness(&witness));
    CHECK(stat_calls==2&&fstat_calls==2&&destroys==before+1);before=reads;
    CHECK(bp_source_read(p,id,0,out,1)==-EIO&&reads==before);CHECK(!bp_source_close(p));
    fixture();cases++;p=open_source();id=match(p,0);CHECK(bp_source_read(p,id,0,out,5)==5);
    change_on_final=1;final_named_at=stat_calls+2;before=reads;
    CHECK(bp_source_read_observed(p,id,0,out,5,&witness)==-EIO&&empty_witness(&witness));
    CHECK(stat_calls==4&&fstat_calls==4&&reads==before);CHECK(!bp_source_close(p));
    /* A transport error after a positive partial read must destroy/drain its
     * pending cache destination before returning, without a valid witness. */
    fixture();cases++;p=open_source();id=match(p,0);fail_at_read=reads+2;
    unsigned drained_before=pending_drained;before=destroys;memset(out,0xcc,16);
    CHECK(bp_source_read_observed(p,id,0,out,5,&witness)==-EIO&&empty_witness(&witness));
    CHECK(stat_calls==1&&fstat_calls==1&&destroys==before+1&&pending_drained==drained_before+1);
    CHECK(bp_source_network_bytes(p)==8191&&out[0]==0xcc);CHECK(!bp_source_close(p));
    fixture();cases++;p=open_source();id=match(p,0);zero_read=1;before=destroys;
    CHECK(bp_source_read_observed(p,id,0,out,5,&witness)==-EIO&&empty_witness(&witness));
    CHECK(stat_calls==1&&fstat_calls==1&&destroys==before+1);CHECK(!bp_source_close(p));
    fixture();cases++;p=open_source();id=match(p,0);oversized_read=1;before=destroys;
    CHECK(bp_source_read_observed(p,id,0,out,5,&witness)==-EIO&&empty_witness(&witness));
    CHECK(stat_calls==1&&fstat_calls==1&&destroys==before+1);CHECK(!bp_source_close(p));
    fixture();cases++;p=open_source();id=match(p,0);cancel_after_read=1;before=reads;
    CHECK(bp_source_read_observed(p,id,0,out,5,&witness)==-ECANCELED&&empty_witness(&witness));
    CHECK(reads==before+1&&stat_calls==1&&fstat_calls==1);CHECK(!bp_source_close(p));
    fixture();cases++;p=open_source();id=match(p,0);fail_final_stat=1;before=destroys;drained_before=pending_drained;
    CHECK(bp_source_read_observed(p,id,0,out,5,&witness)==-EIO&&empty_witness(&witness));
    CHECK(stat_calls==2&&fstat_calls==1&&destroys==before+1&&pending_drained==drained_before+1);CHECK(!bp_source_close(p));
    /* A hit splits transport runs. Planning neither counts a hit nor changes
     * LRU, and never fetches the existing middle block again. */
    fixture();cases++;set_stream_size(2u*BP_COALESCE_BYTES);short_limit=BP_COALESCE_BYTES;
    p=open_source();id=match(p,0);CHECK(bp_source_read(p,id,0,out,1)==1);
    CHECK(bp_source_read(p,id,4u*BW_CACHE_BLOCK,out,1)==1);trace_count=0;before=reads;
    CHECK(bp_source_read(p,id,0,large_out,7u*BW_CACHE_BLOCK)==7u*BW_CACHE_BLOCK&&reads==before+2);
    CHECK(trace_count==2&&trace[0].at==BW_CACHE_BLOCK&&trace[0].requested==3u*BW_CACHE_BLOCK&&trace[1].at==5u*BW_CACHE_BLOCK&&trace[1].requested==2u*BW_CACHE_BLOCK);
    for(size_t i=0;i<7u*BW_CACHE_BLOCK;i++)large_expected[i]=body(0,i);
    CHECK(!memcmp(large_out,large_expected,7u*BW_CACHE_BLOCK));bp_source_performance(p,&perf);
    CHECK(perf.cache_hit_bytes==2u*BW_CACHE_BLOCK&&perf.cache_hits==2&&perf.cache_misses==7&&perf.payload_bytes==7u*BW_CACHE_BLOCK);
    CHECK(!bp_source_close(p));
    /* Tiny random reads keep the existing one-block rounding. No arbitrary
     *1MiB read-ahead; a later outside block must still miss. */
    fixture();cases++;set_stream_size(2u*BP_COALESCE_BYTES);short_limit=BP_COALESCE_BYTES;
    p=open_source();id=match(p,0);CHECK(bp_source_read(p,id,7u*BW_CACHE_BLOCK+3u,out,1)==1);
    CHECK(trace_count==1&&trace[0].at==7u*BW_CACHE_BLOCK&&trace[0].requested==BW_CACHE_BLOCK&&out[0]==body(0,7u*BW_CACHE_BLOCK+3u));
    CHECK(bp_source_read(p,id,8u*BW_CACHE_BLOCK,out,1)==1&&trace_count==2);CHECK(!bp_source_close(p));
    /* A17-block unaligned request needs two bounded runs, no further block. */
    fixture();cases++;set_stream_size(2u*BP_COALESCE_BYTES+7u);short_limit=BP_COALESCE_BYTES;
    p=open_source();id=match(p,0);CHECK(bp_source_read(p,id,5,large_out,sizeof large_out)==sizeof large_out);
    CHECK(trace_count==2&&trace[0].at==0&&trace[0].requested==BP_COALESCE_BYTES&&trace[1].at==BP_COALESCE_BYTES&&trace[1].requested==BW_CACHE_BLOCK);
    for(size_t i=0;i<sizeof large_expected;i++)large_expected[i]=body(0,5+i);CHECK(!memcmp(large_out,large_expected,sizeof large_out));CHECK(!bp_source_close(p));
    /* Negotiation and positive short replies constrain real calls, even when
     * the complete staged run spans more than one negotiated request. */
    fixture();cases++;set_stream_size(2u*BP_COALESCE_BYTES);short_limit=BP_COALESCE_BYTES;negotiated_limit=32768;
    p=open_source();id=match(p,0);CHECK(bp_source_read(p,id,0,large_out,sizeof large_out)==sizeof large_out&&trace_count==32);
    for(unsigned i=0;i<trace_count;i++)CHECK(trace[i].requested==32768&&trace[i].at==(uint64_t)i*32768);
    bp_source_performance(p,&perf);CHECK(perf.largest_pread_request==32768&&perf.coalesced_runs==1&&perf.pread.calls==32);CHECK(!bp_source_close(p));
    fixture();cases++;set_stream_size(2u*BP_COALESCE_BYTES);short_limit=7777;negotiated_limit=49152;
    p=open_source();id=match(p,0);CHECK(bp_source_read(p,id,0,large_out,sizeof large_out)==sizeof large_out);
    CHECK(trace_count==(sizeof large_out+7776)/7777);uint64_t returned=0;
    for(unsigned i=0;i<trace_count;i++){CHECK(trace[i].at==returned&&trace[i].requested<=49152&&trace[i].returned>0&&trace[i].returned<=trace[i].requested);returned+=trace[i].returned;}
    CHECK(returned==sizeof large_out);for(size_t i=0;i<sizeof large_expected;i++)large_expected[i]=body(0,i);CHECK(!memcmp(large_out,large_expected,sizeof large_out));CHECK(!bp_source_close(p));
    fixture();cases++;p=open_source();id=match(p,0);negotiated_limit=0;before=reads;
    CHECK(bp_source_read(p,id,0,out,5)==-EIO&&reads==before);CHECK(!bp_source_close(p));
    /* Cancellation during a partial staged run cannot publish any cache entry
     * or caller bytes. A subsequent uncanceled job must fetch the run afresh. */
    fixture();cases++;set_stream_size(2u*BP_COALESCE_BYTES);p=open_source();id=match(p,0);cancel_after_read=1;memset(large_out,0xcc,sizeof large_out);
    CHECK(bp_source_read_observed(p,id,0,large_out,sizeof large_out,&witness)==-ECANCELED&&empty_witness(&witness));bp_source_performance(p,&perf);
    CHECK(perf.cache_resident==0&&perf.coalesced_runs==0&&perf.payload_bytes==8191&&perf.failed_reads==1&&perf.canceled_reads==1&&large_out[0]==0xcc);
    canceled=cancel_after_read=0;short_limit=BP_COALESCE_BYTES;trace_count=0;
    CHECK(bp_source_read(p,id,0,large_out,sizeof large_out)==sizeof large_out&&trace_count==1&&trace[0].at==0&&trace[0].requested==BP_COALESCE_BYTES);
    CHECK(!memcmp(large_out,large_expected,sizeof large_out));CHECK(!bp_source_close(p));
    /* Partial-window errors drain scratch before returning. No partial cache
     * entry or valid witness survives, and the poisoned source cannot replay. */
    fixture();cases++;set_stream_size(2u*BP_COALESCE_BYTES);p=open_source();id=match(p,0);fail_at_read=reads+2;drained_before=pending_drained;
    CHECK(bp_source_read_observed(p,id,0,large_out,sizeof large_out,&witness)==-EIO&&empty_witness(&witness)&&pending_drained==drained_before+1);bp_source_performance(p,&perf);
    CHECK(perf.cache_resident==0&&perf.coalesced_runs==0&&perf.payload_bytes==8191);before=reads;
    CHECK(bp_source_read(p,id,0,out,1)==-EIO&&reads==before);CHECK(!bp_source_close(p));
    /* Failure in a later run makes earlier provisional cache blocks unusable. */
    fixture();cases++;set_stream_size(3u*BP_COALESCE_BYTES);short_limit=BP_COALESCE_BYTES;p=open_source();id=match(p,0);
    CHECK(bp_source_read(p,id,0,out,1)==1);fail_at_read=reads+2;before=reads;drained_before=pending_drained;
    /* The first cached block is copied, a later run succeeds, and the final
     * run fails without an out-of-bounds caller destination. */
    static unsigned char two_mib[2u*BP_COALESCE_BYTES];
    CHECK(bp_source_read_observed(p,id,0,two_mib,sizeof two_mib,&witness)==-EIO&&empty_witness(&witness)&&pending_drained==drained_before+1);
    CHECK(reads==before+2);before=reads;CHECK(bp_source_read(p,id,0,out,1)==-EIO&&reads==before);CHECK(!bp_source_close(p));
    /* More than1024 blocks retain the exact64MiB payload budget and evict.
     * Reloading the oldest block must fetch again, with ordinary fences. */
    fixture();cases++;set_stream_size(66u*BP_COALESCE_BYTES);short_limit=BP_COALESCE_BYTES;p=open_source();id=match(p,0);
    for(unsigned i=0;i<65;i++){CHECK(bp_source_read(p,id,(uint64_t)i*BP_COALESCE_BYTES,large_out,sizeof large_out)==sizeof large_out);CHECK(large_out[0]==body(0,(uint64_t)i*BP_COALESCE_BYTES)&&large_out[sizeof large_out-1]==body(0,(uint64_t)(i+1)*BP_COALESCE_BYTES-1));}
    bp_source_performance(p,&perf);CHECK(perf.cache_resident==BW_CACHE_SLOTS&&perf.cache_evictions==16&&perf.coalesced_runs==65&&trace_count==65);
    before=reads;CHECK(bp_source_read(p,id,0,out,1)==1&&reads==before+1);bp_source_performance(p,&perf);CHECK(perf.cache_resident==BW_CACHE_SLOTS&&perf.cache_evictions==17);CHECK(!bp_source_close(p));
    /* Telemetry is diagnostic: failed/backward clocks preserve errno and do
     * not refuse a valid source read or publish a false identity witness. */
    fixture();cases++;p=open_source();id=match(p,0);clock_fault=1;errno=EBUSY;
    CHECK(bp_source_read(p,id,0,out,5)==5&&errno==EBUSY);bp_source_performance(p,&perf);CHECK(perf.clock_failures>0&&!perf.read.nanoseconds&&!perf.named_stat.nanoseconds);CHECK(!bp_source_close(p));
    fixture();cases++;p=open_source();id=match(p,0);clock_reverse=1;
    CHECK(bp_source_read(p,id,0,out,5)==5);bp_source_performance(p,&perf);CHECK(perf.clock_failures>0&&!perf.read.nanoseconds&&!perf.pread.nanoseconds);CHECK(!bp_source_close(p));
    memset(&perf,0xa5,sizeof perf);bp_source_performance(NULL,&perf);struct bp_source_performance empty={0};CHECK(!memcmp(&perf,&empty,sizeof perf));
    CHECK(contexts==destroys);printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"transportModelOnly\":true,\"nativeReadProved\":false,\"requestScopedFences\":true,\"coalescingMaximumBytes\":%u,\"scratchSeparateFrom64MiBCache\":true,\"cold1MiB\":{\"namedStats\":2,\"heldStats\":2,\"fullDataCalls\":1},\"cached1MiB\":{\"namedStats\":2,\"heldStats\":2,\"dataCalls\":0},\"partialThreeBlockRead\":{\"namedStats\":2,\"heldStats\":2,\"dataCalls\":17},\"contextsDestroyed\":%u,\"pendingDestinationDrainedBeforeReuse\":%u,\"telemetryClock\":\"modeled monotonic nanoseconds; faults nonauthoritative\"}\n",cases,checks,BP_COALESCE_BYTES,destroys,pending_drained);return 0;
}
