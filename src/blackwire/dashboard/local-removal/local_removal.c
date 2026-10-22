/* SPDX-License-Identifier: GPL-3.0-or-later
 * Explicit confirmed local uninstall, separate from automatic BW cleanup. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#ifdef BD_LOCAL_REMOVAL_HOST_TEST
#define BW_COPY_HOST_TEST 1
#endif
#include "local_removal.h"
#include "copy_fs.h"
#include "copy_sha256.h"
#include "native_registration.h"
#include "source_param.h"
#include "shadowmount_client.h"
#include <dirent.h>

#define LM_DEPTH 48u
#define LM_NODE_BYTES 360u
#define LM_RECORD_BYTES 4096u
static const char lm_scope[]="Blackwire confirmed local removals v1\n";
struct lm_anchor {int fd;char path[BD_SOURCE_BYTES];struct stat st;};
struct lm_node {uint32_t parent;unsigned directory;char leaf[256];struct stat st;};
struct lm_control {int fd;char name[32];struct stat st;};
struct bd_local_removal {
    struct bd_local_removal_config config;struct bd_local_removal_ops ops;
    struct bd_local_removal_request request;struct bd_local_removal_result result;
    char data_path[BD_SOURCE_BYTES],apps_path[BD_SOURCE_BYTES],db_path[BD_SOURCE_BYTES];
    char folder_roots[BD_LOCAL_REMOVE_ROOTS][BD_SOURCE_BYTES],package_roots[BD_LOCAL_REMOVE_ROOTS][BD_SOURCE_BYTES];
    char content[128],original_leaf[256],quarantine[BD_SOURCE_BYTES];
    struct lm_anchor data,ledger,operation,apps,app,source_parent,source,package_parent,package_title;
    int package_fd;
    struct lm_node *nodes;uint32_t count,capacity,*slots,slot_count;
    struct lm_control controls[8];unsigned control_count;
    struct stat source_param,registered_param,mount;
    uint8_t source_param_sha[32],registered_param_sha[32],mount_sha[32],record_sha[32];
    uint32_t deleted;unsigned inventory_checked,consumed,manual_consumed,complete,failed,mutation;
};
static int lm_guard(void *opaque){struct bd_local_removal *p=opaque;return !p||!p->ops.current||p->ops.current(p->ops.opaque)?-1:0;}
static int lm_nonce(const char *p){if(!p||strnlen(p,33)!=32)return 0;unsigned any=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",p[i]))return 0;any|=p[i]!='0';}return any!=0;}
static int lm_title(const char *p){if(!p||strnlen(p,10)!=9||memcmp(p,"PPSA",4)||!strcmp(p,"PPSA99171")||!strcmp(p,"PPSA99170"))return 0;for(unsigned i=4;i<9;i++)if(p[i]<'0'||p[i]>'9')return 0;return 1;}
static int lm_path(const char *p){
    size_t n=p?strnlen(p,BD_SOURCE_BYTES):0;if(n<2||n>=BD_SOURCE_BYTES||p[0]!='/'||p[n-1]=='/')return 0;
    for(size_t at=1;at<n;){size_t end=at;while(end<n&&p[end]!='/'){if((unsigned char)p[end]<32||p[end]==127||p[end]=='\\')return 0;end++;}
        if(end==at||(end-at==1&&p[at]=='.')||(end-at==2&&!memcmp(p+at,"..",2)))return 0;at=end+1;}return 1;
}
static int lm_under(const char *p,const char *r){size_t n=strlen(r);return !strncmp(p,r,n)&&p[n]=='/'&&p[n+1];}
static int lm_join(char out[BD_SOURCE_BYTES],const char *a,const char *b){int n=snprintf(out,BD_SOURCE_BYTES,"%s/%s",a,b);return n<0||(size_t)n>=BD_SOURCE_BYTES?-1:0;}
static int lm_node_same(const struct stat *a,const struct stat *b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid;}
static int lm_same(const struct stat *a,const struct stat *b){return lm_node_same(a,b)&&a->st_size==b->st_size&&a->st_nlink==b->st_nlink&&a->st_blocks==b->st_blocks&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static int lm_anchor_current(struct lm_anchor *a){char path[PATH_MAX];struct stat named,held;return a->fd<0||!realpath(a->path,path)||strcmp(a->path,path)||lstat(a->path,&named)||!S_ISDIR(named.st_mode)||fstat(a->fd,&held)||!lm_node_same(&a->st,&held)||!lm_node_same(&held,&named)?-1:0;}
static int lm_anchor_open(struct lm_anchor *a,const char *path,unsigned private){
    char real[PATH_MAX];struct stat named;if(!lm_path(path)||!realpath(path,real)||strcmp(path,real)||lstat(path,&named)||!S_ISDIR(named.st_mode))return -1;
    a->fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);if(a->fd<0)return -1;strcpy(a->path,path);
    return fstat(a->fd,&a->st)||!lm_node_same(&named,&a->st)||(private&&((a->st.st_mode&07777)!=0700||a->st.st_uid!=geteuid()))||lm_anchor_current(a)?-1:0;
}
static int lm_base(struct bd_local_removal *p){
    if(lm_guard(p)||lm_anchor_current(&p->data)||lm_anchor_current(&p->apps)||
       (p->ledger.fd>=0&&lm_anchor_current(&p->ledger))||(p->operation.fd>=0&&lm_anchor_current(&p->operation)))return -1;
    for(unsigned i=0;i<p->control_count;i++){struct stat held,named;struct lm_control *c=&p->controls[i];
        if(fstat(c->fd,&held)||cp_fstatat(p->operation.fd,c->name,&named,AT_SYMLINK_NOFOLLOW)||!lm_same(&c->st,&held)||!lm_same(&held,&named))return -1;}
    return 0;
}
static int lm_out(struct bd_local_removal *p,enum bw_game_uninstall_phase phase,const char *reason,int rc,struct bd_local_removal_result *out){
    p->result.phase=phase;p->result.reason=reason;p->result.mutation_started=p->mutation;p->result.active=p->mutation&&!p->complete;
    if(out)*out=p->result;if(p->ops.observe){struct bw_game_uninstall_progress q={phase,p->result.files_removed,p->result.files_total,reason};p->ops.observe(p->ops.opaque,&q);}return rc;
}
static int lm_file(struct bd_local_removal *p,int root,const char *path,size_t limit,void **body,size_t *size,struct stat *st,uint8_t hash[32]){
    *body=NULL;*size=0;
    /* Explicit two-component metadata paths only; no arbitrary traversal. */
    int parent=root,owned=-1;const char *name=path;char first[256];struct stat directory;const char *slash=strchr(path,'/');
    if(slash){size_t n=(size_t)(slash-path);if(!n||n>=sizeof first||strchr(slash+1,'/'))return -1;memcpy(first,path,n);first[n]=0;
        if(cp_fstatat(root,first,&directory,AT_SYMLINK_NOFOLLOW)||!S_ISDIR(directory.st_mode))return -1;
        owned=v2_openat(root,first,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK,0);if(owned<0)return -1;parent=owned;name=slash+1;}
    int fd=v2_openat(parent,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0),result=-1;struct stat before,held,named;uint8_t *bytes=NULL;
    if(fd<0||lm_guard(p)||fstat(fd,&before)||!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_size<=0||(uint64_t)before.st_size>limit)goto done;
    bytes=malloc((size_t)before.st_size);if(!bytes)goto done;size_t at=0;
    while(at<(size_t)before.st_size){if(lm_guard(p))goto done;ssize_t n=pread(fd,bytes+at,(size_t)before.st_size-at,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>(size_t)before.st_size-at)goto done;at+=(size_t)n;}
    if(fstat(fd,&held)||cp_fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW)||!lm_same(&before,&held)||!lm_same(&held,&named)||lm_guard(p))goto done;
    if(owned>=0&&(fstat(owned,&held)||cp_fstatat(root,first,&named,AT_SYMLINK_NOFOLLOW)||!lm_same(&directory,&held)||!lm_same(&held,&named)))goto done;
    cp_sha_bytes(bytes,at,hash);*body=bytes;bytes=NULL;*size=at;*st=before;result=0;
done:free(bytes);if(fd>=0&&close(fd))result=-1;if(owned>=0&&close(owned))result=-1;if(result){free(*body);*body=NULL;*size=0;}return result;
}
static int lm_collect(struct bd_local_removal *p,unsigned initial){
    struct bd_smp_record *rows=calloc(BD_CATALOG_MAX,sizeof *rows);size_t count=0;int result=-1;if(!rows)return -1;
    if(p->ops.collect){if(p->ops.collect(p->ops.opaque,rows,BD_CATALOG_MAX,&count)||count>BD_CATALOG_MAX)goto done;}
    else {struct bw_sm_connection c={p->config.smp_port,lm_guard,p};struct bw_sm_document d={0};
        int bad=bw_sm_observe(&c,BW_SM_OBSERVE_GAMES,&d)||bd_smp_records_parse(d.bytes,d.size,rows,BD_CATALOG_MAX,&count);bw_sm_document_clear(&d);if(bad)goto done;}
    unsigned selected=0;for(size_t i=0;i<count;i++)if(!strcmp(rows[i].title_id,p->request.selected.title_id)){
        struct bd_smp_record *r=&rows[i];if(strcmp(r->source,p->request.selected.source)||strcmp(r->runtime_source,r->source)||
           r->installed_pkg!=(p->request.selected.kind==BD_INSTALLED_PACKAGE)||r->image_backed||++selected>1)goto done;
        if(initial){struct bd_local_environment env={p->apps_path,p->db_path,1,NULL};struct bd_local_observation obs;struct bd_item item;struct bd_catalog catalog;
            if(bd_local_observe(r,&env,&obs)||!obs.metadata_verified||obs.item.facts!=BD_LOCAL_REQUIRED||obs.item.exclusions||
               bd_catalog_init(&catalog,&item,1,p->request.selected.catalog_generation)||bd_catalog_add_local(&catalog,&obs.item)||catalog.count!=1||
               item.kind!=p->request.selected.kind||strcmp(item.id,p->request.selected.id)||memcmp(item.source_fingerprint,p->request.selected.source_fingerprint,32))goto done;
            strcpy(p->content,r->content_id);
        }else if(strcmp(r->content_id,p->content))goto done;
    }if(initial&&selected!=1)goto done;result=lm_guard(p);
done:free(rows);return result;
}
static int lm_root_policy(const struct bd_local_removal_config *c,const struct bd_item *i){
    if(!c||!i||!lm_title(i->title_id)||!memchr(i->id,0,sizeof i->id)||!i->id[0]||!i->catalog_generation||
       !memchr(i->source,0,sizeof i->source)||!memchr(i->registered_source,0,sizeof i->registered_source)||!lm_path(i->source)||strcmp(i->source,i->registered_source)||
       (i->kind!=BD_LOCAL_FOLDER&&i->kind!=BD_INSTALLED_PACKAGE)||c->folder_root_count>BD_LOCAL_REMOVE_ROOTS||c->package_root_count>BD_LOCAL_REMOVE_ROOTS)return -1;
    const char *data=c->data_root?c->data_root:"/data/blackwire";if(!strcmp(i->source,data)||lm_under(i->source,data)||!strcmp(i->source,"/data/blackwire")||lm_under(i->source,"/data/blackwire"))return -1;
    if(i->kind==BD_LOCAL_FOLDER){
        static const char *protected_roots[]={"/user","/system","/system_data","/preinst","/dev","/proc","/mnt/sandbox","/mnt/ext0/user"};
        for(unsigned j=0;j<sizeof protected_roots/sizeof protected_roots[0];j++)if(!strcmp(i->source,protected_roots[j])||lm_under(i->source,protected_roots[j]))return -1;
        size_t n=c->folder_root_count?c->folder_root_count:1;for(size_t j=0;j<n;j++){const char *r=c->folder_root_count?c->folder_roots[j]:"/data/homebrew/Games";if(lm_path(r)&&lm_under(i->source,r))return 0;}}
    else {size_t n=c->package_root_count?c->package_root_count:1;for(size_t j=0;j<n;j++){const char *r=c->package_root_count?c->package_roots[j]:(c->apps_root?c->apps_root:"/user/app");char expected[BD_SOURCE_BYTES],base[BD_SOURCE_BYTES];if(lm_path(r)&&!lm_join(base,r,i->title_id)&&!lm_join(expected,base,"app.pkg")&&!strcmp(expected,i->source))return 0;}}
    return -1;
}
int bd_local_removal_capable(const struct bd_local_removal_config *c,const struct bd_item *i){return lm_root_policy(c,i)==0;}
static int lm_add(struct bd_local_removal *p,uint32_t parent,const char *name,const struct stat *st,unsigned directory){
    size_t length=strlen(name);if(!length||length>=sizeof p->nodes[0].leaf||p->count==BD_LOCAL_REMOVE_ENTRIES)return -1;
    if(p->count==p->capacity){uint32_t cap=p->capacity?p->capacity*2:256;if(cap>BD_LOCAL_REMOVE_ENTRIES)cap=BD_LOCAL_REMOVE_ENTRIES;void *next=realloc(p->nodes,(size_t)cap*sizeof *p->nodes);if(!next)return -1;p->nodes=next;p->capacity=cap;}
    struct lm_node *n=&p->nodes[p->count++];memset(n,0,sizeof *n);n->parent=parent;n->directory=directory;n->st=*st;memcpy(n->leaf,name,length+1);if(!directory)p->result.files_total++;return 0;
}
static int lm_inventory(struct bd_local_removal *p,int fd,uint32_t parent,unsigned depth){
    if(depth>LM_DEPTH||lm_base(p))return -1;int duplicate=dup(fd);if(duplicate<0)return -1;DIR *dir=fdopendir(duplicate);if(!dir){close(duplicate);return -1;}rewinddir(dir);int result=-1;struct dirent *e;
    for(;;){errno=0;e=readdir(dir);if(!e){if(errno)goto done;break;}if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        struct stat named,held;if(lm_base(p)||cp_fstatat(fd,e->d_name,&named,AT_SYMLINK_NOFOLLOW)||named.st_dev!=p->source.st.st_dev||
           (!S_ISREG(named.st_mode)&&!S_ISDIR(named.st_mode))||(S_ISREG(named.st_mode)&&named.st_nlink!=1))goto done;
        int child=v2_openat(fd,e->d_name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|(S_ISDIR(named.st_mode)?O_DIRECTORY:0),0);if(child<0)goto done;
        uint32_t id=p->count;int bad=fstat(child,&held)||!lm_same(&named,&held)||lm_add(p,parent,e->d_name,&held,S_ISDIR(held.st_mode));
        if(!bad&&S_ISDIR(held.st_mode))bad=lm_inventory(p,child,id,depth+1);
        struct stat after;if(!bad&&(fstat(child,&after)||cp_fstatat(fd,e->d_name,&held,AT_SYMLINK_NOFOLLOW)||!lm_same(&named,&after)||!lm_same(&after,&held)))bad=1;
        if(close(child))bad=1;if(bad)goto done;
    }result=0;
done:if(closedir(dir))result=-1;return result;
}
static uint64_t lm_hash_name(uint32_t parent,const char *name){uint64_t h=UINT64_C(1469598103934665603)^parent;while(*name)h=(h^(unsigned char)*name++)*UINT64_C(1099511628211);return h;}
static int lm_index(struct bd_local_removal *p){p->slot_count=2;while(p->slot_count<p->count*2)p->slot_count*=2;p->slots=calloc(p->slot_count,sizeof *p->slots);if(!p->slots)return -1;
    for(uint32_t i=1;i<p->count;i++){uint32_t at=(uint32_t)lm_hash_name(p->nodes[i].parent,p->nodes[i].leaf)&(p->slot_count-1);while(p->slots[at])at=(at+1)&(p->slot_count-1);p->slots[at]=i;}return 0;}
static uint32_t lm_find(struct bd_local_removal *p,uint32_t parent,const char *name){uint32_t at=(uint32_t)lm_hash_name(parent,name)&(p->slot_count-1);while(p->slots[at]){uint32_t i=p->slots[at];if(p->nodes[i].parent==parent&&!strcmp(p->nodes[i].leaf,name))return i;at=(at+1)&(p->slot_count-1);}return 0;}
static int lm_verify_tree(struct bd_local_removal *p,int fd,uint32_t parent,unsigned depth,uint32_t *seen){
    if(depth>LM_DEPTH||lm_base(p))return -1;int duplicate=dup(fd);if(duplicate<0)return -1;DIR *d=fdopendir(duplicate);if(!d){close(duplicate);return -1;}rewinddir(d);int result=-1;
    for(;;){errno=0;struct dirent *e=readdir(d);if(!e){if(errno)goto done;break;}if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
        uint32_t id=lm_find(p,parent,e->d_name);struct stat s;if(!id||cp_fstatat(fd,e->d_name,&s,AT_SYMLINK_NOFOLLOW)||!lm_same(&p->nodes[id].st,&s)||lm_base(p))goto done;(*seen)++;
        if(p->nodes[id].directory){int child=v2_openat(fd,e->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK,0);if(child<0)goto done;int bad=lm_verify_tree(p,child,id,depth+1,seen);if(close(child))bad=1;if(bad)goto done;}
    }result=0;
done:if(closedir(d))result=-1;return result;
}
static void lm_u64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static void lm_stat_bytes(uint8_t p[96],const struct stat *s){uint64_t values[]={(uint64_t)s->st_dev,(uint64_t)s->st_ino,(uint64_t)s->st_size,(uint64_t)s->st_mode,(uint64_t)s->st_nlink,(uint64_t)s->st_uid,(uint64_t)s->st_gid,(uint64_t)s->st_blocks,(uint64_t)s->st_mtim.tv_sec,(uint64_t)s->st_mtim.tv_nsec,(uint64_t)s->st_ctim.tv_sec,(uint64_t)s->st_ctim.tv_nsec};for(unsigned i=0;i<12;i++)lm_u64(p+i*8,values[i]);}
static int lm_write_new(struct bd_local_removal *p,const char *name,const void *bytes,size_t size){
    if(strlen(name)>=sizeof p->controls[0].name||p->control_count==8||lm_base(p))return -1;int fd=v2_openat(p->operation.fd,name,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR|O_NONBLOCK,0600);if(fd<0)return -1;int bad=fchmod(fd,0600);size_t at=0;
    while(!bad&&at<size){if(lm_base(p)){bad=1;break;}size_t want=size-at<65536?size-at:65536;ssize_t n=write(fd,(const uint8_t *)bytes+at,want);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>want){bad=1;break;}at+=(size_t)n;}
    if(!bad)bad=fsync(fd);uint8_t buffer[65536];at=0;while(!bad&&at<size){size_t want=size-at<sizeof buffer?size-at:sizeof buffer;ssize_t n=pread(fd,buffer,want,(off_t)at);if(n<0&&errno==EINTR)continue;if(n!=(ssize_t)want||memcmp(buffer,(const uint8_t *)bytes+at,want)){bad=1;break;}at+=want;}
    struct stat held,named;if(!bad)bad=fstat(fd,&held)||cp_fstatat(p->operation.fd,name,&named,AT_SYMLINK_NOFOLLOW)||!lm_same(&held,&named)||held.st_nlink!=1||(held.st_mode&07777)!=0600||(uint64_t)held.st_size!=size||lm_base(p)||fsync(p->operation.fd);
    if(!bad){struct lm_control *control=&p->controls[p->control_count++];control->fd=fd;control->st=held;strcpy(control->name,name);fd=-1;}
    if(fd>=0&&close(fd))bad=1;return bad?-1:0;
}
static int lm_ledger_scope(struct bd_local_removal *p,unsigned created){
    const char *name=".blackwire-local-removals";int flags=O_NOFOLLOW|O_NONBLOCK|(created?O_CREAT|O_EXCL|O_RDWR:O_RDONLY);
    int fd=v2_openat(p->ledger.fd,name,flags,0600);if(fd<0)return -1;int bad=0;
    if(created)bad=fchmod(fd,0600)||write(fd,lm_scope,sizeof lm_scope-1)!=(ssize_t)(sizeof lm_scope-1)||fsync(fd)||fsync(p->ledger.fd);
    char bytes[sizeof lm_scope];struct stat held,named;if(!bad)bad=fstat(fd,&held)||!S_ISREG(held.st_mode)||held.st_nlink!=1||(held.st_mode&07777)!=0600||held.st_uid!=geteuid()||
        held.st_size!=(off_t)(sizeof lm_scope-1)||pread(fd,bytes,sizeof bytes,0)!=(ssize_t)(sizeof lm_scope-1)||memcmp(bytes,lm_scope,sizeof lm_scope-1)||
        cp_fstatat(p->ledger.fd,name,&named,AT_SYMLINK_NOFOLLOW)||!lm_same(&held,&named)||lm_base(p);
    if(close(fd))bad=1;return bad?-1:0;
}
/* An unresolved durable operation also blocks a DIFFERENT nonce after a
 * resident restart. No recovery/adoption or new normal uninstall is inferred. */
static int lm_ledger_idle(struct bd_local_removal *p){
    int duplicate=dup(p->ledger.fd);if(duplicate<0)return -1;DIR *dir=fdopendir(duplicate);if(!dir){close(duplicate);return -1;}rewinddir(dir);int result=-1;
    for(;;){errno=0;struct dirent *entry=readdir(dir);if(!entry){if(errno)goto done;break;}
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,"..")||!strcmp(entry->d_name,".blackwire-local-removals"))continue;
        if(!lm_nonce(entry->d_name)||lm_base(p))goto done;
        struct stat named,held;if(cp_fstatat(p->ledger.fd,entry->d_name,&named,AT_SYMLINK_NOFOLLOW)||!S_ISDIR(named.st_mode)||(named.st_mode&07777)!=0700||named.st_uid!=geteuid())goto done;
        int fd=v2_openat(p->ledger.fd,entry->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0)goto done;
        int bad=fstat(fd,&held)||!lm_same(&named,&held);void *record=NULL,*marker=NULL;size_t rn=0,mn=0;struct stat rs,ms;uint8_t rh[32],mh[32],digest[32];
        if(!bad)bad=lm_file(p,fd,"intent.bin",LM_RECORD_BYTES,&record,&rn,&rs,rh)||rn!=LM_RECORD_BYTES;
        if(!bad){const uint8_t *r=record;cp_sha_bytes(r,LM_RECORD_BYTES-32,digest);
            bad=memcmp(r,"BWLRM001",8)||memcmp(r+16,entry->d_name,33)||memcmp(r+LM_RECORD_BYTES-32,digest,32);}
        if(!bad)bad=lm_file(p,fd,"done",32,&marker,&mn,&ms,mh)||mn!=32||memcmp(marker,digest,32);
        free(record);free(marker);if(!bad)bad=fstat(fd,&held)||!lm_same(&named,&held)||cp_fstatat(p->ledger.fd,entry->d_name,&held,AT_SYMLINK_NOFOLLOW)||!lm_same(&named,&held)||lm_base(p);
        if(close(fd))bad=1;if(bad)goto done;
    }result=0;
done:if(closedir(dir))result=-1;return result;
}
static int lm_save_intent(struct bd_local_removal *p){
    size_t bytes=(size_t)p->count*LM_NODE_BYTES;uint8_t *inventory=calloc(bytes?bytes:1,1);if(!inventory)return -1;
    for(uint32_t i=0;i<p->count;i++){uint8_t *out=inventory+(size_t)i*LM_NODE_BYTES;memcpy(out,p->nodes[i].leaf,256);lm_u64(out+256,((uint64_t)p->nodes[i].directory<<32)|p->nodes[i].parent);lm_stat_bytes(out+264,&p->nodes[i].st);}
    uint8_t record[LM_RECORD_BYTES]={0};memcpy(record,"BWLRM001",8);lm_u64(record+8,LM_RECORD_BYTES);memcpy(record+16,p->request.operation,33);
    lm_u64(record+56,p->request.selected.kind);lm_u64(record+64,p->request.selected.catalog_generation);memcpy(record+72,p->request.selected.id,BD_ID_BYTES);
    memcpy(record+168,p->request.selected.title_id,10);memcpy(record+184,p->request.selected.source,BD_SOURCE_BYTES);memcpy(record+1208,p->request.selected.source_fingerprint,32);
    memcpy(record+1240,p->content,128);memcpy(record+1368,p->quarantine,BD_SOURCE_BYTES);lm_stat_bytes(record+2392,&p->source.st);lm_stat_bytes(record+2488,&p->app.st);
    lm_u64(record+2584,p->count);cp_sha_bytes(inventory,bytes,record+2592);memcpy(record+2624,p->source_param_sha,32);memcpy(record+2656,p->registered_param_sha,32);memcpy(record+2688,p->mount_sha,32);
    cp_sha_bytes(record,LM_RECORD_BYTES-32,record+LM_RECORD_BYTES-32);memcpy(p->record_sha,record+LM_RECORD_BYTES-32,32);
    int bad=lm_write_new(p,"inventory.bin",inventory,bytes)||lm_write_new(p,"intent.bin",record,sizeof record);free(inventory);return bad?-1:0;
}
static int lm_meta(struct bd_local_removal *p,unsigned capture){
    const char *names[]={"sce_sys/param.json","sce_sys/param.json","mount.lnk"};
    int roots[]={p->request.selected.kind==BD_LOCAL_FOLDER?p->source.fd:p->app.fd,p->app.fd,p->app.fd};
    struct stat *st[]={&p->source_param,&p->registered_param,&p->mount};uint8_t *hashes[]={p->source_param_sha,p->registered_param_sha,p->mount_sha};
    unsigned count=p->request.selected.kind==BD_LOCAL_FOLDER?3:2;
    if(lm_anchor_current(&p->app)||(p->source.fd>=0&&lm_anchor_current(&p->source)))return -1;
    for(unsigned i=0;i<count;i++){
        void *body=NULL;size_t size=0;struct stat observed;uint8_t hash[32];
        if(lm_file(p,roots[i],names[i],i==2?BD_SOURCE_BYTES-1:BW_SOURCE_PARAM_LIMIT,&body,&size,&observed,hash))return -1;
        int bad=0;if(i<2){char title[10],content[128];bad=bw_source_param_identity(body,size,title,content)||strcmp(title,p->request.selected.title_id)||strcmp(content,p->content);}
        else{while(size&&(((char *)body)[size-1]=='\r'||((char *)body)[size-1]=='\n'))size--;bad=size!=strlen(p->request.selected.source)||memcmp(body,p->request.selected.source,size);}
        free(body);if(bad||(!capture&&(!lm_same(st[i],&observed)||memcmp(hashes[i],hash,32))))return -1;
        if(capture){*st[i]=observed;memcpy(hashes[i],hash,32);}
    }
    return bw_registration_db_match(p->db_path,p->request.selected.title_id,p->content)==1?lm_base(p):-1;
}
static int lm_pkg_current(struct bd_local_removal *p){
    if(p->request.selected.kind!=BD_INSTALLED_PACKAGE)return 0;
    struct stat held,named;
    return p->package_fd<0||lm_anchor_current(&p->package_parent)||lm_anchor_current(&p->package_title)||
        fstat(p->package_fd,&held)||cp_fstatat(p->package_title.fd,"app.pkg",&named,AT_SYMLINK_NOFOLLOW)||
        !lm_same(&p->source.st,&held)||!lm_same(&held,&named)?-1:0;
}
static int lm_absent_name(int parent,const char *name){struct stat st;return cp_fstatat(parent,name,&st,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT?0:-1;}
static int lm_source_original_absent(struct bd_local_removal *p){
    if(p->request.selected.kind!=BD_LOCAL_FOLDER)return 0;
    return lm_anchor_current(&p->source_parent)||lm_absent_name(p->source_parent.fd,p->original_leaf)?-1:0;
}
static int lm_registration_absent(struct bd_local_removal *p){
    if(lm_base(p)||lm_absent_name(p->apps.fd,p->request.selected.title_id)||
       bw_registration_db_match(p->db_path,p->request.selected.title_id,p->content)!=0||lm_base(p))return -1;
    return 0;
}
static int lm_pkg_absent(struct bd_local_removal *p){
    struct stat title;if(lm_anchor_current(&p->package_parent))return -1;
    if(cp_fstatat(p->package_parent.fd,p->request.selected.title_id,&title,AT_SYMLINK_NOFOLLOW))return errno==ENOENT?0:-1;
    if(!S_ISDIR(title.st_mode))return -1;int fd=v2_openat(p->package_parent.fd,p->request.selected.title_id,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK,0);
    if(fd<0)return -1;struct stat held;int bad=fstat(fd,&held)||!lm_node_same(&title,&held)||lm_absent_name(fd,"app.pkg");if(close(fd))bad=1;return bad?-1:0;
}
static int lm_manual_present(struct bd_local_removal *p){
    if(p->ops.manual_present)return p->ops.manual_present(p->ops.opaque,p->request.selected.source);
    struct bw_sm_connection c={p->config.smp_port,lm_guard,p};struct bw_sm_reply r={0};return bw_sm_post(&c,BW_SM_MANUAL_LIST,p->request.selected.source,&r)?-1:(int)r.present;
}
static int lm_manual_remove(struct bd_local_removal *p){
    if(p->ops.manual_remove)return p->ops.manual_remove(p->ops.opaque,p->request.selected.source);
    struct bw_sm_connection c={p->config.smp_port,lm_guard,p};struct bw_sm_reply r={0};return bw_sm_post(&c,BW_SM_MANUAL_REMOVE,p->request.selected.source,&r)?-1:0;
}
static int lm_uninstall(struct bd_local_removal *p){
    if(p->ops.uninstall)return p->ops.uninstall(p->ops.opaque,p->request.selected.title_id);
    struct bw_sm_connection c={p->config.smp_port,lm_guard,p};struct bw_sm_uninstall_reply r={0};return bw_sm_uninstall(&c,p->request.selected.title_id,&r)?-1:0;
}
static int lm_rmdir_at(int fd,const char *name){
#ifdef BD_LOCAL_REMOVAL_HOST_TEST
    return unlinkat(fd,name,AT_REMOVEDIR);
#else
    return (int)cp_at(SYS_unlinkat,fd,(long)name,AT_REMOVEDIR,0,0);
#endif
}
static int lm_parent(struct bd_local_removal *p,uint32_t id){
    uint32_t chain[LM_DEPTH],count=0,parent=p->nodes[id].parent;while(parent){if(parent>=id||count==LM_DEPTH)return -1;chain[count++]=parent;parent=p->nodes[parent].parent;}
    int fd=dup(p->source.fd);if(fd<0)return -1;
    while(count){uint32_t next=chain[--count];struct stat named,held;int child=-1;
        if(cp_fstatat(fd,p->nodes[next].leaf,&named,AT_SYMLINK_NOFOLLOW)||!lm_node_same(&p->nodes[next].st,&named)||
           (child=v2_openat(fd,p->nodes[next].leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK,0))<0||fstat(child,&held)||!lm_node_same(&named,&held)){if(child>=0)close(child);close(fd);return -1;}
        if(close(fd)){close(child);return -1;}fd=child;
    }return fd;
}
static int lm_delete(struct bd_local_removal *p){
    if(!p->inventory_checked){uint32_t seen=0;if(lm_anchor_current(&p->source)||lm_verify_tree(p,p->source.fd,0,0,&seen)||seen+1!=p->count)return -1;p->inventory_checked=1;
        if(lm_write_new(p,"delete.started",p->record_sha,32))return -1;}
    for(uint32_t remaining=p->count-1;remaining;remaining--){
        uint32_t id=remaining;struct lm_node *node=&p->nodes[id];if(lm_base(p)||lm_source_original_absent(p)||lm_anchor_current(&p->source))return -1;
        int parent=lm_parent(p,id);if(parent<0)return -1;struct stat named,held;int fd=-1;
        int bad=cp_fstatat(parent,node->leaf,&named,AT_SYMLINK_NOFOLLOW)||
            (node->directory?!lm_node_same(&node->st,&named):!lm_same(&node->st,&named));
        if(!bad){fd=v2_openat(parent,node->leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK|(node->directory?O_DIRECTORY:0),0);bad=fd<0||fstat(fd,&held)||!lm_same(&named,&held)||lm_base(p);}
        if(!bad){struct stat last;bad=cp_fstatat(parent,node->leaf,&last,AT_SYMLINK_NOFOLLOW)||!lm_same(&held,&last);}
        if(!bad){bad=node->directory?lm_rmdir_at(parent,node->leaf):cp_unlinkat(parent,node->leaf);if(!bad){p->deleted++;if(!node->directory)p->result.files_removed++;bad=fsync(parent)||lm_absent_name(parent,node->leaf);}}
        if(fd>=0&&close(fd))bad=1;if(close(parent))bad=1;if(bad)return -1;
        (void)lm_out(p,BW_GAME_REMOVE_FILES,"Removing the selected game's files.",0,NULL);
    }
    if(lm_base(p)||lm_source_original_absent(p)||lm_anchor_current(&p->source)||lm_rmdir_at(p->operation.fd,"source")||fsync(p->operation.fd)||lm_absent_name(p->operation.fd,"source"))return -1;
    p->result.source_absent=1;return 0;
}
static int lm_close(struct bd_local_removal *p){
    int bad=0;struct lm_anchor *a[]={&p->source,&p->source_parent,&p->package_title,&p->package_parent,&p->app,&p->operation,&p->ledger,&p->apps,&p->data};
    if(p->package_fd>=0&&close(p->package_fd))bad=1;
    for(unsigned i=0;i<p->control_count;i++)if(close(p->controls[i].fd))bad=1;
    for(unsigned i=0;i<sizeof a/sizeof a[0];i++)if(a[i]->fd>=0){if(close(a[i]->fd))bad=1;a[i]->fd=-1;}
    free(p->nodes);free(p->slots);free(p);return bad?-1:0;
}
int bd_local_removal_destroy(struct bd_local_removal *p){if(!p)return -1;if(p->mutation&&!p->complete)return 1;return lm_close(p);}
static int lm_configuration(struct bd_local_removal *p,const struct bd_local_removal_config *c){
    p->config=*c;p->ops=c->ops;
    const char *paths[]={c->data_root?c->data_root:"/data/blackwire",c->apps_root?c->apps_root:"/user/app",c->database?c->database:"/system_data/priv/mms/app.db"};
    char *out[]={p->data_path,p->apps_path,p->db_path};for(unsigned i=0;i<3;i++){if(!lm_path(paths[i]))return -1;strcpy(out[i],paths[i]);}
    p->config.data_root=p->data_path;p->config.apps_root=p->apps_path;p->config.database=p->db_path;
    for(size_t i=0;i<c->folder_root_count;i++){if(!lm_path(c->folder_roots[i]))return -1;strcpy(p->folder_roots[i],c->folder_roots[i]);p->config.folder_roots[i]=p->folder_roots[i];}
    for(size_t i=0;i<c->package_root_count;i++){if(!lm_path(c->package_roots[i]))return -1;strcpy(p->package_roots[i],c->package_roots[i]);p->config.package_roots[i]=p->package_roots[i];}return 0;
}
int bd_local_removal_begin(const struct bd_local_removal_config *c,const struct bd_local_removal_request *request,
    struct bd_local_removal **owner,struct bd_local_removal_result *out){
    if(out)memset(out,0,sizeof *out);
    if(!c||!request||!owner||*owner||!out||!c->ops.current||!lm_nonce(request->operation)||lm_root_policy(c,&request->selected)||
       !request->confirmation.confirmed||request->confirmation.confirmed!=1||!memchr(request->confirmation.id,0,sizeof request->confirmation.id)||
       !memchr(request->confirmation.title_id,0,10)||strcmp(request->confirmation.id,request->selected.id)||
       strcmp(request->confirmation.title_id,request->selected.title_id)||request->confirmation.kind!=request->selected.kind||
       request->confirmation.catalog_generation!=request->selected.catalog_generation||
       (request->selected.kind==BD_LOCAL_FOLDER&&!c->ops.quarantine_allowed))return 1;
    struct bd_local_removal *p=calloc(1,sizeof *p);if(!p)return -1;
    p->data.fd=p->ledger.fd=p->operation.fd=p->apps.fd=p->app.fd=p->source_parent.fd=p->source.fd=p->package_parent.fd=p->package_title.fd=p->package_fd=-1;
    p->request=*request;int result=1;const char *failure="The selected local game changed; refresh before confirming Uninstall.";char path[BD_SOURCE_BYTES],app[BD_SOURCE_BYTES];
    if(lm_configuration(p,c)||lm_guard(p)||lm_anchor_open(&p->data,p->data_path,0)||lm_anchor_open(&p->apps,p->apps_path,0)||
       lm_collect(p,1)||lm_join(app,p->apps_path,p->request.selected.title_id)||lm_anchor_open(&p->app,app,0))goto done;
    if(request->selected.kind==BD_LOCAL_FOLDER){
        strcpy(path,request->selected.source);char *slash=strrchr(path,'/');if(!slash||!slash[1]||strlen(slash+1)>=sizeof p->original_leaf)goto done;strcpy(p->original_leaf,slash+1);*slash=0;
        if(lm_anchor_open(&p->source_parent,path,0)||lm_anchor_open(&p->source,request->selected.source,0)||
           lm_add(p,UINT32_MAX,"source",&p->source.st,1)||lm_inventory(p,p->source.fd,0,0)||lm_index(p))goto done;
    }else{
        size_t n=c->package_root_count?c->package_root_count:1;unsigned found=0;
        for(size_t i=0;i<n;i++){const char *root=c->package_root_count?c->package_roots[i]:p->apps_path;char base[BD_SOURCE_BYTES],name[BD_SOURCE_BYTES];
            if(!lm_join(base,root,request->selected.title_id)&&!lm_join(name,base,"app.pkg")&&!strcmp(name,request->selected.source)){
                if(lm_anchor_open(&p->package_parent,root,0)||lm_anchor_open(&p->package_title,base,0)||
                   (p->package_fd=v2_openat(p->package_title.fd,"app.pkg",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0))<0||
                   fstat(p->package_fd,&p->source.st)||!S_ISREG(p->source.st.st_mode)||p->source.st.st_nlink!=1||lm_pkg_current(p))goto done;found=1;break;}}
        if(!found)goto done;
    }
    if(lm_meta(p,1)||lm_collect(p,1)||lm_join(path,p->data_path,"local-removals"))goto done;
    int made=v2_mkdirat(p->data.fd,"local-removals",0700);if(made&&errno!=EEXIST)goto done;
    if(lm_anchor_open(&p->ledger,path,1)||lm_ledger_scope(p,made==0)||lm_ledger_idle(p)||v2_mkdirat(p->ledger.fd,request->operation,0700)||lm_join(path,p->ledger.path,request->operation)||
       lm_anchor_open(&p->operation,path,1)||fsync(p->ledger.fd))goto done;
    p->mutation=1;*owner=p;
    if(request->selected.kind==BD_LOCAL_FOLDER){if(lm_join(p->quarantine,p->operation.path,"source")||p->source.st.st_dev!=p->operation.st.st_dev||
        p->ops.quarantine_allowed(p->ops.opaque,p->quarantine)||lm_save_intent(p))goto unknown;
        uint32_t seen=0;if(lm_verify_tree(p,p->source.fd,0,0,&seen)||seen+1!=p->count||lm_meta(p,0)||lm_collect(p,1)||lm_anchor_current(&p->source)||
           lm_anchor_current(&p->source_parent)||lm_absent_name(p->operation.fd,"source")||lm_base(p)||
           p->ops.quarantine_allowed(p->ops.opaque,p->quarantine))goto unknown;
        if(cp_renameat(p->source_parent.fd,p->original_leaf,p->operation.fd,"source"))goto unknown;
        p->result.quarantined=1;strcpy(p->source.path,p->quarantine);
        if(lm_source_original_absent(p)||lm_anchor_current(&p->source)||fsync(p->source_parent.fd)||fsync(p->operation.fd)||
           p->ops.quarantine_allowed(p->ops.opaque,p->quarantine)||lm_write_new(p,"quarantined",p->record_sha,32))goto unknown;
    }else if(lm_save_intent(p))goto unknown;
    if(lm_meta(p,0)||lm_collect(p,0)||lm_pkg_current(p)||lm_base(p)||lm_write_new(p,"uninstall.called",p->record_sha,32)||lm_pkg_current(p))goto unknown;
    p->consumed=p->result.uninstall_attempted=1;
    (void)lm_out(p,BW_GAME_REMOVE_REGISTRATION,"Uninstalling the selected local game.",0,NULL);
    int accepted=lm_uninstall(p);p->result.uninstall_accepted=accepted==0;
    result=0;return lm_out(p,accepted==0?BW_GAME_REMOVE_REGISTRATION:BW_GAME_REMOVE_UNKNOWN,
        accepted==0?"Uninstall requested. Checking its result.":"The uninstall result is not confirmed. Checking without resending.",result,out);
unknown:result=-1;failure="The selected removal needs checking. Its exact files and operation are retained.";
done:
    if(p->mutation){*owner=p;p->failed=!p->consumed;return lm_out(p,BW_GAME_REMOVE_UNKNOWN,failure,-1,out);}
    *out=(struct bd_local_removal_result){.phase=BW_GAME_REMOVE_ERROR,.reason=failure};if(lm_close(p))return -1;return result;
}
int bd_local_removal_poll(struct bd_local_removal *p,struct bd_local_removal_result *out){
    if(!p||!out)return -1;if(p->complete){*out=p->result;return 1;}
    if(p->failed||!p->consumed||lm_base(p))return lm_out(p,BW_GAME_REMOVE_UNKNOWN,"This removal needs checking; no request will be repeated.",-1,out);
    if(lm_registration_absent(p)||lm_registration_absent(p))return lm_out(p,BW_GAME_REMOVE_UNKNOWN,"Waiting to confirm the selected game's system entry was removed.",0,out);
    p->result.registration_confirmed=1;
    if(p->request.selected.kind==BD_INSTALLED_PACKAGE){
        if(lm_pkg_absent(p)||lm_pkg_absent(p))return lm_out(p,BW_GAME_REMOVE_UNKNOWN,"The system entry is gone; checking package removal.",0,out);p->result.source_absent=1;p->result.manual_absent=1;
    }else{
        if(lm_source_original_absent(p)||lm_anchor_current(&p->source)||p->ops.quarantine_allowed(p->ops.opaque,p->quarantine))return lm_out(p,BW_GAME_REMOVE_UNKNOWN,"The retained game folder changed; its remaining files were preserved.",-1,out);
        int manual=lm_manual_present(p);if(manual<0)return lm_out(p,BW_GAME_REMOVE_UNKNOWN,"Checking the selected game's library entry.",0,out);
        if(manual&&!p->manual_consumed){if(lm_write_new(p,"manual.called",p->record_sha,32))return lm_out(p,BW_GAME_REMOVE_UNKNOWN,"The library removal could not be recorded.",-1,out);
            p->manual_consumed=p->result.manual_remove_attempted=1;(void)lm_manual_remove(p);}
        if(lm_manual_present(p)!=0||lm_manual_present(p)!=0)return lm_out(p,BW_GAME_REMOVE_UNKNOWN,"Waiting to confirm the selected library entry was removed.",0,out);
        p->result.manual_absent=1;
        if(lm_registration_absent(p)||lm_delete(p)){p->failed=1;return lm_out(p,BW_GAME_REMOVE_UNKNOWN,"Removal stopped because a file or operation changed. Remaining files were preserved.",-1,out);}
    }
    if(lm_registration_absent(p)||lm_registration_absent(p)||lm_base(p)||lm_write_new(p,"done",p->record_sha,32))return lm_out(p,BW_GAME_REMOVE_UNKNOWN,"Checking the final removal result.",-1,out);
    p->complete=1;p->result.library_removed=1;return lm_out(p,BW_GAME_REMOVE_DONE,"The selected game was uninstalled.",1,out);
}
