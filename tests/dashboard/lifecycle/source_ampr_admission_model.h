#ifndef BW_SOURCE_AMPR_ADMISSION_MODEL_H
#define BW_SOURCE_AMPR_ADMISSION_MODEL_H
/* Recording model for orchestration-only tests. Actual receipt/generation/
 * witness production is separately exercised by test_fast_ready_play.c. */
#ifndef BW_AMPR_MODEL_CHECK
#define BW_AMPR_MODEL_CHECK CHECK
#endif
static struct bw_native_launch_admission *ampr_model_owner;
static unsigned ampr_model_source_checked;
static char ampr_model_execution_token,ampr_model_removal_token;
static void ampr_model_open(struct bw_native_launch_admission *p){ampr_model_owner=p;ampr_model_source_checked=0;}
int bw_native_launch_admission_open_for_removal(struct bw_native_launch_admission **p,const char *root,
    const struct bw_provider_binding *identity,const struct bw_native_prepared *prepared,const char *run,
    int (*current)(void *),void *opaque,const char **error){
    return bw_native_launch_admission_open(p,root,identity,prepared,run,current,opaque,error);
}
int bw_native_launch_admission_source_connect(struct bw_native_launch_admission *p,const struct bw_share_config *c,
    const struct bw_reader_selection *selected,double deadline,const char **error){
    BW_AMPR_MODEL_CHECK(p&&p==ampr_model_owner&&c&&selected&&deadline>0);if(error)*error=NULL;ampr_model_source_checked=1;return 0;
}
int bw_native_launch_admission_source_ampr(struct bw_native_launch_admission *p,const struct bw_preload_source_ampr **out){
    BW_AMPR_MODEL_CHECK(p&&p==ampr_model_owner&&out);*out=NULL;if(!ampr_model_source_checked)return -1;
    *out=(const struct bw_preload_source_ampr *)&ampr_model_execution_token;return 0;
}
int bw_native_launch_admission_source_ampr_for_removal(struct bw_native_launch_admission *p,const struct bw_preload_source_ampr **out){
    BW_AMPR_MODEL_CHECK(p&&p==ampr_model_owner&&out);*out=(const struct bw_preload_source_ampr *)&ampr_model_removal_token;return 0;
}
/* This retained orchestration fixture has no source-owned fakelib2 rows.
 * It may model non-applicability only; real positives use actual admission. */
int bw_native_launch_admission_legacy_companions_for_removal(struct bw_native_launch_admission *p,const struct bw_preload_legacy_companions **out){
    BW_AMPR_MODEL_CHECK(p&&p==ampr_model_owner&&out);*out=NULL;return 1;
}
int bw_preload_bundle_select_legacy_source_for_removal(const struct bw_preload_bundle_upgrade_request *q,
    const struct bw_preload_legacy_companions *w,struct bw_preload_reader_pin *pin,struct bw_preload_bundle_result *d){
    (void)q;(void)w;(void)pin;(void)d;abort();
}
int bw_preload_bundle_upgrade_source_controlled(const struct bw_preload_bundle_upgrade_request *q,
    const struct bw_preload_bundle_control *c,const struct bw_preload_source_ampr *w,struct bw_preload_bundle_result *d){
    BW_AMPR_MODEL_CHECK(w==(const struct bw_preload_source_ampr *)&ampr_model_execution_token&&ampr_model_source_checked);
    return bw_preload_bundle_upgrade_controlled(q,c,d);
}
int bw_preload_bundle_verify_source_checked(const struct bw_preload_bundle_request *q,const struct bw_preload_source_ampr *w,struct bw_preload_bundle_result *d){
    BW_AMPR_MODEL_CHECK(w==(const struct bw_preload_source_ampr *)&ampr_model_execution_token&&ampr_model_source_checked);
    return bw_preload_bundle_verify_checked(q,d);
}
int bw_preload_bundle_select_source_for_removal(const struct bw_preload_bundle_upgrade_request *q,
    const struct bw_preload_source_ampr *w,struct bw_preload_reader_pin *pin,struct bw_preload_bundle_result *d){
    BW_AMPR_MODEL_CHECK(w==(const struct bw_preload_source_ampr *)&ampr_model_removal_token);
    return bw_preload_bundle_select_for_removal(q,pin,d);
}
#endif
