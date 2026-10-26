/* Actual bundle+codec filesystem unit. The synthetic owner models an already
 * admitted receipt; actual native Send/admission integration is separate. */
#define main companion_codec_other_main
#include "test_source_companions.c"
#undef main
#include "preload_bundle.h"
#include "preload_companions_internal.h"
#include "../streamreader/startup_mode.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
static int write_fault,close_fault;
ssize_t __real_write(int,const void *,size_t);int __real_close(int);
ssize_t __wrap_write(int fd,const void *bytes,size_t size){if(write_fault){write_fault=0;errno=EIO;return -1;}return __real_write(fd,bytes,size);}
int __wrap_close(int fd){int result=__real_close(fd);if(close_fault){close_fault=0;errno=EIO;return -1;}return result;}
static double seconds(void){struct timespec t;C(!clock_gettime(CLOCK_MONOTONIC,&t));return(double)t.tv_sec+(double)t.tv_nsec/1e9;}
static void path(char out[2048],const char *a,const char *b){int n=snprintf(out,2048,"%s/%s",a,b);C(n>0&&n<2048);}
static void directory(const char *name){char copy[2048];C(strlen(name)<sizeof copy);strcpy(copy,name);for(char *s=copy+1;*s;s++)if(*s=='/'){*s=0;C(!mkdir(copy,0755)||errno==EEXIST);*s='/';}C(!mkdir(copy,0755)||errno==EEXIST);C(!chmod(name,0755));}
static void put(const char *name,const void *bytes,size_t size,mode_t mode){char parent[2048];strcpy(parent,name);char *slash=strrchr(parent,'/');C(slash);*slash=0;directory(parent);int fd=open(name,O_WRONLY|O_CREAT|O_EXCL,mode);C(fd>=0&&fchmod(fd,mode)==0);size_t at=0;while(at<size){ssize_t n=write(fd,(const uint8_t *)bytes+at,size-at);C(n>0);at+=(size_t)n;}C(!close(fd));}
struct owner {int valid;char root[2048],state[2048];struct stat root_id,state_id;};
static int current(void *opaque){struct owner *o=opaque;struct stat a,b;return !o->valid||lstat(o->root,&a)||lstat(o->state,&b)||a.st_dev!=o->root_id.st_dev||a.st_ino!=o->root_id.st_ino||b.st_dev!=o->state_id.st_dev||b.st_ino!=o->state_id.st_ino?-1:0;}
static void exercise(const char *base,unsigned id,uint32_t presence,const struct file *files,size_t count,unsigned fault){
    cases++;char data[2048],game[2048],state[2048],name[2048],sub[64];snprintf(sub,sizeof sub,"case-%u",id);path(data,base,sub);directory(data);
    struct fixture f;init_real(&f,files,count);char games[2048],states[2048];path(games,data,"games");path(states,data,"native-prepared");path(game,games,f.provider.binding.game);path(state,states,f.provider.binding.game);directory(game);directory(state);
    struct owner owner={.valid=1};strcpy(owner.root,game);strcpy(owner.state,state);C(!lstat(game,&owner.root_id)&&!lstat(state,&owner.state_id));
    f.binding.roots=(struct bw_companion_roots){owner.root_id.st_dev,owner.root_id.st_ino,owner.state_id.st_dev,owner.state_id.st_ino};
    struct bw_companion_projection *projection=NULL;const char *error=NULL;C(!bw_companion_projection_create(&projection,&f.binding,&f.source,presence,&error));struct bw_companion_tables tables;C(!bw_companion_projection_tables(projection,&tables));
    struct bw_manifest manifest;C(!bw_manifest_open(&manifest,tables.map,tables.map_size));
    for(unsigned i=0;i<manifest.count;i++){struct bw_manifest_record row;C(!bw_manifest_get(&manifest,i,&row));path(name,game,row.path);
        if(!strcmp(row.path,BW_COMPANION_RECEIPT_PATH)){put(name,tables.receipt,tables.receipt_size,0644);continue;}
        uint32_t source=i;struct bw_manifest_record original;int found=0;
        for(unsigned j=0;j<bw_companion_projection_count(projection);j++){struct bw_companion_member member;C(!bw_companion_projection_member(projection,j,&member));if(member.destination_row==i){source=member.source_row;found=1;break;}}
        struct bw_manifest old;C(!bw_manifest_open(&old,f.source.map,f.source.map_size));
        if(!found){for(unsigned j=0;j<old.count;j++){C(!bw_manifest_get(&old,j,&original));if(!strcmp(original.path,row.path)){source=j;found=1;break;}}}C(found&&!bw_manifest_get(&old,source,&original));
        unsigned byte=0x31;for(size_t j=0;j<count;j++)if(!strcmp(files[j].path,original.path))byte=files[j].byte;
        C(row.size<=300);uint8_t bytes[300];memset(bytes,(int)byte,sizeof bytes);put(name,bytes,(size_t)row.size,(mode_t)bw_startup_mode(row.path));
    }
    uint8_t record[384]={0};memcpy(record,"BWNAT001",8);p32(record+8,1);p32(record+12,384);memcpy(record+16,f.run,32);memcpy(record+48,f.provider.binding.sha256,32);memcpy(record+80,f.generation,32);cp_sha_bytes(tables.map,tables.map_size,record+112);cp_sha_bytes(tables.digests,tables.digest_size,record+144);p64(record+304,owner.root_id.st_dev);p64(record+312,owner.root_id.st_ino);p64(record+320,owner.state_id.st_dev);p64(record+328,owner.state_id.st_ino);cp_sha_bytes(record,352,record+352);path(name,state,"prepared.bin");put(name,record,sizeof record,0600);
    struct bw_companion_prepared_seal parent_seal;seal(&tables,&parent_seal);struct bw_companion_receipt *receipt=NULL;C(!bw_companion_receipt_open(&receipt,&f.binding,&f.source,&tables,&parent_seal,&error));
    struct bw_preload_companions witness={.receipt=receipt,.purpose=BW_SOURCE_AMPR_EXECUTION,.current_owner=current,.owner=&owner};C(strlen(game)<sizeof witness.root);strcpy(witness.root,game);
    struct bw_preload_bundle_request q={game,current,&owner,seconds()+60};struct bw_preload_bundle_result result={0};
    if(fault==1){write_fault=1;C(bw_preload_bundle_install_projection_checked(&q,&witness,&result)==-2&&result.mutation_started&&!result.release_unknown);goto done;}
    C(!bw_preload_bundle_install_projection_checked(&q,&witness,&result));C(result.mutation_started);C(!bw_preload_bundle_verify_projection_checked(&q,&witness,&result));C(!result.mutation_started);
    path(name,game,"fakelib2/libSceAmpr.sprx");struct stat reader;C(!lstat(name,&reader));C(!bw_preload_bundle_install_projection_checked(&q,&witness,&result)&&!result.mutation_started);struct stat again;C(!lstat(name,&again)&&reader.st_ino==again.st_ino);
    char run[33];memset(run,'b',32);run[32]=0;uint8_t record_sha[32];cp_sha_bytes(record,sizeof record,record_sha);
    struct bw_preload_bundle_upgrade_request upgrade={.bundle=q,.data_root=data,.game=f.provider.binding.game,.prepared_run=f.run,.launch_run=run,.prepared_record_sha256=record_sha};struct bw_preload_bundle_control split={current,&owner};
    C(!bw_preload_bundle_upgrade_projection_controlled(&upgrade,&split,&witness,&result)&&!result.mutation_started);C(!bw_preload_bundle_upgrade_workspace(data,f.provider.binding.game));
    struct bw_preload_reader_pin pin;C(bw_preload_bundle_select_projection_for_removal(&upgrade,&witness,&pin,&result)==-1&&!result.mutation_started);
    witness.purpose=BW_SOURCE_AMPR_REMOVAL;C(bw_preload_bundle_verify_projection_checked(&q,&witness,&result)==-1);C(!bw_preload_bundle_select_projection_for_removal(&upgrade,&witness,&pin,&result)&&pin.size==bw_preload_embedded_image.size&&!memcmp(pin.sha256,bw_preload_embedded_image.sha256,32));witness.purpose=BW_SOURCE_AMPR_EXECUTION;
    char extra[2048];path(extra,game,"fakelib2/extra.txt");put(extra,"x",1,0644);C(bw_preload_bundle_verify_projection_checked(&q,&witness,&result)==-1&&!result.mutation_started&&!result.release_unknown);C(!unlink(extra));
    path(extra,game,"fakelib2/unbound-dir");directory(extra);C(bw_preload_bundle_verify_projection_checked(&q,&witness,&result)==-1&&!result.mutation_started);C(!rmdir(extra));
    for(unsigned i=0;i<bw_companion_receipt_count(receipt);i++){struct bw_companion_member member;C(!bw_companion_receipt_member(receipt,i,&member));if(member.role==BW_COMPANION_READER_SLOT)continue;
        path(extra,game,member.destination_path);int fd=open(extra,O_RDWR|O_NOFOLLOW);C(fd>=0);uint8_t byte;C(pread(fd,&byte,1,0)==1);uint8_t wrong=byte^1;C(pwrite(fd,&wrong,1,0)==1);C(bw_preload_bundle_verify_projection_checked(&q,&witness,&result)==-1&&!result.mutation_started);C(pwrite(fd,&byte,1,0)==1&&!close(fd));
        mode_t mode=(mode_t)bw_startup_mode(member.destination_path);C(!chmod(extra,mode==0644?0755:0644));C(bw_preload_bundle_verify_projection_checked(&q,&witness,&result)==-1);C(!chmod(extra,mode));
        char linked[2048];path(linked,data,"second-link");C(!link(extra,linked));C(bw_preload_bundle_verify_projection_checked(&q,&witness,&result)==-1);C(!unlink(linked));break;
    }
    owner.valid=0;C(bw_preload_bundle_verify_projection_checked(&q,&witness,&result)==-1&&!result.mutation_started);owner.valid=1;
    witness.root[0]^=1;C(bw_preload_bundle_verify_projection_checked(&q,&witness,&result)==-1);witness.root[0]^=1;
    C(!bw_preload_bundle_verify_projection_checked(&q,&witness,&result));close_fault=1;C(bw_preload_bundle_verify_projection_checked(&q,&witness,&result)==-2&&result.release_unknown&&!result.mutation_started);
done:bw_companion_receipt_clear(&receipt);bw_companion_projection_clear(&projection);destroy(&f);
}
int main(int argc,char **argv){C(argc==2);const struct file f2[]={{"fakelib2/libSceAmpr.sprx",19,1,3},{"fakelib2/foreign.sprx",17,1,4},{"fakelib2/README.txt",7,1,5},{"fakelib2/options/config.ini",11,1,6},{"fakelib2/helper.so",13,1,8}};
    const struct file f1[]={{"fakelib/libSceAmpr.sprx",19,1,3},{"fakelib/foreign.sprx",17,1,4},{"fakelib/README.txt",7,1,5},{"fakelib/options/config.ini",11,1,6},{"fakelib/helper.so",13,1,8}};
    exercise(argv[1],1,2,f2,5,0);exercise(argv[1],2,1,f1,5,0);exercise(argv[1],3,3,f1,5,0);exercise(argv[1],4,2,NULL,0,1);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualCodecAndBundle\":true,\"syntheticAdmissionOwner\":true,\"moduleExecution\":false,\"consoleActions\":0}\n",cases,checks);return 0;}
