#define _DEFAULT_SOURCE 1
#define BW_PRELOAD_BUNDLE_HOST_TEST 1
#include "under-test.c"
#include <stdarg.h>
#include <assert.h>
extern const uint8_t fixture_previous[];
extern const size_t fixture_previous_size;
extern const uint8_t fixture_public07[];
extern const size_t fixture_public07_size;
static unsigned checks,cases,guard_calls,fault_used,observed_workspace,global_closes,game_closes;
static int fault,deny,held_fault_fd=-1;
static char current_root[2048],current_state[2048],workspace[2048];
static ino_t old_inode;
static time_t clock_shift;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"case%u fault%d line%d: %s errno%d\n",cases,fault,__LINE__,#x,errno);exit(2);}}while(0)
enum {NONE,WRITE_INTENT,WRITE_BACKUP,WRITE_NEW,SYNC_NEW,SYNC_PRIVATE,SYNC_STATE,RENAME_BEFORE,RENAME_AFTER,
 CLOSE_OLD,CLOSE_BACKUP,CLOSE_INTENT,CLOSE_STATE,CLOSE_WORK,CLOSE_RETAINED,CANCEL_NEW,CANCEL_PUBLISHED,CANCEL_CLEANED,
 CLOSE_GLOBAL,CLOSE_DIR,CLOSE_CURRENT,CLOSE_INSTALL_GLOBAL,EXPIRE_INITIAL,EXPIRE_NEW};
int __real_close(int);ssize_t __real_write(int,const void *,size_t);int __real_fsync(int);int __real_renameat(int,const char *,int,const char *);
int __real_closedir(DIR *);
int __real_clock_gettime(clockid_t,struct timespec *);
int __wrap_clock_gettime(clockid_t id,struct timespec *t){int result=__real_clock_gettime(id,t);if(!result&&id==CLOCK_MONOTONIC)t->tv_sec+=clock_shift;return result;}
static void fd_path(int fd,char out[4096]){char p[64];snprintf(p,sizeof p,"/proc/self/fd/%d",fd);ssize_t n=readlink(p,out,4095);if(n<0)out[0]=0;else out[n]=0;}
int __wrap_close(int fd){char name[4096];fd_path(fd,name);int global=strstr(name,BW_PRELOAD_GLOBAL_ROOT "/libSceAmpr.sprx")!=NULL;if(global)global_closes++;int game=strstr(name,"/fakelib/libSceAppContent.sprx")!=NULL;if(game)game_closes++;int hit=!fault_used&&
 ((fault==CLOSE_OLD&&strstr(name,"/fakelib2/libSceAmpr.sprx (deleted)"))||
 (fault==CLOSE_BACKUP&&strstr(name,"/.blackwire-reader-upgrade/old-reader"))||
 (fault==CLOSE_INTENT&&strstr(name,"/.blackwire-reader-upgrade/intent"))||
 (fault==CLOSE_STATE&&!strcmp(name,current_state))||(fault==CLOSE_WORK&&!strncmp(name,workspace,strlen(workspace))&&
    (!name[strlen(workspace)]||!strcmp(name+strlen(workspace)," (deleted)")))||
 (fault==CLOSE_RETAINED&&strstr(name,"/.blackwire-reader-upgrade/old-reader"))||
 (fault==CLOSE_GLOBAL&&strstr(name,BW_PRELOAD_GLOBAL_ROOT "/libSceAmpr.sprx"))||
 (fault==CLOSE_CURRENT&&strstr(name,"/fakelib2/libSceAmpr.sprx"))||
 (fault==CLOSE_INSTALL_GLOBAL&&game&&game_closes==1));
 if(hit){fault_used++;if(fault==CLOSE_RETAINED){held_fault_fd=fd;errno=EIO;return -1;}int r=__real_close(fd);(void)r;errno=EIO;return -1;}return __real_close(fd);}
int __wrap_closedir(DIR *d){char name[4096];fd_path(dirfd(d),name);int hit=!fault_used&&fault==CLOSE_DIR&&(strstr(name,BW_PRELOAD_GLOBAL_ROOT)||strstr(name,"/fakelib2"));int r=__real_closedir(d);if(hit){fault_used++;errno=EIO;return -1;}return r;}
ssize_t __wrap_write(int fd,const void *p,size_t n){char name[4096];fd_path(fd,name);
 int hit=!fault_used&&((fault==WRITE_INTENT&&strstr(name,"/.blackwire-reader-upgrade/intent"))||
 (fault==WRITE_BACKUP&&strstr(name,"/.blackwire-reader-upgrade/old-reader"))||(fault==WRITE_NEW&&strstr(name,"/.blackwire-reader-upgrade/new-reader")));
 if(hit){fault_used++;errno=EIO;return -1;}return __real_write(fd,p,n);}
int __wrap_fsync(int fd){char name[4096];fd_path(fd,name);int hit=!fault_used&&((fault==SYNC_NEW&&strstr(name,"/.blackwire-reader-upgrade/new-reader"))||
 (fault==SYNC_PRIVATE&&strstr(name,"/fakelib2")&&name[strlen(name)-1]=='2')||(fault==SYNC_STATE&&!strcmp(name,current_state)));
 if(hit){fault_used++;errno=EIO;return -1;}return __real_fsync(fd);}
int __wrap_renameat(int a,const char *b,int c,const char *d){if(!fault_used&&fault==RENAME_BEFORE){fault_used++;errno=EIO;return -1;}
 int r=__real_renameat(a,b,c,d);if(!fault_used&&fault==RENAME_AFTER&&!r){fault_used++;errno=EIO;return -1;}return r;}
static void joined_path(char out[2048],const char *a,const char *b){int n=snprintf(out,2048,"%s/%s",a,b);CHECK(n>0&&n<2048);}
static void write_file(const char *name,const void *bytes,size_t n,mode_t mode){int fd=open(name,O_CREAT|O_EXCL|O_WRONLY|O_NOFOLLOW,mode);CHECK(fd>=0&&!fchmod(fd,mode));CHECK(__real_write(fd,bytes,n)==(ssize_t)n);CHECK(!__real_close(fd));}
static void copy_file(const char *a,const char *b){int fd=open(a,O_RDONLY|O_NOFOLLOW);CHECK(fd>=0);struct stat st;CHECK(!fstat(fd,&st)&&st.st_size>=0);uint8_t *p=malloc((size_t)st.st_size);CHECK(p&&pread(fd,p,(size_t)st.st_size,0)==st.st_size&&!__real_close(fd));write_file(b,p,(size_t)st.st_size,0755);free(p);}
static void digest(const char *name,uint8_t out[32]){int fd=open(name,O_RDONLY|O_NOFOLLOW);CHECK(fd>=0);uint8_t b[32768];struct cp_sha sha;cp_sha_init(&sha);for(;;){ssize_t n=read(fd,b,sizeof b);CHECK(n>=0);if(!n)break;cp_sha_update(&sha,b,(size_t)n);}cp_sha_final(&sha,out);CHECK(!__real_close(fd));}
static unsigned fd_count(void){DIR *d=opendir("/proc/self/fd");CHECK(d);unsigned n=0;struct dirent *e;while((e=readdir(d)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,".."))n++;CHECK(!closedir(d));return n;}
static double monotonic(void){struct timespec t;CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec/1e9;}
static int current(void *p){CHECK(p==&guard_calls);guard_calls++;struct stat st;if(!lstat(workspace,&st))observed_workspace=1;
 char file[2048];joined_path(file,current_root,"fakelib2/libSceAmpr.sprx");int published=!lstat(file,&st)&&old_inode&&st.st_ino!=old_inode;
 if(deny)return -1;
 if(fault==CANCEL_NEW){char path[2048];joined_path(path,workspace,"new-reader");if(!lstat(path,&st)){fault_used=1;return -1;}}
 if(fault==CANCEL_PUBLISHED&&published){fault_used=1;return -1;}
 if(fault==CANCEL_CLEANED&&observed_workspace&&lstat(workspace,&st)<0&&errno==ENOENT){fault_used=1;return -1;}
 if(fault==EXPIRE_INITIAL&&!fault_used){fault_used=1;clock_shift=1000;}
 if(fault==EXPIRE_NEW&&!fault_used){char path[2048];joined_path(path,workspace,"new-reader");if(!lstat(path,&st)&&st.st_size==(off_t)bw_preload_embedded_image.size){fault_used=1;clock_shift=1000;}}
 return 0;}
static struct bw_preload_bundle_upgrade_request fixture(const char *base,unsigned current_image){
 cases++;fault=deny=0;fault_used=observed_workspace=global_closes=0;clock_shift=0;old_inode=0;char area[2048],data[2048],name[2048],game[75],state_scope[2048];
 char leaf[32];snprintf(leaf,sizeof leaf,"case-%02u",cases);joined_path(area,base,leaf);CHECK(!mkdir(area,0700));joined_path(data,area,"data");CHECK(!mkdir(data,0700));
 joined_path(name,data,"games");CHECK(!mkdir(name,0700));joined_path(state_scope,data,"native-prepared");CHECK(!mkdir(state_scope,0700));
 strcpy(game,"PPSA99999_");for(unsigned i=0;i<32;i++)snprintf(game+10+2*i,3,"%02x",i+1);
 joined_path(current_root,name,game);CHECK(!mkdir(current_root,0700));joined_path(current_state,state_scope,game);CHECK(!mkdir(current_state,0700));joined_path(workspace,current_state,PU_WORKSPACE);
 struct stat a,b;CHECK(!lstat(current_root,&a)&&!lstat(current_state,&b));uint8_t record[384]={0};memcpy(record,"BWNAT001",8);record[8]=1;record[12]=128;record[13]=1;
 memset(record+16,'a',32);for(unsigned i=0;i<32;i++)record[48+i]=(uint8_t)(i+1);
 pu_put(record+304,a.st_dev);pu_put(record+312,a.st_ino);pu_put(record+320,b.st_dev);pu_put(record+328,b.st_ino);cp_sha_bytes(record,352,record+352);
 static uint8_t record_sha[32];cp_sha_bytes(record,sizeof record,record_sha);joined_path(name,current_state,"prepared.bin");write_file(name,record,sizeof record,0600);
 joined_path(name,current_root,"blackwire.files");write_file(name,"manifest-sentinel",17,0600);joined_path(name,current_root,"eboot.bin");write_file(name,"startup-sentinel",16,0755);
 char folder[2048];joined_path(folder,current_root,"fakelib2");CHECK(!mkdir(folder,0755));
 for(unsigned i=0;i<5;i++){joined_path(name,folder,pb_globals[i].name);if(i==0)write_file(name,current_image==1?bw_preload_embedded_image.bytes:current_image==2?fixture_public07:fixture_previous,current_image==1?bw_preload_embedded_image.size:current_image==2?fixture_public07_size:fixture_previous_size,0755);
  else{char source[2048];joined_path(source,BW_PRELOAD_GLOBAL_ROOT,pb_globals[i].name);copy_file(source,name);}}
 joined_path(name,folder,"libSceAmpr.sprx");CHECK(!lstat(name,&a));old_inode=a.st_ino;
 static char captured_root[2048],captured_data[2048],captured_game[75];strcpy(captured_root,current_root);strcpy(captured_data,data);strcpy(captured_game,game);
 return (struct bw_preload_bundle_upgrade_request){.bundle={captured_root,current,&guard_calls,monotonic()+120},.data_root=captured_data,.game=captured_game,
  .prepared_run="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",.launch_run="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",.prepared_record_sha256=record_sha};
}
static void preserved(const uint8_t receipt[32],const uint8_t map[32]){char p[2048];uint8_t h[32];joined_path(p,current_state,"prepared.bin");digest(p,h);CHECK(!memcmp(h,receipt,32));joined_path(p,current_root,"blackwire.files");digest(p,h);CHECK(!memcmp(h,map,32));}
static int execute(struct bw_preload_bundle_upgrade_request *q,int expected,unsigned expect_current){char file[2048];uint8_t receipt[32],map[32],h[32];joined_path(file,current_state,"prepared.bin");digest(file,receipt);joined_path(file,current_root,"blackwire.files");digest(file,map);
 unsigned open_before=fd_count();struct bw_preload_bundle_result r={0};int result=bw_preload_bundle_upgrade_checked(q,&r);
 if(result!=expected)fprintf(stderr,"result%d expected%d stage%s library%s reason%s error%d\n",result,expected,r.stage?r.stage:"",r.library?r.library:"",r.reason?r.reason:"",r.system_error);
 CHECK(result==expected);
 preserved(receipt,map);joined_path(file,current_root,"fakelib2/libSceAmpr.sprx");digest(file,h);
 if(expect_current==1)CHECK(!memcmp(h,bw_preload_embedded_image.sha256,32));else{uint8_t old[32];cp_sha_bytes(expect_current==2?fixture_public07:fixture_previous,expect_current==2?fixture_public07_size:fixture_previous_size,old);CHECK(!memcmp(h,old,32));}
 if(held_fault_fd>=0){CHECK(fd_count()==open_before+1);CHECK(!__real_close(held_fault_fd));held_fault_fd=-1;}CHECK(fd_count()==open_before);
 if(expected)CHECK(r.reason||expected==-2);else {CHECK(!r.reason&&!strcmp(r.stage,"complete"));CHECK(access(workspace,F_OK)<0&&errno==ENOENT);CHECK(!bw_preload_bundle_upgrade_workspace(q->data_root,q->game));}return result;}
static void playgo_fixture(unsigned variant){
 char folder[2048],source[2048],target[2048],leaf[64];joined_path(folder,current_root,"fakelib");CHECK(!mkdir(folder,0755));
 for(unsigned i=1;i<5;i++){joined_path(source,BW_PRELOAD_GLOBAL_ROOT,pb_globals[i].name);joined_path(target,folder,pb_globals[i].name);copy_file(source,target);}
 snprintf(leaf,sizeof leaf,"playgo-%u.sprx",variant);joined_path(source,BW_PRELOAD_GLOBAL_ROOT,leaf);joined_path(target,folder,"libScePlayGo.sprx");copy_file(source,target);
 joined_path(target,current_root,"fakelib2/libScePlayGo.sprx");copy_file(source,target);
}
int main(int argc,char **argv){CHECK(argc==2);char file[2048];struct bw_preload_bundle_result r;
 /* The development reader is admitted only at its exact independently
  * observed digest and size; neither near-match may enter owned migration. */
 uint8_t dev3_hash[32];CHECK(!pb_hex(pu_compressed_dev3.hex,dev3_hash));
 CHECK(pu_previous_match(pu_compressed_dev3.size,dev3_hash));
 CHECK(!pu_previous_match(pu_compressed_dev3.size+1,dev3_hash));
 dev3_hash[0]^=1;CHECK(!pu_previous_match(pu_compressed_dev3.size,dev3_hash));
 uint8_t dev5_hash[32];CHECK(!pb_hex("135148e2de73b5b54d6f18dbfb95c1f2cd2d7d7e3e6dac60b756e844e6cbc684",dev5_hash));
 CHECK(pu_previous_match(829328,dev5_hash));
 CHECK(!pu_previous_match(829329,dev5_hash));
 dev5_hash[0]^=1;CHECK(!pu_previous_match(829328,dev5_hash));
 uint8_t dev6_hash[32];CHECK(!pb_hex("c9d069b520c367c971e52432a053d7105a7019cb19ee97185120bffb6190e025",dev6_hash));
 CHECK(pu_previous_match(831168,dev6_hash));
 CHECK(!pu_previous_match(831169,dev6_hash));
 dev6_hash[0]^=1;CHECK(!pu_previous_match(831168,dev6_hash));
 uint8_t beta2_hash[32];CHECK(!pb_hex("39cc8ff48a8b53886be0f7c5f1d43bc504fb989c00a9adc050dcade72865c441",beta2_hash));
 CHECK(pu_previous_match(843696,beta2_hash));CHECK(!pu_previous_match(843695,beta2_hash));CHECK(!pu_previous_match(843697,beta2_hash));
 beta2_hash[0]^=1;CHECK(!pu_previous_match(843696,beta2_hash));
 struct bw_preload_bundle_upgrade_request q=fixture(argv[1],0);CHECK(!bw_preload_bundle_upgrade_workspace(q.data_root,q.game));execute(&q,0,1);CHECK(access(workspace,F_OK)<0&&errno==ENOENT);CHECK(!bw_preload_bundle_upgrade_workspace(q.data_root,q.game));CHECK(!bw_preload_bundle_verify_checked(&q.bundle,&r));
 q=fixture(argv[1],1);struct stat before,after;joined_path(file,current_root,"fakelib2/libSceAmpr.sprx");CHECK(!lstat(file,&before));execute(&q,0,1);CHECK(!lstat(file,&after)&&pb_same(&before,&after));
 /* Reader-only owned installations migrate without any global pack. */
 q=fixture(argv[1],0);for(unsigned i=1;i<5;i++){char folder[2048];joined_path(folder,current_root,"fakelib2");joined_path(file,folder,pb_globals[i].name);CHECK(!unlink(file));}execute(&q,0,1);CHECK(!bw_preload_bundle_verify_checked(&q.bundle,&r));
 for(unsigned state=0;state<3;state++){q=fixture(argv[1],state==2);if(state==0)CHECK(!mkdir(workspace,0700));else if(state==1)write_file(workspace,"foreign",7,0600);else CHECK(!symlink("/unrelated",workspace));CHECK(bw_preload_bundle_upgrade_workspace(q.data_root,q.game)==1);execute(&q,-1,state==2);}
 q=fixture(argv[1],0);q.prepared_run="cccccccccccccccccccccccccccccccc";execute(&q,-1,0);
 q=fixture(argv[1],0);q.launch_run="00000000000000000000000000000000";execute(&q,-1,0);
 q=fixture(argv[1],0);uint8_t wrong[32]={1};q.prepared_record_sha256=wrong;execute(&q,-1,0);
 q=fixture(argv[1],0);q.bundle.deadline=monotonic()-1;execute(&q,-1,0);
 q=fixture(argv[1],0);deny=1;execute(&q,-1,0);
 for(int f=WRITE_INTENT;f<=CANCEL_CLEANED;f++){
  q=fixture(argv[1],0);fault=f;int published=f==SYNC_PRIVATE||f==RENAME_AFTER||(f>=CLOSE_OLD&&f<=CLOSE_RETAINED)||f==CANCEL_PUBLISHED||f==CANCEL_CLEANED;
  execute(&q,-2,(unsigned)published);CHECK(fault_used==1);
  if(f!=CLOSE_STATE&&f!=CLOSE_WORK&&f!=CANCEL_CLEANED){fault=0;deny=0;q.launch_run="cccccccccccccccccccccccccccccccc";CHECK(bw_preload_bundle_upgrade_checked(&q,&r)==-1);}
 }
 q=fixture(argv[1],0);fault=CLOSE_GLOBAL;unsigned closed_globals=global_closes;execute(&q,0,1);CHECK(!fault_used&&global_closes==closed_globals);
 for(int f=CLOSE_DIR;f<=CLOSE_CURRENT;f++){
  q=fixture(argv[1],f==CLOSE_CURRENT);fault=f;execute(&q,-2,f==CLOSE_CURRENT);CHECK(fault_used==1&&access(workspace,F_OK)<0&&errno==ENOENT);
 }
 /* Both admitted predecessors use their own hash/length in durable intent and backup. */
 q=fixture(argv[1],2);execute(&q,0,1);CHECK(access(workspace,F_OK)<0&&errno==ENOENT);
 q=fixture(argv[1],2);fault=WRITE_NEW;execute(&q,-2,2);CHECK(fault_used==1);
 joined_path(file,workspace,"old-reader");uint8_t backup_hash[32],public07_hash[32];digest(file,backup_hash);cp_sha_bytes(fixture_public07,fixture_public07_size,public07_hash);CHECK(!memcmp(backup_hash,public07_hash,32));
 struct stat backup_stat;CHECK(!lstat(file,&backup_stat)&&backup_stat.st_size==(off_t)fixture_public07_size);
 joined_path(file,workspace,"intent");uint8_t intent_body[1024];int intent_fd=open(file,O_RDONLY);CHECK(intent_fd>=0&&read(intent_fd,intent_body,sizeof intent_body)==sizeof intent_body&&!__real_close(intent_fd));CHECK(!memcmp(intent_body+224,public07_hash,32));
 q=fixture(argv[1],2);q.prepared_run="cccccccccccccccccccccccccccccccc";execute(&q,-1,2);
 q=fixture(argv[1],2);deny=1;execute(&q,-1,2);
 q=fixture(argv[1],2);CHECK(bw_preload_bundle_verify_checked(&q.bundle,&r)==-1&&access(workspace,F_OK)<0);
 q=fixture(argv[1],2);joined_path(file,current_root,"fakelib2/libSceAmpr.sprx");int bad07=open(file,O_RDWR);CHECK(bad07>=0);uint8_t bad_byte=23;CHECK(pwrite(bad07,&bad_byte,1,0)==1&&!__real_close(bad07));uint8_t tampered_before[32],tampered_after[32];digest(file,tampered_before);CHECK(bw_preload_bundle_upgrade_checked(&q,&r)==-1);digest(file,tampered_after);CHECK(!memcmp(tampered_before,tampered_after,32)&&access(workspace,F_OK)<0);
 /* A same-size unknown reader is never adopted. */
 q=fixture(argv[1],0);joined_path(file,current_root,"fakelib2/libSceAmpr.sprx");int fd=open(file,O_RDWR);CHECK(fd>=0);uint8_t byte=17;CHECK(pwrite(fd,&byte,1,0)==1&&!__real_close(fd));uint8_t hash[32],after_hash[32];digest(file,hash);CHECK(bw_preload_bundle_upgrade_checked(&q,&r)==-1);digest(file,after_hash);CHECK(!memcmp(hash,after_hash,32)&&access(workspace,F_OK)<0);
 q=fixture(argv[1],0);joined_path(file,current_root,"fakelib2/foreign.sprx");write_file(file,"foreign",7,0755);CHECK(bw_preload_bundle_upgrade_checked(&q,&r)==-1&&access(workspace,F_OK)<0);
 q=fixture(argv[1],0);joined_path(file,current_root,"fakelib2/libSceAmpr.sprx");CHECK(!chmod(file,0644));CHECK(bw_preload_bundle_upgrade_checked(&q,&r)==-1&&access(workspace,F_OK)<0);
 q=fixture(argv[1],0);joined_path(file,current_root,"fakelib2/libSceAmpr.sprx");char linked[2048];joined_path(linked,current_root,"foreign-link");CHECK(!link(file,linked));CHECK(bw_preload_bundle_upgrade_checked(&q,&r)==-1&&access(workspace,F_OK)<0);
 q=fixture(argv[1],0);joined_path(file,current_root,"fakelib2/libSceAmpr.sprx");joined_path(linked,current_root,"foreign-reader");CHECK(!rename(file,linked)&&!symlink(linked,file));CHECK(bw_preload_bundle_upgrade_checked(&q,&r)==-1&&access(workspace,F_OK)<0);
 q=fixture(argv[1],0);joined_path(file,current_root,"fakelib2/libSceGameUpdate.sprx");CHECK(!truncate(file,1));CHECK(bw_preload_bundle_upgrade_checked(&q,&r)==-1&&access(workspace,F_OK)<0);
 q=fixture(argv[1],0);CHECK(bw_preload_bundle_verify_checked(&q.bundle,&r)==-1&&access(workspace,F_OK)<0); /* no migration in second check */
 q=fixture(argv[1],0);for(unsigned i=0;i<5;i++){char folder[2048];joined_path(folder,current_root,"fakelib2");joined_path(file,folder,pb_globals[i].name);CHECK(!unlink(file));}joined_path(file,current_root,"fakelib2");CHECK(!rmdir(file));
 CHECK(bw_preload_bundle_verify_checked(&q.bundle,&r)==-1&&access(file,F_OK)<0);
 char game_folder[2048];joined_path(game_folder,current_root,"fakelib");CHECK(!mkdir(game_folder,0755));
 for(unsigned i=1;i<5;i++){char source[2048],dest[2048];joined_path(source,BW_PRELOAD_GLOBAL_ROOT,pb_globals[i].name);joined_path(dest,game_folder,pb_globals[i].name);copy_file(source,dest);}
 global_closes=game_closes=0;fault=CLOSE_INSTALL_GLOBAL;
 CHECK(bw_preload_bundle_upgrade_checked(&q,&r)==-2&&fault_used==1&&!r.mutation_started&&access(file,F_OK)<0&&access(workspace,F_OK)<0);
 q=fixture(argv[1],0);fault=EXPIRE_INITIAL;execute(&q,-1,0);CHECK(fault_used==1&&access(workspace,F_OK)<0);
 q=fixture(argv[1],0);fault=EXPIRE_NEW;execute(&q,-2,0);CHECK(fault_used==1&&access(workspace,F_OK)==0);
 for(unsigned v=0;v<4;v++){q=fixture(argv[1],0);playgo_fixture(v);execute(&q,0,1);}
 q=fixture(argv[1],0);playgo_fixture(0);joined_path(file,current_root,"fakelib2/libScePlayGo.sprx");CHECK(!unlink(file));execute(&q,-1,0);
 q=fixture(argv[1],0);playgo_fixture(0);joined_path(file,current_root,"fakelib2/libScePlayGo.sprx");int playgo_fd=open(file,O_RDWR|O_NOFOLLOW);CHECK(playgo_fd>=0);unsigned char playgo_byte;CHECK(pread(playgo_fd,&playgo_byte,1,0)==1);playgo_byte^=1;CHECK(pwrite(playgo_fd,&playgo_byte,1,0)==1&&!__real_close(playgo_fd));execute(&q,-1,0);
 printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"sameWorker\":true,\"actualFilesystem\":true}\n",cases,checks);return 0;}
