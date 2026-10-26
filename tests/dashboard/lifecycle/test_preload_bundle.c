/* Actual bundle qualifier on private ordinary host fixtures; no console calls. */
#define _DEFAULT_SOURCE 1
#define BW_PRELOAD_BUNDLE_HOST_TEST 1
#include "preload_bundle.c"
#include <sys/types.h>

static unsigned checks,cases,guard_calls;
static int deny_guard,close_fault,write_fault,sync_fault;
static const char *close_name;
static char mutate_source[4096],mutate_trigger[4096];
static unsigned mutation_used,mutation_mode,mutation_at;
static char mutate_from[4096];
static void change_byte(const char *);
static void copy_file(const char *,const char *,mode_t);
static char fixture_base[4096];
#define CHECK(x) do {checks++;if(!(x)){fprintf(stderr,"failed line %d: %s\n",__LINE__,#x);exit(2);}}while(0)
int __real_close(int);
ssize_t __real_write(int,const void *,size_t);
int __real_fsync(int);
static int fd_name(int fd,const char *needle){
    char path[64],name[4096];snprintf(path,sizeof path,"/proc/self/fd/%d",fd);
    ssize_t n=readlink(path,name,sizeof name-1);if(n<0)return 0;name[n]=0;
    return strstr(name,needle)!=NULL;
}
int __wrap_close(int fd){
    int fail=close_fault&&fd_name(fd,close_name);
    int result=__real_close(fd);
    if(fail){close_fault=0;errno=EIO;return -1;}return result;
}
ssize_t __wrap_write(int fd,const void *data,size_t size){
    if(write_fault&&fd_name(fd,"/fakelib2/")){
        write_fault=0;errno=EIO;return -1;
    }
    return __real_write(fd,data,size);
}
int __wrap_fsync(int fd){
    if(sync_fault&&fd_name(fd,"/fakelib2/")){
        sync_fault=0;errno=EIO;return -1;
    }
    return __real_fsync(fd);
}
static int current(void *opaque){CHECK(opaque==&guard_calls);guard_calls++;
    if(mutate_trigger[0]&&!mutation_used&&guard_calls>=mutation_at){struct stat st;if(!lstat(mutate_trigger,&st)){mutation_used=1;
        if(mutation_mode==1){char previous[4112];int n=snprintf(previous,sizeof previous,"%s.original",mutate_source);CHECK(n>0&&(size_t)n<sizeof previous);CHECK(!rename(mutate_source,previous));copy_file(previous,mutate_source,0644);}
        else if(mutation_mode==2)copy_file(mutate_from,mutate_source,0644);
        else change_byte(mutate_source);}}
    return deny_guard?-1:0;}
static double monotonic(void){struct timespec t;CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));return (double)t.tv_sec+(double)t.tv_nsec/1e9;}
static void copy_file(const char *a,const char *b,mode_t mode){
    int in=open(a,O_RDONLY|O_NOFOLLOW),out=open(b,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,mode);
    CHECK(in>=0&&out>=0);char bytes[32768];
    for(;;){ssize_t n=read(in,bytes,sizeof bytes);CHECK(n>=0);if(!n)break;CHECK(write(out,bytes,(size_t)n)==n);}
    CHECK(!fchmod(out,mode)&&!close(out)&&!close(in));
}
static void change_byte(const char *path){
    int fd=open(path,O_RDWR|O_NOFOLLOW);unsigned char b;CHECK(fd>=0&&pread(fd,&b,1,0)==1);b^=0x80;
    CHECK(pwrite(fd,&b,1,0)==1&&!close(fd));
}
static void path(char out[4096],const char *root,const char *leaf){int n=snprintf(out,4096,"%s/%s",root,leaf);CHECK(n>0&&n<4096);}
static struct bw_preload_bundle_request request(char root[4096],int prepared){
    int n=snprintf(root,4096,"%s/case-%02u",fixture_base,cases++);CHECK(n>0&&n<4096);CHECK(!mkdir(root,0755));
    char game_lib[4096],source[4096],target[4096];path(game_lib,root,"fakelib");CHECK(!mkdir(game_lib,0755));
    for(unsigned i=1;i<5;i++){path(source,BW_PRELOAD_GLOBAL_ROOT,pb_globals[i].name);path(target,game_lib,pb_globals[i].name);copy_file(source,target,0644);}
    struct bw_preload_bundle_request q={root,current,&guard_calls,monotonic()+120.0};
    if(prepared){struct bw_preload_bundle_result d={0};CHECK(!bw_preload_bundle_install_checked(&q,&d));CHECK(!d.reason&&!strcmp(d.stage,"complete")&&d.mutation_started);}
    return q;
}
static void failure_result(const struct bw_preload_bundle_request *q,int expected,const char *stage,const char *reason,const char *library,int error,unsigned expected_mutation){
    struct bw_preload_bundle_result d={0};CHECK(bw_preload_bundle_install_checked(q,&d)==expected);
    CHECK(d.stage&&!strcmp(d.stage,stage));CHECK(d.reason&&!strcmp(d.reason,reason));
    CHECK((!d.library&&!library)||(d.library&&library&&!strcmp(d.library,library)));
    CHECK(d.system_error==error);CHECK(d.mutation_started==expected_mutation);
}
static void failure(const struct bw_preload_bundle_request *q,int expected,const char *stage,const char *reason,const char *library,int error){
    failure_result(q,expected,stage,reason,library,error,(unsigned)(expected==-2));
}
int main(int argc,char **argv){
    CHECK(argc==2&&strlen(argv[1])<sizeof fixture_base);strcpy(fixture_base,argv[1]);
    char root[4096],file[4096],other[4096],global[4096];
    struct bw_preload_bundle_request q=request(root,0);struct bw_preload_bundle_result d={0};
    CHECK(!bw_preload_bundle_install_checked(&q,&d)&&!d.reason&&d.mutation_started);
    CHECK(!strcmp(d.stage,"complete"));
    path(file,root,"fakelib2/libSceAmpr.sprx");struct stat before,after;CHECK(!stat(file,&before));
    CHECK(!bw_preload_bundle_install_checked(&q,&d)&&!d.reason&&!d.mutation_started);
    CHECK(!stat(file,&after)&&pb_same(&before,&after));
    CHECK(!bw_preload_bundle_install(&q)); /* unchanged legacy entry */

    q=request(root,1);path(file,root,"fakelib2/libSceAmpr.sprx");change_byte(file);
    close_fault=1;close_name="/fakelib2/libSceAmpr.sprx";
    failure_result(&q,-2,"private-library-hash","reader_library_hash_differs","libSceAmpr.sprx",0,0);
    CHECK(!close_fault); /* late close failure never replaces the first reason */
    q=request(root,1);path(file,root,"fakelib2/libSceAmpr.sprx");CHECK(!chmod(file,0644));
    failure(&q,-1,"private-library-metadata","private_reader_library_mode_differs","libSceAmpr.sprx",0);
    q=request(root,1);path(file,root,"fakelib2");CHECK(!chmod(file,0777));
    failure(&q,-1,"private-directory","private_reader_directory_mode_differs",NULL,0);
    q=request(root,1);path(file,root,"fakelib2/foreign.sprx");int fd=open(file,O_WRONLY|O_CREAT|O_EXCL,0644);CHECK(fd>=0&&!close(fd));
    failure(&q,-1,"private-library-names","reader_directory_names_differ",NULL,0);
    q=request(root,1);path(file,root,"fakelib2/libSceGameUpdate.sprx");CHECK(!unlink(file));
    failure(&q,-1,"private-library-names","reader_directory_is_incomplete",NULL,0);
    q=request(root,1);path(file,root,"fakelib2/libSceGameUpdate.sprx");path(other,root,"same-library");CHECK(!link(file,other));
    failure(&q,-1,"private-library-metadata","reader_library_link_count_differs","libSceGameUpdate.sprx",0);
    q=request(root,1);path(file,root,"fakelib2/libSceGameUpdate.sprx");path(other,root,"outside-library");CHECK(!rename(file,other)&&!symlink(other,file));
    failure(&q,-1,"private-library-open","reader_library_open_failed","libSceGameUpdate.sprx",ELOOP);
    q=request(root,1);path(file,root,"fakelib2/libSceGameUpdate.sprx");CHECK(!truncate(file,1));
    failure(&q,-1,"private-library-metadata","reader_library_size_differs","libSceGameUpdate.sprx",0);

    q=request(root,0);deny_guard=1;
    failure(&q,-1,"request","bundle_authority_changed",NULL,0);deny_guard=0;
    q=request(root,0);q.deadline=monotonic()-1;
    failure(&q,-1,"request","bundle_deadline_expired",NULL,0);
    CHECK(bw_preload_bundle_install_checked(NULL,&d)==-1&&!strcmp(d.reason,"invalid_reader_bundle_request"));
    CHECK(!d.mutation_started&&!d.system_error);

    q=request(root,0);write_fault=1;
    failure(&q,-2,"private-library-write","private_reader_library_write_failed","libSceAmpr.sprx",EIO);
    CHECK(!write_fault);path(file,root,"fakelib2/libSceAmpr.sprx");CHECK(!stat(file,&before)&&!before.st_size);
    failure(&q,-1,"private-library-names","reader_directory_is_incomplete",NULL,0);
    CHECK(!stat(file,&after)&&pb_same(&before,&after)); /* no replay/repair */
    q=request(root,0);sync_fault=1;
    failure(&q,-2,"private-library-sync","private_reader_library_sync_failed","libSceAmpr.sprx",EIO);CHECK(!sync_fault);
    q=request(root,0);close_fault=1;close_name="/fakelib/libSceAppContent.sprx";
    failure_result(&q,-2,"game-library-hash","reader_library_close_failed","libSceAppContent.sprx",EIO,0);CHECK(!close_fault);

    q=request(root,0);path(global,root,"fakelib/libSceGameUpdate.sprx");change_byte(global);
    failure(&q,-1,"game-library-hash","reader_library_hash_differs","libSceGameUpdate.sprx",0);
    q=request(root,0);path(global,root,"fakelib/.DS_Store");fd=open(global,O_CREAT|O_EXCL|O_WRONLY,0600);CHECK(fd>=0&&write(fd,"metadata",8)==8&&!close(fd));
    CHECK(!lstat(global,&before));CHECK(!bw_preload_bundle_install_checked(&q,&d)&&d.source_entries_ignored==1&&!d.reason);
    CHECK(!lstat(global,&after)&&pb_same(&before,&after));path(file,root,"fakelib2/.DS_Store");CHECK(lstat(file,&after)<0&&errno==ENOENT);
    q=request(root,0);path(global,root,"fakelib/foreign.sprx");fd=open(global,O_CREAT|O_EXCL|O_WRONLY,0600);CHECK(fd>=0&&!close(fd));
    CHECK(bw_preload_bundle_install_checked(&q,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"unqualified_game_companion_module"));
    CHECK(!strcmp(d.entry_class,"unqualified-source-module")&&!strcmp(d.entry_name,"foreign.sprx")&&!d.entry_name_truncated);
    q=request(root,0);path(global,root,"fakelib/unknown.PRX");CHECK(!symlink("/unreachable-source-module",global));
    CHECK(bw_preload_bundle_install_checked(&q,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"unqualified_game_companion_module"));
    CHECK(!strcmp(d.entry_name,"unknown.PRX")&&!lstat(global,&after)&&S_ISLNK(after.st_mode));
    q=request(root,0);path(global,root,"fakelib/source-metadata");CHECK(!symlink("/unreachable-source-metadata",global));CHECK(!lstat(global,&before));
    CHECK(!bw_preload_bundle_install_checked(&q,&d)&&d.source_entries_ignored==1&&!d.reason);
    CHECK(!lstat(global,&after)&&pb_same(&before,&after));path(file,root,"fakelib2/source-metadata");CHECK(lstat(file,&after)<0&&errno==ENOENT);
    q=request(root,1);path(global,root,"fakelib2/bad\n\"\\name");fd=open(global,O_CREAT|O_EXCL|O_WRONLY,0600);CHECK(fd>=0&&!close(fd));
    CHECK(bw_preload_bundle_install_checked(&q,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"reader_directory_names_differ"));
    CHECK(!strcmp(d.entry_class,"unselected-private-entry")&&!strcmp(d.entry_name,"bad\\x0a\\x22\\x5cname")&&!d.entry_name_truncated);
    char reused[4096];memset(reused,'x',sizeof reused);CHECK(!strcmp(d.entry_name,"bad\\x0a\\x22\\x5cname")); /* copied after directory close */
    q=request(root,1);path(global,root,"fakelib/legacy-extra.PRX");fd=open(global,O_CREAT|O_EXCL|O_WRONLY,0600);CHECK(fd>=0&&!close(fd));
    CHECK(bw_preload_bundle_verify_checked(&q,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"unqualified_game_companion_module")&&!strcmp(d.entry_name,"legacy-extra.PRX"));
    q=request(root,1);path(global,root,"fakelib/libSceGameUpdate.sprx");change_byte(global);
    CHECK(bw_preload_bundle_verify_checked(&q,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"reader_library_hash_differs"));

    char long_name[256];memset(long_name,1,255);long_name[255]=0;q=request(root,1);path(global,root,"fakelib2");path(file,global,long_name);
    fd=open(file,O_CREAT|O_EXCL|O_WRONLY,0600);CHECK(fd>=0&&!close(fd));
    CHECK(bw_preload_bundle_install_checked(&q,&d)==-1&&d.entry_name_truncated&&strlen(d.entry_name)==256);
    CHECK(!strncmp(d.entry_name,"\\x01",4)&&!strchr(d.entry_name,'\n'));
#ifdef BW_PRELOAD_PLAYGO_FIXTURES
    /* Synthetic fixed pin bytes exercise actual copy, verification and refusal
     * logic. These bytes are data-only, never executable runtime modules. */
    for(unsigned variant=0;variant<4;variant++){
        char leaf[64];snprintf(leaf,sizeof leaf,"playgo-%u.sprx",variant);q=request(root,0);
        path(global,BW_PRELOAD_GLOBAL_ROOT,leaf);path(file,root,"fakelib/libScePlayGo.sprx");copy_file(global,file,0644);CHECK(!lstat(file,&before));
        CHECK(!bw_preload_bundle_install_checked(&q,&d)&&!d.reason&&d.mutation_started);CHECK(!lstat(file,&after)&&pb_same(&before,&after));
        path(other,root,"fakelib2/libScePlayGo.sprx");CHECK(!lstat(other,&after)&&S_ISREG(after.st_mode)&&(after.st_mode&07777)==0755);
        CHECK(!bw_preload_bundle_verify_checked(&q,&d)&&!d.reason&&!d.mutation_started);
        change_byte(other);CHECK(bw_preload_bundle_verify_checked(&q,&d)==-1&&!strcmp(d.reason,"reader_library_hash_differs"));
    }
    q=request(root,0);path(global,BW_PRELOAD_GLOBAL_ROOT,"playgo-0.sprx");path(file,root,"fakelib/libScePlayGo.sprx");copy_file(global,file,0644);change_byte(file);
    failure(&q,-1,"game-library-hash","reader_library_hash_differs","libScePlayGo.sprx",0);
    q=request(root,0);path(file,root,"fakelib/libScePlayGo.sprx");fd=open(file,O_CREAT|O_EXCL|O_WRONLY,0644);CHECK(fd>=0&&write(fd,"unknown",7)==7&&!close(fd));
    CHECK(bw_preload_bundle_install_checked(&q,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"unqualified_game_playgo_variant"));CHECK(!strcmp(d.entry_name,"libScePlayGo.sprx"));
    q=request(root,0);path(file,root,"fakelib/libScePlayGo.sprx");CHECK(!symlink(global,file));failure(&q,-1,"game-library-open","reader_library_open_failed","libScePlayGo.sprx",ELOOP);
    q=request(root,0);path(file,root,"fakelib/libScePlayGo.sprx");copy_file(global,file,0644);path(other,root,"outside-playgo");CHECK(!link(file,other));failure(&q,-1,"game-library-metadata","reader_library_link_count_differs","libScePlayGo.sprx",0);
    q=request(root,0);path(file,root,"fakelib/libScePlayGo.sprx");copy_file(global,file,0644);path(mutate_source,root,"fakelib/libScePlayGo.sprx");path(mutate_trigger,root,"fakelib2/libScePlayGo.sprx");mutation_used=0;
    CHECK(bw_preload_bundle_install_checked(&q,&d)==-2&&d.mutation_started&&mutation_used&&d.reason);mutate_trigger[0]=0;
    q=request(root,0);path(file,root,"fakelib/libScePlayGo.sprx");copy_file(global,file,0644);path(mutate_source,root,"fakelib/libScePlayGo.sprx");path(mutate_trigger,root,"fakelib2/libScePlayGo.sprx");mutation_used=0;mutation_mode=1;
    CHECK(bw_preload_bundle_install_checked(&q,&d)==-2&&d.mutation_started&&mutation_used&&d.reason);mutate_trigger[0]=0;mutation_mode=0;
    q=request(root,1);path(mutate_source,root,"fakelib/libScePlayGo.sprx");path(mutate_trigger,root,"fakelib2/libSceAmpr.sprx");path(mutate_from,BW_PRELOAD_GLOBAL_ROOT,"playgo-0.sprx");mutation_used=0;mutation_mode=2;mutation_at=guard_calls+60;
    CHECK(bw_preload_bundle_verify_checked(&q,&d)==-1&&!d.mutation_started&&mutation_used&&d.reason);mutate_trigger[0]=0;mutation_mode=mutation_at=0;
    q=request(root,0);path(file,root,"fakelib/libScePlayGo.sprx");copy_file(global,file,0644);CHECK(!bw_preload_bundle_install_checked(&q,&d));path(file,root,"fakelib2/libScePlayGo.sprx");path(other,root,"outside-playgo");CHECK(!rename(file,other)&&!symlink(other,file));
    CHECK(bw_preload_bundle_verify_checked(&q,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"reader_library_open_failed"));
    q=request(root,0);path(file,root,"fakelib/libScePlayGo.sprx");copy_file(global,file,0644);CHECK(!bw_preload_bundle_install_checked(&q,&d));path(file,root,"fakelib2/libScePlayGo.sprx");CHECK(!unlink(file));
    CHECK(bw_preload_bundle_verify_checked(&q,&d)==-1&&!strcmp(d.reason,"reader_directory_is_incomplete"));
#endif
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"guardCalls\":%u,\"consoleActions\":0}\n",cases,checks,guard_calls);
    return 0;
}
