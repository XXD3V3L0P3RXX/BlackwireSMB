#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#ifdef BW_PRELOAD_BUNDLE_HOST_TEST
#define BW_COPY_HOST_TEST
#endif
#include "preload_bundle.h"
#include "send_diagnostic.h"
#include "../streamreader/copy_fs.h"
#include "../streamreader/copy_sha256.h"
#include <dirent.h>
#include <time.h>

/* Qualified optional game companions and legacy owned bundles. They are not
 * a prerequisite for an ordinary game, and no global library path is opened. */
struct pb_pin {const char *name;size_t size;const char *hex;};
static const struct pb_pin pb_globals[5]={
    {"libSceAmpr.sprx",423350,"69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a"},
    {"libSceAppContent.sprx",32982,"53e485e22970c693ab70c05bd78b8f627442ed41ca9a49692b38615046494a6b"},
    {"libSceGameUpdate.sprx",9126,"1e31ddbe4809b7c5ec117e9b7e1227befe757c31d1f5f73e6c7d5a3d2d238973"},
    {"libSceNpEntitlementAccess.sprx",43286,"3f1826f20e174ff0f7aaa8d7f3f7e386dfcca1136c9e4c0e72a49e041c0b1488"},
    {"libSceAmpr.sprx.orig",423350,"69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a"}
};
enum {PB_PLAYGO=5,PB_MEMBERS=6};
static const char *pb_name(unsigned i){return i==PB_PLAYGO?"libScePlayGo.sprx":pb_globals[i].name;}
struct pb_operation {
    const struct bw_preload_bundle_request *request;
    const struct bw_preload_bundle_control *control;
    int root,source,folder,changed,release_unknown;
    int source_state; /* 0=unobserved, 1=absent, 2=held game/fakelib */
    unsigned source_mask,members,scanned_mask,source_extras_reported,legacy_five;
    const struct bw_preload_playgo_pin *playgo;
    char source_path[1152];
    struct stat root_identity,source_identity,folder_identity;
    struct bw_preload_bundle_result *diagnostic;
    const char *stage,*library;
};
static int pb_fail(struct pb_operation *p,const char *reason,int error) {
    if(p->diagnostic&&!p->diagnostic->reason){
        bw_send_log("bundle-failure","stage=%s reason=%s library=%s root=%s errno=%d",p->stage?p->stage:"",reason,p->library?p->library:"",p->request&&p->request->root?p->request->root:"",error);
        p->diagnostic->stage=p->stage;p->diagnostic->reason=reason;
        p->diagnostic->library=p->library;p->diagnostic->system_error=error;
    }
    return -1;
}
static int pb_close(struct pb_operation *p,int fd,const char *reason){
    if(close(fd)){p->release_unknown=1;return pb_fail(p,reason,errno);}return 0;
}
static int pb_guard(struct pb_operation *p) {
    struct timespec t;const struct bw_preload_bundle_request *r=p->request;
    if(!r->current||!__builtin_isfinite(r->deadline)||r->deadline<=0)return pb_fail(p,"invalid_bundle_authority",0);
    if(clock_gettime(CLOCK_MONOTONIC,&t))return pb_fail(p,"bundle_clock_failed",errno);
    double now=(double)t.tv_sec+(double)t.tv_nsec/1e9;
    if(!__builtin_isfinite(now)||now>=r->deadline)return pb_fail(p,"bundle_deadline_expired",0);
    if(r->current(r->opaque))return pb_fail(p,"bundle_authority_changed",0);
    if(clock_gettime(CLOCK_MONOTONIC,&t))return pb_fail(p,"bundle_clock_failed",errno);
    double after=(double)t.tv_sec+(double)t.tv_nsec/1e9;
    if(!__builtin_isfinite(after)||after<now||after>=r->deadline)return pb_fail(p,"bundle_deadline_expired",0);
    return 0;
}
/* Expensive CONTROL evidence belongs at operation boundaries, not inside
 * every library hash/write chunk. Cheap lease/deadline and exact local path/
 * file identity checks still run at the original data-loop frequency. */
static int pb_checkpoint(struct pb_operation *p){
    int result=pb_guard(p);if(result||!p->control)return result;
    if(!p->control->checkpoint)return pb_fail(p,"invalid_bundle_checkpoint",0);
    if(p->control->checkpoint(p->control->opaque))return pb_fail(p,"bundle_authority_changed",0);
    return pb_guard(p);
}
static int pb_node(const struct stat *a,const struct stat *b) {
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid;
}
static int pb_same(const struct stat *a,const struct stat *b) {
    return pb_node(a,b)&&a->st_nlink==b->st_nlink&&a->st_size==b->st_size&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int pb_directory_fail(struct pb_operation *p,const char *path,const char *call,const char *reason,int error){
    bw_send_log("directory-failure","stage=%s call=%s path=%s reason=%s errno=%d",p->stage?p->stage:"",call,path,reason,error);
    return pb_fail(p,reason,error);
}
static int pb_dir(struct pb_operation *p,const char *path,int fd,const struct stat *expected) {
    char resolved[PATH_MAX];struct stat named,held;
    if(pb_guard(p))return -1;
    if(!realpath(path,resolved))return pb_directory_fail(p,path,"realpath","reader_directory_resolution_failed",errno);
    if(strcmp(path,resolved))return pb_directory_fail(p,path,"canonical-path","reader_directory_path_changed",0);
    if(lstat(path,&named))return pb_directory_fail(p,path,"lstat","reader_directory_named_stat_failed",errno);
    if(!S_ISDIR(named.st_mode))return pb_directory_fail(p,path,"directory-type","reader_directory_type_differs",0);
    if(fstat(fd,&held))return pb_directory_fail(p,path,"fstat","reader_directory_held_stat_failed",errno);
    if(!pb_node(&named,&held)||!pb_node(expected,&held))return pb_directory_fail(p,path,"identity","reader_directory_identity_changed",0);
    return pb_guard(p);
}
static int pb_source_guard(struct pb_operation *p){
    if(p->source_state==2)return pb_dir(p,p->source_path,p->source,&p->source_identity);
    if(p->source_state==1){struct stat named;
        if(cp_fstatat(p->root,"fakelib",&named,AT_SYMLINK_NOFOLLOW)>=0||errno!=ENOENT)
            return pb_fail(p,"game_companion_source_changed",0);
    }return 0;
}
static int pb_paths(struct pb_operation *p) {
    char path[1152];int n=snprintf(path,sizeof path,"%s/fakelib2",p->request->root);
    if(n<0||(size_t)n>=sizeof path)return pb_fail(p,"reader_directory_path_too_long",0);
    if(pb_dir(p,p->request->root,p->root,&p->root_identity)||pb_source_guard(p))return -1;
    return p->folder<0?0:pb_dir(p,path,p->folder,&p->folder_identity);
}
static int pb_open_directory(struct pb_operation *p,const char *path,struct stat *identity) {
    bw_send_log("directory-open","stage=%s path=%s",p->stage?p->stage:"",path);
    char resolved[PATH_MAX];struct stat named,held;
    if(!realpath(path,resolved))return pb_directory_fail(p,path,"realpath","reader_directory_resolution_failed",errno);
    if(strcmp(path,resolved))return pb_directory_fail(p,path,"canonical-path","reader_directory_path_changed",0);
    if(lstat(path,&named))return pb_directory_fail(p,path,"lstat","reader_directory_named_stat_failed",errno);
    if(!S_ISDIR(named.st_mode))return pb_directory_fail(p,path,"directory-type","reader_directory_type_differs",0);
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if(fd<0)return pb_directory_fail(p,path,"open","reader_directory_open_failed",errno);
    if(fstat(fd,&held)) {pb_directory_fail(p,path,"fstat","reader_directory_held_stat_failed",errno);pb_close(p,fd,"reader_directory_close_failed");return -1;}
    if(!pb_node(&named,&held)) {pb_directory_fail(p,path,"identity","reader_directory_identity_changed",0);pb_close(p,fd,"reader_directory_close_failed");return -1;}
    bw_send_log("directory-open-ok","stage=%s path=%s dev=%llu ino=%llu mode=%o uid=%lu gid=%lu",p->stage?p->stage:"",path,(unsigned long long)held.st_dev,(unsigned long long)held.st_ino,(unsigned)held.st_mode,(unsigned long)held.st_uid,(unsigned long)held.st_gid);
    *identity=held;return fd;
}
/* Never retain a dirent pointer or interpolate unescaped entry bytes into a
 * log. 256 output bytes bound even malformed/control/non-ASCII names. */
static void pb_entry_name(const char *name,char out[257],unsigned *truncated){
    static const char hex[]="0123456789abcdef";const unsigned char *s=(const unsigned char *)name;size_t n=0;
    while(*s){unsigned char c=*s;unsigned take=(c>=33&&c<=126&&c!='\\'&&c!='"')?1u:4u;
        if(n+take>=257)break;
        if(take==1)out[n++]=(char)c;else{out[n++]='\\';out[n++]='x';out[n++]=hex[c>>4];out[n++]=hex[c&15];}s++;
    }out[n]=0;*truncated=*s!=0;
}
static int pb_entry_fail(struct pb_operation *p,const char *name,const char *kind,const char *reason){
    char escaped[257];unsigned truncated;pb_entry_name(name,escaped,&truncated);
    if(p->diagnostic&&!p->diagnostic->reason){
        p->diagnostic->entry_class=kind;memcpy(p->diagnostic->entry_name,escaped,strlen(escaped)+1);
        p->diagnostic->entry_name_truncated=truncated;
    }
    bw_send_log("bundle-entry-refused","stage=%s class=%s name=\"%s\" truncated=%u",p->stage?p->stage:"",kind,escaped,truncated);
    return pb_fail(p,reason,0);
}
/* Module-like original entries cannot be silently discarded by our exclusive
 * fakelib2. All names not on the bounded pin list with .sprx/.prx/.so (any
 * ASCII case) refuse; other entries are opaque metadata, never followed. */
static int pb_module_name(const char *name){
    const char *dot=strrchr(name,'.');if(!dot)return 0;char suffix[6];size_t n=strlen(dot);
    if(n>=sizeof suffix)return 0;for(size_t i=0;i<=n;i++){unsigned char c=(unsigned char)dot[i];suffix[i]=(char)(c>='A'&&c<='Z'?c+('a'-'A'):c);}
    return !strcmp(suffix,".sprx")||!strcmp(suffix,".prx")||!strcmp(suffix,".so");
}
/* A source fakelib is not our owned private bundle. Stock ShadowMount chooses
 * fakelib2 exclusively before fakelib (sm_fakelib.c, 1.7beta7). Only the fixed,
 * hash-qualified companion names below can enter that private directory.
 * PlayGo has a separate bounded exact-artifact pin list. Unqualified source
 * modules refuse; opaque metadata/links are not opened, followed or copied.
 * No ignored source entry grants execution or deletion authority.
 * Private enumeration remains exact-set and rejects every unselected name. */
static int pb_entries(struct pb_operation *p,int fd,int source) {
    int duplicate=dup(fd);if(duplicate<0)return pb_fail(p,"reader_directory_dup_failed",errno);
    DIR *d=fdopendir(duplicate);if(!d){pb_fail(p,"reader_directory_enumeration_open_failed",errno);pb_close(p,duplicate,"reader_directory_close_failed");return -1;}
    rewinddir(d);unsigned seen=0,ignored=0;int bad=0;
    for(;;) {
        if(pb_paths(p)){bad=1;break;}
        errno=0;struct dirent *e=readdir(d);
        if(!e){if(errno){bad=1;pb_fail(p,"reader_directory_enumeration_failed",errno);}break;}
        if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        unsigned i=0;while(i<PB_MEMBERS&&strcmp(e->d_name,pb_name(i)))i++;
        if(i==PB_MEMBERS&&source){
            if(pb_module_name(e->d_name)){bad=1;pb_entry_fail(p,e->d_name,"unqualified-source-module","unqualified_game_companion_module");break;}
            if(ignored!=~0u)ignored++;
            if(!p->source_extras_reported){char escaped[257];unsigned truncated;pb_entry_name(e->d_name,escaped,&truncated);
                bw_send_log("bundle-source-extra-retained","stage=%s class=unselected-source-entry name=\"%s\" truncated=%u selected=0",p->stage?p->stage:"",escaped,truncated);
                p->source_extras_reported=1;}
            continue;
        }
        if(i==PB_MEMBERS||(seen&(1u<<i))){bad=1;pb_entry_fail(p,e->d_name,i==PB_MEMBERS?"unselected-private-entry":"duplicate-known-entry","reader_directory_names_differ");break;}
        seen|=1u<<i;
    }
    if(closedir(d)){p->release_unknown=1;bad=1;pb_fail(p,"reader_directory_close_failed",errno);}
    if(!bad){p->scanned_mask=seen;if(source&&p->diagnostic)p->diagnostic->source_entries_ignored=ignored;}
    return bad?-1:0;
}
static int pb_source_open(struct pb_operation *p);
static int pb_verify_set(struct pb_operation *p,int parent,int destination);
static int pb_profile(struct pb_operation *p){
    const char *stage=p->stage,*library=p->library;
    if(pb_entries(p,p->folder,0))return -1;unsigned seen=p->scanned_mask;
    if(!(seen&1u))return pb_fail(p,"reader_directory_is_incomplete",0);
    /* Preserve complete legacy fixed five-file bundles, including those made
     * before optional per-game companions. A present source is nevertheless
     * enumerated and fully qualified: unknown modules or changed known bytes
     * cannot bypass validation merely because the private set looks legacy. */
    if(!p->source_state){if(pb_source_open(p))return -1;}
    else if(p->source_state==2&&pb_verify_set(p,p->source,0))return -1;
    if(seen==31u&&!(p->source_mask&(1u<<PB_PLAYGO)))p->legacy_five=1;
    p->stage=stage;p->library=library;
    if(seen!=(p->legacy_five?31u:(p->source_mask|1u))||(p->members&&seen!=p->members))
        return pb_fail(p,"reader_directory_is_incomplete",0);
    p->members=seen;return 0;
}
static int pb_hex(const char *hex,uint8_t out[32]) {
    const char *digits="0123456789abcdef";if(!hex||strnlen(hex,65)!=64)return -1;
    for(unsigned i=0;i<32;i++) {
        const char *a=strchr(digits,hex[2*i]),*b=strchr(digits,hex[2*i+1]);if(!a||!b)return -1;
        out[i]=(uint8_t)(((a-digits)<<4)|(b-digits));
    }
    return 0;
}
static int pb_material(struct pb_operation *p,unsigned i,int fd,size_t *size,uint8_t hash[32]){
    if(i!=PB_PLAYGO){*size=pb_globals[i].size;return pb_hex(pb_globals[i].hex,hash)?pb_fail(p,"invalid_fixed_reader_library_hash",0):0;}
    struct stat held;if(fstat(fd,&held))return pb_fail(p,"reader_library_held_stat_failed",errno);
    if(!S_ISREG(held.st_mode)||held.st_size<0)return pb_fail(p,"reader_library_type_differs",0);
    const struct bw_preload_playgo_pin *pin=bw_preload_playgo_pin((uint64_t)held.st_size);
    if(!pin)return pb_entry_fail(p,pb_name(i),"unqualified-playgo-variant","unqualified_game_playgo_variant");
    if(p->playgo&&p->playgo!=pin)return pb_fail(p,"game_playgo_variant_changed",0);
    p->playgo=pin;*size=pin->size;return pb_hex(pin->hex,hash)?pb_fail(p,"invalid_fixed_reader_library_hash",0):0;
}
static int pb_file(struct pb_operation *p,int parent,const char *name,int fd,size_t size,
                   const uint8_t expected[32],int destination) {
    struct stat before,named,after;struct cp_sha sha;uint8_t bytes[32768],hash[32];
    p->stage=destination?"private-library-metadata":"game-library-metadata";p->library=name;
    if(pb_paths(p))return -1;
    if(fstat(fd,&before))return pb_fail(p,"reader_library_held_stat_failed",errno);
    if(!S_ISREG(before.st_mode))return pb_fail(p,"reader_library_type_differs",0);
    if(before.st_nlink!=1)return pb_fail(p,"reader_library_link_count_differs",0);
    if(before.st_size<0||(uint64_t)before.st_size!=size)return pb_fail(p,"reader_library_size_differs",0);
    if(destination&&(before.st_mode&07777)!=0755)return pb_fail(p,"private_reader_library_mode_differs",0);
    if(destination&&before.st_uid!=geteuid())return pb_fail(p,"private_reader_library_owner_differs",0);
    if(cp_fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW))return pb_fail(p,"reader_library_named_stat_failed",errno);
    if(!pb_same(&before,&named))return pb_fail(p,"reader_library_named_identity_differs",0);
    p->stage=destination?"private-library-hash":"game-library-hash";
    cp_sha_init(&sha);
    for(size_t at=0;at<size;) {
        if(pb_paths(p))return -1;
        size_t take=size-at;if(take>sizeof bytes)take=sizeof bytes;
        ssize_t got=pread(fd,bytes,take,(off_t)at);
        if(got<0&&errno==EINTR)continue;
        if(got<0)return pb_fail(p,"reader_library_read_failed",errno);
        if(!got||(size_t)got>take)return pb_fail(p,"reader_library_read_truncated",0);
        cp_sha_update(&sha,bytes,(size_t)got);at+=(size_t)got;
    }
    cp_sha_final(&sha,hash);
    if(pb_paths(p))return -1;
    ssize_t extra=pread(fd,bytes,1,(off_t)size);
    if(extra<0)return pb_fail(p,"reader_library_tail_read_failed",errno);
    if(extra)return pb_fail(p,"reader_library_size_changed",0);
    if(fstat(fd,&after))return pb_fail(p,"reader_library_final_stat_failed",errno);
    if(!pb_same(&before,&after))return pb_fail(p,"reader_library_held_identity_changed",0);
    if(cp_fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW))return pb_fail(p,"reader_library_final_named_stat_failed",errno);
    if(!pb_same(&before,&named))return pb_fail(p,"reader_library_named_identity_changed",0);
    if(memcmp(hash,expected,32))return pb_fail(p,"reader_library_hash_differs",0);
    return pb_paths(p);
}
static int pb_verify_set(struct pb_operation *p,int parent,int destination) {
    p->stage=destination?"private-library-names":"game-library-names";p->library=NULL;
    if(!destination&&p->source_state!=2)return pb_paths(p);
    if(destination){if(pb_profile(p))return -1;}
    else if(pb_entries(p,parent,1)||p->scanned_mask!=p->source_mask)return pb_fail(p,"game_companion_names_changed",0);
    unsigned mask=destination?p->members:p->source_mask;
    for(unsigned i=0;i<PB_MEMBERS;i++) {
        if(!(mask&(1u<<i)))continue;
        uint8_t hash[32];size_t size=0;
        p->stage=destination?"private-library-open":"game-library-open";p->library=pb_name(i);
        int fd=v2_openat(parent,pb_name(i),O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
        if(fd<0)return pb_fail(p,"reader_library_open_failed",errno);
        int result=0;
        if(destination&&i==0){size=bw_preload_embedded_image.size;memcpy(hash,bw_preload_embedded_image.sha256,32);}
        else result=pb_material(p,i,fd,&size,hash);
        if(!result)result=pb_file(p,parent,pb_name(i),fd,size,hash,destination);
        if(pb_close(p,fd,"reader_library_close_failed"))result=-1;if(result)return -1;
    }
    p->stage=destination?"private-library-names":"game-library-names";p->library=NULL;
    if(pb_entries(p,parent,!destination))return -1;
    if(p->scanned_mask!=mask)return pb_fail(p,destination?"reader_directory_is_incomplete":"game_companion_names_changed",0);
    return pb_paths(p);
}
static int pb_source_open(struct pb_operation *p){
    if(p->source_state)return pb_source_guard(p);
    p->stage="game-library-directory";p->library=NULL;struct stat named;
    if(pb_paths(p))return -1;
    int n=snprintf(p->source_path,sizeof p->source_path,"%s/fakelib",p->request->root);
    if(n<0||(size_t)n>=sizeof p->source_path)return pb_fail(p,"game_companion_path_too_long",0);
    if(cp_fstatat(p->root,"fakelib",&named,AT_SYMLINK_NOFOLLOW)){
        if(errno!=ENOENT)return pb_fail(p,"game_companion_directory_stat_failed",errno);
        p->source_state=1;p->source_mask=0;return pb_paths(p);
    }
    p->source=pb_open_directory(p,p->source_path,&p->source_identity);if(p->source<0)return -1;p->source_state=2;
    if(pb_entries(p,p->source,1))return -1;
    if(p->scanned_mask&1u)return pb_entry_fail(p,pb_globals[0].name,"unexpected-source-reader","unexpected_game_reader_file");
    p->source_mask=p->scanned_mask;return pb_verify_set(p,p->source,0);
}
static int pb_copy(struct pb_operation *p,unsigned i) {
    const char *name=pb_name(i);int source=-1,fd=-1,result=-1;size_t size=0;uint8_t hash[32],bytes[32768];
    struct stat source_identity={0},source_after={0};
    p->stage="game-library-open";p->library=name;
    if(i==0){size=bw_preload_embedded_image.size;memcpy(hash,bw_preload_embedded_image.sha256,32);}
    else {
        source=v2_openat(p->source,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
        if(source<0)return pb_fail(p,"reader_library_open_failed",errno);
        if(pb_material(p,i,source,&size,hash)||pb_file(p,p->source,name,source,size,hash,0))goto done;
        if(fstat(source,&source_identity)){pb_fail(p,"reader_library_held_stat_failed",errno);goto done;}
    }
    if(pb_paths(p)||pb_checkpoint(p))goto done;
    p->stage="private-library-create";
    fd=v2_openat(p->folder,name,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0755);
    if(fd<0){pb_fail(p,"private_reader_library_create_failed",errno);goto done;}p->changed=1;
    if(fchmod(fd,0755)){pb_fail(p,"private_reader_library_chmod_failed",errno);goto done;}
    p->stage="private-library-write";
    for(size_t at=0;at<size;) {
        if(pb_paths(p))goto done;
        size_t take=size-at;if(take>sizeof bytes)take=sizeof bytes;
        const uint8_t *data=i?bytes:bw_preload_embedded_image.bytes+at;
        if(i) {
            ssize_t got=pread(source,bytes,take,(off_t)at);
            if(got<0&&errno==EINTR)continue;
            if(got<0){pb_fail(p,"reader_library_read_failed",errno);goto done;}
            if(!got||(size_t)got>take){pb_fail(p,"reader_library_read_truncated",0);goto done;}
            take=(size_t)got;data=bytes;
        }
        size_t written=0;
        while(written<take) {
            if(pb_paths(p))goto done;
            ssize_t put=write(fd,data+written,take-written);
            if(put<0&&errno==EINTR)continue;
            if(put<0){pb_fail(p,"private_reader_library_write_failed",errno);goto done;}
            if(!put||(size_t)put>take-written){pb_fail(p,"private_reader_library_write_truncated",0);goto done;}
            written+=(size_t)put;
        }
        at+=take;
    }
    if(pb_paths(p)||pb_checkpoint(p))goto done;
    p->stage="private-library-sync";
    if(fsync(fd)){pb_fail(p,"private_reader_library_sync_failed",errno);goto done;}
    if(pb_file(p,p->folder,name,fd,size,hash,1))goto done;
    p->stage="private-directory-sync";
    if(fsync(p->folder)){pb_fail(p,"private_reader_directory_sync_failed",errno);goto done;}
    if(pb_paths(p))goto done;
    if(i){
        if(fstat(source,&source_after)){pb_fail(p,"reader_library_final_stat_failed",errno);goto done;}
        if(!pb_same(&source_identity,&source_after)){pb_fail(p,"reader_library_held_identity_changed",0);goto done;}
        if(pb_file(p,p->source,name,source,size,hash,0))goto done;
    }
    result=0;
done:
    if(fd>=0&&pb_close(p,fd,"private_reader_library_close_failed"))result=-1;
    if(source>=0&&pb_close(p,source,"reader_library_close_failed"))result=-1;
    return result;
}
static int pb_install_checked(const struct bw_preload_bundle_request *r,struct bw_preload_bundle_result *diagnostic,int create,const struct bw_preload_bundle_control *control) {
    if(diagnostic)memset(diagnostic,0,sizeof *diagnostic);
    struct pb_operation p={.request=r,.control=control,.root=-1,.source=-1,.folder=-1,.diagnostic=diagnostic,.stage="request"};
    int result=-1;uint8_t hash[32];
    if(!r||!r->root||strnlen(r->root,1024)>=1024||!r->current||
       !bw_preload_embedded_image.bytes||bw_preload_embedded_image.size<1024||bw_preload_embedded_image.size>4u*1024u*1024u||
       bw_preload_embedded_image.private_wait_milliseconds!=10000||
       pb_hex(bw_preload_embedded_image.scope_sha256,hash))return pb_fail(&p,"invalid_reader_bundle_request",0);
    if(pb_checkpoint(&p))return -1;
    p.stage="embedded-image";
    cp_sha_bytes(bw_preload_embedded_image.bytes,bw_preload_embedded_image.size,hash);
    if(memcmp(hash,bw_preload_embedded_image.sha256,32))return pb_fail(&p,"embedded_reader_image_hash_differs",0);
    p.stage="prepared-directory";
    p.root=pb_open_directory(&p,r->root,&p.root_identity);if(p.root<0)goto done;
    p.stage="private-directory";p.library=NULL;
    struct stat named;
    if(!cp_fstatat(p.root,"fakelib2",&named,AT_SYMLINK_NOFOLLOW)) {
        p.folder=v2_openat(p.root,"fakelib2",O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
        if(p.folder<0){pb_fail(&p,"private_reader_directory_open_failed",errno);goto done;}
        if(fstat(p.folder,&p.folder_identity)){pb_fail(&p,"reader_directory_held_stat_failed",errno);goto done;}
        if(!pb_node(&named,&p.folder_identity)){pb_fail(&p,"reader_directory_identity_changed",0);goto done;}
        if(!S_ISDIR(named.st_mode)){pb_fail(&p,"reader_directory_type_differs",0);goto done;}
        if((named.st_mode&07777)!=0755){pb_fail(&p,"private_reader_directory_mode_differs",0);goto done;}
        if(named.st_uid!=geteuid()){pb_fail(&p,"private_reader_directory_owner_differs",0);goto done;}
        if(pb_verify_set(&p,p.folder,1))goto done;
    }else {
        if(errno!=ENOENT){pb_fail(&p,"private_reader_directory_stat_failed",errno);goto done;}
        if(!create){pb_fail(&p,"private_reader_directory_absent",0);goto done;}
        if(pb_source_open(&p))goto done;p.members=p.source_mask|1u;p.stage="private-directory";p.library=NULL;
        if(pb_paths(&p)||pb_checkpoint(&p))goto done;
        if(v2_mkdirat(p.root,"fakelib2",0755)){pb_fail(&p,"private_reader_directory_create_failed",errno);goto done;}p.changed=1;
        p.folder=v2_openat(p.root,"fakelib2",O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
        if(p.folder<0){pb_fail(&p,"private_reader_directory_open_failed",errno);goto done;}
        if(fchmod(p.folder,0755)){pb_fail(&p,"private_reader_directory_chmod_failed",errno);goto done;}
        if(fstat(p.folder,&p.folder_identity)){pb_fail(&p,"reader_directory_held_stat_failed",errno);goto done;}
        if(!S_ISDIR(p.folder_identity.st_mode)){pb_fail(&p,"reader_directory_type_differs",0);goto done;}
        if(p.folder_identity.st_uid!=geteuid()){pb_fail(&p,"private_reader_directory_owner_differs",0);goto done;}
        if(fsync(p.root)){pb_fail(&p,"prepared_reader_directory_sync_failed",errno);goto done;}
        if(pb_paths(&p))goto done;
        for(unsigned i=0;i<PB_MEMBERS;i++)if((p.members&(1u<<i))&&pb_copy(&p,i))goto done;
        if(pb_verify_set(&p,p.folder,1))goto done;
    }
    if(pb_verify_set(&p,p.source,0)||pb_paths(&p)||pb_checkpoint(&p))goto done;
    result=0;
done:
    if(p.folder>=0&&pb_close(&p,p.folder,"private_reader_directory_close_failed"))result=-1;
    if(p.source>=0&&pb_close(&p,p.source,"game_companion_directory_close_failed"))result=-1;
    if(p.root>=0&&pb_close(&p,p.root,"prepared_reader_directory_close_failed"))result=-1;
    if(diagnostic){diagnostic->mutation_started=(unsigned)(p.changed!=0);diagnostic->release_unknown=(unsigned)(p.release_unknown!=0);if(!result)diagnostic->stage="complete";}
    return result&&(p.changed||p.release_unknown)?-2:result;
}
int bw_preload_bundle_install_checked(const struct bw_preload_bundle_request *r,struct bw_preload_bundle_result *d) {
    return pb_install_checked(r,d,1,NULL);
}
int bw_preload_bundle_verify_checked(const struct bw_preload_bundle_request *r,struct bw_preload_bundle_result *d) {
    return pb_install_checked(r,d,0,NULL);
}
int bw_preload_bundle_install(const struct bw_preload_bundle_request *r) {
    return bw_preload_bundle_install_checked(r,NULL);
}


/* One joined CONTROL worker only; no DATA, registration or protocol callback. */
#include <pthread.h>
static pthread_mutex_t pu_mutex=PTHREAD_MUTEX_INITIALIZER;
static const struct pb_pin pu_previous={"libSceAmpr.sprx",759872,
    "4c638196ec2becd205ca9b096fc49603c6c8b3accc254136d77ac01b1d9978f9"};
/* Exact public07 image independently observed after its owned migration.
 * Other source-built images are not admitted by similarity or filename. */
static const struct pb_pin pu_public07={"libSceAmpr.sprx",759872,
    "2d19ac454b9b0bb1690c12dfbad8ac105c185815e5be499b63a2d713021d438b"};
/* Exact reader shared by public08/public09, verified before this update. */
static const struct pb_pin pu_public09={"libSceAmpr.sprx",759872,
    "a6c069485a05a8c70d2fcc5709c7bc18149b517092637cf8b42a880c9ba4c91c"};
/* Exact public10a reader retained for the normal owned upgrade. */
static const struct pb_pin pu_public10a={"libSceAmpr.sprx",765328,
    "4642f74934b54b1e93d480584b7485d34f82ac19ce95d62e44f9ab9ee1ebccf0"};
/* Exact deployed live diagnostic reader retained for owned upgrade. */
static const struct pb_pin pu_live10a={"libSceAmpr.sprx",770304,
    "e3a01a21f833af2d25417489afbb6885f59131c195cdecc0ac50c5ebcbce08d4"};
/* Exact deployed reference348 reader; ordinary guarded upgrade only. */
static const struct pb_pin pu_reference348={"libSceAmpr.sprx",769168,
    "348fe7e8a0f74b505e4148e96f2bfc27a57c035dd9c6b681b3d1cc774b1c77cb"};
/* Exact currently deployed HOME reader; normal owned upgrade only. */
static const struct pb_pin pu_home7944={"libSceAmpr.sprx",796032,
    "7944f590fe317a028acf52228f4fb1d474e1e819ed24abe9c76f814cd57b1616"};
/* Exact installed startup-diagnostic reader; normal owned upgrade only. */
static const struct pb_pin pu_home_diag7c={"libSceAmpr.sprx",798432,
    "7c2b0df0b067b9f8599ce7cf7fcb6ed55fee6a55a8f17e66cfcc9eb0c3425009"};
/* Reader independently verified inside the published v0.1.0-preview.1 ELF.
 * Retain the exact size/hash check when upgrading existing prepared games. */
static const struct pb_pin pu_standalone_preview1={"libSceAmpr.sprx",798192,
    "d02f20f4ee8b1db1617e7b2c7ad4f76821e8b9da2523ac185a2e881ca33462be"};
/* Exact independently read-back compressed dev.1/dev.2 DATA reader. */
static const struct pb_pin pu_compressed_dev2={"libSceAmpr.sprx",828416,
    "6e088f332d114f5f60c3baead9d549685d5e8e94d07c5e4970bdbaeca4fb66df"};
/* Exact development 3 reader read back from both prepared test copies. */
static const struct pb_pin pu_compressed_dev3={"libSceAmpr.sprx",828736,
    "3b91a0b41203ce15afe40937dfe4e2b13c289f6fdfeedf11076e4c86eb7b6033"};
/* Exact development 5 reader read back from the prepared compressed games. */
static const struct pb_pin pu_compressed_dev5={"libSceAmpr.sprx",829328,
    "135148e2de73b5b54d6f18dbfb95c1f2cd2d7d7e3e6dac60b756e844e6cbc684"};
/* Exact SDK-qualified two-source reader, for copies prepared by dev.6. */
static const struct pb_pin pu_two_sources_dev6={"libSceAmpr.sprx",831168,
    "c9d069b520c367c971e52432a053d7105a7019cb19ee97185120bffb6190e025"};
/* Exact embedded reader shared by published v0.3.0-beta.1/beta.2.
 * Beta.2 package ad3505cca3c6b409152e8c12024d81582a351aee473d7fecee4d82bc4e1e252c
 * independently contains these complete reader bytes. Its verified source
 * manifest scope is 8b945ecde010223cf7e462cc399ad480d237f1cef67a6bcb925ec0b903ee3911
 * (metadata/build-manifest.json, frozen commit 974ac247b1461e7a116e13c556192dec055f3e7a).
 * Only this exact size/hash is recognized; no caller-provided predecessor or
 * source scope can widen migration or destructive removal authority. */
static const struct pb_pin pu_beta03_2={"libSceAmpr.sprx",843696,
    "39cc8ff48a8b53886be0f7c5f1d43bc504fb989c00a9adc050dcade72865c441"};
static int pu_previous_match(uint64_t size,const uint8_t actual[32]){
    const struct pb_pin *known[]={&pu_previous,&pu_public07,&pu_public09,&pu_public10a,&pu_live10a,&pu_reference348,&pu_home7944,&pu_home_diag7c,&pu_standalone_preview1,&pu_compressed_dev2,&pu_compressed_dev3,&pu_compressed_dev5,&pu_two_sources_dev6,&pu_beta03_2};uint8_t expected[32];
    for(unsigned i=0;i<sizeof known/sizeof known[0];i++)
        if(size==known[i]->size&&!pb_hex(known[i]->hex,expected)&&!memcmp(actual,expected,32))return 1;
    return 0;
}
#define PU_WORKSPACE ".blackwire-reader-upgrade"
enum {PU_INTENT,PU_BACKUP,PU_NEW,PU_FILES};
static const char *pu_names[PU_FILES]={"intent","old-reader","new-reader"};
struct pu_operation {
    struct pb_operation p;
    struct bw_preload_bundle_request request,original;
    char root_path[1152],state_path[1152];
    int state,receipt,work,reader,files[PU_FILES];
    struct stat state_id,receipt_id,work_id,reader_id,file_ids[PU_FILES];
    unsigned created,published,removed,close_unknown;
};
static uint64_t pu_u64(const uint8_t *b){uint64_t x=0;for(unsigned i=0;i<8;i++)x|=(uint64_t)b[i]<<(8*i);return x;}
static uint32_t pu_u32(const uint8_t *b){return (uint32_t)pu_u64((const uint8_t[8]){b[0],b[1],b[2],b[3],0,0,0,0});}
static void pu_put(uint8_t *b,uint64_t x){for(unsigned i=0;i<8;i++)b[i]=(uint8_t)(x>>(8*i));}
static int pu_nonce(const char *s){if(!s||strnlen(s,33)!=32)return 0;unsigned nonzero=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",s[i]))return 0;nonzero|=(unsigned)(s[i]!='0');}return nonzero!=0;}
static int pu_component(const char *s){
    if(!s||strnlen(s,75)!=74||s[9]!='_')return 0;
    for(unsigned i=0;i<9;i++)if(!((s[i]>='A'&&s[i]<='Z')||(s[i]>='0'&&s[i]<='9')))return 0;
    for(unsigned i=10;i<74;i++)if(!strchr("0123456789abcdef",s[i]))return 0;return 1;
}
int bw_preload_bundle_upgrade_workspace(const char *data_root,const char *game){
    if(!data_root||!pu_component(game))return -1;
    char state[1152],path[1200],resolved[PATH_MAX];struct stat before,after,workspace;
    int n=snprintf(state,sizeof state,"%s/native-prepared/%s",data_root,game);
    if(n<0||(size_t)n>=sizeof state||!realpath(state,resolved)||strcmp(state,resolved)||
       lstat(state,&before)||!S_ISDIR(before.st_mode))return -1;
    n=snprintf(path,sizeof path,"%s/%s",state,PU_WORKSPACE);
    if(n<0||(size_t)n>=sizeof path)return -1;
    int result=lstat(path,&workspace);if(result&&errno!=ENOENT)return -1;
    if(lstat(state,&after)||!pb_node(&before,&after))return -1;
    return result?0:1;
}
static int pu_anchor(int fd,const char *path,const struct stat *id){
    struct stat held,named;char resolved[PATH_MAX];
    return fd<0||!realpath(path,resolved)||strcmp(path,resolved)||lstat(path,&named)||!S_ISDIR(named.st_mode)||
        fstat(fd,&held)||!pb_node(id,&held)||!pb_node(&held,&named)?-1:0;
}
static int pu_current(void *opaque){
    struct pu_operation *u=opaque;struct stat held,named;
    if(u->original.current(u->original.opaque))return -1;
    if(u->state>=0){
        if(pu_anchor(u->state,u->state_path,&u->state_id))return -1;
        if(!u->created){
            if(!cp_fstatat(u->state,PU_WORKSPACE,&named,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)return -1;
        }else if(!u->removed){
            if(u->work<0||fstat(u->work,&held)||cp_fstatat(u->state,PU_WORKSPACE,&named,AT_SYMLINK_NOFOLLOW)||
               !S_ISDIR(named.st_mode)||!pb_node(&u->work_id,&held)||!pb_node(&held,&named))return -1;
        }else if(u->work<0||fstat(u->work,&held)||!pb_node(&u->work_id,&held)||held.st_nlink!=0||
                 !cp_fstatat(u->state,PU_WORKSPACE,&named,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)return -1;
    }
    if(u->receipt>=0&&(fstat(u->receipt,&held)||cp_fstatat(u->state,"prepared.bin",&named,AT_SYMLINK_NOFOLLOW)||
       !pb_same(&u->receipt_id,&held)||!pb_same(&held,&named)))return -1;
    return 0;
}
static int pu_guard(struct pu_operation *u){
    if(pb_guard(&u->p))return -1;
    if(u->p.root>=0&&pb_dir(&u->p,u->root_path,u->p.root,&u->p.root_identity))return -1;
    if(pb_source_guard(&u->p))return -1;
    if(u->p.folder>=0){char path[1152];int n=snprintf(path,sizeof path,"%s/fakelib2",u->root_path);
        if(n<0||(size_t)n>=sizeof path||pb_dir(&u->p,path,u->p.folder,&u->p.folder_identity))return -1;}
    return pb_guard(&u->p);
}
static int pu_close(struct pu_operation *u,int *fd){
    if(*fd<0)return 0;int owned=*fd;*fd=-1;
    if(close(owned)){u->close_unknown=1;return pb_fail(&u->p,"reader_upgrade_close_unconfirmed",errno);}return 0;
}
/* Capture a complete stable file on the held parent. No acceptance of a
 * changing body or a filename's previous identity. Optional bytes are small. */
static int pu_digest(struct pu_operation *u,int parent,const char *leaf,int fd,mode_t mode,
    size_t exact,uint8_t out[32],struct stat *identity,uint8_t *body){
    struct stat before,after,named;uint8_t bytes[32768];struct cp_sha sha;
    u->p.stage="reader-upgrade-material";u->p.library=leaf;
    if(pu_guard(u)||fstat(fd,&before)||!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_uid!=geteuid()||
       (before.st_mode&07777)!=mode||before.st_size<0||(uint64_t)before.st_size>4u*1024u*1024u||
       (exact!=(size_t)-1&&(uint64_t)before.st_size!=exact)||cp_fstatat(parent,leaf,&named,AT_SYMLINK_NOFOLLOW)||
       !pb_same(&before,&named))return pb_fail(&u->p,"reader_upgrade_material_metadata_differs",0);
    cp_sha_init(&sha);size_t size=(size_t)before.st_size;
    for(size_t at=0;at<size;){
        if(pu_guard(u))return -1;size_t take=size-at<sizeof bytes?size-at:sizeof bytes;
        ssize_t got=pread(fd,bytes,take,(off_t)at);if(got<0&&errno==EINTR)continue;
        if(got<=0||(size_t)got>take)return pb_fail(&u->p,"reader_upgrade_material_read_failed",got<0?errno:0);
        cp_sha_update(&sha,bytes,(size_t)got);if(body)memcpy(body+at,bytes,(size_t)got);at+=(size_t)got;
    }
    if(pu_guard(u)||pread(fd,bytes,1,(off_t)size)!=0||fstat(fd,&after)||cp_fstatat(parent,leaf,&named,AT_SYMLINK_NOFOLLOW)||
       !pb_same(&before,&after)||!pb_same(&after,&named))return pb_fail(&u->p,"reader_upgrade_material_changed",errno);
    cp_sha_final(&sha,out);if(identity)*identity=after;return 0;
}
static int pu_write(struct pu_operation *u,unsigned which,const uint8_t *bytes,size_t size,int source,mode_t mode,const uint8_t expected[32]){
    u->p.stage="reader-upgrade-create";u->p.library=pu_names[which];
    if(pu_guard(u)||pb_checkpoint(&u->p))return -1;
    int fd=v2_openat(u->work,pu_names[which],O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,mode);
    if(fd<0)return pb_fail(&u->p,"reader_upgrade_create_failed",errno);u->files[which]=fd;
    if(fchmod(fd,mode))return pb_fail(&u->p,"reader_upgrade_mode_failed",errno);
    uint8_t buffer[32768],actual[32];
    for(size_t at=0;at<size;){
        if(pu_guard(u))return -1;size_t take=size-at<sizeof buffer?size-at:sizeof buffer;
        if(source>=0){ssize_t got=pread(source,buffer,take,(off_t)at);if(got<0&&errno==EINTR)continue;
            if(got<=0||(size_t)got>take)return pb_fail(&u->p,"reader_upgrade_backup_read_failed",got<0?errno:0);take=(size_t)got;}
        const uint8_t *data=source>=0?buffer:bytes+at;size_t done=0;
        while(done<take){if(pu_guard(u))return -1;ssize_t n=write(fd,data+done,take-done);if(n<0&&errno==EINTR)continue;
            if(n<=0||(size_t)n>take-done)return pb_fail(&u->p,"reader_upgrade_write_failed",n<0?errno:0);done+=(size_t)n;}
        at+=take;
    }
    u->p.stage="reader-upgrade-sync";if(pu_guard(u)||pb_checkpoint(&u->p)||fsync(fd))return pb_fail(&u->p,"reader_upgrade_sync_failed",errno);
    if(pu_digest(u,u->work,pu_names[which],fd,mode,size,actual,&u->file_ids[which],NULL)||memcmp(actual,expected,32)){
        return pb_fail(&u->p,"reader_upgrade_written_bytes_differ",0);
    }
    return 0;
}
static int pu_other_members(struct pu_operation *u){
    if(pb_profile(&u->p))return -1;
    for(unsigned i=1;i<PB_MEMBERS;i++){
        if(!(u->p.members&(1u<<i)))continue;
        uint8_t hash[32];size_t size=0;
        int fd=v2_openat(u->p.folder,pb_name(i),O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
        if(fd<0)return pb_fail(&u->p,"reader_upgrade_companion_open_failed",errno);
        int bad=pb_material(&u->p,i,fd,&size,hash)||pb_file(&u->p,u->p.folder,pb_name(i),fd,size,hash,1);
        if(pu_close(u,&fd))bad=-1;if(bad)return -1;
    }return pu_guard(u);
}
static int pu_owned_leaf(struct pu_operation *u,unsigned i){
    struct stat named;return pu_guard(u)||cp_fstatat(u->work,pu_names[i],&named,AT_SYMLINK_NOFOLLOW)||
        !pb_same(&named,&u->file_ids[i])?-1:0;
}
static int pb_upgrade_checked(const struct bw_preload_bundle_upgrade_request *q,const struct bw_preload_bundle_control *control,struct bw_preload_bundle_result *d){
    if(d)memset(d,0,sizeof *d);
    struct pu_operation u={.state=-1,.receipt=-1,.work=-1,.reader=-1};
    u.p=(struct pb_operation){.root=-1,.source=-1,.folder=-1,.diagnostic=d,.stage="reader-upgrade-request"};
    for(unsigned i=0;i<PU_FILES;i++)u.files[i]=-1;
    uint8_t record[384],actual[32],previous[32],intent[1024];int result=-1,locked=0;
    if(!q||!q->bundle.root||!q->bundle.current||!q->data_root||!pu_component(q->game)||
       !pu_nonce(q->prepared_run)||!pu_nonce(q->launch_run)||!q->prepared_record_sha256)return pb_fail(&u.p,"invalid_reader_upgrade_request",0);
    int n=snprintf(u.root_path,sizeof u.root_path,"%s/games/%s",q->data_root,q->game);
    int z=snprintf(u.state_path,sizeof u.state_path,"%s/native-prepared/%s",q->data_root,q->game);
    if(n<0||(size_t)n>=sizeof u.root_path||z<0||(size_t)z>=sizeof u.state_path||strcmp(u.root_path,q->bundle.root))
        return pb_fail(&u.p,"reader_upgrade_root_differs",0);
    if(pthread_mutex_trylock(&pu_mutex))return pb_fail(&u.p,"reader_upgrade_worker_busy",0);locked=1;
    u.original=q->bundle;u.request=q->bundle;u.request.root=u.root_path;u.request.current=pu_current;u.request.opaque=&u;u.p.request=&u.request;
    u.p.control=control;
    if(pb_checkpoint(&u.p)||!bw_preload_embedded_image.bytes||bw_preload_embedded_image.size<1024||bw_preload_embedded_image.size>4u*1024u*1024u||
       bw_preload_embedded_image.private_wait_milliseconds!=10000||pb_hex(bw_preload_embedded_image.scope_sha256,actual))goto done;
    cp_sha_bytes(bw_preload_embedded_image.bytes,bw_preload_embedded_image.size,actual);
    if(memcmp(actual,bw_preload_embedded_image.sha256,32)){pb_fail(&u.p,"embedded_reader_image_hash_differs",0);goto done;}
    u.p.root=pb_open_directory(&u.p,u.root_path,&u.p.root_identity);if(u.p.root<0)goto done;
    u.state=pb_open_directory(&u.p,u.state_path,&u.state_id);if(u.state<0)goto done;
    if(pu_current(&u)){pb_fail(&u.p,"reader_upgrade_workspace_present_or_authority_changed",0);goto done;}
    u.receipt=v2_openat(u.state,"prepared.bin",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(u.receipt<0||fstat(u.receipt,&u.receipt_id)){pb_fail(&u.p,"reader_upgrade_receipt_open_failed",errno);goto done;}
    if(pu_digest(&u,u.state,"prepared.bin",u.receipt,0600,sizeof record,actual,NULL,record)||memcmp(actual,q->prepared_record_sha256,32)||
       memcmp(record,"BWNAT001",8)||pu_u32(record+8)!=1||pu_u32(record+12)!=sizeof record||memcmp(record+16,q->prepared_run,32)||
       (uint64_t)u.p.root_identity.st_dev!=pu_u64(record+304)||(uint64_t)u.p.root_identity.st_ino!=pu_u64(record+312)||
       (uint64_t)u.state_id.st_dev!=pu_u64(record+320)||(uint64_t)u.state_id.st_ino!=pu_u64(record+328)){
        pb_fail(&u.p,"reader_upgrade_prepared_tuple_differs",0);goto done;}
    cp_sha_bytes(record,352,actual);if(memcmp(actual,record+352,32)){pb_fail(&u.p,"reader_upgrade_receipt_checksum_differs",0);goto done;}
    for(unsigned i=0;i<32;i++){static const char digits[]="0123456789abcdef";if(q->game[10+2*i]!=digits[record[48+i]>>4]||q->game[11+2*i]!=digits[record[48+i]&15]){
        pb_fail(&u.p,"reader_upgrade_binding_differs",0);goto done;}}
    struct stat named;if(cp_fstatat(u.p.root,"fakelib2",&named,AT_SYMLINK_NOFOLLOW)){
        if(errno!=ENOENT){pb_fail(&u.p,"private_reader_directory_stat_failed",errno);goto done;}
        struct bw_preload_bundle_result local={0};struct bw_preload_bundle_result *reported=d?d:&local;
        result=pb_install_checked(&u.request,reported,1,control);
        if(reported->mutation_started)u.p.changed=1;
        if(reported->release_unknown)u.p.release_unknown=1;
        goto done;
    }
    char folder[1152];n=snprintf(folder,sizeof folder,"%s/fakelib2",u.root_path);if(n<0||(size_t)n>=sizeof folder)goto done;
    u.p.folder=pb_open_directory(&u.p,folder,&u.p.folder_identity);
    if(u.p.folder<0||(u.p.folder_identity.st_mode&07777)!=0755||u.p.folder_identity.st_uid!=geteuid())goto done;
    if(pb_profile(&u.p))goto done;
    u.reader=v2_openat(u.p.folder,"libSceAmpr.sprx",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(u.reader<0){pb_fail(&u.p,"reader_upgrade_reader_open_failed",errno);goto done;}
    if(pu_digest(&u,u.p.folder,"libSceAmpr.sprx",u.reader,0755,(size_t)-1,actual,&u.reader_id,NULL))goto done;
    if((uint64_t)u.reader_id.st_size==bw_preload_embedded_image.size&&!memcmp(actual,bw_preload_embedded_image.sha256,32)){
        result=pb_verify_set(&u.p,u.p.folder,1)||pb_verify_set(&u.p,u.p.source,0)||pu_guard(&u)?-1:0;goto done;
    }
    if(!pu_previous_match((uint64_t)u.reader_id.st_size,actual)){
        pb_fail(&u.p,"reader_upgrade_predecessor_unknown",0);goto done;}
    memcpy(previous,actual,32);
    if(pu_other_members(&u)||pu_guard(&u))goto done;
    if(pb_checkpoint(&u.p))goto done;
    u.p.stage="reader-upgrade-reserve";u.p.library=NULL;u.p.changed=1;
    if(v2_mkdirat(u.state,PU_WORKSPACE,0700)){pb_fail(&u.p,"reader_upgrade_workspace_create_unconfirmed",errno);goto done;}
    u.work=v2_openat(u.state,PU_WORKSPACE,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
    if(u.work<0||fchmod(u.work,0700)||fstat(u.work,&u.work_id)||!S_ISDIR(u.work_id.st_mode)||u.work_id.st_uid!=geteuid()||u.work_id.st_dev!=u.p.folder_identity.st_dev){
        pb_fail(&u.p,"reader_upgrade_workspace_unconfirmed",errno);goto done;}u.created=1;
    memset(intent,0,sizeof intent);memcpy(intent,"BWREUP01",8);pu_put(intent+8,1);memcpy(intent+16,q->game,74);
    memcpy(intent+96,q->prepared_run,32);memcpy(intent+128,q->launch_run,32);memcpy(intent+160,q->prepared_record_sha256,32);
    memcpy(intent+192,bw_preload_embedded_image.sha256,32);memcpy(intent+224,previous,32);
    uint64_t facts[]={u.reader_id.st_dev,u.reader_id.st_ino,(uint64_t)u.reader_id.st_size,u.reader_id.st_mode,u.reader_id.st_uid,u.reader_id.st_gid,u.reader_id.st_nlink,
        (uint64_t)u.reader_id.st_mtim.tv_sec,(uint64_t)u.reader_id.st_mtim.tv_nsec,(uint64_t)u.reader_id.st_ctim.tv_sec,(uint64_t)u.reader_id.st_ctim.tv_nsec,
        u.p.root_identity.st_dev,u.p.root_identity.st_ino,u.state_id.st_dev,u.state_id.st_ino,u.p.folder_identity.st_dev,u.p.folder_identity.st_ino};
    for(unsigned i=0;i<sizeof facts/sizeof facts[0];i++)pu_put(intent+256+8*i,facts[i]);cp_sha_bytes(intent,992,intent+992);cp_sha_bytes(intent,sizeof intent,actual);
    if(pu_write(&u,PU_INTENT,intent,sizeof intent,-1,0600,actual)||pu_write(&u,PU_BACKUP,NULL,(size_t)u.reader_id.st_size,u.reader,0755,previous)||
       pu_write(&u,PU_NEW,bw_preload_embedded_image.bytes,bw_preload_embedded_image.size,-1,0755,bw_preload_embedded_image.sha256)||
       fsync(u.work)||fsync(u.state)||pu_guard(&u))goto done;
    struct stat held;if(fstat(u.reader,&held)||cp_fstatat(u.p.folder,"libSceAmpr.sprx",&named,AT_SYMLINK_NOFOLLOW)||
       !pb_same(&u.reader_id,&held)||!pb_same(&held,&named)||pu_other_members(&u)||pu_owned_leaf(&u,PU_NEW)){
        pb_fail(&u.p,"reader_upgrade_publication_identity_changed",0);goto done;}
    if(pb_checkpoint(&u.p))goto done;
    u.p.stage="reader-upgrade-publish";
    if(cp_renameat(u.work,pu_names[PU_NEW],u.p.folder,"libSceAmpr.sprx")){pb_fail(&u.p,"reader_upgrade_publication_unconfirmed",errno);goto done;}u.published=1;
    if(fsync(u.p.folder)||fsync(u.work)||pb_verify_set(&u.p,u.p.folder,1)||pb_verify_set(&u.p,u.p.source,0)||pu_guard(&u)||pb_checkpoint(&u.p))goto done;
    if(fstat(u.reader,&held)||!pb_node(&u.reader_id,&held)||held.st_nlink!=0){pb_fail(&u.p,"reader_upgrade_old_reader_release_unconfirmed",0);goto done;}
    /* Close material first: every fault leaves intent/backup and the fixed
     * reservation. Directory anchors stay held throughout named cleanup. */
    if(pu_close(&u,&u.reader))goto done;
    for(unsigned i=0;i<PU_FILES;i++)if(pu_close(&u,&u.files[i]))goto done;
    if(pu_close(&u,&u.receipt))goto done;
    if(pb_checkpoint(&u.p))goto done;
    u.p.stage="reader-upgrade-cleanup";u.p.library=NULL;
    if(pu_owned_leaf(&u,PU_BACKUP)||cp_unlinkat(u.work,pu_names[PU_BACKUP])||pu_owned_leaf(&u,PU_INTENT)||cp_unlinkat(u.work,pu_names[PU_INTENT])||fsync(u.work)||pu_guard(&u)){
        pb_fail(&u.p,"reader_upgrade_cleanup_unconfirmed",errno);goto done;}
    if(pb_checkpoint(&u.p))goto done;
    /* Only this held, positively empty directory is removed. No traversal. */
#ifdef BW_COPY_HOST_TEST
    if(unlinkat(u.state,PU_WORKSPACE,AT_REMOVEDIR)){
#else
    if(cp_at(SYS_unlinkat,u.state,(long)PU_WORKSPACE,AT_REMOVEDIR,0,0)){
#endif
        pb_fail(&u.p,"reader_upgrade_workspace_remove_unconfirmed",errno);goto done;}
    u.removed=1;if(fsync(u.state)||pu_guard(&u)){pb_fail(&u.p,"reader_upgrade_final_state_unconfirmed",errno);goto done;}
    result=0;
done:
    for(unsigned i=0;i<PU_FILES;i++)if(pu_close(&u,&u.files[i]))result=-1;
    int *owned[]={&u.reader,&u.receipt,&u.work,&u.state,&u.p.folder,&u.p.source,&u.p.root};
    for(unsigned i=0;i<sizeof owned/sizeof owned[0];i++)if(pu_close(&u,owned[i]))result=-1;
    if(!result&&pb_checkpoint(&u.p))result=-1;
    if(locked&&pthread_mutex_unlock(&pu_mutex)){u.close_unknown=1;result=-1;pb_fail(&u.p,"reader_upgrade_worker_release_unconfirmed",0);}
    if(result)pb_fail(&u.p,"reader_upgrade_validation_or_durability_unconfirmed",0);
    if(d){d->mutation_started=(unsigned)(u.p.changed!=0);d->release_unknown=(unsigned)(u.close_unknown||u.p.release_unknown);if(!result&&!u.close_unknown)d->stage="complete";}
    return result||u.close_unknown||u.p.release_unknown?(u.p.changed||u.close_unknown||u.p.release_unknown?-2:-1):0;
}

int bw_preload_bundle_upgrade_checked(const struct bw_preload_bundle_upgrade_request *q,struct bw_preload_bundle_result *d){
    return pb_upgrade_checked(q,NULL,d);
}
int bw_preload_bundle_upgrade_controlled(const struct bw_preload_bundle_upgrade_request *q,
    const struct bw_preload_bundle_control *control,struct bw_preload_bundle_result *d){
    if(!control||!control->checkpoint){if(d){memset(d,0,sizeof *d);d->stage="request";d->reason="invalid_bundle_checkpoint";}return -1;}
    return pb_upgrade_checked(q,control,d);
}

/* Same held private directory, with no dependency on global library state:
 * deleting an owned copy does not repair or adopt the global source set. */
static int pr_entries(struct pu_operation *u){
    if(pu_guard(u))return -1;
    int fd=dup(u->p.folder);if(fd<0)return pb_fail(&u->p,"reader_removal_directory_dup_failed",errno);
    DIR *dir=fdopendir(fd);if(!dir){pb_fail(&u->p,"reader_removal_directory_open_failed",errno);pb_close(&u->p,fd,"reader_removal_directory_close_failed");return -1;}
    rewinddir(dir);unsigned seen=0;int bad=0;
    for(;;){
        if(pu_guard(u)){bad=1;break;}errno=0;struct dirent *entry=readdir(dir);
        if(!entry){if(errno){pb_fail(&u->p,"reader_removal_directory_read_failed",errno);bad=1;}break;}
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
        unsigned i=0;while(i<PB_MEMBERS&&strcmp(entry->d_name,pb_name(i)))i++;
        if(i==PB_MEMBERS||(seen&(1u<<i))){pb_entry_fail(&u->p,entry->d_name,i==PB_MEMBERS?"unselected-private-entry":"duplicate-known-entry","reader_removal_library_names_differ");bad=1;break;}seen|=1u<<i;
    }
    if(closedir(dir)){u->p.release_unknown=1;pb_fail(&u->p,"reader_removal_directory_close_failed",errno);bad=1;}
    if(!(seen&1u)){pb_fail(&u->p,"reader_removal_library_set_incomplete",0);bad=1;}
    if(!bad&&pb_profile(&u->p))bad=1;
    return bad?-1:pu_guard(u);
}
int bw_preload_bundle_select_for_removal(const struct bw_preload_bundle_upgrade_request *q,
    struct bw_preload_reader_pin *out,struct bw_preload_bundle_result *d){
    if(d)memset(d,0,sizeof *d);if(out)memset(out,0,sizeof *out);
    struct pu_operation u={.state=-1,.receipt=-1,.work=-1,.reader=-1};
    u.p=(struct pb_operation){.root=-1,.source=-1,.folder=-1,.diagnostic=d,.stage="reader-removal-selection"};
    for(unsigned i=0;i<PU_FILES;i++)u.files[i]=-1;
    struct bw_preload_reader_pin selected={0};uint8_t record[384],hash[32],expected[32];int locked=0,result=-1;
    if(!q||!out||!q->bundle.root||!q->bundle.current||!q->data_root||!pu_component(q->game)||
       !pu_nonce(q->prepared_run)||!pu_nonce(q->launch_run)||!q->prepared_record_sha256||
       bw_preload_embedded_image.size<1024||bw_preload_embedded_image.size>4u*1024u*1024u)
        return pb_fail(&u.p,"invalid_reader_removal_selection",0);
    int n=snprintf(u.root_path,sizeof u.root_path,"%s/games/%s",q->data_root,q->game);
    int z=snprintf(u.state_path,sizeof u.state_path,"%s/native-prepared/%s",q->data_root,q->game);
    if(n<0||(size_t)n>=sizeof u.root_path||z<0||(size_t)z>=sizeof u.state_path||strcmp(u.root_path,q->bundle.root))
        return pb_fail(&u.p,"reader_removal_root_differs",0);
    if(pthread_mutex_trylock(&pu_mutex))return pb_fail(&u.p,"reader_removal_worker_busy",0);locked=1;
    u.original=q->bundle;u.request=q->bundle;u.request.root=u.root_path;u.request.current=pu_current;u.request.opaque=&u;u.p.request=&u.request;
    if(pb_guard(&u.p))goto done;
    u.p.root=pb_open_directory(&u.p,u.root_path,&u.p.root_identity);if(u.p.root<0)goto done;
    u.state=pb_open_directory(&u.p,u.state_path,&u.state_id);if(u.state<0)goto done;
    if(pu_current(&u)){pb_fail(&u.p,"reader_update_workspace_present_or_authority_changed",0);goto done;}
    u.receipt=v2_openat(u.state,"prepared.bin",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(u.receipt<0||fstat(u.receipt,&u.receipt_id)){pb_fail(&u.p,"reader_removal_receipt_open_failed",errno);goto done;}
    if(pu_digest(&u,u.state,"prepared.bin",u.receipt,0600,sizeof record,hash,NULL,record)||memcmp(hash,q->prepared_record_sha256,32)||
       memcmp(record,"BWNAT001",8)||pu_u32(record+8)!=1||pu_u32(record+12)!=sizeof record||memcmp(record+16,q->prepared_run,32)||
       (uint64_t)u.p.root_identity.st_dev!=pu_u64(record+304)||(uint64_t)u.p.root_identity.st_ino!=pu_u64(record+312)||
       (uint64_t)u.state_id.st_dev!=pu_u64(record+320)||(uint64_t)u.state_id.st_ino!=pu_u64(record+328)){
        pb_fail(&u.p,"reader_removal_prepared_tuple_differs",0);goto done;}
    cp_sha_bytes(record,352,hash);if(memcmp(hash,record+352,32)){pb_fail(&u.p,"reader_removal_receipt_checksum_differs",0);goto done;}
    for(unsigned i=0;i<32;i++){static const char hex[]="0123456789abcdef";
        if(q->game[10+2*i]!=hex[record[48+i]>>4]||q->game[11+2*i]!=hex[record[48+i]&15]){pb_fail(&u.p,"reader_removal_binding_differs",0);goto done;}}
    selected.size=bw_preload_embedded_image.size;memcpy(selected.sha256,bw_preload_embedded_image.sha256,32);
    struct stat named;if(cp_fstatat(u.p.root,"fakelib2",&named,AT_SYMLINK_NOFOLLOW)){
        if(errno!=ENOENT){pb_fail(&u.p,"reader_removal_directory_stat_failed",errno);goto done;}
        if(pu_guard(&u))goto done;result=0;goto done;
    }
    char path[1152];n=snprintf(path,sizeof path,"%s/fakelib2",u.root_path);
    if(n<0||(size_t)n>=sizeof path){pb_fail(&u.p,"reader_removal_directory_path_too_long",0);goto done;}
    u.p.folder=pb_open_directory(&u.p,path,&u.p.folder_identity);
    if(u.p.folder<0||(u.p.folder_identity.st_mode&07777)!=0755||u.p.folder_identity.st_uid!=geteuid()){
        pb_fail(&u.p,"reader_removal_directory_metadata_differs",0);goto done;}
    if(pr_entries(&u))goto done;
    u.reader=v2_openat(u.p.folder,"libSceAmpr.sprx",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(u.reader<0){pb_fail(&u.p,"reader_removal_library_open_failed",errno);goto done;}
    if(pu_digest(&u,u.p.folder,"libSceAmpr.sprx",u.reader,0755,(size_t)-1,hash,&u.reader_id,NULL))goto done;
    if((uint64_t)u.reader_id.st_size!=selected.size||memcmp(hash,selected.sha256,32)){
        if(!pu_previous_match((uint64_t)u.reader_id.st_size,hash)){
            pb_fail(&u.p,"reader_removal_reader_unknown",0);goto done;}
        selected.size=(size_t)u.reader_id.st_size;memcpy(selected.sha256,hash,32);
    }
    for(unsigned i=1;i<PB_MEMBERS;i++){
        if(!(u.p.members&(1u<<i)))continue;
        int fd=v2_openat(u.p.folder,pb_name(i),O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);size_t size=0;
        if(fd<0){pb_fail(&u.p,"reader_removal_companion_open_failed",errno);goto done;}
        int bad=pb_material(&u.p,i,fd,&size,expected)||pu_digest(&u,u.p.folder,pb_name(i),fd,0755,size,hash,NULL,NULL)||memcmp(hash,expected,32);
        if(pu_close(&u,&fd))bad=-1;
        if(bad){pb_fail(&u.p,"reader_removal_companion_differs",0);goto done;}
    }
    if(pr_entries(&u)||pu_guard(&u))goto done;result=0;
done:;
    int *owned[]={&u.reader,&u.receipt,&u.state,&u.p.folder,&u.p.source,&u.p.root};
    for(unsigned i=0;i<sizeof owned/sizeof owned[0];i++)if(pu_close(&u,owned[i]))result=-1;
    if(!result&&pb_guard(&u.p))result=-1;
    if(locked&&pthread_mutex_unlock(&pu_mutex)){u.close_unknown=1;result=-1;pb_fail(&u.p,"reader_removal_worker_release_unconfirmed",0);}
    if(result)pb_fail(&u.p,"reader_removal_selection_unconfirmed",0);
    if(d){d->mutation_started=0;d->release_unknown=(unsigned)(u.close_unknown||u.p.release_unknown);if(!result&&!u.close_unknown&&!u.p.release_unknown)d->stage="complete";}
    if(result||u.close_unknown||u.p.release_unknown)return u.close_unknown||u.p.release_unknown?-2:-1;
    *out=selected;return 0;
}
