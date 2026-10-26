#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "native_register.h"
#include "native_launch_admission.h"
#include "launch_timing.h"
#include "native_send.h"
#include "native_prepared_target.h"
#include "../streamreader/copy_sha256.h"
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
struct context {const struct bw_native_register_request *request;uint32_t source_kind,source_policy,prepared_policy;unsigned char expected[32],provenance[32];char root[2048],record[2048];char selected[BW_READER_SELECTION_BYTES+1];size_t selected_size;};
static int identity(const struct stat *a,const struct stat *b){return S_ISREG(b->st_mode)&&b->st_nlink==1&&(b->st_mode&07777)==0600&&b->st_size==384&&
    a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static int pin_legacy(struct context *c){
    BW_LT_COUNT(BW_LT_RECORD_PINS,1);
    char canonical[PATH_MAX];unsigned char bytes[385],again[384],hash[32];int fd=-1,result=-1;struct stat held,after,named;
    if(!realpath(c->record,canonical)||strcmp(c->record,canonical))return -1;fd=open(c->record,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
    if(fstat(fd,&held)||!identity(&held,&held)||pread(fd,bytes,sizeof bytes,0)!=384||pread(fd,again,sizeof again,0)!=384||memcmp(bytes,again,384))goto done;
    cp_sha_bytes(bytes,384,hash);
    if(memcmp(hash,c->expected,32)||fstat(fd,&after)||lstat(c->record,&named)||!identity(&held,&after)||!identity(&after,&named)||
        !realpath(c->record,canonical)||strcmp(c->record,canonical))goto done;
    result=0;
done:
    if(close(fd))result=-1;return result;
}
static int derived_identity(const struct stat *a,const struct stat *b,uint32_t policy){
    mode_t mode=policy==BW_BATCH_POLICY?0644:0600;size_t limit=policy==BW_BATCH_POLICY?BW_BATCH_TERMINAL_LIMIT:BW_DERIVED_TERMINAL_LIMIT;
    return S_ISREG(b->st_mode)&&b->st_nlink==1&&(b->st_mode&07777)==mode&&b->st_uid==geteuid()&&a->st_uid==b->st_uid&&b->st_size>0&&
        (uint64_t)b->st_size<=limit&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int pin_derived(struct context *c){
    _Static_assert(BW_BATCH_TERMINAL_LIMIT<=BW_DERIVED_TERMINAL_LIMIT,"complete terminal buffer bound");
    char canonical[PATH_MAX];uint8_t bytes[BW_DERIVED_TERMINAL_LIMIT+1],again[BW_DERIVED_TERMINAL_LIMIT],hash[32];
    struct bw_derived_terminal_record terminal;struct bw_batch_terminal_record batch;
    const struct bw_provider_binding *original=NULL;const struct bw_derived_prepared_identity *prepared=NULL;
    const char *title=NULL;const uint8_t *provenance=NULL;uint32_t count=0;
    struct stat held,after,named;int fd=-1,result=-1;
    if(!realpath(c->record,canonical)||strcmp(c->record,canonical))return -1;
    fd=open(c->record,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
    if(fstat(fd,&held)||!derived_identity(&held,&held,c->prepared_policy))goto done;size_t size=(size_t)held.st_size;
    if(pread(fd,bytes,size+1,0)!=(ssize_t)size||pread(fd,again,size,0)!=(ssize_t)size||memcmp(bytes,again,size))goto done;
    if(c->prepared_policy==BW_BATCH_POLICY){if(bw_batch_terminal_decode(&batch,bytes,size,NULL))goto done;original=&batch.original;prepared=&batch.prepared;title=batch.title;count=batch.count;provenance=batch.provenance.sha256;}
    else if(c->prepared_policy==BW_DERIVED_PLAN_POLICY){if(bw_derived_terminal_decode(&terminal,bytes,size,NULL))goto done;original=&terminal.original;prepared=&terminal.prepared;title=terminal.title;count=terminal.count;provenance=terminal.provenance.sha256;}
    else goto done;
    const struct bw_native_register_request *r=c->request;const struct bw_reader_selection *s=r->selected;
    const struct bw_provider_binding *b=r->identity;
    if(original->kind!=b->kind||original->policy!=b->policy||
       strcmp(original->binding.source,b->binding.source)||strcmp(original->binding.game,b->binding.game)||
       strcmp(original->binding.title,b->binding.title)||memcmp(original->binding.sha256,b->binding.sha256,32)||
       prepared->policy!=c->prepared_policy||strcmp(prepared->component,s->prepared_game)||
       strcmp(title,s->title)||count!=s->manifest_count||memcmp(provenance,c->provenance,32))goto done;
    cp_sha_bytes(bytes,size,hash);
    if(memcmp(hash,c->expected,32)||fstat(fd,&after)||lstat(c->record,&named)||!derived_identity(&held,&after,c->prepared_policy)||!derived_identity(&after,&named,c->prepared_policy)||
       !realpath(c->record,canonical)||strcmp(c->record,canonical))goto done;
    result=0;
done:
    if(close(fd))result=-1;return result;
}
static int pin_current(struct context *c){
    const struct bw_native_register_request *r=c->request;
    if(r->startup){
        if((c->prepared_policy&&c->prepared_policy!=3)||bw_native_launch_admission_current(r->startup,r->data_root,r->identity,r->selected,NULL))return -1;
        if(c->prepared_policy==3){const struct bw_preload_companions *witness=NULL;return bw_native_launch_admission_companions(r->startup,&witness);}
        return 0;
    }
    if(c->prepared_policy==3)return -1; /* no raw-policy fallback into derived ownership */
    return c->prepared_policy?pin_derived(c):pin_legacy(c);
}
static int map_matches(const void *bytes,size_t size,const struct bw_reader_selection *s){
    struct bw_manifest map;if(!bytes||bw_manifest_open(&map,bytes,size)||map.count!=s->manifest_count||map.checksum!=s->manifest_hash)return -1;
    unsigned boot=0;for(uint32_t i=0;i<map.count;i++){
        struct bw_manifest_record row;if(bw_manifest_get(&map,i,&row))return -1;
        if(!strcmp(row.path,"eboot.bin")){if(boot||row.flags!=BW_MANIFEST_LOCAL||row.size!=s->eboot_size||row.size<16)return -1;boot=1;}
    }
    return boot?0:-1;
}
static int derived_map_matches(const struct bw_native_derived_snapshot *snapshot,const struct bw_reader_selection *s){
    const struct bw_derived_provenance_facts *facts=bw_derived_provenance_view(&snapshot->provenance);return !facts||map_matches(facts->plan.map,facts->plan.map_size,s)?-1:0;
}
static int batch_map_matches(const struct bw_native_batch_snapshot *snapshot,const struct bw_reader_selection *s){
    const struct bw_batch_facts *facts=bw_batch_provenance_view(&snapshot->provenance);return !facts||map_matches(facts->map,facts->map_size,s)?-1:0;
}
static int type_current(const struct context *c){
    const struct bw_native_register_request *r=c->request;
    if(!r->identity||!r->selected||(uint32_t)r->identity->kind!=c->source_kind||r->identity->policy!=c->source_policy||
       r->selected->source_kind!=c->source_kind||r->selected->source_policy!=c->source_policy||r->selected->prepared_policy!=c->prepared_policy)return 0;
    if(c->prepared_policy){char bytes[BW_READER_SELECTION_BYTES+1];size_t size=0;
        if(bw_reader_selection_serialize(r->selected,bytes,sizeof bytes,&size)||size!=c->selected_size||memcmp(bytes,c->selected,size))return 0;}
    return 1;
}
static int current(void *opaque){struct context *c=opaque;return !type_current(c)||c->request->current(c->request->guard)||!type_current(c)||pin_current(c)?-1:0;}
static enum bw_registration_state state(void *opaque){struct context *c=opaque;const struct bw_native_register_request *r=c->request;return bw_native_registration_state(r->data_root,r->apps,r->database,r->selected);}
static int post(void *opaque,enum bw_sm_operation kind,const char *root,struct bw_sm_reply *reply){struct context *c=opaque;struct bw_sm_connection connection={c->request->port,current,c};return bw_sm_post(&connection,kind,root,reply);}
static double now(void *opaque){(void)opaque;struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (double)t.tv_sec+(double)t.tv_nsec/1e9;}
static void pause_ms(void *opaque,unsigned ms){(void)opaque;struct timespec t={(time_t)(ms/1000),(long)(ms%1000)*1000000};nanosleep(&t,NULL);}
static void progress(void *opaque,const char *message){struct context *c=opaque;if(c->request->observe){struct bw_reader_event event={.phase="preflight",.message=message};c->request->observe(c->request->observer,&event);}}
int bw_native_register(const struct bw_native_register_request *r,struct bw_register_result *out){
    if(!out)return -1;memset(out,0,sizeof *out);out->message="The selected preparation is invalid or changed.";
    if(!r||!r->data_root||!r->apps||!r->database||!r->identity||!r->selected||!r->current||
       r->identity->policy!=BW_PROVIDER_POLICY_V1||(r->identity->kind!=BW_PROVIDER_FOLDER&&r->identity->kind!=BW_PROVIDER_CONTAINER))return -1;
    const struct bw_reader_selection *s=r->selected;const struct bw_source_binding *b=&r->identity->binding;
    char serialized[BW_READER_SELECTION_BYTES+1];size_t serialized_size=0;
    if(bw_reader_selection_serialize(s,serialized,sizeof serialized,&serialized_size)||s->source_kind!=(uint32_t)r->identity->kind||s->source_policy!=r->identity->policy)return -1;
    if(strnlen(b->game,sizeof b->game)!=74||strnlen(b->source,sizeof b->source)>BW_MANIFEST_MAX_PATH||strnlen(b->title,sizeof b->title)!=9||
       !memchr(s->source,0,sizeof s->source)||!memchr(s->game,0,sizeof s->game)||!memchr(s->title,0,sizeof s->title)||
       strnlen(s->binding_hex,sizeof s->binding_hex)!=64||strnlen(s->prepared_record_hex,sizeof s->prepared_record_hex)!=64||strnlen(s->run,sizeof s->run)!=32||
       strcmp(s->source,b->source)||strcmp(s->title,b->title)||strcmp(s->game,b->game)||strcmp(s->binding_hex,b->game+10))return -1;
    const char *digits="0123456789abcdef";struct context c={.request=r,.source_kind=s->source_kind,.source_policy=s->source_policy,.prepared_policy=s->prepared_policy};unsigned nonzero=0;
    for(unsigned i=0;i<32;i++){
        if(!strchr(digits,s->run[i]))return -1;nonzero|=s->run[i]!='0';const char *a=strchr(digits,s->prepared_record_hex[2*i]),*v=strchr(digits,s->prepared_record_hex[2*i+1]);
        if(!a||!v)return -1;c.expected[i]=(unsigned char)(((a-digits)<<4)|(v-digits));
        if(c.prepared_policy){const char *x=strchr(digits,s->provenance_hex[2*i]),*y=strchr(digits,s->provenance_hex[2*i+1]);
            if(!x||!y)return -1;c.provenance[i]=(unsigned char)(((x-digits)<<4)|(y-digits));}
    }
    if(!nonzero)return -1;char component[75];if(bw_native_selected_root(s,component))return -1;
    if(c.prepared_policy){c.selected_size=serialized_size;memcpy(c.selected,serialized,serialized_size+1);}
    int n=snprintf(c.root,sizeof c.root,"%s/games/%s",r->data_root,component);
    if(n<0||(size_t)n>=sizeof c.root)return -1;
    if(c.prepared_policy==BW_BATCH_POLICY){
        if(bw_native_batch_record_path_exact(r->data_root,r->identity,component,c.expected,c.record,sizeof c.record))return -1;
    }else{
        int m=snprintf(c.record,sizeof c.record,"%s/%s/%s/prepared.bin",r->data_root,
            c.prepared_policy&&c.prepared_policy!=3?"native-derived-prepared":"native-prepared",b->game);
        if(m<0||(size_t)m>=sizeof c.record)return -1;
    }
    if(current(&c))return -1;
    struct bw_native_prepared fresh={0};out->message="The local startup files could not be verified; registration was not changed.";
    if(r->startup){
        /* The normal Ready-Play owner already hashed the required boot/module
         * set once. Reuse only its exact held proof, never a cached boolean. */
        if((c.prepared_policy&&c.prepared_policy!=3)||bw_native_launch_admission_current(r->startup,r->data_root,r->identity,s,&fresh))return -1;
        if(c.prepared_policy==3&&(fresh.prepared_policy!=3||strcmp(fresh.prepared_component,s->game)||memcmp(fresh.provenance_sha256,c.provenance,32)))return -1;
    }else if(c.prepared_policy==BW_BATCH_POLICY){
        /* Fresh destination LOCAL bodies only; actual PC/cache admission is
         * independently owned by normal launch preflight. */
        struct bw_native_batch_snapshot snapshot={0};
        int bad=bw_native_batch_local_verify_exact(r->data_root,r->identity,component,c.expected,current,&c,&snapshot)||batch_map_matches(&snapshot,s)||
            bw_native_prepared_from_batch(&snapshot.prepared,&fresh)||fresh.prepared_policy!=BW_BATCH_POLICY||
            strcmp(fresh.prepared_component,component)||memcmp(fresh.provenance_sha256,c.provenance,32);
        bw_native_batch_snapshot_destroy(&snapshot);if(bad)return -1;
    }else if(c.prepared_policy){
        /* Registration needs fresh destination LOCAL hashes, not a claim of
         * fresh PC bodies. Launch preflight owns the actual source admission. */
        struct bw_native_derived_snapshot snapshot={0};
        int bad=bw_native_derived_send_local_verify(r->data_root,r->identity,c.expected,current,&c,&snapshot)||
            derived_map_matches(&snapshot,s)||
            bw_native_prepared_from_derived(&snapshot.prepared,&fresh)||fresh.prepared_policy!=c.prepared_policy||
            strcmp(fresh.prepared_component,component)||memcmp(fresh.provenance_sha256,c.provenance,32);
        bw_native_derived_snapshot_destroy(&snapshot);if(bad)return -1;
    }else if(BW_LT_CALL(BW_LT_LOCAL_REGISTER,bw_native_send_verify(r->data_root,r->identity,current,&c,&fresh)))return -1;
    if(fresh.state!=BW_PREPARED_VALID||(!r->startup&&!fresh.bytes_verified)||memcmp(fresh.record_sha256,c.expected,32)||current(&c))return -1;
    const struct bw_register_ops ops={&c,current,state,post,now,pause_ms,progress};int result=BW_LT_CALL(BW_LT_REGISTER_JOB,bw_register_job(c.root,&ops,out));
    if(!result&&current(&c)){out->registered=0;out->message="The selected preparation changed after registration; launch was stopped.";return -1;}return result;
}
