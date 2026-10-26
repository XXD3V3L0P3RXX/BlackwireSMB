/* Actual old strict Send -> original step7 bundle refusal -> DEFAULT wrapper
 * -> ordinary adapter/core cleanup. Only PC/SMP fixture transport is modeled. */
#define BW_UNINSTALL_REMOTE_MAIN retained_remote_main
#include "test_game_uninstall_remote.c"
#include "preload_bundle.h"
int bw_test_default_legacy_remove(const struct bw_game_uninstall_request *,struct bw_game_uninstall_result *);
int main(int argc,char **argv){
    CHECK(argc==3);remote_case="send-only";test_scenario=remote_case;scenario=argv[2];char fixture[2048],log[2048],name[2048];
    path(test_data,argv[1],"data");path(source_root,argv[1],"source");path(fixture,argv[1],"metadata");path(log,argv[1],"game.log");path(test_apps,argv[1],"apps");path(test_db,argv[1],"app.db");path(api_scan,argv[1],"unrelated-scan");CHECK(!mkdir(api_scan,0755));
    struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");struct bw_provider_binding identity;CHECK(!bw_provider_binding_init(&identity,&config,"Fixture","PPSA99999",BW_PROVIDER_FOLDER));
    struct bw_source_provider provider={0};struct bw_source_smb source={.smb=(struct smb2_context *)(uintptr_t)1,.deadline=cp_now()+60};provider.identity=identity;provider.source=&source;provider.bundle.ready=1;strcpy(provider.bundle.source,"Fixture");strcpy(provider.bundle.game,identity.binding.game);strcpy(provider.bundle.title,"PPSA99999");
    size_t size;path(name,fixture,"map.bin");provider.bundle.map.bytes=file_read(name,&provider.bundle.map.size);path(name,fixture,"digests.bin");provider.bundle.digests=file_read(name,&provider.bundle.digest_size);path(name,fixture,"selection.txt");void *selected=file_read(name,&size);CHECK(size<sizeof provider.bundle.selection);memcpy(provider.bundle.selection,selected,size);free(selected);
    struct bw_manifest map;CHECK(!bw_manifest_open(&map,provider.bundle.map.bytes,provider.bundle.map.size));for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record row;CHECK(!bw_manifest_get(&map,i,&row));if(row.flags==BW_MANIFEST_LOCAL){provider.bundle.map.local_count++;provider.bundle.map.local_bytes+=row.size;}else{provider.bundle.map.stream_count++;provider.bundle.map.remote_bytes+=row.size;}}
    memset(provider.bundle.generation_sha256,0x3a,32);struct bw_native_send_files send={.data_root=test_data,.game_log=log,.provider=&provider,.current=current,.opaque=&provider};struct bw_native_prepared prepared={0};
    CHECK(!bw_native_send_files(&send,&prepared)&&prepared_ready(&prepared)&&!prepared.prepared_policy);cases++;
    snprintf(test_root,sizeof test_root,"%s/games/%s",test_data,identity.binding.game);snprintf(test_state,sizeof test_state,"%s/native-prepared/%s",test_data,identity.binding.game);preserve_before(argv[1]);
    struct bw_preload_bundle_request bundle={test_root,remote_current,&provider,cp_now()+20};struct bw_preload_bundle_result failure={0};
    CHECK(bw_preload_bundle_install_checked(&bundle,&failure)==-1&&!failure.mutation_started&&failure.reason&&!strcmp(failure.reason,"reader_directory_names_differ"));cases++;
    path(name,test_root,"fakelib2/README.txt");struct stat sparse;CHECK(!lstat(name,&sparse)&&sparse.st_blocks==0);
    if(!strcmp(argv[2],"unknown-extra")){path(name,test_root,"fakelib2/foreign-unmapped.sprx");file_write(name,"unowned",7,0755);}
    if(!strcmp(argv[2],"allocated-stream")){path(name,test_root,"fakelib2/README.txt");int fd=open(name,O_WRONLY);CHECK(fd>=0&&write(fd,"x",1)==1&&!close(fd));}
    struct bw_game_uninstall_request request={.environment={test_data,test_apps,test_db},.identity=identity,.prepared=prepared,.ops={.opaque=&provider,.current=remote_current}};
    strcpy(request.operation,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");strcpy(request.confirmed_title,"PPSA99999");unsigned lookups=remote_lookups,reads=remote_reads,fds=descriptor_count();
    struct bw_game_uninstall_result out={0};int result=bw_test_default_legacy_remove(&request,&out);cases++;
    CHECK(descriptor_count()==fds&&remote_lookups==lookups&&remote_reads==reads&&!api_remove_calls&&!api_uninstall_calls);
    if(!strcmp(argv[2],"normal")){if(result)fprintf(stderr,"default sourcef2 removal phase%u reason%s\n",out.phase,out.reason?out.reason:"");CHECK(!result&&out.phase==BW_GAME_REMOVE_DONE&&out.library_removed&&out.source_absent&&out.prepared_record_absent);CHECK(lstat(test_root,&sparse)<0&&errno==ENOENT&&lstat(test_state,&sparse)<0&&errno==ENOENT);}
    else{CHECK(result&&!out.mutation_started&&!out.library_removed&&!lstat(test_root,&sparse));if(!strcmp(argv[2],"unknown-extra")){path(name,test_root,"fakelib2/foreign-unmapped.sprx");CHECK(!lstat(name,&sparse)&&sparse.st_size==7);}}
    preserve_after(argv[1]);free(provider.bundle.map.bytes);free((void *)provider.bundle.digests);printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualOldSendAndBundleRefusal\":true,\"actualDefaultWrapperAndNormalCore\":true,\"pcLookupsDuringRemoval\":%u,\"pcReadsDuringRemoval\":%u,\"sourceAndSavePreserved\":true,\"consoleActions\":0}\n",cases,checks,remote_lookups-lookups,remote_reads-reads);return 0;
}
