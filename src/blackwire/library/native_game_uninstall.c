/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exact ordinary prepared-copy deletion only. No PC I/O or process control. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#include "native_game_uninstall.h"
#include "native_registration.h"
#include "preload_bundle.h"
#include "native_json.h"
#include "source_param.h"
#include "source_companions.h"
#include "send_diagnostic.h"
#include "../streamreader/copy_fs.h"
#include "../streamreader/copy_sha256.h"
#include "../streamreader/preparation_receipt.h"
#include "../streamreader/startup_mode.h"
#include <dirent.h>
#include <pthread.h>
#include <sys/file.h>

#define GU_INTENT 4096u
#define GU_PHASE 128u
#define GU_LIMIT 262144u
#define GU_POOL (64u*1024u*1024u)
static pthread_mutex_t gu_lock=PTHREAD_MUTEX_INITIALIZER;
static const char gu_scope[]="Blackwire game removals v1\n";
static const char gu_prepared[]="Blackwire native preparations v1\n";
static const char gu_send_lock[]="Blackwire native send lock v1\n";
struct gu_node {char *path;struct stat st;unsigned state;};
struct gu_plan {
    const struct bw_game_uninstall_request *r;
    struct bw_game_uninstall_result *out;
    struct bw_removed_source removed;
    int data,games,scope,apps,root,state,ledger,send_lock;
    struct stat ids[7],lock_identity;
    unsigned lock_pinned;
    struct prepare_v2 anchor[7];
    unsigned char receipt[384],*map,*digests;
    unsigned char *original_map,*original_digests,*companions;
    size_t map_size,digest_size,original_map_size,original_digest_size,companion_size;
    struct stat original_identity[2];
    struct bw_companion_receipt *projection;
    struct bw_manifest manifest;
    struct {const char *path;uint32_t id;} *rows;
    struct gu_node *nodes;size_t count,capacity,pool;
    unsigned char intent[GU_INTENT],inventory[32];
    char content[128],intent_leaf[128],registration_leaf[128],files_leaf[128],done_leaf[128];
    unsigned files_total,root_removed,state_removed,continuation,identity_diagnostic,release_unknown,preparation_only;
    uint64_t current_calls;
    const char *checking;
};
static uint32_t gu_u32(const unsigned char *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t gu_u64(const unsigned char *p){return gu_u32(p)|(uint64_t)gu_u32(p+4)<<32;}
static void gu_put32(unsigned char *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static void gu_put64(unsigned char *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(v>>(8*i));}
static int gu_nonce(const char *p){if(!p||strnlen(p,33)!=32)return 0;unsigned n=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",p[i]))return 0;n|=p[i]!='0';}return n!=0;}
static int gu_nonzero(const unsigned char *p,size_t n){unsigned v=0;while(n--)v|=*p++;return v!=0;}
static int gu_node_same(const struct stat *a,const struct stat *b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid;}
static int gu_same(const struct stat *a,const struct stat *b){return gu_node_same(a,b)&&a->st_nlink==b->st_nlink&&a->st_size==b->st_size&&a->st_blocks==b->st_blocks&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
/* Diagnostic bits follow the existing exact identity predicate, not a new policy:
 * dev ino mode uid gid nlink size blocks mtime-sec mtime-nsec ctime-sec ctime-nsec.
 * Log only the first failed check; relative names are copied, bounded and escaped.
 * This best-effort private log never supplies ownership or retry authority. */
#define GU_STAT_NODE_MASK 31u
#define GU_STAT_FULL_MASK 4095u
static unsigned gu_stat_difference(const struct stat *a,const struct stat *b){
    unsigned mask=0;
    if(a->st_dev!=b->st_dev)mask|=1u;if(a->st_ino!=b->st_ino)mask|=2u;
    if(a->st_mode!=b->st_mode)mask|=4u;if(a->st_uid!=b->st_uid)mask|=8u;
    if(a->st_gid!=b->st_gid)mask|=16u;if(a->st_nlink!=b->st_nlink)mask|=32u;
    if(a->st_size!=b->st_size)mask|=64u;if(a->st_blocks!=b->st_blocks)mask|=128u;
    if(a->st_mtim.tv_sec!=b->st_mtim.tv_sec)mask|=256u;if(a->st_mtim.tv_nsec!=b->st_mtim.tv_nsec)mask|=512u;
    if(a->st_ctim.tv_sec!=b->st_ctim.tv_sec)mask|=1024u;if(a->st_ctim.tv_nsec!=b->st_ctim.tv_nsec)mask|=2048u;return mask;
}
static void gu_identity_diag(struct gu_plan *p,const char *check,unsigned area,const char *path,int error,
                             unsigned compared,const struct stat *expected,const struct stat *observed){
    if(p->identity_diagnostic)return;p->identity_diagnostic=1;
    char escaped[257];size_t used=0;unsigned truncated=0;
    const unsigned char *name=(const unsigned char *)(path?path:"");
    for(;*name;name++){
        unsigned c=*name;size_t need=c>=33&&c<=126&&c!='\\'&&c!='"'?1:4;
        if(need>sizeof escaped-1-used){truncated=1;break;}
        if(need==1)escaped[used++]=(char)c;
        else {static const char hex[]="0123456789abcdef";escaped[used++]='\\';escaped[used++]='x';escaped[used++]=hex[c>>4];escaped[used++]=hex[c&15];}
    }escaped[used]=0;
    struct stat zero={0};if(!expected)expected=&zero;if(!observed)observed=&zero;
    unsigned changed=compared?gu_stat_difference(expected,observed)&compared:0;
    bw_send_log("uninstall_identity","operation=%.32s currentCall=%"PRIu64" check=%s area=%u node=\"%s\" truncated=%u errno=%d compared=%u changed=%u stat=%u expected=[%llu,%llu,%llu,%llu,%llu,%llu,%lld,%lld,%lld,%ld,%lld,%ld] observed=[%llu,%llu,%llu,%llu,%llu,%llu,%lld,%lld,%lld,%ld,%lld,%ld]",
        p->r?p->r->operation:"",p->current_calls,check,area,escaped,truncated,error,compared,changed,compared!=0,
        (unsigned long long)expected->st_dev,(unsigned long long)expected->st_ino,(unsigned long long)expected->st_mode,
        (unsigned long long)expected->st_uid,(unsigned long long)expected->st_gid,(unsigned long long)expected->st_nlink,
        (long long)expected->st_size,(long long)expected->st_blocks,(long long)expected->st_mtim.tv_sec,expected->st_mtim.tv_nsec,
        (long long)expected->st_ctim.tv_sec,expected->st_ctim.tv_nsec,
        (unsigned long long)observed->st_dev,(unsigned long long)observed->st_ino,(unsigned long long)observed->st_mode,
        (unsigned long long)observed->st_uid,(unsigned long long)observed->st_gid,(unsigned long long)observed->st_nlink,
        (long long)observed->st_size,(long long)observed->st_blocks,(long long)observed->st_mtim.tv_sec,observed->st_mtim.tv_nsec,
        (long long)observed->st_ctim.tv_sec,observed->st_ctim.tv_nsec);
}
static int gu_finish(struct gu_plan *p,enum bw_game_uninstall_phase phase,const char *why,int result){p->out->phase=phase;p->out->reason=why;return result;}
static void gu_progress(struct gu_plan *p,enum bw_game_uninstall_phase phase,const char *message){p->out->phase=phase;if(p->r->ops.observe){struct bw_game_uninstall_progress v={phase,p->out->files_removed,p->files_total,message};p->r->ops.observe(p->r->ops.opaque,&v);}}
static void gu_init(struct gu_plan *p){p->data=p->games=p->scope=p->apps=p->root=p->state=p->ledger=p->send_lock=-1;}
static int gu_provider(const struct bw_provider_binding *identity){
    const struct bw_source_binding *b=&identity->binding;
    if((identity->kind!=BW_PROVIDER_FOLDER&&identity->kind!=BW_PROVIDER_CONTAINER)||identity->policy!=BW_PROVIDER_POLICY_V1||
       strnlen(b->title,10)!=9||memcmp(b->title,"PPSA",4)||!strcmp(b->title,"PPSA99171")||!strcmp(b->title,"PPSA99170")||
       strnlen(b->game,75)!=74||memcmp(b->game,b->title,9)||b->game[9]!='_'||strnlen(b->source,sizeof b->source)>=sizeof b->source||bw_manifest_validate_path(b->source,strlen(b->source)))return -1;
    for(unsigned i=4;i<9;i++)if(b->title[i]<'0'||b->title[i]>'9')return -1;
    char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",b->sha256[i]);return !gu_nonzero(b->sha256,32)||strcmp(hash,b->game+10)?-1:0;
}
static int gu_prepared_policy(const struct bw_native_prepared *p){
    if(!p->prepared_policy)return 0;
    return p->prepared_policy!=3||!memchr(p->game,0,sizeof p->game)||!memchr(p->prepared_component,0,sizeof p->prepared_component)||
        strcmp(p->prepared_component,p->game)||!gu_nonzero(p->provenance_sha256,32)||
        p->provenance_size<BW_COMPANION_HEADER_BYTES||p->provenance_size>BW_COMPANION_RECEIPT_LIMIT?-1:0;
}
static int gu_identity(const struct bw_game_uninstall_request *r){return gu_provider(&r->identity)||r->prepared.state!=BW_PREPARED_VALID||gu_prepared_policy(&r->prepared)||
    (r->legacy_source_fakelib2&&(r->legacy_source_fakelib2!=1||r->prepared.prepared_policy||r->reader_size||gu_nonzero(r->reader_sha256,32)))||
    !gu_nonce(r->operation)||!gu_nonce(r->prepared.run)||!gu_nonzero(r->prepared.record_sha256,32)||strnlen(r->confirmed_title,10)!=9||strcmp(r->confirmed_title,r->identity.binding.title)||strcmp(r->prepared.game,r->identity.binding.game)?-1:0;}
/* Do not retry a failed close: ownership may already have been consumed.
 * Keep uncertainty separate from destructive intent and latch it per plan. */
static int gu_release(struct gu_plan *p,int fd){int rc=close(fd);if(rc)p->release_unknown=1;return rc;}
static int gu_dir_release(struct gu_plan *p,DIR *dir){int rc=closedir(dir);if(rc)p->release_unknown=1;return rc;}
static int gu_parent(struct gu_plan *p,const struct prepare_v2 *cfg,int rootfd,const char *relative,int create,
                     char leaf[256],char parent_relative[1024]) {
  char walk[1024]; size_t used=0;
  if (v2_copy(walk,sizeof walk,relative)) return v2_fail("A destination path exceeds its manifest limit");
  int fd=dup(rootfd); if (fd<0) return v2_fail("Cannot anchor a destination directory");
  parent_relative[0]=0;
  for (char *part=walk;;) {
    char *slash=strchr(part,'/');
    if (!slash) {
      if (v2_copy(leaf,256,part) || !v2_directory_current(cfg,rootfd,fd,parent_relative)) {
        gu_release(p,fd); return v2_fail("A destination directory changed or is not canonical");
      }
      return fd;
    }
    *slash=0;
    if (!v2_directory_current(cfg,rootfd,fd,parent_relative)) {
      gu_release(p,fd); return v2_fail("A destination directory changed during traversal");
    }
    int next=v2_openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
    if (next<0 && errno==ENOENT) {
      if (!create) { gu_release(p,fd); return -2; }
      if (v2_mkdirat(fd,part,0755)) { gu_release(p,fd); return v2_fail("A missing destination directory appeared or could not be created"); }
      next=v2_openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
      if (next>=0 && fchmod(next,0755)) { gu_release(p,next); gu_release(p,fd); return v2_fail("Cannot set access on a new destination directory"); }
    }
    if (next<0) { gu_release(p,fd); return v2_fail("A destination directory is linked or is not a directory"); }
    size_t length=strlen(part);
    if (used+length+(used?1:0)>=1024) { gu_release(p,next); gu_release(p,fd); return v2_fail("A destination directory path exceeds its limit"); }
    if (used) parent_relative[used++]='/';
    memcpy(parent_relative+used,part,length+1); used+=length;
    if (!v2_directory_current(cfg,rootfd,next,parent_relative)) {
      gu_release(p,next); gu_release(p,fd); return v2_fail("A destination directory has an unsafe identity");
    }
    gu_release(p,fd); fd=next; part=slash+1;
  }
}
static int gu_open_directory(struct gu_plan *p,const char *path,struct prepare_v2 *a,struct stat *id,int owned){
    if(!path||strnlen(path,2048)>=2048||path[0]!='/')return -1;memset(a,0,sizeof *a);strcpy(a->root,path);
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
    if(!v2_root_current(a,fd)||fstat(fd,id)||!S_ISDIR(id->st_mode)||
       (owned&&(id->st_uid!=geteuid()||(id->st_mode&0022)))){gu_release(p,fd);return -1;}return fd;
}
static int gu_open_root(struct gu_plan *p,const char *path,struct prepare_v2 *a,struct stat *id){return gu_open_directory(p,path,a,id,1);}
/* Sony's shared apps parent is not a Blackwire-owned private namespace (0770
 * is a normal observed mode). Keep its canonical held/named identity and all
 * subsequent title/source/param/link/DB checks; never chmod or adopt it. */
static int gu_open_apps(struct gu_plan *p,const char *path,struct prepare_v2 *a,struct stat *id){return gu_open_directory(p,path,a,id,0);}
static int gu_open_child(struct gu_plan *p,int parent,unsigned anchor,const char *name,unsigned target){
    char path[2048];int n=snprintf(path,sizeof path,"%s/%s",p->anchor[anchor].root,name);if(n<0||(size_t)n>=sizeof path)return -1;
    int fd=gu_open_root(p,path,p->anchor+target,p->ids+target);struct stat named;
    if(fd>=0&&(cp_fstatat(parent,name,&named,AT_SYMLINK_NOFOLLOW)||!gu_node_same(&named,p->ids+target)||p->ids[target].st_dev!=p->ids[0].st_dev)){gu_release(p,fd);fd=-1;}return fd;
}
static int gu_anchor(struct gu_plan *p){
    int fds[]={p->data,p->games,p->scope,p->apps,p->root,p->state,p->ledger};
    for(unsigned i=0;i<7;i++){if(fds[i]<0)continue;if((i==4&&p->root_removed)||(i==5&&p->state_removed))continue;struct stat st;
        if(fstat(fds[i],&st)){gu_identity_diag(p,"anchor_fstat",i,"",errno,0,NULL,NULL);return -1;}
        if(!gu_node_same(p->ids+i,&st)){gu_identity_diag(p,"anchor_held_changed",i,"",0,GU_STAT_NODE_MASK,p->ids+i,&st);return -1;}
        if(!v2_root_current(p->anchor+i,fds[i])){gu_identity_diag(p,"anchor_named_current",i,"",0,0,NULL,NULL);return -1;}}
    if(p->lock_pinned){struct stat st,named;
        if(fstat(p->send_lock,&st)){gu_identity_diag(p,"lock_fstat",0,".blackwire-send.lock",errno,0,NULL,NULL);return -1;}
        if(cp_fstatat(p->data,".blackwire-send.lock",&named,AT_SYMLINK_NOFOLLOW)){gu_identity_diag(p,"lock_named_stat",0,".blackwire-send.lock",errno,0,NULL,NULL);return -1;}
        if(!gu_same(&p->lock_identity,&st)){gu_identity_diag(p,"lock_held_changed",0,".blackwire-send.lock",0,GU_STAT_FULL_MASK,&p->lock_identity,&st);return -1;}
        if(!gu_same(&st,&named)){gu_identity_diag(p,"lock_named_changed",0,".blackwire-send.lock",0,GU_STAT_FULL_MASK,&st,&named);return -1;}}return 0;
}
static int gu_current(struct gu_plan *p){
    p->current_calls++;if(p->release_unknown||!p->r->ops.current||p->r->ops.current(p->r->ops.opaque)){gu_identity_diag(p,"authority_current",0,"",0,0,NULL,NULL);return -1;}
    return gu_anchor(p)?-1:0;
}
static int gu_read(struct gu_plan *p,int dir,const char *leaf,size_t maximum,void **out,size_t *size,struct stat *identity){
    *out=NULL;*size=0;int fd=v2_openat(dir,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0)return errno==ENOENT?1:-1;
    struct stat st,after,named;int bad=fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_uid!=geteuid()||st.st_size<0||(uint64_t)st.st_size>maximum;unsigned char *body=NULL;size_t at=0;
    if(!bad){body=malloc((size_t)st.st_size+1);if(!body)bad=1;}
    while(!bad&&at<(size_t)st.st_size){ssize_t n=pread(fd,body+at,(size_t)st.st_size-at,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>(size_t)st.st_size-at){bad=1;break;}at+=(size_t)n;}
    if(!bad&&(fstat(fd,&after)||cp_fstatat(dir,leaf,&named,AT_SYMLINK_NOFOLLOW)||!gu_same(&st,&after)||!gu_same(&after,&named)))bad=1;
    if(gu_release(p,fd))bad=1;if(bad){free(body);return -1;}body[at]=0;*out=body;*size=at;if(identity)*identity=st;return 0;
}
static int gu_exact(struct gu_plan *p,int dir,const char *leaf,const void *body,size_t size,mode_t mode){void *got=NULL;size_t n=0;struct stat st;int bad=gu_read(p,dir,leaf,size,&got,&n,&st)||n!=size||(n==size&&memcmp(got,body,n))||(st.st_mode&07777)!=mode;free(got);return bad?-1:0;}
static int gu_absent_at(int dir,const char *leaf){struct stat st;return cp_fstatat(dir,leaf,&st,AT_SYMLINK_NOFOLLOW)&&errno==ENOENT?0:-1;}
static int gu_title_absent(struct gu_plan *p){return gu_current(p)||gu_absent_at(p->apps,p->r->identity.binding.title)||bw_registration_db_match(p->r->environment.database,p->r->identity.binding.title,p->content)!=0||gu_current(p)?-1:0;}
static int gu_all_absent(struct gu_plan *p){return gu_title_absent(p)||gu_absent_at(p->games,p->r->identity.binding.game)||gu_absent_at(p->scope,p->r->identity.binding.game)||gu_title_absent(p)?-1:0;}
static int gu_removed_current(void *v){return gu_current(v);}
static int gu_removed_absent(void *v,const struct bw_removed_source *s){struct gu_plan *p=v;return memcmp(s->prepared_sha256,p->removed.prepared_sha256,32)||strcmp(s->operation,p->removed.operation)||gu_all_absent(p)?-1:0;}
static int gu_hash(struct gu_plan *p,int fd,uint64_t size,unsigned char out[32]){
    unsigned char *buffer=malloc(256u*1024u);if(!buffer)return -1;struct cp_sha hash;cp_sha_init(&hash);uint64_t at=0;int bad=0;
    while(at<size){if(gu_current(p)){bad=1;break;}size_t want=(size_t)(size-at>256u*1024u?256u*1024u:size-at);ssize_t n=pread(fd,buffer,want,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>want){bad=1;break;}cp_sha_update(&hash,buffer,(size_t)n);at+=(size_t)n;}
    if(!bad)cp_sha_final(&hash,out);free(buffer);return bad?-1:0;
}
static int gu_receipt(struct gu_plan *p,const unsigned char *r,size_t size,int current){
    unsigned char hash[32];char run[33];if(size!=384)return -1;cp_sha_bytes(r,352,hash);memcpy(run,r+16,32);run[32]=0;
    if(memcmp(r,"BWNAT001",8)||gu_u32(r+8)!=1||gu_u32(r+12)!=384||!gu_nonce(run)||memcmp(r+48,p->r->identity.binding.sha256,32)||memcmp(hash,r+352,32)||
       gu_u64(r+304)!=(uint64_t)p->ids[4].st_dev||gu_u64(r+312)!=(uint64_t)p->ids[4].st_ino||gu_u64(r+320)!=(uint64_t)p->ids[5].st_dev||gu_u64(r+328)!=(uint64_t)p->ids[5].st_ino)return -1;
    for(unsigned i=336;i<352;i++)if(r[i])return -1;
    if(current){cp_sha_bytes(r,size,hash);if(memcmp(hash,p->r->prepared.record_sha256,32)||memcmp(r+16,p->r->prepared.run,32))return -1;}return 0;
}
static int gu_binding_json(struct gu_plan *p,struct bw_json *json){const struct bw_source_binding *b=&p->r->identity.binding;char hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",b->sha256[i]);
    bw_json_text(json,"{\"version\":1,\"source\":");bw_json_string(json,b->source);bw_json_text(json,",\"game\":");bw_json_string(json,b->game);bw_json_text(json,",\"titleId\":");bw_json_string(json,b->title);bw_json_text(json,",\"bindingSha256\":");bw_json_string(json,hex);
    if(p->r->identity.kind==BW_PROVIDER_CONTAINER)bw_json_text(json,",\"sourceKind\":\"container\",\"sourcePolicy\":\"unsigned-contiguous-d32-pfs-v1\"");
    bw_json_text(json,"}\n");return json->bad?-1:0;}
static int gu_row_compare(const void *a,const void *b){const typeof(((struct gu_plan *)0)->rows[0]) *x=a,*y=b;return strcmp(x->path,y->path);}
static int gu_find(struct gu_plan *p,const char *name,uint32_t *id,int prefix){size_t lo=0,hi=p->manifest.count;while(lo<hi){size_t m=(lo+hi)/2;if(strcmp(p->rows[m].path,name)<0)lo=m+1;else hi=m;}if(lo==p->manifest.count)return 0;
    if(prefix){size_t n=strlen(name);return !strncmp(p->rows[lo].path,name,n)&&p->rows[lo].path[n]=='/';}if(strcmp(p->rows[lo].path,name))return 0;*id=p->rows[lo].id;return 1;}
static int gu_add(struct gu_plan *p,const char *path,const struct stat *st,unsigned state){
    if(p->count==GU_LIMIT||strlen(path)+1>GU_POOL-p->pool)return -1;if(p->count==p->capacity){size_t n=p->capacity?p->capacity*2:128;if(n>GU_LIMIT)n=GU_LIMIT;void *memory=realloc(p->nodes,n*sizeof *p->nodes);if(!memory)return -1;p->nodes=memory;p->capacity=n;}
    char *copy=strdup(path);if(!copy)return -1;p->nodes[p->count++]=(struct gu_node){copy,*st,state};p->pool+=strlen(copy)+1;if(S_ISREG(st->st_mode))p->files_total++;return 0;
}
static int gu_control(struct gu_plan *p,const char *name,const unsigned char *body,size_t size){
    if(size>4096||memchr(body,0,size))return -1;char run[33]={0},title[10]={0},game[75]={0},hash[17]={0},again[4097];uint64_t boot=0;unsigned count=0;int consumed=0;
    int n=sscanf((const char *)body,"version=2\nrun_id=%32[0-9a-f]\ntitle_id=%9[A-Z0-9]\ngame=%74[^\n]\neboot_size=%"SCNu64"\nmanifest_count=%u\nmanifest_hash=%16[0-9a-f]\n%n",run,title,game,&boot,&count,hash,&consumed);
    if(n!=6||!gu_nonce(run)||strcmp(title,p->r->identity.binding.title)||strcmp(game,p->r->identity.binding.game)||!count||count>BW_MANIFEST_MAX_FILES||strlen(hash)!=16)return -1;
    const char *tail=strncmp(name,"register-v2.conf",16)?"":"mode=verify\n";
    int length=snprintf(again,sizeof again,"version=2\nrun_id=%s\ntitle_id=%s\ngame=%s\neboot_size=%"PRIu64"\nmanifest_count=%u\nmanifest_hash=%s\n%s",run,title,game,boot,count,hash,tail);
    return consumed<0||length<0||(size_t)length!=size||memcmp(body,again,size)?-1:0;
}
static int gu_storage_json(struct gu_plan *p,const unsigned char *body,size_t size){
    char run[33]={0},title[10]={0},game[149]={0},map[65]={0},generation[65]={0},expected[149],again[1600];uint64_t allocated=0,local=0,remote=0;unsigned entries=0;int consumed=0;
    int count=sscanf((const char *)body,"{\"version\":2,\"preparationRunId\":\"%32[0-9a-f]\",\"titleId\":\"%9[A-Z0-9]\",\"gameHex\":\"%148[0-9a-f]\",\"manifestSha256\":\"%64[0-9a-f]\",\"generationSha256\":\"%64[0-9a-f]\",\"allocatedBytes\":%"SCNu64",\"entries\":%u,\"localBytes\":%"SCNu64",\"remoteBytes\":%"SCNu64",\"allocationVerified\":true}\n%n",run,title,game,map,generation,&allocated,&entries,&local,&remote,&consumed);
    for(unsigned i=0;i<74;i++)snprintf(expected+2*i,3,"%02x",(unsigned char)p->r->identity.binding.game[i]);
    if(count!=9||!gu_nonce(run)||strcmp(title,p->r->identity.binding.title)||strcmp(game,expected)||strlen(map)!=64||strlen(generation)!=64||!entries||entries>GU_LIMIT)return -1;
    int n=snprintf(again,sizeof again,"{\"version\":2,\"preparationRunId\":\"%s\",\"titleId\":\"%s\",\"gameHex\":\"%s\",\"manifestSha256\":\"%s\",\"generationSha256\":\"%s\",\"allocatedBytes\":%"PRIu64",\"entries\":%u,\"localBytes\":%"PRIu64",\"remoteBytes\":%"PRIu64",\"allocationVerified\":true}\n",run,title,game,map,generation,allocated,entries,local,remote);
    return consumed<0||n<0||(size_t)n!=size||memcmp(again,body,size)?-1:0;
}
static int gu_state_file(struct gu_plan *p,const char *name,const unsigned char *body,size_t size,mode_t mode){
    int previous=0;const char *base=name;char leaf[128];const char *suffix=strstr(name,".previous.");if(suffix){if(!gu_nonce(suffix+10)||(size_t)(suffix-name)>=sizeof leaf)return -1;memcpy(leaf,name,(size_t)(suffix-name));leaf[suffix-name]=0;base=leaf;previous=1;}
    if(!strcmp(base,"prepared.bin"))return mode==(previous?0644:0600)?gu_receipt(p,body,size,!previous):-1;
    if(!strcmp(base,"stream.conf")||!strcmp(base,"register-v2.conf"))return mode==(previous?0600:0644)?gu_control(p,base,body,size):-1;
    char receipt[64];snprintf(receipt,sizeof receipt,"preparation-%s.json",p->r->identity.binding.title);
    if(!strcmp(base,receipt))return mode==(previous?0600:0644)?gu_storage_json(p,body,size):-1;
    if(!strcmp(base,"source.json")&&!previous){struct bw_json json={.maximum=4096};int bad=mode!=0644||gu_binding_json(p,&json)||json.used!=size||(json.used==size&&memcmp(body,json.bytes,size));free(json.bytes);return bad?-1:0;}return -1;
}
/* Projection authority is entirely local: the physical BWNAT001 table pins
 * seal a LOCAL receipt row, which in turn pins both ORIGINAL source tables.
 * Never infer this refinement from a directory name or an arbitrary receipt. */
static int gu_projection(struct gu_plan *p){
    if(p->r->prepared.prepared_policy!=3)return 0;
    uint32_t id=0;if(!gu_find(p,BW_COMPANION_RECEIPT_PATH,&id,0))return -1;
    struct bw_manifest_record row;struct stat st;void *body=NULL;size_t size=0;
    if(bw_manifest_get(&p->manifest,id,&row)||row.flags!=BW_MANIFEST_LOCAL||
       row.size!=p->r->prepared.provenance_size||row.size<BW_COMPANION_HEADER_BYTES||row.size>BW_COMPANION_RECEIPT_LIMIT||
       gu_current(p)||gu_read(p,p->root,BW_COMPANION_RECEIPT_PATH,BW_COMPANION_RECEIPT_LIMIT,&body,&size,&st))return -1;
    p->companions=body;p->companion_size=size;unsigned char hash[32];cp_sha_bytes(body,size,hash);
    if(size!=row.size||(st.st_mode&07777)!=bw_startup_mode(BW_COMPANION_RECEIPT_PATH)||
       memcmp(hash,p->digests+56u+32u*id,32)||memcmp(hash,p->r->prepared.provenance_sha256,32)||gu_current(p))return -1;
    const uint64_t map_size=gu_u64(p->companions+216),digest_size=gu_u64(p->companions+224);
    const size_t map_limit=BW_MANIFEST_HEADER_SIZE+(size_t)BW_MANIFEST_MAX_FILES*BW_MANIFEST_RECORD_SIZE+BW_MANIFEST_MAX_PATH_BYTES;
    if(map_size<BW_MANIFEST_HEADER_SIZE||map_size>map_limit||digest_size<56u||digest_size>56u+32u*(size_t)BW_MANIFEST_MAX_FILES)return -1;
    if(gu_read(p,p->state,BW_COMPANION_ORIGINAL_MAP_NAME,(size_t)map_size,&body,&size,p->original_identity))return -1;
    p->original_map=body;p->original_map_size=size;cp_sha_bytes(body,size,hash);
    if(size!=map_size||(p->original_identity[0].st_mode&07777)!=0600||memcmp(hash,p->companions+152,32)||gu_current(p))return -1;
    if(gu_read(p,p->state,BW_COMPANION_ORIGINAL_DIGEST_NAME,(size_t)digest_size,&body,&size,p->original_identity+1))return -1;
    p->original_digests=body;p->original_digest_size=size;cp_sha_bytes(body,size,hash);
    if(size!=digest_size||(p->original_identity[1].st_mode&07777)!=0600||memcmp(hash,p->companions+184,32)||gu_current(p))return -1;
    struct bw_companion_binding binding={.provider=&p->r->identity,.prepare_run=p->r->prepared.run,
        .source_generation_sha256=p->receipt+80,.roots={gu_u64(p->receipt+304),gu_u64(p->receipt+312),gu_u64(p->receipt+320),gu_u64(p->receipt+328)}};
    struct bw_companion_source_tables original={p->original_map,p->original_map_size,p->original_digests,p->original_digest_size};
    struct bw_companion_tables physical={.map=p->map,.map_size=p->map_size,.digests=p->digests,.digest_size=p->digest_size,.receipt=p->companions,.receipt_size=p->companion_size};
    struct bw_companion_prepared_seal seal;memcpy(seal.map_sha256,p->receipt+112,32);memcpy(seal.digests_sha256,p->receipt+144,32);
    return bw_companion_receipt_open(&p->projection,&binding,&original,&physical,&seal,NULL)||gu_current(p)?-1:0;
}
static int gu_original_state_file(struct gu_plan *p,const char *name,int fd,const struct stat *st){
    if(!p->projection)return 1;
    unsigned id=!strcmp(name,BW_COMPANION_ORIGINAL_MAP_NAME)?0u:!strcmp(name,BW_COMPANION_ORIGINAL_DIGEST_NAME)?1u:2u;
    if(id==2)return 1;
    const size_t size=id?p->original_digest_size:p->original_map_size;unsigned char hash[32];
    return (st->st_mode&07777)!=0600||(uint64_t)st->st_size!=size||!gu_same(st,p->original_identity+id)||
        gu_hash(p,fd,size,hash)||memcmp(hash,p->companions+(id?184:152),32)?-1:0;
}
static int gu_pin_hex(const char *text,unsigned char out[32]){for(unsigned i=0;i<32;i++){unsigned value;if(sscanf(text+2*i,"%2x",&value)!=1)return -1;out[i]=(unsigned char)value;}return 0;}
static int gu_special(struct gu_plan *p,const char *name,int fd,const struct stat *st){
    const char *leaves[]={"blackwire.files","blackwire.generation.digests","ampr_emu.index"};
    for(unsigned i=0;i<3;i++)if(!strcmp(name,leaves[i])){unsigned char hash[32];return (st->st_mode&07777)!=0644||(uint64_t)st->st_size!=gu_u64(p->receipt+280+8*i)||gu_hash(p,fd,(uint64_t)st->st_size,hash)||memcmp(hash,p->receipt+112+32*i,32)?-1:0;}
    if(!strcmp(name,"blackwire.generation.owner")){struct bw_json json={.maximum=4096};int bad=gu_binding_json(p,&json)||gu_exact(p,p->root,name,json.bytes,json.used,0644);free(json.bytes);return bad?-1:0;}
    const char *prefix="blackwire.generation.";size_t n=strlen(prefix);if(strlen(name)==n+36&&!memcmp(name,prefix,n)&&!strcmp(name+n+32,".bin")){
        unsigned char body[128],expected[128];char run[33];memcpy(run,name+n,32);run[32]=0;
        if((st->st_mode&07777)!=0644||st->st_size!=128||!gu_nonce(run)||pread(fd,body,128,0)!=128||bw_generation_bytes(expected,run,p->r->identity.binding.title,p->r->identity.binding.game,body+96)||memcmp(body,expected,128))return -1;
        if(!strcmp(run,p->r->prepared.run)){unsigned char hash[32];cp_sha_bytes(body,128,hash);if(memcmp(hash,p->receipt+208,32))return -1;}return 0;
    }
    if(!strncmp(name,"fakelib2/",9)){
        if(p->r->legacy_source_fakelib2)return -1;
        /* Projected source-owned rows were checked FIRST against the sealed
         * physical map. Only the generated, trusted selected reader may be an
         * extra. A legacy fixed-name module is no authority in this policy. */
        if(p->projection&&strcmp(name,"fakelib2/libSceAmpr.sprx"))return -1;
        static const struct {const char *leaf;uint64_t size;const char *hash;} pins[]={
            {"libSceAppContent.sprx",32982,"53e485e22970c693ab70c05bd78b8f627442ed41ca9a49692b38615046494a6b"},
            {"libSceGameUpdate.sprx",9126,"1e31ddbe4809b7c5ec117e9b7e1227befe757c31d1f5f73e6c7d5a3d2d238973"},
            {"libSceNpEntitlementAccess.sprx",43286,"3f1826f20e174ff0f7aaa8d7f3f7e386dfcca1136c9e4c0e72a49e041c0b1488"},
            {"libSceAmpr.sprx.orig",423350,"69e6c4d5e4f5fb83c9e01815db5861c4c75734acbf4595cafa50d4c218116d1a"}};
        unsigned char expected[32],got[32];uint64_t size=0;if(!strcmp(name+9,"libSceAmpr.sprx")){size=p->r->reader_size;memcpy(expected,p->r->reader_sha256,32);if(!size||!gu_nonzero(expected,32))return -1;}
        else if(!strcmp(name+9,"libScePlayGo.sprx")){
            const struct bw_preload_playgo_pin *pin=st->st_size<0?NULL:bw_preload_playgo_pin((uint64_t)st->st_size);
            if(!pin||gu_pin_hex(pin->hex,expected))return -1;size=pin->size;
        }else for(unsigned i=0;i<4;i++)if(!strcmp(name+9,pins[i].leaf)){size=pins[i].size;if(gu_pin_hex(pins[i].hash,expected))return -1;break;}
        return !size||(st->st_mode&07777)!=0755||(uint64_t)st->st_size!=size||gu_hash(p,fd,size,got)||memcmp(got,expected,32)?-1:0;
    }return -1;
}
static int gu_walk(struct gu_plan *p,int directory,const char *prefix,unsigned state,unsigned depth){
    if(depth>48||gu_current(p))return -1;struct stat before,after;if(fstat(directory,&before))return -1;
    int cursor=v2_openat(directory,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);if(cursor<0)return -1;DIR *dir=fdopendir(cursor);if(!dir){gu_release(p,cursor);return -1;}int bad=0;struct dirent *entry;
    for(;;){errno=0;entry=readdir(dir);if(!entry){if(errno)bad=1;break;}if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
        char path[1024];int length=snprintf(path,sizeof path,"%s%s%s",prefix,*prefix?"/":"",entry->d_name);struct stat named,held,named_after;int fd=-1;
        if(length<0||(size_t)length>=sizeof path||bw_manifest_validate_path(path,(size_t)length)||gu_current(p)||cp_fstatat(directory,entry->d_name,&named,AT_SYMLINK_NOFOLLOW)||named.st_uid!=geteuid()||named.st_dev!=p->ids[0].st_dev||(!S_ISDIR(named.st_mode)&&!S_ISREG(named.st_mode))){bad=1;break;}
        if(S_ISDIR(named.st_mode)){
            uint32_t unused=0;if(state||(named.st_mode&07777)!=0755||(!gu_find(p,path,&unused,1)&&strcmp(path,"fakelib2"))){bad=1;break;}
            fd=v2_openat(directory,entry->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK,0);
            if(fd<0||fstat(fd,&held)||!gu_same(&named,&held)||gu_add(p,path,&held,state)||gu_walk(p,fd,path,state,depth+1))bad=1;
        }else{
            if(named.st_nlink!=1||named.st_size<0){bad=1;break;}fd=v2_openat(directory,entry->d_name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
            if(fd<0||fstat(fd,&held)||!gu_same(&named,&held)){bad=1;goto row_done;}
            if(state){int original=gu_original_state_file(p,path,fd,&held);if(original<0)bad=1;
                else if(original){void *body=NULL;size_t size=0;struct stat st;bad=gu_read(p,directory,entry->d_name,4096,&body,&size,&st)||!gu_same(&held,&st)||gu_state_file(p,path,body,size,held.st_mode&07777);free(body);}}
            else {uint32_t id=0;if(gu_find(p,path,&id,0)){struct bw_manifest_record row;unsigned char hash[32];bad=bw_manifest_get(&p->manifest,id,&row)||row.size!=(uint64_t)held.st_size||(held.st_mode&07777)!=bw_startup_mode(row.path);
                    if(!bad&&row.flags==BW_MANIFEST_STREAM)bad=held.st_blocks!=0;
                    else if(!bad)bad=gu_hash(p,fd,row.size,hash)||memcmp(hash,p->digests+56u+32u*id,32);
                }else bad=gu_special(p,path,fd,&held);}
            if(!bad&&gu_add(p,path,&held,state))bad=1;
        }
row_done:if(!bad&&(fstat(fd,&held)||cp_fstatat(directory,entry->d_name,&named_after,AT_SYMLINK_NOFOLLOW)||!gu_same(&named,&held)||!gu_same(&held,&named_after)))bad=1;
        if(fd>=0&&gu_release(p,fd))bad=1;if(bad)break;
    }
    if(gu_dir_release(p,dir))bad=1;if(fstat(directory,&after)||!gu_same(&before,&after))bad=1;return bad?-1:0;
}
static int gu_node_compare(const void *a,const void *b){const struct gu_node *x=a,*y=b;int n=(int)x->state-(int)y->state;return n?n:strcmp(x->path,y->path);}
static void gu_stat_wire(unsigned char out[112],const struct stat *st){memset(out,0,112);uint64_t values[]={(uint64_t)st->st_dev,(uint64_t)st->st_ino,(uint64_t)st->st_mode,(uint64_t)st->st_uid,(uint64_t)st->st_gid,(uint64_t)st->st_nlink,(uint64_t)st->st_size,(uint64_t)st->st_blocks,(uint64_t)st->st_mtim.tv_sec,(uint64_t)st->st_mtim.tv_nsec,(uint64_t)st->st_ctim.tv_sec,(uint64_t)st->st_ctim.tv_nsec};for(unsigned i=0;i<12;i++)gu_put64(out+8*i,values[i]);}
static void gu_inventory(struct gu_plan *p){qsort(p->nodes,p->count,sizeof *p->nodes,gu_node_compare);struct cp_sha h;cp_sha_init(&h);unsigned char st[112];
    for(unsigned i=4;i<=5;i++){gu_stat_wire(st,p->ids+i);cp_sha_update(&h,st,sizeof st);}for(size_t i=0;i<p->count;i++){unsigned char state=(unsigned char)p->nodes[i].state;cp_sha_update(&h,&state,1);cp_sha_update(&h,p->nodes[i].path,strlen(p->nodes[i].path)+1);gu_stat_wire(st,&p->nodes[i].st);cp_sha_update(&h,st,sizeof st);}cp_sha_final(&h,p->inventory);}
static int gu_named(struct gu_plan *p,const struct gu_node *node,int fine,int *parent_out,char leaf[256]){
    unsigned a=node->state?5:4;int root=node->state?p->state:p->root;char relative[1024];int parent=gu_parent(p,p->anchor+a,root,node->path,0,leaf,relative);
    if(parent<0){gu_identity_diag(p,"node_parent_open",a,node->path,errno,0,NULL,NULL);return -1;}
    int bad=gu_current(p);if(!bad){struct stat st;
        if(cp_fstatat(parent,leaf,&st,AT_SYMLINK_NOFOLLOW)){gu_identity_diag(p,"node_named_stat",a,node->path,errno,0,NULL,NULL);bad=1;}
        else if(fine?!gu_same(&node->st,&st):!gu_node_same(&node->st,&st)){
            gu_identity_diag(p,"node_named_changed",a,node->path,0,fine?GU_STAT_FULL_MASK:GU_STAT_NODE_MASK,&node->st,&st);bad=1;}}
    if(!bad&&*relative){size_t low=0,high=p->count;struct gu_node key={.path=relative,.state=node->state};while(low<high){size_t m=(low+high)/2;if(gu_node_compare(p->nodes+m,&key)<0)low=m+1;else high=m;}
        struct stat st;
        if(low==p->count||gu_node_compare(p->nodes+low,&key)||!S_ISDIR(p->nodes[low].st.st_mode)){
            gu_identity_diag(p,"node_parent_inventory",a,node->path,0,0,NULL,NULL);bad=1;}
        else if(fstat(parent,&st)){gu_identity_diag(p,"node_parent_fstat",a,node->path,errno,0,NULL,NULL);bad=1;}
        else if(!gu_node_same(&p->nodes[low].st,&st)){
            gu_identity_diag(p,"node_parent_changed",a,node->path,0,GU_STAT_NODE_MASK,&p->nodes[low].st,&st);bad=1;}}
    if(bad){if(gu_release(p,parent))return -1;return -1;}*parent_out=parent;return 0;
}
static int gu_revalidate(struct gu_plan *p){
    if(gu_current(p))return -1;for(unsigned a=4;a<=5;a++){struct stat st;
        if(fstat(a==4?p->root:p->state,&st)){gu_identity_diag(p,"root_fstat",a,"",errno,0,NULL,NULL);return -1;}
        if(!gu_same(p->ids+a,&st)){gu_identity_diag(p,"root_changed",a,"",0,GU_STAT_FULL_MASK,p->ids+a,&st);return -1;}}
    for(size_t i=0;i<p->count;i++){int parent=-1;char leaf[256];if(gu_named(p,p->nodes+i,1,&parent,leaf))return -1;
        if(gu_release(p,parent)){gu_identity_diag(p,"node_parent_close",p->nodes[i].state?5:4,p->nodes[i].path,errno,0,NULL,NULL);return -1;}}
    return gu_current(p);
}
static int gu_marker(struct gu_plan *p,const char *name,const void *bytes,size_t size){if(gu_current(p))return -1;int fd=v2_openat(p->ledger,name,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR|O_NONBLOCK,0600);if(fd<0)return -1;int bad=fchmod(fd,0600);size_t at=0;
    while(!bad&&at<size){if(gu_current(p)){bad=1;break;}ssize_t n=write(fd,(const unsigned char *)bytes+at,size-at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>size-at){bad=1;break;}at+=(size_t)n;}
    if(!bad&&(fsync(fd)||gu_current(p)))bad=1;if(gu_release(p,fd))bad=1;if(!bad&&(fsync(p->ledger)||gu_exact(p,p->ledger,name,bytes,size,0600)||gu_current(p)))bad=1;return bad?-1:0;}
static void gu_phase(unsigned char out[GU_PHASE],const struct gu_plan *p,const char *magic){memset(out,0,GU_PHASE);memcpy(out,magic,8);memcpy(out+16,p->intent+GU_INTENT-32,32);memcpy(out+48,p->r->operation,33);cp_sha_bytes(out,96,out+96);}
static int gu_phase_read(struct gu_plan *p,const char *name,const char *magic){unsigned char expected[GU_PHASE];void *got=NULL;size_t size=0;struct stat st;gu_phase(expected,p,magic);int loaded=gu_read(p,p->ledger,name,GU_PHASE,&got,&size,&st);
    if(!loaded&&(size!=GU_PHASE||(st.st_mode&07777)!=0600||memcmp(got,expected,GU_PHASE)))loaded=-1;free(got);return loaded;}
static int gu_ledger(struct gu_plan *p,int create){
    const char *name=p->preparation_only?"preparation-removals":"game-removals";
    const char *marker=p->preparation_only?".blackwire-preparation-removals":".blackwire-game-removals";
    const char *scope=p->preparation_only?"Blackwire preparation removals v1\n":gu_scope;
    struct stat st;if(cp_fstatat(p->data,name,&st,AT_SYMLINK_NOFOLLOW)){if(!create||errno!=ENOENT||gu_current(p)||v2_mkdirat(p->data,name,0700))return -1;
        p->ledger=gu_open_child(p,p->data,0,name,6);if(p->ledger<0||(p->ids[6].st_mode&07777)!=0700||gu_marker(p,marker,scope,strlen(scope))||fsync(p->data))return -1;
    }else {p->ledger=gu_open_child(p,p->data,0,name,6);if(p->ledger<0||(p->ids[6].st_mode&07777)!=0700||gu_exact(p,p->ledger,marker,scope,strlen(scope),0600))return -1;}return 0;
}
static int gu_journal_load(struct gu_plan *p){
    const struct bw_source_binding *b=&p->r->identity.binding;int n=snprintf(p->intent_leaf,sizeof p->intent_leaf,"%s.%s.intent",b->game,p->r->operation);if(n<0||(size_t)n>=sizeof p->intent_leaf)return -1;
    snprintf(p->registration_leaf,sizeof p->registration_leaf,"%s.%s.registration",b->game,p->r->operation);snprintf(p->files_leaf,sizeof p->files_leaf,"%s.%s.files",b->game,p->r->operation);snprintf(p->done_leaf,sizeof p->done_leaf,"%s.%s.done",b->game,p->r->operation);
    void *body=NULL;size_t size=0;struct stat st;int loaded=gu_read(p,p->ledger,p->intent_leaf,GU_INTENT,&body,&size,&st);
    if(!loaded){unsigned char hash[32],paths_hash[32];const unsigned char *v=body;
        struct cp_sha h;cp_sha_init(&h);const char *paths[]={p->r->environment.data_root,p->r->environment.apps_root,p->r->environment.database};for(unsigned i=0;i<3;i++)cp_sha_update(&h,paths[i],strlen(paths[i])+1);cp_sha_final(&h,paths_hash);
        if(size!=GU_INTENT)loaded=-1;
        else {cp_sha_bytes(body,GU_INTENT-32,hash);if((st.st_mode&07777)!=0600||memcmp(v,p->preparation_only?"BWGPR001":"BWGUR001",8)||gu_u32(v+8)!=1||gu_u32(v+12)!=GU_INTENT||gu_u32(v+16)!=(uint32_t)p->r->identity.kind||gu_u32(v+20)!=BW_PROVIDER_POLICY_V1||memcmp(hash,v+GU_INTENT-32,32)||memcmp(v+24,b->sha256,32)||memcmp(v+56,p->r->prepared.record_sha256,32)||memcmp(v+88,p->r->operation,33)||memcmp(v+128,b->title,10)||memcmp(v+144,b->game,75)||!memchr(v+224,0,sizeof b->source)||strcmp((const char *)v+224,b->source)||!memchr(v+1568,0,128)||memcmp(v+1696,paths_hash,32))loaded=-1;
            else {memcpy(p->intent,body,GU_INTENT);memcpy(p->content,v+1568,128);}}}
    free(body);if(loaded<0)return -1;
    int cursor=v2_openat(p->ledger,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);if(cursor<0)return -1;DIR *dir=fdopendir(cursor);if(!dir){gu_release(p,cursor);return -1;}int bad=0;unsigned count=0;struct dirent *entry;
    for(;;){errno=0;entry=readdir(dir);if(!entry){if(errno)bad=1;break;}if(++count>4096){bad=1;break;}const char *name=entry->d_name;if(strncmp(name,b->game,74)||name[74]!='.'||!strcmp(name,p->intent_leaf)||!strcmp(name,p->registration_leaf)||!strcmp(name,p->files_leaf)||!strcmp(name,p->done_leaf))continue;
        size_t name_size=strlen(name);
        if(name_size==113&&!strcmp(name+107,".files")){char pair[128];memcpy(pair,name,107);strcpy(pair+107,".intent");struct stat ps;if(cp_fstatat(p->ledger,pair,&ps,AT_SYMLINK_NOFOLLOW))bad=1;if(bad)break;continue;}
        if(name_size==120&&!strcmp(name+107,".registration")){char pair[128];memcpy(pair,name,107);strcpy(pair+107,".intent");struct stat ps;if(cp_fstatat(p->ledger,pair,&ps,AT_SYMLINK_NOFOLLOW))bad=1;if(bad)break;continue;}
        if(name_size==112&&!strcmp(name+107,".done")){char pair[128];memcpy(pair,name,107);strcpy(pair+107,".intent");struct stat ps;if(cp_fstatat(p->ledger,pair,&ps,AT_SYMLINK_NOFOLLOW))bad=1;if(bad)break;continue;}
        if(name_size!=114||strcmp(name+107,".intent")){bad=1;break;}void *old=NULL;size_t old_size=0;struct stat os;
        if(gu_read(p,p->ledger,name,GU_INTENT,&old,&old_size,&os)||old_size!=GU_INTENT||(os.st_mode&07777)!=0600){free(old);bad=1;break;}
        unsigned char hash[32];cp_sha_bytes(old,GU_INTENT-32,hash);unsigned char *v=old;char done[128];memcpy(done,name,107);strcpy(done+107,".done");void *marked=NULL;size_t mn=0;struct stat ms;
        unsigned char expected[GU_PHASE]={0};memcpy(expected,p->preparation_only?"BWGPD001":"BWGUD001",8);memcpy(expected+16,v+GU_INTENT-32,32);memcpy(expected+48,v+88,33);cp_sha_bytes(expected,96,expected+96);
        if(memcmp(v,p->preparation_only?"BWGPR001":"BWGUR001",8)||memcmp(v+GU_INTENT-32,hash,32)||memcmp(v+24,b->sha256,32)||!memcmp(v+56,p->r->prepared.record_sha256,32)||gu_read(p,p->ledger,done,GU_PHASE,&marked,&mn,&ms)||mn!=GU_PHASE||(ms.st_mode&07777)!=0600||memcmp(marked,expected,GU_PHASE))bad=1;
        free(old);free(marked);if(bad)break;
    }
    if(gu_dir_release(p,dir))bad=1;
    if(loaded==1&&(gu_absent_at(p->ledger,p->registration_leaf)||gu_absent_at(p->ledger,p->files_leaf)||gu_absent_at(p->ledger,p->done_leaf)))bad=1;
    return bad?-1:loaded;
}
static void gu_intent_encode(struct gu_plan *p){unsigned char *v=p->intent;const struct bw_source_binding *b=&p->r->identity.binding;memset(v,0,GU_INTENT);memcpy(v,p->preparation_only?"BWGPR001":"BWGUR001",8);gu_put32(v+8,1);gu_put32(v+12,GU_INTENT);gu_put32(v+16,p->r->identity.kind);gu_put32(v+20,p->r->identity.policy);memcpy(v+24,b->sha256,32);memcpy(v+56,p->r->prepared.record_sha256,32);memcpy(v+88,p->r->operation,33);memcpy(v+128,b->title,10);memcpy(v+144,b->game,75);memcpy(v+224,b->source,strlen(b->source)+1);gu_stat_wire(v+1280,p->ids+4);gu_stat_wire(v+1408,p->ids+5);memcpy(v+1536,p->inventory,32);memcpy(v+1568,p->content,128);
    struct cp_sha h;cp_sha_init(&h);const char *paths[]={p->r->environment.data_root,p->r->environment.apps_root,p->r->environment.database};for(unsigned i=0;i<3;i++)cp_sha_update(&h,paths[i],strlen(paths[i])+1);cp_sha_final(&h,v+1696);cp_sha_bytes(v,GU_INTENT-32,v+GU_INTENT-32);
}
static int gu_load_plan(struct gu_plan *p){
    p->checking="prepared_root_identity";
    const struct bw_source_binding *b=&p->r->identity.binding;p->root=gu_open_child(p,p->games,1,b->game,4);p->state=gu_open_child(p,p->scope,2,b->game,5);if(p->root<0||p->state<0)return -1;
    p->checking="actual_prepared_metadata";struct bw_native_prepared actual={0};int inspected=bw_native_send_inspect(p->r->environment.data_root,&p->r->identity,&actual);if(inspected==-2)p->release_unknown=1;
    if(inspected||actual.state!=BW_PREPARED_VALID||actual.prepared_policy!=p->r->prepared.prepared_policy||
       (actual.prepared_policy==3&&(strcmp(actual.prepared_component,p->r->prepared.prepared_component)||memcmp(actual.provenance_sha256,p->r->prepared.provenance_sha256,32)||actual.provenance_size!=p->r->prepared.provenance_size))||
       memcmp(actual.record_sha256,p->r->prepared.record_sha256,32)||strcmp(actual.run,p->r->prepared.run)||gu_current(p))return -1;
    p->checking="exact_receipt_and_map";
    void *body=NULL;size_t size=0;struct stat st;if(gu_read(p,p->state,"prepared.bin",384,&body,&size,&st)||size!=384||(st.st_mode&07777)!=0600||gu_receipt(p,body,size,1)){free(body);return -1;}memcpy(p->receipt,body,384);free(body);
    if(gu_read(p,p->root,"blackwire.files",40u*1024u*1024u,&body,&size,&st)||size!=gu_u64(p->receipt+280)){free(body);return -1;}p->map=body;p->map_size=size;unsigned char hash[32];cp_sha_bytes(body,size,hash);if(memcmp(hash,p->receipt+112,32)||bw_manifest_open(&p->manifest,body,size)||p->manifest.count!=gu_u32(p->receipt+268))return -1;
    if(gu_read(p,p->root,"blackwire.generation.digests",56u+32u*BW_MANIFEST_MAX_FILES,&body,&size,&st)||size!=gu_u64(p->receipt+288)){free(body);return -1;}p->digests=body;p->digest_size=size;cp_sha_bytes(body,size,hash);if(size!=56u+32u*p->manifest.count||memcmp(hash,p->receipt+144,32)||memcmp(body,"BWDIG001",8)||gu_u32((unsigned char *)body+8)!=1||gu_u32((unsigned char *)body+12)!=p->manifest.count||gu_u64((unsigned char *)body+16)!=p->manifest.checksum||memcmp((unsigned char *)body+24,p->receipt+112,32))return -1;
    p->rows=calloc(p->manifest.count,sizeof *p->rows);if(!p->rows)return -1;for(uint32_t i=0;i<p->manifest.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&p->manifest,i,&row))return -1;p->rows[i].path=row.path;p->rows[i].id=i;}
    qsort(p->rows,p->manifest.count,sizeof *p->rows,gu_row_compare);
    if(p->r->legacy_source_fakelib2){unsigned mapped=0;
        for(uint32_t i=0;i<p->manifest.count;i++)if(!strncmp(p->rows[i].path,"fakelib2/",9))mapped++;
        if(!mapped)return -1;
    }
    p->checking="sealed_companion_projection";if(gu_projection(p))return -1;
    p->checking="complete_prepared_tree";if(gu_walk(p,p->root,"",0,0))return -1;p->checking="complete_private_record_tree";if(gu_walk(p,p->state,"",1,0))return -1;
    unsigned fake_count=0,fake_dir=0,generated=0;
    for(size_t i=0;i<p->count;i++)if(!p->nodes[i].state){
        if(!strcmp(p->nodes[i].path,"fakelib2"))fake_dir++;
        else if(!strncmp(p->nodes[i].path,"fakelib2/",9)){
            fake_count++;uint32_t id=0;
            if(S_ISREG(p->nodes[i].st.st_mode)&&!gu_find(p,p->nodes[i].path,&id,0))generated++;
        }
    }
    /* Source-owned rows (and their required directories) were already matched
     * exactly above. The legacy six-member budget constrains generated extras,
     * never the number of independently owned original companion files. */
    if(!p->projection&&fake_dir&&(!fake_count||generated>6))return -1;
    /* The complete manifest must occur exactly once; the tree walk rejects
     * extras but cannot infer missing expected entries from directory counts. */
    for(uint32_t i=0;i<p->manifest.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&p->manifest,i,&row))return -1;char leaf[256],relative[1024];int parent=gu_parent(p,p->anchor+4,p->root,row.path,0,leaf,relative);if(parent<0)return -1;struct stat named;int bad=cp_fstatat(parent,leaf,&named,AT_SYMLINK_NOFOLLOW);if(gu_release(p,parent))bad=1;if(bad)return -1;}
    int parent=-1;char leaf[256],relative[1024];parent=gu_parent(p,p->anchor+4,p->root,"sce_sys/param.json",0,leaf,relative);if(parent<0)return -1;body=NULL;size=0;char title[10];int bad=gu_read(p,parent,leaf,BW_SOURCE_PARAM_LIMIT,&body,&size,NULL)||bw_source_param_identity(body,size,title,p->content)||strcmp(title,b->title);free(body);if(gu_release(p,parent))bad=1;if(bad)return -1;
    p->checking="inventory_named_identity_recheck";gu_inventory(p);return gu_revalidate(p);
}
static int gu_rmdir(int parent,const char *leaf){
#ifdef BW_COPY_HOST_TEST
    return unlinkat(parent,leaf,AT_REMOVEDIR);
#else
    return (int)cp_at(SYS_unlinkat,parent,(long)leaf,AT_REMOVEDIR,0,0);
#endif
}
static int gu_continuation_guard(struct gu_plan *p){
    return gu_title_absent(p)||p->r->ops.source_excluded(p->r->ops.opaque,&p->removed)||gu_title_absent(p)?-1:0;
}
static int gu_delete_nodes(struct gu_plan *p,unsigned state){
    for(size_t at=p->count;at;at--){const struct gu_node *node=p->nodes+at-1;if(node->state!=state)continue;int parent=-1;char leaf[256];int directory=S_ISDIR(node->st.st_mode);
        if(p->continuation&&gu_continuation_guard(p))return -1;
        if(gu_named(p,node,!directory,&parent,leaf))return -1;int bad=directory?gu_rmdir(parent,leaf):cp_unlinkat(parent,leaf);if(!bad&&(fsync(parent)||gu_absent_at(parent,leaf)||gu_current(p)))bad=1;if(gu_release(p,parent))bad=1;if(bad)return -1;
        if(!directory){p->out->files_removed++;gu_progress(p,state?BW_GAME_REMOVE_RECORD:BW_GAME_REMOVE_FILES,state?"Removing its private preparation record":"Removing the selected PS5 startup copy");}}
    unsigned a=state?5:4;int parent=state?p->scope:p->games;struct stat named,held;int fd=state?p->state:p->root;
    if((p->continuation&&gu_continuation_guard(p))||gu_current(p)||fstat(fd,&held)||cp_fstatat(parent,p->r->identity.binding.game,&named,AT_SYMLINK_NOFOLLOW)||!gu_node_same(p->ids+a,&held)||!gu_node_same(&held,&named)||gu_rmdir(parent,p->r->identity.binding.game))return -1;
    if(state)p->state_removed=1;else p->root_removed=1;return fsync(parent)||gu_absent_at(parent,p->r->identity.binding.game)||gu_current(p)?-1:0;
}
static int gu_close(struct gu_plan *p){int bad=0;int *fds[]={&p->root,&p->state,&p->send_lock,&p->ledger,&p->apps,&p->scope,&p->games,&p->data};for(unsigned i=0;i<8;i++)if(*fds[i]>=0){if(gu_release(p,*fds[i]))bad=1;*fds[i]=-1;}for(size_t i=0;i<p->count;i++)free(p->nodes[i].path);free(p->nodes);free(p->rows);free(p->map);free(p->digests);bw_companion_receipt_clear(&p->projection);free(p->original_map);free(p->original_digests);free(p->companions);return bad?-1:0;}
static int gu_complete(struct gu_plan *p){
    if(gu_all_absent(p)||p->r->ops.source_excluded(p->r->ops.opaque,&p->removed)||gu_all_absent(p))return -1;
    p->out->registration_confirmed=p->out->source_absent=p->out->prepared_record_absent=1;
    unsigned char done[GU_PHASE];gu_phase(done,p,"BWGUD001");int existing=gu_phase_read(p,p->done_leaf,"BWGUD001");if(existing<0||(existing==1&&gu_marker(p,p->done_leaf,done,sizeof done)))return -1;
    gu_progress(p,BW_GAME_REMOVE_COMMITTING,"Updating the removed-games library");struct bw_removed_ops ops={p,gu_removed_current,gu_removed_absent};struct bw_removed_result removed;
    if(bw_removed_commit(p->r->environment.data_root,&p->removed,&ops,&removed)||removed.state!=BW_REMOVED_COMMITTED||gu_all_absent(p))return -1;p->out->library_removed=1;return 0;
}
static int gu_preparation_owner_clear(struct gu_plan *p){
    struct bw_nrc_candidate *rows=calloc(BW_NRC_CANDIDATE_LIMIT,sizeof *rows);size_t count=0;if(!rows)return -1;
    int status=bw_nrc_candidates(&p->r->environment,rows,BW_NRC_CANDIDATE_LIMIT,&count);
    if(status==BW_NRC_RELEASE_UNCONFIRMED)p->release_unknown=1;
    for(size_t i=0;!status&&i<count;i++)if(!strcmp(rows[i].subject.title,p->r->identity.binding.title))status=-1;
    free(rows);return status||gu_current(p)?-1:0;
}
int bw_native_preparation_remove(const struct bw_preparation_remove_request *q,struct bw_game_uninstall_result *out){
    if(out)memset(out,0,sizeof *out);
    if(!q||!out||!q->ops.current||!q->ops.source_excluded)return -1;
    /* No registration callback exists in this public purpose. The temporary
     * view is private to exact inventory/deletion helpers and is never passed
     * to the title-wide normal uninstall or Removed-library store. */
    struct bw_game_uninstall_request r={.environment=q->environment,.identity=q->identity,.prepared=q->prepared,
        .ops={.opaque=q->ops.opaque,.current=q->ops.current,.source_excluded=q->ops.source_excluded,.observe=q->ops.observe},
        .reader_size=q->reader_size,.legacy_source_fakelib2=q->legacy_source_fakelib2};
    memcpy(r.operation,q->operation,sizeof r.operation);memcpy(r.confirmed_title,q->confirmed_title,sizeof r.confirmed_title);memcpy(r.reader_sha256,q->reader_sha256,32);
    if(gu_identity(&r)||!r.environment.data_root||!r.environment.apps_root||!r.environment.database)return -1;
    if(pthread_mutex_trylock(&gu_lock))return gu_finish(&(struct gu_plan){.out=out},BW_GAME_REMOVE_ERROR,"another_removal_component_is_active",1);
    struct gu_plan p={.r=&r,.out=out,.checking="preparation_only_trusted_roots",.preparation_only=1};gu_init(&p);int rc=-1;
    p.removed.identity=r.identity;memcpy(p.removed.operation,r.operation,33);memcpy(p.removed.prepared_sha256,r.prepared.record_sha256,32);
    gu_progress(&p,BW_GAME_REMOVE_CHECKING,"Checking only this owned startup copy");
    p.data=gu_open_root(&p,r.environment.data_root,p.anchor,p.ids);if(p.data<0||gu_current(&p))goto done;
    p.games=gu_open_child(&p,p.data,0,"games",1);p.scope=gu_open_child(&p,p.data,0,"native-prepared",2);
    p.apps=gu_open_apps(&p,r.environment.apps_root,p.anchor+3,p.ids+3);
    if(p.games<0||p.scope<0||p.apps<0||gu_exact(&p,p.scope,".blackwire-native-prepared",gu_prepared,sizeof gu_prepared-1,0644)||gu_current(&p))goto done;
    p.send_lock=v2_openat(p.data,".blackwire-send.lock",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(p.send_lock<0||gu_exact(&p,p.data,".blackwire-send.lock",gu_send_lock,sizeof gu_send_lock-1,0600)||flock(p.send_lock,LOCK_EX|LOCK_NB)||fstat(p.send_lock,&p.lock_identity))goto done;p.lock_pinned=1;
    p.checking="preparation_only_registration_owner_present_or_unknown";if(gu_preparation_owner_clear(&p))goto done;
    if(gu_load_plan(&p))goto done;out->files_total=p.files_total;
    p.checking="preparation_only_source_exclusion_unconfirmed";
    if(r.ops.source_excluded(r.ops.opaque,&p.removed)||gu_current(&p)||gu_preparation_owner_clear(&p))goto done;
    p.checking="preparation_only_journal_unconfirmed";
    if(gu_ledger(&p,1))goto done;int prior=gu_journal_load(&p);
    if(prior!=1){if(!prior)out->mutation_started=1;goto done;}
    gu_intent_encode(&p);out->mutation_started=1;if(gu_marker(&p,p.intent_leaf,p.intent,GU_INTENT))goto done;
    unsigned char phase[GU_PHASE];gu_phase(phase,&p,"BWGPF001");
    if(gu_marker(&p,p.files_leaf,phase,sizeof phase)||gu_revalidate(&p)||gu_preparation_owner_clear(&p)||r.ops.source_excluded(r.ops.opaque,&p.removed))goto done;
    p.checking="preparation_only_file_deletion_unconfirmed";gu_progress(&p,BW_GAME_REMOVE_FILES,"Removing only the owned startup files");
    if(gu_delete_nodes(&p,0))goto done;out->source_absent=1;
    p.checking="preparation_only_record_deletion_unconfirmed";gu_progress(&p,BW_GAME_REMOVE_RECORD,"Removing only this preparation record");
    if(r.ops.source_excluded(r.ops.opaque,&p.removed)||gu_preparation_owner_clear(&p)||gu_delete_nodes(&p,1))goto done;out->prepared_record_absent=1;
    int root=p.root,state=p.state;p.root=p.state=-1;if(gu_release(&p,root)|gu_release(&p,state))goto done;
    p.checking="preparation_only_final_absence_unconfirmed";
    if(gu_absent_at(p.games,r.identity.binding.game)||gu_absent_at(p.scope,r.identity.binding.game)||gu_current(&p)||
       r.ops.source_excluded(r.ops.opaque,&p.removed)||gu_preparation_owner_clear(&p))goto done;
    gu_phase(phase,&p,"BWGPD001");if(gu_marker(&p,p.done_leaf,phase,sizeof phase))goto done;
    out->preparation_only=1;rc=gu_finish(&p,BW_GAME_REMOVE_DONE,"Owned startup files removed. Existing console game metadata was preserved.",0);
done:
    if(gu_close(&p)){p.checking="preparation_only_descriptor_close_unconfirmed";rc=-1;}
    out->release_unknown=p.release_unknown;if(p.release_unknown)rc=-1;
    if(rc<0){out->preparation_only=0;gu_finish(&p,out->mutation_started||out->release_unknown?BW_GAME_REMOVE_UNKNOWN:BW_GAME_REMOVE_ERROR,p.checking,-1);}
    pthread_mutex_unlock(&gu_lock);return rc;
}
int bw_native_game_uninstall(const struct bw_game_uninstall_request *r,struct bw_game_uninstall_result *out){
    if(out)memset(out,0,sizeof *out);if(!r||!out||gu_identity(r)||!r->environment.data_root||!r->environment.apps_root||!r->environment.database||!r->ops.current||!r->ops.registration_absent||!r->ops.source_excluded)return -1;
    if(pthread_mutex_trylock(&gu_lock))return gu_finish(&(struct gu_plan){.out=out},BW_GAME_REMOVE_ERROR,"another_removal_component_is_active",1);
    struct gu_plan p={.r=r,.out=out,.checking="trusted_roots_and_private_journal"};gu_init(&p);p.removed.identity=r->identity;memcpy(p.removed.operation,r->operation,33);memcpy(p.removed.prepared_sha256,r->prepared.record_sha256,32);int rc=-1;
    gu_progress(&p,BW_GAME_REMOVE_CHECKING,"Checking this prepared copy");p.data=gu_open_root(&p,r->environment.data_root,p.anchor,p.ids);if(p.data<0||gu_current(&p))goto done;
    p.games=gu_open_child(&p,p.data,0,"games",1);p.scope=gu_open_child(&p,p.data,0,"native-prepared",2);p.apps=gu_open_apps(&p,r->environment.apps_root,p.anchor+3,p.ids+3);
    if(p.games<0||p.scope<0||p.apps<0||gu_exact(&p,p.scope,".blackwire-native-prepared",gu_prepared,sizeof gu_prepared-1,0644)||gu_current(&p))goto done;
    p.send_lock=v2_openat(p.data,".blackwire-send.lock",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(p.send_lock<0||gu_exact(&p,p.data,".blackwire-send.lock",gu_send_lock,sizeof gu_send_lock-1,0600)||flock(p.send_lock,LOCK_EX|LOCK_NB)||fstat(p.send_lock,&p.lock_identity))goto done;p.lock_pinned=1;
    if(gu_ledger(&p,1))goto done;int prior=gu_journal_load(&p);if(prior<0)goto done;
    if(!prior){out->mutation_started=1;int files=gu_phase_read(&p,p.files_leaf,"BWGUF001"),registration=gu_phase_read(&p,p.registration_leaf,"BWGUG001");if(files<0||registration<0)goto done;if(!files||!registration){if(!gu_complete(&p)){rc=gu_finish(&p,BW_GAME_REMOVE_DONE,"removed_from_ps5_pc_backup_and_saves_preserved",0);goto done;}goto done;}}
    unsigned char old_intent[GU_INTENT];if(!prior)memcpy(old_intent,p.intent,GU_INTENT);
    if(gu_load_plan(&p))goto done;out->files_total=p.files_total;
    if((r->ops.source_safe?r->ops.source_safe(r->ops.opaque,&p.removed):r->ops.source_excluded(r->ops.opaque,&p.removed))||gu_current(&p))goto done;
    gu_intent_encode(&p);if(!prior){if(memcmp(old_intent,p.intent,GU_INTENT))goto done;}else {out->mutation_started=1;if(gu_marker(&p,p.intent_leaf,p.intent,GU_INTENT))goto done;}
    gu_progress(&p,BW_GAME_REMOVE_REGISTRATION,"Removing the selected PS5 registration");int registration=gu_phase_read(&p,p.registration_leaf,"BWGUG001");if(registration<0)goto done;
    if(registration==1){unsigned char attempted[GU_PHASE];gu_phase(attempted,&p,"BWGUG001");if(gu_marker(&p,p.registration_leaf,attempted,sizeof attempted)||gu_revalidate(&p))goto done;registration=r->ops.registration_absent(r->ops.opaque,&p.removed);}
    /* A transport status never proves absence. Even a lost reply can be
     * resolved within this retained live invocation by independent readback.
     * A later invocation with a consumed registration marker observes only. */
    (void)registration;p.checking="independent_registered_title_absence_unconfirmed";if(gu_title_absent(&p)||gu_title_absent(&p))goto done;p.checking="source_exclusion_or_inventory_changed";if(r->ops.source_excluded(r->ops.opaque,&p.removed)||gu_revalidate(&p))goto done;out->registration_confirmed=1;
    unsigned char phase[GU_PHASE];gu_phase(phase,&p,"BWGUF001");p.checking="file_deletion_intent_unconfirmed";if(gu_marker(&p,p.files_leaf,phase,sizeof phase))goto done;p.checking="pre_unlink_inventory_or_title_changed";if(gu_revalidate(&p)||gu_title_absent(&p))goto done;
    gu_progress(&p,BW_GAME_REMOVE_FILES,"Removing the selected PS5 startup copy");p.checking="startup_copy_deletion_unconfirmed";if(gu_delete_nodes(&p,0))goto done;out->source_absent=1;
    gu_progress(&p,BW_GAME_REMOVE_RECORD,"Removing its private preparation record");p.checking="private_record_deletion_unconfirmed";if(gu_delete_nodes(&p,1))goto done;out->prepared_record_absent=1;
    /* Close the removed roots before committing a successful library result.
     * A close uncertainty cannot be hidden by an already-created tombstone. */
    int root=p.root,state=p.state;p.root=p.state=-1;if(gu_release(&p,root)|gu_release(&p,state))goto done;
    p.checking="final_absence_or_library_commit_unconfirmed";if(gu_complete(&p))goto done;rc=gu_finish(&p,BW_GAME_REMOVE_DONE,"removed_from_ps5_pc_backup_and_saves_preserved",0);
done:if(gu_close(&p)){p.checking="descriptor_close_unconfirmed";rc=-1;}
    out->release_unknown=p.release_unknown;if(p.release_unknown){p.checking="descriptor_release_unconfirmed";rc=-1;}
    if(rc<0)gu_finish(&p,out->mutation_started||out->release_unknown?BW_GAME_REMOVE_UNKNOWN:BW_GAME_REMOVE_ERROR,p.checking,-1);pthread_mutex_unlock(&gu_lock);return rc;
}
int bw_native_game_uninstall_continue_files(const struct bw_game_uninstall_files_request *q,struct bw_game_uninstall_result *out){
    if(out)*out=(struct bw_game_uninstall_result){.phase=BW_GAME_REMOVE_UNKNOWN,.reason="original_file_continuation_request_invalid"};
    if(!q||!out||!q->ops.current||!q->ops.source_excluded)return -1;
    /* A separate public type makes replaying registration impossible. This
     * private view is used only by existing inventory/deletion helpers; its
     * registration_absent/source_safe pointers remain NULL throughout. */
    struct bw_game_uninstall_request r={.environment=q->environment,.identity=q->identity,.prepared=q->prepared,
        .ops={.opaque=q->ops.opaque,.current=q->ops.current,.source_excluded=q->ops.source_excluded,.observe=q->ops.observe},.reader_size=q->reader_size,.legacy_source_fakelib2=q->legacy_source_fakelib2};
    memcpy(r.operation,q->operation,sizeof r.operation);memcpy(r.confirmed_title,q->confirmed_title,sizeof r.confirmed_title);memcpy(r.reader_sha256,q->reader_sha256,sizeof r.reader_sha256);
    if(gu_identity(&r)||!r.environment.data_root||!r.environment.apps_root||!r.environment.database)return -1;
    if(pthread_mutex_trylock(&gu_lock))return gu_finish(&(struct gu_plan){.out=out},BW_GAME_REMOVE_UNKNOWN,"another_removal_component_is_active",1);
    struct gu_plan p={.r=&r,.out=out,.checking="original_file_continuation_roots_unconfirmed",.continuation=1};gu_init(&p);int rc=-1;
    p.removed.identity=r.identity;memcpy(p.removed.operation,r.operation,33);memcpy(p.removed.prepared_sha256,r.prepared.record_sha256,32);
    gu_progress(&p,BW_GAME_REMOVE_CHECKING,"Checking the original Uninstall file phase");
    p.data=gu_open_root(&p,r.environment.data_root,p.anchor,p.ids);if(p.data<0||gu_current(&p))goto done;
    p.games=gu_open_child(&p,p.data,0,"games",1);p.scope=gu_open_child(&p,p.data,0,"native-prepared",2);p.apps=gu_open_apps(&p,r.environment.apps_root,p.anchor+3,p.ids+3);
    if(p.games<0||p.scope<0||p.apps<0||gu_exact(&p,p.scope,".blackwire-native-prepared",gu_prepared,sizeof gu_prepared-1,0644)||gu_current(&p))goto done;
    p.send_lock=v2_openat(p.data,".blackwire-send.lock",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(p.send_lock<0||gu_exact(&p,p.data,".blackwire-send.lock",gu_send_lock,sizeof gu_send_lock-1,0600)||flock(p.send_lock,LOCK_EX|LOCK_NB)||fstat(p.send_lock,&p.lock_identity))goto done;p.lock_pinned=1;
    p.checking="original_file_continuation_journal_unconfirmed";
    if(gu_ledger(&p,0)||gu_journal_load(&p)!=0)goto done;out->mutation_started=1;
    if(gu_phase_read(&p,p.registration_leaf,"BWGUG001")!=0)goto done;
    int files=gu_phase_read(&p,p.files_leaf,"BWGUF001"),done=gu_phase_read(&p,p.done_leaf,"BWGUD001");if(files<0||done<0)goto done;
    if(!files){
        /* Existing file authority is consumed. This path can only prove an
         * already externally/previously completed result; never unlink. */
        p.checking="consumed_file_phase_observation_unknown";
        if(!gu_complete(&p))rc=gu_finish(&p,BW_GAME_REMOVE_DONE,"removed_from_ps5_pc_backup_and_saves_preserved",0);
        goto done;
    }
    if(!done){p.checking="done_without_file_phase_invalid";goto done;}
    p.checking="file_continuation_title_or_source_exclusion_unconfirmed";if(gu_continuation_guard(&p)||gu_continuation_guard(&p))goto done;
    unsigned char original[GU_INTENT];memcpy(original,p.intent,sizeof original);
    if(gu_load_plan(&p))goto done;out->files_total=p.files_total;gu_intent_encode(&p);
    p.checking="original_file_continuation_inventory_changed";if(memcmp(original,p.intent,sizeof original)||gu_revalidate(&p)||gu_continuation_guard(&p))goto done;
    /* Recheck both absent phase names immediately before arming this distinct
     * phase. O_EXCL plus durable readback consumes it before the first unlink. */
    p.checking="file_continuation_phase_already_armed";
    if(gu_phase_read(&p,p.files_leaf,"BWGUF001")!=1||gu_phase_read(&p,p.done_leaf,"BWGUD001")!=1)goto done;
    unsigned char phase[GU_PHASE];gu_phase(phase,&p,"BWGUF001");p.checking="file_continuation_intent_unconfirmed";
    if(gu_marker(&p,p.files_leaf,phase,sizeof phase))goto done;
    p.checking="file_continuation_inventory_or_title_changed";if(gu_revalidate(&p)||gu_continuation_guard(&p))goto done;out->registration_confirmed=1;
    gu_progress(&p,BW_GAME_REMOVE_FILES,"Removing the original Uninstall startup copy");p.checking="continued_startup_copy_deletion_unconfirmed";if(gu_delete_nodes(&p,0))goto done;out->source_absent=1;
    p.checking="file_continuation_private_record_guard_unconfirmed";if(gu_continuation_guard(&p))goto done;
    gu_progress(&p,BW_GAME_REMOVE_RECORD,"Removing its original private preparation record");p.checking="continued_private_record_deletion_unconfirmed";if(gu_delete_nodes(&p,1))goto done;out->prepared_record_absent=1;
    int root=p.root,state=p.state;p.root=p.state=-1;p.checking="continued_removed_root_close_unconfirmed";if(gu_release(&p,root)|gu_release(&p,state))goto done;
    p.checking="continued_final_absence_or_library_commit_unconfirmed";if(gu_complete(&p))goto done;
    rc=gu_finish(&p,BW_GAME_REMOVE_DONE,"removed_from_ps5_pc_backup_and_saves_preserved",0);
done:if(gu_close(&p)){p.checking="continued_descriptor_close_unconfirmed";rc=-1;}
    out->release_unknown=p.release_unknown;if(p.release_unknown){p.checking="continued_descriptor_release_unconfirmed";rc=-1;}
    if(rc<0)gu_finish(&p,BW_GAME_REMOVE_UNKNOWN,p.checking,-1);pthread_mutex_unlock(&gu_lock);return rc;
}
int bw_native_game_uninstall_probe(const struct bw_nrc_environment *environment,const struct bw_provider_binding *identity,const struct bw_native_prepared *prepared,struct bw_game_uninstall_capability *out){
    if(out)memset(out,0,sizeof *out);if(!environment||!identity||!prepared||!out||!environment->data_root||!environment->apps_root||!environment->database||gu_provider(identity))return -1;
    out->reason="prepared_copy_metadata_unavailable";struct bw_game_uninstall_request request={.environment=*environment,.identity=*identity,.prepared=*prepared};struct bw_game_uninstall_result ignored={0};struct gu_plan p={.r=&request,.out=&ignored};gu_init(&p);int rc=-1;
    struct bw_removed_source removed={0};struct bw_removed_result visibility={0};if(bw_removed_lookup(environment->data_root,identity,&removed,&visibility))goto done;
    p.data=gu_open_root(&p,environment->data_root,p.anchor,p.ids);if(p.data<0)goto done;p.games=gu_open_child(&p,p.data,0,"games",1);p.scope=gu_open_child(&p,p.data,0,"native-prepared",2);p.apps=gu_open_apps(&p,environment->apps_root,p.anchor+3,p.ids+3);
    if(p.games<0||p.scope<0||p.apps<0||gu_anchor(&p))goto done;
    if(visibility.state==BW_REMOVED_COMMITTED){out->removed=1;out->reason="durable_removed_source";
        /* The readonly matcher returns0 only when no title row exists; this
         * sentinel never accepts any installed content as a match. */
        for(unsigned i=0;i<2;i++)if(gu_absent_at(p.games,identity->binding.game)||gu_absent_at(p.scope,identity->binding.game)||gu_absent_at(p.apps,identity->binding.title)||bw_registration_db_match(environment->database,identity->binding.title,"BLACKWIRE_REMOVED_TITLE_ABSENCE")!=0||gu_anchor(&p))goto visible;
        out->can_restore=1;out->reason="removed_source_can_be_shown_again";
visible:rc=0;goto done;
    }
    if(prepared->state!=BW_PREPARED_VALID||gu_prepared_policy(prepared)||strcmp(prepared->game,identity->binding.game)||!gu_nonce(prepared->run)||!gu_nonzero(prepared->record_sha256,32)){rc=0;goto done;}
    p.root=gu_open_child(&p,p.games,1,identity->binding.game,4);p.state=gu_open_child(&p,p.scope,2,identity->binding.game,5);if(p.root<0||p.state<0)goto done;
    void *body=NULL;size_t size=0;struct stat st;int loaded=gu_read(&p,p.state,"prepared.bin",384,&body,&size,&st);int bad=loaded||size!=384||(st.st_mode&07777)!=0600||gu_receipt(&p,body,size,1);if(!bad)memcpy(p.receipt,body,384);free(body);if(bad)goto done;
    struct bw_json json={.maximum=4096};bad=gu_binding_json(&p,&json)||gu_exact(&p,p.state,"source.json",json.bytes,json.used,0644)||gu_exact(&p,p.root,"blackwire.generation.owner",json.bytes,json.used,0644);free(json.bytes);if(bad)goto done;
    int parent=-1;char leaf[256],relative[1024];parent=gu_parent(&p,p.anchor+4,p.root,"sce_sys/param.json",0,leaf,relative);if(parent<0)goto done;body=NULL;size=0;char title[10];bad=gu_read(&p,parent,leaf,BW_SOURCE_PARAM_LIMIT,&body,&size,NULL)||bw_source_param_identity(body,size,title,p.content)||strcmp(title,identity->binding.title);free(body);if(gu_release(&p,parent))bad=1;if(bad)goto done;
    struct bw_reader_selection selected={0};strcpy(selected.title,identity->binding.title);strcpy(selected.game,identity->binding.game);strcpy(selected.source,identity->binding.source);strcpy(selected.run,prepared->run);selected.source_kind=identity->kind;selected.source_policy=BW_PROVIDER_POLICY_V1;selected.manifest_count=gu_u32(p.receipt+268);
    unsigned char header[40];int map=v2_openat(p.root,"blackwire.files",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);struct stat first,after,named;
    if(map<0)goto done;bad=fstat(map,&first)||!S_ISREG(first.st_mode)||first.st_nlink!=1||first.st_uid!=geteuid()||(first.st_mode&07777)!=0644||first.st_size<40||(uint64_t)first.st_size!=gu_u64(p.receipt+280)||pread(map,header,sizeof header,0)!=(ssize_t)sizeof header||fstat(map,&after)||cp_fstatat(p.root,"blackwire.files",&named,AT_SYMLINK_NOFOLLOW)||!gu_same(&first,&after)||!gu_same(&after,&named);
    if(gu_release(&p,map))bad=1;if(bad||memcmp(header,"BWMAP002",8)||gu_u32(header+8)!=2||gu_u32(header+12)!=32||gu_u32(header+16)!=selected.manifest_count)goto done;selected.manifest_hash=gu_u64(header+32);
    if(cp_fstatat(p.root,"eboot.bin",&st,AT_SYMLINK_NOFOLLOW)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_uid!=geteuid()||(st.st_mode&07777)!=0755||st.st_size<=0)goto done;selected.eboot_size=(uint64_t)st.st_size;
    for(unsigned i=0;i<32;i++){snprintf(selected.binding_hex+2*i,3,"%02x",identity->binding.sha256[i]);snprintf(selected.prepared_record_hex+2*i,3,"%02x",prepared->record_sha256[i]);}
    /* Only bounded receipt/header/stat metadata is used for presentation.
     * This advisory selection is never persisted or passed to Play/reader. */
    struct bw_native_registration_observation observation={0};
    enum bw_registration_state registration=bw_native_registration_observe(environment->data_root,environment->apps_root,environment->database,&selected,&observation);
    if(registration!=BW_REG_ABSENT&&registration!=BW_REG_MATCH){
        out->can_remove_startup=!observation.database.release_or_identity_unknown;
        out->reason=out->can_remove_startup?"Console registration is conflicting or unconfirmed. Remove startup files only preserves that existing console installation.":
            "Console registration could not be observed or released reliably. Refresh status before removing startup files.";
        rc=0;goto done;
    }
    if(registration==BW_REG_MATCH){int app=v2_openat(p.apps,identity->binding.title,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK,0);if(app<0)goto done;bad=gu_absent_at(app,"app.pkg");if(gu_release(&p,app))bad=1;if(bad){out->reason="installed_package_is_not_an_owned_prepared_folder";rc=0;goto done;}}
    if(gu_anchor(&p))goto done;out->can_uninstall=1;out->reason="owned_prepared_folder_confirmation_required";rc=0;
done:if(gu_close(&p))rc=-1;if(rc<0){out->can_uninstall=out->can_restore=out->can_remove_startup=0;out->reason="prepared_or_removed_source_metadata_unknown";}return rc;
}
