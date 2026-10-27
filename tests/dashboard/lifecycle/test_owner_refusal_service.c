/* SPDX-License-Identifier: GPL-3.0-or-later
 * Joined real service -> native reserved Send -> real NRC inventory. Actual
 * Send produces every owner receipt. Settings/idle/SMP are explicit host
 * observations; no console, network, payload or real game is contacted. */
#define bw_test_send_owner_admission retained_owner_admission_matrix
#include "test_uninstall_release_worker.c"
#undef bw_test_send_owner_admission
#include "dashboard_service.h"
#include "native_document.h"
#include <sys/wait.h>

struct joined_fixture {
    struct owner_admission_context *context;struct bw_native_backend *backend;
    unsigned reserves,releases,dispatches,nonces,response_mode;char native_error[512],owner_game[75];
};
static const char *const jf_allowed[]={
    "Registration ownership could not be released reliably. No startup files were sent; resolve the retained operation before retrying.",
    "Blackwire registration ownership could not be verified. No startup files were sent; keep existing copies and refresh status.",
    "Registration cleanup is pending or needs attention; Send will not repeat it. No startup files were sent.",
    "Another Blackwire copy already owns this title. Uninstall that copy before switching sources. No startup files were sent."
};
static void jf_response(struct bw_native_response *out,int status,const char *body){
    out->status=status;out->type="application/json";out->size=strlen(body);out->bytes=strdup(body);RCHECK(out->bytes);
}
static int jf_error(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    if(kn==5&&!memcmp(key,"error",5))return bw_document_string(raw,size,opaque,512);return 0;
}
static int jf_reserve(void *opaque,const char nonce[33],const char token[33]){
    struct joined_fixture *f=opaque;f->reserves++;return bw_native_backend_external_reserve(f->backend,nonce,token);
}
static int jf_release(void *opaque,const char nonce[33]){
    struct joined_fixture *f=opaque;f->releases++;return bw_native_backend_external_release(f->backend,nonce);
}
static int jf_read(void *opaque,const char *method,const char *url,const void *body,size_t size,const char *token,struct bw_native_response *out){
    struct joined_fixture *f=opaque;return bw_native_backend_handle_authenticated(f->backend,method,url,body,size,token,out);
}
static int jf_dispatch(void *opaque,const char nonce[33],const char *method,const char *url,const void *body,size_t size,const char *token,struct bw_native_response *out){
    struct joined_fixture *f=opaque;f->dispatches++;
    if(f->response_mode==20||f->response_mode==21){
        /* Explicit cache transition AFTER the actual service catalog and
         * dispatch guards, immediately before the actual native request.
         * A catalog freshly captured with these flags may disable Send. */
        RCHECK(!pthread_mutex_lock(&f->backend->lock)&&!f->backend->registration_pending_count&&!f->backend->registration_ledger_unknown);
        if(f->response_mode==20){f->backend->registration_pending_count=1;strcpy(f->backend->registration_pending[0].title,"PPSA99999");strcpy(f->backend->registration_pending[0].game,f->owner_game);}
        else f->backend->registration_ledger_unknown=1;
        RCHECK(!pthread_mutex_unlock(&f->backend->lock));
    }else if(f->response_mode){
        const char *body="{\"error\":\"unrecognized fixture refusal\"}";char duplicate[1024],allowed[512];
        unsigned mode=f->response_mode;int status=409,rc=0;
        if(mode==2)body="{\"error\":";
        if(mode==3){snprintf(duplicate,sizeof duplicate,"{\"error\":\"%s\",\"error\":\"%s\"}",jf_allowed[3],jf_allowed[3]);body=duplicate;}
        if(mode==4)body="{\"error\":7}";
        if(mode==5)body="{\"message\":\"fixture refusal\"}";
        if(mode==6){snprintf(duplicate,sizeof duplicate,"{\"error\":\"%s\",\"extra\":true}",jf_allowed[3]);body=duplicate;}
        if(mode==7||mode==8||mode>=10){unsigned index=mode>=10?mode-10:3;RCHECK(index<4);snprintf(allowed,sizeof allowed,"{\"error\":\"%s\"}",jf_allowed[index]);body=allowed;}
        if(mode==7)status=400;
        if(mode==8)rc=-1;
        if(mode==9){status=202;body="{\"ok\":true}";}
        jf_response(out,status,body);return rc;
    }
    int rc=bw_native_backend_handle_reserved(f->backend,nonce,method,url,body,size,token,out);
    memset(f->native_error,0,sizeof f->native_error);RCHECK(!rc&&out->status==409&&!bw_document_fields(out->bytes,out->size,jf_error,f->native_error));
    if(f->response_mode==20||f->response_mode==21){
        RCHECK(!pthread_mutex_lock(&f->backend->lock)&&!f->backend->control_release_unknown);
        f->backend->registration_pending_count=0;memset(f->backend->registration_pending,0,sizeof f->backend->registration_pending);f->backend->registration_ledger_unknown=0;
        RCHECK(!pthread_mutex_unlock(&f->backend->lock));
    }
    return rc;
}
static int jf_idle(void *opaque){RCHECK(opaque);return 0;}
static int jf_nonce(void *opaque,char nonce[33]){struct joined_fixture *f=opaque;memset(nonce,'c',32);snprintf(nonce+30,3,"%02x",++f->nonces);return 0;}
static int jf_http(void *opaque,enum bd_http_route route,const struct bd_http_options *options,struct bd_http_reply *out){
    RCHECK(opaque&&options&&route==BD_HTTP_SMP_GAMES);const char body[]="{\"status\":0,\"count\":0,\"games\":[]}";
    memset(out,0,sizeof *out);out->status=200;out->size=sizeof body-1;out->bytes=malloc(sizeof body);RCHECK(out->bytes);memcpy(out->bytes,body,sizeof body);return 0;
}
static int jf_clock(void *opaque,uint64_t *out){RCHECK(opaque&&out);*out=1000;return 0;}
static int jf_local(void *opaque,const struct bd_item *item,const char nonce[33]){(void)opaque;(void)item;(void)nonce;abort();}
static int jf_local_state(void *opaque,const char nonce[33]){(void)opaque;(void)nonce;abort();}
static int jf_capable(void *opaque,const struct bd_item *item){(void)opaque;(void)item;return 0;}
/* Unreached native identity operations, exactly as in maintained service tests. */
int rc_game_identity_admit(const struct rc_game_selection *s,struct rc_game_identity *out){(void)s;(void)out;abort();}
int rc_game_identity_current(const struct rc_game_identity *s,struct rc_game_identity *out){(void)s;(void)out;abort();}
int rc_game_identity_admit_with_ops(const struct rc_game_identity_ops *o,const struct rc_game_selection *s,struct rc_game_identity *out){(void)o;(void)s;(void)out;abort();}
int rc_game_identity_current_with_ops(const struct rc_game_identity_ops *o,const struct rc_game_identity *s,struct rc_game_identity *out){(void)o;(void)s;(void)out;abort();}
struct joined_catalog {char id[BD_ID_BYTES];uint64_t generation;unsigned items;};
static int jf_item_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct joined_catalog *c=opaque;if(kn==2&&!memcmp(key,"id",2))return bw_document_string(raw,size,c->id,sizeof c->id);return 0;
}
static int jf_item(void *opaque,const void *raw,size_t size){struct joined_catalog *c=opaque;c->items++;return bw_document_fields(raw,size,jf_item_field,c);}
static int jf_catalog_field(void *opaque,const char *key,size_t kn,const void *raw,size_t size){
    struct joined_catalog *c=opaque;
    if(kn==5&&!memcmp(key,"items",5))return bw_document_array(raw,size,jf_item,c);
    if(kn==17&&!memcmp(key,"catalogGeneration",17)){char number[32];RCHECK(size<sizeof number);memcpy(number,raw,size);number[size]=0;char *end=NULL;c->generation=strtoull(number,&end,10);RCHECK(end&&!*end);return 0;}return 0;
}
void bw_test_send_owner_admission(const struct bw_game_uninstall_request *original){
    retained_owner_admission_matrix(original);
    struct owner_admission_context context={0};strcpy(context.config.server,"192.0.2.1");strcpy(context.config.share,"FixtureShare");
    context.generation.present=1;context.generation.file.st_mode=S_IFREG|0600;context.generation.file.st_nlink=1;memset(context.generation.sha256,0x61,32);
    char area[2048],data[2048],name[2048],root[2048],log[2048],app[2048];oa_path(area,original->environment.data_root,"joined-owner-refusal");RCHECK(!mkdir(area,0700));
    oa_path(data,area,"data");RCHECK(!mkdir(data,0700));oa_path(context.apps,area,"apps");RCHECK(!mkdir(context.apps,0700));oa_path(context.database,area,"app.db");
    oa_sql(context.database,"CREATE TABLE tbl_contentinfo(titleId TEXT,contentId TEXT)");oa_path(log,area,"game.log");oa_file(log,"",0,0644);
    struct bw_source_provider *provider=original->ops.opaque;struct bw_native_prepared prepared={0};
    struct bw_native_send_files actual={.data_root=data,.game_log=log,.provider=provider,.current=oa_current,.opaque=&context};
    RCHECK(!bw_native_send_files(&actual,&prepared)&&prepared.state==BW_PREPARED_VALID);
    struct bw_manifest manifest;RCHECK(!bw_manifest_open(&manifest,provider->bundle.map.bytes,provider->bundle.map.size));
    struct bw_reader_selection selected={.source_kind=provider->identity.kind,.source_policy=provider->identity.policy,.manifest_count=manifest.count,.manifest_hash=manifest.checksum};
    strcpy(selected.source,provider->identity.binding.source);strcpy(selected.game,provider->identity.binding.game);strcpy(selected.title,provider->identity.binding.title);memset(selected.run,'d',32);selected.run[32]=0;
    for(uint32_t i=0;i<manifest.count;i++){struct bw_manifest_record row;RCHECK(!bw_manifest_get(&manifest,i,&row));if(!strcmp(row.path,"eboot.bin"))selected.eboot_size=row.size;}
    for(unsigned i=0;i<32;i++){snprintf(selected.binding_hex+2*i,3,"%02x",provider->identity.binding.sha256[i]);snprintf(selected.prepared_record_hex+2*i,3,"%02x",prepared.record_sha256[i]);}
    struct bw_nrc_environment environment={data,context.apps,context.database};struct bw_nrc_ops ops={.opaque=&context,.current=oa_current,.sources=oa_sources,.uninstall=oa_uninstall};
    struct bw_nrc_ticket *ticket=NULL;struct bw_nrc_result result={0};RCHECK(!bw_nrc_begin(&environment,&selected,&ops,&ticket,&result)&&ticket);
    oa_path(name,data,"games");oa_path(root,name,selected.game);oa_path(app,context.apps,selected.title);RCHECK(!mkdir(app,0755));oa_path(name,app,"sce_sys");RCHECK(!mkdir(name,0755));
    oa_path(name,app,"sce_sys/param.json");const char param[]="{\"titleId\":\"PPSA99999\",\"contentId\":\"UP0000-PPSA99999_00-BLACKWIREFIXTURE0\"}";oa_file(name,param,sizeof param-1,0644);
    oa_path(name,app,"mount.lnk");char mount[2100];int written=snprintf(mount,sizeof mount,"%s\n",root);RCHECK(written>0&&(size_t)written<sizeof mount);oa_file(name,mount,(size_t)written,0644);
    oa_sql(context.database,"INSERT INTO tbl_contentinfo VALUES('PPSA99999','UP0000-PPSA99999_00-BLACKWIREFIXTURE0')");
    struct bw_register_result registered={.add_attempted=1,.registered=1};RCHECK(!bw_nrc_finish(ticket,0,&registered,&result)&&result.state==BW_NRC_OWNED&&!bw_nrc_ticket_destroy(ticket));
    struct bw_native_backend *p=calloc(1,sizeof *p);RCHECK(p&&!pthread_mutex_init(&p->lock,NULL)&&!pthread_mutex_init(&p->dispatch_lock,NULL));
    p->opaque=&context;p->ops.read_settings=oa_settings;p->ops.prepare=oa_prepare;p->ops.nonce=oa_nonce;p->ops.idle=oa_current;
    p->settings_enabled=p->settings_known=1;p->settings_status=BW_SETUP_VALID;p->settings_generation=context.generation;p->settings_revision=2;p->generation=5;p->phase=BACKEND_READY;
    strcpy(p->server,context.config.server);strcpy(p->share,context.config.share);strcpy(p->setup_token,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    p->jobs=bw_native_jobs_create();p->library.catalog.items=calloc(1,sizeof *p->library.catalog.items);p->library.prepared=calloc(1,sizeof *p->library.prepared);
    RCHECK(p->jobs&&p->library.catalog.items&&p->library.prepared);p->library.catalog.count=p->library.catalog.capacity=1;
    struct bw_catalog_item *item=p->library.catalog.items;strcpy(item->source,"Fixture Alternative");strcpy(item->name,"PC fixture");strcpy(item->title,"PC fixture");strcpy(item->title_id,selected.title);item->state=BW_CATALOG_FOLDER;
    RCHECK(!bw_source_catalog_binding(&context.selected,&context.config,item)&&strcmp(context.selected.binding.game,selected.game));
    const char *saved_data=backend_data_root;struct bw_nrc_environment saved_environment=registration_environment;backend_data_root=data;registration_environment=environment;
    struct joined_fixture f={.context=&context,.backend=p};strcpy(f.owner_game,selected.game);
    struct bd_service_config cfg={.backend=p,.apps_root=context.apps,.database=context.database,.native_frontend_disabled=1,.observe_bfp=0,
      .ops={.opaque=&f,.idle=jf_idle,.reserve=jf_reserve,.release=jf_release,.backend_reserved=jf_dispatch,.local_launch=jf_local,.local_state=jf_local_state,.local_capable=jf_capable,.backend_read=jf_read,.http=jf_http,.now_ms=jf_clock,.nonce=jf_nonce}};
    struct bd_dashboard_service *service=bd_service_create(&cfg);RCHECK(service&&!bd_service_attach_preparation_guard(service));
    struct bw_native_response response={0};RCHECK(!bd_service_handle(service,"GET","/api/dashboard/library",NULL,0,NULL,&response)&&response.status==200);
    struct joined_catalog catalog={0};RCHECK(!bw_document_fields(response.bytes,response.size,jf_catalog_field,&catalog)&&catalog.items==1&&catalog.generation&&*catalog.id);bw_native_response_clear(&response);
    char body[512];int n=snprintf(body,sizeof body,"{\"id\":\"%s\",\"kind\":1,\"action\":\"send\",\"catalogGeneration\":%llu}",catalog.id,(unsigned long long)catalog.generation);RCHECK(n>0&&(size_t)n<sizeof body);
    unsigned char before[32],after[32];oa_snapshot(area,before);
    RCHECK(!bd_service_handle(service,"POST","/api/dashboard/action",body,(size_t)n,p->setup_token,&response)&&response.status==409);
    char error[512]={0};RCHECK(!bw_document_fields(response.bytes,response.size,jf_error,error));
    const char expected[]="Another Blackwire copy already owns this title. Uninstall that copy before switching sources. No startup files were sent.";
    RCHECK(!strcmp(f.native_error,expected)&&f.reserves==1&&f.dispatches==1&&f.releases==1&&!*p->external_nonce&&!context.nonce_calls&&!context.prepare_calls&&!p->active&&!p->joinable);
    struct bw_native_job_view jobs={0};RCHECK(!bw_native_jobs_snapshot(p->jobs,&jobs)&&!jobs.count);oa_snapshot(area,after);RCHECK(!memcmp(before,after,32));
    int old=getenv("BW_OWNER_REFUSAL_EXPECT_OLD")!=NULL;
    if(old)RCHECK(!strcmp(error,"The selected operation was refused before a known acceptance.")&&strcmp(error,expected));
    else RCHECK(!strcmp(error,expected));
    bw_native_response_clear(&response);
    unsigned fallback_controls=0,whitelist_controls=0,unknown_controls=0,early_controls=0;
    int early_old=getenv("BW_OWNER_EARLY_REFUSAL_EXPECT_OLD")!=NULL;
    if(!old){
        /* Alternate recognized and malformed replies in this SAME service.
         * A previous stack refusal must never leak into a new nonce. */
        const unsigned modes[]={1,10,2,11,3,12,4,5,6,7};
        for(unsigned i=0;i<sizeof modes/sizeof *modes;i++){
            f.response_mode=modes[i];unsigned reserves=f.reserves,releases=f.releases,dispatches=f.dispatches;
            RCHECK(!bd_service_handle(service,"POST","/api/dashboard/action",body,(size_t)n,p->setup_token,&response)&&response.status==409);
            memset(error,0,sizeof error);RCHECK(!bw_document_fields(response.bytes,response.size,jf_error,error));
            if(f.response_mode>=10){RCHECK(!strcmp(error,jf_allowed[f.response_mode-10]));whitelist_controls++;}
            else{RCHECK(!strcmp(error,"The selected operation was refused before a known acceptance."));fallback_controls++;}
            RCHECK(f.reserves==reserves+1&&f.releases==releases+1&&f.dispatches==dispatches+1&&!*p->external_nonce&&!context.nonce_calls&&!context.prepare_calls&&!p->active&&!p->joinable);
            RCHECK(!bw_native_jobs_snapshot(p->jobs,&jobs)&&!jobs.count);oa_snapshot(area,after);RCHECK(!memcmp(before,after,32));bw_native_response_clear(&response);
        }
        for(unsigned mode=20;mode<=21;mode++){
            f.response_mode=mode;unsigned reserves=f.reserves,releases=f.releases,dispatches=f.dispatches;
            RCHECK(!bd_service_handle(service,"POST","/api/dashboard/action",body,(size_t)n,p->setup_token,&response)&&response.status==409);
            memset(error,0,sizeof error);RCHECK(!bw_document_fields(response.bytes,response.size,jf_error,error));
            if(early_old){
                RCHECK(!strcmp(f.native_error,"Registration cleanup is pending or needs attention; Send will not repeat it"));
                RCHECK(!strcmp(error,"The selected operation was refused before a known acceptance."));
            }else{
                const char *expected_early=jf_allowed[mode==20?2:1];RCHECK(!strcmp(f.native_error,expected_early)&&!strcmp(error,expected_early));
                if(mode==21)RCHECK(!strstr(error,"cleanup is pending"));
            }
            RCHECK(f.reserves==reserves+1&&f.releases==releases+1&&f.dispatches==dispatches+1&&!*p->external_nonce&&!context.nonce_calls&&!context.prepare_calls&&!p->active&&!p->joinable);
            RCHECK(!bw_native_jobs_snapshot(p->jobs,&jobs)&&!jobs.count);oa_snapshot(area,after);RCHECK(!memcmp(before,after,32));bw_native_response_clear(&response);early_controls++;
        }
        /* Transport uncertainty and malformed202 cannot release authority.
         * Each isolated child ends while that model owner is intentionally
         * retained; no destructor, force reset or fake success is invoked. */
        for(unsigned mode=8;mode<=9;mode++){
            RCHECK(!fflush(NULL));pid_t child=fork();RCHECK(child>=0);
            if(!child){
                f.response_mode=mode;unsigned releases=f.releases;
                RCHECK(!bd_service_handle(service,"POST","/api/dashboard/action",body,(size_t)n,p->setup_token,&response)&&response.status==503);
                memset(error,0,sizeof error);RCHECK(!bw_document_fields(response.bytes,response.size,jf_error,error)&&strstr(error,"uncertain")&&!strstr(error,"Another Blackwire copy"));
                RCHECK(*p->external_nonce&&f.releases==releases&&!context.nonce_calls&&!context.prepare_calls&&!p->active&&!p->joinable);
                RCHECK(!bw_native_jobs_snapshot(p->jobs,&jobs)&&!jobs.count&&bd_service_destroy(service)==1);
                oa_snapshot(area,after);RCHECK(!memcmp(before,after,32));bw_native_response_clear(&response);
                printf("{\"ownerServiceUnknownControl\":true,\"mode\":%u,\"authorityRetained\":true,\"noJobOrCopy\":true,\"fixtureTreeUnchanged\":true,\"isolatedProcessExit\":true}\n",mode);
                RCHECK(!fflush(NULL));_exit(0);
            }
            int status=0;RCHECK(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status));unknown_controls++;
            oa_snapshot(area,after);RCHECK(!memcmp(before,after,32));
        }
    }
    RCHECK(!bd_service_destroy(service));
    RCHECK(!bw_native_jobs_destroy(p->jobs));bw_native_library_destroy(&p->library);RCHECK(!pthread_mutex_destroy(&p->dispatch_lock)&&!pthread_mutex_destroy(&p->lock));free(p);
    backend_data_root=saved_data;registration_environment=saved_environment;
    printf("{\"ownerServiceJoined\":true,\"expectedOldRed\":%s,\"expectedEarlyOldRed\":%s,\"ownershipRefusalPreserved\":%s,\"actualNrcOwner\":true,\"actualSendPreflight\":true,\"actualReservedBackend\":true,\"noJobOrCopy\":true,\"outerReservationReleased\":true,\"fixtureTreeUnchanged\":true,\"fallbackResetControls\":%u,\"additionalWhitelistControls\":%u,\"unknownRetentionControls\":%u,\"actualEarlyCacheRefusalControls\":%u,\"cacheTransitionModeledAfterCatalog\":true,\"earlyCacheCleartext\":%s,\"checks\":%u}\n",old?"true":"false",early_old?"true":"false",old?"false":"true",fallback_controls,whitelist_controls,unknown_controls,early_controls,early_old?"false":"true",release_checks);
}

