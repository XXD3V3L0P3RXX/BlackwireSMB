#include "native_prepared_target.h"
#include "native_startup_hint.h"
#include <string.h>
static int component(const char *value,const char *title){
    if(strnlen(value,75)!=74||memcmp(value,title,9)||value[9]!='_')return -1;
    for(unsigned i=10;i<74;i++)if(!strchr("0123456789abcdef",value[i]))return -1;return 0;
}
int bw_native_prepared_from_derived(const struct bw_native_derived_prepared *in,struct bw_native_prepared *out){
    if(!out)return -1;memset(out,0,sizeof *out);out->state=BW_PREPARED_UNKNOWN;
    if(!in||in->base.state!=BW_PREPARED_VALID||in->prepared_policy!=BW_DERIVED_PLAN_POLICY||
       strnlen(in->base.game,75)!=74||component(in->prepared_component,in->base.game)||
       !strcmp(in->prepared_component,in->base.game)||strcmp(in->prepared_component,in->prepared.component)||
       in->prepared.policy!=BW_DERIVED_PLAN_POLICY||!in->provenance_size||in->provenance_size>BW_DERIVED_PROVENANCE_LIMIT||
       !in->terminal_size||in->terminal_size>BW_DERIVED_TERMINAL_LIMIT||memcmp(in->terminal_sha256,in->base.record_sha256,32))return -1;
    *out=in->base;out->prepared_policy=BW_DERIVED_PLAN_POLICY;strcpy(out->prepared_component,in->prepared_component);
    memcpy(out->provenance_sha256,in->provenance_sha256,32);out->provenance_size=in->provenance_size;return 0;
}
int bw_native_prepared_from_batch(const struct bw_native_batch_prepared *in,struct bw_native_prepared *out){
    if(!out)return -1;memset(out,0,sizeof *out);out->state=BW_PREPARED_UNKNOWN;
    if(!in||in->base.state!=BW_PREPARED_VALID||in->base.prepared_policy!=BW_BATCH_POLICY||
       strnlen(in->base.game,75)!=74||component(in->base.prepared_component,in->base.game)||
       !strcmp(in->base.prepared_component,in->base.game)||strcmp(in->base.prepared_component,in->prepared.component)||
       in->prepared.policy!=BW_BATCH_POLICY||!in->base.provenance_size||in->base.provenance_size>BW_BATCH_PROVENANCE_LIMIT||
       !in->terminal_size||in->terminal_size>BW_BATCH_TERMINAL_LIMIT||memcmp(in->terminal_sha256,in->base.record_sha256,32))return -1;
    *out=in->base;return 0;
}
int bw_native_prepared_inspect(const char *base,const struct bw_provider_binding *identity,struct bw_native_prepared *out){
    if(!out)return -1;memset(out,0,sizeof *out);out->state=BW_PREPARED_UNKNOWN;
    if(!base||!identity)return -1;
#ifdef BW_NATIVE_PRELOAD
    if(identity->kind==BW_PROVIDER_FOLDER){
        /* This is an explicit current automatic preparation policy, not a
         * fallback from a failed engine-specific candidate. Require positive
         * absence of same-source old policies: incomplete, busy, valid old
         * history or unknown close stays a migration blocker for root-owned
         * inspection/retirement. No historical state is adopted or removed. */
        struct bw_native_batch_snapshot batch={0};batch.prepared.base.state=BW_PREPARED_UNKNOWN;
        int result=bw_native_batch_send_inspect(base,identity,&batch);
        int absent=batch.prepared.base.state==BW_PREPARED_ABSENT&&(result==0||result==BW_SEND_FAILED);
        bw_native_batch_snapshot_destroy(&batch);
        if(!absent){out->retirement=BW_RETIRE_RECOVERY_REQUIRED;out->retirement_error="An older batch preparation is present or unconfirmed; inspect it before preparing the raw-folder reader";return -1;}
        struct bw_native_derived_snapshot derived={0};derived.prepared.base.state=BW_PREPARED_UNKNOWN;
        result=bw_native_derived_send_inspect(base,identity,&derived);
        absent=derived.prepared.base.state==BW_PREPARED_ABSENT&&(result==0||result==BW_SEND_FAILED);
        bw_native_derived_snapshot_destroy(&derived);
        if(!absent){out->retirement=BW_RETIRE_RECOVERY_REQUIRED;out->retirement_error="An older derived preparation is present or unconfirmed; inspect it before preparing the raw-folder reader";return -1;}
        /* native_send's actual ordinary state/map/index/namespace inspector
         * remains authoritative. Its reload never grants bytes_verified.
         * Packed startup hints belong to the late-reader engine pipeline and
         * must not classify streamed archive contents as missing LOCAL files. */
        result=bw_native_send_inspect(base,identity,out);
        if(!result&&out->state==BW_PREPARED_VALID){
            if(out->prepared_policy){out->state=BW_PREPARED_UNKNOWN;return -1;}
            /* In this build the same hint function retains only the pure
             * recorded STREAM -> current LOCAL metadata classification check.
             * Such a stale plain receipt must still take the existing owned
             * classification-retirement path, never be advertised ready. */
            unsigned upgrade=0;
            if(bw_native_legacy_startup_upgrade(base,identity,out,&upgrade)){out->state=BW_PREPARED_UNKNOWN;return -1;}
            out->startup_upgrade_required=upgrade;
        }
        return result;
    }
#endif
    if(identity->kind==BW_PROVIDER_FOLDER){
        struct bw_native_batch_snapshot batch={0};batch.prepared.base.state=BW_PREPARED_UNKNOWN;
        int result=bw_native_batch_send_inspect(base,identity,&batch);
        enum bw_prepared_state observed=batch.prepared.base.state;
        if(observed!=BW_PREPARED_ABSENT){
            if(!result&&observed==BW_PREPARED_VALID){
                result=bw_native_prepared_from_batch(&batch.prepared,out);
                const struct bw_batch_facts *facts=bw_batch_provenance_view(&batch.provenance);
                if(!result&&(!facts||strcmp(facts->bootstrap.prefix,BW_PBB_CURRENT_PREFIX)))out->startup_upgrade_required=1;
            }
            else out->state=observed;
            bw_native_batch_snapshot_destroy(&batch);return result;
        }
        bw_native_batch_snapshot_destroy(&batch);
    }
    if(identity->kind==BW_PROVIDER_FOLDER){
        struct bw_native_derived_snapshot snapshot={0};snapshot.prepared.base.state=BW_PREPARED_UNKNOWN;
        int result=bw_native_derived_send_inspect(base,identity,&snapshot);
        enum bw_prepared_state observed=snapshot.prepared.base.state;
        if(observed!=BW_PREPARED_ABSENT){
            if(!result&&observed==BW_PREPARED_VALID){
                result=bw_native_prepared_from_derived(&snapshot.prepared,out);
                /* A V1 descriptor-only preparation predates common packed
                 * engine bootstrap discovery. Normal Start must reevaluate
                 * it through prepare(), not replay an unchanged failing run.
                 * Actual Send returns a fresh V1 result if no batch applies. */
                if(!result)out->startup_upgrade_required=1;
            }
            else out->state=observed;
            bw_native_derived_snapshot_destroy(&snapshot);return result;
        }
        /* The derived inspector reports an observed ENOENT as ABSENT/-1.
         * Only that explicit state permits the legacy path. Busy/early errors
         * retain the UNKNOWN sentinel above and cannot become absence. */
        bw_native_derived_snapshot_destroy(&snapshot);
    }
    int result=bw_native_send_inspect(base,identity,out);
    if(!result&&out->state==BW_PREPARED_VALID&&(identity->kind==BW_PROVIDER_FOLDER||identity->kind==BW_PROVIDER_CONTAINER)){
        unsigned upgrade=0;if(bw_native_legacy_startup_upgrade(base,identity,out,&upgrade)){out->state=BW_PREPARED_UNKNOWN;return -1;}
        out->startup_upgrade_required=upgrade;
    }
    return result;
}
int bw_native_selected_root(const struct bw_reader_selection *selected,char out[75]){
    if(!out)return -1;out[0]=0;if(!selected)return -1;
    char text[BW_READER_SELECTION_BYTES+1];size_t size=0;
    if(bw_reader_selection_serialize(selected,text,sizeof text,&size))return -1;
    const char *root=selected->prepared_policy?selected->prepared_game:selected->game;
    if(component(root,selected->title))return -1;strcpy(out,root);return 0;
}
