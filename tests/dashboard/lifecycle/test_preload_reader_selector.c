/* Actual read-only selector on synthetic fixed control material. */
static int selecting;
static unsigned selection_writes;
static unsigned select_cancel_at;
#define main retained_upgrade_main
#include "test_upgrade_fixture.c"
#undef main
static void reader_remove(void){char folder[2048],name[2048];joined_path(folder,current_root,"fakelib2");for(unsigned i=0;i<5;i++){joined_path(name,folder,pb_globals[i].name);CHECK(!unlink(name));}CHECK(!rmdir(folder));}
static void select_case(struct bw_preload_bundle_upgrade_request *q,int expected,int pin_kind){
 uint8_t receipt[32],map[32],before[32],after[32];char name[2048];joined_path(name,current_state,"prepared.bin");digest(name,receipt);joined_path(name,current_root,"blackwire.files");digest(name,map);
 joined_path(name,current_root,"fakelib2/libSceAmpr.sprx");struct stat identity,final;int present=!lstat(name,&identity),regular=present&&S_ISREG(identity.st_mode);if(regular)digest(name,before);
 unsigned opened=fd_count();struct bw_preload_reader_pin pin;memset(&pin,0xff,sizeof pin);struct bw_preload_bundle_result diagnostic={0};
 selecting=1;int result=bw_preload_bundle_select_for_removal(q,&pin,&diagnostic);selecting=0;
 if(result!=expected)fprintf(stderr,"selection result%d expected%d stage%s reason%s errno%d\n",result,expected,diagnostic.stage?diagnostic.stage:"",diagnostic.reason?diagnostic.reason:"",diagnostic.system_error);
 CHECK(result==expected&&!diagnostic.mutation_started&&!selection_writes);preserved(receipt,map);
 if(regular){digest(name,after);CHECK(!memcmp(before,after,32));}else if(present)CHECK(!lstat(name,&final)&&pb_same(&identity,&final));else CHECK(access(name,F_OK)<0&&errno==ENOENT);
 if(held_fault_fd>=0){CHECK(fd_count()==opened+1&&!__real_close(held_fault_fd));held_fault_fd=-1;}CHECK(fd_count()==opened);
 if(!expected){uint8_t hash[32];if(pin_kind==2){cp_sha_bytes(fixture_public07,fixture_public07_size,hash);CHECK(pin.size==fixture_public07_size);}else if(pin_kind){memcpy(hash,bw_preload_embedded_image.sha256,32);CHECK(pin.size==bw_preload_embedded_image.size);}else{cp_sha_bytes(fixture_previous,fixture_previous_size,hash);CHECK(pin.size==fixture_previous_size);}CHECK(!memcmp(hash,pin.sha256,32)&&!diagnostic.reason);}
 else{CHECK(!pin.size&&diagnostic.reason);for(unsigned i=0;i<32;i++)CHECK(!pin.sha256[i]);}
}
int main(int argc,char **argv){CHECK(argc==2);struct bw_preload_bundle_upgrade_request q;char name[2048],other[2048];
 q=fixture(argv[1],0);select_case(&q,0,0);
 q=fixture(argv[1],1);select_case(&q,0,1);
 q=fixture(argv[1],1);for(unsigned i=1;i<5;i++){char folder[2048];joined_path(folder,current_root,"fakelib2");joined_path(name,folder,pb_globals[i].name);CHECK(!unlink(name));}select_case(&q,0,1);
 q=fixture(argv[1],2);select_case(&q,0,2);
 q=fixture(argv[1],2);joined_path(name,current_root,"fakelib2/libSceAmpr.sprx");int fd07=open(name,O_RDWR);CHECK(fd07>=0);uint8_t byte07=17;CHECK(pwrite(fd07,&byte07,1,0)==1&&!__real_close(fd07));select_case(&q,-1,2);
 q=fixture(argv[1],2);uint8_t wrong07[32]={1};q.prepared_record_sha256=wrong07;select_case(&q,-1,2);
 q=fixture(argv[1],2);CHECK(!mkdir(workspace,0700));select_case(&q,-1,2);
 q=fixture(argv[1],2);fault=CLOSE_CURRENT;select_case(&q,-2,2);CHECK(fault_used==1);

 q=fixture(argv[1],0);reader_remove();select_case(&q,0,1);
 q=fixture(argv[1],1);CHECK(!mkdir(workspace,0700));select_case(&q,-1,1);
 q=fixture(argv[1],0);joined_path(name,current_root,"fakelib2/libSceAmpr.sprx");int fd=open(name,O_RDWR);CHECK(fd>=0);uint8_t b=17;CHECK(pwrite(fd,&b,1,0)==1&&!__real_close(fd));select_case(&q,-1,0);
 q=fixture(argv[1],0);joined_path(name,current_root,"fakelib2/foreign.sprx");write_file(name,"foreign",7,0755);select_case(&q,-1,0);
 q=fixture(argv[1],0);joined_path(name,current_root,"fakelib2/libSceGameUpdate.sprx");CHECK(!unlink(name));select_case(&q,-1,0);
 q=fixture(argv[1],0);joined_path(name,current_root,"fakelib2/libSceGameUpdate.sprx");CHECK(!truncate(name,1));select_case(&q,-1,0);
 q=fixture(argv[1],0);joined_path(name,current_root,"fakelib2/libSceAmpr.sprx");joined_path(other,current_root,"outside");CHECK(!rename(name,other)&&!symlink(other,name));select_case(&q,-1,0);
 q=fixture(argv[1],0);joined_path(name,current_root,"fakelib2/libSceAmpr.sprx");joined_path(other,current_root,"outside");CHECK(!link(name,other));select_case(&q,-1,0);
 q=fixture(argv[1],0);joined_path(name,current_root,"fakelib2/libSceAmpr.sprx");CHECK(!chmod(name,0644));select_case(&q,-1,0);
 q=fixture(argv[1],0);uint8_t wrong[32]={1};q.prepared_record_sha256=wrong;select_case(&q,-1,0);
 q=fixture(argv[1],0);q.prepared_run="cccccccccccccccccccccccccccccccc";select_case(&q,-1,0);
 q=fixture(argv[1],0);deny=1;select_case(&q,-1,0);
 q=fixture(argv[1],0);select_cancel_at=guard_calls+40;select_case(&q,-1,0);CHECK(guard_calls>=select_cancel_at);select_cancel_at=0;
 q=fixture(argv[1],0);fault=EXPIRE_INITIAL;select_case(&q,-1,0);CHECK(fault_used==1);
 q=fixture(argv[1],0);fault=CLOSE_CURRENT;select_case(&q,-2,0);CHECK(fault_used==1);
 q=fixture(argv[1],0);fault=CLOSE_DIR;select_case(&q,-2,0);CHECK(fault_used==1);
 q=fixture(argv[1],0);fault=CLOSE_STATE;select_case(&q,-2,0);CHECK(fault_used==1);
 for(unsigned v=0;v<4;v++){q=fixture(argv[1],0);playgo_fixture(v);select_case(&q,0,0);}
 q=fixture(argv[1],0);playgo_fixture(0);joined_path(name,current_root,"fakelib2/libScePlayGo.sprx");CHECK(!unlink(name));select_case(&q,-1,0);
 q=fixture(argv[1],0);playgo_fixture(0);joined_path(name,current_root,"fakelib2/libScePlayGo.sprx");CHECK(!truncate(name,1));select_case(&q,-1,0);
 q=fixture(argv[1],0);playgo_fixture(0);joined_path(name,current_root,"fakelib2/libScePlayGo.sprx");joined_path(other,current_root,"outside-playgo");CHECK(!rename(name,other)&&!symlink(other,name));select_case(&q,-1,0);
 printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"selectorWrites\":%u,\"actualFilesystem\":true}\n",cases,checks,selection_writes);return 0;
}
