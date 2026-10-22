/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exact-event ownership ledger and one-shot missing-source reconciliation.
 * No process control, database mutation, source deletion or global SMP policy. */
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "native_registration_cleanup.h"
#include "native_registration_retirement.h"
#include "native_registration_removed.h"
#include "native_prepared_target.h"
#include "source_param.h"
#include "registration_param_compat.h"
#include "../streamreader/copy_sha256.h"
#include "../streamreader/copy_fs.h"
#include <pthread.h>
#include <dirent.h>

#define NRC_WIRE 8224u
#define NRC_HASH_AT 8192u
#define NRC_SELECTION_AT 4096u
#define NRC_STAMP_AT 2512u
#define NRC_STAMP_WORDS 11u
#define NRC_MARKER 128u
#define NRC_MAGIC UINT64_C(0x314b435452434e42)
enum {R_DATA,R_GAMES,R_APPS,R_SOURCE,R_SOURCE_PARAM,R_RECEIPT,R_APP,R_APP_PARAM,R_MOUNT,R_COUNT};
struct stamp {uint64_t v[NRC_STAMP_WORDS];};
struct record {
    unsigned kind;struct bw_nrc_subject subject;
    char selection[BW_READER_SELECTION_BYTES+1u];size_t selection_size;
    uint32_t source_param_size,app_param_size,mount_size;
    unsigned char source_param_hash[32],app_param_hash[32],mount_hash[32];
    struct stamp stamp[R_COUNT];
};
struct anchor {int fd;char path[BW_NRC_PATH_BYTES];struct stat st;};
struct context {struct bw_nrc_environment env;char roots[3][BW_NRC_PATH_BYTES];struct anchor data,games,apps,ledger;};
struct bw_nrc_ticket {uint64_t magic;unsigned finished;struct context c;struct bw_nrc_ops ops;struct record record;unsigned char intent_hash[32];};
static pthread_mutex_t nrc_lock=PTHREAD_MUTEX_INITIALIZER;
static const char ledger_marker[]="Blackwire registration ownership ledger v1\n";

static int result(struct bw_nrc_result *r,enum bw_nrc_state state,const char *why,int rc){if(r){r->state=state;r->reason=why;}return rc;}
static int first_failure(struct bw_nrc_result *r,const char *stage,int raw,int error){
    if(r&&!r->stage){r->stage=stage;r->raw_result=raw;r->error_snapshot=error;}return -1;
}
#define NRC_CALL(d,label,expression) do {int nrc_rc=(expression);if(nrc_rc){first_failure((d),(label),nrc_rc,errno);goto done;}}while(0)
#define NRC_PRED(d,label,expression) do {if(expression){first_failure((d),(label),0,0);goto done;}}while(0)
static int title_valid(const char *s){if(!s||strnlen(s,10)!=9||memcmp(s,"PPSA",4)||!strcmp(s,"PPSA99171")||!strcmp(s,"PPSA99170"))return 0;for(unsigned i=4;i<9;i++)if(s[i]<'0'||s[i]>'9')return 0;return 1;}
static int hex(const char *s,size_t n,unsigned char *out){static const char digits[]="0123456789abcdef";unsigned any=0;if(!s||strnlen(s,n+1)!=n)return -1;for(size_t i=0;i<n;i++){const char *p=strchr(digits,s[i]);if(!p)return -1;unsigned x=(unsigned)(p-digits);any|=x;if(out){if(!(i&1u))out[i/2]=(unsigned char)(x<<4);else out[i/2]|=(unsigned char)x;}}return any?0:-1;}
static int run_valid(const char *s){return !hex(s,32,NULL);}
static int join(char out[BW_NRC_PATH_BYTES],const char *a,const char *b){int n=snprintf(out,BW_NRC_PATH_BYTES,"%s/%s",a,b);return n<0||(unsigned)n>=BW_NRC_PATH_BYTES?-1:0;}
static int leaf(char out[96],const char *title,const char *run,const char *suffix){if(!title_valid(title)||!run_valid(run))return -1;int n=snprintf(out,96,"%s.%s.%s",title,run,suffix);return n<0||n>=96?-1:0;}
static int ops_valid(const struct bw_nrc_ops *o,int uninstall){return o&&o->current&&o->sources&&(!uninstall||o->uninstall);}
static int current(const struct bw_nrc_ops *o,const struct bw_nrc_subject *s){return o->current(o->opaque)||(s&&o->sources(o->opaque,s))?-1:0;}
static int node(const struct stat *a,const struct stat *b){return a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_mode==b->st_mode&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid;}
static int same(const struct stat *a,const struct stat *b){return node(a,b)&&a->st_nlink==b->st_nlink&&a->st_size==b->st_size&&a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;}
static int stamp_set(struct stamp *o,const struct stat *s){
    if(s->st_size<0||s->st_mtim.tv_sec<0||s->st_ctim.tv_sec<0||s->st_mtim.tv_nsec<0||s->st_mtim.tv_nsec>=1000000000L||s->st_ctim.tv_nsec<0||s->st_ctim.tv_nsec>=1000000000L)return -1;
    uint64_t v[]={s->st_dev,s->st_ino,(uint64_t)s->st_size,s->st_mode,s->st_uid,s->st_gid,s->st_nlink,(uint64_t)s->st_mtim.tv_sec,(uint64_t)s->st_mtim.tv_nsec,(uint64_t)s->st_ctim.tv_sec,(uint64_t)s->st_ctim.tv_nsec};memcpy(o->v,v,sizeof v);return 0;
}
static int stamp_same(const struct stamp *a,const struct stat *s,int full){struct stamp b;if(stamp_set(&b,s))return 0;if(full)return !memcmp(a,&b,sizeof b);return a->v[0]==b.v[0]&&a->v[1]==b.v[1]&&a->v[3]==b.v[3]&&a->v[4]==b.v[4]&&a->v[5]==b.v[5];}
static int anchor_current(const struct anchor *a){char real[PATH_MAX];struct stat held,named;return a->fd<0||fstat(a->fd,&held)||lstat(a->path,&named)||!S_ISDIR(held.st_mode)||!node(&a->st,&held)||!node(&held,&named)||!realpath(a->path,real)||strcmp(a->path,real)?-1:0;}
static int anchor_open(struct anchor *a,const char *path,int private){
    char real[PATH_MAX];struct stat named;size_t n=path?strnlen(path,BW_NRC_PATH_BYTES):0;
    if(!n||n>=BW_NRC_PATH_BYTES||path[0]!='/'||!realpath(path,real)||strcmp(path,real)||lstat(path,&named)||!S_ISDIR(named.st_mode))return -1;
    a->fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_NONBLOCK);if(a->fd<0)return -1;
    memcpy(a->path,path,n+1);if(fstat(a->fd,&a->st)||!node(&named,&a->st)||
       (private&&(a->st.st_uid!=geteuid()||(a->st.st_mode&07777)!=0700))||anchor_current(a)){close(a->fd);a->fd=-1;return -1;}return 0;
}
static void context_init(struct context *c){memset(c,0,sizeof *c);c->data.fd=c->games.fd=c->apps.fd=c->ledger.fd=-1;}
static int context_close(struct context *c){int bad=0;struct anchor *a[]={&c->ledger,&c->apps,&c->games,&c->data};for(size_t i=0;i<4;i++)if(a[i]->fd>=0){if(close(a[i]->fd))bad=1;a[i]->fd=-1;}return bad?-1:0;}
static int context_base(struct context *c,const struct bw_nrc_environment *e){
    if(!e||!e->data_root||!e->apps_root||!e->database)return -1;const char *p[]={e->data_root,e->apps_root,e->database};
    for(unsigned i=0;i<3;i++){size_t n=strnlen(p[i],BW_NRC_PATH_BYTES);if(!n||n>=BW_NRC_PATH_BYTES)return -1;memcpy(c->roots[i],p[i],n+1);}
    c->env=(struct bw_nrc_environment){c->roots[0],c->roots[1],c->roots[2]};char path[BW_NRC_PATH_BYTES];
    return anchor_open(&c->data,c->env.data_root,0)||join(path,c->env.data_root,"games")||anchor_open(&c->games,path,0)||anchor_open(&c->apps,c->env.apps_root,0)?-1:0;
}
static int anchors_current(struct context *c){return anchor_current(&c->data)||anchor_current(&c->games)||anchor_current(&c->apps)||(c->ledger.fd>=0&&anchor_current(&c->ledger))?-1:0;}
static int read_exact(struct context *c,const char *name,void *out,size_t size){
    int fd=v2_openat(c->ledger.fd,name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0)return errno==ENOENT?1:-1;
    struct stat first,held,named;int bad=fstat(fd,&first)||!S_ISREG(first.st_mode)||first.st_nlink!=1||(first.st_mode&07777)!=0600||first.st_uid!=geteuid()||first.st_size<0||(uint64_t)first.st_size!=size;
    size_t at=0;while(!bad&&at<size){ssize_t n=pread(fd,(unsigned char *)out+at,size-at,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>size-at){bad=1;break;}at+=(size_t)n;}
    if(!bad&&(fstat(fd,&held)||cp_fstatat(c->ledger.fd,name,&named,AT_SYMLINK_NOFOLLOW)||!same(&first,&held)||!same(&held,&named)||anchors_current(c)))bad=1;
    if(close(fd))bad=1;return bad?-1:0;
}
static int write_new(struct context *c,const char *name,const void *bytes,size_t size,const struct bw_nrc_ops *ops,const struct bw_nrc_subject *subject){
    if(anchors_current(c)||current(ops,subject))return -1;
    int fd=v2_openat(c->ledger.fd,name,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR|O_NONBLOCK,0600);if(fd<0)return -1;
    int bad=fchmod(fd,0600);size_t at=0;while(!bad&&at<size){if(anchors_current(c)||current(ops,subject)){bad=1;break;}ssize_t n=write(fd,(const unsigned char *)bytes+at,size-at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>size-at){bad=1;break;}at+=(size_t)n;}
    if(!bad&&(fsync(fd)||anchors_current(c)||current(ops,subject)))bad=1;
    if(close(fd))bad=1;
    void *check=malloc(size);if(!check)bad=1;
    if(!bad&&(read_exact(c,name,check,size)||memcmp(check,bytes,size)||fsync(c->ledger.fd)||anchors_current(c)||current(ops,subject)))bad=1;
    free(check);return bad?-1:0; /* Every partial name is retained; no retry/delete. */
}
static int ledger_open(struct context *c,int create,const struct bw_nrc_ops *ops,const struct bw_nrc_subject *subject){
    char path[BW_NRC_PATH_BYTES];struct stat st;if(join(path,c->env.data_root,BW_NRC_LEDGER_LEAF))return -1;
    int made=0;if(lstat(path,&st)){if(errno!=ENOENT)return -1;if(!create)return 1;if(current(ops,subject)||anchors_current(c)||mkdir(path,0700))return -1;made=1;}
    if(anchor_open(&c->ledger,path,1))return -1;
    if(made){if(write_new(c,".blackwire-registration-owners",ledger_marker,sizeof ledger_marker-1,ops,subject)||fsync(c->data.fd))return -1;}
    else{char check[sizeof ledger_marker-1];if(read_exact(c,".blackwire-registration-owners",check,sizeof check)||memcmp(check,ledger_marker,sizeof check))return -1;}
    return 0;
}
static void put32(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(8*i));}
static void put64(unsigned char *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(n>>(8*i));}
static uint32_t get32(const unsigned char *p){uint32_t n=0;for(unsigned i=0;i<4;i++)n|=(uint32_t)p[i]<<(8*i);return n;}
static uint64_t get64(const unsigned char *p){uint64_t n=0;for(unsigned i=0;i<8;i++)n|=(uint64_t)p[i]<<(8*i);return n;}
static int subject_init(struct record *r,const struct context *c,const struct bw_reader_selection *s){
    if(!s||s->source_kind!=BW_PROVIDER_FOLDER||s->source_policy!=BW_PROVIDER_POLICY_V1||s->prepared_policy||!title_valid(s->title)||!run_valid(s->run)||hex(s->prepared_record_hex,64,r->subject.prepared_record_sha256)||
       bw_native_selected_root(s,r->subject.game)||bw_reader_selection_serialize(s,r->selection,sizeof r->selection,&r->selection_size)||!r->selection_size||r->selection_size>BW_READER_SELECTION_BYTES)return -1;
    memcpy(r->subject.title,s->title,10);memcpy(r->subject.run,s->run,33);cp_sha_bytes(r->selection,r->selection_size,r->subject.selection_sha256);
    return join(r->subject.source,c->games.path,r->subject.game);
}
static void encode(const struct record *r,unsigned char out[NRC_WIRE]){
    memset(out,0,NRC_WIRE);memcpy(out,"BWROC001",8);put32(out+8,1);put32(out+12,NRC_WIRE);put32(out+16,r->kind);put32(out+20,(uint32_t)r->selection_size);put32(out+24,r->source_param_size);put32(out+28,r->app_param_size);
    memcpy(out+32,r->subject.title,10);memcpy(out+48,r->subject.run,33);memcpy(out+96,r->subject.game,75);memcpy(out+176,r->subject.content,128);memcpy(out+304,r->subject.source,BW_NRC_PATH_BYTES);
    memcpy(out+2352,r->subject.prepared_record_sha256,32);memcpy(out+2384,r->subject.selection_sha256,32);memcpy(out+2416,r->source_param_hash,32);memcpy(out+2448,r->app_param_hash,32);memcpy(out+2480,r->mount_hash,32);
    for(unsigned i=0;i<R_COUNT;i++)for(unsigned j=0;j<NRC_STAMP_WORDS;j++)put64(out+NRC_STAMP_AT+8u*(NRC_STAMP_WORDS*i+j),r->stamp[i].v[j]);put32(out+3328,r->mount_size);
    memcpy(out+NRC_SELECTION_AT,r->selection,r->selection_size);cp_sha_bytes(out,NRC_HASH_AT,out+NRC_HASH_AT);
}
static int decode(struct record *r,const struct context *c,const unsigned char in[NRC_WIRE],const char *title,const char *run){
    unsigned char hash[32];cp_sha_bytes(in,NRC_HASH_AT,hash);uint32_t n=get32(in+20);
    if(memcmp(in,"BWROC001",8)||get32(in+8)!=1||get32(in+12)!=NRC_WIRE||get32(in+16)!=2||!n||n>BW_READER_SELECTION_BYTES||memcmp(hash,in+NRC_HASH_AT,32))return -1;
    struct bw_reader_selection selected;if(bw_reader_selection_parse(&selected,in+NRC_SELECTION_AT,n))return -1;
    memset(r,0,sizeof *r);if(subject_init(r,c,&selected)||strcmp(r->subject.title,title)||strcmp(r->subject.run,run))return -1;
    if(!memchr(in+176,0,128)||!in[176])return -1;size_t content_size=strlen((const char *)in+176);for(size_t i=content_size+1;i<128;i++)if(in[176+i])return -1;
    memcpy(r->subject.content,in+176,128);r->kind=2;r->source_param_size=get32(in+24);r->app_param_size=get32(in+28);r->mount_size=get32(in+3328);
    if(!r->source_param_size||r->source_param_size>BW_SOURCE_PARAM_LIMIT||!r->app_param_size||r->app_param_size>BW_SOURCE_PARAM_LIMIT||!r->mount_size||r->mount_size>BW_NRC_PATH_BYTES+1u)return -1;
    memcpy(r->source_param_hash,in+2416,32);memcpy(r->app_param_hash,in+2448,32);memcpy(r->mount_hash,in+2480,32);
    for(unsigned i=0;i<R_COUNT;i++)for(unsigned j=0;j<NRC_STAMP_WORDS;j++)r->stamp[i].v[j]=get64(in+NRC_STAMP_AT+8u*(NRC_STAMP_WORDS*i+j));
    unsigned char *again=malloc(NRC_WIRE);if(!again)return -1;encode(r,again);int bad=memcmp(in,again,NRC_WIRE);free(again);return bad?-1:0;
}
struct blob {unsigned char *bytes;size_t size;struct stat st;unsigned char hash[32];};
static void blob_free(struct blob *b){free(b->bytes);memset(b,0,sizeof *b);}
static int blob_read(const char *path,size_t maximum,struct blob *b){
    memset(b,0,sizeof *b);char real[PATH_MAX];struct stat held,named;int fd=-1,bad=1;
    if(!realpath(path,real)||strcmp(path,real))return -1;fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
    if(fstat(fd,&b->st)||!S_ISREG(b->st.st_mode)||b->st.st_nlink!=1||b->st.st_size<=0||(uint64_t)b->st.st_size>maximum)goto done;
    b->size=(size_t)b->st.st_size;b->bytes=malloc(b->size);if(!b->bytes)goto done;size_t at=0;
    while(at<b->size){ssize_t n=pread(fd,b->bytes+at,b->size-at,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>b->size-at)goto done;at+=(size_t)n;}
    cp_sha_bytes(b->bytes,b->size,b->hash);if(fstat(fd,&held)||lstat(path,&named)||!same(&b->st,&held)||!same(&held,&named)||!realpath(path,real)||strcmp(path,real))goto done;bad=0;
done:if(close(fd))bad=1;if(bad)blob_free(b);return bad?-1:0;
}
static int path_dir(const char *path,struct stat *out){struct anchor a={.fd=-1};int bad=anchor_open(&a,path,0);if(!bad){*out=a.st;if(close(a.fd))bad=1;}return bad?-1:0;}
static int source_capture(struct context *c,struct record *r,int compare){
    char path[BW_NRC_PATH_BYTES],title[10],content[128];struct stat st;struct blob param={0},receipt={0};int bad=1;
    if(path_dir(r->subject.source,&st)||join(path,r->subject.source,"sce_sys/param.json")||blob_read(path,BW_SOURCE_PARAM_LIMIT,&param)||
       bw_source_param_identity(param.bytes,param.size,title,content)||strcmp(title,r->subject.title))goto done;
    int n=snprintf(path,sizeof path,"%s/native-prepared/%s/prepared.bin",c->env.data_root,r->subject.game);
    if(n<0||(size_t)n>=sizeof path||blob_read(path,384,&receipt)||receipt.size!=384||(receipt.st.st_mode&07777)!=0600||memcmp(receipt.hash,r->subject.prepared_record_sha256,32))goto done;
    if(compare){if(!stamp_same(&r->stamp[R_SOURCE],&st,1)||!stamp_same(&r->stamp[R_SOURCE_PARAM],&param.st,1)||!stamp_same(&r->stamp[R_RECEIPT],&receipt.st,1)||
        param.size!=r->source_param_size||memcmp(param.hash,r->source_param_hash,32)||strcmp(content,r->subject.content))goto done;}
    else {if(stamp_set(&r->stamp[R_SOURCE],&st)||stamp_set(&r->stamp[R_SOURCE_PARAM],&param.st)||stamp_set(&r->stamp[R_RECEIPT],&receipt.st))goto done;
        r->source_param_size=(uint32_t)param.size;memcpy(r->source_param_hash,param.hash,32);memcpy(r->subject.content,content,strlen(content)+1);}
    bad=0;
done:blob_free(&param);blob_free(&receipt);return bad?-1:0;
}
static int app_absent(struct context *c,const struct record *r){
    struct stat st;if(anchors_current(c)||bw_registration_db_match(c->env.database,r->subject.title,r->subject.content)!=0)return -1;
    if(cp_fstatat(c->apps.fd,r->subject.title,&st,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT||anchors_current(c))return -1;return 0;
}
static int no_file(const char *path){struct stat st;if(!lstat(path,&st))return -1;return errno==ENOENT?0:-1;}
static int mount_matches(const struct blob *b,const char *source){size_t n=b->size;if(n&&b->bytes[n-1]=='\n')n--;if(n&&b->bytes[n-1]=='\r')n--;return n==strlen(source)&&!memcmp(b->bytes,source,n)?0:-1;}
static int registration_capture(struct context *c,struct record *r,const struct bw_reader_selection *selected,struct bw_nrc_result *diagnostic){
    char app[BW_NRC_PATH_BYTES],path[BW_NRC_PATH_BYTES],title[10],content[128];struct stat st;struct blob source={0},param={0},mount={0};int bad=1;
    NRC_CALL(diagnostic,"ownership_capture_registration",bw_native_registration_verify(c->env.data_root,c->env.apps_root,c->env.database,selected));
    NRC_CALL(diagnostic,"ownership_capture_app_path",join(app,c->env.apps_root,r->subject.title));
    NRC_CALL(diagnostic,"ownership_capture_app_directory",path_dir(app,&st));
    NRC_CALL(diagnostic,"ownership_capture_app_param_path",join(path,app,"sce_sys/param.json"));
    NRC_CALL(diagnostic,"ownership_capture_app_param",blob_read(path,BW_SOURCE_PARAM_LIMIT,&param));
    NRC_CALL(diagnostic,"ownership_capture_app_identity",bw_source_param_identity(param.bytes,param.size,title,content));
    NRC_PRED(diagnostic,"ownership_capture_title_content",strcmp(title,r->subject.title)||strcmp(content,r->subject.content));
    NRC_CALL(diagnostic,"ownership_capture_source_param_path",join(path,r->subject.source,"sce_sys/param.json"));
    NRC_CALL(diagnostic,"ownership_capture_source_param",blob_read(path,BW_SOURCE_PARAM_LIMIT,&source));
    NRC_PRED(diagnostic,"ownership_capture_param_pair",bw_registration_param_pair(source.bytes,source.size,param.bytes,param.size)<0);
    NRC_CALL(diagnostic,"ownership_capture_mount_path",join(path,app,"mount.lnk"));
    NRC_CALL(diagnostic,"ownership_capture_mount",blob_read(path,BW_NRC_PATH_BYTES+1u,&mount));
    NRC_CALL(diagnostic,"ownership_capture_mount_target",mount_matches(&mount,r->subject.source));
    NRC_CALL(diagnostic,"ownership_capture_image_path",join(path,app,"mount_img.lnk"));
    NRC_CALL(diagnostic,"ownership_capture_image_absence",no_file(path));
    NRC_CALL(diagnostic,"ownership_capture_long_image_path",join(path,app,"mount_image.lnk"));
    NRC_CALL(diagnostic,"ownership_capture_long_image_absence",no_file(path));
    NRC_CALL(diagnostic,"ownership_capture_app_stamp",stamp_set(&r->stamp[R_APP],&st));
    NRC_CALL(diagnostic,"ownership_capture_param_stamp",stamp_set(&r->stamp[R_APP_PARAM],&param.st));
    NRC_CALL(diagnostic,"ownership_capture_mount_stamp",stamp_set(&r->stamp[R_MOUNT],&mount.st));
    r->app_param_size=(uint32_t)param.size;r->mount_size=(uint32_t)mount.size;memcpy(r->app_param_hash,param.hash,32);memcpy(r->mount_hash,mount.hash,32);bad=0;
done:blob_free(&source);blob_free(&param);blob_free(&mount);return bad?-1:0;
}
int bw_nrc_begin(const struct bw_nrc_environment *e,const struct bw_reader_selection *s,const struct bw_nrc_ops *ops,struct bw_nrc_ticket **out,struct bw_nrc_result *r){
    if(out)*out=NULL;if(r)memset(r,0,sizeof *r);if(!out||!r||!ops_valid(ops,0)||pthread_mutex_lock(&nrc_lock))return -1;
    int rc=-1;struct bw_nrc_ticket *t=calloc(1,sizeof *t);unsigned char *wire=NULL;if(!t)goto done;context_init(&t->c);t->ops=*ops;
    NRC_CALL(r,"ownership_intent_anchors",context_base(&t->c,e));
    NRC_CALL(r,"ownership_intent_selection",subject_init(&t->record,&t->c,s));
    NRC_CALL(r,"ownership_intent_initial_current",current(ops,NULL));
    NRC_CALL(r,"ownership_intent_source_capture",source_capture(&t->c,&t->record,0));
    NRC_CALL(r,"ownership_intent_current_sources",current(ops,&t->record.subject));
    NRC_CALL(r,"ownership_intent_named_anchors",anchors_current(&t->c));
    if(bw_native_registration_state(e->data_root,e->apps_root,e->database,s)!=BW_REG_ABSENT||app_absent(&t->c,&t->record)||current(ops,&t->record.subject)||
       bw_native_registration_state(e->data_root,e->apps_root,e->database,s)!=BW_REG_ABSENT||app_absent(&t->c,&t->record)){rc=1;result(r,BW_NRC_UNOWNED,"initial_registration_not_absent_no_adoption",rc);goto done;}
    NRC_CALL(r,"ownership_intent_ledger",ledger_open(&t->c,1,ops,&t->record.subject));
    NRC_CALL(r,"ownership_intent_data_stamp",stamp_set(&t->record.stamp[R_DATA],&t->c.data.st));
    NRC_CALL(r,"ownership_intent_games_stamp",stamp_set(&t->record.stamp[R_GAMES],&t->c.games.st));
    NRC_CALL(r,"ownership_intent_apps_stamp",stamp_set(&t->record.stamp[R_APPS],&t->c.apps.st));
    t->record.kind=1;wire=malloc(NRC_WIRE);char name[96];NRC_PRED(r,"ownership_intent_memory",!wire);
    NRC_CALL(r,"ownership_intent_leaf",leaf(name,s->title,s->run,"intent"));encode(&t->record,wire);
    NRC_CALL(r,"ownership_intent_write_readback",write_new(&t->c,name,wire,NRC_WIRE,ops,&t->record.subject));
    NRC_CALL(r,"ownership_intent_source_unchanged",source_capture(&t->c,&t->record,1));
    NRC_CALL(r,"ownership_intent_final_absence",app_absent(&t->c,&t->record));
    memcpy(t->intent_hash,wire+NRC_HASH_AT,32);t->magic=NRC_MAGIC;*out=t;t=NULL;rc=0;result(r,BW_NRC_INTENT,"new_registration_intent_durable",0);
done:free(wire);if(t){if(context_close(&t->c))rc=BW_NRC_RELEASE_UNCONFIRMED;free(t);}if(rc<0)result(r,BW_NRC_UNKNOWN,r->stage?r->stage:"registration_ownership_intent_unconfirmed",rc);pthread_mutex_unlock(&nrc_lock);return rc;
}
int bw_nrc_finish(struct bw_nrc_ticket *t,int registration_status,const struct bw_register_result *registered,struct bw_nrc_result *r){
    if(r)memset(r,0,sizeof *r);if(!t||!r||pthread_mutex_lock(&nrc_lock))return -1;int rc=-1;unsigned char *wire=NULL,*intent=NULL;char name[96];
    NRC_PRED(r,"ownership_commit_ticket",t->magic!=NRC_MAGIC||t->finished);t->finished=1;
    NRC_PRED(r,"ownership_commit_registration_result",registration_status||!registered||registered->registered!=1||(!registered->add_attempted&&!registered->scan_attempted)||registered->add_attempted>1||registered->scan_attempted>1||registered->add_confirmed>1||
       (registered->add_confirmed&&!registered->add_attempted)||(registered->add_attempted&&registered->scan_attempted));
    NRC_CALL(r,"ownership_commit_current_sources",current(&t->ops,&t->record.subject));
    NRC_CALL(r,"ownership_commit_anchors",anchors_current(&t->c));
    wire=malloc(NRC_WIRE);intent=malloc(NRC_WIRE);NRC_PRED(r,"ownership_commit_memory",!wire||!intent);
    NRC_CALL(r,"ownership_commit_intent_leaf",leaf(name,t->record.subject.title,t->record.subject.run,"intent"));
    NRC_CALL(r,"ownership_commit_intent_readback",read_exact(&t->c,name,intent,NRC_WIRE));
    encode(&t->record,wire);NRC_PRED(r,"ownership_commit_intent_bytes",memcmp(intent,wire,NRC_WIRE)||memcmp(intent+NRC_HASH_AT,t->intent_hash,32));
    NRC_CALL(r,"ownership_commit_source_unchanged",source_capture(&t->c,&t->record,1));
    struct bw_reader_selection selected;
    NRC_CALL(r,"ownership_commit_selection",bw_reader_selection_parse(&selected,t->record.selection,t->record.selection_size));
    NRC_CALL(r,"ownership_commit_registration_capture",registration_capture(&t->c,&t->record,&selected,r));
    NRC_CALL(r,"ownership_commit_final_current_sources",current(&t->ops,&t->record.subject));
    NRC_CALL(r,"ownership_commit_final_source_unchanged",source_capture(&t->c,&t->record,1));
    NRC_CALL(r,"ownership_commit_final_registration",bw_native_registration_verify(t->c.env.data_root,t->c.env.apps_root,t->c.env.database,&selected));
    NRC_CALL(r,"ownership_commit_final_anchors",anchors_current(&t->c));
    t->record.kind=2;encode(&t->record,wire);
    NRC_CALL(r,"ownership_commit_owner_leaf",leaf(name,t->record.subject.title,t->record.subject.run,"owner"));
    NRC_CALL(r,"ownership_commit_owner_write_readback",write_new(&t->c,name,wire,NRC_WIRE,&t->ops,&t->record.subject));
    rc=0;result(r,BW_NRC_OWNED,"exact_registration_ownership_committed",0);
done:free(wire);free(intent);if(rc)result(r,BW_NRC_UNKNOWN,r->stage?r->stage:"registration_ownership_commit_unconfirmed",rc);pthread_mutex_unlock(&nrc_lock);return rc;
}
int bw_nrc_ticket_destroy(struct bw_nrc_ticket *t){if(!t)return 0;if(pthread_mutex_lock(&nrc_lock))return -1;int rc=context_close(&t->c);memset(t,0,sizeof *t);free(t);pthread_mutex_unlock(&nrc_lock);return rc;}
static void marker(unsigned char out[NRC_MARKER],const char magic[8],const unsigned char owner[32],const struct bw_nrc_subject *s){memset(out,0,NRC_MARKER);memcpy(out,magic,8);memcpy(out+16,owner,32);memcpy(out+48,s->run,33);memcpy(out+82,s->title,10);cp_sha_bytes(out,96,out+96);}
static int marker_read(struct context *c,const char *name,const unsigned char expected[NRC_MARKER]){unsigned char b[NRC_MARKER];int rc=read_exact(c,name,b,sizeof b);return rc?rc:memcmp(b,expected,sizeof b)?-1:0;}
static int record_anchors(struct context *c,const struct record *r){
    return anchors_current(c)||!stamp_same(&r->stamp[R_DATA],&c->data.st,0)||!stamp_same(&r->stamp[R_GAMES],&c->games.st,0)||!stamp_same(&r->stamp[R_APPS],&c->apps.st,0)?-1:0;
}
static int source_missing(struct context *c,const struct record *r){
    struct stat st;if(record_anchors(c,r))return -1;
    if(cp_fstatat(c->games.fd,r->subject.game,&st,AT_SYMLINK_NOFOLLOW)==0)return S_ISLNK(st.st_mode)?-1:1;
    return errno==ENOENT&&!anchors_current(c)?0:-1;
}
static int registration_current(struct context *c,const struct record *r,unsigned *missing_mount){
    char app[BW_NRC_PATH_BYTES],path[BW_NRC_PATH_BYTES],title[10],content[128];struct stat st,after;struct blob param={0},link={0};int bad=1;*missing_mount=0;
    if(join(app,c->env.apps_root,r->subject.title)||path_dir(app,&st)||!stamp_same(&r->stamp[R_APP],&st,0)||join(path,app,"sce_sys/param.json")||blob_read(path,BW_SOURCE_PARAM_LIMIT,&param)||
       param.size!=r->app_param_size||!stamp_same(&r->stamp[R_APP_PARAM],&param.st,1)||memcmp(param.hash,r->app_param_hash,32)||bw_source_param_identity(param.bytes,param.size,title,content)||strcmp(title,r->subject.title)||strcmp(content,r->subject.content)||
       bw_registration_db_match(c->env.database,title,content)!=1||join(path,app,"mount_img.lnk")||no_file(path)||join(path,app,"mount_image.lnk")||no_file(path)||join(path,app,"mount.lnk"))goto done;
    if(lstat(path,&after)){if(errno!=ENOENT)goto done;*missing_mount=1;}
    else if(blob_read(path,BW_NRC_PATH_BYTES+1u,&link)||link.size!=r->mount_size||!stamp_same(&r->stamp[R_MOUNT],&link.st,1)||memcmp(link.hash,r->mount_hash,32)||mount_matches(&link,r->subject.source))goto done;
    if(path_dir(app,&after)||!node(&st,&after)||anchors_current(c))goto done;bad=0;
done:blob_free(&param);blob_free(&link);return bad?-1:0;
}
int bw_nrc_reconcile(const struct bw_nrc_environment *e,const char title[10],const char run[33],const struct bw_nrc_ops *ops,struct bw_nrc_result *r){
    if(r)memset(r,0,sizeof *r);if(!r||!title_valid(title)||!run_valid(run)||!ops_valid(ops,1)||pthread_mutex_lock(&nrc_lock))return -1;
    int rc=-1;struct context c;context_init(&c);struct record *record=calloc(1,sizeof *record);unsigned char *wire=malloc(NRC_WIRE);char name[96];
    if(!record||!wire||!e||!e->data_root)goto done;
    char ledger_path[BW_NRC_PATH_BYTES];struct stat ledger_stat;if(join(ledger_path,e->data_root,BW_NRC_LEDGER_LEAF))goto done;
    if(lstat(ledger_path,&ledger_stat)){if(errno==ENOENT)rc=result(r,BW_NRC_UNOWNED,"no_ownership_ledger_no_action",0);goto done;}
    if(current(ops,NULL)||context_base(&c,e))goto done;
    int opened=ledger_open(&c,0,ops,NULL);if(opened==1){rc=result(r,BW_NRC_UNOWNED,"no_ownership_ledger_no_action",0);goto done;}if(opened)goto done;
    if(leaf(name,title,run,"owner"))goto done;int loaded=read_exact(&c,name,wire,NRC_WIRE);if(loaded==1){rc=result(r,BW_NRC_UNOWNED,"no_committed_ownership_no_action",0);goto done;}if(loaded||decode(record,&c,wire,title,run)||record_anchors(&c,record)||current(ops,&record->subject))goto done;
    unsigned char expected[NRC_MARKER];marker(expected,"BWROCA01",wire+NRC_HASH_AT,&record->subject);if(leaf(name,title,run,"absent"))goto done;int status=marker_read(&c,name,expected);
    if(!status){rc=result(r,BW_NRC_ABSENT,"prior_lifecycle_absence_already_verified",0);goto done;}if(status<0)goto done;
    marker(expected,"BWROCR01",wire+NRC_HASH_AT,&record->subject);if(leaf(name,title,run,"retire"))goto done;
    int transient=marker_read(&c,name,expected);if(transient<0)goto done;
    if(!transient){rc=result(r,BW_NRC_PENDING,"transient_retirement_requires_its_separate_observer",0);goto done;}
    marker(expected,"BWROCP01",wire+NRC_HASH_AT,&record->subject);if(leaf(name,title,run,"pending"))goto done;int pending=marker_read(&c,name,expected);if(pending<0)goto done;
    if(!pending){
        rc=result(r,BW_NRC_PENDING,"uninstall_outcome_pending_observe_only",0);
        if(app_absent(&c,record)||current(ops,&record->subject)||app_absent(&c,record)||current(ops,&record->subject))goto done;
        marker(expected,"BWROCA01",wire+NRC_HASH_AT,&record->subject);if(leaf(name,title,run,"absent")||write_new(&c,name,expected,sizeof expected,ops,&record->subject)){rc=-1;goto done;}
        rc=result(r,BW_NRC_ABSENT,"app_directory_and_title_row_absent_twice",0);goto done;
    }
    int missing=source_missing(&c,record);if(missing>0){rc=result(r,BW_NRC_BLOCKED,"owned_source_still_present",0);goto done;}if(missing<0)goto done;
    unsigned first=0,second=0;if(registration_current(&c,record,&first)||current(ops,&record->subject)||source_missing(&c,record)||registration_current(&c,record,&second)||first!=second||current(ops,&record->subject))goto done;
    r->missing_mount_link=second;
    /* Even a final refusal AFTER this durable marker is never retried: crash
     * or network ambiguity cannot distinguish sent from unsent later. */
    if(leaf(name,title,run,"pending")||write_new(&c,name,expected,sizeof expected,ops,&record->subject))goto done;
    rc=result(r,BW_NRC_PENDING,"uninstall_intent_durable_no_retry",0);
    if(source_missing(&c,record)||registration_current(&c,record,&second)||current(ops,&record->subject))goto done;
    r->uninstall_attempted=1;int requested=ops->uninstall(ops->opaque,record->subject.title);r->uninstall_accepted=requested==0;
    r->reason=requested==0?"normal_uninstall_requested_absence_unverified":requested==1?"normal_uninstall_refused_no_retry":"normal_uninstall_outcome_unknown_no_retry";
done:
    if(context_close(&c))rc=-1;free(record);free(wire);if(rc<0)result(r,BW_NRC_UNKNOWN,"ownership_or_missing_source_observation_unknown",rc);pthread_mutex_unlock(&nrc_lock);return rc;
}
static int candidate_order(const void *a,const void *b){const struct bw_nrc_candidate *x=a,*y=b;unsigned xp=x->pending||x->retiring,yp=y->pending||y->retiring;if(xp!=yp)return xp?-1:1;int n=strcmp(x->subject.title,y->subject.title);return n?n:strcmp(x->subject.run,y->subject.run);}
static int parse_name(const char *name,char title[10],char run[33],const char **suffix){
    size_t n=strnlen(name,96);if(n<46||n>=96||name[9]!='.'||name[42]!='.')return -1;memcpy(title,name,9);title[9]=0;memcpy(run,name+10,32);run[32]=0;
    if(!title_valid(title)||!run_valid(run))return -1;*suffix=name+43;return !strcmp(*suffix,"owner")||!strcmp(*suffix,"intent")||!strcmp(*suffix,"pending")||!strcmp(*suffix,"absent")||!strcmp(*suffix,"retire")||!strcmp(*suffix,"uninstall")?0:-1;
}
int bw_nrc_candidates(const struct bw_nrc_environment *e,struct bw_nrc_candidate *out,size_t capacity,size_t *count){
    if(count)*count=0;if(!e||!e->data_root||!out||!count||!capacity||capacity>BW_NRC_CANDIDATE_LIMIT||pthread_mutex_lock(&nrc_lock))return -1;
    memset(out,0,capacity*sizeof *out);int rc=-1;size_t used=0,entries=0;struct context c;context_init(&c);DIR *dir=NULL;unsigned char *wire=NULL;struct record *r=NULL;char path[BW_NRC_PATH_BYTES];struct stat before,after;
    if(join(path,e->data_root,BW_NRC_LEDGER_LEAF))goto done;
    if(lstat(path,&before)){if(errno==ENOENT)rc=0;goto done;}
    if(context_base(&c,e)||ledger_open(&c,0,NULL,NULL)||fstat(c.ledger.fd,&before))goto done;
    dir=opendir(c.ledger.path);wire=malloc(NRC_WIRE);r=calloc(1,sizeof *r);if(!dir||!wire||!r)goto done;
    struct dirent *entry;errno=0;
    while((entry=readdir(dir))){
        if(++entries>4096u)goto done;
        if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,"..")||!strcmp(entry->d_name,".blackwire-registration-owners")){errno=0;continue;}
        char title[10],run[33],name[96];const char *suffix;if(parse_name(entry->d_name,title,run,&suffix))goto done;
        if(strcmp(suffix,"owner")){errno=0;continue;}
        if(read_exact(&c,entry->d_name,wire,NRC_WIRE)||decode(r,&c,wire,title,run))goto done;
        unsigned char expected[NRC_MARKER];marker(expected,"BWROCA01",wire+NRC_HASH_AT,&r->subject);if(leaf(name,title,run,"absent"))goto done;
        int resolved=marker_read(&c,name,expected);if(resolved<0)goto done;if(!resolved){errno=0;continue;}
        marker(expected,"BWROCP01",wire+NRC_HASH_AT,&r->subject);if(leaf(name,title,run,"pending"))goto done;int pending=marker_read(&c,name,expected);if(pending<0||used==capacity)goto done;
        marker(expected,"BWROCR01",wire+NRC_HASH_AT,&r->subject);if(leaf(name,title,run,"retire"))goto done;int retiring=marker_read(&c,name,expected);if(retiring<0)goto done;
        out[used++]=(struct bw_nrc_candidate){r->subject,pending==0,retiring==0};errno=0;
    }
    if(errno||fstat(c.ledger.fd,&after)||!same(&before,&after)||anchors_current(&c))goto done;
    rc=0;qsort(out,used,sizeof *out,candidate_order);*count=used;
done:if(dir&&closedir(dir))rc=-1;if(context_close(&c))rc=-1;free(wire);free(r);if(rc){memset(out,0,capacity*sizeof *out);*count=0;}pthread_mutex_unlock(&nrc_lock);return rc;
}

static int superseded_current(struct context *c,const struct record *old,struct record *fresh,
    const struct bw_nrc_generation_observer *o){
    return o->current(o->opaque)||record_anchors(c,old)||source_capture(c,fresh,1)||
        o->manual_present(o->opaque,&old->subject)!=0||o->sources(o->opaque,&old->subject)||
        app_absent(c,old)||o->current(o->opaque)||source_capture(c,fresh,1)||record_anchors(c,old)?-1:0;
}
int bw_nrc_observe_superseded(const struct bw_nrc_environment *e,const struct bw_nrc_subject *expected,
    const struct bw_reader_selection *selected,const struct bw_nrc_generation_observer *o,struct bw_nrc_result *out){
    if(out)memset(out,0,sizeof *out);
    if(!e||!expected||!selected||!out||!o||!o->current||!o->sources||!o->manual_present||
       !title_valid(expected->title)||!run_valid(expected->run)||pthread_mutex_lock(&nrc_lock))return -1;
    struct context c;context_init(&c);struct record *old=calloc(1,sizeof *old),*fresh=calloc(1,sizeof *fresh);
    unsigned char *wire=malloc(NRC_WIRE);struct bw_reader_selection original;char name[96];int rc=-1,publication_unknown=0;
    struct bw_nrc_ops base={.opaque=o->opaque,.current=o->current,.sources=o->sources};
    NRC_PRED(out,"superseded_memory",!old||!fresh||!wire);
    NRC_CALL(out,"superseded_initial_control",o->current(o->opaque));
    NRC_CALL(out,"superseded_anchors",context_base(&c,e));
    NRC_CALL(out,"superseded_ledger",ledger_open(&c,0,&base,NULL));
    NRC_CALL(out,"superseded_owner_leaf",leaf(name,expected->title,expected->run,"owner"));
    NRC_CALL(out,"superseded_owner_read",read_exact(&c,name,wire,NRC_WIRE));
    NRC_CALL(out,"superseded_owner_decode",decode(old,&c,wire,expected->title,expected->run));
    NRC_PRED(out,"superseded_owner_changed",memcmp(&old->subject,expected,sizeof *expected));
    NRC_CALL(out,"superseded_original_selection",bw_reader_selection_parse(&original,old->selection,old->selection_size));
    NRC_CALL(out,"superseded_fresh_selection",subject_init(fresh,&c,selected));
    NRC_PRED(out,"superseded_origin_conflict",strcmp(old->subject.title,fresh->subject.title)||
        strcmp(old->subject.game,fresh->subject.game)||strcmp(old->subject.source,fresh->subject.source)||
        strcmp(original.source,selected->source)||strcmp(original.binding_hex,selected->binding_hex)||
        original.source_kind!=selected->source_kind||original.source_policy!=selected->source_policy||
        original.prepared_policy!=selected->prepared_policy||!strcmp(old->subject.run,selected->run)||
        !memcmp(old->subject.prepared_record_sha256,fresh->subject.prepared_record_sha256,32));
    NRC_CALL(out,"superseded_fresh_source_capture",source_capture(&c,fresh,0));
    NRC_PRED(out,"superseded_content_conflict",strcmp(old->subject.content,fresh->subject.content));
    const char *suffixes[]={"pending","retire","uninstall"};const char *magics[]={"BWROCP01","BWROCR01","BWROCU01"};
    unsigned char marker_bytes[NRC_MARKER];
    for(unsigned i=0;i<3;i++){
        marker(marker_bytes,magics[i],wire+NRC_HASH_AT,&old->subject);
        NRC_CALL(out,"superseded_prior_phase_leaf",leaf(name,expected->title,expected->run,suffixes[i]));
        NRC_PRED(out,"superseded_prior_phase_not_absent",marker_read(&c,name,marker_bytes)!=1);
    }
    NRC_CALL(out,"superseded_first_absence",superseded_current(&c,old,fresh,o));
    NRC_CALL(out,"superseded_second_absence",superseded_current(&c,old,fresh,o));
    marker(marker_bytes,"BWROCA01",wire+NRC_HASH_AT,&old->subject);
    NRC_CALL(out,"superseded_absent_leaf",leaf(name,expected->title,expected->run,"absent"));
    int prior=marker_read(&c,name,marker_bytes);NRC_PRED(out,"superseded_absent_record_unknown",prior<0);
    if(prior){int written=write_new(&c,name,marker_bytes,sizeof marker_bytes,&base,&old->subject);
        if(written){publication_unknown=1;first_failure(out,"superseded_absent_write_readback",written,errno);goto done;}}
    NRC_CALL(out,"superseded_final_absence",superseded_current(&c,old,fresh,o));
    NRC_CALL(out,"superseded_completed_absence",superseded_current(&c,old,fresh,o));
    rc=result(out,BW_NRC_ABSENT,"same_origin_old_registration_independently_absent",0);
done:
    if(context_close(&c)){rc=BW_NRC_RELEASE_UNCONFIRMED;if(!out->stage)first_failure(out,"superseded_anchor_release",-1,errno);}
    if(publication_unknown)rc=BW_NRC_RELEASE_UNCONFIRMED; /* Partial/close/readback ambiguity is never replay authority. */
    free(old);free(fresh);free(wire);
    if(rc<0)result(out,BW_NRC_UNKNOWN,out->stage?out->stage:"superseded_observation_unknown_no_requests",rc);
    pthread_mutex_unlock(&nrc_lock);return rc;
}

/* Present-source retirement reuses the private exact-event ledger above. It
 * deliberately does not broaden missing-source reconcile or adopt a title. */
static int retire_current(const struct bw_nrc_retirement_ops *o,const struct bw_nrc_subject *s){
    return current(&o->registration,s)||o->released(o->registration.opaque,s)||
        o->outside_scan_roots(o->registration.opaque,s)||current(&o->registration,s)?-1:0;
}
static int retire_manual_absent(const struct bw_nrc_retirement_ops *o,const struct bw_nrc_subject *s){
    return retire_current(o,s)||o->manual_present(o->registration.opaque,s)!=0||
        retire_current(o,s)||o->manual_present(o->registration.opaque,s)!=0||retire_current(o,s)?-1:0;
}
static int retire_absence(struct context *c,struct record *r,const struct bw_nrc_retirement_ops *o){
    return record_anchors(c,r)||source_capture(c,r,1)||retire_manual_absent(o,&r->subject)||
        app_absent(c,r)||retire_current(o,&r->subject)||app_absent(c,r)||retire_current(o,&r->subject)?-1:0;
}
static int retire_registration_current(struct context *c,const struct record *r,unsigned *missing){
    char app[BW_NRC_PATH_BYTES],path[BW_NRC_PATH_BYTES];
    return registration_current(c,r,missing)||join(app,c->env.apps_root,r->subject.title)||
        join(path,app,"app.pkg")||no_file(path)||anchors_current(c)?-1:0;
}
int bw_nrc_retire_present(const struct bw_nrc_environment *e,const char title[10],const char run[33],
    const struct bw_nrc_retirement_ops *ops,struct bw_nrc_retirement_result *out){
    if(out)memset(out,0,sizeof *out);
    if(!out||!title_valid(title)||!run_valid(run)||!ops||!ops_valid(&ops->registration,1)||
       !ops->released||!ops->outside_scan_roots||!ops->manual_present||!ops->manual_remove||pthread_mutex_lock(&nrc_lock))return -1;
    struct bw_nrc_result *r=&out->registration;const struct bw_nrc_ops *base=&ops->registration;
    struct context c;context_init(&c);struct record *record=calloc(1,sizeof *record);
    unsigned char *wire=malloc(NRC_WIRE);char name[96],path[BW_NRC_PATH_BYTES];struct stat st;int rc=-1;
    if(!record||!wire||!e||!e->data_root||join(path,e->data_root,BW_NRC_LEDGER_LEAF))goto done;
    if(lstat(path,&st)){if(errno==ENOENT)rc=result(r,BW_NRC_UNOWNED,"no_ownership_ledger_no_retirement",0);goto done;}
    if(current(base,NULL)||context_base(&c,e))goto done;
    int opened=ledger_open(&c,0,base,NULL);if(opened==1){rc=result(r,BW_NRC_UNOWNED,"no_ownership_ledger_no_retirement",0);goto done;}if(opened)goto done;
    if(leaf(name,title,run,"owner"))goto done;int loaded=read_exact(&c,name,wire,NRC_WIRE);
    if(loaded==1){rc=result(r,BW_NRC_UNOWNED,"no_committed_registration_event_no_retirement",0);goto done;}
    if(loaded||decode(record,&c,wire,title,run)||record_anchors(&c,record)||
       retire_current(ops,&record->subject)||source_capture(&c,record,1))goto done;
    unsigned char expected[NRC_MARKER];marker(expected,"BWROCA01",wire+NRC_HASH_AT,&record->subject);
    if(leaf(name,title,run,"absent"))goto done;int absent=marker_read(&c,name,expected);if(absent<0)goto done;
    if(!absent){
        if(retire_absence(&c,record,ops))goto done;out->manual_absent=1;
        rc=result(r,BW_NRC_ABSENT,"retired_registration_still_independently_absent",0);goto done;
    }
    marker(expected,"BWROCP01",wire+NRC_HASH_AT,&record->subject);if(leaf(name,title,run,"pending"))goto done;
    int pending=marker_read(&c,name,expected);if(pending<0)goto done;
    marker(expected,"BWROCR01",wire+NRC_HASH_AT,&record->subject);if(leaf(name,title,run,"retire"))goto done;
    int retiring=marker_read(&c,name,expected);if(retiring<0)goto done;
    marker(expected,"BWROCU01",wire+NRC_HASH_AT,&record->subject);if(leaf(name,title,run,"uninstall"))goto done;
    int uninstalling=marker_read(&c,name,expected);if(uninstalling<0)goto done;
    if(!pending){
        rc=result(r,BW_NRC_PENDING,"retirement_pending_observe_only",0);
        if(!retire_absence(&c,record,ops)){
            marker(expected,"BWROCA01",wire+NRC_HASH_AT,&record->subject);
            if(leaf(name,title,run,"absent")||write_new(&c,name,expected,sizeof expected,base,&record->subject)){rc=-1;goto done;}
            out->manual_absent=1;rc=result(r,BW_NRC_ABSENT,"manual_path_app_directory_and_title_row_absent",0);goto done;
        }
        /* A missing retire marker means an old cleanup or a crash before the
         * manual attempt was durably armed. Never invent/replay that attempt. */
        if(retiring||!uninstalling)goto done;
    }else{
        if(!retiring||!uninstalling)goto done;unsigned missing=0;
        if(retire_registration_current(&c,record,&missing)||missing||source_capture(&c,record,1)||retire_current(ops,&record->subject))goto done;
        int present=ops->manual_present(base->opaque,&record->subject);if(present<0||present>1)goto done;
        marker(expected,"BWROCR01",wire+NRC_HASH_AT,&record->subject);
        if(leaf(name,title,run,"retire")||write_new(&c,name,expected,sizeof expected,base,&record->subject))goto done;
        marker(expected,"BWROCP01",wire+NRC_HASH_AT,&record->subject);
        if(leaf(name,title,run,"pending")||write_new(&c,name,expected,sizeof expected,base,&record->subject))goto done;
        rc=result(r,BW_NRC_PENDING,"manual_retirement_intent_durable_no_retry",0);
        if(source_capture(&c,record,1)||retire_registration_current(&c,record,&missing)||missing||retire_current(ops,&record->subject))goto done;
        if(present){
            out->manual_remove_attempted=1;int requested=ops->manual_remove(base->opaque,&record->subject);
            out->manual_remove_accepted=requested==0;
            r->reason=requested==0?"manual_remove_requested_readback_required":requested==1?"manual_remove_refused_no_retry":"manual_remove_outcome_unknown_no_retry";
        }
    }
    if(retire_manual_absent(ops,&record->subject))goto done;out->manual_absent=1;
    unsigned missing=0;
    if(source_capture(&c,record,1)||retire_registration_current(&c,record,&missing)||retire_current(ops,&record->subject))goto done;
    r->missing_mount_link=missing; /* A normal queued scan can remove this link. */
    marker(expected,"BWROCU01",wire+NRC_HASH_AT,&record->subject);
    if(leaf(name,title,run,"uninstall")||write_new(&c,name,expected,sizeof expected,base,&record->subject))goto done;
    rc=result(r,BW_NRC_PENDING,"normal_uninstall_intent_durable_no_retry",0);
    if(source_capture(&c,record,1)||retire_registration_current(&c,record,&missing)||
       retire_manual_absent(ops,&record->subject)||retire_current(ops,&record->subject))goto done;
    r->uninstall_attempted=1;int requested=base->uninstall(base->opaque,record->subject.title);r->uninstall_accepted=requested==0;
    r->reason=requested==0?"normal_uninstall_requested_absence_unverified":requested==1?"normal_uninstall_refused_no_retry":"normal_uninstall_outcome_unknown_no_retry";
done:
    if(context_close(&c))rc=-1;free(record);free(wire);
    if(rc<0)result(r,BW_NRC_UNKNOWN,"owned_registration_retirement_evidence_unknown",rc);
    pthread_mutex_unlock(&nrc_lock);return rc;
}
static int retirement_observe_current(const struct bw_nrc_retirement_observer *o,const struct bw_nrc_subject *s){
    return o->current(o->opaque)||o->sources(o->opaque,s)||o->outside_scan_roots(o->opaque,s)?-1:0;
}
static int retirement_observe_absence(struct context *c,struct record *r,const struct bw_nrc_retirement_observer *o){
    if(record_anchors(c,r)||source_capture(c,r,1))return -1;
    for(unsigned i=0;i<2;i++)if(retirement_observe_current(o,&r->subject)||o->manual_present(o->opaque,&r->subject)!=0||app_absent(c,r)||source_capture(c,r,1)||retirement_observe_current(o,&r->subject))return -1;
    return 0;
}
int bw_nrc_retirement_observe(const struct bw_nrc_environment *e,const char title[10],const char run[33],
    const struct bw_nrc_retirement_observer *ops,struct bw_nrc_retirement_result *out){
    if(out)memset(out,0,sizeof *out);
    if(!out||!title_valid(title)||!run_valid(run)||!ops||!ops->current||!ops->sources||!ops->outside_scan_roots||!ops->manual_present||pthread_mutex_lock(&nrc_lock))return -1;
    struct bw_nrc_ops base={.opaque=ops->opaque,.current=ops->current,.sources=ops->sources};
    struct bw_nrc_result *r=&out->registration;struct context c;context_init(&c);struct record *record=calloc(1,sizeof *record);unsigned char *wire=malloc(NRC_WIRE);int rc=-1;char name[96],path[BW_NRC_PATH_BYTES];struct stat st;
    if(!record||!wire||!e||!e->data_root||ops->current(ops->opaque))goto done;
    int n=snprintf(path,sizeof path,"%s/%s",e->data_root,BW_NRC_LEDGER_LEAF);if(n<0||(size_t)n>=sizeof path)goto done;
    if(lstat(path,&st)){if(errno==ENOENT)rc=result(r,BW_NRC_UNOWNED,"no_ownership_ledger_no_observation",0);goto done;}
    if(context_base(&c,e))goto done;int opened=ledger_open(&c,0,&base,NULL);if(opened==1){rc=result(r,BW_NRC_UNOWNED,"no_ownership_ledger_no_observation",0);goto done;}if(opened)goto done;
    if(leaf(name,title,run,"owner"))goto done;int loaded=read_exact(&c,name,wire,NRC_WIRE);
    if(loaded==1){rc=result(r,BW_NRC_UNOWNED,"no_committed_registration_event_no_observation",0);goto done;}
    if(loaded||decode(record,&c,wire,title,run)||record_anchors(&c,record)||retirement_observe_current(ops,&record->subject)||source_capture(&c,record,1))goto done;
    unsigned char expected[NRC_MARKER];marker(expected,"BWROCR01",wire+NRC_HASH_AT,&record->subject);if(leaf(name,title,run,"retire"))goto done;int retiring=marker_read(&c,name,expected);if(retiring<0)goto done;
    marker(expected,"BWROCP01",wire+NRC_HASH_AT,&record->subject);if(leaf(name,title,run,"pending"))goto done;int pending=marker_read(&c,name,expected);if(pending<0)goto done;
    if(retiring||pending){rc=result(r,BW_NRC_BLOCKED,"no_existing_retiring_pending_observation_only",0);goto done;}
    rc=result(r,BW_NRC_PENDING,"retirement_observation_pending_no_request_authority",0);
    if(retirement_observe_absence(&c,record,ops))goto done;
    marker(expected,"BWROCA01",wire+NRC_HASH_AT,&record->subject);if(leaf(name,title,run,"absent"))goto done;int absent=marker_read(&c,name,expected);if(absent<0){rc=-1;goto done;}
    if(absent&&write_new(&c,name,expected,sizeof expected,&base,&record->subject)){rc=-1;goto done;}
    if(retirement_observe_absence(&c,record,ops)){rc=-1;goto done;}out->manual_absent=1;rc=result(r,BW_NRC_ABSENT,"manual_path_app_directory_and_title_row_observed_absent",0);
done:if(context_close(&c))rc=-1;free(record);free(wire);if(rc<0)result(r,BW_NRC_UNKNOWN,"retirement_observation_evidence_unknown_no_requests",rc);pthread_mutex_unlock(&nrc_lock);return rc;
}

static int removed_equal(const struct bw_removed_source *a,const struct bw_removed_source *b){
    return a->identity.kind==b->identity.kind&&a->identity.policy==b->identity.policy&&
        !strcmp(a->identity.binding.source,b->identity.binding.source)&&!strcmp(a->identity.binding.game,b->identity.binding.game)&&
        !strcmp(a->identity.binding.title,b->identity.binding.title)&&!memcmp(a->identity.binding.sha256,b->identity.binding.sha256,32)&&
        !memcmp(a->prepared_sha256,b->prepared_sha256,32)&&!memcmp(a->operation,b->operation,33);
}
static int removed_record_current(struct context *c,const struct record *r,const struct bw_removed_source *removed){
    struct bw_removed_source actual={0};struct bw_removed_result status={0};struct bw_reader_selection selected;
    if(bw_removed_lookup(c->env.data_root,&removed->identity,&actual,&status)||status.state!=BW_REMOVED_COMMITTED||
       !removed_equal(&actual,removed)||bw_reader_selection_parse(&selected,r->selection,r->selection_size))return -1;
    unsigned char binding[32];if(hex(selected.binding_hex,64,binding)||actual.identity.kind!=BW_PROVIDER_FOLDER||actual.identity.policy!=BW_PROVIDER_POLICY_V1||
       strcmp(selected.source,actual.identity.binding.source)||strcmp(r->subject.title,actual.identity.binding.title)||
       strcmp(r->subject.game,actual.identity.binding.game)||memcmp(binding,actual.identity.binding.sha256,32)||
       memcmp(r->subject.prepared_record_sha256,actual.prepared_sha256,32))return -1;
    return 0;
}
static int removed_observer_current(const struct bw_nrc_removed_observer *o,const struct bw_nrc_subject *s){
    return o->current(o->opaque)||o->sources(o->opaque,s)?-1:0;
}
static int removed_prepared_scope(struct anchor *prepared){
    static const char expected[]="Blackwire native preparations v1\n";char path[BW_NRC_PATH_BYTES];struct blob marker={0};
    if(anchor_current(prepared)||prepared->st.st_uid!=geteuid()||(prepared->st.st_mode&07777)!=0755||join(path,prepared->path,".blackwire-native-prepared"))return -1;
    int bad=blob_read(path,sizeof expected-1,&marker)||marker.size!=sizeof expected-1||marker.st.st_uid!=geteuid()||
        (marker.st.st_mode&07777)!=0644||memcmp(marker.bytes,expected,sizeof expected-1)||anchor_current(prepared);
    blob_free(&marker);return bad?-1:0;
}
static int removed_absence(struct context *c,struct anchor *prepared,const struct record *r,
    const struct bw_removed_source *removed,const struct bw_nrc_removed_observer *o){
    struct stat st;
    if(record_anchors(c,r)||removed_prepared_scope(prepared)||removed_observer_current(o,&r->subject)||removed_record_current(c,r,removed)||
       source_missing(c,r)||cp_fstatat(prepared->fd,r->subject.game,&st,AT_SYMLINK_NOFOLLOW)==0||errno!=ENOENT||
       o->manual_present(o->opaque,&r->subject)!=0||app_absent(c,r)||removed_observer_current(o,&r->subject)||
       removed_prepared_scope(prepared)||record_anchors(c,r)||removed_record_current(c,r,removed))return -1;
    return 0;
}
int bw_nrc_observe_removed(const struct bw_nrc_environment *e,const char title[10],const char run[33],
    const struct bw_removed_source *removed,const struct bw_nrc_removed_observer *ops,struct bw_nrc_result *out){
    if(out)memset(out,0,sizeof *out);
    if(!out||!removed||!title_valid(title)||!run_valid(run)||!ops||!ops->current||!ops->sources||!ops->manual_present||pthread_mutex_lock(&nrc_lock))return -1;
    struct context c;context_init(&c);struct anchor prepared={.fd=-1};struct record *record=calloc(1,sizeof *record);
    unsigned char *wire=malloc(NRC_WIRE);char name[96],path[BW_NRC_PATH_BYTES];struct stat st;int rc=-1;
    struct bw_nrc_ops base={.opaque=ops->opaque,.current=ops->current,.sources=ops->sources};
    if(!record||!wire||!e||!e->data_root||ops->current(ops->opaque))goto done;
    if(join(path,e->data_root,BW_NRC_LEDGER_LEAF))goto done;
    if(lstat(path,&st)){if(errno==ENOENT)rc=result(out,BW_NRC_UNOWNED,"no_original_owner_no_removed_completion",0);goto done;}
    if(context_base(&c,e)||ledger_open(&c,0,&base,NULL)||leaf(name,title,run,"owner"))goto done;
    int loaded=read_exact(&c,name,wire,NRC_WIRE);if(loaded==1){rc=result(out,BW_NRC_UNOWNED,"no_original_registration_event_no_removed_completion",0);goto done;}
    if(loaded||decode(record,&c,wire,title,run)||record_anchors(&c,record)||removed_record_current(&c,record,removed)||
       removed_observer_current(ops,&record->subject)||join(path,c.env.data_root,"native-prepared")||anchor_open(&prepared,path,0)||removed_prepared_scope(&prepared))goto done;
    const char *suffixes[]={"pending","retire","uninstall"};const char *magics[]={"BWROCP01","BWROCR01","BWROCU01"};unsigned char expected[NRC_MARKER];
    for(unsigned i=0;i<3;i++){marker(expected,magics[i],wire+NRC_HASH_AT,&record->subject);if(leaf(name,title,run,suffixes[i])||marker_read(&c,name,expected)!=1)goto done;}
    if(removed_absence(&c,&prepared,record,removed,ops)||removed_absence(&c,&prepared,record,removed,ops))goto done;
    marker(expected,"BWROCA01",wire+NRC_HASH_AT,&record->subject);if(leaf(name,title,run,"absent"))goto done;
    int absent=marker_read(&c,name,expected);if(absent<0||(absent&&write_new(&c,name,expected,sizeof expected,&base,&record->subject)))goto done;
    if(removed_absence(&c,&prepared,record,removed,ops)||removed_absence(&c,&prepared,record,removed,ops))goto done;
    rc=result(out,BW_NRC_ABSENT,"original_owner_and_exact_removed_generation_observed_absent",0);
done:if(prepared.fd>=0&&close(prepared.fd))rc=-1;if(context_close(&c))rc=-1;free(record);free(wire);
    if(rc<0)result(out,BW_NRC_UNKNOWN,"removed_owner_completion_unknown_no_requests",rc);pthread_mutex_unlock(&nrc_lock);return rc;
}
