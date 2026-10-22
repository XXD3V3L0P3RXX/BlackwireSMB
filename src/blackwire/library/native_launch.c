#include "native_launch.h"
#include "native_prepared_target.h"
#include "../streamreader/filemap.h"
#include <stdio.h>
#include <string.h>
static int inspect_expected(const char *base,const struct bw_provider_binding *identity,
    const struct bw_native_prepared *expected,struct bw_native_prepared *out){
    if(!expected->prepared_policy)return bw_native_send_inspect(base,identity,out);
    if(expected->prepared_policy==BW_DERIVED_PLAN_POLICY){
        struct bw_native_derived_snapshot snapshot={0};
        int result=bw_native_derived_send_inspect(base,identity,&snapshot);
        if(!result)result=bw_native_prepared_from_derived(&snapshot.prepared,out);
        bw_native_derived_snapshot_destroy(&snapshot);return result;
    }
    if(expected->prepared_policy==BW_BATCH_POLICY){
        struct bw_native_batch_snapshot snapshot={0};
        int result=bw_native_batch_inspect_exact(base,identity,expected->prepared_component,expected->record_sha256,&snapshot);
        if(!result)result=bw_native_prepared_from_batch(&snapshot.prepared,out);
        bw_native_batch_snapshot_destroy(&snapshot);return result;
    }
    return -1;
}
int bw_native_launch_selection(const char *base,const struct bw_provider_binding *identity,const struct bw_native_prepared *expected,const char *run,struct bw_reader_selection *out){
    if(!out)return -1;memset(out,0,sizeof *out);
    if(!base||!identity||identity->policy!=BW_PROVIDER_POLICY_V1||
       (identity->kind!=BW_PROVIDER_FOLDER&&identity->kind!=BW_PROVIDER_CONTAINER)||
       !expected||expected->state!=BW_PREPARED_VALID||!run||strnlen(run,33)!=32)return -1;
    const struct bw_source_binding *binding=&identity->binding;
    unsigned nonzero=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",run[i]))return -1;nonzero|=run[i]!='0';}if(!nonzero)return -1;
    struct bw_native_prepared before={0},after={0};struct bw_files files={0};struct bw_reader_selection selected={0};char root[1024];int result=-1;
    if(inspect_expected(base,identity,expected,&before)||before.state!=BW_PREPARED_VALID||strcmp(expected->game,binding->game)||
       before.prepared_policy!=expected->prepared_policy||strcmp(before.prepared_component,expected->prepared_component)||
       before.provenance_size!=expected->provenance_size||memcmp(before.provenance_sha256,expected->provenance_sha256,32)||
       memcmp(before.record_sha256,expected->record_sha256,32))goto done;
    const char *target=before.prepared_policy?before.prepared_component:binding->game;
    int n=snprintf(root,sizeof root,"%s/games/%s",base,target);if(n<0||(size_t)n>=sizeof root||bw_files_load(&files,root,"blackwire.files"))goto done;
    strcpy(selected.source,binding->source);strcpy(selected.game,binding->game);strcpy(selected.title,binding->title);strcpy(selected.run,run);strcpy(selected.binding_hex,binding->game+10);
    for(unsigned i=0;i<32;i++)snprintf(selected.prepared_record_hex+2*i,3,"%02x",before.record_sha256[i]);
    selected.manifest_count=bw_files_count(&files);selected.manifest_hash=bw_files_checksum(&files);
    selected.source_kind=(uint32_t)identity->kind;selected.source_policy=identity->policy;
    if(before.prepared_policy){selected.prepared_policy=before.prepared_policy;strcpy(selected.prepared_game,before.prepared_component);
        for(unsigned i=0;i<32;i++)snprintf(selected.provenance_hex+2*i,3,"%02x",before.provenance_sha256[i]);}
    for(uint32_t i=0;i<selected.manifest_count;i++){struct bw_manifest_record row;if(bw_files_get(&files,i,&row))goto done;if(!strcmp(row.path,"eboot.bin")&&row.flags==BW_MANIFEST_LOCAL)selected.eboot_size=row.size;}
    if(!selected.eboot_size||selected.eboot_size>UINT64_C(2147483648)||inspect_expected(base,identity,expected,&after)||after.state!=BW_PREPARED_VALID||
       memcmp(before.record_sha256,after.record_sha256,32))goto done;
    char serialized[BW_READER_SELECTION_BYTES+1];size_t serialized_size=0;
    if(bw_reader_selection_serialize(&selected,serialized,sizeof serialized,&serialized_size))goto done;
    *out=selected;result=0;
done:
    bw_files_destroy(&files);return result;
}

#include "../streamreader/copy_sha256.h"
static int snapshot_binding_same(const struct bw_provider_binding *a,const struct bw_provider_binding *b){
    return a&&b&&a->kind==b->kind&&a->policy==b->policy&&
        memchr(a->binding.source,0,sizeof a->binding.source)&&memchr(b->binding.source,0,sizeof b->binding.source)&&
        memchr(a->binding.game,0,sizeof a->binding.game)&&memchr(b->binding.game,0,sizeof b->binding.game)&&
        memchr(a->binding.title,0,sizeof a->binding.title)&&memchr(b->binding.title,0,sizeof b->binding.title)&&
        !strcmp(a->binding.source,b->binding.source)&&!strcmp(a->binding.game,b->binding.game)&&
        !strcmp(a->binding.title,b->binding.title)&&!memcmp(a->binding.sha256,b->binding.sha256,32);
}
static int snapshot_prepared_same(const struct bw_derived_prepared_identity *a,const struct bw_derived_prepared_identity *b){
    return a->policy==b->policy&&memchr(a->domain,0,sizeof a->domain)&&memchr(b->domain,0,sizeof b->domain)&&
        memchr(a->component,0,sizeof a->component)&&memchr(b->component,0,sizeof b->component)&&
        !strcmp(a->domain,b->domain)&&!strcmp(a->component,b->component)&&
        !memcmp(a->policy_sha256,b->policy_sha256,32)&&!memcmp(a->namespace_sha256,b->namespace_sha256,32)&&
        !memcmp(a->commitment_sha256,b->commitment_sha256,32);
}
int bw_native_launch_batch_snapshot(const struct bw_provider_binding *identity,const struct bw_native_batch_snapshot *snapshot,const char *run,struct bw_reader_selection *out){
    if(!out)return -1;memset(out,0,sizeof *out);
    if(!identity||identity->kind!=BW_PROVIDER_FOLDER||identity->policy!=BW_PROVIDER_POLICY_V1||
       !snapshot||!run||strnlen(run,33)!=32)return -1;
    const struct bw_batch_terminal_record *r=&snapshot->terminal;
    const struct bw_batch_facts *f=bw_batch_provenance_view(&snapshot->provenance);
    struct bw_native_prepared prepared={0};struct bw_manifest map={0};struct bw_reader_selection selected={0};
    uint8_t terminal[BW_BATCH_TERMINAL_LIMIT],hash[32];size_t size=0;const char *error=NULL;
    if(!f||bw_native_prepared_from_batch(&snapshot->prepared,&prepared)||
       bw_batch_terminal_encode(r,terminal,sizeof terminal,&size,&error)||size!=snapshot->prepared.terminal_size)return -1;
    cp_sha_bytes(terminal,size,hash);
    if(memcmp(hash,snapshot->prepared.terminal_sha256,32)||!snapshot_binding_same(identity,&r->original)||
       !snapshot_binding_same(identity,&f->original)||!snapshot_prepared_same(&r->prepared,&snapshot->prepared.prepared)||
       !snapshot_prepared_same(&r->prepared,&f->prepared)||strcmp(prepared.game,identity->binding.game)||
       !memchr(prepared.run,0,sizeof prepared.run)||strcmp(prepared.run,r->run)||
       strcmp(prepared.prepared_component,r->prepared.component)||prepared.local_count!=r->local_count||
       prepared.stream_count!=r->stream_count||prepared.entries!=r->entries||prepared.local_bytes!=r->local_bytes||
       prepared.remote_bytes!=r->remote_bytes||prepared.allocated_bytes!=r->allocated_bytes||
       prepared.provenance_size!=r->provenance.size||memcmp(prepared.provenance_sha256,r->provenance.sha256,32)||
       !snapshot->provenance_bytes||snapshot->provenance_size!=r->provenance.size||snapshot->provenance_size>BW_BATCH_PROVENANCE_LIMIT||
       !f->map||!f->digests||!f->count||f->count>BW_BATCH_MAX_RECORDS||f->map_size>40u+BW_BATCH_MAX_RECORDS*(32u+241u)||
       f->digest_size!=56u+32u*f->count||f->map_size!=r->map.size||f->digest_size!=r->digests.size||
       f->count!=r->count||f->local_count!=r->local_count||f->stream_count!=r->stream_count||
       f->local_bytes!=r->local_bytes||f->remote_bytes!=r->remote_bytes||f->bootstrap_bytes!=r->bootstrap_bytes)return -1;
    cp_sha_bytes(snapshot->provenance_bytes,snapshot->provenance_size,hash);if(memcmp(hash,r->provenance.sha256,32))return -1;
    cp_sha_bytes(f->map,f->map_size,hash);if(memcmp(hash,r->map.sha256,32)||memcmp(hash,f->map_sha256,32))return -1;
    cp_sha_bytes(f->digests,f->digest_size,hash);if(memcmp(hash,r->digests.sha256,32)||memcmp(hash,f->digests_sha256,32)||
       bw_manifest_open(&map,f->map,f->map_size)||map.count!=r->count)return -1;
    strcpy(selected.source,identity->binding.source);strcpy(selected.game,identity->binding.game);
    strcpy(selected.title,identity->binding.title);strcpy(selected.run,run);strcpy(selected.binding_hex,identity->binding.game+10);
    strcpy(selected.prepared_game,prepared.prepared_component);selected.prepared_policy=BW_BATCH_POLICY;
    selected.source_kind=(uint32_t)identity->kind;selected.source_policy=identity->policy;
    selected.manifest_count=map.count;selected.manifest_hash=map.checksum;
    for(unsigned i=0;i<32;i++){
        snprintf(selected.prepared_record_hex+2*i,3,"%02x",prepared.record_sha256[i]);
        snprintf(selected.provenance_hex+2*i,3,"%02x",prepared.provenance_sha256[i]);
    }
    for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&map,i,&row))return -1;
        if(!strcmp(row.path,"eboot.bin")&&row.flags==BW_MANIFEST_LOCAL)selected.eboot_size=row.size;}
    char serialized[BW_READER_SELECTION_BYTES+1];size_t serialized_size=0;
    if(!selected.eboot_size||selected.eboot_size>UINT64_C(2147483648)||
       bw_reader_selection_serialize(&selected,serialized,sizeof serialized,&serialized_size))return -1;
    *out=selected;return 0;
}
