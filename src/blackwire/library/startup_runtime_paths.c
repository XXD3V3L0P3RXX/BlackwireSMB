#define _POSIX_C_SOURCE 200809L
#include "startup_runtime_control_internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
/* Settings api1 exposes custom roots only. These are the inspected installed
 * repository's fallback and always-managed roots (sm_paths.h), not guesses. */
static const char *const defaults[]={
    "/data/homebrew","/data/etaHEN/games","/mnt/ext0/homebrew","/mnt/ext0/etaHEN/games","/mnt/ext1/homebrew","/mnt/ext1/etaHEN/games",
    "/mnt/usb0/homebrew","/mnt/usb1/homebrew","/mnt/usb2/homebrew","/mnt/usb3/homebrew","/mnt/usb4/homebrew","/mnt/usb5/homebrew","/mnt/usb6/homebrew","/mnt/usb7/homebrew",
    "/mnt/usb0/etaHEN/games","/mnt/usb1/etaHEN/games","/mnt/usb2/etaHEN/games","/mnt/usb3/etaHEN/games","/mnt/usb4/etaHEN/games","/mnt/usb5/etaHEN/games","/mnt/usb6/etaHEN/games","/mnt/usb7/etaHEN/games",
    "/mnt/usb0","/mnt/usb1","/mnt/usb2","/mnt/usb3","/mnt/usb4","/mnt/usb5","/mnt/usb6","/mnt/usb7","/mnt/ext0","/mnt/ext1"
};
static int syntax(const char *s){
    if(!s||s[0]!='/'||strnlen(s,BW_SRC_PATH_BYTES)>=BW_SRC_PATH_BYTES)return -1;
    for(size_t i=0;s[i];i++){unsigned char c=(unsigned char)s[i];if(c<32||c==127||c=='\\')return -1;}
    if(!s[1])return 0;
    for(const char *p=s+1;*p;){const char *end=strchr(p,'/');size_t n=end?(size_t)(end-p):strlen(p);if(!n||(n==1&&p[0]=='.')||(n==2&&!memcmp(p,"..",2)))return -1;if(!end)break;p=end+1;if(!*p)return -1;}return 0;
}
/* Resolve existing aliases. For unrelated absent paths, resolve the nearest
 * existing ancestor and append only already-validated literal components.
 * An existing dangling link or inaccessible ancestor is unknown, not absent. */
static int canonical(const char *path,char out[4096],int (*current)(void *),void *opaque){
    if(syntax(path)||!current||current(opaque))return -1;char work[BW_SRC_PATH_BYTES];strcpy(work,path);size_t cut=strlen(work);
    for(unsigned step=0;step<BW_SRC_PATH_BYTES;step++){
        if(current(opaque))return -1;char resolved[4096];
        if(realpath(work,resolved)){
            const char *suffix=path+cut;int n=snprintf(out,4096,"%s%s",resolved,!strcmp(resolved,"/")&&*suffix=='/'?suffix+1:suffix);
            return n<0||n>=4096||current(opaque)?-1:0;
        }
        if(errno!=ENOENT)return -1;struct stat st;if(!lstat(work,&st)||errno!=ENOENT)return -1;
        char *slash=strrchr(work,'/');if(!slash)return -1;if(slash==work){work[1]=0;cut=1;}else{cut=(size_t)(slash-work);*slash=0;}
    }return -1;
}
int bw_src_path_excluded(const char *scope,const char *path,int (*current)(void *),void *opaque){
    char base[4096],candidate[4096];if(canonical(scope,base,current,opaque)||strcmp(base,scope)||canonical(path,candidate,current,opaque))return -1;
    size_t n=strlen(base);if(!strncmp(base,candidate,n)&&(!candidate[n]||candidate[n]=='/'))return -1;return current(opaque)?-1:0;
}
int bw_src_paths_excluded(const struct bw_src_reply *reply,int settings,int (*current)(void *),void *opaque){
    static const char scope[]="/data/blackwire/.native-startup-rollback";
    if(!reply||reply->path_count>BW_SRC_PATH_LIMIT||(!reply->paths&&reply->path_count))return -1;
    for(size_t i=0;i<reply->path_count;i++)if(bw_src_path_excluded(scope,reply->paths[i],current,opaque))return -1;
    if(settings){
        if(!reply->path_count)for(size_t i=0;i<sizeof defaults/sizeof *defaults;i++)if(bw_src_path_excluded(scope,defaults[i],current,opaque))return -1;
        /* get_backport_scan_path() adds the fixed homebrew root even when
         * custom scan paths replace the ordinary fallback root list. */
        if(bw_src_path_excluded(scope,"/data/homebrew",current,opaque)||bw_src_path_excluded(scope,"/mnt/shadowmnt",current,opaque)||bw_src_path_excluded(scope,"/mnt/shadowmnt/pfsc",current,opaque))return -1;
    }return !current||current(opaque)?-1:0;
}
