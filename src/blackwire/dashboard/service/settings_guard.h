/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BLACKWIRE_DASHBOARD_SETTINGS_GUARD_H
#define BLACKWIRE_DASHBOARD_SETTINGS_GUARD_H
#include "frontend_identity.h"
#include <stdio.h>
#include <string.h>
/* Lifecycle filtering only, never identity admission. The caller must bracket
 * this with the retained frontend's real current/package checks. No general
 * launch/preparation idle check calls this function. */
static inline int bd_scoped_log_idle(const char *path,const struct bd_frontend_identity *owner,unsigned awaiting_alive){
    if(!path||!owner||owner->pid<=1||owner->app_id<=1||!owner->birth_sec||
       strcmp(owner->title_id,BD_FRONTEND_TITLE))return -1;
    struct {char title[16];int alive,pid;} games[128];memset(games,0,sizeof games);
    size_t count=0;FILE *fp=fopen(path,"r");char line[1024];int bad=0;
    if(!fp)return -1;
    while(fgets(line,sizeof line,fp)){
        if(!strchr(line,'\n')){bad=1;break;}
        char *at=NULL;int alive=0,pid=0;
        if((at=strstr(line,"[GAME] started: "))){at+=strlen("[GAME] started: ");alive=1;}
        else if((at=strstr(line,"[GAME] process exited, awaiting same-app handoff: "))){at+=strlen("[GAME] process exited, awaiting same-app handoff: ");alive=awaiting_alive!=0;}
        else if((at=strstr(line,"[GAME] finalizing stopped application: ")))at+=strlen("[GAME] finalizing stopped application: ");
        else continue;
        char title[16]={0};
        if(sscanf(at,"%15s pid=%d",title,&pid)!=2||strlen(title)!=9||pid<=0){bad=1;break;}
        for(size_t j=0;j<9;j++)if(!((title[j]>='A'&&title[j]<='Z')||(title[j]>='0'&&title[j]<='9')))bad=1;
        size_t i=0;for(;i<count;i++)if(!strcmp(games[i].title,title))break;
        if(i==count){if(count==sizeof games/sizeof games[0]){bad=1;break;}memcpy(games[i].title,title,sizeof title);count++;}
        if(alive){games[i].pid=pid;games[i].alive=1;}
        else if(games[i].pid==pid)games[i].alive=0;
    }
    if(ferror(fp))bad=1;
    if(fclose(fp))bad=1;
    for(size_t i=0;i<count;i++)if(games[i].alive&&
        (strcmp(games[i].title,owner->title_id)||games[i].pid!=owner->pid))bad=1;
    return bad?-1:0;
}
static inline int bd_settings_log_idle(const char *path,const struct bd_frontend_identity *owner){return bd_scoped_log_idle(path,owner,0);}
/* Preparation retains the native sender's stricter handoff uncertainty:
 * another title stays busy until its exact final stopped event. */
static inline int bd_preparation_log_idle(const char *path,const struct bd_frontend_identity *owner){return bd_scoped_log_idle(path,owner,1);}
#endif
