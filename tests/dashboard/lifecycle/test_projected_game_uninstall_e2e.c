/* Actual provider/Send -> removal admission -> reader selection -> normal
 * offline uninstall. SMB PDU service/normal exclusion callbacks are modeled. */
#define main retained_namespace_fixture_main
#include "../../compressed/namespace_test.c"
#undef main
#include "native_send.c"
#include "native_game_uninstall.h"
#include "preload_bundle.h"
#include <sqlite3.h>
static unsigned registration_calls,exclusion_calls;
static void file_write(const char *path,const void *body,size_t size){int fd=open(path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0644);C(fd>=0&&write(fd,body,size)==(ssize_t)size&&!close(fd));}
static void mutate(const char *path){int fd=open(path,O_RDWR|O_NOFOLLOW);uint8_t b;C(fd>=0&&pread(fd,&b,1,0)==1);b^=1;C(pwrite(fd,&b,1,0)==1&&!close(fd));}
static int registration_absent(void *opaque,const struct bw_removed_source *removed){C(opaque&&removed);registration_calls++;return 0;}
static int excluded(void *opaque,const struct bw_removed_source *removed){C(opaque&&removed);exclusion_calls++;return 0;}
static void empty_db(const char *path){sqlite3 *db=NULL;C(sqlite3_open(path,&db)==SQLITE_OK);C(sqlite3_exec(db,"CREATE TABLE tbl_contentinfo(titleId TEXT,contentId TEXT)",NULL,NULL,NULL)==SQLITE_OK);C(sqlite3_close(db)==SQLITE_OK);}
static void fingerprint(const char *path,uint8_t out[32]){FILE *f=fopen(path,"rb");C(f);uint8_t bytes[1024];struct cp_sha sha;cp_sha_init(&sha);size_t n;while((n=fread(bytes,1,sizeof bytes,f)))cp_sha_update(&sha,bytes,n);C(!ferror(f)&&!fclose(f));cp_sha_final(&sha,out);}
#ifndef BW_PROJECTED_REMOVE_FIXTURE_ONLY
int main(int argc,char **argv){C(argc==6);int folder=!strcmp(argv[1],"folder");const char *mode=argv[5];
    struct smb2_context context={0};C(strlen(argv[2])<sizeof context.root);strcpy(context.root,argv[2]);for(unsigned i=0;i<64;i++)context.handles[i].fd=-1;
    struct bw_source_smb source={.smb=&context,.deadline=now()+600};struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");
    struct bw_provider_binding binding;C(!bw_provider_binding_init(&binding,&config,folder?"fixture":"fixture.ffpfsc","PPSA99001",folder?BW_PROVIDER_FOLDER:BW_PROVIDER_CONTAINER));
    struct bw_container_smb *image=NULL;struct image_source input={.context=&context};
    if(!folder){load_image(&input,argv[3]);image=calloc(1,sizeof *image);C(image);strcpy(image->path,"fixture.ffpfsc");image->handle=(struct smb2fh *)image;image->admitted.smb2_ino=7;image->admitted.smb2_size=input.size;image->admitted.smb2_mtime=1700000000;struct bw_container_ops ops={image_read,image_generation};C(!bw_container_open(&image->container,&ops,&input,input.size));}
    char data[1600],apps[1600],db[1600],save[1600],log[1600],game[1800],state[1800],path[2000];
    C(snprintf(data,sizeof data,"%s/data",argv[4])>0&&!mkdir(data,0755));C(snprintf(apps,sizeof apps,"%s/apps",argv[4])>0&&!mkdir(apps,0770));C(snprintf(db,sizeof db,"%s/app.db",argv[4])>0);empty_db(db);
    C(snprintf(save,sizeof save,"%s/saved-game.bin",argv[4])>0);file_write(save,"untouched save",14);uint8_t before_save[32];fingerprint(save,before_save);
    C(snprintf(log,sizeof log,"%s/game.log",argv[4])>0);file_write(log,"",0);
    struct bw_source_provider provider={0};C(!bw_source_provider_prepare_policy(&provider,&binding,&source,image,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",32u*1024u*1024u,current,&context,BW_SOURCE_PLAN_COMPANIONS_V1));
    struct bw_native_send_files send={.data_root=data,.game_log=log,.provider=&provider,.current=current,.opaque=&context};struct bw_native_prepared prepared={0};
    C(!bw_native_send_files(&send,&prepared)&&prepared.state==BW_PREPARED_VALID&&prepared.bytes_verified&&prepared.prepared_policy==3);
    C(snprintf(game,sizeof game,"%s/games/%s",data,binding.binding.game)>0);C(snprintf(state,sizeof state,"%s/native-prepared/%s",data,binding.binding.game)>0);
    bw_source_provider_destroy(&provider);C(!context.open);unsigned requests=context.requests,fences=input.fences;
    if(strcmp(mode,"absent-reader")){C(snprintf(path,sizeof path,"%s/fakelib2",game)>0);C(!mkdir(path,0755)||errno==EEXIST);C(snprintf(path,sizeof path,"%s/fakelib2/libSceAmpr.sprx",game)>0);file_write(path,bw_preload_embedded_image.bytes,bw_preload_embedded_image.size);C(!chmod(path,0755));}
    struct bw_native_launch_admission *admission=NULL;const char *error=NULL;C(!bw_native_launch_admission_open_for_removal(&admission,data,&binding,&prepared,"cccccccccccccccccccccccccccccccc",current,&context,&error)&&admission);
    const struct bw_preload_companions *companions=NULL;C(!bw_native_launch_admission_companions_for_removal(admission,&companions)&&companions);
    struct bw_preload_bundle_upgrade_request selection={.bundle={game,current,&context,now()+20},.data_root=data,.game=binding.binding.game,.prepared_run=prepared.run,.launch_run="cccccccccccccccccccccccccccccccc",.prepared_record_sha256=prepared.record_sha256};
    struct bw_preload_reader_pin pin={0};struct bw_preload_bundle_result diagnostic={0};int selected=bw_preload_bundle_select_projection_for_removal(&selection,companions,&pin,&diagnostic);
    if(selected)fprintf(stderr,"selector rc%d stage%s reason%s library%s errno%d\n",selected,diagnostic.stage?diagnostic.stage:"",diagnostic.reason?diagnostic.reason:"",diagnostic.library?diagnostic.library:"",diagnostic.system_error);
    C(!selected&&!diagnostic.mutation_started&&pin.size==bw_preload_embedded_image.size&&!memcmp(pin.sha256,bw_preload_embedded_image.sha256,32));
    C(!bw_native_launch_admission_close(&admission)&&!admission);
    if(!strcmp(mode,"receipt-tamper")){snprintf(path,sizeof path,"%s/blackwire.companions",game);mutate(path);}
    if(!strcmp(mode,"original-tamper")){snprintf(path,sizeof path,"%s/source.original.files",state);mutate(path);}
    if(!strcmp(mode,"extra-legacy")){snprintf(path,sizeof path,"%s/fakelib2/libSceAppContent.sprx",game);file_write(path,"unowned legacy name",19);C(!chmod(path,0755));}
    if(!strcmp(mode,"transient-lease")){snprintf(path,sizeof path,"%s/fakelib2/blackwire-reader.projection",game);file_write(path,"unreleased active input",23);C(!chmod(path,0600));}
    struct bw_game_uninstall_request request={.environment={data,apps,db},.identity=binding,.prepared=prepared,.ops={.opaque=&context,.current=current,.registration_absent=registration_absent,.source_excluded=excluded},.reader_size=pin.size};
    memcpy(request.reader_sha256,pin.sha256,32);strcpy(request.operation,"cccccccccccccccccccccccccccccccc");strcpy(request.confirmed_title,binding.binding.title);
    struct bw_game_uninstall_result result={0};int removed=bw_native_game_uninstall(&request,&result);int positive=!strcmp(mode,"normal")||!strcmp(mode,"absent-reader");
    if(positive){if(removed)fprintf(stderr,"uninstall failure phase%d reason%s files%u/%u\n",result.phase,result.reason?result.reason:"",result.files_removed,result.files_total);
        C(!removed&&result.phase==BW_GAME_REMOVE_DONE&&result.registration_confirmed&&result.source_absent&&result.prepared_record_absent&&result.library_removed&&!result.release_unknown&&result.files_removed==result.files_total&&result.files_total>0&&registration_calls==1&&exclusion_calls>0);
        struct stat st;C(lstat(game,&st)<0&&errno==ENOENT&&lstat(state,&st)<0&&errno==ENOENT);unsigned registered=registration_calls;C(!bw_native_game_uninstall(&request,&result)&&result.phase==BW_GAME_REMOVE_DONE&&registration_calls==registered);
    }else C(removed<0&&result.phase!=BW_GAME_REMOVE_DONE&&!result.mutation_started&&!result.files_removed&&!registration_calls);
    uint8_t after[32];fingerprint(save,after);C(!memcmp(before_save,after,32));C(context.requests==requests&&input.fences==fences&&!context.open);free(image);free(input.bytes);
    printf("{\"passed\":true,\"checks\":%u,\"actualProviderAndSend\":true,\"actualRemovalWitnessAndSelector\":true,\"actualNormalUninstall\":true,\"sourceRequestsAfterPreparation\":0,\"normalExclusionCallbacksModeled\":true,\"removed\":%s,\"consoleActions\":0}\n",checks,positive?"true":"false");return 0;
}
#endif
