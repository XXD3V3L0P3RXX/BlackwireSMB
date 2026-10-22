/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real ledger/filesystem/SQLite tests; normal SMP transport and complete idle,
 * process-release and scan-root observations are explicit modeled callbacks. */
#define nrc_test_write nrc_legacy_test_write
#define main retained_cleanup_main
#include "test_registration_cleanup.c"
#undef main
#undef nrc_test_write
#include "native_registration_retirement.h"

static const char *retire_write_fault;
static unsigned retire_write_faulted;
ssize_t nrc_test_write(int fd,const void *bytes,size_t size){
    if(retire_write_fault&&suffix(fd,retire_write_fault)){
        if(retire_write_faulted++){errno=EIO;return -1;}
        return write(fd,bytes,size>7?7:size);
    }
    return nrc_legacy_test_write(fd,bytes,size);
}
struct retirement_fixture {
    struct fixture f; /* Existing callbacks use this exact first member. */
    struct bw_nrc_retirement_ops ops;
    char manual[P],save[P];
    unsigned released_calls,root_calls,manual_calls,removes;
    int release_value,roots_value,manual_unknown,remove_value,remove_effect;
};
static void exact_subject(struct retirement_fixture *t,const struct bw_nrc_subject *s){
    CHECK(!strcmp(s->title,t->f.selected.title)&&!strcmp(s->run,t->f.selected.run));
    CHECK(!strcmp(s->source,t->f.source)&&!strcmp(s->content,t->f.content));
}
static int released(void *opaque,const struct bw_nrc_subject *s){
    struct retirement_fixture *t=opaque;exact_subject(t,s);t->released_calls++;return t->release_value;
}
static int outside(void *opaque,const struct bw_nrc_subject *s){
    struct retirement_fixture *t=opaque;exact_subject(t,s);t->root_calls++;return t->roots_value;
}
static int manual_present(void *opaque,const struct bw_nrc_subject *s){
    struct retirement_fixture *t=opaque;exact_subject(t,s);t->manual_calls++;if(t->manual_unknown)return -1;
    FILE *in=fopen(t->manual,"r");if(!in)return -1;char line[P+2];unsigned found=0;
    while(fgets(line,sizeof line,in)){size_t n=strlen(line);if(!n||line[n-1]!='\n'){CHECK(fclose(in)==0);return -1;}line[n-1]=0;if(!strcmp(line,s->source))found++;}
    int bad=ferror(in);if(fclose(in))bad=1;return bad||found>1?-1:found?1:0;
}
static void manual_set(struct retirement_fixture *t,int present){
    FILE *out=fopen(t->manual,"w");CHECK(out);if(present)CHECK(fprintf(out,"%s\n",t->f.source)>0);
    CHECK(fprintf(out,"%s/full-manual\n",t->f.root)>0);CHECK(fflush(out)==0&&fsync(fileno(out))==0&&fclose(out)==0);
}
static void durable(struct fixture *f,const char *leaf_name,const char magic[8]){
    char p[P];marker_path(f,leaf_name,p);struct stat st;CHECK(lstat(p,&st)==0&&S_ISREG(st.st_mode)&&st.st_nlink==1&&(st.st_mode&07777)==0600&&st.st_size==128);
    int fd=open(p,O_RDONLY|O_NOFOLLOW);CHECK(fd>=0);char got[8];CHECK(read(fd,got,8)==8&&!memcmp(got,magic,8));CHECK(close(fd)==0);
}
static int manual_remove(void *opaque,const struct bw_nrc_subject *s){
    struct retirement_fixture *t=opaque;exact_subject(t,s);CHECK(++t->removes==1);
    durable(&t->f,"pending","BWROCP01");durable(&t->f,"retire","BWROCR01");
    if(t->remove_effect)manual_set(t,0);return t->remove_value;
}
static int retirement_uninstall(void *opaque,const char title[10]){
    struct retirement_fixture *t=opaque;durable(&t->f,"uninstall","BWROCU01");
    struct bw_nrc_subject s={0};strcpy(s.title,t->f.selected.title);strcpy(s.run,t->f.selected.run);
    strcpy(s.source,t->f.source);strcpy(s.content,t->f.content);CHECK(manual_present(t,&s)==0);
    CHECK(t->released_calls&&t->root_calls&&pending_synced&&pending_dir_synced);
    return uninstall(&t->f,title);
}
static void init_retirement(struct retirement_fixture *t,const char *name,int own){
    memset(t,0,sizeof *t);init(&t->f,name);retire_write_fault=NULL;retire_write_faulted=0;
    path(t->manual,t->f.root,"manual.lst");path(t->save,t->f.root,"save.data");file(t->save,"saved game remains\n",19,0600);
    manual_set(t,1);t->remove_effect=1;
    t->ops=(struct bw_nrc_retirement_ops){.registration=t->f.ops,.released=released,.outside_scan_roots=outside,.manual_present=manual_present,.manual_remove=manual_remove};
    t->ops.registration.opaque=t;t->ops.registration.uninstall=retirement_uninstall;
    if(own)owned(&t->f,0);
}
static int retire_call(struct retirement_fixture *t,struct bw_nrc_retirement_result *r){
    char p[P];path(p,t->f.source,"eboot.bin");unsigned char before[5][32],after[5][32];
    const char *names[]={p,t->f.receipt,t->save,t->f.db};
    for(unsigned i=0;i<4;i++)hash_file(names[i],before[i]);
    char full[P];path(full,t->f.root,"full-manual/eboot.bin");hash_file(full,before[4]);
    int rc=bw_nrc_retire_present(&t->f.env,t->f.selected.title,t->f.selected.run,&t->ops,r);
    for(unsigned i=0;i<4;i++)hash_file(names[i],after[i]);hash_file(full,after[4]);
    CHECK(!memcmp(before,after,sizeof before)&&open_count==close_count);return rc;
}
static int observe_call(struct retirement_fixture *t,struct bw_nrc_retirement_result *r){
    struct bw_nrc_retirement_observer o={t,t->f.ops.current,t->f.ops.sources,outside,manual_present};
    unsigned releases=t->released_calls,removes=t->removes,requests=t->f.uninstalls;
    int rc=bw_nrc_retirement_observe(&t->f.env,t->f.selected.title,t->f.selected.run,&o,r);
    CHECK(releases==t->released_calls&&removes==t->removes&&requests==t->f.uninstalls&&open_count==close_count);return rc;
}
static void observer_cases(void){
    struct retirement_fixture t;struct bw_nrc_retirement_result r;char p[P];struct stat st;
    init_retirement(&t,"observe-no-owner-inert",0);CHECK(observe_call(&t,&r)==0&&r.registration.state==BW_NRC_UNOWNED);
    init_retirement(&t,"observe-owned-not-pending-inert",1);CHECK(observe_call(&t,&r)==0&&r.registration.state==BW_NRC_BLOCKED);no_pending(&t.f);
    init_retirement(&t,"observe-pending-no-next-uninstall",1);t.remove_effect=0;t.remove_value=-1;CHECK(retire_call(&t,&r)==0&&t.removes==1&&!t.f.uninstalls);
    manual_set(&t,0);t.release_value=-1;CHECK(observe_call(&t,&r)==0&&r.registration.state==BW_NRC_PENDING&&!t.f.uninstalls);
    marker_path(&t.f,"uninstall",p);CHECK(lstat(p,&st)<0&&errno==ENOENT);
    remove_registration(&t.f);CHECK(observe_call(&t,&r)==0&&r.registration.state==BW_NRC_ABSENT&&!t.f.uninstalls);CHECK(observe_call(&t,&r)==0&&r.registration.state==BW_NRC_ABSENT);
    init_retirement(&t,"observe-manual-presence-veto",1);t.remove_effect=0;t.remove_value=1;CHECK(retire_call(&t,&r)==0);remove_registration(&t.f);CHECK(observe_call(&t,&r)==0&&r.registration.state==BW_NRC_PENDING);
    init_retirement(&t,"observe-manual-unknown-veto",1);CHECK(retire_call(&t,&r)==0);remove_registration(&t.f);t.manual_unknown=1;CHECK(observe_call(&t,&r)==0&&r.registration.state==BW_NRC_PENDING);
    init_retirement(&t,"observe-partial-app-absence-veto",1);CHECK(retire_call(&t,&r)==0);sql(&t.f,"DELETE FROM tbl_contentinfo WHERE titleId='PPSA10001';");CHECK(observe_call(&t,&r)==0&&r.registration.state==BW_NRC_PENDING);
    init_retirement(&t,"observe-scan-root-changed-veto",1);CHECK(retire_call(&t,&r)==0);remove_registration(&t.f);t.roots_value=-1;CHECK(observe_call(&t,&r)<0);
    init_retirement(&t,"observe-source-replacement-veto",1);CHECK(retire_call(&t,&r)==0);remove_registration(&t.f);unsigned char bytes[384];int fd=open(t.f.receipt,O_RDONLY);CHECK(fd>=0&&read(fd,bytes,sizeof bytes)==sizeof bytes&&close(fd)==0);replace(t.f.receipt,bytes,sizeof bytes,0600);CHECK(observe_call(&t,&r)<0);
    init_retirement(&t,"observe-missing-pending-no-create",1);fault=F_PENDING_OPEN;CHECK(retire_call(&t,&r)<0);fault=F_NONE;remove_registration(&t.f);manual_set(&t,0);CHECK(observe_call(&t,&r)==0&&r.registration.state==BW_NRC_BLOCKED);marker_path(&t.f,"absent",p);CHECK(lstat(p,&st)<0&&errno==ENOENT);
    init_retirement(&t,"observe-foreign-reinstalled-veto",1);CHECK(retire_call(&t,&r)==0);remove_registration(&t.f);CHECK(observe_call(&t,&r)==0&&r.registration.state==BW_NRC_ABSENT);registered(&t.f);CHECK(observe_call(&t,&r)==0&&r.registration.state==BW_NRC_PENDING&&!r.registration.uninstall_attempted);
}
static void retirement_cases(void){
    struct retirement_fixture t;struct bw_nrc_retirement_result r;struct bw_nrc_result old;char p[P],q[P];
    init_retirement(&t,"retire-present-owned",1);
    CHECK(reconcile(&t.f,&old)==0&&old.state==BW_NRC_BLOCKED); /* Old-code negative control. */
    CHECK(retire_call(&t,&r)==0&&r.registration.state==BW_NRC_PENDING&&r.manual_remove_attempted&&r.manual_remove_accepted&&r.manual_absent&&r.registration.uninstall_attempted&&t.f.uninstalls==1);
    CHECK(retire_call(&t,&r)==0&&r.registration.state==BW_NRC_PENDING&&!r.manual_remove_attempted&&!r.registration.uninstall_attempted&&t.removes==1&&t.f.uninstalls==1);
    remove_registration(&t.f);CHECK(retire_call(&t,&r)==0&&r.registration.state==BW_NRC_ABSENT&&r.manual_absent);CHECK(retire_call(&t,&r)==0&&r.registration.state==BW_NRC_ABSENT&&t.f.uninstalls==1);
    struct bw_nrc_candidate list[4];size_t count=9;CHECK(bw_nrc_candidates(&t.f.env,list,4,&count)==0&&!count);
    init_retirement(&t,"retire-preexisting-match-inert",0);registered(&t.f);CHECK(retire_call(&t,&r)==0&&r.registration.state==BW_NRC_UNOWNED&&!t.removes&&!t.f.uninstalls);
    init_retirement(&t,"retire-reader-active-refused",1);t.release_value=1;CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);no_pending(&t.f);
    init_retirement(&t,"retire-reader-release-unknown",1);t.release_value=-1;CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);no_pending(&t.f);
    init_retirement(&t,"retire-custom-scan-root-refused",1);t.roots_value=1;CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);no_pending(&t.f);
    init_retirement(&t,"retire-scan-roots-unknown",1);t.roots_value=-1;CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);no_pending(&t.f);
    init_retirement(&t,"retire-full-console-idle-unknown",1);t.f.current_value=-1;CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);no_pending(&t.f);
    const char *conflicts[]={"retire-foreign-full","retire-foreign-manual","retire-foreign-pkg","retire-foreign-image","retire-external-operation-unknown"};
    for(unsigned i=0;i<5;i++){init_retirement(&t,conflicts[i],1);t.f.sources_value=-1;CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);no_pending(&t.f);}
    init_retirement(&t,"retire-independent-pkg-veto",1);path(p,t.f.app,"app.pkg");file(p,"foreign package",15,0644);CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);no_pending(&t.f);
    init_retirement(&t,"retire-same-title-db-replaced",1);sql(&t.f,"UPDATE tbl_contentinfo SET contentId='foreign' WHERE titleId='PPSA10001';");CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);no_pending(&t.f);
    init_retirement(&t,"retire-source-param-replaced",1);path(p,t.f.source,"sce_sys/param.json");replace(p,t.f.param,strlen(t.f.param),0644);CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);
    init_retirement(&t,"retire-mount-replaced",1);path(p,t.f.app,"mount.lnk");replace(p,t.f.source,strlen(t.f.source),0644);CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);
    init_retirement(&t,"retire-receipt-replaced",1);unsigned char bytes[384];int fd=open(t.f.receipt,O_RDONLY);CHECK(fd>=0&&read(fd,bytes,sizeof bytes)==sizeof bytes&&close(fd)==0);replace(t.f.receipt,bytes,sizeof bytes,0600);CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);
    init_retirement(&t,"retire-manual-list-unknown",1);t.manual_unknown=1;CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);no_pending(&t.f);
    init_retirement(&t,"retire-manual-lost-reply-readback",1);t.remove_value=-1;CHECK(retire_call(&t,&r)==0&&t.removes==1&&!r.manual_remove_accepted&&r.manual_absent&&t.f.uninstalls==1);
    init_retirement(&t,"retire-manual-refused-no-retry",1);t.remove_effect=0;t.remove_value=1;CHECK(retire_call(&t,&r)==0&&r.registration.state==BW_NRC_PENDING&&t.removes==1&&!t.f.uninstalls);CHECK(retire_call(&t,&r)==0&&t.removes==1&&!t.f.uninstalls);
    init_retirement(&t,"retire-manual-lost-no-effect-no-retry",1);t.remove_effect=0;t.remove_value=-1;CHECK(retire_call(&t,&r)==0&&t.removes==1&&!t.f.uninstalls);CHECK(retire_call(&t,&r)==0&&t.removes==1&&!t.f.uninstalls);
    init_retirement(&t,"retire-old-observer-cannot-complete",1);t.remove_effect=0;t.remove_value=1;CHECK(retire_call(&t,&r)==0);remove_registration(&t.f);
    CHECK(reconcile(&t.f,&old)==0&&old.state==BW_NRC_PENDING&&!t.f.uninstalls);marker_path(&t.f,"absent",p);struct stat st;CHECK(lstat(p,&st)<0&&errno==ENOENT);
    CHECK(bw_nrc_candidates(&t.f.env,list,4,&count)==0&&count==1&&list[0].retiring&&list[0].pending);
    manual_set(&t,0);CHECK(retire_call(&t,&r)==0&&r.registration.state==BW_NRC_ABSENT&&!t.f.uninstalls);
    init_retirement(&t,"retire-crash-before-pending-inert",1);fault=F_PENDING_OPEN;CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);fault=F_NONE;
    CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);CHECK(reconcile(&t.f,&old)==0&&old.state==BW_NRC_PENDING&&!t.f.uninstalls);
    CHECK(bw_nrc_candidates(&t.f.env,list,4,&count)==0&&count==1&&list[0].retiring&&!list[0].pending);
    init_retirement(&t,"retire-manual-already-absent",1);manual_set(&t,0);CHECK(retire_call(&t,&r)==0&&!t.removes&&r.manual_absent&&t.f.uninstalls==1);
    for(int status=-1;status<=1;status++){init_retirement(&t,status<0?"retire-uninstall-unknown":status?"retire-uninstall-refused":"retire-uninstall-accepted",1);t.f.uninstall_value=status;CHECK(retire_call(&t,&r)==0&&t.f.uninstalls==1&&r.registration.uninstall_accepted==(unsigned)(status==0));CHECK(retire_call(&t,&r)==0&&t.f.uninstalls==1&&t.removes==1);}
    init_retirement(&t,"retire-pending-before-manual-partial",1);retire_write_fault=".retire";CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);retire_write_fault=NULL;CHECK(retire_call(&t,&r)<0&&!t.removes&&!t.f.uninstalls);
    init_retirement(&t,"retire-uninstall-marker-partial",1);retire_write_fault=".uninstall";CHECK(retire_call(&t,&r)==0&&r.registration.state==BW_NRC_PENDING&&t.removes==1&&!t.f.uninstalls);retire_write_fault=NULL;CHECK(retire_call(&t,&r)<0&&t.removes==1&&!t.f.uninstalls);
    init_retirement(&t,"retire-app-dir-only-not-complete",1);CHECK(retire_call(&t,&r)==0);path(q,t.f.root,"app-backup");CHECK(rename(t.f.app,q)==0);CHECK(retire_call(&t,&r)==0&&r.registration.state==BW_NRC_PENDING&&t.f.uninstalls==1);
    init_retirement(&t,"retire-db-only-not-complete",1);CHECK(retire_call(&t,&r)==0);sql(&t.f,"DELETE FROM tbl_contentinfo WHERE titleId='PPSA10001';");CHECK(retire_call(&t,&r)==0&&r.registration.state==BW_NRC_PENDING&&t.f.uninstalls==1);
    init_retirement(&t,"retire-foreign-reinstalled-preserved",1);CHECK(retire_call(&t,&r)==0);remove_registration(&t.f);CHECK(retire_call(&t,&r)==0&&r.registration.state==BW_NRC_ABSENT);registered(&t.f);CHECK(retire_call(&t,&r)<0&&t.f.uninstalls==1);
    CHECK(open_count==close_count);
}
int main(int argc,char **argv){
    if(argc==5)return retained_cleanup_main(argc,argv);
    program=argv[0];case_name="startup";CHECK(argc==2&&strlen(argv[1])<P);strcpy(suite,argv[1]);make(suite,0700);
    observations();vetoes();fault_cases();acquisition_edges();listing();retirement_cases();observer_cases();
    CHECK(open_count==close_count);printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"realFilesystem\":true,\"realSqlite\":true,\"moduleOpens\":%u,\"moduleCloses\":%u,\"normalSmpCallbacksModeled\":true,\"consoleActions\":0}\n",cases,checks,open_count,close_count);return 0;
}
