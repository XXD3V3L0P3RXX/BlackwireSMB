#include "reader_preflight.h"
#include "native_launch_admission.h"
#include "launch_timing.h"
#include "native_derived_context.h"
#include "native_batch_context.h"
#include "native_prepared_target.h"
#include "../streamreader/copy_sha256.h"
#include <float.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int image_same(const struct smb2_stat_64 *a,const struct smb2_stat_64 *b) {
    return a->smb2_type==SMB2_TYPE_FILE && b->smb2_type==SMB2_TYPE_FILE &&
        a->smb2_ino==b->smb2_ino && a->smb2_size==b->smb2_size &&
        a->smb2_mtime==b->smb2_mtime && a->smb2_mtime_nsec==b->smb2_mtime_nsec &&
        a->smb2_ctime==b->smb2_ctime && a->smb2_ctime_nsec==b->smb2_ctime_nsec;
}
/* Exact provider LE metadata domain, not an invented durable receipt. */
static void reader_image_generation(const struct bw_reader_data_admission *a,uint8_t out[32]) {
    bw_provider_image_generation(&a->identity,&a->image,a->map_sha256,out);
}
static int legacy_preflight(const struct bw_reader_preflight *p,struct bw_native_prepared *out) {
    if(p&&p->error)*p->error="Invalid or cancelled launch request";
    if(p&&p->data_admission)memset(p->data_admission,0,sizeof *p->data_admission);
    if(!out)return -1;memset(out,0,sizeof *out);out->state=BW_PREPARED_UNKNOWN;
    if(!p||!p->data_root||!p->connection||!p->selected||!p->source||!p->source->smb||!p->current||p->current(p->opaque))return -1;
    const struct bw_reader_selection *s=p->selected;struct bw_provider_binding identity;
    struct bw_source_provider *provider=NULL;struct bw_container_smb *image=NULL;
    struct bw_native_prepared checked={0};struct bw_reader_data_admission admission={0};int result=-1;
    const char *failure="The selected PC source binding changed";
    if(!memchr(s->source,0,sizeof s->source)||!memchr(s->game,0,sizeof s->game)||!memchr(s->title,0,sizeof s->title)||
       !memchr(s->run,0,sizeof s->run)||!memchr(s->binding_hex,0,sizeof s->binding_hex)||!memchr(s->prepared_record_hex,0,sizeof s->prepared_record_hex)||
       s->source_policy!=BW_PROVIDER_POLICY_V1||(s->source_kind!=BW_PROVIDER_FOLDER&&s->source_kind!=BW_PROVIDER_CONTAINER)||
       bw_provider_binding_init(&identity,p->connection,s->source,s->title,(enum bw_provider_kind)s->source_kind)||
       strcmp(identity.binding.game,s->game)||strcmp(identity.binding.game+10,s->binding_hex))goto done;
    uint8_t expected[32];const char *digits="0123456789abcdef";
    failure="The selected preparation record hash is invalid";
    if(strlen(s->prepared_record_hex)!=64)goto done;
    for(unsigned i=0;i<32;i++) {
        const char *a=strchr(digits,s->prepared_record_hex[2*i]),*b=strchr(digits,s->prepared_record_hex[2*i+1]);
        if(!a||!b)goto done;expected[i]=(uint8_t)(((a-digits)<<4)|(b-digits));
    }
    failure="The current PC source could not be scanned and hashed";
    provider=calloc(1,sizeof *provider);if(!provider)goto done;
    if(identity.kind==BW_PROVIDER_CONTAINER) {
        image=calloc(1,sizeof *image);
        if(!image||p->current(p->opaque)||bw_container_smb_open_owned(image,&p->source->smb,identity.binding.source)||p->current(p->opaque))goto done;
    }
    if(BW_LT_CALL(BW_LT_PC_SOURCE,bw_source_provider_prepare(provider,&identity,p->source,image,s->run,BW_PROVIDER_MAX_LOCAL_BYTES,p->current,p->opaque))||p->current(p->opaque))goto done;
    failure="The selected file map or startup executable changed";
    struct bw_manifest map;if(bw_manifest_open(&map,provider->bundle.map.bytes,provider->bundle.map.size)||map.count!=s->manifest_count||map.checksum!=s->manifest_hash)goto done;
    unsigned boot=0;for(uint32_t i=0;i<map.count;i++) {
        struct bw_manifest_record row;if(bw_manifest_get(&map,i,&row))goto done;
        if(!strcmp(row.path,"eboot.bin")){if(boot||row.flags!=BW_MANIFEST_LOCAL||row.size!=s->eboot_size||row.size<16)goto done;boot=1;}
    }
    if(!boot)goto done;
    failure="The prepared startup files, PC generation or selected record differ";
    if(BW_LT_CALL(BW_LT_LOCAL_PREFLIGHT,bw_native_send_preflight(p->data_root,&identity,expected,provider,p->current,p->opaque,&checked))||
       checked.state!=BW_PREPARED_VALID||!checked.bytes_verified)goto done;
    failure="The PC source or launch selection changed after local verification";
    if(p->current(p->opaque)||BW_LT_CALL(BW_LT_PC_FINAL_FENCE,bw_source_provider_current(provider))||p->current(p->opaque))goto done;
    admission.identity=identity;cp_sha_bytes(provider->bundle.map.bytes,provider->bundle.map.size,admission.map_sha256);
    memcpy(admission.generation_sha256,provider->bundle.generation_sha256,32);
    if(image)admission.image=image->admitted;
    admission.valid=1;result=0;
done:
    /* Destroy wrappers/tokens BEFORE close. Publish no admission if close or
     * final selection recheck failed; never zero an owned live handle. */
    if(provider){bw_source_provider_destroy(provider);free(provider);}
    if(image){if(bw_container_smb_close(image)){result=-1;failure="Closing the verified PC image failed";}free(image);}
    if(!result&&p->current(p->opaque)){result=-1;failure="The launch selection changed during source cleanup";}
    if(!result){*out=checked;if(p->data_admission)*p->data_admission=admission;}
    if(p->error)*p->error=result?failure:NULL;return result;
}

void bw_reader_data_admission_destroy(struct bw_reader_data_admission *p) {
    if(!p)return;free(p->provenance_bytes);memset(p,0,sizeof *p);
}
static int preflight_hex(const char text[65],uint8_t out[32]) {
    const char *digits="0123456789abcdef";
    if(strnlen(text,65)!=64)return -1;
    for(unsigned i=0;i<32;i++) {
        const char *a=strchr(digits,text[2*i]),*b=strchr(digits,text[2*i+1]);
        if(!a||!b)return -1;out[i]=(uint8_t)(((a-digits)<<4)|(b-digits));
    }
    return 0;
}
static int derived_selected(const struct bw_reader_selection *s,
    const struct bw_native_derived_snapshot *snapshot,const uint8_t terminal[32],const uint8_t provenance[32]) {
    const struct bw_native_derived_prepared *prepared=&snapshot->prepared;
    const struct bw_derived_provenance_facts *facts=bw_derived_provenance_view(&snapshot->provenance);
    struct bw_manifest map;uint8_t hash[32];
    if(!facts||prepared->base.state!=BW_PREPARED_VALID||prepared->prepared_policy!=BW_DERIVED_PLAN_POLICY||
       strcmp(prepared->base.game,s->game)||strcmp(prepared->prepared_component,s->prepared_game)||
       strcmp(facts->plan.identity.component,s->prepared_game)||
       memcmp(prepared->terminal_sha256,terminal,32)||memcmp(prepared->base.record_sha256,terminal,32)||
       memcmp(prepared->provenance_sha256,provenance,32)||!snapshot->provenance_bytes||
       snapshot->provenance_size!=prepared->provenance_size||!snapshot->provenance_size||
       snapshot->provenance_size>BW_DERIVED_PROVENANCE_LIMIT||
       bw_manifest_open(&map,facts->plan.map,facts->plan.map_size)||map.count!=s->manifest_count||map.checksum!=s->manifest_hash)return -1;
    cp_sha_bytes(snapshot->provenance_bytes,snapshot->provenance_size,hash);
    if(memcmp(hash,provenance,32))return -1;
    unsigned boot=0;
    for(uint32_t i=0;i<map.count;i++) {
        struct bw_manifest_record row;if(bw_manifest_get(&map,i,&row))return -1;
        if(!strcmp(row.path,"eboot.bin")) {
            if(boot||row.flags!=BW_MANIFEST_LOCAL||row.size!=s->eboot_size||row.size<16)return -1;boot=1;
        }
    }
    return boot?0:-1;
}
static int derived_preflight(const struct bw_reader_preflight *p,struct bw_native_prepared *out) {
    const char *failure="The derived launch request lacks its current DATA lease";int result=-1,retained=0;
    struct bw_native_derived_snapshot history={0},verified={0};
    struct bw_source_provider *provider=NULL;struct bw_native_derived_context *context=NULL;
    struct bw_reader_data_admission admission={0};struct bw_native_prepared checked={0};
    struct bw_provider_binding identity;uint8_t terminal[32],provenance[32];
    char serialized[BW_READER_SELECTION_BYTES+1],again[BW_READER_SELECTION_BYTES+1];size_t size=0,again_size=0;
    const struct bw_reader_selection *s=p->selected;
    if(!p->data_admission||!p->data_current||!p->data_epoch||!p->game_log||
       p->data_current(p->data_opaque)||bw_reader_selection_serialize(s,serialized,sizeof serialized,&size))goto done;
    failure="The derived launch source or terminal pin is invalid";
    if(s->source_kind!=BW_PROVIDER_FOLDER||s->source_policy!=BW_PROVIDER_POLICY_V1||
       bw_provider_binding_init(&identity,p->connection,s->source,s->title,BW_PROVIDER_FOLDER)||
       strcmp(identity.binding.game,s->game)||strcmp(identity.binding.game+10,s->binding_hex)||
       preflight_hex(s->prepared_record_hex,terminal)||preflight_hex(s->provenance_hex,provenance))goto done;
    failure="The derived terminal or provenance no longer matches this selection";
    if(bw_native_derived_send_inspect(p->data_root,&identity,&history)||derived_selected(s,&history,terminal,provenance)||p->current(p->opaque))goto done;
    const struct bw_derived_provenance_facts *facts=bw_derived_provenance_view(&history.provenance);
    failure="The original PC source could not be freshly scanned and hashed";
    provider=calloc(1,sizeof *provider);context=calloc(1,sizeof *context);
    if(!provider||!context||bw_source_provider_prepare(provider,&identity,p->source,NULL,s->run,
       BW_PROVIDER_MAX_LOCAL_BYTES,p->data_current,p->data_opaque)||p->data_current(p->data_opaque)||p->current(p->opaque))goto done;
    failure="The packed startup project or original source generation changed";
    if(bw_native_derived_context_reopen(context,provider,p->data_current,p->data_opaque,p->data_epoch,facts)||p->current(p->opaque))goto done;
    failure="The derived startup files or exact terminal could not be freshly verified";
    const struct bw_native_derived_verify request={.data_root=p->data_root,.game_log=p->game_log,
        .expected_terminal_sha256=terminal,.original=provider,.project=context->project,.local=&context->local,
        .current=p->current,.opaque=p->opaque};
    if(bw_native_derived_send_verify(&request,&verified)||!verified.prepared.base.bytes_verified||
       derived_selected(s,&verified,terminal,provenance)||bw_native_prepared_from_derived(&verified.prepared,&checked)||
       p->data_current(p->data_opaque)||p->current(p->opaque))goto done;
    facts=bw_derived_provenance_view(&verified.provenance);if(!facts)goto done;
    admission.identity=identity;memcpy(admission.map_sha256,facts->plan.map_sha256,32);
    memcpy(admission.generation_sha256,provider->bundle.generation_sha256,32);
    admission.derived_claim.original=facts->original;
    strcpy(admission.derived_claim.prepared_game,s->prepared_game);strcpy(admission.derived_claim.run,s->run);
    admission.derived_claim.prepared_policy=s->prepared_policy;admission.derived_claim.manifest_count=s->manifest_count;
    admission.derived_claim.manifest_hash=s->manifest_hash;admission.derived_claim.eboot_size=s->eboot_size;
    memcpy(admission.derived_claim.provenance_sha256,provenance,32);memcpy(admission.derived_claim.receipt_sha256,terminal,32);
    memcpy(admission.derived_claim.map_sha256,facts->plan.map_sha256,32);
    memcpy(admission.derived_claim.commitment_sha256,facts->plan.identity.commitment_sha256,32);
    memcpy(admission.terminal_sha256,terminal,32);
    admission.provenance_bytes=verified.provenance_bytes;admission.provenance_size=verified.provenance_size;
    verified.provenance_bytes=NULL;verified.provenance_size=0;result=0;
done:
    /* The STREAM owner receives bytes/facts only, after all preparation views
     * are destroyed. Refused busy cleanup never frees a still-borrowed owner. */
    if(context) {
        if(bw_native_derived_context_destroy(context)){result=-1;retained=1;failure="The derived preparation context could not be safely released";}
        else free(context);
    }
    if(provider&&!retained){bw_source_provider_destroy(provider);free(provider);}
    bw_native_derived_snapshot_destroy(&verified);bw_native_derived_snapshot_destroy(&history);
    if(!result&&(p->data_current(p->data_opaque)||p->current(p->opaque)||
       bw_reader_selection_serialize(s,again,sizeof again,&again_size)||again_size!=size||memcmp(again,serialized,size))) {
        result=-1;failure="The derived launch selection changed during source cleanup";
    }
    if(!result){admission.valid=1;*p->data_admission=admission;memset(&admission,0,sizeof admission);*out=checked;}
    bw_reader_data_admission_destroy(&admission);
    if(p->error)*p->error=result?failure:NULL;return result;
}
static int batch_selected(const struct bw_reader_selection *s,const struct bw_native_batch_snapshot *snapshot,
    const uint8_t terminal[32],const uint8_t provenance[32]) {
    const struct bw_native_batch_prepared *prepared=&snapshot->prepared;
    const struct bw_batch_facts *facts=bw_batch_provenance_view(&snapshot->provenance);
    struct bw_manifest map;uint8_t hash[32];
    if(!facts||prepared->base.state!=BW_PREPARED_VALID||prepared->base.prepared_policy!=BW_BATCH_POLICY||
       strcmp(prepared->base.game,s->game)||strcmp(prepared->base.prepared_component,s->prepared_game)||
       strcmp(facts->prepared.component,s->prepared_game)||facts->prepared.policy!=BW_BATCH_POLICY||
       memcmp(prepared->terminal_sha256,terminal,32)||memcmp(prepared->base.record_sha256,terminal,32)||
       memcmp(prepared->base.provenance_sha256,provenance,32)||!snapshot->provenance_bytes||
       snapshot->provenance_size!=prepared->base.provenance_size||!snapshot->provenance_size||
       snapshot->provenance_size>BW_BATCH_PROVENANCE_LIMIT||
       bw_manifest_open(&map,facts->map,facts->map_size)||map.count!=s->manifest_count||map.checksum!=s->manifest_hash)return -1;
    cp_sha_bytes(snapshot->provenance_bytes,snapshot->provenance_size,hash);if(memcmp(hash,provenance,32))return -1;
    unsigned boot=0;
    for(uint32_t i=0;i<map.count;i++){
        struct bw_manifest_record row;if(bw_manifest_get(&map,i,&row))return -1;
        if(!strcmp(row.path,"eboot.bin")){if(boot||row.flags!=BW_MANIFEST_LOCAL||row.size!=s->eboot_size||row.size<16)return -1;boot=1;}
    }
    return boot?0:-1;
}
/* Cache04 counts its own archive reads separately from original-provider
 * traffic. The real Play owner must also bound that aggregate work. Keep this
 * synchronous DATA guard free of settings, filesystem and source operations;
 * the context/provider are destroyed before its stack-owned lease expires. */
struct batch_preflight_lease {
    const struct bw_reader_preflight *request;
    struct bw_source_smb *source;
    struct smb2_context *connection;
    double deadline;
};
static int batch_data_current(void *opaque) {
    const struct batch_preflight_lease *lease=opaque;
    struct timespec stamp;
    if(clock_gettime(CLOCK_MONOTONIC,&stamp))return -1;
    double at=(double)stamp.tv_sec+(double)stamp.tv_nsec/1e9;
    if(!lease||!lease->request||!lease->source||lease->request->source!=lease->source||
       lease->source->smb!=lease->connection||lease->source->deadline!=lease->deadline||lease->source->error||
       !(lease->deadline>=-DBL_MAX&&lease->deadline<=DBL_MAX)||at<0||at>=lease->deadline||
       lease->source->content_bytes>BW_PROVIDER_MAX_LOCAL_BYTES*3u+UINT64_C(268435456)||
       lease->source->requests>524288u)return -1;
    if(lease->request->data_current(lease->request->data_opaque)||clock_gettime(CLOCK_MONOTONIC,&stamp))return -1;
    at=(double)stamp.tv_sec+(double)stamp.tv_nsec/1e9;
    return at<0||at>=lease->deadline?-1:0;
}
static int batch_preflight(const struct bw_reader_preflight *p,struct bw_native_prepared *out) {
    const char *failure="The packed startup launch lacks its current DATA lease";int result=-1,retained=0;
    struct bw_native_batch_snapshot history={0},verified={0};
    struct bw_source_provider *provider=NULL;struct bw_native_batch_context *context=NULL;
    struct bw_reader_data_admission admission={0};struct bw_native_prepared checked={0};
    struct bw_provider_binding identity;uint8_t terminal[32],provenance[32];
    char serialized[BW_READER_SELECTION_BYTES+1],again[BW_READER_SELECTION_BYTES+1];size_t size=0,again_size=0;
    const struct bw_reader_selection *s=p->selected;
    struct batch_preflight_lease lease={.request=p,.source=p->source,.connection=p->source->smb,.deadline=p->source->deadline};
    if(!p->data_admission||!p->data_current||!p->data_epoch||!p->game_log||p->data_current(p->data_opaque)||
       batch_data_current(&lease)||bw_reader_selection_serialize(s,serialized,sizeof serialized,&size))goto done;
    failure="The packed startup source or terminal pin is invalid";
    if(!bw_reader_selection_is_batch(s)||s->source_kind!=BW_PROVIDER_FOLDER||s->source_policy!=BW_PROVIDER_POLICY_V1||
       bw_provider_binding_init(&identity,p->connection,s->source,s->title,BW_PROVIDER_FOLDER)||
       strcmp(identity.binding.game,s->game)||strcmp(identity.binding.game+10,s->binding_hex)||
       preflight_hex(s->prepared_record_hex,terminal)||preflight_hex(s->provenance_hex,provenance))goto done;
    failure="The packed startup terminal or provenance changed";
    if(bw_native_batch_inspect_exact(p->data_root,&identity,s->prepared_game,terminal,&history)||batch_selected(s,&history,terminal,provenance)||p->current(p->opaque))goto done;
    const struct bw_batch_facts *facts=bw_batch_provenance_view(&history.provenance);
    failure="The original PC source could not be freshly scanned and hashed";
    provider=calloc(1,sizeof *provider);context=calloc(1,sizeof *context);
    if(!provider||!context||bw_source_provider_prepare(provider,&identity,p->source,NULL,s->run,BW_PROVIDER_MAX_LOCAL_BYTES,
       batch_data_current,&lease)||batch_data_current(&lease)||p->current(p->opaque))goto done;
    failure="The packed startup data or original source generation changed";
    if(bw_native_batch_context_reopen(context,provider,batch_data_current,&lease,p->data_epoch,facts)||
       p->current(p->opaque))goto done;
    failure="The packed startup files or exact terminal could not be freshly verified";
    const struct bw_native_batch_verify request={.data_root=p->data_root,.game_log=p->game_log,.expected_terminal_sha256=terminal,
        .plan=bw_native_batch_context_plan(context),.plan_request=bw_native_batch_context_request(context),
        .current=p->current,.opaque=p->opaque};
    if(bw_native_batch_send_verify(&request,&verified)||!verified.prepared.base.bytes_verified||
       batch_selected(s,&verified,terminal,provenance)||bw_native_prepared_from_batch(&verified.prepared,&checked)||
       bw_native_batch_context_revalidate(context)||batch_data_current(&lease)||p->current(p->opaque))goto done;
    facts=bw_batch_provenance_view(&verified.provenance);if(!facts)goto done;
    admission.identity=identity;memcpy(admission.map_sha256,facts->map_sha256,32);
    memcpy(admission.generation_sha256,provider->bundle.generation_sha256,32);
    admission.derived_claim.original=facts->original;strcpy(admission.derived_claim.prepared_game,s->prepared_game);
    strcpy(admission.derived_claim.run,s->run);admission.derived_claim.prepared_policy=s->prepared_policy;
    admission.derived_claim.manifest_count=s->manifest_count;admission.derived_claim.manifest_hash=s->manifest_hash;
    admission.derived_claim.eboot_size=s->eboot_size;memcpy(admission.derived_claim.provenance_sha256,provenance,32);
    memcpy(admission.derived_claim.receipt_sha256,terminal,32);memcpy(admission.derived_claim.map_sha256,facts->map_sha256,32);
    memcpy(admission.derived_claim.commitment_sha256,facts->prepared.commitment_sha256,32);memcpy(admission.terminal_sha256,terminal,32);
    admission.provenance_bytes=verified.provenance_bytes;admission.provenance_size=verified.provenance_size;
    verified.provenance_bytes=NULL;verified.provenance_size=0;result=0;
done:
    if(context){if(bw_native_batch_context_destroy(context)){result=-1;retained=1;failure="The packed startup context could not be safely released";}else free(context);}
    if(provider&&!retained){bw_source_provider_destroy(provider);free(provider);}
    bw_native_batch_snapshot_destroy(&verified);bw_native_batch_snapshot_destroy(&history);
    if(!result&&(batch_data_current(&lease)||p->current(p->opaque)||
       bw_reader_selection_serialize(s,again,sizeof again,&again_size)||again_size!=size||memcmp(again,serialized,size))){
        result=-1;failure="The packed startup launch selection changed during source cleanup";
    }
    if(!result){admission.valid=1;*p->data_admission=admission;memset(&admission,0,sizeof admission);*out=checked;}
    bw_reader_data_admission_destroy(&admission);if(p->error)*p->error=result?failure:NULL;return result;
}
int bw_reader_preflight_run(const struct bw_reader_preflight *p,struct bw_native_prepared *out) {
    if(p&&p->error)*p->error="Invalid or cancelled launch request";
    if(out){memset(out,0,sizeof *out);out->state=BW_PREPARED_UNKNOWN;}
    if(p&&p->data_admission) {
        if(p->data_admission->provenance_bytes){p->data_admission->valid=0;if(p->error)*p->error="Destroy the previous derived admission before reuse";return -1;}
        memset(p->data_admission,0,sizeof *p->data_admission);
    }
    if(!out||!p||!p->data_root||!p->connection||!p->selected||!p->source||!p->source->smb||!p->current||p->current(p->opaque))return -1;
    if(p->startup){
        struct bw_provider_binding identity;struct bw_reader_data_admission admission={0};
        const char *failure="The verified startup launch no longer matches its selected receipt; choose Send to repair it.";
        if(!p->data_current||!p->data_epoch||p->data_current(p->data_opaque)||
           (p->selected->source_kind!=BW_PROVIDER_FOLDER&&p->selected->source_kind!=BW_PROVIDER_CONTAINER)||
           (p->selected->prepared_policy&&!bw_reader_selection_is_projection(p->selected))||
           (!bw_reader_selection_is_projection(p->selected)&&(p->selected->prepared_game[0]||p->selected->provenance_hex[0]))||
           bw_provider_binding_init(&identity,p->connection,p->selected->source,p->selected->title,(enum bw_provider_kind)p->selected->source_kind)||
           bw_native_launch_admission_current(p->startup,p->data_root,&identity,p->selected,out)||
           bw_native_launch_admission_source(p->startup,p->connection,p->source,p->selected,admission.map_sha256,admission.generation_sha256,&failure)||
           p->current(p->opaque)||bw_native_launch_admission_current(p->startup,p->data_root,&identity,p->selected,out)){
            memset(out,0,sizeof *out);out->state=BW_PREPARED_UNKNOWN;if(p->error)*p->error=failure;return -1;
        }
        if(bw_reader_selection_is_projection(p->selected)){
            const struct bw_preload_companions *companions=NULL;
            if(bw_native_launch_admission_companions(p->startup,&companions)){
                memset(out,0,sizeof *out);out->state=BW_PREPARED_UNKNOWN;if(p->error)*p->error="The held projected source evidence changed before DATA admission.";return -1;
            }
        }
        admission.identity=identity;admission.valid=1;if(p->data_admission)*p->data_admission=admission;
        if(p->error)*p->error=NULL;return 0;
    }
    if(bw_reader_selection_is_batch(p->selected))return batch_preflight(p,out);
    if(bw_reader_selection_is_derived(p->selected))return derived_preflight(p,out);
    if(p->selected->prepared_policy||p->selected->prepared_game[0]||p->selected->provenance_hex[0])return -1;
    return legacy_preflight(p,out);
}

static int reader_fail(struct bw_reader_container *p) {if(p)p->failed=1;return -1;}
static int reader_current(struct bw_reader_container *p) {
    if(!p||!p->ready||p->failed||!p->image||!p->current||p->current(p->opaque)||
       !image_same(&p->admitted.image,&p->image->admitted)||bw_container_validate(&p->image->container)||p->current(p->opaque)||p->failed)return reader_fail(p);
    return 0;
}
int bw_reader_container_current(struct bw_reader_container *p) {
    if(!p||p->busy)return reader_fail(p);p->busy=1;int result=reader_current(p);p->busy=0;return result;
}
int bw_reader_container_open(struct bw_reader_container *p,struct smb2_context *smb,
    const struct bw_reader_data_admission *a,const void *map_bytes,size_t map_size,int (*current)(void *),void *opaque) {
    if(!p)return -1;if(p->busy)return reader_fail(p);
    if(p->image||p->ready||p->failed||!smb||!a||!a->valid||!map_bytes||!current||
       a->identity.kind!=BW_PROVIDER_CONTAINER||a->identity.policy!=BW_PROVIDER_POLICY_V1||
       map_size<BW_MANIFEST_HEADER_SIZE||map_size>BW_MANIFEST_HEADER_SIZE+
           (size_t)BW_COMPRESSED_DATA_MAX_FILES*BW_MANIFEST_RECORD_SIZE+BW_COMPRESSED_DATA_MAX_PATH_BYTES)return -1;
    /* Acquire before the FIRST external callback; a recursive open may never
     * allocate an owner that the outer call can overwrite. */
    p->busy=1;int result=-1;
    if(current(opaque)||p->failed)goto done;
    uint8_t hash[32];reader_image_generation(a,hash);if(memcmp(hash,a->generation_sha256,32))goto done;
    cp_sha_bytes(map_bytes,map_size,hash);if(memcmp(hash,a->map_sha256,32))goto done;
    struct bw_manifest map;if(bw_manifest_open(&map,map_bytes,map_size))goto done;
    p->admitted=*a;p->map_bytes=map_bytes;p->map_size=map_size;p->current=current;p->opaque=opaque;
    p->image=calloc(1,sizeof *p->image);
    if(!p->image||bw_container_smb_open(p->image,smb,a->identity.binding.source)||current(opaque)||
       p->failed||!image_same(&a->image,&p->image->admitted)||bw_container_manifest_bind(&p->files,&p->image->container,&map)||current(opaque)||p->failed)goto done;
    p->ready=1;result=reader_current(p);
done:
    p->busy=0;return result?reader_fail(p):0;
}
static int attach_cache(struct bw_reader_container *p,struct bw_cache *cache) {
    if(reader_current(p)||!cache)return -1;
    if(p->stream.ready)return p->stream.cache==cache?0:reader_fail(p);
    struct bw_cstream_admission admission={.image=p->image,.files=&p->files,.binding=&p->admitted.identity.binding,
        .map_bytes=p->map_bytes,.map_size=p->map_size,.expected_binding_sha256=p->admitted.identity.binding.sha256,
        .expected_map_sha256=p->admitted.map_sha256,.current=p->current,.opaque=p->opaque,.cache=cache};
    return bw_compressed_stream_bind(&p->stream,&admission)?reader_fail(p):0;
}
int bw_reader_container_boot(struct bw_reader_container *p,uint64_t size,uint8_t out[32]) {
    if(out)memset(out,0,32);if(!p||!out||p->busy||size<16)return -1;
    p->busy=1;int result=-1;uint8_t bytes[32];struct bw_manifest map;
    if(reader_current(p)||bw_manifest_open(&map,p->map_bytes,p->map_size))goto done;
    for(uint32_t i=0;i<map.count;i++) {
        struct bw_manifest_record row;if(bw_manifest_get(&map,i,&row))goto done;
        if(strcmp(row.path,"eboot.bin"))continue;
        const struct bw_container_file *file=bw_container_manifest_file(&p->files,i);
        if(!file||row.flags!=BW_MANIFEST_LOCAL||row.size!=size||bw_container_file_read(file,0,bytes,16)||
           p->current(p->opaque)||bw_container_file_read(file,size-16,bytes+16,16)||reader_current(p))goto done;
        memcpy(out,bytes,32);result=0;break;
    }
done:
    p->busy=0;return result?reader_fail(p):0;
}
int bw_reader_container_load(struct bw_reader_container *p,struct bw_cache *cache,uint32_t id,
    uint64_t block,uint8_t *dest,size_t capacity,size_t *out_size) {
    if(out_size)*out_size=0;if(!p||p->busy||!out_size)return -1;
    p->busy=1;int result=attach_cache(p,cache);
    if(!result)result=bw_compressed_stream_load_block(&p->stream,id,block,dest,capacity,out_size);
    if(p->stream.failed)p->failed=1;if(p->failed){result=-1;*out_size=0;}p->busy=0;return result;
}
int bw_reader_container_get(struct bw_reader_container *p,struct bw_cache *cache,uint32_t id,
    uint64_t block,const uint8_t **bytes,size_t *size) {
    if(bytes)*bytes=NULL;if(size)*size=0;if(!p||p->busy||!bytes||!size)return -1;
    p->busy=1;int result=attach_cache(p,cache);
    if(!result)result=bw_compressed_stream_cached_get(&p->stream,id,block,bytes,size);
    if(p->stream.failed)p->failed=1;if(p->failed){result=-1;*bytes=NULL;*size=0;}p->busy=0;return result;
}
int bw_reader_container_close(struct bw_reader_container *p) {
    if(!p)return -1;if(p->busy)return reader_fail(p);
    bw_compressed_stream_destroy(&p->stream);bw_container_manifest_destroy(&p->files);
    int result=0;if(p->image){result=bw_container_smb_close(p->image);free(p->image);}
    memset(p,0,sizeof *p);return result;
}
