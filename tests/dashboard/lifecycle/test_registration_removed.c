/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual durable Removed/owner files and readonly SQLite observations. Normal
 * source/manual collectors and the caller's reservation remain modeled. */
#define nrc_test_write nrc_legacy_test_write
#define nrc_test_close nrc_legacy_test_close
#define main retained_cleanup_main
#include "test_registration_cleanup.c"
#undef main
#undef nrc_test_write
#undef nrc_test_close
#include "native_registration_removed.h"

static int absent_write_fault,absent_close_fault;
static unsigned absent_faulted;
ssize_t nrc_test_write(int fd,const void *p,size_t n){
    if(absent_write_fault&&suffix(fd,".absent")){if(absent_faulted++){errno=EIO;return -1;}return write(fd,p,n>7?7:n);}
    return nrc_legacy_test_write(fd,p,n);
}
int nrc_test_close(int fd){
    int inject=absent_close_fault&&suffix(fd,".absent")&&!absent_faulted++;
    int rc=nrc_legacy_test_close(fd);if(inject){errno=EIO;return -1;}return rc;
}
struct removed_fixture {
    struct fixture f;
    struct bw_removed_source removed;
    unsigned manual_calls,manual_unknown,manual_value;
    char state[P],state_parked[P],prepared[P];
};
static void removed_identity(struct removed_fixture *t){
    t->removed.identity.kind=BW_PROVIDER_FOLDER;t->removed.identity.policy=BW_PROVIDER_POLICY_V1;
    strcpy(t->removed.identity.binding.source,t->f.selected.source);strcpy(t->removed.identity.binding.title,t->f.selected.title);
    strcpy(t->removed.identity.binding.game,t->f.selected.game);memset(t->removed.identity.binding.sha256,0xaa,32);
    for(unsigned i=0;i<32;i++){unsigned x=0;CHECK(sscanf(t->f.selected.prepared_record_hex+2*i,"%2x",&x)==1);t->removed.prepared_sha256[i]=(unsigned char)x;}
    strcpy(t->removed.operation,"000000000000000000000000000000ab");
}
static int actual_absence(void *opaque,const struct bw_removed_source *s){
    struct removed_fixture *t=opaque;struct stat st;
    CHECK(!strcmp(s->identity.binding.game,t->f.selected.game));
    return lstat(t->f.source,&st)==0||errno!=ENOENT||lstat(t->state,&st)==0||errno!=ENOENT||
        lstat(t->f.app,&st)==0||errno!=ENOENT||bw_registration_db_match(t->f.db,t->f.selected.title,t->f.content)!=0?-1:0;
}
static void commit_removed(struct removed_fixture *t){
    struct bw_removed_ops o={t,current,actual_absence};struct bw_removed_result r;
    CHECK(bw_removed_commit(t->f.data,&t->removed,&o,&r)==0&&r.state==BW_REMOVED_COMMITTED);
}
static void init_removed(struct removed_fixture *t,const char *name,int own,int commit){
    memset(t,0,sizeof *t);init(&t->f,name);absent_write_fault=absent_close_fault=0;absent_faulted=0;
    if(own)owned(&t->f,0);removed_identity(t);path(t->prepared,t->f.data,"native-prepared");path(t->state,t->prepared,t->f.selected.game);
    CHECK(chmod(t->prepared,0755)==0);char marker[P];path(marker,t->prepared,".blackwire-native-prepared");static const char scope[]="Blackwire native preparations v1\n";file(marker,scope,sizeof scope-1,0644);
    path(t->state_parked,t->f.root,"old-prepared-state");missing(&t->f);CHECK(rename(t->state,t->state_parked)==0);
    if(own)remove_registration(&t->f);if(commit)commit_removed(t);
}
static int manual_absent(void *opaque,const struct bw_nrc_subject *s){
    struct removed_fixture *t=opaque;t->manual_calls++;CHECK(!strcmp(s->source,t->f.source)&&!strcmp(s->content,t->f.content));
    return t->manual_unknown?-1:(int)t->manual_value;
}
static int observe_removed(struct removed_fixture *t,struct bw_nrc_result *r){
    struct bw_nrc_removed_observer o={t,current,sources,manual_absent};
    unsigned attempts=t->f.uninstalls;unsigned char a[32],b[32],x[32],y[32];char p[P];path(p,t->f.root,"full-manual/eboot.bin");
    hash_file(p,a);hash_file(t->f.db,x);int rc=bw_nrc_observe_removed(&t->f.env,t->f.selected.title,t->f.selected.run,&t->removed,&o,r);
    hash_file(p,b);hash_file(t->f.db,y);CHECK(!memcmp(a,b,32)&&!memcmp(x,y,32)&&t->f.uninstalls==attempts&&open_count==close_count);return rc;
}
static void no_completion(struct removed_fixture *t){char p[P];struct stat st;marker_path(&t->f,"absent",p);CHECK(lstat(p,&st)<0&&errno==ENOENT);no_pending(&t->f);}
static void observer_refused(struct removed_fixture *t){struct bw_nrc_result r;CHECK(observe_removed(t,&r)<0&&r.state==BW_NRC_UNKNOWN&&!r.uninstall_attempted);no_completion(t);}
static void automatic_marker(struct removed_fixture *t,const char *leaf_name,const char magic[8]){
    unsigned char owner[8224],marker[128]={0};char p[P];marker_path(&t->f,"owner",p);int fd=open(p,O_RDONLY|O_NOFOLLOW);
    CHECK(fd>=0&&read(fd,owner,sizeof owner)==sizeof owner&&close(fd)==0);memcpy(marker,magic,8);memcpy(marker+16,owner+8192,32);
    memcpy(marker+48,t->f.selected.run,33);memcpy(marker+82,t->f.selected.title,10);cp_sha_bytes(marker,96,marker+96);
    marker_path(&t->f,leaf_name,p);file(p,marker,sizeof marker,0600);
}
static void new_cases(void){
    struct removed_fixture t;struct bw_nrc_result r;char p[P],q[P];struct stat st;
    init_removed(&t,"removed-exact-owner-completes",1,1);CHECK(reconcile(&t.f,&r)<0&&r.state!=BW_NRC_ABSENT);no_completion(&t);
    CHECK(observe_removed(&t,&r)==0&&r.state==BW_NRC_ABSENT&&t.manual_calls>=4&&!r.uninstall_attempted);
    CHECK(observe_removed(&t,&r)==0&&r.state==BW_NRC_ABSENT);struct bw_nrc_candidate list[4];size_t count=9;CHECK(bw_nrc_candidates(&t.f.env,list,4,&count)==0&&!count);
    init_removed(&t,"removed-no-event-inert",0,1);CHECK(observe_removed(&t,&r)==0&&r.state==BW_NRC_UNOWNED);no_completion(&t);
    init_removed(&t,"removed-no-tombstone",1,0);observer_refused(&t);
    init_removed(&t,"removed-partial-tombstone",1,1);path(p,t.f.data,"removed-sources");char leaf_name[128];CHECK(snprintf(leaf_name,sizeof leaf_name,"%s.complete",t.f.selected.game)>0);path(q,p,leaf_name);CHECK(unlink(q)==0);observer_refused(&t);
    init_removed(&t,"removed-wrong-operation",1,1);t.removed.operation[31]='c';observer_refused(&t);
    init_removed(&t,"removed-wrong-receipt",1,1);t.removed.prepared_sha256[0]^=1;observer_refused(&t);
    init_removed(&t,"removed-wrong-source",1,1);strcpy(t.removed.identity.binding.source,"Different Game");observer_refused(&t);
    init_removed(&t,"removed-wrong-binding",1,1);t.removed.identity.binding.sha256[0]^=1;observer_refused(&t);
    init_removed(&t,"removed-valid-other-source-tombstone",1,0);strcpy(t.removed.identity.binding.source,"Other Backup");commit_removed(&t);observer_refused(&t);
    init_removed(&t,"removed-valid-other-generation-tombstone",1,0);t.removed.prepared_sha256[0]^=1;commit_removed(&t);observer_refused(&t);
    init_removed(&t,"removed-source-directory-replacement",1,1);make(t.f.source,0755);observer_refused(&t);
    init_removed(&t,"removed-source-symlink",1,1);CHECK(symlink(t.f.parked,t.f.source)==0);observer_refused(&t);
    init_removed(&t,"removed-source-file",1,1);file(t.f.source,"foreign",7,0644);observer_refused(&t);
    init_removed(&t,"removed-private-leaf-replacement",1,1);make(t.state,0700);observer_refused(&t);
    init_removed(&t,"removed-private-leaf-symlink",1,1);CHECK(symlink(t.state_parked,t.state)==0);observer_refused(&t);
    init_removed(&t,"removed-whole-private-parent-lost",1,1);path(p,t.prepared,".blackwire-native-prepared");CHECK(unlink(p)==0&&rmdir(t.prepared)==0);observer_refused(&t);
    init_removed(&t,"removed-private-parent-symlink",1,1);path(p,t.prepared,".blackwire-native-prepared");CHECK(unlink(p)==0&&rmdir(t.prepared)==0);CHECK(symlink(t.state_parked,t.prepared)==0);observer_refused(&t);
    init_removed(&t,"removed-wrong-prepared-mode",1,1);CHECK(chmod(t.prepared,0777)==0);observer_refused(&t);
    init_removed(&t,"removed-missing-prepared-marker",1,1);path(p,t.prepared,".blackwire-native-prepared");CHECK(unlink(p)==0);observer_refused(&t);
    init_removed(&t,"removed-replaced-prepared-marker",1,1);path(p,t.prepared,".blackwire-native-prepared");replace(p,"foreign",7,0644);observer_refused(&t);
    init_removed(&t,"removed-games-parent-replaced",1,1);path(q,t.f.root,"old-games");CHECK(rename(t.f.games,q)==0);make(t.f.games,0755);observer_refused(&t);
    init_removed(&t,"removed-apps-parent-replaced",1,1);path(q,t.f.root,"old-apps");CHECK(rename(t.f.apps,q)==0);make(t.f.apps,0755);observer_refused(&t);
    init_removed(&t,"removed-app-directory-present",1,1);make(t.f.app,0755);observer_refused(&t);
    init_removed(&t,"removed-db-title-present",1,1);row(&t.f,t.f.content);observer_refused(&t);
    init_removed(&t,"removed-db-foreign-reinstall",1,1);row(&t.f,"foreign");observer_refused(&t);
    init_removed(&t,"removed-collector-unknown",1,1);t.f.sources_value=-1;observer_refused(&t);
    init_removed(&t,"removed-manual-unknown",1,1);t.manual_unknown=1;observer_refused(&t);
    init_removed(&t,"removed-manual-present",1,1);t.manual_value=1;observer_refused(&t);
    init_removed(&t,"removed-reservation-lost",1,1);t.f.current_value=-1;observer_refused(&t);
    const char *auto_leaves[]={"pending","retire","uninstall"};const char *auto_magic[]={"BWROCP01","BWROCR01","BWROCU01"};
    for(unsigned i=0;i<3;i++){init_removed(&t,i==0?"removed-existing-pending-veto":i==1?"removed-existing-retire-veto":"removed-existing-uninstall-veto",1,1);automatic_marker(&t,auto_leaves[i],auto_magic[i]);CHECK(observe_removed(&t,&r)<0&&r.state==BW_NRC_UNKNOWN&&!t.f.uninstalls);marker_path(&t.f,"absent",p);CHECK(lstat(p,&st)<0&&errno==ENOENT);}
    init_removed(&t,"removed-late-source-conflict",1,1);t.f.refuse_sources_at=t.f.source_calls+3;observer_refused(&t);
    enum fault bad[]={F_AT_ACCESS,F_AT_NOTDIR,F_AT_IO};for(unsigned i=0;i<3;i++){init_removed(&t,i==0?"removed-eacces":i==1?"removed-notdir":"removed-io-error",1,1);fault=bad[i];observer_refused(&t);}
    init_removed(&t,"removed-partial-marker-no-retry",1,1);absent_write_fault=1;CHECK(observe_removed(&t,&r)<0&&r.state==BW_NRC_UNKNOWN);absent_write_fault=0;CHECK(observe_removed(&t,&r)<0&&!t.f.uninstalls);
    init_removed(&t,"removed-marker-close-unknown-readback",1,1);absent_close_fault=1;CHECK(observe_removed(&t,&r)<0&&r.state==BW_NRC_UNKNOWN);absent_close_fault=0;CHECK(observe_removed(&t,&r)==0&&r.state==BW_NRC_ABSENT&&!t.f.uninstalls);
    init_removed(&t,"removed-foreign-reinstalled-after-complete",1,1);CHECK(observe_removed(&t,&r)==0&&r.state==BW_NRC_ABSENT);registered(&t.f);CHECK(observe_removed(&t,&r)<0&&r.state==BW_NRC_UNKNOWN&&!t.f.uninstalls);
    init_removed(&t,"removed-restart-observation-only",1,1);pid_t child=fork();CHECK(child>=0);if(!child){execl(program,program,"--removed",t.f.root,t.f.selected.title,t.f.selected.run,(char *)NULL);_exit(121);}int status;CHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0);CHECK(observe_removed(&t,&r)==0&&r.state==BW_NRC_ABSENT);
    CHECK(open_count==close_count);
}
int main(int argc,char **argv){
    if(argc==5&&!strcmp(argv[1],"--removed")){
        struct removed_fixture t={0};strcpy(t.f.root,argv[2]);strcpy(t.f.selected.title,argv[3]);strcpy(t.f.selected.run,argv[4]);
        path(t.f.data,t.f.root,"data");path(t.f.games,t.f.data,"games");path(t.f.apps,t.f.root,"apps");path(t.f.db,t.f.root,"app.db");
        memcpy(t.f.selected.game,t.f.selected.title,9);t.f.selected.game[9]='_';memset(t.f.selected.game+10,'a',64);path(t.f.source,t.f.games,t.f.selected.game);
        CHECK(snprintf(t.f.content,sizeof t.f.content,"UP0000-%s_00-BLACKWIRETEST0001",t.f.selected.title)>0);t.f.env=(struct bw_nrc_environment){t.f.data,t.f.apps,t.f.db};t.f.ops=(struct bw_nrc_ops){&t,current,sources,uninstall};
        size_t count=0;CHECK(bw_removed_list(t.f.data,&t.removed,1,&count)==0&&count==1);struct bw_nrc_removed_observer o={&t,current,sources,manual_absent};struct bw_nrc_result r;
        CHECK(bw_nrc_observe_removed(&t.f.env,t.f.selected.title,t.f.selected.run,&t.removed,&o,&r)==0&&r.state==BW_NRC_ABSENT&&!t.f.uninstalls&&open_count==close_count);return 0;
    }
    if(argc==5)return retained_cleanup_main(argc,argv);
    program=argv[0];case_name="startup";CHECK(argc==2&&strlen(argv[1])<P);strcpy(suite,argv[1]);make(suite,0700);
    observations();vetoes();fault_cases();acquisition_edges();listing();new_cases();CHECK(open_count==close_count);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"realFilesystem\":true,\"realSqlite\":true,\"moduleOpens\":%u,\"moduleCloses\":%u,\"normalSmpCallbacksModeled\":true,\"consoleActions\":0}\n",cases,checks,open_count,close_count);return 0;
}
