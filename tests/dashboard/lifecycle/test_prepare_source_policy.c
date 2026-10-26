/* Actual initial lifecycle/ledger/SQLite plus prepare-only and strict collectors.
 * Only normal SMP transport responses and the controller lease are modeled. */
#define BW_INITIAL_ABSENCE_MAIN retained_initial_absence_main
#define bw_sm_observe retained_prepare_observe
#include "test_registration_initial_absence.c"
#undef bw_sm_observe
#undef BW_INITIAL_ABSENCE_MAIN

static unsigned mixed_mode,policy_cases,vary_snapshot;
static char unrelated[P];
int bw_sm_observe(const struct bw_sm_connection *c,enum bw_sm_observation kind,struct bw_sm_document *out){
    int rc=retained_prepare_observe(c,kind,out);if(rc||!mixed_mode)return rc;
    char addition[P+128];addition[0]=0;
    if(kind==BW_SM_OBSERVE_GAMES&&mixed_mode==1)
        CHECK(snprintf(addition,sizeof addition,"{\"path\":\"%s%s\",\"title_id\":\"PPSA90002\"}",unrelated,vary_snapshot&&api->observations>=3?"-changed":"")>0);
    else if(kind==BW_SM_OBSERVE_MANUAL_PATHS&&mixed_mode!=1)
        CHECK(snprintf(addition,sizeof addition,"\"%s\"",unrelated)>0);
    if(!addition[0])return 0;
    char *last=strrchr((char *)out->bytes,']'),*count=strstr((char *)out->bytes,"\"count\":");CHECK(last&&count);count+=8;
    CHECK(*count=='0'||*count=='1');unsigned had=(unsigned)(*count-'0');*count=(char)('1'+had);
    size_t prefix=(size_t)(last-(char *)out->bytes),length=prefix+strlen(addition)+8;char *text=malloc(length);CHECK(text);
    int n=snprintf(text,length,"%.*s%s%s]}",(int)prefix,(char *)out->bytes,had?",":"",addition);CHECK(n>0&&(size_t)n<length);
    bw_sm_document_clear(out);document(out,text);free(text);return 0;
}
static int witness_value,witness_calls,witness_flip;
static int parser_witness(void *p,const struct bw_nrc_subject *s){CHECK(p&&s);witness_calls++;return witness_flip&&witness_calls>2?1:witness_value;}
static void parser_case(struct lifecycle_fixture *t,const char *row,const char *manual,int expected,unsigned unknown_rows,unsigned unknown_paths){
    policy_cases++;
    char games[16384],paths[8192];int n=snprintf(games,sizeof games,"{\"status\":0,\"count\":%u,\"games\":[%s]}",row[0]?1u:0u,row);CHECK(n>0&&(size_t)n<sizeof games);
    n=snprintf(paths,sizeof paths,"{\"status\":0,\"count\":%u,\"paths\":[%s]}",manual[0]?1u:0u,manual);CHECK(n>0&&(size_t)n<sizeof paths);
    struct bw_nrc_subject s={0};strcpy(s.source,t->f.source);strcpy(s.title,t->f.selected.title);strcpy(s.content,t->f.content);
    struct bw_nrc_remote remote={.connection={.current=register_current,.opaque=t}};witness_calls=0;
    int rc=bw_nrc_remote_prepare_documents(&remote,&s,games,strlen(games),paths,strlen(paths),parser_witness,t);
    CHECK((rc==0)==(expected==0));
    if(!rc){CHECK(remote.preparation_only&&remote.unknown_game_rows==unknown_rows&&remote.unknown_manual_paths==unknown_paths);}
    CHECK(!t->adds&&!t->removes&&!t->uninstall_calls&&!t->unmount_calls&&open_count==close_count);
}
static void parser_cases(void){
    struct lifecycle_fixture t;cached_absent(&t,"prepare-parser-matrix");mixed_mode=0;witness_value=witness_flip=0;
    char row[8192],manual[4096],other[P];path(other,t.f.root,"older-source");
    CHECK(snprintf(row,sizeof row,"{\"path\":\"%s\",\"title_id\":\"PPSA90002\"}",other)>0);parser_case(&t,row,"",0,1,0);
    CHECK(snprintf(row,sizeof row,"{\"path\":\"%s\",\"title_id\":\"\"}",other)>0);parser_case(&t,row,"",0,1,0);
    const char *suffix[]={"/","/.","/child","/../"};
    for(unsigned i=0;i<4;i++){CHECK(snprintf(row,sizeof row,"{\"path\":\"%s%s\",\"title_id\":\"PPSA90002\"}",t.f.source,suffix[i])>0);parser_case(&t,row,"",-1,0,0);}
    CHECK(snprintf(row,sizeof row,"{\"path\":\"%s\",\"title_id\":\"PPSA90002\"}",t.f.games)>0);parser_case(&t,row,"",-1,0,0);
    char alias[P];path(alias,t.f.root,"selected-alias");CHECK(!symlink(t.f.source,alias));
    CHECK(snprintf(row,sizeof row,"{\"path\":\"%s\",\"title_id\":\"PPSA90002\"}",alias)>0);parser_case(&t,row,"",-1,0,0);
    CHECK(snprintf(manual,sizeof manual,"\"%s\"",alias)>0);parser_case(&t,"",manual,-1,0,0);
    CHECK(snprintf(row,sizeof row,"{\"path\":\"%s\",\"title_id\":\"%s\"}",other,t.f.selected.title)>0);parser_case(&t,row,"",-1,0,0);
    CHECK(snprintf(row,sizeof row,"{\"path\":\"%s\",\"path\":\"%s\",\"title_id\":\"PPSA90002\"}",other,other)>0);parser_case(&t,row,"",-1,0,0);
    parser_case(&t,"{\"title_id\":\"PPSA90002\"}","",-1,0,0);
    char image[P];path(image,t.f.root,"other.ffpfsc");file(image,"neutral fixture",15,0600);CHECK(snprintf(manual,sizeof manual,"\"%s\"",image)>0);parser_case(&t,"",manual,0,0,1);
    path(other,t.f.root,"missing-manual");CHECK(snprintf(manual,sizeof manual,"\"%s\"",other)>0);parser_case(&t,"",manual,0,0,1);
    witness_value=-1;parser_case(&t,"",manual,-1,0,0);witness_value=0;witness_flip=1;parser_case(&t,"",manual,-1,0,0);witness_flip=0;
    CHECK(snprintf(row,sizeof row,"{\"path\":\"%s\",\"runtime_path\":\"%s\",\"source_type\":\"folder\",\"title_id\":\"%s\",\"content_id\":\"%s\",\"installed\":true,\"installed_pkg\":false,\"managed\":false,\"source_available\":true,\"image_backed\":false,\"mounted\":true}",t.f.source,t.f.source,t.f.selected.title,t.f.content)>0);
    parser_case(&t,row,"",-1,0,0);witness_value=1;parser_case(&t,row,"",0,0,0);witness_value=0;
    {
        size_t capacity=512u*1024u,used=0;char *games=malloc(capacity);CHECK(games);
        int n=snprintf(games,capacity,"{\"status\":0,\"count\":300,\"games\":[");CHECK(n>0);used=(size_t)n;
        for(unsigned i=0;i<300;i++){
            n=snprintf(games+used,capacity-used,"%s{\"path\":\"%s/older-%u\",\"title_id\":\"PPSA90002\"}",i?",":"",t.f.root,i);
            CHECK(n>0&&(size_t)n<capacity-used);used+=(size_t)n;
        }
        CHECK(used+3<capacity);memcpy(games+used,"]}",3);used+=2;
        struct bw_nrc_subject subject={0};strcpy(subject.source,t.f.source);strcpy(subject.title,t.f.selected.title);strcpy(subject.content,t.f.content);
        struct bw_nrc_remote remote={.connection={.current=register_current,.opaque=&t}};const char empty[]="{\"status\":0,\"count\":0,\"paths\":[]}";witness_calls=0;
        CHECK(!bw_nrc_remote_prepare_documents(&remote,&subject,games,used,empty,sizeof empty-1,parser_witness,&t));
        CHECK(remote.preparation_only&&remote.unknown_game_rows==300&&!remote.unknown_manual_paths);free(games);policy_cases++;
    }
}
int main(int argc,char **argv){
    CHECK(argc==2&&strlen(argv[1])<P);program=argv[0];case_name="startup";strcpy(suite,argv[1]);make(suite,0700);parser_cases();
    for(unsigned mode=1;mode<=5;mode++){
        char name[64];snprintf(name,sizeof name,"prepare-existing-library-%u",mode);struct lifecycle_fixture t;cached_absent(&t,name);mixed_mode=mode;
        path(unrelated,t.f.root,mode==2?"other.ffpfsc":mode==3?"stale-source":"other-source");
        if(mode==2)file(unrelated,"neutral fixture",15,0600);
        if(mode==4)make(unrelated,0755);
        if(mode==5){char target[P];path(target,t.f.root,"other-real");make(target,0755);CHECK(!symlink(target,unrelated));}
        struct bw_nrc_result result;CHECK(!bw_nrl_begin(&t.request,&t.owner,&result)&&t.owner&&result.state==BW_NRC_INTENT);
        struct bw_register_result registered_result={0};normal_register(&t,&registered_result);
        CHECK(!bw_nrl_registered(t.owner,0,&registered_result,&result)&&result.state==BW_NRC_OWNED);
        struct bw_native_reader_release release=release_of(&t);struct bw_nrc_subject subject=owner_subject(&t);
        struct bw_nrc_remote remote={.connection={.current=retirement_guard,.opaque=&t}};
        CHECK(bw_nrc_remote_sources(&remote,&subject)<0&&!remote.preparation_only);
        struct bw_nrc_retirement_result retired;CHECK(retire(&t,&release,&retired)<0&&!t.removes&&!t.uninstall_calls&&!t.unmount_calls);
        CHECK(bw_native_registration_state(t.f.data,t.f.apps,t.f.db,&t.f.selected)==BW_REG_MATCH);finish(&t);
        policy_cases++;
    }
    for(unsigned changed=0;changed<2;changed++){
        struct lifecycle_fixture t;cached_absent(&t,changed?"prepare-db-witness-lost":"prepare-app-witness-lost");mixed_mode=3;path(unrelated,t.f.root,"stale-source");
        struct bw_nrc_result result;CHECK(!bw_nrl_begin(&t.request,&t.owner,&result)&&result.state==BW_NRC_INTENT);
        struct bw_register_result registered_result={0};normal_register(&t,&registered_result);
        if(changed)row(&t.f,"FOREIGN-CONTENT");
        else{char link[P];path(link,t.f.app,"mount.lnk");replace(link,"/unrelated/source\n",18,0644);}
        CHECK(bw_nrl_registered(t.owner,0,&registered_result,&result)<0&&result.state==BW_NRC_UNKNOWN);
        char owner[P];struct stat st;marker_path(&t.f,"owner",owner);CHECK(lstat(owner,&st)<0&&errno==ENOENT);
        CHECK(!t.removes&&!t.uninstall_calls&&!t.unmount_calls);finish(&t);policy_cases++;
    }
    {
        struct lifecycle_fixture t;cached_absent(&t,"prepare-preexisting-match-stays-unowned");mixed_mode=3;path(unrelated,t.f.root,"stale-source");registered(&t.f);
        struct bw_nrc_result result;CHECK(!bw_nrl_begin(&t.request,&t.owner,&result)&&result.state==BW_NRC_UNOWNED);
        struct bw_register_result existing={.registered=1,.message="preexisting registration preserved"};
        CHECK(!bw_nrl_registered(t.owner,0,&existing,&result)&&result.state==BW_NRC_UNOWNED);
        CHECK(!t.adds&&!t.removes&&!t.uninstall_calls&&!t.unmount_calls);finish(&t);policy_cases++;
    }
    {
        struct lifecycle_fixture t;cached_absent(&t,"prepare-apps-observer-close-unknown");mixed_mode=0;
        arm_release_close(t.f.apps);struct bw_nrc_result result;
        CHECK(bw_nrl_begin(&t.request,&t.owner,&result)==BW_NRC_RELEASE_UNCONFIRMED&&!t.owner);
        release_close_enabled=0;CHECK(release_close_hits>=1&&open_count==close_count);
        CHECK(!t.adds&&!t.removes&&!t.uninstall_calls&&!t.unmount_calls);policy_cases++;
    }
    {
        struct lifecycle_fixture t;cached_absent(&t,"prepare-snapshot-changed");mixed_mode=1;vary_snapshot=1;path(unrelated,t.f.root,"unrelated-old-row");
        struct bw_nrc_result result;CHECK(bw_nrl_begin(&t.request,&t.owner,&result)<0&&!t.owner);
        CHECK(result.detail&&!strcmp(result.detail,"cleanup_remote_snapshot_changed"));
        CHECK(!t.adds&&!t.removes&&!t.uninstall_calls&&!t.unmount_calls&&open_count==close_count);vary_snapshot=0;policy_cases++;
    }
    mixed_mode=0;
    printf("{\"passed\":true,\"cases\":%u,\"fixtures\":%u,\"checks\":%u,\"actualReadonlySqlite\":true,\"prepareOwnerNeedsExactRegistration\":true,\"strictRetirementRefusesUnknownSources\":true,\"consoleActions\":0}\n",policy_cases,cases,checks);return 0;
}
