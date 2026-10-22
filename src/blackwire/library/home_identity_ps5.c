/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "home_identity_ps5.h"
#include <stddef.h>
#include <string.h>
#include <ps5/kernel.h>
/* Source-established public declaration already used by game_identity.c and
 * Payload Manager. No unknown field is interpreted. */
struct home_app_info {uint32_t app_id;uint64_t unknown1;char title_id[14];char unknown2[0x3c];};
_Static_assert(offsetof(struct home_app_info,title_id)==16,"App-info title field");
_Static_assert(sizeof(struct home_app_info)==96,"App-info buffer layout");
int sceKernelGetAppInfo(pid_t,struct home_app_info *);
int sceLncUtilGetAppIdOfRunningBigApp(void);
int sceLncUtilGetAppTitleId(uint32_t,char *);
static int title(const char *s,size_t capacity){
    if(!memchr(s,0,capacity)||s[9]||memcmp(s,"PPSA",4))return 0;
    for(unsigned i=4;i<9;i++)if(s[i]<'0'||s[i]>'9')return 0;return 1;
}
int bw_home_identity_app(int32_t pid,uint32_t *app,char out[10]){
    if(!app||!out||pid<=1)return -1;*app=0;memset(out,0,10);
    struct home_app_info info;memset(&info,0xa5,sizeof info);
    if(sceKernelGetAppInfo(pid,&info)||!info.app_id||!title(info.title_id,sizeof info.title_id))return -1;
    int running=sceLncUtilGetAppIdOfRunningBigApp();char sony[128];memset(sony,0xa5,sizeof sony);
    if(running<=0||(uint32_t)running!=info.app_id||sceLncUtilGetAppTitleId(info.app_id,sony)||!title(sony,sizeof sony)||memcmp(sony,info.title_id,10))return -1;
    *app=info.app_id;memcpy(out,info.title_id,10);return 0;
}
int bw_home_identity_current(const struct bw_native_session *s){
    uint32_t app=0;char observed[10];return !s||!s->bound||bw_home_identity_app(s->process.pid,&app,observed)||app!=s->app_id||memcmp(observed,s->title,10)?-1:0;
}
int bw_home_identity_uid(pid_t pid,uid_t *out){
    if(!out||pid<=1)return -1;uid_t value=kernel_get_ucred_uid(pid);if(value==(uid_t)-1)return -1;*out=value;return 0;
}
