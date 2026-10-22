/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _XOPEN_SOURCE 700
#include "native_removed_sources.h"
#include "copy_sha256.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#define P 2048
static unsigned checks,cases,opens,closes,unlinks,writes,serial,fail_written;
static const char *case_name;static char base[P],fdpath[4096][P];static unsigned char live[4096];
enum fault {NO_FAULT,RECORD_PARTIAL,COMMIT_PARTIAL,RESTORE_CLOSE,UNLINK_REFUSED,UNLINK_UNCERTAIN};
static enum fault fault;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"case=%s line=%d expr=%s errno=%d\n",case_name,__LINE__,#x,errno);abort();}}while(0)
static void remember(int fd,const char *s){if(fd>=0){CHECK(fd<4096&&!live[fd]&&strlen(s)<P);live[fd]=1;strcpy(fdpath[fd],s);opens++;}}
static int ends(int fd,const char *s){size_t n=fd>=0&&fd<4096?strlen(fdpath[fd]):0,k=strlen(s);return n>=k&&!strcmp(fdpath[fd]+n-k,s);}
int rm_test_open(const char *s,int flags,...){mode_t m=0;if(flags&O_CREAT){va_list a;va_start(a,flags);m=(mode_t)va_arg(a,int);va_end(a);}int fd=open(s,flags,m);remember(fd,s);return fd;}
int rm_test_openat(int dir,const char *s,int flags,...){mode_t m=0;if(flags&O_CREAT){va_list a;va_start(a,flags);m=(mode_t)va_arg(a,int);va_end(a);}int fd=openat(dir,s,flags,m);remember(fd,s);return fd;}
int rm_test_close(int fd){int inject=fault==RESTORE_CLOSE&&ends(fd,".restore")&&!fail_written;if(inject)fail_written=1;int rc=close(fd);if(fd>=0&&fd<4096&&live[fd]){live[fd]=0;closes++;}if(inject){errno=EIO;return -1;}return rc;}
ssize_t rm_test_write(int fd,const void *b,size_t n){writes++;if((fault==RECORD_PARTIAL&&ends(fd,".removed"))||(fault==COMMIT_PARTIAL&&ends(fd,".complete"))){if(fail_written++){errno=EIO;return -1;}return write(fd,b,n>7?7:n);}return write(fd,b,n);}
int rm_test_unlinkat(int fd,const char *s,int flags){unlinks++;if(fault==UNLINK_REFUSED){errno=EACCES;return -1;}int rc=unlinkat(fd,s,flags);if(fault==UNLINK_UNCERTAIN&&!rc){errno=EIO;return -1;}return rc;}
struct fixture {char root[P],data[P],pc[P],save[P],source[P],state[P],app[P];struct bw_removed_source item;struct bw_removed_ops ops;unsigned current_calls,absent_calls,fail_at;int busy,unknown;};
static void path(char out[P],const char *a,const char *b){int n=snprintf(out,P,"%s/%s",a,b);CHECK(n>0&&n<P);}
static void make(const char *s,mode_t m){CHECK(mkdir(s,m)==0&&chmod(s,m)==0);}
static void file(const char *s,const char *body,mode_t m){int fd=open(s,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,m);CHECK(fd>=0&&fchmod(fd,m)==0);CHECK(write(fd,body,strlen(body))==(ssize_t)strlen(body)&&fsync(fd)==0&&close(fd)==0);}
static void hash(const char *s,unsigned char out[32]){int fd=open(s,O_RDONLY|O_NOFOLLOW);CHECK(fd>=0);struct cp_sha state;cp_sha_init(&state);unsigned char b[4096];ssize_t n;while((n=read(fd,b,sizeof b))>0)cp_sha_update(&state,b,n);CHECK(n==0&&close(fd)==0);cp_sha_final(&state,out);}
static int current(void *p){struct fixture *f=p;return f->busy||(++f->current_calls==f->fail_at)?-1:0;}
static int absent(void *p,const struct bw_removed_source *s){struct fixture *f=p;f->absent_calls++;CHECK(!strcmp(s->identity.binding.game,f->item.identity.binding.game));if(f->unknown)return -1;struct stat st;const char *paths[]={f->source,f->state,f->app};for(unsigned i=0;i<3;i++){if(!lstat(paths[i],&st)||errno!=ENOENT)return -1;}return 0;}
static void init(struct fixture *f,const char *name){
    memset(f,0,sizeof *f);case_name=name;cases++;fault=NO_FAULT;fail_written=0;
    int n=snprintf(f->root,P,"%s/%03u-%s",base,++serial,name);CHECK(n>0&&n<P);make(f->root,0700);path(f->data,f->root,"data");make(f->data,0755);
    path(f->pc,f->root,"pc-original");file(f->pc,"original preserved PC bytes\n",0644);path(f->save,f->root,"saved-data");file(f->save,"preserved save bytes\n",0600);
    path(f->source,f->data,"owned-game");path(f->state,f->data,"owned-state");path(f->app,f->root,"registered-app");
    f->item.identity.kind=BW_PROVIDER_FOLDER;f->item.identity.policy=BW_PROVIDER_POLICY_V1;strcpy(f->item.identity.binding.source,"Example Game");strcpy(f->item.identity.binding.title,"PPSA10001");memset(f->item.identity.binding.sha256,0xaa,32);
    strcpy(f->item.identity.binding.game,"PPSA10001_");memset(f->item.identity.binding.game+10,'a',64);memset(f->item.prepared_sha256,0xbb,32);memset(f->item.operation,'1',32);f->ops=(struct bw_removed_ops){f,current,absent};
}
static int lookup(struct fixture *f,struct bw_removed_result *r){struct bw_removed_source s;int rc=bw_removed_lookup(f->data,&f->item.identity,&s,r);CHECK(opens==closes);return rc;}
static int commit(struct fixture *f,struct bw_removed_result *r){unsigned char a[64],b[64];hash(f->pc,a);hash(f->save,a+32);int rc=bw_removed_commit(f->data,&f->item,&f->ops,r);hash(f->pc,b);hash(f->save,b+32);CHECK(!memcmp(a,b,64)&&opens==closes);return rc;}
static int restore(struct fixture *f,const char *op,struct bw_removed_result *r){unsigned char a[64],b[64];hash(f->pc,a);hash(f->save,a+32);int rc=bw_removed_restore(f->data,&f->item.identity,op,&f->ops,r);hash(f->pc,b);hash(f->save,b+32);CHECK(!memcmp(a,b,64)&&opens==closes);return rc;}
static void leaf(struct fixture *f,const char *suffix,char out[P]){char name[160],ledger[P];int n=snprintf(name,sizeof name,"%s.%s",f->item.identity.binding.game,suffix);CHECK(n>0&&n<(int)sizeof name);path(ledger,f->data,"removed-sources");path(out,ledger,name);}
int main(int argc,char **argv){
    CHECK(argc==2&&strlen(argv[1])<P);strcpy(base,argv[1]);make(base,0700);struct fixture f;struct bw_removed_result r;struct bw_removed_source list[4];size_t count;char p[P],q[P],op[33];memset(op,'2',32);op[32]=0;
    init(&f,"missing-ledger-is-visible");CHECK(lookup(&f,&r)==0&&r.state==BW_REMOVED_VISIBLE);CHECK(bw_removed_list(f.data,list,4,&count)==0&&!count);path(p,f.data,"removed-sources");struct stat st;CHECK(lstat(p,&st)<0&&errno==ENOENT);
    init(&f,"durable-remove-and-restore");CHECK(commit(&f,&r)==0&&r.state==BW_REMOVED_COMMITTED&&f.absent_calls>=4);CHECK(lookup(&f,&r)==0&&r.state==BW_REMOVED_COMMITTED);CHECK(bw_removed_list(f.data,list,4,&count)==0&&count==1&&!strcmp(list[0].identity.binding.source,"Example Game"));unsigned w=writes;CHECK(commit(&f,&r)==0&&writes==w);CHECK(restore(&f,op,&r)==0&&r.state==BW_REMOVED_RESTORED);CHECK(lookup(&f,&r)==0&&r.state==BW_REMOVED_VISIBLE);unsigned u=unlinks;CHECK(restore(&f,op,&r)==0&&r.state==BW_REMOVED_RESTORED&&unlinks==u);CHECK(bw_removed_list(f.data,list,4,&count)==0&&!count);
    init(&f,"source-still-present-not-removed");make(f.source,0755);CHECK(commit(&f,&r)<0&&r.state==BW_REMOVED_UNKNOWN);CHECK(lookup(&f,&r)==0&&r.state==BW_REMOVED_VISIBLE);
    init(&f,"private-record-still-present");make(f.state,0755);CHECK(commit(&f,&r)<0);CHECK(lookup(&f,&r)==0&&r.state==BW_REMOVED_VISIBLE);
    init(&f,"registered-title-still-present");make(f.app,0755);CHECK(commit(&f,&r)<0);CHECK(lookup(&f,&r)==0&&r.state==BW_REMOVED_VISIBLE);
    init(&f,"unknown-absence-refuses");f.unknown=1;CHECK(commit(&f,&r)<0);CHECK(lookup(&f,&r)==0&&r.state==BW_REMOVED_VISIBLE);
    init(&f,"busy-reservation-refuses");f.busy=1;CHECK(commit(&f,&r)<0);f.busy=0;CHECK(lookup(&f,&r)==0&&r.state==BW_REMOVED_VISIBLE);
    init(&f,"identity-domain-isolation");CHECK(commit(&f,&r)==0);struct bw_provider_binding other=f.item.identity;strcpy(other.binding.source,"Other Game");struct bw_removed_source s;CHECK(bw_removed_lookup(f.data,&other,&s,&r)<0&&r.state==BW_REMOVED_UNKNOWN);
    init(&f,"different-generation-not-overwritten");CHECK(commit(&f,&r)==0);memset(f.item.prepared_sha256,0xcc,32);CHECK(commit(&f,&r)<0);CHECK(lookup(&f,&r)==0&&r.state==BW_REMOVED_COMMITTED);
    enum fault failures[]={RECORD_PARTIAL,COMMIT_PARTIAL};for(unsigned i=0;i<2;i++){init(&f,i?"commit-marker-partial-unknown":"record-partial-unknown");fault=failures[i];CHECK(commit(&f,&r)<0);fault=NO_FAULT;CHECK(lookup(&f,&r)<0&&r.state==BW_REMOVED_UNKNOWN);w=writes;CHECK(commit(&f,&r)<0&&writes==w);CHECK(bw_removed_list(f.data,list,4,&count)<0&&!count);}
    enum fault restores[]={RESTORE_CLOSE,UNLINK_REFUSED,UNLINK_UNCERTAIN};for(unsigned i=0;i<3;i++){init(&f,i==0?"restore-intent-close-uncertain":i==1?"restore-unlink-refused":"restore-unlink-uncertain");CHECK(commit(&f,&r)==0);fault=restores[i];CHECK(restore(&f,op,&r)<0&&r.state==BW_REMOVED_UNKNOWN);fault=NO_FAULT;u=unlinks;CHECK(restore(&f,op,&r)<0&&unlinks==u);}
    init(&f,"record-hardlink-refuses");CHECK(commit(&f,&r)==0);leaf(&f,"removed",p);path(q,f.root,"foreign-link");CHECK(link(p,q)==0);CHECK(lookup(&f,&r)<0);CHECK(restore(&f,op,&r)<0);
    init(&f,"record-symlink-refuses");CHECK(commit(&f,&r)==0);leaf(&f,"removed",p);CHECK(unlink(p)==0&&symlink(f.pc,p)==0);CHECK(lookup(&f,&r)<0&&restore(&f,op,&r)<0);
    init(&f,"orphan-complete-is-unknown");CHECK(commit(&f,&r)==0);leaf(&f,"removed",p);CHECK(unlink(p)==0);CHECK(lookup(&f,&r)<0&&bw_removed_list(f.data,list,4,&count)<0&&!count);
    init(&f,"unknown-private-file-not-ignored");CHECK(commit(&f,&r)==0);path(p,f.data,"removed-sources/unrecognized");file(p,"unrelated preserved\n",0600);CHECK(bw_removed_list(f.data,list,4,&count)<0&&!count);
    init(&f,"restore-does-not-adopt-new-copy");CHECK(commit(&f,&r)==0);make(f.source,0755);CHECK(restore(&f,op,&r)<0);CHECK(lookup(&f,&r)==0&&r.state==BW_REMOVED_COMMITTED);
    for(unsigned i=1;i<30;i++){init(&f,"guard-invalidates-during-publication");f.fail_at=i;int rc=commit(&f,&r);CHECK(rc==0||r.state==BW_REMOVED_UNKNOWN);f.fail_at=0;CHECK(lookup(&f,&r)<=0);CHECK(opens==closes);}
    CHECK(opens==closes);printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"moduleOpens\":%u,\"moduleCloses\":%u,\"pcAndSavePreserved\":true,\"realFilesystem\":true,\"consoleActions\":0}\n",cases,checks,opens,closes);return 0;
}
