/* SPDX-License-Identifier: GPL-3.0-or-later
 * Private durable library state only. No PC I/O, registration or game deletion. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "native_removed_sources.h"
#include "../streamreader/copy_fs.h"
#include "../streamreader/copy_sha256.h"
#include <dirent.h>
#include <pthread.h>

#define RM_WIRE 2048u
#define RM_COMMIT 128u
#define RM_RESTORE 2304u
#define RM_PATH 2048u
static pthread_mutex_t rm_lock=PTHREAD_MUTEX_INITIALIZER;
static const char rm_marker[]="Blackwire removed sources v1\n";
struct rm_context {int data,ledger;char root[RM_PATH],path[RM_PATH];struct stat ds,ls;};
static int rm_result(struct bw_removed_result *r,enum bw_removed_state s,const char *why,int rc){if(r){r->state=s;r->reason=why;}return rc;}
static int rm_node(const struct stat *a,const struct stat *b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid;}
static int rm_same(const struct stat *a,const struct stat *b){return rm_node(a,b)&&a->st_nlink==b->st_nlink&&a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static int rm_nonzero(const unsigned char *p,size_t n){unsigned a=0;for(size_t i=0;i<n;i++)a|=p[i];return a!=0;}
static int rm_nonce(const char *p){if(!p||strnlen(p,33)!=32)return 0;unsigned n=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",p[i]))return 0;n|=p[i]!='0';}return n!=0;}
static int rm_identity(const struct bw_provider_binding *p){
    if(!p||(p->kind!=BW_PROVIDER_FOLDER&&p->kind!=BW_PROVIDER_CONTAINER)||p->policy!=BW_PROVIDER_POLICY_V1||
       strnlen(p->binding.source,sizeof p->binding.source)>=sizeof p->binding.source||
       bw_manifest_validate_path(p->binding.source,strlen(p->binding.source))||
       strnlen(p->binding.title,10)!=9||memcmp(p->binding.title,"PPSA",4)||
       !strcmp(p->binding.title,"PPSA99171")||!strcmp(p->binding.title,"PPSA99170")||
       strnlen(p->binding.game,75)!=74||memcmp(p->binding.game,p->binding.title,9)||p->binding.game[9]!='_'||!rm_nonzero(p->binding.sha256,32))return -1;
    for(unsigned i=4;i<9;i++)if(p->binding.title[i]<'0'||p->binding.title[i]>'9')return -1;
    char hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",p->binding.sha256[i]);return strcmp(hex,p->binding.game+10)?-1:0;
}
static int rm_identity_same(const struct bw_provider_binding *a,const struct bw_provider_binding *b){return !rm_identity(a)&&!rm_identity(b)&&a->kind==b->kind&&a->policy==b->policy&&!strcmp(a->binding.source,b->binding.source)&&!strcmp(a->binding.game,b->binding.game)&&!strcmp(a->binding.title,b->binding.title)&&!memcmp(a->binding.sha256,b->binding.sha256,32);}
static void rm_put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static uint32_t rm_get32(const unsigned char *p){uint32_t v=0;for(unsigned i=0;i<4;i++)v|=(uint32_t)p[i]<<(8*i);return v;}
static int rm_encode(const struct bw_removed_source *p,unsigned char out[RM_WIRE]){
    if(!p||rm_identity(&p->identity)||!rm_nonce(p->operation)||!rm_nonzero(p->prepared_sha256,32))return -1;
    memset(out,0,RM_WIRE);memcpy(out,"BWRMV001",8);rm_put32(out+8,1);rm_put32(out+12,RM_WIRE);
    rm_put32(out+16,p->identity.kind);rm_put32(out+20,p->identity.policy);memcpy(out+24,p->identity.binding.sha256,32);
    memcpy(out+56,p->prepared_sha256,32);memcpy(out+88,p->operation,33);memcpy(out+128,p->identity.binding.title,10);
    memcpy(out+144,p->identity.binding.game,75);memcpy(out+224,p->identity.binding.source,strlen(p->identity.binding.source)+1);
    cp_sha_bytes(out,RM_WIRE-32,out+RM_WIRE-32);return 0;
}
static int rm_decode(struct bw_removed_source *p,const unsigned char in[RM_WIRE]){
    if(memcmp(in,"BWRMV001",8)||rm_get32(in+8)!=1||rm_get32(in+12)!=RM_WIRE||
       !memchr(in+88,0,33)||!memchr(in+128,0,10)||!memchr(in+144,0,75)||!memchr(in+224,0,sizeof p->identity.binding.source))return -1;
    memset(p,0,sizeof *p);p->identity.kind=rm_get32(in+16);p->identity.policy=rm_get32(in+20);
    memcpy(p->identity.binding.sha256,in+24,32);memcpy(p->prepared_sha256,in+56,32);memcpy(p->operation,in+88,33);
    memcpy(p->identity.binding.title,in+128,10);memcpy(p->identity.binding.game,in+144,75);memcpy(p->identity.binding.source,in+224,sizeof p->identity.binding.source);
    unsigned char encoded[RM_WIRE];return rm_encode(p,encoded)||memcmp(encoded,in,RM_WIRE)?-1:0;
}
static int rm_leaf(char out[128],const struct bw_provider_binding *p,const char *suffix){int n=snprintf(out,128,"%s.%s",p->binding.game,suffix);return n<0||n>=128?-1:0;}
static void rm_init(struct rm_context *c){memset(c,0,sizeof *c);c->data=c->ledger=-1;}
static int rm_close(struct rm_context *c){int bad=0;if(c->ledger>=0&&close(c->ledger))bad=1;if(c->data>=0&&close(c->data))bad=1;c->ledger=c->data=-1;return bad?-1:0;}
static int rm_anchor(struct rm_context *c){
    char real[PATH_MAX];struct stat a,b;
    if(c->data<0||fstat(c->data,&a)||lstat(c->root,&b)||!rm_node(&c->ds,&a)||!rm_node(&a,&b)||!realpath(c->root,real)||strcmp(real,c->root))return -1;
    return c->ledger>=0&&(fstat(c->ledger,&a)||lstat(c->path,&b)||!rm_node(&c->ls,&a)||!rm_node(&a,&b)||!realpath(c->path,real)||strcmp(real,c->path))?-1:0;
}
static int rm_current(struct rm_context *c,const struct bw_removed_ops *o){return !o||!o->current||o->current(o->opaque)||rm_anchor(c)?-1:0;}
static int rm_absent(struct rm_context *c,const struct bw_removed_ops *o,const struct bw_removed_source *s){return rm_current(c,o)||!o->absent||o->absent(o->opaque,s)||rm_current(c,o)||o->absent(o->opaque,s)||rm_current(c,o)?-1:0;}
static int rm_read(struct rm_context *c,const char *name,void *out,size_t n,struct stat *identity){
    int fd=v2_openat(c->ledger,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0)return errno==ENOENT?1:-1;
    struct stat first,after,named;int bad=fstat(fd,&first)||!S_ISREG(first.st_mode)||first.st_nlink!=1||first.st_uid!=geteuid()||(first.st_mode&07777)!=0600||first.st_size<0||(uint64_t)first.st_size!=n;size_t at=0;
    while(!bad&&at<n){ssize_t got=pread(fd,(unsigned char *)out+at,n-at,(off_t)at);if(got<0&&errno==EINTR)continue;if(got<=0||(size_t)got>n-at){bad=1;break;}at+=(size_t)got;}
    if(!bad&&(fstat(fd,&after)||cp_fstatat(c->ledger,name,&named,AT_SYMLINK_NOFOLLOW)||!rm_same(&first,&after)||!rm_same(&after,&named)||rm_anchor(c)))bad=1;
    if(!bad&&identity)*identity=first;if(close(fd))bad=1;return bad?-1:0;
}
static int rm_write(struct rm_context *c,const char *name,const void *body,size_t n,const struct bw_removed_ops *o){
    if(rm_current(c,o))return -1;int fd=v2_openat(c->ledger,name,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR|O_NONBLOCK,0600);if(fd<0)return -1;
    int bad=fchmod(fd,0600);size_t at=0;
    while(!bad&&at<n){if(rm_current(c,o)){bad=1;break;}ssize_t got=write(fd,(const unsigned char *)body+at,n-at);if(got<0&&errno==EINTR)continue;if(got<=0||(size_t)got>n-at){bad=1;break;}at+=(size_t)got;}
    if(!bad&&(fsync(fd)||rm_current(c,o)))bad=1;if(close(fd))bad=1;
    void *copy=malloc(n);if(!copy)bad=1;if(!bad&&(rm_read(c,name,copy,n,NULL)||memcmp(copy,body,n)||fsync(c->ledger)||rm_current(c,o)))bad=1;free(copy);return bad?-1:0;
}
static int rm_open(struct rm_context *c,const char *root,int create,const struct bw_removed_ops *o){
    char real[PATH_MAX];struct stat named;if(!root||strnlen(root,RM_PATH)>=RM_PATH||root[0]!='/'||!realpath(root,real)||strcmp(root,real)||lstat(root,&named)||!S_ISDIR(named.st_mode))return -1;
    strcpy(c->root,root);int n=snprintf(c->path,sizeof c->path,"%s/removed-sources",root);if(n<0||(size_t)n>=sizeof c->path)return -1;
    c->data=open(root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);if(c->data<0||fstat(c->data,&c->ds)||!rm_node(&named,&c->ds)||rm_anchor(c))return -1;
    int made=0;if(lstat(c->path,&named)){if(errno!=ENOENT)return -1;if(!create)return 1;if(rm_current(c,o)||v2_mkdirat(c->data,"removed-sources",0700))return -1;made=1;}
    c->ledger=v2_openat(c->data,"removed-sources",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK,0);
    if(c->ledger<0||fstat(c->ledger,&c->ls)||!S_ISDIR(c->ls.st_mode)||c->ls.st_uid!=geteuid()||(c->ls.st_mode&07777)!=0700||rm_anchor(c))return -1;
    if(made){if(rm_write(c,".blackwire-removed-sources",rm_marker,sizeof rm_marker-1,o)||fsync(c->data))return -1;}
    else{char marker[sizeof rm_marker-1];if(rm_read(c,".blackwire-removed-sources",marker,sizeof marker,NULL)||memcmp(marker,rm_marker,sizeof marker))return -1;}return 0;
}
static void rm_complete(unsigned char out[RM_COMMIT],const unsigned char record[RM_WIRE]){memset(out,0,RM_COMMIT);memcpy(out,"BWRMC001",8);memcpy(out+16,record+RM_WIRE-32,32);memcpy(out+48,record+88,33);cp_sha_bytes(out,96,out+96);}
static int rm_load(struct rm_context *c,const struct bw_provider_binding *id,struct bw_removed_source *out,unsigned char wire[RM_WIRE],struct stat *rs,struct stat *cs){
    char leaf[128];unsigned char expected[RM_COMMIT],got[RM_COMMIT];if(rm_leaf(leaf,id,"removed"))return -1;int a=rm_read(c,leaf,wire,RM_WIRE,rs);
    if(rm_leaf(leaf,id,"complete"))return -1;int b=rm_read(c,leaf,got,sizeof got,cs);if(a==1&&b==1)return 1;
    if(a||b||rm_decode(out,wire)||!rm_identity_same(id,&out->identity))return -1;rm_complete(expected,wire);return memcmp(got,expected,sizeof got)?-1:0;
}
int bw_removed_lookup(const char *root,const struct bw_provider_binding *id,struct bw_removed_source *out,struct bw_removed_result *res){
    if(out)memset(out,0,sizeof *out);if(res)memset(res,0,sizeof *res);if(!out||!res||rm_identity(id)||pthread_mutex_lock(&rm_lock))return -1;
    struct rm_context c;rm_init(&c);unsigned char wire[RM_WIRE];int rc=-1,opened=rm_open(&c,root,0,NULL);
    if(opened==1)rc=rm_result(res,BW_REMOVED_VISIBLE,"no_removed_sources_ledger",0);
    else if(!opened){int loaded=rm_load(&c,id,out,wire,NULL,NULL);if(loaded==1)rc=rm_result(res,BW_REMOVED_VISIBLE,"source_not_removed",0);else if(!loaded)rc=rm_result(res,BW_REMOVED_COMMITTED,"durable_removed_source",0);}
    if(rm_close(&c))rc=-1;if(rc<0){memset(out,0,sizeof *out);rm_result(res,BW_REMOVED_UNKNOWN,"removed_source_evidence_unknown",rc);}pthread_mutex_unlock(&rm_lock);return rc;
}
int bw_removed_commit(const char *root,const struct bw_removed_source *s,const struct bw_removed_ops *o,struct bw_removed_result *res){
    if(res)memset(res,0,sizeof *res);unsigned char wire[RM_WIRE],complete[RM_COMMIT];if(!res||rm_encode(s,wire)||!o||!o->current||!o->absent||pthread_mutex_lock(&rm_lock))return -1;
    struct rm_context c;rm_init(&c);int rc=-1;char leaf[128];
    if(rm_open(&c,root,1,o)||rm_absent(&c,o,s))goto done;
    struct bw_removed_source prior;unsigned char old[RM_WIRE];int loaded=rm_load(&c,&s->identity,&prior,old,NULL,NULL);
    if(!loaded){if(!memcmp(old,wire,sizeof old))rc=rm_result(res,BW_REMOVED_COMMITTED,"same_removed_record_already_committed",0);goto done;}if(loaded<0)goto done;
    if(rm_leaf(leaf,&s->identity,"removed")||rm_write(&c,leaf,wire,sizeof wire,o)||rm_absent(&c,o,s))goto done;
    rm_complete(complete,wire);if(rm_leaf(leaf,&s->identity,"complete")||rm_write(&c,leaf,complete,sizeof complete,o)||rm_absent(&c,o,s))goto done;
    rc=rm_result(res,BW_REMOVED_COMMITTED,"removed_source_committed_after_verified_absence",0);
done:if(rm_close(&c))rc=-1;if(rc<0)rm_result(res,BW_REMOVED_UNKNOWN,"removed_source_commit_uncertain_no_replay",rc);pthread_mutex_unlock(&rm_lock);return rc;
}
static int rm_unlink(struct rm_context *c,const char *name,const struct stat *expected,const struct bw_removed_ops *o){struct stat st;return rm_current(c,o)||cp_fstatat(c->ledger,name,&st,AT_SYMLINK_NOFOLLOW)||!rm_same(expected,&st)||cp_unlinkat(c->ledger,name)||fsync(c->ledger)||rm_current(c,o)?-1:0;}
int bw_removed_restore(const char *root,const struct bw_provider_binding *id,const char operation[33],const struct bw_removed_ops *o,struct bw_removed_result *res){
    if(res)memset(res,0,sizeof *res);if(!res||rm_identity(id)||!rm_nonce(operation)||!o||!o->current||!o->absent||pthread_mutex_lock(&rm_lock))return -1;
    struct rm_context c;rm_init(&c);int rc=-1,opened=rm_open(&c,root,0,NULL);char leaf[128],intent_name[128];unsigned char wire[RM_WIRE],intent[RM_RESTORE],prior[RM_RESTORE];struct bw_removed_source s={0};struct stat rs,cs;
    if(opened==1){rc=rm_result(res,BW_REMOVED_VISIBLE,"no_removed_source_to_restore",0);goto done;}if(opened||rm_current(&c,o))goto done;
    int n=snprintf(intent_name,sizeof intent_name,"%s.%s.restore",id->binding.game,operation);if(n<0||(size_t)n>=sizeof intent_name)goto done;
    int pending=rm_read(&c,intent_name,prior,sizeof prior,NULL);if(pending<0)goto done;
    int loaded=rm_load(&c,id,&s,wire,&rs,&cs);
    if(!pending){
        unsigned char hash[32];cp_sha_bytes(prior,RM_RESTORE-32,hash);
        if(memcmp(prior,"BWRST001",8)||memcmp(prior+RM_RESTORE-32,hash,32)||memcmp(prior+2056,operation,33)||rm_decode(&s,prior+8)||!rm_identity_same(id,&s.identity)||rm_absent(&c,o,&s))goto done;
        if(loaded==1)rc=rm_result(res,BW_REMOVED_RESTORED,"restore_observed_after_prior_one_shot_intent",0);
        goto done; /* Existing intent never grants another unlink. */
    }
    if(loaded==1){rc=rm_result(res,BW_REMOVED_VISIBLE,"source_is_already_visible",0);goto done;}if(loaded||rm_absent(&c,o,&s))goto done;
    memset(intent,0,sizeof intent);memcpy(intent,"BWRST001",8);memcpy(intent+8,wire,sizeof wire);memcpy(intent+2056,operation,33);cp_sha_bytes(intent,RM_RESTORE-32,intent+RM_RESTORE-32);
    if(rm_write(&c,intent_name,intent,sizeof intent,o)||rm_absent(&c,o,&s)||rm_leaf(leaf,id,"complete")||rm_unlink(&c,leaf,&cs,o)||rm_leaf(leaf,id,"removed")||rm_unlink(&c,leaf,&rs,o))goto done;
    if(rm_load(&c,id,&s,wire,NULL,NULL)!=1||rm_absent(&c,o,&s))goto done;rc=rm_result(res,BW_REMOVED_RESTORED,"library_visibility_restored_no_game_files_created",0);
done:if(rm_close(&c))rc=-1;if(rc<0)rm_result(res,BW_REMOVED_UNKNOWN,"restore_outcome_unknown_no_replay",rc);pthread_mutex_unlock(&rm_lock);return rc;
}
int bw_removed_list(const char *root,struct bw_removed_source *out,size_t capacity,size_t *count){
    if(count)*count=0;if(!out||!count||!capacity||capacity>BW_REMOVED_LIMIT||pthread_mutex_lock(&rm_lock))return -1;memset(out,0,capacity*sizeof *out);
    struct rm_context c;rm_init(&c);DIR *dir=NULL;int rc=-1,opened=rm_open(&c,root,0,NULL);size_t used=0,scanned=0;struct stat before,after;
    if(opened==1){rc=0;goto done;}if(opened||fstat(c.ledger,&before))goto done;dir=opendir(c.path);if(!dir)goto done;
    struct dirent *entry;errno=0;while((entry=readdir(dir))){if(++scanned>4096)goto done;const char *name=entry->d_name;
        if(!strcmp(name,".")||!strcmp(name,"..")||!strcmp(name,".blackwire-removed-sources")){errno=0;continue;}
        size_t n=strlen(name);if(n==82&&!strcmp(name+74,".removed")){
            unsigned char wire[RM_WIRE];struct bw_removed_source s;if(used==capacity||rm_read(&c,name,wire,sizeof wire,NULL)||rm_decode(&s,wire))goto done;
            char expected[128];if(rm_leaf(expected,&s.identity,"removed")||strcmp(name,expected)||rm_load(&c,&s.identity,&out[used],wire,NULL,NULL))goto done;used++;
        }else if(n==83&&!strcmp(name+74,".complete")){
            char pair[128];unsigned char wire[RM_WIRE];struct bw_removed_source s;
            memcpy(pair,name,74);strcpy(pair+74,".removed");
            if(rm_read(&c,pair,wire,sizeof wire,NULL)||rm_decode(&s,wire)||rm_load(&c,&s.identity,&s,wire,NULL,NULL))goto done;
        }else if(n==115&&name[74]=='.'&&!strcmp(name+107,".restore")){
            unsigned char intent[RM_RESTORE],hash[32];struct bw_removed_source s;char expected[128],operation[33];
            if(rm_read(&c,name,intent,sizeof intent,NULL)||memcmp(intent,"BWRST001",8)||rm_decode(&s,intent+8))goto done;
            memcpy(operation,name+75,32);operation[32]=0;cp_sha_bytes(intent,RM_RESTORE-32,hash);
            int length=snprintf(expected,sizeof expected,"%s.%s.restore",s.identity.binding.game,operation);
            if(length<0||length>=(int)sizeof expected||strcmp(name,expected)||!rm_nonce(operation)||memcmp(intent+2056,operation,33)||memcmp(hash,intent+RM_RESTORE-32,32))goto done;
        }else goto done;
        errno=0;
    }
    if(errno||fstat(c.ledger,&after)||!rm_same(&before,&after)||rm_anchor(&c))goto done;rc=0;*count=used;
done:if(dir&&closedir(dir))rc=-1;if(rm_close(&c))rc=-1;if(rc){memset(out,0,capacity*sizeof *out);*count=0;}pthread_mutex_unlock(&rm_lock);return rc;
}
