#ifndef BLACKWIRE_DASHBOARD_CATALOG_ADAPTER_H
#define BLACKWIRE_DASHBOARD_CATALOG_ADAPTER_H
#include "../model/dashboard_model.h"

/* Parsed observations from the existing local ShadowMount service, not data
 * accepted from a browser action. Readiness is independently inspected below. */
struct bd_smp_record {
    char title[BD_TITLE_BYTES],title_id[10],content_id[128];
    char source[BD_SOURCE_BYTES],runtime_source[BD_SOURCE_BYTES],source_type[24];
    unsigned installed,installed_pkg,managed,source_available,image_backed;
};
int bd_smp_records_parse(const void *,size_t,struct bd_smp_record *,size_t,size_t *);

struct bd_local_environment {
    const char *apps_root,*database;
    /* Legacy field name: policy permits scoped source stability checks. The
     * fact is set only after the real metadata/registration rechecks below.
     * It is not global transfer-idle or whole-asset completeness knowledge.
     * A configured activity guard may withhold it on busy/unknown state. */
    unsigned copy_idle_observed;
    const char *active_transfer_source;
};
struct bd_local_observation {
    struct bd_local_input item;
    char registered_source[BD_SOURCE_BYTES];
    unsigned metadata_verified,all_assets_hashed;
};
/* Read-only canonical source, boot/metadata, mount-link and actual SQLite
 * title/content binding checks. No registration, copy, repair or launch. */
int bd_local_observe(const struct bd_smp_record *,const struct bd_local_environment *,struct bd_local_observation *);
#endif
