/* Actual envelope/companion codec, explicit synthetic parent-record fixtures.
 * This does not claim a native Send, filesystem lease or launch capability. */
#define main companion_fixture_main
#include "../dashboard/lifecycle/test_source_companions.c"
#undef main
#include "reader_projection.h"
static void as_hex(const uint8_t *raw,char out[65]){for(unsigned i=0;i<32;i++)snprintf(out+2*i,3,"%02x",raw[i]);}
static void reader_case(unsigned kind,unsigned flags){
    const struct file files[]={{"fakelib/liboriginal.sprx",17,1,4},{"fakelib/README.txt",8,1,6},
        {"fakelib2/libSceAmpr.sprx",21,1,7},{"fakelib2/libchosen.so",23,1,8},{"fakelib2/README.txt",9,1,9}};
    struct fixture f;init_real(&f,files,sizeof files/sizeof *files);f.provider.kind=(enum bw_provider_kind)kind;
    struct bw_companion_projection *projection=NULL;C(!bw_companion_projection_create(&projection,&f.binding,&f.source,flags,NULL));
    struct bw_companion_tables t;C(!bw_companion_projection_tables(projection,&t));struct bw_manifest map;C(!bw_manifest_open(&map,t.map,t.map_size));
    uint8_t record[384]={0};memcpy(record,"BWNAT001",8);p32(record+8,1);p32(record+12,384);memcpy(record+16,f.run,32);memcpy(record+48,f.provider.binding.sha256,32);memcpy(record+80,f.generation,32);
    cp_sha_bytes(t.map,t.map_size,record+112);cp_sha_bytes(t.digests,t.digest_size,record+144);memset(record+176,3,64);
    p64(record+240,t.local_bytes);p64(record+248,t.remote_bytes);p32(record+264,map.count);p32(record+268,map.count);p32(record+272,t.local_count);p32(record+276,t.stream_count);
    p64(record+280,t.map_size);p64(record+288,t.digest_size);p64(record+296,48u+24u*map.count);
    p64(record+304,1);p64(record+312,2);p64(record+320,1);p64(record+328,3);cp_sha_bytes(record,352,record+352);
    struct bw_reader_selection s={0};s.source_kind=kind;s.source_policy=1;s.prepared_policy=3;s.eboot_size=16;s.manifest_count=map.count;s.manifest_hash=map.checksum;
    strcpy(s.source,f.provider.binding.source);strcpy(s.title,f.provider.binding.title);strcpy(s.game,f.provider.binding.game);strcpy(s.prepared_game,s.game);
    memset(s.run,'b',32);as_hex(f.provider.binding.sha256,s.binding_hex);uint8_t hash[32];cp_sha_bytes(record,sizeof record,hash);as_hex(hash,s.prepared_record_hex);cp_sha_bytes(t.receipt,t.receipt_size,hash);as_hex(hash,s.provenance_hex);
    struct bw_reader_projection_parts parts={record,sizeof record,f.source.map,f.source.map_size,f.source.digests,f.source.digest_size,t.digests,t.digest_size,t.receipt,t.receipt_size};
    void *envelope=NULL;size_t n=0;C(!bw_reader_projection_encode(&parts,&envelope,&n));
    struct bw_reader_projection *reader=NULL;C(!bw_reader_projection_open(&reader,&s,t.map,t.map_size,envelope,n,NULL));
    uint8_t original_hash[32],generation[32];uint64_t mtime;C(!bw_reader_projection_source(reader,original_hash,generation,&mtime));
    cp_sha_bytes(f.source.map,f.source.map_size,hash);C(!memcmp(hash,original_hash,32)&&!memcmp(generation,f.generation,32)&&mtime==(kind==2?17u:0u));
    bw_reader_projection_clear(&reader);cases++;
    const size_t changes[]={0,8,12,16,24,32,40,48,56,64,64+80,64+112,64+352,64+384,64+384+f.source.map_size,n-1};
    for(unsigned i=0;i<sizeof changes/sizeof *changes;i++){
        uint8_t *bad=malloc(n);C(bad);memcpy(bad,envelope,n);bad[changes[i]]^=1;
        C(bw_reader_projection_open(&reader,&s,t.map,t.map_size,bad,n,NULL)<0&&!reader);free(bad);cases++;
    }
    C(bw_reader_projection_open(&reader,&s,t.map,t.map_size,envelope,n-1,NULL)<0&&!reader);cases++;
    struct bw_reader_selection other=s;other.provenance_hex[0]=other.provenance_hex[0]=='0'?'1':'0';C(bw_reader_projection_open(&reader,&other,t.map,t.map_size,envelope,n,NULL)<0&&!reader);cases++;
    other=s;other.prepared_game[0]='Q';C(bw_reader_projection_open(&reader,&other,t.map,t.map_size,envelope,n,NULL)<0&&!reader);cases++;
    other=s;other.prepared_policy=0;C(bw_reader_projection_open(&reader,&other,t.map,t.map_size,envelope,n,NULL)<0&&!reader);cases++;
    struct bw_reader_projection_parts short_parts=parts;short_parts.prepared_record_size=383;void *bad=NULL;size_t bad_n=1;
    C(bw_reader_projection_encode(&short_parts,&bad,&bad_n)<0&&!bad&&!bad_n);cases++;
    C(bw_reader_projection_open(&reader,&s,t.map,t.map_size,envelope,n,NULL)==0);free(envelope);
    C(!bw_reader_projection_source(reader,original_hash,generation,&mtime)&&!memcmp(generation,f.generation,32));
    bw_reader_projection_clear(&reader);bw_companion_projection_clear(&projection);destroy(&f);cases++;
}
int main(void){reader_case(1,3);reader_case(2,3);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualEnvelopeAndCompanionCodec\":true,\"syntheticParentRecords\":true,\"runtimeAuthorityProved\":false}\n",cases,checks);return 0;}
