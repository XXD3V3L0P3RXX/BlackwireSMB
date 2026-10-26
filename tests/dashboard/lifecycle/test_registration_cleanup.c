/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real isolated filesystem + source-built SQLite tests. The module has no
 * synthetic registration/DB implementation; only I/O faults and normal HTTP
 * callback outcomes are modeled. No console, source tree or user-data action. */
#define _XOPEN_SOURCE 700
#include "native_registration_cleanup.h"
#include "native_prepared_target.h"
#include "copy_sha256.h"
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sqlite3.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define P 2048
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"check failed case=%s line=%d: %s errno=%d\n",case_name,__LINE__,#x,errno);abort();}} while(0)
static unsigned checks,cases;
static const char *case_name,*program;
static char suite[P];
static unsigned serial;
enum fault {F_NONE,F_AT_ACCESS,F_AT_NOTDIR,F_AT_IO,F_PENDING_OPEN,F_PENDING_PARTIAL,F_PENDING_SYNC,F_PENDING_CLOSE,F_PENDING_READ,F_OWNER_PARTIAL,F_SHORT_READ,F_INTERRUPTED};
static enum fault fault;
static char fault_game[75];
static unsigned open_count,close_count,pending_synced,pending_dir_synced,write_faulted,pread_interrupts,write_interrupts;
static char fd_path[4096][P];
static unsigned char fd_live[4096];
static void remember(int fd,const char *p){if(fd>=0){CHECK(fd<4096&&!fd_live[fd]);fd_live[fd]=1;open_count++;CHECK(strlen(p)<P);strcpy(fd_path[fd],p);}}
static int suffix(int fd,const char *s){size_t a=fd>=0&&fd<4096?strlen(fd_path[fd]):0,b=strlen(s);return a>=b&&!strcmp(fd_path[fd]+a-b,s);}
static int real_open(const char *p,int flags,mode_t mode){return open(p,flags,mode);}
int nrc_test_open(const char *p,int flags,...){mode_t mode=0;if(flags&O_CREAT){va_list v;va_start(v,flags);mode=(mode_t)va_arg(v,int);va_end(v);}int fd=real_open(p,flags,mode);remember(fd,p);return fd;}
int nrc_test_openat(int dir,const char *p,int flags,...){
    mode_t mode=0;if(flags&O_CREAT){va_list v;va_start(v,flags);mode=(mode_t)va_arg(v,int);va_end(v);}
    if(fault==F_PENDING_OPEN&&strstr(p,".pending")&&(flags&O_CREAT)){errno=EACCES;return -1;}
    int fd=openat(dir,p,flags,mode);remember(fd,p);return fd;
}
int nrc_test_close(int fd){int inject=fault==F_PENDING_CLOSE&&suffix(fd,".pending")&&write_faulted==0;if(inject)write_faulted=1;int rc=close(fd);if(fd>=0&&fd<4096&&fd_live[fd]){fd_live[fd]=0;close_count++;}if(inject){errno=EIO;return -1;}return rc;}
int nrc_test_fstatat(int dir,const char *p,struct stat *s,int flags){
    if(!strcmp(p,fault_game)&&(fault==F_AT_ACCESS||fault==F_AT_NOTDIR||fault==F_AT_IO)){errno=fault==F_AT_ACCESS?EACCES:fault==F_AT_NOTDIR?ENOTDIR:EIO;return -1;}
    return fstatat(dir,p,s,flags);
}
ssize_t nrc_test_write(int fd,const void *p,size_t n){
    if(fault==F_INTERRUPTED&&write_interrupts++<2){errno=EINTR;return -1;}
    if((fault==F_PENDING_PARTIAL&&suffix(fd,".pending"))||(fault==F_OWNER_PARTIAL&&suffix(fd,".owner"))){if(write_faulted++){errno=EIO;return -1;}return write(fd,p,n>7?7:n);}
    return write(fd,p,n);
}
ssize_t nrc_test_pread(int fd,void *p,size_t n,off_t at){
    if(fault==F_PENDING_READ&&suffix(fd,".pending")){errno=EIO;return -1;}
    if(fault==F_INTERRUPTED&&pread_interrupts++<2){errno=EINTR;return -1;}
    if(fault==F_SHORT_READ&&n>3)n=3;
    return pread(fd,p,n,at);
}
int nrc_test_fsync(int fd){
    if(fault==F_PENDING_SYNC&&suffix(fd,".pending")){errno=EIO;return -1;}
    int rc=fsync(fd);if(!rc){if(suffix(fd,".pending"))pending_synced++;else if(pending_synced)pending_dir_synced++;}return rc;
}
struct fixture {
    char root[P],data[P],games[P],apps[P],db[P],source[P],app[P],receipt[P],parked[P],param[512],content[128];
    struct bw_reader_selection selected;struct bw_nrc_environment env;struct bw_nrc_ops ops;
    unsigned current_calls,source_calls,uninstalls,refuse_current_at,refuse_sources_at;
    int current_value,sources_value,uninstall_value;
};
static void path(char *out,const char *a,const char *b){int n=snprintf(out,P,"%s/%s",a,b);CHECK(n>0&&n<P);}
static void make(const char *p,mode_t m){CHECK(mkdir(p,m)==0);CHECK(chmod(p,m)==0);}
static void file(const char *p,const void *bytes,size_t n,mode_t m){int fd=open(p,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,m);CHECK(fd>=0);CHECK(fchmod(fd,m)==0);CHECK(write(fd,bytes,n)==(ssize_t)n);CHECK(fsync(fd)==0);CHECK(close(fd)==0);}
static void replace(const char *p,const void *bytes,size_t n,mode_t m){CHECK(unlink(p)==0);file(p,bytes,n,m);}
static void hash_file(const char *p,unsigned char out[32]){int fd=open(p,O_RDONLY|O_NOFOLLOW);CHECK(fd>=0);struct cp_sha s;cp_sha_init(&s);unsigned char b[8192];ssize_t n;while((n=read(fd,b,sizeof b))>0)cp_sha_update(&s,b,(size_t)n);CHECK(n==0);cp_sha_final(&s,out);CHECK(close(fd)==0);}
static int current(void *opaque){struct fixture *f=opaque;f->current_calls++;return (f->refuse_current_at&&f->current_calls>=f->refuse_current_at)?-1:f->current_value;}
static int sources(void *opaque,const struct bw_nrc_subject *s){struct fixture *f=opaque;f->source_calls++;CHECK(!strcmp(s->title,f->selected.title));CHECK(!strcmp(s->source,f->source));CHECK(!strcmp(s->content,f->content));return (f->refuse_sources_at&&f->source_calls>=f->refuse_sources_at)?-1:f->sources_value;}
static void marker_path(struct fixture *f,const char *suffix_name,char out[P]){char leaf[128];int n=snprintf(leaf,sizeof leaf,"registration-owners/%s.%s.%s",f->selected.title,f->selected.run,suffix_name);CHECK(n>0&&n<(int)sizeof leaf);path(out,f->data,leaf);}
static int uninstall(void *opaque,const char title[10]){
    struct fixture *f=opaque;CHECK(!strcmp(title,f->selected.title));f->uninstalls++;CHECK(f->uninstalls==1);CHECK(pending_synced&&pending_dir_synced);
    char p[P];marker_path(f,"pending",p);struct stat st;CHECK(lstat(p,&st)==0&&S_ISREG(st.st_mode)&&(st.st_mode&07777)==0600&&st.st_size==128);
    int fd=open(p,O_RDONLY|O_NOFOLLOW);CHECK(fd>=0);char magic[8];CHECK(read(fd,magic,8)==8&&!memcmp(magic,"BWROCP01",8));CHECK(close(fd)==0);
    return f->uninstall_value;
}
static void sql(struct fixture *f,const char *statement){sqlite3 *db=NULL;CHECK(sqlite3_open_v2(f->db,&db,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE,NULL)==SQLITE_OK);CHECK(sqlite3_exec(db,statement,NULL,NULL,NULL)==SQLITE_OK);CHECK(sqlite3_close(db)==SQLITE_OK);}
static void row(struct fixture *f,const char *content){char q[512];int n=snprintf(q,sizeof q,"INSERT INTO tbl_contentinfo VALUES('%s','%s');",f->selected.title,content);CHECK(n>0&&n<(int)sizeof q);sql(f,q);}
static void subject(struct fixture *f,unsigned which){
    memset(&f->selected,0,sizeof f->selected);int n=snprintf(f->selected.title,sizeof f->selected.title,"PPSA%05u",which);CHECK(n==9);
    memset(f->selected.binding_hex,'a',64);f->selected.binding_hex[64]=0;n=snprintf(f->selected.game,sizeof f->selected.game,"%s_%s",f->selected.title,f->selected.binding_hex);CHECK(n==74);
    n=snprintf(f->selected.run,sizeof f->selected.run,"%032x",which);CHECK(n==32);strcpy(f->selected.source,"Owned Game");f->selected.source_kind=BW_PROVIDER_FOLDER;f->selected.source_policy=BW_PROVIDER_POLICY_V1;f->selected.eboot_size=16;f->selected.manifest_count=2;f->selected.manifest_hash=1;
    n=snprintf(f->content,sizeof f->content,"UP0000-%s_00-BLACKWIRETEST0001",f->selected.title);CHECK(n>0&&n<(int)sizeof f->content);
    n=snprintf(f->param,sizeof f->param,"{\"titleId\":\"%s\",\"contentId\":\"%s\",\"applicationDrmType\":\"standard\"}\n",f->selected.title,f->content);CHECK(n>0&&n<(int)sizeof f->param);
    path(f->source,f->games,f->selected.game);path(f->app,f->apps,f->selected.title);
    char p[P],prepared[P];path(prepared,f->data,"native-prepared");if(mkdir(prepared,0700))CHECK(errno==EEXIST);path(p,prepared,f->selected.game);make(p,0700);path(f->receipt,p,"prepared.bin");
    unsigned char receipt[384];for(unsigned i=0;i<sizeof receipt;i++)receipt[i]=(unsigned char)(i+which);file(f->receipt,receipt,sizeof receipt,0600);unsigned char digest[32];cp_sha_bytes(receipt,sizeof receipt,digest);for(unsigned i=0;i<32;i++)snprintf(f->selected.prepared_record_hex+2*i,3,"%02x",digest[i]);
    make(f->source,0755);path(p,f->source,"sce_sys");make(p,0755);path(p,f->source,"sce_sys/param.json");file(p,f->param,strlen(f->param),0644);path(p,f->source,"eboot.bin");file(p,"test executable\n",16,0755);
    path(f->parked,f->root,f->selected.game);strcpy(fault_game,f->selected.game);
}
static void init(struct fixture *f,const char *name){
    memset(f,0,sizeof *f);case_name=name;cases++;fault=F_NONE;write_faulted=pread_interrupts=write_interrupts=pending_synced=pending_dir_synced=0;
    int n=snprintf(f->root,P,"%s/%03u-%s",suite,++serial,name);CHECK(n>0&&n<P);make(f->root,0700);path(f->data,f->root,"data");make(f->data,0755);path(f->games,f->data,"games");make(f->games,0755);path(f->apps,f->root,"apps");make(f->apps,0755);path(f->db,f->root,"app.db");sql(f,"CREATE TABLE tbl_contentinfo(titleId TEXT,contentId TEXT); INSERT INTO tbl_contentinfo VALUES('PPSA10000','protected-full-copy');");
    f->env=(struct bw_nrc_environment){f->data,f->apps,f->db};f->ops=(struct bw_nrc_ops){f,current,sources,uninstall};subject(f,10001);
    char p[P];path(p,f->root,"full-manual");make(p,0755);path(p,f->root,"full-manual/eboot.bin");file(p,"unrelated full game stays byte exact\n",36,0755);
}
static void registered(struct fixture *f){char p[P];make(f->app,0755);path(p,f->app,"sce_sys");make(p,0755);path(p,f->app,"sce_sys/param.json");file(p,f->param,strlen(f->param),0644);path(p,f->app,"mount.lnk");file(p,f->source,strlen(f->source),0644);row(f,f->content);}
static void owned(struct fixture *f,int scan){struct bw_nrc_ticket *ticket=NULL;struct bw_nrc_result r;CHECK(bw_nrc_begin(&f->env,&f->selected,&f->ops,&ticket,&r)==0&&ticket&&r.state==BW_NRC_INTENT);registered(f);struct bw_register_result reg={.add_attempted=scan?0u:1u,.add_confirmed=scan?0u:1u,.scan_attempted=scan?1u:0u,.registered=1,.message="actual fixture registration"};CHECK(bw_nrc_finish(ticket,0,&reg,&r)==0&&r.state==BW_NRC_OWNED);CHECK(bw_nrc_ticket_destroy(ticket)==0);}
static int reconcile(struct fixture *f,struct bw_nrc_result *r){unsigned char a[32],b[32],x[32],y[32];char p[P];path(p,f->root,"full-manual/eboot.bin");hash_file(p,a);hash_file(f->db,x);int rc=bw_nrc_reconcile(&f->env,f->selected.title,f->selected.run,&f->ops,r);hash_file(f->db,y);hash_file(p,b);CHECK(!memcmp(a,b,32)&&!memcmp(x,y,32));CHECK(open_count==close_count);return rc;}
static void missing(struct fixture *f){CHECK(rename(f->source,f->parked)==0);}
static void remove_registration(struct fixture *f){char p[P];path(p,f->app,"mount.lnk");if(unlink(p))CHECK(errno==ENOENT);path(p,f->app,"sce_sys/param.json");CHECK(unlink(p)==0);path(p,f->app,"sce_sys");CHECK(rmdir(p)==0);CHECK(rmdir(f->app)==0);char q[128];int n=snprintf(q,sizeof q,"DELETE FROM tbl_contentinfo WHERE titleId='%s';",f->selected.title);CHECK(n>0&&n<(int)sizeof q);sql(f,q);}
static void no_pending(struct fixture *f){char p[P];marker_path(f,"pending",p);struct stat st;CHECK(lstat(p,&st)<0&&errno==ENOENT);}
static void refused(struct fixture *f){struct bw_nrc_result r;CHECK(reconcile(f,&r)<0&&r.state==BW_NRC_UNKNOWN&&f->uninstalls==0);no_pending(f);}
static void pending_once(struct fixture *f,int response){f->uninstall_value=response;struct bw_nrc_result r;CHECK(reconcile(f,&r)==0&&r.state==BW_NRC_PENDING&&r.uninstall_attempted==1&&r.uninstall_accepted==(unsigned)(response==0)&&f->uninstalls==1);CHECK(reconcile(f,&r)==0&&r.state==BW_NRC_PENDING&&r.uninstall_attempted==0&&f->uninstalls==1);}
static void observations(void){
    struct fixture f;struct bw_nrc_result r;struct bw_nrc_ticket *t=NULL;struct bw_register_result reg={.add_attempted=1,.add_confirmed=1,.scan_attempted=0,.registered=1,.message=NULL};
    init(&f,"preexisting-match-not-adopted");registered(&f);CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)==1&&!t&&r.state==BW_NRC_UNOWNED);char p[P];path(p,f.data,BW_NRC_LEDGER_LEAF);struct stat st;CHECK(lstat(p,&st)<0&&errno==ENOENT);CHECK(reconcile(&f,&r)==0&&r.state==BW_NRC_UNOWNED&&f.uninstalls==0);
    init(&f,"preexisting-pending-not-adopted");make(f.app,0755);path(p,f.app,"mount.lnk");file(p,f.source,strlen(f.source),0644);CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)==1&&!t);no_pending(&f);
    init(&f,"registration-failed-no-owner");CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)==0);registered(&f);CHECK(bw_nrc_finish(t,-1,&reg,&r)<0);CHECK(bw_nrc_ticket_destroy(t)==0);missing(&f);CHECK(reconcile(&f,&r)==0&&r.state==BW_NRC_UNOWNED&&f.uninstalls==0);
    init(&f,"no-real-mutation-no-owner");CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)==0);registered(&f);struct bw_register_result no_action={.add_attempted=0,.add_confirmed=0,.scan_attempted=0,.registered=1,.message=NULL};CHECK(bw_nrc_finish(t,0,&no_action,&r)<0);CHECK(bw_nrc_ticket_destroy(t)==0);missing(&f);CHECK(reconcile(&f,&r)==0&&r.state==BW_NRC_UNOWNED);
    init(&f,"source-present-blocked");owned(&f,0);CHECK(reconcile(&f,&r)==0&&r.state==BW_NRC_BLOCKED&&!f.uninstalls);no_pending(&f);
    for(int v=-1;v<=1;v++){init(&f,v<0?"unknown-no-retry":v?"refusal-no-retry":"accepted-no-retry");owned(&f,v==1);missing(&f);pending_once(&f,v);}
    init(&f,"independent-removal-completes");owned(&f,0);missing(&f);pending_once(&f,0);remove_registration(&f);CHECK(reconcile(&f,&r)==0&&r.state==BW_NRC_ABSENT&&f.uninstalls==1);CHECK(reconcile(&f,&r)==0&&r.state==BW_NRC_ABSENT&&f.uninstalls==1);struct bw_nrc_candidate list[4];size_t count=99;CHECK(bw_nrc_candidates(&f.env,list,4,&count)==0&&count==0);
    init(&f,"app-dir-alone-not-absence");owned(&f,0);missing(&f);pending_once(&f,0);path(p,f.app,"moved");char app_backup[P];path(app_backup,f.root,"registered-backup");CHECK(rename(f.app,app_backup)==0);CHECK(reconcile(&f,&r)==0&&r.state==BW_NRC_PENDING&&f.uninstalls==1);
    init(&f,"db-row-alone-not-absence");owned(&f,0);missing(&f);pending_once(&f,0);sql(&f,"DELETE FROM tbl_contentinfo WHERE titleId='PPSA10001';");CHECK(reconcile(&f,&r)==0&&r.state==BW_NRC_PENDING&&f.uninstalls==1);
    init(&f,"missing-link-scanner-state");owned(&f,0);missing(&f);path(p,f.app,"mount.lnk");CHECK(unlink(p)==0);CHECK(reconcile(&f,&r)==0&&r.state==BW_NRC_PENDING&&r.missing_mount_link&&f.uninstalls==1);
    init(&f,"pending-survives-new-process");owned(&f,0);missing(&f);pending_once(&f,0);pid_t child=fork();CHECK(child>=0);if(!child){execl(program,program,"--observe",f.root,f.selected.title,f.selected.run,(char *)NULL);_exit(120);}int status;CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);
    CHECK(open_count==close_count);
}
static void vetoes(void){
    struct fixture f;char p[P],q[P];struct bw_nrc_result r;
    const char *kinds[]={"alternative-full","alternative-manual","alternative-pkg","alternative-image","external-unknown"};
    for(unsigned i=0;i<sizeof kinds/sizeof kinds[0];i++){init(&f,kinds[i]);owned(&f,0);missing(&f);f.sources_value=-1;refused(&f);}
    init(&f,"new-job-or-cancel");owned(&f,0);missing(&f);f.current_value=-1;refused(&f);
    init(&f,"late-cancel-before-pending");owned(&f,0);missing(&f);f.refuse_current_at=f.current_calls+3;refused(&f);
    init(&f,"late-source-conflict");owned(&f,0);missing(&f);f.refuse_sources_at=f.source_calls+3;refused(&f);
    init(&f,"source-symlink-unknown");owned(&f,0);missing(&f);CHECK(symlink(f.parked,f.source)==0);refused(&f);
    init(&f,"source-file-present-blocked");owned(&f,0);missing(&f);file(f.source,"replacement",11,0644);CHECK(reconcile(&f,&r)==0&&r.state==BW_NRC_BLOCKED&&!f.uninstalls);no_pending(&f);
    enum fault errors[]={F_AT_ACCESS,F_AT_NOTDIR,F_AT_IO};const char *names[]={"eacces-not-absent","enotdir-not-absent","io-error-not-absent"};
    for(unsigned i=0;i<3;i++){init(&f,names[i]);owned(&f,0);missing(&f);fault=errors[i];refused(&f);}
    init(&f,"games-parent-replaced");owned(&f,0);missing(&f);path(q,f.root,"games-backup");CHECK(rename(f.games,q)==0);make(f.games,0755);refused(&f);
    init(&f,"app-node-replaced");owned(&f,0);missing(&f);path(q,f.root,"app-backup");CHECK(rename(f.app,q)==0);make(f.app,0755);refused(&f);
    init(&f,"param-content-changed");owned(&f,0);missing(&f);path(p,f.app,"sce_sys/param.json");replace(p,"{}",2,0644);refused(&f);
    init(&f,"param-identical-new-inode");owned(&f,0);missing(&f);path(p,f.app,"sce_sys/param.json");path(q,f.root,"old-param.json");CHECK(rename(p,q)==0);file(p,f.param,strlen(f.param),0644);refused(&f);
    init(&f,"param-special-mode");owned(&f,0);missing(&f);path(p,f.app,"sce_sys/param.json");CHECK(chmod(p,04644)==0);refused(&f);
    init(&f,"param-fine-mtime-change");owned(&f,0);missing(&f);path(p,f.app,"sce_sys/param.json");struct stat st;CHECK(lstat(p,&st)==0);struct timespec times[2]={st.st_atim,st.st_mtim};times[1].tv_nsec=(times[1].tv_nsec+1)%1000000000;CHECK(utimensat(AT_FDCWD,p,times,AT_SYMLINK_NOFOLLOW)==0);refused(&f);
    init(&f,"param-hardlink");owned(&f,0);missing(&f);path(p,f.app,"sce_sys/param.json");path(q,f.root,"param-hardlink");CHECK(link(p,q)==0);refused(&f);
    init(&f,"mount-bytes-changed");owned(&f,0);missing(&f);path(p,f.app,"mount.lnk");replace(p,"/unrelated/full",15,0644);refused(&f);
    init(&f,"mount-is-symlink");owned(&f,0);missing(&f);path(p,f.app,"mount.lnk");CHECK(unlink(p)==0);CHECK(symlink(f.receipt,p)==0);refused(&f);
    init(&f,"mount-image-present");owned(&f,0);missing(&f);path(p,f.app,"mount_img.lnk");file(p,"image",5,0644);refused(&f);
    init(&f,"mount-long-image-present");owned(&f,0);missing(&f);path(p,f.app,"mount_image.lnk");file(p,"image",5,0644);refused(&f);
    init(&f,"db-content-conflict");owned(&f,0);missing(&f);sql(&f,"UPDATE tbl_contentinfo SET contentId='another-package' WHERE titleId='PPSA10001';");refused(&f);
    init(&f,"db-duplicate-title");owned(&f,0);missing(&f);row(&f,f.content);refused(&f);
    init(&f,"db-missing-not-valid-registration");owned(&f,0);missing(&f);sql(&f,"DELETE FROM tbl_contentinfo WHERE titleId='PPSA10001';");refused(&f);
    init(&f,"db-malformed");owned(&f,0);missing(&f);replace(f.db,"invalid",7,0644);refused(&f);
    init(&f,"db-symlink");owned(&f,0);missing(&f);path(q,f.root,"saved.db");CHECK(rename(f.db,q)==0);CHECK(symlink(q,f.db)==0);CHECK(bw_nrc_reconcile(&f.env,f.selected.title,f.selected.run,&f.ops,&r)<0&&!f.uninstalls);no_pending(&f);
    init(&f,"pending-parent-replaced");owned(&f,0);missing(&f);pending_once(&f,0);remove_registration(&f);path(q,f.root,"apps-backup");CHECK(rename(f.apps,q)==0);make(f.apps,0755);CHECK(reconcile(&f,&r)<0&&r.state==BW_NRC_UNKNOWN&&f.uninstalls==1);
    init(&f,"whole-root-reset-inert");owned(&f,0);path(q,f.root,"data-backup");CHECK(rename(f.data,q)==0);make(f.data,0755);unsigned before=f.current_calls+f.source_calls;struct bw_nrc_candidate list[4];size_t count=9;CHECK(bw_nrc_candidates(&f.env,list,4,&count)==0&&!count);CHECK(reconcile(&f,&r)==0&&r.state==BW_NRC_UNOWNED&&f.uninstalls==0);CHECK(before==f.current_calls+f.source_calls);path(p,f.data,BW_NRC_LEDGER_LEAF);CHECK(lstat(p,&st)<0&&errno==ENOENT);
}
static void fault_cases(void){
    struct fixture f;struct bw_nrc_result r;char p[P];struct stat st;
    enum fault faults[]={F_PENDING_OPEN,F_PENDING_PARTIAL,F_PENDING_SYNC,F_PENDING_CLOSE,F_PENDING_READ};const char *names[]={"pending-create-refused","pending-partial-retained","pending-fsync-uncertain","pending-close-uncertain","pending-readback-uncertain"};
    for(unsigned i=0;i<5;i++){init(&f,names[i]);owned(&f,0);missing(&f);fault=faults[i];CHECK(reconcile(&f,&r)<0&&!f.uninstalls);fault=F_NONE;if(i){marker_path(&f,"pending",p);CHECK(lstat(p,&st)==0);CHECK(reconcile(&f,&r)<=0&&!f.uninstalls);}else no_pending(&f);}
    init(&f,"owner-partial-not-authority");struct bw_nrc_ticket *t=NULL;CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)==0);registered(&f);struct bw_register_result reg={.add_attempted=1,.add_confirmed=1,.scan_attempted=0,.registered=1,.message=NULL};fault=F_OWNER_PARTIAL;CHECK(bw_nrc_finish(t,0,&reg,&r)<0);CHECK(bw_nrc_ticket_destroy(t)==0);fault=F_NONE;missing(&f);refused(&f);struct bw_nrc_candidate list[4];size_t count=8;CHECK(bw_nrc_candidates(&f.env,list,4,&count)<0&&!count);
    init(&f,"short-positive-reads-complete");fault=F_SHORT_READ;owned(&f,0);missing(&f);pending_once(&f,0);CHECK(pread_interrupts==0);
    init(&f,"interrupted-io-preserves-material");fault=F_INTERRUPTED;owned(&f,0);missing(&f);pending_once(&f,0);CHECK(pread_interrupts>=2&&write_interrupts>=2);
    init(&f,"owner-tamper");owned(&f,0);missing(&f);marker_path(&f,"owner",p);int fd=open(p,O_WRONLY);CHECK(fd>=0);CHECK(pwrite(fd,"x",1,120)==1);CHECK(close(fd)==0);refused(&f);
    init(&f,"owner-hardlink");owned(&f,0);missing(&f);marker_path(&f,"owner",p);char q[P];path(q,f.root,"linked-owner");CHECK(link(p,q)==0);refused(&f);
    init(&f,"ledger-special-mode");owned(&f,0);missing(&f);path(p,f.data,BW_NRC_LEDGER_LEAF);CHECK(chmod(p,02700)==0);refused(&f);
    init(&f,"ledger-missing-marker");owned(&f,0);missing(&f);path(p,f.data,"registration-owners/.blackwire-registration-owners");CHECK(unlink(p)==0);refused(&f);
    CHECK(open_count==close_count);
}
static void acquisition_edges(void){
    struct fixture f;struct bw_nrc_ticket *t=NULL;struct bw_nrc_result r;char p[P];
    unsigned ids[]={99170,99171};for(unsigned i=0;i<2;i++){init(&f,i?"protect-current-dashboard":"protect-retired-dashboard");subject(&f,ids[i]);CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)<0&&!t);CHECK(bw_nrc_reconcile(&f.env,f.selected.title,f.selected.run,&f.ops,&r)<0&&!f.uninstalls);}
    init(&f,"unknown-provider-inert");f.selected.source_kind=99;CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)<0&&!t);
    init(&f,"unsealed-record-inert");memset(f.selected.prepared_record_hex,'0',64);CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)<0&&!t);
    init(&f,"record-body-mismatch");path(p,f.source,"sce_sys/param.json");replace(f.receipt,"short",5,0600);CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)<0&&!t);
    init(&f,"source-param-change-during-register");CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)==0);registered(&f);path(p,f.source,"sce_sys/param.json");replace(p,f.param,strlen(f.param),0644);struct bw_register_result reg={.add_attempted=1,.add_confirmed=1,.scan_attempted=0,.registered=1,.message=NULL};CHECK(bw_nrc_finish(t,0,&reg,&r)<0);CHECK(bw_nrc_ticket_destroy(t)==0);
    init(&f,"receipt-change-during-register");CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)==0);registered(&f);int fd=open(f.receipt,O_WRONLY);CHECK(fd>=0);CHECK(pwrite(fd,"x",1,0)==1);CHECK(close(fd)==0);CHECK(bw_nrc_finish(t,0,&reg,&r)<0);CHECK(bw_nrc_ticket_destroy(t)==0);
    init(&f,"ticket-cannot-finish-twice");CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)==0);registered(&f);CHECK(bw_nrc_finish(t,0,&reg,&r)==0);CHECK(bw_nrc_finish(t,0,&reg,&r)<0);CHECK(bw_nrc_ticket_destroy(t)==0);
    init(&f,"selection-run-cannot-reuse-intent");CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)==0);CHECK(bw_nrc_ticket_destroy(t)==0);CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)<0&&!t);
    CHECK(open_count==close_count);
}
/* Faults act on the actual durable intent/source/registration, so diagnostics
 * must expose the first refused existing check without granting ownership. */
static void acquisition_diagnostics(void){
    struct fixture f;struct bw_nrc_ticket *t=NULL;struct bw_nrc_result r;
    struct bw_register_result reg={.add_attempted=1,.add_confirmed=1,.scan_attempted=0,.registered=1,.message=NULL};char p[P];struct stat st;
    init(&f,"diagnostic-register-result");CHECK(!bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r));registered(&f);
    CHECK(bw_nrc_finish(t,-1,&reg,&r)<0&&!strcmp(r.stage,"ownership_commit_registration_result")&&!r.error_snapshot);
    CHECK(!bw_nrc_ticket_destroy(t));marker_path(&f,"owner",p);CHECK(lstat(p,&st)<0&&errno==ENOENT);
    init(&f,"diagnostic-current-sources");CHECK(!bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r));registered(&f);f.current_value=-1;
    CHECK(bw_nrc_finish(t,0,&reg,&r)<0&&!strcmp(r.stage,"ownership_commit_current_sources"));CHECK(!bw_nrc_ticket_destroy(t));
    init(&f,"diagnostic-intent-bytes");CHECK(!bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r));registered(&f);marker_path(&f,"intent",p);
    int fd=open(p,O_WRONLY);CHECK(fd>=0&&pwrite(fd,"x",1,120)==1&&!close(fd));
    CHECK(bw_nrc_finish(t,0,&reg,&r)<0&&!strcmp(r.stage,"ownership_commit_intent_bytes"));CHECK(!bw_nrc_ticket_destroy(t));
    init(&f,"diagnostic-source-replaced");CHECK(!bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r));registered(&f);path(p,f.source,"sce_sys/param.json");replace(p,f.param,strlen(f.param),0644);
    CHECK(bw_nrc_finish(t,0,&reg,&r)<0&&!strcmp(r.stage,"ownership_commit_source_unchanged"));CHECK(!bw_nrc_ticket_destroy(t));
    init(&f,"diagnostic-image-link");CHECK(!bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r));registered(&f);path(p,f.app,"mount_img.lnk");file(p,"image",5,0644);
    CHECK(bw_nrc_finish(t,0,&reg,&r)<0&&!strcmp(r.stage,"ownership_capture_image_absence"));CHECK(!bw_nrc_ticket_destroy(t));
    init(&f,"diagnostic-shared-native-modes");CHECK(!chmod(f.apps,0770));CHECK(!bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r));registered(&f);
    CHECK(!chmod(f.app,0777));path(p,f.app,"sce_sys/param.json");CHECK(!chmod(p,0666));path(p,f.app,"mount.lnk");CHECK(!chmod(p,0666));
    CHECK(!bw_nrc_finish(t,0,&reg,&r)&&r.state==BW_NRC_OWNED&&!r.stage);CHECK(!bw_nrc_ticket_destroy(t));
    CHECK(open_count==close_count);
}
struct thread_arg {struct fixture *f;struct bw_nrc_result r;int rc;};
static void *thread_reconcile(void *p){struct thread_arg *a=p;a->rc=bw_nrc_reconcile(&a->f->env,a->f->selected.title,a->f->selected.run,&a->f->ops,&a->r);return NULL;}
static void listing(void){
    struct fixture f;struct bw_nrc_result r;struct bw_nrc_candidate list[4];size_t count;char p[P];
    init(&f,"listing-and-pending-priority");owned(&f,0);struct fixture other=f;other.current_calls=other.source_calls=other.uninstalls=0;other.ops.opaque=&other;subject(&other,10002);owned(&other,0);missing(&other);pending_once(&other,0);CHECK(bw_nrc_candidates(&f.env,list,4,&count)==0&&count==2&&list[0].pending&& !strcmp(list[0].subject.title,other.selected.title)&&!list[1].pending);CHECK(bw_nrc_candidates(&f.env,list,1,&count)<0&&!count);remove_registration(&other);CHECK(reconcile(&other,&r)==0&&r.state==BW_NRC_ABSENT);CHECK(bw_nrc_candidates(&f.env,list,4,&count)==0&&count==1&&!strcmp(list[0].subject.title,f.selected.title));
    init(&f,"unknown-ledger-name-fails-list");owned(&f,0);path(p,f.data,"registration-owners/unrelated");file(p,"x",1,0600);CHECK(bw_nrc_candidates(&f.env,list,4,&count)<0&&!count);
    init(&f,"intent-only-is-inert");struct bw_nrc_ticket *t=NULL;CHECK(bw_nrc_begin(&f.env,&f.selected,&f.ops,&t,&r)==0);CHECK(bw_nrc_ticket_destroy(t)==0);CHECK(bw_nrc_candidates(&f.env,list,4,&count)==0&&!count);
    init(&f,"concurrent-reconcile-one-request");owned(&f,0);missing(&f);struct thread_arg args[2]={{.f=&f},{.f=&f}};pthread_t threads[2];CHECK(pthread_create(&threads[0],NULL,thread_reconcile,&args[0])==0);CHECK(pthread_create(&threads[1],NULL,thread_reconcile,&args[1])==0);CHECK(pthread_join(threads[0],NULL)==0&&pthread_join(threads[1],NULL)==0);CHECK(args[0].rc==0&&args[1].rc==0&&f.uninstalls==1&&args[0].r.uninstall_attempted+args[1].r.uninstall_attempted==1);CHECK(open_count==close_count);
}
int main(int argc,char **argv){
    program=argv[0];case_name="startup";
    if(argc==5&&!strcmp(argv[1],"--observe")){
        struct fixture f={0};CHECK(strlen(argv[2])<P);strcpy(f.root,argv[2]);path(f.data,f.root,"data");path(f.games,f.data,"games");path(f.apps,f.root,"apps");path(f.db,f.root,"app.db");CHECK(strlen(argv[3])==9&&strlen(argv[4])==32);strcpy(f.selected.title,argv[3]);strcpy(f.selected.run,argv[4]);memcpy(f.selected.game,f.selected.title,9);f.selected.game[9]='_';memset(f.selected.game+10,'a',64);path(f.source,f.games,f.selected.game);CHECK(snprintf(f.content,sizeof f.content,"UP0000-%s_00-BLACKWIRETEST0001",f.selected.title)>0);f.env=(struct bw_nrc_environment){f.data,f.apps,f.db};f.ops=(struct bw_nrc_ops){&f,current,sources,uninstall};struct bw_nrc_result r;CHECK(bw_nrc_reconcile(&f.env,f.selected.title,f.selected.run,&f.ops,&r)==0&&r.state==BW_NRC_PENDING&&!f.uninstalls);return 0;
    }
    CHECK(argc==2&&strlen(argv[1])<P);strcpy(suite,argv[1]);make(suite,0700);
    observations();vetoes();fault_cases();acquisition_edges();acquisition_diagnostics();listing();CHECK(open_count==close_count);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"realFilesystem\":true,\"realSqlite\":true,\"moduleOpens\":%u,\"moduleCloses\":%u,\"consoleActions\":0}\n",cases,checks,open_count,close_count);return 0;
}
