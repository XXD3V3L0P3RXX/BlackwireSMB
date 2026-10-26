#include "settings-check-under-test.h"
#include <stdio.h>
#include <stdarg.h>
#include <fcntl.h>
static unsigned cases,checks,connects,destroys,saves;
static int mode,idle_failure,late_failure;static uint32_t nt_status;
static char active_share[128],seen[2][128];static struct bw_share_config saved_config;
#define C(x) do {checks++;if(!(x)){fprintf(stderr,"case %u line %u: %s\n",cases,__LINE__,#x);exit(1);}} while(0)
enum {OK=0,MISSING_PRIMARY,MISSING_SECONDARY,AUTH_DENIED,TIMEOUT_CATALOG,CHANGED_CATALOG,ARTWORK,INVALID_CATALOG,
      FILE_DENIED,FILE_NOT_PERMITTED,OPEN_FAILED_UNKNOWN,IMAGE_GENERATION_CHANGED,READ_EOF_CLOSE_DENIED};
static void bw_send_log(const char *event,const char *format,...){(void)event;(void)format;}
struct smb2_context *smb2_init_context(void){return (struct smb2_context *)&connects;}
void smb2_destroy_context(struct smb2_context *s){C(s);destroys++;}
void smb2_set_timeout(struct smb2_context *s,int seconds){C(s&&seconds==10);}
void smb2_set_sign(struct smb2_context *s,int sign){C(s&&sign==1);}
void smb2_set_security_mode(struct smb2_context *s,uint16_t flags){C(s&&flags==(SMB2_NEGOTIATE_SIGNING_ENABLED|SMB2_NEGOTIATE_SIGNING_REQUIRED));}
void smb2_set_user(struct smb2_context *s,const char *user){C(s&&!strcmp(user,"project-fixture"));}
void smb2_set_password(struct smb2_context *s,const char *password){C(s&&!strcmp(password,"PRIVATE-FIXTURE-SECRET"));}
int smb2_connect_share(struct smb2_context *s,const char *server,const char *share,const char *user){
    C(s&&!strcmp(server,"192.0.2.10")&&!strcmp(user,"project-fixture")&&connects<2);
    strcpy(active_share,share);strcpy(seen[connects++],share);nt_status=0;
    if((mode==MISSING_PRIMARY&&connects==1)||(mode==MISSING_SECONDARY&&connects==2)){nt_status=UINT32_C(0xc00000cc);return -ENOENT;}
    if(mode==AUTH_DENIED){nt_status=UINT32_C(0xc000006d);return -EACCES;}return 0;
}
int smb2_get_nterror(struct smb2_context *s){C(s);return (int)nt_status;}
#ifndef BW_SETTINGS_ACTUAL_CATALOG
int bw_source_catalog_scan(struct bw_source_smb *s,struct bw_source_catalog *out){
    C(s&&s->smb);out->collections=1;s->requests=3;s->entries=1;
    if(mode==TIMEOUT_CATALOG||mode==CHANGED_CATALOG){out->error="catalog_image_generation_or_io_failed";s->error=mode==TIMEOUT_CATALOG?"source_scan_expired":"catalog_source_parent_changed";return -1;}
    if(mode==ARTWORK){out->count=out->capacity=1;out->items=calloc(1,sizeof *out->items);C(out->items);out->items[0].state=BW_CATALOG_CONTAINER_READY;}
    if(mode==INVALID_CATALOG)out->error="catalog_validation_failed";return 0;
}
int bw_source_catalog_container_cover(struct bw_source_smb *s,const struct bw_catalog_item *i,size_t max,void **bytes,size_t *size){
    (void)i;(void)max;(void)bytes;(void)size;C(mode==ARTWORK);s->error="catalog_image_cover_generation_or_io_failed";return -1;
}
int bw_source_smb_small_file(struct bw_source_smb *s,const char *path,size_t maximum,void **bytes,size_t *size,struct bw_source_info *info){
    (void)s;(void)path;(void)maximum;(void)bytes;(void)size;(void)info;abort();
}
void bw_source_catalog_destroy(struct bw_source_catalog *p){free(p->items);memset(p,0,sizeof *p);}
int bw_source_catalog_available(const struct bw_catalog_item *p){return p&&(p->state==BW_CATALOG_FOLDER||p->state==BW_CATALOG_CONTAINER_READY);}
void bw_source_catalog_image_hash(const struct smb2_stat_64 *s,uint8_t hash[32]){(void)s;(void)hash;abort();}
int bw_source_catalog_binding(struct bw_provider_binding *out,const struct bw_share_config *c,const struct bw_catalog_item *i){(void)out;(void)c;(void)i;abort();}
#else
/* Complete production catalog -> discovery -> setup worker failure chain.
 * The first share is empty; the second lists one immutable neutral image. */
static unsigned image_closes;
int bw_source_smb_list(struct bw_source_smb *s,const char *path,bw_source_visit_fn visit,void *opaque){
 C(s&&s->smb&&!path[0]);if(!strcmp(active_share,"Compressed"))return 0;
 C(!strcmp(active_share,"PS5Games"));s->requests++;s->entries++;
 struct bw_source_info info={.size=4096,.inode=123,
 .mtime_ticks=UINT64_C(116444736010000000),.ctime_ticks=UINT64_C(116444736010000000)};
 return visit(opaque,"Neutral game.ffpfsc",&info);
}
int smb2_stat(struct smb2_context *s,const char *path,struct smb2_stat_64 *out){
 C(s&&!strcmp(path,"Neutral game.ffpfsc"));
 if(mode==FILE_DENIED)return -EACCES;if(mode==FILE_NOT_PERMITTED)return -EPERM;
 memset(out,0,sizeof *out);out->smb2_type=SMB2_TYPE_FILE;out->smb2_size=4096;out->smb2_ino=123;out->smb2_mtime=out->smb2_ctime=1;return 0;
}
struct smb2fh *smb2_open(struct smb2_context *s,const char *path,int flags){
 C(s&&!strcmp(path,"Neutral game.ffpfsc")&&flags==O_RDONLY);
 if(mode==OPEN_FAILED_UNKNOWN){errno=EACCES;return NULL;}
 C(mode==IMAGE_GENERATION_CHANGED||mode==READ_EOF_CLOSE_DENIED);return (struct smb2fh *)&image_closes;
}
int smb2_fstat(struct smb2_context *s,struct smb2fh *h,struct smb2_stat_64 *out){
 C(s&&h==(struct smb2fh *)&image_closes&&(mode==IMAGE_GENERATION_CHANGED||mode==READ_EOF_CLOSE_DENIED));
 memset(out,0,sizeof *out);out->smb2_type=SMB2_TYPE_FILE;out->smb2_size=4096;out->smb2_ino=mode==IMAGE_GENERATION_CHANGED?124:123;out->smb2_mtime=out->smb2_ctime=1;return 0;
}
int smb2_close(struct smb2_context *s,struct smb2fh *h){C(s&&h==(struct smb2fh *)&image_closes);image_closes++;return mode==READ_EOF_CLOSE_DENIED?-EACCES:0;}
uint32_t smb2_get_max_read_size(struct smb2_context *s){C(s&&mode==READ_EOF_CLOSE_DENIED);return 65536;}
/* Either an exact header request or a bounded metadata window may reach EOF.
 * The fixture's declared image is 4096 bytes; request size is not the error
 * provenance contract this case verifies. */
int smb2_pread(struct smb2_context *s,struct smb2fh *h,uint8_t *b,uint32_t n,uint64_t at){C(s&&h==(struct smb2fh *)&image_closes&&b&&n>=48&&n<=4096&&!at&&mode==READ_EOF_CLOSE_DENIED);return 0;}
int bw_source_smb_file_info(struct bw_source_smb *s,const char *path,struct bw_source_info *out){(void)s;(void)path;(void)out;abort();}
int bw_source_smb_small_file(struct bw_source_smb *s,const char *path,size_t max,void **bytes,size_t *size,struct bw_source_info *info){(void)s;(void)path;(void)max;(void)bytes;(void)size;(void)info;abort();}
#endif
int bw_native_setup_generation_equal(const struct bw_setup_generation *a,const struct bw_setup_generation *b){return !memcmp(a,b,sizeof *a);}
static int removal_cache(struct bw_native_backend *p,struct bw_native_library *l,const struct bw_share_config *c){(void)p;(void)l;(void)c;return late_failure?-1:0;}
static int fixture_idle(void *p){C(p);return idle_failure;}
static int fixture_read(void *p,struct bw_share_config *c,struct bw_setup_generation *g){C(p);*c=saved_config;memset(g,0,sizeof *g);g->present=1;return BW_SETUP_VALID;}
static int fixture_save(void *p,const struct bw_share_config *c,const struct bw_setup_generation *g,const char *nonce,int (*guard)(void *),void *opaque,struct bw_setup_saved *saved){
    C(p&&g&&nonce&&guard&&!guard(opaque));saves++;saved_config=*c;saved->published=saved->verified=1;return BW_SETUP_SAVE_OK;
}
static struct bw_share_config config(void){struct bw_share_config c={0};strcpy(c.server,"192.0.2.10");strcpy(c.share,"Compressed");strcpy(c.share2,"PS5Games");strcpy(c.user,"project-fixture");strcpy(c.password,"PRIVATE-FIXTURE-SECRET");return c;}
static void init(struct bw_native_backend *p,int m){
    memset(p,0,sizeof *p);C(!pthread_mutex_init(&p->lock,NULL));mode=m;connects=destroys=saves=0;idle_failure=late_failure=0;memset(seen,0,sizeof seen);saved_config=config();strcpy(saved_config.share,"OldLibrary");
    p->settings_enabled=p->active=1;p->settings_known=1;p->settings_status=BW_SETUP_VALID;p->setup_operation=1;p->worker_kind=WORK_SETUP;p->setup_phase=SETUP_CHECKING;p->pending.config=config();
    p->ops.discover=discover;p->ops.idle=fixture_idle;p->ops.read_settings=fixture_read;p->ops.save_settings=fixture_save;p->opaque=p;
}
static void done(struct bw_native_backend *p){bw_native_library_destroy(&p->library);C(!pthread_mutex_destroy(&p->lock));}
static char *failure_json(struct bw_native_backend *p){struct bw_json j={.maximum=4096};setup_failure_json(&j,p);C(!j.bad&&j.bytes);C(!strstr(j.bytes,"PRIVATE-FIXTURE-SECRET")&&!strstr(j.bytes,"project-fixture"));return j.bytes;}
static void failed_case(int m,const char *share,const char *stage,const char *message){
    cases++;struct bw_native_backend p;init(&p,m);scan(&p);
    C(!p.active&&p.phase==BACKEND_FAILED&&p.setup_phase==SETUP_FAILED&&p.setup_error==SETUP_CONNECTION_ERROR&&!saves);
    C(!strcmp(saved_config.share,"OldLibrary"));C(!strcmp(p.check_failure.share,share)&&!strcmp(p.check_failure.stage,stage));
    C(strstr(setup_message(&p),share)&&strstr(setup_message(&p),message)&&strstr(setup_message(&p),"Saved settings were not replaced."));
    C(!strstr(setup_message(&p),"PRIVATE-FIXTURE-SECRET")&&!strstr(setup_message(&p),"project-fixture"));char *j=failure_json(&p);C(strstr(j,"\"settingsSaved\":false"));puts(j);free(j);C(destroys==connects);done(&p);
}
int main(void){
#ifdef BW_SETTINGS_ACTUAL_CATALOG
    const int modes[]={FILE_DENIED,FILE_NOT_PERMITTED,OPEN_FAILED_UNKNOWN,IMAGE_GENERATION_CHANGED,READ_EOF_CLOSE_DENIED};
    for(unsigned k=0;k<sizeof modes/sizeof *modes;k++){
        cases++;struct bw_native_backend p;init(&p,modes[k]);struct bw_share_config before=saved_config;scan(&p);
        C(!p.active&&p.phase==BACKEND_FAILED&&p.setup_phase==SETUP_FAILED&&p.setup_error==SETUP_CONNECTION_ERROR&&!saves);
        C(!memcmp(&before,&saved_config,sizeof before)&&connects==2&&destroys==connects);
        C(!strcmp(p.check_failure.share,"PS5Games")&&!strcmp(p.check_failure.stage,"catalog")&&
          !strcmp(p.check_failure.source,"Neutral game.ffpfsc")&&!strcmp(p.check_failure.source_stage,"container_open"));
        C(p.check_failure.nt_status==0&&strstr(p.check_message,"Saved settings were not replaced."));
        if(modes[k]==FILE_DENIED||modes[k]==FILE_NOT_PERMITTED){
            C(p.check_failure.result==(modes[k]==FILE_DENIED?-EACCES:-EPERM));
            C(p.check_failure.transport_result_known&&!strcmp(p.check_failure.source_operation,"admit_stat"));
            C(strstr(p.check_message,"read access to a game file was refused")&&strstr(p.check_message,"SMB share permissions")&&strstr(p.check_message,"Windows file Read permissions"));
            C(!strstr(p.check_message,"source changed")&&!strstr(p.check_message,"sign-in was refused"));
        }else if(modes[k]==OPEN_FAILED_UNKNOWN){
            C(p.check_failure.result==-1&&!p.check_failure.transport_result_known&&!strcmp(p.check_failure.source_operation,"open_readonly")&&!strstr(p.check_message,"read access to a game file was refused"));
            C(strstr(p.check_message,"could not be read or verified"));
        }else if(modes[k]==READ_EOF_CLOSE_DENIED){
            C(p.check_failure.transport_result==0&&p.check_failure.transport_result_known&&!strcmp(p.check_failure.source_operation,"pread"));
            C(!strstr(p.check_message,"read access to a game file was refused"));
        }else C(p.check_failure.container_result==BW_CONTAINER_STALE&&strstr(p.check_message,"could not be read consistently"));
        char *j=failure_json(&p);C(strstr(j,"\"sourcePath\":\"Neutral game.ffpfsc\"")&&strstr(j,"\"sourceStage\":\"container_open\"")&&strstr(j,"\"transportResult\":"));puts(j);free(j);done(&p);
    }
    cases++;struct bw_native_backend fresh;init(&fresh,FILE_DENIED);memset(&saved_config,0,sizeof saved_config);
    fresh.settings_status=BW_SETUP_MISSING;struct bw_share_config absent=saved_config;scan(&fresh);
    C(!saves&&!memcmp(&saved_config,&absent,sizeof absent)&&fresh.setup_phase==SETUP_FAILED&&!fresh.check_settings_saved);
    C(strstr(fresh.check_message,"Saved settings were not replaced."));done(&fresh);
    fprintf(stderr,"{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualCatalogDiscoveryAndSetupWorker\":true,\"normalDiagnosticsOff\":true,\"priorSettingsBytePreserved\":true,\"smbTransportMocked\":true,\"consoleContacted\":false}\n",cases,checks);return 0;
#else
    failed_case(MISSING_PRIMARY,"Compressed","smb-connect","shared folder was not found");
    failed_case(MISSING_SECONDARY,"PS5Games","smb-connect","shared folder was not found");C(connects==2&&!strcmp(seen[0],"Compressed")&&!strcmp(seen[1],"PS5Games"));
    failed_case(AUTH_DENIED,"Compressed","smb-connect","sign-in was refused");
    failed_case(TIMEOUT_CATALOG,"Compressed","catalog","scan timed out");
    failed_case(CHANGED_CATALOG,"Compressed","catalog","could not be read consistently");
    failed_case(ARTWORK,"Compressed","artwork","could not be read consistently");
    failed_case(INVALID_CATALOG,"Compressed","catalog","catalog could not be read");
    cases++;struct bw_native_backend p;init(&p,OK);strcpy(p.check_message,"previous failure");strcpy(p.check_failure.share,"DeletedTestShare");strcpy(p.check_failure.stage,"smb-connect");scan(&p);
    C(p.phase==BACKEND_READY&&p.setup_phase==SETUP_SAVED&&saves==1&&connects==2&&!strcmp(saved_config.share,"Compressed")&&!strcmp(saved_config.share2,"PS5Games")&&!*p.check_message&&!*p.check_failure.stage);
    char *j=failure_json(&p);C(!strcmp(j,"null"));free(j);done(&p);
    cases++;init(&p,OK);p.pending.unchanged=1;saved_config=p.pending.config;p.pending.expected.present=1;scan(&p);C(p.phase==BACKEND_READY&&p.setup_checked_only&&!saves);done(&p);
    cases++;init(&p,OK);idle_failure=1;scan(&p);C(p.setup_error==SETUP_IDLE_ERROR&&!connects&&!saves&&!strstr(setup_message(&p),"connection failed"));done(&p);
    cases++;init(&p,OK);late_failure=1;scan(&p);C(saves==1&&p.setup_error==SETUP_CONNECTION_ERROR&&strstr(setup_message(&p),"settings were saved")&&!strstr(setup_message(&p),"not replaced"));done(&p);
    cases++;init(&p,OK);struct bw_native_check_failure f={0};check_failure(&f,"Sp\xC3\xA9""cial\"Share","catalog",-1,0,"private-password=PRIVATE-FIXTURE-SECRET","source_scan_expired");check_failure_message(&p,&f,0);p.active=0;p.phase=BACKEND_FAILED;
    C(!strcmp(p.check_failure.reason,"detail_unavailable"));C(bw_json_utf8(p.check_message));j=failure_json(&p);C(strstr(j,"Sp\xC3\xA9""cial\\\"Share")&&!strstr(j,"PRIVATE-FIXTURE-SECRET"));free(j);done(&p);
    cases++;init(&p,OK);check_failure(&f,"Compressed","catalog",-1,0,"catalog_image_scan_expired",NULL);check_failure_message(&p,&f,0);p.active=1;j=failure_json(&p);C(!strcmp(j,"null"));free(j);done(&p);
    fprintf(stderr,"{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualDiscoveryAndSetupWorker\":true,\"smbTransportMocked\":true,\"consoleContacted\":false}\n",cases,checks);return 0;
#endif
}

