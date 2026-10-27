/* Real producer and durable prepared-target reload after source discovery.
 * Source transport/image bytes are synthetic; no VALID receipt is stubbed. */
#define BW_NATIVE_SEND_HOST_TEST 1
#define BW_NATIVE_FS_HOST_TEST 1
#define BW_NATIVE_PRELOAD 1
#define main private_retained_namespace_main
#include "../../compressed/namespace_test.c"
#undef main
#include "native_send.c"
#include "native_launch.h"
#include "native_prepared_target.h"

static unsigned absence_calls;
static void old_policy_absent(const char *base,const char *scope,const struct bw_provider_binding *identity){char path[2200];struct stat st;C(identity->kind==BW_PROVIDER_FOLDER&&snprintf(path,sizeof path,"%s/%s",base,scope)>0&&lstat(path,&st)<0&&errno==ENOENT);absence_calls++;}
int bw_native_derived_send_inspect(const char *a,const struct bw_provider_binding *b,struct bw_native_derived_snapshot *c){old_policy_absent(a,"native-derived-prepared",b);memset(c,0,sizeof *c);c->prepared.base.state=BW_PREPARED_ABSENT;return BW_SEND_FAILED;}
void bw_native_derived_snapshot_destroy(struct bw_native_derived_snapshot *a){memset(a,0,sizeof *a);}

int bw_native_batch_inspect_exact(const char *a,const struct bw_provider_binding *b,const char *c,const uint8_t d[32],struct bw_native_batch_snapshot *e){(void)a;(void)b;(void)c;(void)d;(void)e;C(0);return -1;}
int bw_native_batch_send_inspect(const char *a,const struct bw_provider_binding *b,struct bw_native_batch_snapshot *c){old_policy_absent(a,"native-batch-prepared",b);memset(c,0,sizeof *c);c->prepared.base.state=BW_PREPARED_ABSENT;return BW_SEND_FAILED;}
void bw_native_batch_snapshot_destroy(struct bw_native_batch_snapshot *a){memset(a,0,sizeof *a);}
const struct bw_batch_facts *bw_batch_provenance_view(const struct bw_batch_provenance *p){(void)p;C(0);return NULL;}
int smb2_stat(struct smb2_context *s,const char *p,struct smb2_stat_64 *o){(void)s;(void)p;(void)o;C(0);return -1;}
struct smb2fh *smb2_open(struct smb2_context *s,const char *p,int f){(void)s;(void)p;(void)f;C(0);return NULL;}
int smb2_fstat(struct smb2_context *s,struct smb2fh *h,struct smb2_stat_64 *o){(void)s;(void)h;(void)o;C(0);return -1;}
int smb2_close(struct smb2_context *s,struct smb2fh *h){(void)s;(void)h;C(0);return -1;}


static unsigned fd_count(void){DIR *d=opendir("/proc/self/fd");C(d);unsigned n=0;for(struct dirent *e;(e=readdir(d));)if(e->d_name[0]!='.')n++;C(!closedir(d));return n;}
int main(int argc,char **argv){
    C(argc==7);unsigned policy=!strncmp(argv[5],"legacy",6)?BW_SOURCE_PLAN_LEGACY:BW_SOURCE_PLAN_COMPANIONS_V1;unsigned expected_policy=policy==BW_SOURCE_PLAN_LEGACY?0:3;unsigned expected_upgrade=!strcmp(argv[5],"legacy-f2")||!strcmp(argv[5],"legacy-f1-extra");const char *fault=argv[6];int folder=!strcmp(argv[1],"folder");struct smb2_context context={0};C(strlen(argv[2])<sizeof context.root);strcpy(context.root,argv[2]);for(unsigned i=0;i<64;i++)context.handles[i].fd=-1;
    struct bw_source_smb source={.smb=&context,.deadline=now()+600};struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");
    struct bw_provider_binding binding;C(!bw_provider_binding_init(&binding,&config,folder?"fixture":"fixture.ffpfsc","PPSA99001",folder?BW_PROVIDER_FOLDER:BW_PROVIDER_CONTAINER));
    struct bw_container_smb *image=NULL;struct image_source input={.context=&context};
    if(!folder){load_image(&input,argv[3]);image=calloc(1,sizeof *image);C(image);strcpy(image->path,"fixture.ffpfsc");image->handle=(struct smb2fh *)image;image->admitted.smb2_ino=7;image->admitted.smb2_size=input.size;image->admitted.smb2_mtime=1700000000;struct bw_container_ops ops={image_read,image_generation};C(!bw_container_open(&image->container,&ops,&input,input.size));}
    char log[2200];C(snprintf(log,sizeof log,"%s/game.log",argv[4])>0);int fd=open(log,O_CREAT|O_EXCL|O_WRONLY,0600);C(fd>=0&&!close(fd));unsigned descriptors=fd_count();
    struct bw_source_provider provider={0};C(!bw_source_provider_prepare_policy(&provider,&binding,&source,image,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",32u*1024u*1024u,current,&context,policy));
    struct bw_native_send_files send={.data_root=argv[4],.game_log=log,.provider=&provider,.current=current,.opaque=&context};struct bw_native_prepared fresh={0};C(!bw_native_send_files(&send,&fresh)&&fresh.state==BW_PREPARED_VALID&&fresh.bytes_verified&&fresh.prepared_policy==expected_policy);
    bw_source_provider_destroy(&provider);C(!context.open);struct bw_native_prepared raw_before={0};C(!bw_native_send_inspect(argv[4],&binding,&raw_before)&&raw_before.state==BW_PREPARED_VALID&&raw_before.prepared_policy==expected_policy&&!raw_before.bytes_verified&&!memcmp(raw_before.record_sha256,fresh.record_sha256,32));
    /* A discovery refresh reconstructs source authority without changing the
     * console receipt. Run the real provider again with a distinct run token. */
    source=(struct bw_source_smb){.smb=&context,.deadline=now()+600};
    C(!bw_source_provider_prepare_policy(&provider,&binding,&source,image,"cccccccccccccccccccccccccccccccc",32u*1024u*1024u,current,&context,policy));C(provider.bundle.ready);bw_source_provider_destroy(&provider);C(!context.open);
    char target[2400],state[2200],game[2200];C(snprintf(state,sizeof state,"%s/native-prepared/%s",argv[4],binding.binding.game)>0);C(snprintf(game,sizeof game,"%s/games/%s",argv[4],binding.binding.game)>0);
    int positive=!strcmp(fault,"none");
    if(!positive){
        const char *leaf=NULL,*directory=game;
        if(!strcmp(fault,"receipt")||!strcmp(fault,"receipt-version")||!strcmp(fault,"record-link")){directory=state;leaf="prepared.bin";}
        else if(!strcmp(fault,"map")||!strcmp(fault,"map-link"))leaf="blackwire.files";
        else if(!strcmp(fault,"digests"))leaf="blackwire.generation.digests";
        else if(!strcmp(fault,"companions")||!strcmp(fault,"missing-companions"))leaf="blackwire.companions";
        else if(!strcmp(fault,"original-map")){directory=state;leaf="source.original.files";}
        else if(!strcmp(fault,"original-digests")){directory=state;leaf="source.original.digests";}
        C(leaf&&snprintf(target,sizeof target,"%s/%s",directory,leaf)>0);
        if(!strcmp(fault,"missing-companions"))C(!unlink(target));
        else if(!strcmp(fault,"record-link")||!strcmp(fault,"map-link")){char held[2500];C(snprintf(held,sizeof held,"%s.held",target)>0&&!rename(target,held)&&!symlink(held,target));}
        else{int fd=open(target,O_RDWR|O_NOFOLLOW);unsigned char byte;off_t at=!strcmp(fault,"receipt-version")?8:0;C(fd>=0&&pread(fd,&byte,1,at)==1);byte^=1;C(pwrite(fd,&byte,1,at)==1&&!fsync(fd)&&!close(fd));}
    }
    struct bw_native_prepared raw={0},wrapped={0};int raw_rc=bw_native_send_inspect(argv[4],&binding,&raw);int wrapper_rc=bw_native_prepared_inspect(argv[4],&binding,&wrapped);
    if(positive){C(!raw_rc&&raw.state==BW_PREPARED_VALID&&raw.prepared_policy==expected_policy&&!raw.bytes_verified&&!memcmp(raw.record_sha256,fresh.record_sha256,32));C(!wrapper_rc&&wrapped.state==BW_PREPARED_VALID&&wrapped.prepared_policy==expected_policy&&!wrapped.bytes_verified&&wrapped.startup_upgrade_required==expected_upgrade&&!memcmp(wrapped.record_sha256,raw.record_sha256,32));
        C(raw.startup_upgrade_required==expected_upgrade);
        if(expected_policy==3)C(!strcmp(wrapped.prepared_component,binding.binding.game)&&wrapped.provenance_size>=384&&wrapped.provenance_size<=1024u*1024u);
    }else{C(raw_rc!=0||raw.state!=BW_PREPARED_VALID);C(wrapper_rc!=0||wrapped.state!=BW_PREPARED_VALID);C(!wrapped.bytes_verified);}
    C(absence_calls==(folder?2u:0u));
    C(fd_count()==descriptors);free(image);free(input.bytes);
    printf("{\"passed\":true,\"checks\":%u,\"format\":\"%s\",\"layout\":\"%s\",\"fault\":\"%s\",\"actualFreshSendValid\":true,\"preparedPolicy\":%u,\"actualSourceProviderRebuiltAfterSend\":true,\"actualRawReloadRc\":%d,\"actualRawReloadState\":%u,\"actualNormalWrapperRc\":%d,\"actualNormalWrapperState\":%u,\"reloadAccepted\":%s,\"bytesVerifiedOnReload\":%u,\"startupUpgradeRequired\":%u,\"oldPolicyAbsenceCalls\":%u,\"consoleActions\":0,\"networkActions\":0}\n",checks,argv[1],argv[5],fault,expected_policy,raw_rc,(unsigned)raw.state,wrapper_rc,(unsigned)wrapped.state,positive?"true":"false",wrapped.bytes_verified,wrapped.startup_upgrade_required,absence_calls);return 0;
}
