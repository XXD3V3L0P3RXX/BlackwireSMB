#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "catalog_adapter.h"
#include "native_document.h"
#include "source_param.h"
#include "native_registration.h"
#include "registration_param_compat.h"
#include "copy_sha256.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {P_PATH=1u,P_RUNTIME=2u,P_TYPE=4u,P_TITLE=8u,P_NAME=16u,P_CONTENT=32u,
      P_INSTALLED=64u,P_PACKAGE=128u,P_MANAGED=256u,P_AVAILABLE=512u,P_IMAGE=1024u};
struct record_parser {struct bd_smp_record value;unsigned seen;};
struct list_parser {struct bd_smp_record *rows;size_t count,capacity,declared;unsigned seen;};
static int decimal(const void *raw,size_t size,size_t maximum,size_t *out){
    const unsigned char *p=raw;size_t n=0;if(!size||(size>1&&p[0]=='0'))return -1;
    for(size_t i=0;i<size;i++){unsigned d=p[i]-'0';if(d>9||d>maximum||n>(maximum-d)/10)return -1;n=n*10+d;}*out=n;return 0;
}
static int boolean(const void *p,size_t n,unsigned *out){
    if(n==4&&!memcmp(p,"true",4)){*out=1;return 0;}if(n==5&&!memcmp(p,"false",5)){*out=0;return 0;}return -1;
}
static int record_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct record_parser *p=opaque;unsigned bit=0;char *text=NULL;size_t cap=0;unsigned *flag=NULL;
#define FIELD(k,b,m) if(kn==sizeof(k)-1&&!memcmp(key,k,kn)){bit=b;text=p->value.m;cap=sizeof p->value.m;}
    FIELD("path",P_PATH,source) else FIELD("runtime_path",P_RUNTIME,runtime_source)
    else FIELD("source_type",P_TYPE,source_type) else FIELD("title_id",P_TITLE,title_id)
    else FIELD("title_name",P_NAME,title) else FIELD("content_id",P_CONTENT,content_id)
#undef FIELD
#define FLAG(k,b,m) if(kn==sizeof(k)-1&&!memcmp(key,k,kn)){bit=b;flag=&p->value.m;}
    else FLAG("installed",P_INSTALLED,installed) else FLAG("installed_pkg",P_PACKAGE,installed_pkg)
    else FLAG("managed",P_MANAGED,managed) else FLAG("source_available",P_AVAILABLE,source_available)
    else FLAG("image_backed",P_IMAGE,image_backed) else return 0;
#undef FLAG
    if(p->seen&bit)return -1;p->seen|=bit;
    return text?bw_document_string(raw,size,text,cap):boolean(raw,size,flag);
}
static int list_item(void *opaque,const void *raw,size_t size){
    struct list_parser *p=opaque;if(p->count>=p->capacity)return -1;
    struct record_parser next={0};
    if(bw_document_fields(raw,size,record_field,&next)||next.seen!=2047u)return -1;
    p->rows[p->count++]=next.value;return 0;
}
static int list_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct list_parser *p=opaque;unsigned bit=0;
    if(kn==6&&!memcmp(key,"status",6))bit=1;
    else if(kn==5&&!memcmp(key,"count",5))bit=2;
    else if(kn==5&&!memcmp(key,"games",5))bit=4;
    else return 0;
    if(p->seen&bit)return -1;p->seen|=bit;
    if(bit==4)return bw_document_array(raw,size,list_item,p);
    size_t n=0;if(decimal(raw,size,BD_CATALOG_MAX,&n))return -1;
    if(bit==1)return n?-1:0;p->declared=n;return 0;
}
int bd_smp_records_parse(const void *raw,size_t size,struct bd_smp_record *out,size_t cap,size_t *count){
    if(count)*count=0;if(!raw||!out||!count||!cap||cap>BD_CATALOG_MAX)return -1;
    struct list_parser p={.rows=out,.capacity=cap};
    if(bw_document_fields(raw,size,list_field,&p)||p.seen!=7||p.count!=p.declared){memset(out,0,cap*sizeof *out);return -1;}
    *count=p.count;return 0;
}
static int same(const struct stat *a,const struct stat *b){
    return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&a->st_mode==b->st_mode&&
           a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
           a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int canonical(const char *path,struct stat *out,int dir){
    char real[PATH_MAX];struct stat s;
    if(!path||strnlen(path,BD_SOURCE_BYTES)>=BD_SOURCE_BYTES||!realpath(path,real)||strcmp(path,real)||lstat(path,&s)||
       (dir?!S_ISDIR(s.st_mode):!S_ISREG(s.st_mode)))return -1;
    if(out)*out=s;return 0;
}
static int join(char out[BD_SOURCE_BYTES],const char *base,const char *leaf){
    int n=snprintf(out,BD_SOURCE_BYTES,"%s/%s",base,leaf);return n<0||(size_t)n>=BD_SOURCE_BYTES?-1:0;
}
static int under(const char *path,const char *root){size_t n=strlen(root);return !strncmp(path,root,n)&&(!path[n]||path[n]=='/');}
static int exists(const char *path){struct stat s;return lstat(path,&s)?(errno==ENOENT?0:-1):1;}
static int blob(const char *path,unsigned char *bytes,size_t limit,size_t *size,struct stat *out){
    struct stat first,held,named;if(canonical(path,&first,0)||first.st_size<=0||(uint64_t)first.st_size>limit||first.st_nlink!=1)return -1;
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;int result=-1;size_t used=0;
    if(fstat(fd,&held)||!same(&first,&held))goto done;
    while(used<(size_t)first.st_size){ssize_t got=pread(fd,bytes+used,(size_t)first.st_size-used,(off_t)used);
        if(got<0&&errno==EINTR)continue;if(got<=0)goto done;used+=(size_t)got;}
    if(fstat(fd,&held)||lstat(path,&named)||!same(&first,&held)||!same(&held,&named)||canonical(path,NULL,0))goto done;
    *size=used;if(out)*out=first;result=0;
done:if(close(fd))result=-1;return result;
}
static void stamp(struct cp_sha *hash,const struct stat *s){
    /* Explicit values avoid padding and make the observed identity portable. */
    uint64_t v[]={(uint64_t)s->st_dev,(uint64_t)s->st_ino,(uint64_t)s->st_size,(uint64_t)s->st_mode,
        (uint64_t)s->st_mtim.tv_sec,(uint64_t)s->st_mtim.tv_nsec,(uint64_t)s->st_ctim.tv_sec,(uint64_t)s->st_ctim.tv_nsec};
    for(size_t i=0;i<sizeof v/sizeof v[0];i++){unsigned char b[8];for(unsigned j=0;j<8;j++)b[j]=(unsigned char)(v[i]>>(8*j));cp_sha_update(hash,b,8);}
}
static int valid_title(const char *s){
    if(strnlen(s,10)!=9||(memcmp(s,"PPSA",4)&&memcmp(s,"CUSA",4)))return 0;
    for(unsigned i=4;i<9;i++)if(s[i]<'0'||s[i]>'9')return 0;return 1;
}
int bd_local_observe(const struct bd_smp_record *r,const struct bd_local_environment *e,struct bd_local_observation *out){
    if(!out)return -1;memset(out,0,sizeof *out);
    if(!r||!e||!e->apps_root||!e->database||!valid_title(r->title_id)||
       !memchr(r->title,0,sizeof r->title)||!r->title[0]||!memchr(r->source,0,sizeof r->source)||r->source[0]!='/'||
       !memchr(r->runtime_source,0,sizeof r->runtime_source)||!memchr(r->content_id,0,sizeof r->content_id)||
       !memchr(r->source_type,0,sizeof r->source_type)||r->installed>1||r->installed_pkg>1||r->managed>1||
       r->source_available>1||r->image_backed>1||e->copy_idle_observed>1)return -1;
    if(strcmp(r->source_type,r->installed_pkg?"pkg":"folder"))return -1;
    out->item=(struct bd_local_input){.title=r->title,.title_id=r->title_id,.source=r->source,.registered_source=out->registered_source,
        .kind=r->installed_pkg?BD_INSTALLED_PACKAGE:BD_LOCAL_FOLDER};
    memcpy(out->registered_source,r->source,strlen(r->source)+1);
    static const char domain[]="blackwire-local-observation-v1";
    struct cp_sha hash;cp_sha_init(&hash);cp_sha_update(&hash,domain,sizeof domain);
    cp_sha_update(&hash,r->title_id,10);cp_sha_update(&hash,r->source,strlen(r->source)+1);cp_sha_update(&hash,r->content_id,strlen(r->content_id)+1);
    char app[BD_SOURCE_BYTES],path[BD_SOURCE_BYTES],link[BD_SOURCE_BYTES],otherlink[BD_SOURCE_BYTES],image_link[BD_SOURCE_BYTES];
    struct stat app_st,source_st,boot_st,param_st,registered_param_st,link_st;
    if(under(r->source,"/data/blackwire"))out->item.exclusions|=BD_LOCAL_BLACKWIRE_OWNED;
    if(e->active_transfer_source&&*e->active_transfer_source&&(under(e->active_transfer_source,r->source)||under(r->source,e->active_transfer_source)))out->item.exclusions|=BD_LOCAL_TRANSFER_ACTIVE;
    if(r->installed)out->item.facts|=BD_LOCAL_REGISTERED;
    if(join(app,e->apps_root,r->title_id)||canonical(app,&app_st,1))goto finished;
    stamp(&hash,&app_st);
    if(canonical(r->source,&source_st,!r->installed_pkg)||!r->source_available)goto finished;
    out->item.facts|=BD_LOCAL_SOURCE_PRESENT;stamp(&hash,&source_st);
    if(join(link,app,"mount.lnk")||join(otherlink,app,"mount_image.lnk")||join(image_link,app,"mount_img.lnk"))goto finished;
    if(r->installed_pkg){
        const char *leaf=strrchr(r->source,'/');size_t n=leaf?(size_t)(leaf-r->source):0;
        if(!leaf||strcmp(leaf,"/app.pkg")||n<10||r->source[n-10]!='/'||memcmp(r->source+n-9,r->title_id,9)||
           source_st.st_size<=0||exists(link)!=0||exists(otherlink)!=0||exists(image_link)!=0)goto finished;
        if(bw_registration_db_match(e->database,r->title_id,r->content_id)!=1)goto finished;
        out->item.facts|=BD_LOCAL_BOOT_METADATA|BD_LOCAL_REGISTRATION_MATCH;
    }else{
        if(strcmp(r->runtime_source,r->source)||!r->managed||r->image_backed)goto finished;
        static const char *markers[]={"blackwire.files","blackwire.digests","blackwire.prepared"};
        for(size_t i=0;i<sizeof markers/sizeof markers[0];i++){
            if(join(path,r->source,markers[i]))goto finished;int found=exists(path);
            if(found<0)goto finished;if(found)out->item.exclusions|=BD_LOCAL_BLACKWIRE_MARKER;
        }
        if(out->item.exclusions&BD_LOCAL_BLACKWIRE_MARKER)goto finished;
        if(join(path,r->source,"eboot.bin")||canonical(path,&boot_st,0)||boot_st.st_size<16||!(boot_st.st_mode&0111))goto finished;
        stamp(&hash,&boot_st);
        unsigned char *source=malloc(BW_SOURCE_PARAM_LIMIT),*registered=malloc(BW_SOURCE_PARAM_LIMIT);
        if(!source||!registered){free(source);free(registered);return -1;}
        size_t source_n=0,registered_n=0,link_n=0;char title[10],content[128];unsigned char target[BD_SOURCE_BYTES];
        int bad=join(path,r->source,"sce_sys/param.json")||blob(path,source,BW_SOURCE_PARAM_LIMIT,&source_n,&param_st)||
            bw_source_param_identity(source,source_n,title,content)||strcmp(title,r->title_id)||strcmp(content,r->content_id)||
            join(path,app,"sce_sys/param.json")||blob(path,registered,BW_SOURCE_PARAM_LIMIT,&registered_n,&registered_param_st)||
            bw_registration_param_pair(source,source_n,registered,registered_n)<0||
            blob(link,target,sizeof target-1,&link_n,&link_st)||exists(otherlink)!=0||exists(image_link)!=0;
        if(!bad){
            while(link_n&&(target[link_n-1]=='\r'||target[link_n-1]=='\n'))link_n--;
            if(link_n!=strlen(r->source)||memcmp(target,r->source,link_n)||bw_registration_db_match(e->database,title,content)!=1)bad=1;
        }
        if(!bad){out->item.facts|=BD_LOCAL_BOOT_METADATA|BD_LOCAL_REGISTRATION_MATCH;cp_sha_update(&hash,source,source_n);stamp(&hash,&param_st);stamp(&hash,&link_st);}
        free(source);free(registered);if(bad)goto finished;
    }
    /* Repeat the named identities before publishing this observation. Dispatch
     * re-observes the full source/registration, including its database row. */
    struct stat again;
    if(canonical(app,&again,1)||!same(&app_st,&again)||canonical(r->source,&again,!r->installed_pkg)||!same(&source_st,&again))goto finished;
    if(bw_registration_db_match(e->database,r->title_id,r->content_id)!=1)goto finished;
    if(!r->installed_pkg&&
       (join(path,r->source,"eboot.bin")||canonical(path,&again,0)||!same(&boot_st,&again)||
        join(path,r->source,"sce_sys/param.json")||canonical(path,&again,0)||!same(&param_st,&again)||
        join(path,app,"sce_sys/param.json")||canonical(path,&again,0)||!same(&registered_param_st,&again)||
        canonical(link,&again,0)||!same(&link_st,&again)))goto finished;
    out->metadata_verified=1;
    if(e->copy_idle_observed&&!(out->item.exclusions&BD_LOCAL_TRANSFER_ACTIVE))out->item.facts|=BD_LOCAL_COPY_IDLE;
finished:
    cp_sha_final(&hash,out->item.source_fingerprint);return 0;
}
