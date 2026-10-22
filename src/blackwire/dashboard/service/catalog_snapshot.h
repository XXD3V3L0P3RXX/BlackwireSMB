#ifndef BLACKWIRE_DASHBOARD_CATALOG_SNAPSHOT_H
#define BLACKWIRE_DASHBOARD_CATALOG_SNAPSHOT_H
#include "catalog_adapter.h"
/* Existing native BW status is consumed in-process, never through a PC
 * server. These functions do not dispatch actions or accept client facts. */
int bd_catalog_append_blackwire(struct bd_catalog *,const void *,size_t,size_t *);
struct bd_catalog_operation {
    const char *id,*item_id,*phase,*message;
    unsigned active;
    unsigned exit_requested;
    int32_t frontend_pid;
    const char *action,*title;
    struct {
        const char *job_id,*job_state,*message;
        unsigned observed,phase,files,files_total,ready;
        uint64_t bytes,bytes_total;
    } preparation;
    struct {unsigned observed,phase,files,files_total;} removal;
};
int bd_catalog_json(const struct bd_catalog *,unsigned,unsigned,const struct bd_catalog_operation *,char **,size_t *);
#endif
