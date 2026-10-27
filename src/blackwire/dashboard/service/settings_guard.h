/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BLACKWIRE_DASHBOARD_SETTINGS_GUARD_H
#define BLACKWIRE_DASHBOARD_SETTINGS_GUARD_H
#include "frontend_identity.h"
#include "../../streamreader/lifecycle.h"
#include <stdio.h>
#include <string.h>
/* Lifecycle filtering only, never identity admission. The caller must bracket
 * this with the retained frontend's real current/package checks. No general
 * launch/preparation idle check calls this function. */
static inline int bd_scoped_log_idle(const char *path,const struct bd_frontend_identity *owner,unsigned awaiting_alive){
    if(!path||!owner||owner->pid<=1||owner->app_id<=1||!owner->birth_sec||
       strcmp(owner->title_id,BD_FRONTEND_TITLE))return -1;
    /* Both purposes retain pending application state. The qualified exact
     * frontend exemption never follows a replacement PID or pending row. */
    (void)awaiting_alive;
    struct bw_lifecycle_idle_fold fold;bw_lifecycle_idle_fold_init(&fold);
    FILE *fp=fopen(path,"r");char line[1024];int bad=0;if(!fp)return -1;
    while(fgets(line,sizeof line,fp)){
        if(!strchr(line,'\n')||bw_lifecycle_idle_fold_line(&fold,line,strlen(line))<0){bad=1;break;}
    }
    if(ferror(fp))bad=1;
    if(fclose(fp))bad=1;
    return bad||bw_lifecycle_idle_fold_finish(&fold,1,owner->title_id,owner->pid)!=0?-1:0;
}
static inline int bd_settings_log_idle(const char *path,const struct bd_frontend_identity *owner){return bd_scoped_log_idle(path,owner,0);}
/* Settings and preparation share the same handoff uncertainty:
 * pending is busy, including for an otherwise qualified exact frontend. */
static inline int bd_preparation_log_idle(const char *path,const struct bd_frontend_identity *owner){return bd_scoped_log_idle(path,owner,1);}
#endif
