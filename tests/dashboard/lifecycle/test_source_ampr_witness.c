/* Actual bundle code; synthetic independently pinned admission-owner fixture.
 * The production witness producer is native_launch_admission.c, not this fixture. */
#define _DEFAULT_SOURCE 1
#define BW_PRELOAD_BUNDLE_HOST_TEST 1
#include "preload_bundle.c"
#include <sys/types.h>
static unsigned checks,cases;
static int write_fault,close_fault,mutate_on_write;
static char mutate_source[1200];
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"source Ampr case%u line%d: %s\n",cases,__LINE__,#x);exit(2);}}while(0)
int __real_close(int);
ssize_t __real_write(int,const void *,size_t);
static int fd_name(int fd,const char *needle){char link[64],path[1400];snprintf(link,sizeof link,"/proc/self/fd/%d",fd);
    ssize_t n=readlink(link,path,sizeof path-1);if(n<0)return 0;path[n]=0;return strstr(path,needle)!=NULL;}
int __wrap_close(int fd){int fail=close_fault&&fd_name(fd,"/fakelib/libSceAmpr.sprx");int result=__real_close(fd);
    if(fail){close_fault=0;errno=EIO;return -1;}return result;}
ssize_t __wrap_write(int fd,const void *bytes,size_t size){
    if(fd_name(fd,"/fakelib2/libSceAmpr.sprx")){
        if(write_fault){write_fault=0;errno=EIO;return -1;}
        if(mutate_on_write){mutate_on_write=0;int source=open(mutate_source,O_RDWR|O_NOFOLLOW);uint8_t b;
            C(source>=0&&pread(source,&b,1,0)==1);b^=1;C(pwrite(source,&b,1,0)==1&&!close(source));}
    }return __real_write(fd,bytes,size);
}
struct owner {unsigned expected_epoch,current_epoch;};
struct fixture {char root[1024],source[1200];struct owner owner;struct bw_preload_source_ampr witness;struct bw_preload_bundle_request request;struct stat source_stat;};
static int owner_current(void *opaque){struct owner *o=opaque;return !o||o->expected_epoch!=o->current_epoch?-1:0;}
static int current(void *opaque){return owner_current(opaque);}
static double monotonic(void){struct timespec t;C(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec/1e9;}
static void file_write(const char *path,const void *bytes,size_t size){int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644);C(fd>=0);
    size_t at=0;while(at<size){ssize_t n=write(fd,(const uint8_t *)bytes+at,size-at);C(n>0);at+=(size_t)n;}C(!close(fd));}
static void path(char out[1200],const char *root,const char *leaf){int n=snprintf(out,1200,"%s/%s",root,leaf);C(n>0&&n<1200);}
static void init(struct fixture *f,const char *base,size_t size,int present){
    memset(f,0,sizeof *f);int n=snprintf(f->root,sizeof f->root,"%s/case-%02u",base,++cases);C(n>0&&(size_t)n<sizeof f->root&&!mkdir(f->root,0755));
    f->owner=(struct owner){7,7};strcpy(f->witness.root,f->root);f->witness.owner=&f->owner;f->witness.current_owner=owner_current;
    f->witness.present=(unsigned)present;f->witness.size=present?size:0;f->witness.purpose=BW_SOURCE_AMPR_EXECUTION;
    f->request=(struct bw_preload_bundle_request){f->root,current,&f->owner,monotonic()+30};
    path(f->source,f->root,"fakelib/libSceAmpr.sprx");
    if(present){
        uint8_t *expected=malloc(size);C(expected);for(size_t i=0;i<size;i++)expected[i]=(uint8_t)(i*37u+11u);
        /* Pin expected independently owned source-generation bytes BEFORE any
         * prepared local file exists. Never learn the expected pin by hashing
         * the local file the bundle is about to check. */
        cp_sha_bytes(expected,size,f->witness.sha256);
        char directory[1200];path(directory,f->root,"fakelib");C(!mkdir(directory,0755));file_write(f->source,expected,size);free(expected);C(!lstat(f->source,&f->source_stat));
    }
}
static int install(struct fixture *f,struct bw_preload_bundle_result *d){return bw_preload_bundle_install_source_checked(&f->request,&f->witness,d);}
static void absent_private(struct fixture *f){char pathbuf[1200];struct stat st;path(pathbuf,f->root,"fakelib2");C(lstat(pathbuf,&st)<0&&errno==ENOENT);}
static void source_unchanged(struct fixture *f){struct stat after;C(!lstat(f->source,&after)&&pb_same(&f->source_stat,&after));
    int fd=open(f->source,O_RDONLY|O_NOFOLLOW);C(fd>=0);struct cp_sha sha;cp_sha_init(&sha);uint8_t bytes[4096],hash[32];
    for(;;){ssize_t n=read(fd,bytes,sizeof bytes);C(n>=0);if(!n)break;cp_sha_update(&sha,bytes,(size_t)n);}cp_sha_final(&sha,hash);C(!close(fd)&&!memcmp(hash,f->witness.sha256,32));}
static void embedded_only(struct fixture *f){char file[1200];struct stat st;path(file,f->root,"fakelib2/libSceAmpr.sprx");
    int fd=open(file,O_RDONLY|O_NOFOLLOW);C(fd>=0&&!fstat(fd,&st)&&st.st_size==(off_t)bw_preload_embedded_image.size);
    uint8_t *bytes=malloc(bw_preload_embedded_image.size);C(bytes&&read(fd,bytes,bw_preload_embedded_image.size)==(ssize_t)bw_preload_embedded_image.size);
    C(!memcmp(bytes,bw_preload_embedded_image.bytes,bw_preload_embedded_image.size)&&!close(fd));free(bytes);
    path(file,f->root,"fakelib2/libSceAmpr.sprx.orig");C(lstat(file,&st)<0&&errno==ENOENT);
}
static void official_playgo(const char *base,const char *input){
    struct stat st;int fd=open(input,O_RDONLY|O_NOFOLLOW);C(fd>=0&&!fstat(fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0&&st.st_size<20000);
    size_t size=(size_t)st.st_size;uint8_t *bytes=malloc(size);C(bytes&&read(fd,bytes,size)==(ssize_t)size&&!close(fd));
    for(unsigned tampered=0;tampered<2;tampered++){
        struct fixture f;struct bw_preload_bundle_result d={0};init(&f,base,0,0);
        char folder[1200],file[1200];path(folder,f.root,"fakelib");C(!mkdir(folder,0755));path(file,f.root,"fakelib/libScePlayGo.sprx");
        if(tampered)bytes[0]^=1;file_write(file,bytes,size);
        int result=install(&f,&d);
        if(tampered){C(result==-1&&!d.mutation_started&&!strcmp(d.reason,"reader_library_hash_differs"));absent_private(&f);}
        else {C(!result&&d.mutation_started);embedded_only(&f);C(!bw_preload_bundle_verify_source_checked(&f.request,&f.witness,&d));
            path(file,f.root,"fakelib2/libScePlayGo.sprx");fd=open(file,O_RDONLY|O_NOFOLLOW);uint8_t *copy=malloc(size);
            C(fd>=0&&copy&&read(fd,copy,size)==(ssize_t)size&&!memcmp(copy,bytes,size)&&!close(fd));free(copy);}
    }free(bytes);
}
int main(int argc,char **argv){C(argc==2||argc==4);struct fixture f;struct bw_preload_bundle_result d={0};
    init(&f,argv[1],111,1);C(bw_preload_bundle_install_checked(&f.request,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"unexpected_game_reader_file"));absent_private(&f);source_unchanged(&f);
    C(bw_preload_bundle_install_source_checked(&f.request,NULL,&d)==-1&&!strcmp(d.reason,"source_ampr_witness_required"));
    const size_t variants[]={17,2048,254326};for(unsigned i=0;i<3;i++){
        init(&f,argv[1],variants[i],1);C(!install(&f,&d)&&d.mutation_started&&!d.release_unknown);source_unchanged(&f);embedded_only(&f);
        C(!bw_preload_bundle_verify_source_checked(&f.request,&f.witness,&d)&&!d.mutation_started);
        C(!install(&f,&d)&&!d.mutation_started);source_unchanged(&f);embedded_only(&f);
    }
    init(&f,argv[1],0,0);C(!install(&f,&d));embedded_only(&f);
    init(&f,argv[1],123,1);f.witness.sha256[0]^=1;C(install(&f,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"reader_library_hash_differs"));absent_private(&f);
    init(&f,argv[1],123,1);f.witness.present=0;f.witness.size=0;C(install(&f,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"source_ampr_presence_differs_from_admitted_manifest"));absent_private(&f);
    init(&f,argv[1],0,0);f.witness.present=1;f.witness.size=123;C(install(&f,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"source_ampr_missing_from_prepared_source"));absent_private(&f);
    init(&f,argv[1],123,1);strcat(f.witness.root,"-another-title");C(install(&f,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"source_ampr_witness_stale_or_mismatched"));absent_private(&f);
    init(&f,argv[1],123,1);f.owner.current_epoch++;C(install(&f,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"source_ampr_witness_stale_or_mismatched"));absent_private(&f);
    init(&f,argv[1],123,1);f.witness.purpose=BW_SOURCE_AMPR_REMOVAL;
    C(install(&f,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"source_ampr_witness_stale_or_mismatched"));absent_private(&f);
    init(&f,argv[1],123,1);char other[1200];path(other,f.root,"original");C(!rename(f.source,other)&&!symlink(other,f.source));C(install(&f,&d)==-1&&!d.mutation_started);absent_private(&f);
    init(&f,argv[1],123,1);path(other,f.root,"linked-source");C(!link(f.source,other));C(install(&f,&d)==-1&&!d.mutation_started&&!strcmp(d.reason,"reader_library_link_count_differs"));absent_private(&f);
    init(&f,argv[1],123,1);write_fault=1;C(install(&f,&d)==-2&&d.mutation_started&&!d.release_unknown);source_unchanged(&f);
    init(&f,argv[1],123,1);close_fault=1;C(install(&f,&d)==-2&&d.release_unknown&&!d.mutation_started);absent_private(&f);
    init(&f,argv[1],123,1);strcpy(mutate_source,f.source);mutate_on_write=1;C(install(&f,&d)==-2&&d.mutation_started);
    init(&f,argv[1],123,1);C(!install(&f,&d));path(other,f.root,"fakelib2/libSceAmpr.sprx");int fd=open(other,O_RDWR|O_NOFOLLOW);uint8_t byte;
    C(fd>=0&&pread(fd,&byte,1,0)==1);byte^=1;C(pwrite(fd,&byte,1,0)==1&&!close(fd));C(bw_preload_bundle_verify_source_checked(&f.request,&f.witness,&d)==-1&&!d.mutation_started);source_unchanged(&f);
    if(argc==4){official_playgo(argv[1],argv[2]);official_playgo(argv[1],argv[3]);}
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"syntheticAdmissionOwner\":true,\"productionAdmissionProducerIntegrated\":false,\"officialPlayGo04Variants\":%u,\"nativeExecution\":false}\n",cases,checks,argc==4?2u:0u);return 0;
}
