/* Actual producer -> retained execution/source admission -> controller envelope
 * -> DATA admission/cache/decoder/read. Only SMB transport is a local-file
 * adapter. Fixture module/eboot bodies are inert bytes and are never executed. */
#define BW_NATIVE_SEND_HOST_TEST 1
#define BW_NATIVE_FS_HOST_TEST 1
#define BW_NATIVE_PRELOAD 1
#define smb2_destroy_context namespace_destroy_context
#define main namespace_fixture_main
#include "../../compressed/namespace_test.c"
#undef main
#undef smb2_destroy_context
void smb2_destroy_context(struct smb2_context *);
#include "native_send.c"
#include "native_projection_handoff.h"
#include "reader/preload/source.h"

/* The raw namespace adapter above models actual SMB directory/held-file PDU
 * decoding. These synchronous methods model the same read-only share for the
 * actual container-source and DATA workers. No network socket is opened. */
struct smb2fh {int fd;struct smb2_context *owner;struct smb2fh *next;};
static struct smb2fh *sync_handles;
static struct smb2_context *allocated_contexts[16];
static char share_root[1024];
static unsigned contexts_created,contexts_destroyed,sync_reads,sync_opens,sync_closes;
static unsigned named_stats,held_stats;
static int remote_metadata_changed;
static void context_init(struct smb2_context *c){strcpy(c->root,share_root);for(unsigned i=0;i<64;i++)c->handles[i].fd=-1;}
struct smb2_context *smb2_init_context(void){struct smb2_context *c=calloc(1,sizeof *c);C(c);context_init(c);unsigned i=0;while(i<16&&allocated_contexts[i])i++;C(i<16);allocated_contexts[i]=c;contexts_created++;return c;}
void smb2_destroy_context(struct smb2_context *c){C(c);struct smb2fh **at=&sync_handles;while(*at){struct smb2fh *h=*at;if(h->owner!=c){at=&h->next;continue;}*at=h->next;C(!close(h->fd));sync_closes++;free(h);}namespace_destroy_context(c);for(unsigned i=0;i<16;i++)if(allocated_contexts[i]==c){allocated_contexts[i]=NULL;contexts_destroyed++;free(c);return;}}
void smb2_set_timeout(struct smb2_context *c,int n){C(c&&n==10);}
void smb2_set_user(struct smb2_context *c,const char *s){C(c&&s&&!strcmp(s,"fixture"));}
void smb2_set_password(struct smb2_context *c,const char *s){C(c&&s&&!s[0]);}
void smb2_set_sign(struct smb2_context *c,int n){C(c&&n==1);}
void smb2_set_security_mode(struct smb2_context *c,uint16_t n){C(c&&n==(SMB2_NEGOTIATE_SIGNING_ENABLED|SMB2_NEGOTIATE_SIGNING_REQUIRED));}
int smb2_connect_share(struct smb2_context *c,const char *server,const char *share,const char *user){C(c&&!strcmp(server,"192.0.2.1")&&!strcmp(share,"FixtureShare")&&!strcmp(user,"fixture"));return 0;}
int smb2_get_nterror(struct smb2_context *c){C(c);return 0;}
static int readonly_path(struct smb2_context *c,const char *path,char out[1500]){C(c&&path);C(!bw_manifest_validate_path(path,strlen(path)));C(!strcmp(path,"fixture.ffpfsc")||!strncmp(path,"fixture/",8));int n=snprintf(out,1500,"%s/%s",c->root,path);C(n>0&&n<1500);return 0;}
static void stat_convert(const struct stat *s,struct smb2_stat_64 *out){memset(out,0,sizeof *out);out->smb2_type=S_ISDIR(s->st_mode)?SMB2_TYPE_DIRECTORY:SMB2_TYPE_FILE;out->smb2_size=(uint64_t)s->st_size;out->smb2_ino=(uint64_t)s->st_ino;out->smb2_mtime=(uint64_t)s->st_mtim.tv_sec+(unsigned)remote_metadata_changed;out->smb2_mtime_nsec=(uint64_t)s->st_mtim.tv_nsec;out->smb2_ctime=(uint64_t)s->st_ctim.tv_sec;out->smb2_ctime_nsec=(uint64_t)s->st_ctim.tv_nsec;}
int smb2_stat(struct smb2_context *c,const char *path,struct smb2_stat_64 *out){char full[1500];readonly_path(c,path,full);struct stat st;named_stats++;if(lstat(full,&st))return -errno;C(S_ISREG(st.st_mode));stat_convert(&st,out);return 0;}
struct smb2fh *smb2_open(struct smb2_context *c,const char *path,int flags){char full[1500];readonly_path(c,path,full);C(flags==O_RDONLY);int fd=open(full,O_RDONLY|O_NOFOLLOW);if(fd<0)return NULL;struct smb2fh *h=calloc(1,sizeof *h);C(h);h->fd=fd;h->owner=c;h->next=sync_handles;sync_handles=h;sync_opens++;return h;}
int smb2_fstat(struct smb2_context *c,struct smb2fh *h,struct smb2_stat_64 *out){C(c&&h&&h->owner==c);struct stat st;held_stats++;if(fstat(h->fd,&st))return -errno;stat_convert(&st,out);return 0;}
int smb2_close(struct smb2_context *c,struct smb2fh *h){C(c&&h&&h->owner==c);struct smb2fh **at=&sync_handles;while(*at&&*at!=h)at=&(*at)->next;C(*at==h);*at=h->next;int result=close(h->fd);free(h);sync_closes++;return result;}
int smb2_pread(struct smb2_context *c,struct smb2fh *h,uint8_t *out,uint32_t count,uint64_t offset){C(c&&h&&h->owner==c&&out&&count&&count<=65536);sync_reads++;if(count>8191)count=8191;ssize_t n=pread(h->fd,out,count,(off_t)offset);return n<0?-errno:(int)n;}
int smb2_stat_async(struct smb2_context *c,const char *path,struct smb2_stat_64 *out,smb2_command_cb cb,void *opaque){C(cb);int rc=smb2_stat(c,path,out);cb(c,rc,NULL,opaque);return 0;}
int smb2_fstat_async(struct smb2_context *c,struct smb2fh *h,struct smb2_stat_64 *out,smb2_command_cb cb,void *opaque){C(cb);int rc=smb2_fstat(c,h,out);cb(c,rc,NULL,opaque);return 0;}
static int unmapped(void *opaque,const char *root,const char *title){C(opaque&&root&&!strcmp(title,"PPSA99001"));return 0;}
static unsigned char stream_byte(uint64_t at){return (unsigned char)((at*13u+7u)%251u);}
static int data_guard(void *opaque){C(opaque);return ((struct smb2_context *)opaque)->canceled;}
static const char share_bytes[]="server=192.0.2.1\nshare=FixtureShare\nuser=fixture\npassword=\n";
static struct bp_source *admit(const struct bw_native_projection_inputs *in,const char *selection,size_t selection_size,const void *envelope,size_t envelope_size,struct smb2_context *guard_context){struct bp_source *data=NULL;C(!bp_source_admit_projection(&data,in->physical_map,in->physical_map_size,selection,selection_size,share_bytes,sizeof share_bytes-1,envelope,envelope_size,data_guard,guard_context)&&data);return data;}
int main(int argc,char **argv){C(argc==5);int folder=!strcmp(argv[1],"folder");C(strlen(argv[2])<sizeof share_root);strcpy(share_root,argv[2]);struct smb2_context context={0};context_init(&context);struct bw_source_smb source={.smb=&context,.deadline=now()+600};struct bw_share_config config={0};C(!bw_share_parse(&config,share_bytes,sizeof share_bytes-1));
    struct bw_provider_binding binding;C(!bw_provider_binding_init(&binding,&config,folder?"fixture":"fixture.ffpfsc","PPSA99001",folder?BW_PROVIDER_FOLDER:BW_PROVIDER_CONTAINER));struct bw_container_smb image={0};if(!folder)C(!bw_container_smb_open_owned(&image,&source.smb,"fixture.ffpfsc"));
    struct bw_source_provider provider={0};C(!bw_source_provider_prepare_policy(&provider,&binding,&source,folder?NULL:&image,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",32u*1024u*1024u,current,&context,BW_SOURCE_PLAN_COMPANIONS_V1));
    uint8_t original_map_sha[32],original_generation[32];cp_sha_bytes(provider.bundle.map.bytes,provider.bundle.map.size,original_map_sha);memcpy(original_generation,provider.bundle.generation_sha256,32);
    char log[1500];C(snprintf(log,sizeof log,"%s/game.log",argv[3])>0);int logfd=open(log,O_CREAT|O_EXCL|O_WRONLY,0600);C(logfd>=0&&!close(logfd));struct bw_native_prepared prepared={0};struct bw_native_send_files send={.data_root=argv[3],.game_log=log,.provider=&provider,.current=current,.opaque=&context,.upgrade_unmapped=unmapped,.upgrade_opaque=&context};C(!bw_native_send_files(&send,&prepared)&&prepared.state==BW_PREPARED_VALID&&prepared.bytes_verified&&prepared.prepared_policy==3);
    struct bw_native_launch_admission *owner=NULL;const char *error=NULL;C(!bw_native_launch_admission_open(&owner,argv[3],&binding,&prepared,"cccccccccccccccccccccccccccccccc",current,&context,&error));struct bw_reader_selection selected;C(!bw_native_launch_admission_selection(owner,&selected)&&bw_reader_selection_is_projection(&selected));struct bw_native_projection_inputs in;C(bw_native_launch_admission_projection_inputs(owner,&in)<0);void *envelope=NULL;size_t envelope_size=0;C(bw_native_projection_handoff_bytes(owner,&selected,&envelope,&envelope_size)<0&&!envelope&&!envelope_size);
    uint8_t physical_map_sha[32],generation[32];C(!bw_native_launch_admission_source(owner,&config,&source,&selected,physical_map_sha,generation,&error)&&!error);C(!memcmp(generation,original_generation,32)&&memcmp(physical_map_sha,original_map_sha,32));C(!bw_native_launch_admission_projection_inputs(owner,&in));uint8_t actual[32];cp_sha_bytes(in.physical_map,in.physical_map_size,actual);C(!memcmp(actual,physical_map_sha,32));cp_sha_bytes(in.original_map,in.original_map_size,actual);C(!memcmp(actual,original_map_sha,32));
    C(!bw_native_projection_handoff_bytes(owner,&selected,&envelope,&envelope_size)&&envelope&&envelope_size);struct bw_reader_projection *proof=NULL;C(!bw_reader_projection_open(&proof,&selected,in.physical_map,in.physical_map_size,envelope,envelope_size,&error)&&proof&&!error);uint64_t image_mtime=0;C(!bw_reader_projection_source(proof,actual,generation,&image_mtime)&&!memcmp(actual,original_map_sha,32)&&!memcmp(generation,original_generation,32));if(!folder)C(image_mtime==image.admitted.smb2_mtime);bw_reader_projection_clear(&proof);
    char selection[BW_READER_SELECTION_BYTES+1];size_t selection_size=0;C(!bw_reader_selection_serialize(&selected,selection,sizeof selection,&selection_size));C(!strncmp(selection,"version=8\n",10));struct bp_source *data=NULL;unsigned before_contexts=contexts_created,before_reads=sync_reads,before_stats=named_stats+held_stats;
    C(bp_source_admit(&data,in.physical_map,in.physical_map_size,selection,selection_size,share_bytes,sizeof share_bytes-1,data_guard,&context)<0&&!data);C(bp_source_admit_projection(&data,in.physical_map,in.physical_map_size,selection,selection_size,share_bytes,sizeof share_bytes-1,NULL,0,data_guard,&context)<0&&!data);
    uint8_t *bad=malloc(envelope_size);C(bad);size_t corrupt[]={0,BW_READER_PROJECTION_HEADER,BW_READER_PROJECTION_HEADER+384,envelope_size-1};for(unsigned i=0;i<sizeof corrupt/sizeof *corrupt;i++){memcpy(bad,envelope,envelope_size);bad[corrupt[i]]^=1;C(bp_source_admit_projection(&data,in.physical_map,in.physical_map_size,selection,selection_size,share_bytes,sizeof share_bytes-1,bad,envelope_size,data_guard,&context)<0&&!data);}free(bad);C(contexts_created==before_contexts&&sync_reads==before_reads&&named_stats+held_stats==before_stats);
    data=admit(&in,selection,selection_size,envelope,envelope_size,&context);C(contexts_created==before_contexts&&sync_reads==before_reads&&named_stats+held_stats==before_stats);C(!bp_source_connect(data));struct bw_manifest physical;C(!bw_manifest_open(&physical,in.physical_map,in.physical_map_size));uint32_t stream_id=UINT32_MAX,matched=UINT32_MAX;unsigned local_companions=0;for(uint32_t i=0;i<physical.count;i++){struct bw_manifest_record row;C(!bw_manifest_get(&physical,i,&row));char app0[2048];C(snprintf(app0,sizeof app0,"/app0/%s",row.path)>0);if(row.flags==BW_MANIFEST_LOCAL){C(bp_source_match(data,app0,strlen(app0),row.size,&matched)==0);if(bw_source_companion_path_namespace(row.path)>0)local_companions++;}else if(!strcmp(row.path,"assets/stream.pak")){C(bp_source_match(data,app0,strlen(app0),row.size,&stream_id)==1&&stream_id==i&&row.size==180017);}}
    C(stream_id!=UINT32_MAX);if(strcmp(argv[4],"empty-f2"))C(local_companions>=2);uint8_t bytes[66000];before_reads=sync_reads;C(bp_source_read(data,stream_id,65520,bytes,sizeof bytes)==sizeof bytes&&sync_reads>before_reads);for(size_t i=0;i<sizeof bytes;i++)C(bytes[i]==stream_byte(65520+i));unsigned cold_reads=sync_reads-before_reads;before_reads=sync_reads;before_stats=named_stats+held_stats;C(bp_source_read(data,stream_id,65520,bytes,9)==9&&sync_reads==before_reads&&named_stats+held_stats>before_stats);for(size_t i=0;i<9;i++)C(bytes[i]==stream_byte(65520+i));C(bp_source_read(data,stream_id,180000,bytes,64)==17);for(size_t i=0;i<17;i++)C(bytes[i]==stream_byte(180000+i));C(!bp_source_close(data));data=NULL;
    /* Same sealed envelope against a different remote metadata generation must
     * fail before returning any asset bytes, with no alternate-source retry. */
    data=admit(&in,selection,selection_size,envelope,envelope_size,&context);C(!bp_source_connect(data));C(bp_source_match(data,"/app0/assets/stream.pak",23,180017,&matched)==1&&matched==stream_id);remote_metadata_changed=1;before_reads=sync_reads;memset(bytes,0xa5,sizeof bytes);C(bp_source_read(data,stream_id,0,bytes,1)<0&&bytes[0]==0xa5&&sync_reads==before_reads);C(!bp_source_close(data));remote_metadata_changed=0;
    free(envelope);C(!bw_native_launch_admission_close(&owner)&&!owner);bw_source_provider_destroy(&provider);if(!folder)C(!bw_container_smb_close(&image));C(!context.open);C(!sync_handles&&sync_opens==sync_closes&&contexts_created==contexts_destroyed);bw_share_clear(&config);
    printf("{\"passed\":true,\"checks\":%u,\"actualProducerAdmissionEnvelopeDATARead\":true,\"originalAndPhysicalGenerationSeparated\":true,\"legacyAdmitRefusesV8\":true,\"tamperedEnvelopeRefusedBeforeTransport\":true,\"coldReadCalls\":%u,\"modeledTransportOnly\":true}\n",checks,cold_reads);return 0;
}
