#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "native_owned_registration.h"
#include "native_launch.h"
#include "native_prepared_target.h"
#include "../streamreader/copy_sha256.h"
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

struct owned_pin {char path[2048];uint8_t hash[32];struct stat held;size_t limit;mode_t mode;unsigned captured;};
enum upgrade_stage {UP_REQUEST,UP_STATE,UP_NEW_LOCAL,UP_NEW_SELECTION,UP_PRIOR_LOOKUP,UP_PRIOR_LOCAL,UP_PRIOR_SELECTION,UP_PINS,UP_REGISTRATION,UP_MUTATION};
enum upgrade_failure {UP_OK,UP_CANCELLED,UP_CHANGED,UP_CLOCK,UP_EXPIRED,UP_PIN_CHANGED};
struct upgrade {
    const struct bw_native_register_request *request;
    struct bw_native_owned_registration_result *result;
    struct bw_provider_binding identity;
    struct bw_reader_selection prior;
    char selected[BW_READER_SELECTION_BYTES+1],old_root[2048],new_root[2048];size_t selected_size;
    struct owned_pin old_pin,new_pin;unsigned pins;
    double deadline,last_time;
    enum upgrade_stage stage;
    enum upgrade_failure failure;
    unsigned mutation_budget;
};
static double upgrade_now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (double)t.tv_sec+(double)t.tv_nsec/1e9;}
static int same_object(const struct stat *a,const struct stat *b,mode_t mode){
    return S_ISREG(b->st_mode)&&b->st_nlink==1&&(b->st_mode&07777)==mode&&b->st_uid==geteuid()&&a->st_uid==b->st_uid&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&a->st_size==b->st_size&&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int pin_current(struct owned_pin *p){
    char canonical[PATH_MAX];uint8_t bytes[BW_DERIVED_TERMINAL_LIMIT+1],again[BW_DERIVED_TERMINAL_LIMIT],hash[32];struct stat held,after,named;
    if(!realpath(p->path,canonical)||strcmp(canonical,p->path))return -1;int fd=open(p->path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;int bad=1;
    if(fstat(fd,&held)||!same_object(&held,&held,p->mode)||held.st_size<=0||(uint64_t)held.st_size>p->limit||
       (p->limit==384&&held.st_size!=384)||(p->captured&&!same_object(&p->held,&held,p->mode)))goto done;
    size_t size=(size_t)held.st_size;
    if(pread(fd,bytes,size+1,0)!=(ssize_t)size||pread(fd,again,size,0)!=(ssize_t)size||memcmp(bytes,again,size))goto done;
    cp_sha_bytes(bytes,size,hash);
    if(memcmp(hash,p->hash,32)||fstat(fd,&after)||lstat(p->path,&named)||!same_object(&held,&after,p->mode)||!same_object(&after,&named,p->mode)||
       !realpath(p->path,canonical)||strcmp(canonical,p->path))goto done;
    p->held=held;p->captured=1;bad=0;
done:
    if(close(fd))bad=1;return bad?-1:0;
}
static int binding_same(const struct bw_provider_binding *a,const struct bw_provider_binding *b){
    return a&&b&&memchr(a->binding.source,0,sizeof a->binding.source)&&memchr(a->binding.game,0,sizeof a->binding.game)&&
        memchr(a->binding.title,0,sizeof a->binding.title)&&a->kind==b->kind&&a->policy==b->policy&&
        !strcmp(a->binding.source,b->binding.source)&&!strcmp(a->binding.game,b->binding.game)&&!strcmp(a->binding.title,b->binding.title)&&
        !memcmp(a->binding.sha256,b->binding.sha256,32);
}
static int current(void *raw){
    struct upgrade *p=raw;const struct bw_native_register_request *r=p->request;
    char text[BW_READER_SELECTION_BYTES+1];size_t size=0;
    if(p->failure)return -1;
    if(r->current(r->guard)){p->failure=UP_CANCELLED;return -1;}
    if(!binding_same(r->identity,&p->identity)||bw_reader_selection_serialize(r->selected,text,sizeof text,&size)||
       size!=p->selected_size||memcmp(text,p->selected,size)){p->failure=UP_CHANGED;return -1;}
    double now=upgrade_now();if(now<0||now<p->last_time){p->failure=UP_CLOCK;return -1;}
    if(now>=p->deadline){p->failure=UP_EXPIRED;return -1;}p->last_time=now;
    if(p->pins&&(pin_current(&p->old_pin)||pin_current(&p->new_pin))){p->failure=UP_PIN_CHANGED;return -1;}return 0;
}
static int post(struct upgrade *p,enum bw_sm_operation op,const char *root,struct bw_sm_reply *reply){
    const struct bw_sm_connection connection={.port=p->request->port,.current=current,.opaque=p};
    return current(p)||bw_sm_post(&connection,op,root,reply)||current(p)?-1:0;
}
static int listed(struct upgrade *p,unsigned *old,unsigned *next){
    struct bw_sm_reply before={0},after={0};
    if(post(p,BW_SM_MANUAL_LIST,p->old_root,&before)||post(p,BW_SM_MANUAL_LIST,p->new_root,&after))return -1;
    *old=before.present;*next=after.present;return 0;
}
/* Only exact original and exact derived registrations are admitted. UNKNOWN
 * can be observed read-only during publication but never authorizes mutation. */
enum registration {REG_BAD=-1,REG_OLD,REG_NEW,REG_WAIT,REG_UNKNOWN};
static enum registration registration(struct upgrade *p){
    if(current(p))return REG_BAD;const struct bw_native_register_request *r=p->request;
    enum bw_registration_state state=bw_native_registration_state(r->data_root,r->apps,r->database,r->selected);
    if(state==BW_REG_MATCH)return current(p)?REG_BAD:REG_NEW;
    if(state==BW_REG_CONFLICT){
        if(bw_native_registration_verify(r->data_root,r->apps,r->database,&p->prior)||current(p))return REG_BAD;
        return REG_OLD;
    }
    if(state==BW_REG_ABSENT||state==BW_REG_PENDING)return current(p)?REG_BAD:REG_WAIT;
    return REG_UNKNOWN;
}
static int mutation_allowed(struct upgrade *p){enum registration state=registration(p);return state!=REG_OLD&&state!=REG_NEW?-1:0;}
static void progress(struct upgrade *p,const char *message){
    if(p->request->observe){const struct bw_reader_event event={.phase="preflight",.message=message};p->request->observe(p->request->observer,&event);}
}
static void stage(struct upgrade *p,enum upgrade_stage value){
    static const char *const messages[]={
        "The owned registration upgrade request could not be validated.",
        "The current registration could not be checked under the accepted launch guard.",
        "The selected startup files or their exact terminal could not be verified. Registration was preserved.",
        "The selected startup folder no longer matches the accepted launch selection. Registration was preserved.",
        "No exact owned earlier registration of this original source could be qualified. Registration was preserved.",
        "The earlier owned startup files or their exact terminal could not be verified. Registration was preserved.",
        "The earlier owned registration no longer matches its exact startup folder. Registration was preserved.",
        "The exact earlier and selected terminal pins could not be retained. Registration was preserved.",
        "The earlier owned registration changed before the upgrade. Registration was preserved.",
        "The owned registration change could not be confirmed. No mutation was repeated."};
    static const char *const steps[]={
        "Checking the owned registration upgrade request.","Checking the current owned registration.",
        "Verifying the selected startup files before registration upgrade.","Checking the exact selected startup folder.",
        "Finding the exact owned earlier registration.","Verifying the earlier owned startup files.",
        "Checking the exact earlier owned registration.","Retaining both exact startup terminal pins.",
        "Confirming the earlier registration before the owned upgrade.","Checking the owned registration change."};
    if(p->failure)return;p->stage=value;p->result->message=messages[value];progress(p,steps[value]);
}
static const char *qualification_timeout(enum upgrade_stage value){
    static const char *const messages[]={
        "Owned registration qualification exceeded its 300-second budget while checking the request. Registration was preserved.",
        "Owned registration qualification exceeded its 300-second budget while checking the current registration. Registration was preserved.",
        "Owned registration qualification exceeded its 300-second budget while verifying the selected startup files. Registration was preserved.",
        "Owned registration qualification exceeded its 300-second budget while checking the selected startup folder. Registration was preserved.",
        "Owned registration qualification exceeded its 300-second budget while finding the earlier owned registration. Registration was preserved.",
        "Owned registration qualification exceeded its 300-second budget while verifying the earlier owned startup files. Registration was preserved.",
        "Owned registration qualification exceeded its 300-second budget while checking the earlier owned registration. Registration was preserved.",
        "Owned registration qualification exceeded its 300-second budget while retaining both exact terminal pins. Registration was preserved.",
        "Owned registration qualification exceeded its 300-second budget while confirming the earlier registration. Registration was preserved.",
        "Owned registration qualification exceeded its 300-second budget before mutation. Registration was preserved."};
    return messages[value];
}
static int mutation_budget(struct upgrade *p){
    /* Never rearm an expired/cancelled qualification. current() rechecks the
     * accepted guard, selection and both exact terminal pins before transition. */
    if(current(p))return -1;double now=upgrade_now();
    if(now<0||now<p->last_time){p->failure=UP_CLOCK;return -1;}
    if(now>=p->deadline){p->failure=UP_EXPIRED;return -1;}
    p->last_time=now;p->deadline=now+60;p->mutation_budget=1;stage(p,UP_MUTATION);return current(p);
}
static int hex(const char *text,uint8_t out[32]){
    const char *digits="0123456789abcdef";if(strnlen(text,65)!=64)return -1;
    for(unsigned i=0;i<32;i++){const char *a=strchr(digits,text[2*i]),*b=strchr(digits,text[2*i+1]);if(!a||!b)return -1;out[i]=(uint8_t)(((a-digits)<<4)|(b-digits));}return 0;
}
static int qualify_prior(struct upgrade *p,unsigned policy){
    const struct bw_native_register_request *r=p->request;struct bw_native_prepared prepared={0};struct bw_native_derived_snapshot snapshot={0};int bad=-1;
    if(current(p))goto done;stage(p,UP_PRIOR_LOCAL);
    if(policy==BW_DERIVED_PLAN_POLICY){
        if(bw_native_derived_send_inspect(r->data_root,r->identity,&snapshot)||snapshot.prepared.base.state!=BW_PREPARED_VALID||snapshot.prepared.prepared_policy!=BW_DERIVED_PLAN_POLICY)goto done;
        uint8_t expected[32];memcpy(expected,snapshot.prepared.terminal_sha256,32);bw_native_derived_snapshot_destroy(&snapshot);
        if(bw_native_derived_send_local_verify(r->data_root,r->identity,expected,current,p,&snapshot)||!snapshot.prepared.base.bytes_verified||bw_native_prepared_from_derived(&snapshot.prepared,&prepared)||prepared.prepared_policy!=BW_DERIVED_PLAN_POLICY)goto done;
    }else if(policy||bw_native_send_verify(r->data_root,r->identity,current,p,&prepared)||prepared.state!=BW_PREPARED_VALID||!prepared.bytes_verified||prepared.prepared_policy)goto done;
    /* Expected-policy launch inspection is deliberately explicit: a newly
     * prepared policy2 generation never selects/reinterprets the old V1 root. */
    stage(p,UP_PRIOR_SELECTION);
    struct bw_reader_selection selected={0};char component[75];
    if(bw_native_launch_selection(r->data_root,r->identity,&prepared,r->selected->run,&selected)||bw_native_registration_verify(r->data_root,r->apps,r->database,&selected)||current(p)||bw_native_selected_root(&selected,component))goto done;
    int a=snprintf(p->old_root,sizeof p->old_root,"%s/games/%s",r->data_root,component),
        b=snprintf(p->old_pin.path,sizeof p->old_pin.path,"%s/%s/%s/prepared.bin",r->data_root,policy?"native-derived-prepared":"native-prepared",r->identity->binding.game);
    if(a<0||(size_t)a>=sizeof p->old_root||b<0||(size_t)b>=sizeof p->old_pin.path)goto done;
    p->prior=selected;memcpy(p->old_pin.hash,prepared.record_sha256,32);p->old_pin.limit=policy?BW_DERIVED_TERMINAL_LIMIT:384u;p->old_pin.mode=0600;bad=0;
done:bw_native_derived_snapshot_destroy(&snapshot);return bad;
}
struct prior_batch_search {
    struct upgrade *owner;
    struct bw_native_prepared prepared;
    unsigned matches;
};
static int prior_batch_candidate(const struct bw_native_batch_snapshot *snapshot,void *opaque){
    struct prior_batch_search *search=opaque;struct upgrade *p=search->owner;
    const struct bw_native_register_request *r=p->request;struct bw_native_prepared candidate={0};
    if(current(p)||bw_native_prepared_from_batch(&snapshot->prepared,&candidate))return -1;
    if(!strcmp(candidate.prepared_component,r->selected->prepared_game))return 0;
    /* Historical metadata only selects an inspection candidate. The actual
     * retained registration must match it, then fresh LOCAL hashes and exact
     * receipt pinning below qualify the one source before any mutation. */
    struct bw_reader_selection selected={0};
    if(bw_native_launch_batch_snapshot(r->identity,snapshot,r->selected->run,&selected)||current(p))return -1;
    int match=!bw_native_registration_verify(r->data_root,r->apps,r->database,&selected);
    if(current(p))return -1;if(!match)return 0;
    if(search->matches++)return -1;search->prepared=candidate;return 0;
}
static int qualify_prior_batch(struct upgrade *p){
    const struct bw_native_register_request *r=p->request;
    struct prior_batch_search search={.owner=p};struct bw_native_batch_snapshot snapshot={0};
    struct bw_native_prepared prepared={0};struct bw_reader_selection selected={0};int bad=-1;
    if(current(p))goto done;stage(p,UP_PRIOR_LOOKUP);
    if(bw_native_batch_visit(r->data_root,r->identity,prior_batch_candidate,&search)||search.matches!=1||current(p))goto done;
    stage(p,UP_PRIOR_LOCAL);
    if(bw_native_batch_local_verify_exact(r->data_root,r->identity,search.prepared.prepared_component,search.prepared.record_sha256,current,p,&snapshot)||
       !snapshot.prepared.base.bytes_verified||bw_native_prepared_from_batch(&snapshot.prepared,&prepared)||
       prepared.prepared_policy!=BW_BATCH_POLICY||strcmp(prepared.prepared_component,search.prepared.prepared_component)||
       memcmp(prepared.record_sha256,search.prepared.record_sha256,32))goto done;
    stage(p,UP_PRIOR_SELECTION);
    if(bw_native_launch_batch_snapshot(r->identity,&snapshot,r->selected->run,&selected)||
       bw_native_registration_verify(r->data_root,r->apps,r->database,&selected)||current(p))goto done;
    int n=snprintf(p->old_root,sizeof p->old_root,"%s/games/%s",r->data_root,prepared.prepared_component);
    if(n<0||(size_t)n>=sizeof p->old_root||
       bw_native_batch_record_path_exact(r->data_root,r->identity,prepared.prepared_component,prepared.record_sha256,p->old_pin.path,sizeof p->old_pin.path))goto done;
    p->prior=selected;memcpy(p->old_pin.hash,prepared.record_sha256,32);
    p->old_pin.limit=BW_BATCH_TERMINAL_LIMIT;p->old_pin.mode=0644;bad=0;
done:
    bw_native_batch_snapshot_destroy(&snapshot);return bad;
}
static int qualify(struct upgrade *p){
    const struct bw_native_register_request *r=p->request;const struct bw_reader_selection *s=r->selected;
    struct bw_native_prepared derived={0};struct bw_native_derived_snapshot snapshot={0};struct bw_native_batch_snapshot batch={0};struct bw_reader_selection selected;
    if(hex(s->prepared_record_hex,p->new_pin.hash)||current(p))return -1;
    stage(p,UP_NEW_LOCAL);
    int bad;
    if(s->prepared_policy==BW_BATCH_POLICY){
        bad=bw_native_batch_local_verify_exact(r->data_root,r->identity,s->prepared_game,p->new_pin.hash,current,p,&batch)||!batch.prepared.base.bytes_verified||bw_native_prepared_from_batch(&batch.prepared,&derived)||derived.prepared_policy!=BW_BATCH_POLICY;
        p->new_pin.limit=BW_BATCH_TERMINAL_LIMIT;p->new_pin.mode=0644;
    }else{
        bad=bw_native_derived_send_local_verify(r->data_root,r->identity,p->new_pin.hash,current,p,&snapshot)||!snapshot.prepared.base.bytes_verified||bw_native_prepared_from_derived(&snapshot.prepared,&derived)||derived.prepared_policy!=BW_DERIVED_PLAN_POLICY;
        p->new_pin.limit=BW_DERIVED_TERMINAL_LIMIT;p->new_pin.mode=0600;
    }
    if(!bad){stage(p,UP_NEW_SELECTION);bad=s->prepared_policy==BW_BATCH_POLICY?
        bw_native_launch_batch_snapshot(r->identity,&batch,s->run,&selected):
        bw_native_launch_selection(r->data_root,r->identity,&derived,s->run,&selected);}
    bw_native_derived_snapshot_destroy(&snapshot);bw_native_batch_snapshot_destroy(&batch);if(bad)return -1;
    char text[BW_READER_SELECTION_BYTES+1];size_t size=0;
    if(bw_reader_selection_serialize(&selected,text,sizeof text,&size)||size!=p->selected_size||memcmp(text,p->selected,size)||current(p))return -1;
    /* Select the actually registered same-original preparation. Failed or
     * foreign candidates never grant a general source switch. */
    if(s->prepared_policy==BW_BATCH_POLICY){if(qualify_prior_batch(p)&&qualify_prior(p,BW_DERIVED_PLAN_POLICY)&&qualify_prior(p,0))return -1;}
    else if(qualify_prior(p,0))return -1;
    int b=snprintf(p->new_root,sizeof p->new_root,"%s/games/%s",r->data_root,s->prepared_game);
    if(b<0||(size_t)b>=sizeof p->new_root||!strcmp(p->old_root,p->new_root))return -1;
    if(s->prepared_policy==BW_BATCH_POLICY){
        if(bw_native_batch_record_path_exact(r->data_root,r->identity,s->prepared_game,p->new_pin.hash,p->new_pin.path,sizeof p->new_pin.path))return -1;
    }else{
        int d=snprintf(p->new_pin.path,sizeof p->new_pin.path,"%s/native-derived-prepared/%s/prepared.bin",r->data_root,s->game);
        if(d<0||(size_t)d>=sizeof p->new_pin.path)return -1;
    }
    stage(p,UP_PINS);if(pin_current(&p->old_pin)||pin_current(&p->new_pin))return -1;p->pins=1;
    stage(p,UP_REGISTRATION);
    return current(p)||registration(p)!=REG_OLD?-1:0;
}
static int execute(struct upgrade *p){
    struct bw_native_owned_registration_result *out=p->result;struct bw_sm_reply reply={0};unsigned old=0,next=0;
    out->message="The owned registration upgrade API is unavailable or incompatible.";
    if(post(p,BW_SM_VERSION,p->new_root,&reply)||reply.capabilities!=7||!reply.remove_supported)return -1;
    out->message="The exact old and new manual source entries could not be checked.";
    if(listed(p,&old,&next)||mutation_allowed(p))return -1;unsigned new_at_entry=next;
    progress(p,"Upgrading this owned game's registration to its verified startup folder.");
    if(old){
        if(mutation_allowed(p))return -1;out->remove_attempted=1;
        if(!post(p,BW_SM_MANUAL_REMOVE,p->old_root,&reply)&&!reply.present)out->remove_confirmed=1;
        out->message="The old manual source exclusion is unconfirmed. No removal was repeated.";
        if(listed(p,&old,&next)||old)return -1;
    }
    if(!next){
        if(mutation_allowed(p))return -1;out->add_attempted=1;
        if(!post(p,BW_SM_MANUAL_ADD,p->new_root,&reply)&&reply.present)out->add_confirmed=1;
        out->message="The new manual source retention is unconfirmed. No add was repeated.";
        if(listed(p,&old,&next)||old||!next)return -1;
    }
    enum registration state=registration(p);if(state==REG_BAD)return -1;
    if(state!=REG_NEW&&!out->add_attempted&&new_at_entry){
        if(state!=REG_OLD&&state!=REG_WAIT)return -1;if(current(p))return -1;out->scan_attempted=1;
        if(!post(p,BW_SM_SCAN,p->new_root,&reply)&&reply.queued)out->scan_confirmed=1;
    }
    double matched=-1;
    while(!current(p)){
        out->message="The upgraded registration is not yet confirmed. Startup copies were kept; no mutation was repeated.";
        if(listed(p,&old,&next)||old||!next)return -1;state=registration(p);if(state==REG_BAD)return -1;
        if(state==REG_NEW){
            if(matched<0)matched=p->last_time;
            if(p->last_time-matched>=2){
                if(listed(p,&old,&next)||old||!next||registration(p)!=REG_NEW||current(p))return -1;
                out->switched=1;out->message="The exact owned startup folder is now retained and registered.";return 0;
            }
        }else matched=-1;
        struct timespec pause={.tv_sec=0,.tv_nsec=250000000};nanosleep(&pause,NULL);
    }
    return -1;
}
int bw_native_owned_registration(const struct bw_native_register_request *r,struct bw_native_owned_registration_result *out){
    if(!out)return -1;memset(out,0,sizeof *out);out->message="The owned registration upgrade request is invalid.";
    if(!r||!r->selected)return -1;
    if(!r->selected->prepared_policy){out->message="Legacy preparations use the unchanged registration path.";return 0;}
    if(!r->data_root||!r->apps||!r->database||!r->identity||!r->current||r->identity->kind!=BW_PROVIDER_FOLDER||r->identity->policy!=BW_PROVIDER_POLICY_V1)return -1;
    struct upgrade *p=calloc(1,sizeof *p);if(!p)return -1;p->request=r;p->result=out;p->identity=*r->identity;
    int result=-1;if(bw_reader_selection_serialize(r->selected,p->selected,sizeof p->selected,&p->selected_size)||
       (!bw_reader_selection_is_derived(r->selected)&&!bw_reader_selection_is_batch(r->selected)))goto done;
    p->last_time=upgrade_now();p->deadline=p->last_time+300;if(p->last_time<0){p->failure=UP_CLOCK;goto done;}if(current(p))goto done;
    stage(p,UP_STATE);
    enum bw_registration_state state=bw_native_registration_state(r->data_root,r->apps,r->database,r->selected);
    if(current(p))goto done;
    if(state==BW_REG_MATCH||state==BW_REG_ABSENT||state==BW_REG_PENDING){out->message="No owned legacy registration conflict needs an upgrade.";result=0;goto done;}
    stage(p,UP_PRIOR_LOOKUP);
    if(state!=BW_REG_CONFLICT||qualify(p)||mutation_budget(p))goto done;result=execute(p);
done:
    if(result&&(out->remove_attempted||out->add_attempted||out->scan_attempted))out->unconfirmed=1;
    if(result){
        if(p->failure==UP_EXPIRED)out->message=p->mutation_budget?
            "The owned registration change exceeded its 60-second budget. No mutation was repeated; the retained state needs confirmation.":qualification_timeout(p->stage);
        else if(p->failure==UP_CLOCK)out->message="The owned registration clock was unavailable or regressed. No further mutation was attempted.";
        else if(p->failure==UP_CANCELLED)out->message="The accepted launch guard changed during owned registration. No further mutation was attempted.";
        else if(p->failure==UP_CHANGED)out->message="The accepted source or launch selection changed during owned registration. No further mutation was attempted.";
        else if(p->failure==UP_PIN_CHANGED)out->message="An exact startup terminal pin changed during owned registration. No further mutation was attempted.";
    }
    free(p);return result;
}
