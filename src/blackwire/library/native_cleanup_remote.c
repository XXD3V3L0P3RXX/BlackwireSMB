/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "native_cleanup_remote.h"
#include "native_document.h"
#include "source_param.h"
#include "send_diagnostic.h"
#include "../streamreader/copy_fs.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define NRC_REMOTE_ITEMS 4096u
enum observation { NRC_STRICT,NRC_REGISTRATION,NRC_ABSENT,NRC_PREPARE_ABSENT,NRC_PREPARE_REGISTERED };
static int preparing(unsigned policy){return policy==NRC_PREPARE_ABSENT||policy==NRC_PREPARE_REGISTERED;}
struct collection {
    struct bw_nrc_remote *remote;
    const struct bw_nrc_subject *subject;
    unsigned seen,count,declared,owned_rows,policy;
};
static int current(struct bw_nrc_remote *r){return !r||!r->connection.current||r->connection.current(r->connection.opaque)?-1:0;}
static int fail(struct bw_nrc_remote *r,const char *reason){if(r)r->reason=reason;return -1;}
static int decimal(const void *raw,size_t size,unsigned maximum,unsigned *out){
    const unsigned char *p=raw;unsigned n=0;if(!size||(size>1&&p[0]=='0'))return -1;
    for(size_t i=0;i<size;i++){unsigned d=p[i]-'0';if(d>9||d>maximum||n>(maximum-d)/10)return -1;n=n*10+d;}*out=n;return 0;
}
static int boolean(const void *raw,size_t size,unsigned *out){
    if(size==4&&!memcmp(raw,"true",4)){*out=1;return 0;}if(size==5&&!memcmp(raw,"false",5)){*out=0;return 0;}return -1;
}
static int same(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_nlink==b->st_nlink&&
        a->st_uid==b->st_uid&&a->st_gid==b->st_gid&&a->st_size==b->st_size&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int literal_path(const char *path){
    if(!path||path[0]!='/'||strnlen(path,BW_NRC_PATH_BYTES)>=BW_NRC_PATH_BYTES||!path[1])return -1;
    for(const char *p=path+1;*p;){const char *end=strchr(p,'/');size_t n=end?(size_t)(end-p):strlen(p);
        if(!n||(n==1&&*p=='.')||(n==2&&!memcmp(p,"..",2)))return -1;
        for(size_t i=0;i<n;i++)if((unsigned char)p[i]<32||(unsigned char)p[i]==127||p[i]=='\\')return -1;
        if(!end)break;p=end+1;if(!*p)return -1;
    }return 0;
}
static int subject_shape(const struct bw_nrc_subject *s){
    if(!s||strnlen(s->title,10)!=9||memcmp(s->title,"PPSA",4)||!s->content[0]||
       !memchr(s->content,0,sizeof s->content)||literal_path(s->source))return -1;
    for(unsigned i=4;i<9;i++)if(s->title[i]<'0'||s->title[i]>'9')return -1;return 0;
}
/* A different manual path is excluded only by its real, stable metadata. A
 * missing or unreadable path could be another copy of this title, so it is
 * unknown rather than inferred from its directory name. */
static int manual_title(struct bw_nrc_remote *r,const char *path,char title[10]){
    int root=-1,sys=-1,fd=-1,result=-1,error=0;struct stat roots[2],syss[2],file[3];char canonical[PATH_MAX];
    unsigned char *bytes=NULL;char content[128];const char *stage="manual_path_or_authority";
#define M_CALL(name,call) do{stage=name;if(call){error=errno;goto done;}}while(0)
#define M_TEST(name,predicate) do{stage=name;if(predicate){error=0;goto done;}}while(0)
    M_TEST("manual_path_or_authority",literal_path(path)||current(r));
    M_CALL("manual_realpath",!realpath(path,canonical));M_TEST("manual_noncanonical",strcmp(canonical,path));
    stage="manual_open_root";root=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);if(root<0){error=errno;goto done;}
    M_CALL("manual_root_stat",fstat(root,roots));M_TEST("manual_root_type",!S_ISDIR(roots[0].st_mode));
    stage="manual_open_sce_sys";sys=v2_openat(root,"sce_sys",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK,0);if(sys<0){error=errno;goto done;}
    M_CALL("manual_sce_sys_stat",fstat(sys,syss));M_TEST("manual_sce_sys_type",!S_ISDIR(syss[0].st_mode));
    stage="manual_open_param";fd=v2_openat(sys,"param.json",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0){error=errno;goto done;}
    M_CALL("manual_param_stat",fstat(fd,file));M_TEST("manual_param_type",!S_ISREG(file[0].st_mode));
    M_TEST("manual_param_links",file[0].st_nlink!=1);M_TEST("manual_param_size",file[0].st_size<=0||file[0].st_size>BW_SOURCE_PARAM_LIMIT);
    size_t size=(size_t)file[0].st_size,at=0;bytes=malloc(size);M_TEST("manual_param_memory",!bytes);
    while(at<size){
        M_TEST("manual_read_authority",current(r));stage="manual_param_read";
        ssize_t got=pread(fd,bytes+at,size-at,(off_t)at);if(got<0&&errno==EINTR)continue;
        if(got<=0){error=got<0?errno:0;goto done;}at+=(size_t)got;
    }
    M_TEST("manual_param_parse",bw_source_param_identity(bytes,size,title,content));
    M_CALL("manual_param_restat",fstat(fd,file+1)||cp_fstatat(sys,"param.json",file+2,AT_SYMLINK_NOFOLLOW));
    M_TEST("manual_param_changed",!same(file,file+1)||!same(file+1,file+2));
    M_CALL("manual_sce_sys_restat",fstat(sys,syss+1));M_TEST("manual_sce_sys_changed",!same(syss,syss+1));
    M_CALL("manual_sce_sys_named_stat",cp_fstatat(root,"sce_sys",syss+1,AT_SYMLINK_NOFOLLOW));M_TEST("manual_sce_sys_name_changed",!same(syss,syss+1));
    M_CALL("manual_root_restat",fstat(root,roots+1));M_TEST("manual_root_changed",!same(roots,roots+1));
    M_CALL("manual_root_named_stat",lstat(path,roots+1));M_TEST("manual_root_name_changed",!same(roots,roots+1));
    M_CALL("manual_final_realpath",!realpath(path,canonical));M_TEST("manual_final_path_or_authority",strcmp(path,canonical)||current(r));
    result=0;
done:
    free(bytes);
    if(fd>=0&&close(fd)){if(!result){stage="manual_param_close";error=errno;}r->observation_release_unknown=1;result=-1;}
    if(sys>=0&&close(sys)){if(!result){stage="manual_sce_sys_close";error=errno;}r->observation_release_unknown=1;result=-1;}
    if(root>=0&&close(root)){if(!result){stage="manual_root_close";error=errno;}r->observation_release_unknown=1;result=-1;}
    if(result){r->substage=stage;r->observation_error=error;}
#undef M_CALL
#undef M_TEST
    return result;
}

enum {G_PATH=1u,G_RUNTIME=2u,G_TYPE=4u,G_TITLE=8u,G_CONTENT=16u,G_INSTALLED=32u,
    G_PKG=64u,G_MANAGED=128u,G_AVAILABLE=256u,G_IMAGE=512u,G_MOUNTED=1024u};
struct game {
    char path[BW_NRC_PATH_BYTES],runtime[BW_NRC_PATH_BYTES],type[24],title[10],content[128];
    unsigned installed,pkg,managed,available,image,mounted,seen;
};
static int game_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct game *p=opaque;unsigned bit=0,*flag=NULL;char *text=NULL;size_t cap=0;
#define TEXT(k,b,m) if(kn==sizeof(k)-1&&!memcmp(key,k,kn)){bit=b;text=p->m;cap=sizeof p->m;}
    TEXT("path",G_PATH,path) else TEXT("runtime_path",G_RUNTIME,runtime) else TEXT("source_type",G_TYPE,type)
    else TEXT("title_id",G_TITLE,title) else TEXT("content_id",G_CONTENT,content)
#undef TEXT
#define FLAG(k,b,m) if(kn==sizeof(k)-1&&!memcmp(key,k,kn)){bit=b;flag=&p->m;}
    else FLAG("installed",G_INSTALLED,installed) else FLAG("installed_pkg",G_PKG,pkg) else FLAG("managed",G_MANAGED,managed)
    else FLAG("source_available",G_AVAILABLE,available) else FLAG("image_backed",G_IMAGE,image) else FLAG("mounted",G_MOUNTED,mounted) else return 0;
#undef FLAG
    if(p->seen&bit)return -1;p->seen|=bit;return text?bw_document_string(raw,size,text,cap):boolean(raw,size,flag);
}
static int game_item(void *opaque,const void *raw,size_t size){
    struct collection *c=opaque;struct game one={0};if(++c->count>NRC_REMOTE_ITEMS||current(c->remote)||
       bw_document_fields(raw,size,game_field,&one)||one.seen!=2047u||!one.title[0]||literal_path(one.path)||
       (one.runtime[0]&&literal_path(one.runtime)))return -1;
    const struct bw_nrc_subject *s=c->subject;
    if(!strcmp(one.title,s->title)){
        unsigned empty_absent=c->policy==NRC_ABSENT&&!one.content[0]&&!one.installed&&!one.managed&&!one.mounted;
        if(++c->owned_rows>1||strcmp(one.path,s->source)||strcmp(one.runtime,s->source)||strcmp(one.type,"folder")||
           (strcmp(one.content,s->content)&&!empty_absent)||one.pkg||one.image||(one.mounted&&c->policy!=NRC_REGISTRATION))return -1;
    }else if(!strcmp(one.path,s->source)||!strcmp(one.runtime,s->source))return -1;
    return 0;
}
static int manual_item(void *opaque,const void *raw,size_t size){
    struct collection *c=opaque;char path[BW_NRC_PATH_BYTES],title[10];
    if(++c->count>NRC_REMOTE_ITEMS||bw_document_string(raw,size,path,sizeof path)||literal_path(path)||current(c->remote))return -1;
    if(!strcmp(path,c->subject->source))return ++c->owned_rows>1?-1:0;
    return manual_title(c->remote,path,title)||!strcmp(title,c->subject->title)?-1:0;
}
/* Lexical normalization is used ONLY to detect overlap/aliases. It never
 * grants ownership or changes a source path. Resolve existing paths too, so a
 * disjoint-looking symlink into the selected tree cannot be ignored. */
static int prepare_normal(const char *path,char out[BW_NRC_PATH_BYTES]){
    if(!path||path[0]!='/'||strnlen(path,BW_NRC_PATH_BYTES)>=BW_NRC_PATH_BYTES)return -1;
    size_t used=1;out[0]='/';out[1]=0;
    for(const char *p=path+1;*p;){
        while(*p=='/')p++;if(!*p)break;const char *end=strchr(p,'/');size_t n=end?(size_t)(end-p):strlen(p);
        for(size_t i=0;i<n;i++)if((unsigned char)p[i]<32||(unsigned char)p[i]==127||p[i]=='\\')return -1;
        if(n==1&&p[0]=='.'){}
        else if(n==2&&p[0]=='.'&&p[1]=='.'){
            if(used==1)return -1;while(used>1&&out[used-1]!='/')used--;if(used>1)used--;out[used]=0;
        }else{
            if(used>1)out[used++]='/';if(n>=BW_NRC_PATH_BYTES-used)return -1;
            memcpy(out+used,p,n);used+=n;out[used]=0;
        }
        if(!end)break;p=end+1;
    }return 0;
}
static int path_contains(const char *a,const char *b){size_t n=strlen(a);return !strcmp(a,"/")||(!strncmp(a,b,n)&&(!b[n]||b[n]=='/'));}
/* 0=disjoint literal, 1=exact selected path, 2=overlap/target alias,
 * 3=disjoint alias (unknown), -1=unclassifiable path. */
static int prepare_path(const char *path,const struct bw_nrc_subject *s){
    char normal[BW_NRC_PATH_BYTES],resolved[PATH_MAX];if(prepare_normal(path,normal))return -1;
    int exact=!strcmp(path,s->source),alias=strcmp(path,normal)!=0;
    if(path_contains(normal,s->source)||path_contains(s->source,normal)){
        if(!exact)return 2;
    }
    if(realpath(path,resolved)){
        if(path_contains(resolved,s->source)||path_contains(s->source,resolved)){
            if(!exact||strcmp(resolved,s->source))return 2;
        }
        if(strcmp(path,resolved))alias=1;
    }
    return exact?1:alias?3:0;
}
static const char *path_class(const char *path){
    if(!strncmp(path,"/data/",6))return "data";if(!strncmp(path,"/mnt/usb",8))return "usb";
    if(!strncmp(path,"/mnt/ext",8))return "external";if(!strncmp(path,"/user/",6))return "user";return "other";
}
static int collection_error(struct collection *c,const char *stage,unsigned missing,const char *class,int error){
    c->remote->substage=stage;c->remote->row_index=c->count;c->remote->missing_fields=missing;
    c->remote->path_class=class;c->remote->observation_error=error;return -1;
}
struct prepare_game {struct game one;unsigned invalid,duplicate;};
static int prepare_game_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct prepare_game *p=opaque;unsigned before=p->one.seen;
    if(game_field(&p->one,key,kn,raw,size)){
        unsigned added=p->one.seen^before;if(!added)p->duplicate=1;else p->invalid|=added;
    }return 0;
}
static int prepare_game_item(void *opaque,const void *raw,size_t size){
    struct collection *c=opaque;struct prepare_game parsed={0};struct game *one=&parsed.one;
    if(++c->count>NRC_REMOTE_ITEMS)return collection_error(c,"item_limit",0,NULL,0);
    if(current(c->remote))return collection_error(c,"authority_changed",0,NULL,0);
    if(bw_document_fields(raw,size,prepare_game_field,&parsed)||parsed.duplicate)return collection_error(c,"malformed_or_duplicate_game_fields",0,NULL,0);
    unsigned missing=2047u&~one->seen;
    if(!(one->seen&G_PATH)||(parsed.invalid&G_PATH)||!one->path[0])return collection_error(c,"game_path_unclassifiable",missing,NULL,0);
    int path=prepare_path(one->path,c->subject),runtime=0;
    if(path<0||path==2)return collection_error(c,"game_path_overlap_or_invalid",missing,path_class(one->path),0);
    if(parsed.invalid&G_RUNTIME)return collection_error(c,"game_runtime_unclassifiable",missing,path_class(one->path),0);
    if(one->runtime[0]){
        runtime=prepare_path(one->runtime,c->subject);
        if(runtime<0||runtime==2)return collection_error(c,"game_runtime_overlap_or_invalid",missing,path_class(one->runtime),0);
    }
    if(!strcmp(one->title,c->subject->title)){
        unsigned absent=c->policy==NRC_PREPARE_ABSENT&&!one->content[0]&&!one->installed&&!one->managed&&!one->mounted;
        if(missing||parsed.invalid||++c->owned_rows>1||path!=1||runtime!=1||strcmp(one->type,"folder")||
           (strcmp(one->content,c->subject->content)&&!absent)||one->pkg||one->image||
           (one->mounted&&c->policy!=NRC_PREPARE_REGISTERED))return collection_error(c,"selected_game_conflict_or_incomplete",missing,path_class(one->path),0);
    }else{
        if(path==1||runtime==1)return collection_error(c,"foreign_title_uses_selected_source",missing,path_class(one->path),0);
        if(!one->title[0]||(parsed.invalid&G_TITLE)){
            char title[10]={0};(void)manual_title(c->remote,one->path,title);
            if(c->remote->observation_release_unknown||current(c->remote))return collection_error(c,"game_metadata_release_or_authority_unknown",missing,path_class(one->path),errno);
            if(!strcmp(title,c->subject->title))return collection_error(c,"game_metadata_same_title_conflict",missing,path_class(one->path),0);
        }
        if(missing||parsed.invalid||!one->title[0]||path==3||runtime==3){
            c->remote->unknown_game_rows++;c->remote->substage="disjoint_game_schema_unknown";
            c->remote->row_index=c->count;c->remote->missing_fields=missing;c->remote->path_class=path_class(one->path);
        }
    }
    return current(c->remote)?collection_error(c,"authority_changed",missing,NULL,0):0;
}
static int prepare_manual_item(void *opaque,const void *raw,size_t size){
    struct collection *c=opaque;char path[BW_NRC_PATH_BYTES],title[10]={0};
    if(++c->count>NRC_REMOTE_ITEMS)return collection_error(c,"item_limit",0,NULL,0);
    if(bw_document_string(raw,size,path,sizeof path)||current(c->remote))return collection_error(c,"manual_path_malformed_or_authority_changed",0,NULL,0);
    int relation=prepare_path(path,c->subject);
    if(relation<0||relation==2)return collection_error(c,"manual_path_overlap_or_invalid",0,path_class(path),0);
    if(relation==1)return ++c->owned_rows>1?collection_error(c,"duplicate_selected_manual_path",0,path_class(path),0):0;
    errno=0;int result=manual_title(c->remote,path,title),error=result?c->remote->observation_error:0;
    if(c->remote->observation_release_unknown||current(c->remote))return collection_error(c,"manual_release_or_authority_unknown",0,path_class(path),error);
    if(!strcmp(title,c->subject->title))return collection_error(c,"manual_same_title_conflict",0,path_class(path),0);
    if(result){c->remote->unknown_manual_paths++;
        c->remote->row_index=c->count;c->remote->path_class=path_class(path);c->remote->observation_error=error;}
    return 0;
}

struct fields {struct collection *collection;unsigned manual;};
static int collection_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct fields *f=opaque;struct collection *c=f->collection;unsigned bit=0,n=0;
    if(kn==6&&!memcmp(key,"status",6))bit=1u;
    else if(kn==5&&!memcmp(key,"count",5))bit=2u;
    else if(f->manual&&kn==5&&!memcmp(key,"paths",5))bit=4u;
    else if(!f->manual&&kn==5&&!memcmp(key,"games",5))bit=4u;else return 0;
    if(c->seen&bit)return -1;c->seen|=bit;
    if(bit==4u)return bw_document_array(raw,size,preparing(c->policy)?
        (f->manual?prepare_manual_item:prepare_game_item):(f->manual?manual_item:game_item),c);
    if(decimal(raw,size,NRC_REMOTE_ITEMS,&n)||(bit==1u&&n))return -1;if(bit==2u)c->declared=n;return 0;
}
static int documents(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,
    const void *games,size_t game_size,const void *manual,size_t manual_size,unsigned policy){
    if(!r||!s)return -1;r->reason="cleanup_remote_unknown";r->substage=r->path_class=NULL;
    r->row_index=r->missing_fields=r->unknown_game_rows=r->unknown_manual_paths=r->observation_release_unknown=0;
    r->preparation_only=(unsigned)preparing(policy);r->observation_error=0;
    if(current(r)||subject_shape(s))return fail(r,"cleanup_remote_subject_invalid");
    struct collection c={.remote=r,.subject=s,.policy=policy};struct fields fields={.collection=&c};
    if(bw_document_fields(games,game_size,collection_field,&fields)||c.seen!=7u||c.count!=c.declared)return fail(r,"cleanup_game_snapshot_conflict_or_unknown");
    c=(struct collection){.remote=r,.subject=s,.policy=policy};fields.manual=1;
    if(bw_document_fields(manual,manual_size,collection_field,&fields)||c.seen!=7u||c.count!=c.declared||current(r))return fail(r,"cleanup_manual_source_conflict_or_unknown");
    r->reason="cleanup_remote_sources_checked";
    if(preparing(policy)&&(r->unknown_game_rows||r->unknown_manual_paths))
        bw_send_log("prepare-source-summary","policy=prepare-only unknown_rows=%u unknown_paths=%u last_row=%u missing=%u substage=%s class=%s errno=%d destructive_proof=0",
            r->unknown_game_rows,r->unknown_manual_paths,r->row_index,r->missing_fields,r->substage?r->substage:"",r->path_class?r->path_class:"",r->observation_error);
    return 0;
}
int bw_nrc_remote_documents(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,
    const void *games,size_t game_size,const void *manual,size_t manual_size){
    return documents(r,s,games,game_size,manual,manual_size,NRC_STRICT);
}
int bw_nrc_remote_registration_documents(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,
    const void *games,size_t game_size,const void *manual,size_t manual_size){
    return documents(r,s,games,game_size,manual,manual_size,NRC_REGISTRATION);
}
static int absence_twice(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,bw_nrc_remote_absent_fn absent,void *opaque){
    if(!r||subject_shape(s)||!absent)return fail(r,"cleanup_registration_absence_unconfirmed");
    for(unsigned i=0;i<2;i++)if(current(r)||absent(opaque,s)||current(r))return fail(r,"cleanup_registration_absence_unconfirmed");
    return 0;
}
int bw_nrc_remote_absent_documents(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,
    const void *games,size_t game_size,const void *manual,size_t manual_size,bw_nrc_remote_absent_fn absent,void *opaque){
    return absence_twice(r,s,absent,opaque)||documents(r,s,games,game_size,manual,manual_size,NRC_ABSENT)||
        absence_twice(r,s,absent,opaque)?-1:0;
}
static int sources(void *opaque,const struct bw_nrc_subject *subject,unsigned policy,bw_nrc_remote_absent_fn absent,void *proof){
    struct bw_nrc_remote *r=opaque;if(!r)return -1;struct bw_sm_reply version={0};
    struct bw_sm_document games[2]={{0}},manual[2]={{0}};int result=-1;
    r->reason="cleanup_remote_capability_unknown";
    if(current(r)||bw_sm_post(&r->connection,BW_SM_VERSION,"/",&version)||!version.uninstall_supported)goto done;
    for(unsigned i=0;i<2;i++){
        r->reason="cleanup_remote_observation_unknown";
        if((policy==NRC_ABSENT&&absence_twice(r,subject,absent,proof))||
           bw_sm_observe(&r->connection,BW_SM_OBSERVE_GAMES,games+i)||
           bw_sm_observe(&r->connection,BW_SM_OBSERVE_MANUAL_PATHS,manual+i)||
           documents(r,subject,games[i].bytes,games[i].size,manual[i].bytes,manual[i].size,policy)||
           (policy==NRC_ABSENT&&absence_twice(r,subject,absent,proof)))goto done;
    }
    if(games[0].size!=games[1].size||manual[0].size!=manual[1].size||
       memcmp(games[0].bytes,games[1].bytes,games[0].size)||memcmp(manual[0].bytes,manual[1].bytes,manual[0].size)||current(r)){
        fail(r,"cleanup_remote_snapshot_changed");goto done;
    }
    r->reason="cleanup_remote_sources_checked";result=0;
done:
    for(unsigned i=0;i<2;i++){bw_sm_document_clear(games+i);bw_sm_document_clear(manual+i);}return result;
}
int bw_nrc_remote_sources(void *opaque,const struct bw_nrc_subject *subject){return sources(opaque,subject,NRC_STRICT,NULL,NULL);}
int bw_nrc_remote_registration_sources(void *opaque,const struct bw_nrc_subject *subject){return sources(opaque,subject,NRC_REGISTRATION,NULL,NULL);}
int bw_nrc_remote_absent_sources(void *opaque,const struct bw_nrc_subject *subject,bw_nrc_remote_absent_fn absent,void *proof){
    if(!absent)return fail(opaque,"cleanup_registration_absence_unconfirmed");
    return sources(opaque,subject,NRC_ABSENT,absent,proof);
}
static int confirmed_twice(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,bw_nrc_remote_registered_fn registered,void *proof,int *expected){
    if(!r||subject_shape(s)||!registered)return fail(r,"cleanup_confirmed_registration_unqualified");
    for(unsigned i=0;i<2;i++){
        if(current(r))return fail(r,"cleanup_confirmed_registration_unqualified");
        int state=registered(proof,s);
        if((state!=0&&state!=1)||(*expected>=0&&state!=*expected)||current(r))return fail(r,"cleanup_confirmed_registration_changed_or_unknown");
        *expected=state;
    }return 0;
}
int bw_nrc_remote_confirmed_documents(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,
    const void *games,size_t game_size,const void *manual,size_t manual_size,bw_nrc_remote_registered_fn registered,void *proof){
    int expected=-1;return confirmed_twice(r,s,registered,proof,&expected)||
        documents(r,s,games,game_size,manual,manual_size,expected?NRC_REGISTRATION:NRC_ABSENT)||
        confirmed_twice(r,s,registered,proof,&expected)?-1:0;
}
int bw_nrc_remote_prepare_documents(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,
    const void *games,size_t game_size,const void *manual,size_t manual_size,bw_nrc_remote_registered_fn witness,void *proof){
    int expected=-1;
    return confirmed_twice(r,s,witness,proof,&expected)||
        documents(r,s,games,game_size,manual,manual_size,expected?NRC_PREPARE_REGISTERED:NRC_PREPARE_ABSENT)||
        confirmed_twice(r,s,witness,proof,&expected)?-1:0;
}
int bw_nrc_remote_prepare_sources(void *opaque,const struct bw_nrc_subject *subject,bw_nrc_remote_registered_fn witness,void *proof){
    struct bw_nrc_remote *r=opaque;if(!r||!witness)return fail(r,"cleanup_prepare_exact_witness_unconfirmed");
    struct bw_sm_reply version={0};struct bw_sm_document games[2]={{0}},manual[2]={{0}};int result=-1,expected=-1;
    r->reason="cleanup_remote_capability_unknown";
    if(current(r)||bw_sm_post(&r->connection,BW_SM_VERSION,"/",&version))goto done;
    for(unsigned i=0;i<2;i++){
        r->reason="cleanup_remote_observation_unknown";
        if(confirmed_twice(r,subject,witness,proof,&expected)||
           bw_sm_observe(&r->connection,BW_SM_OBSERVE_GAMES,games+i)||bw_sm_observe(&r->connection,BW_SM_OBSERVE_MANUAL_PATHS,manual+i)||
           documents(r,subject,games[i].bytes,games[i].size,manual[i].bytes,manual[i].size,expected?NRC_PREPARE_REGISTERED:NRC_PREPARE_ABSENT)||
           confirmed_twice(r,subject,witness,proof,&expected))goto done;
    }
    if(games[0].size!=games[1].size||manual[0].size!=manual[1].size||memcmp(games[0].bytes,games[1].bytes,games[0].size)||
       memcmp(manual[0].bytes,manual[1].bytes,manual[0].size)||current(r)){fail(r,"cleanup_remote_snapshot_changed");goto done;}
    r->reason="cleanup_prepare_sources_observed_not_destructive_proof";result=0;
done:
    if(result)bw_send_log("prepare-source-failure","policy=prepare-only reason=%s row=%u missing=%u unknown_rows=%u unknown_paths=%u substage=%s class=%s errno=%d",
        r->reason?r->reason:"",r->row_index,r->missing_fields,r->unknown_game_rows,r->unknown_manual_paths,r->substage?r->substage:"",r->path_class?r->path_class:"",r->observation_error);
    for(unsigned i=0;i<2;i++){bw_sm_document_clear(games+i);bw_sm_document_clear(manual+i);}return result;
}

int bw_nrc_remote_confirmed_sources(void *opaque,const struct bw_nrc_subject *subject,bw_nrc_remote_registered_fn registered,void *proof){
    struct bw_nrc_remote *r=opaque;if(!r||!registered)return fail(r,"cleanup_confirmed_registration_unqualified");
    struct bw_sm_reply version={0};struct bw_sm_document games[2]={{0}},manual[2]={{0}};int result=-1,expected=-1;
    r->reason="cleanup_remote_capability_unknown";
    if(current(r)||bw_sm_post(&r->connection,BW_SM_VERSION,"/",&version)||!version.uninstall_supported)goto done;
    for(unsigned i=0;i<2;i++){
        if(confirmed_twice(r,subject,registered,proof,&expected)||
           bw_sm_observe(&r->connection,BW_SM_OBSERVE_GAMES,games+i)||bw_sm_observe(&r->connection,BW_SM_OBSERVE_MANUAL_PATHS,manual+i)||
           documents(r,subject,games[i].bytes,games[i].size,manual[i].bytes,manual[i].size,expected?NRC_REGISTRATION:NRC_ABSENT)||
           confirmed_twice(r,subject,registered,proof,&expected))goto done;
    }
    if(games[0].size!=games[1].size||manual[0].size!=manual[1].size||memcmp(games[0].bytes,games[1].bytes,games[0].size)||
       memcmp(manual[0].bytes,manual[1].bytes,manual[0].size)||current(r)){fail(r,"cleanup_remote_snapshot_changed");goto done;}
    r->reason="cleanup_remote_sources_checked";result=0;
done:for(unsigned i=0;i<2;i++){bw_sm_document_clear(games+i);bw_sm_document_clear(manual+i);}return result;
}
int bw_nrc_remote_uninstall(void *opaque,const char title[10]){
    struct bw_nrc_remote *r=opaque;if(!r||current(r))return -1;
    struct bw_sm_uninstall_reply reply={0};int result=bw_sm_uninstall(&r->connection,title,&reply);
    r->reason=result?(reply.refused_before_platform?
        (!strcmp(reply.error_reason,"title_unmount_busy")?"cleanup_uninstall_refused_title_unmount_busy_before_platform":"cleanup_uninstall_refused_before_platform"):
        "cleanup_uninstall_outcome_unknown"):"cleanup_uninstall_requested_not_removal_proof";
    return result?(reply.refused_before_platform?1:-1):0;
}
int bw_nrc_remote_release_runtime(struct bw_nrc_remote *r,const struct bw_nrc_subject *s,bw_nrc_remote_registered_fn registered,void *proof){
    if(!r||!s||!registered)return fail(r,"cleanup_runtime_release_authority_unknown");
    struct bw_sm_reply version={0};int state=-1;
    if(confirmed_twice(r,s,registered,proof,&state)||state!=1||
       bw_nrc_remote_confirmed_sources(r,s,registered,proof)||
       bw_sm_post(&r->connection,BW_SM_VERSION,"/",&version)||!version.unmount_supported||
       confirmed_twice(r,s,registered,proof,&state)||state!=1)
        return fail(r,"cleanup_runtime_release_authority_or_capability_unknown");
    struct bw_sm_unmount_reply reply={0};
    if(bw_sm_unmount(&r->connection,s->title,&reply))
        return fail(r,reply.response_complete&&reply.api_status==16?"cleanup_runtime_release_busy_no_retry":"cleanup_runtime_release_outcome_unknown_no_retry");
    /* Release does not remove the registration or grant a new baseline. */
    if(confirmed_twice(r,s,registered,proof,&state)||state!=1||bw_nrc_remote_sources(r,s)||
       confirmed_twice(r,s,registered,proof,&state)||state!=1)
        return fail(r,"cleanup_runtime_release_fresh_unmounted_binding_unconfirmed");
    r->reason="cleanup_runtime_released_same_registration_reconfirmed";return 0;
}
const char *bw_nrc_remote_status_message(const struct bw_nrc_remote *r){
    if(!r||!r->reason)return "The game removal result is unknown. Refresh status without retrying.";
    if(!strcmp(r->reason,"cleanup_runtime_release_busy_no_retry"))return "Could not release the game mount because it is busy. No uninstall was submitted.";
    if(!strcmp(r->reason,"cleanup_runtime_release_authority_or_capability_unknown"))return "Could not confirm permission or support to release the game mount. No uninstall was submitted.";
    if(!strcmp(r->reason,"cleanup_runtime_release_fresh_unmounted_binding_unconfirmed"))return "Could not confirm the released game mount. No uninstall was submitted.";
    if(!strcmp(r->reason,"cleanup_runtime_release_outcome_unknown_no_retry"))return "Could not confirm release of the game mount. No uninstall was submitted; refresh status without retrying.";
    if(!strcmp(r->reason,"cleanup_uninstall_refused_title_unmount_busy_before_platform"))return "ShadowMount could not release the game mount and refused uninstall before removing the registration.";
    if(!strcmp(r->reason,"cleanup_uninstall_refused_before_platform"))return "ShadowMount refused uninstall before removing the registration. See the diagnostic reason.";
    return "The game removal result is unknown. Refresh status without retrying.";
}
