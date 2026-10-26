/* Transport/ownership tests only. The envelope below deliberately models
 * framing, not source authority; the producer-to-DATA suite checks that. */
#define main legacy_home_main
#include "home_test.c"
#undef main
#include "reader_projection.h"
#include "preload_launch.h"

static void projected(struct fixture *f) {
    f->selection.prepared_policy=3;
    strcpy(f->selection.prepared_game,f->selection.game);
    memset(f->selection.provenance_hex,'c',64);
}
static void *envelope(size_t size) {
    unsigned char *p=calloc(1,size);C(p);memcpy(p,"BWDAT001",8);return p;
}
static void projection_path(struct fixture *f,char out[1400]) {
    path(out,1400,f->lib,BW_PRELOAD_HANDOFF_PROJECTION);
}
static void all_absent(struct fixture *f,const char *projection) {
    C(access(f->share,F_OK)&&errno==ENOENT);
    C(access(f->selected,F_OK)&&errno==ENOENT);
    C(access(projection,F_OK)&&errno==ENOENT);
    C(!access(f->sentinel,F_OK));
}
static void projection_tests(const char *base) {
    struct fixture f;char p[1400];struct stat st;
    init(&f,base);projected(&f);projection_path(&f,p);
    size_t size=128*1024+3;void *bytes=envelope(size);
    C(!bw_home_session_publish_projection(&f.owner,f.root,&f.selection,&f.config,&bytes,size));
    C(!bytes&&f.owner.publication_complete);
    C(f.owner.private_inputs.projected&&f.owner.private_inputs.projection.descriptor_held);
    C(f.owner.private_inputs.projection.ownership_confirmed);
    C(!stat(p,&st)&&(st.st_mode&07777)==0600&&st.st_uid==geteuid()&&st.st_size==(off_t)size);
    int fd=open(p,O_RDONLY);C(fd>=0);char magic[8];C(read(fd,magic,8)==8&&!memcmp(magic,"BWDAT001",8));C(!close(fd));
    release(&f);all_absent(&f,p);
    C(f.owner.private_inputs.projection.removed&&f.owner.private_inputs.projection.close_attempted);
    C(f.owner.private_inputs.descriptors_closed);

    /* Ordinary publication cannot silently accept v8 without its evidence. */
    init(&f,base);projected(&f);projection_path(&f,p);
    C(bw_home_session_publish(&f.owner,f.root,&f.selection,&f.config));
    release(&f);all_absent(&f,p);

    /* Explicit projection publication cannot attach evidence to legacy v4. */
    init(&f,base);projection_path(&f,p);bytes=envelope(64);
    C(bw_home_session_publish_projection(&f.owner,f.root,&f.selection,&f.config,&bytes,64));
    C(!bytes);release(&f);all_absent(&f,p);

    init(&f,base);projected(&f);projection_path(&f,p);bytes=envelope(64);
    ((char*)bytes)[0]='X';C(bw_home_session_publish_projection(&f.owner,f.root,&f.selection,&f.config,&bytes,64));
    C(!bytes);release(&f);all_absent(&f,p);

    /* A reserved file belonging to another attempt is never overwritten. */
    init(&f,base);projected(&f);projection_path(&f,p);write_file(p,"foreign",7,0600);bytes=envelope(64);
    C(bw_home_session_publish_projection(&f.owner,f.root,&f.selection,&f.config,&bytes,64));
    C(!bytes);release(&f);C(!access(p,F_OK));C(access(f.share,F_OK)&&errno==ENOENT);C(access(f.selected,F_OK)&&errno==ENOENT);

    init(&f,base);projection_path(&f,p);write_file(p,"foreign",7,0600);
    C(bw_home_session_publish(&f.owner,f.root,&f.selection,&f.config));release(&f);C(!access(p,F_OK));

    /* Changing the third input refuses cleanup BEFORE either other unlink. */
    init(&f,base);projected(&f);projection_path(&f,p);bytes=envelope(64);
    C(!bw_home_session_publish_projection(&f.owner,f.root,&f.selection,&f.config,&bytes,64));C(!bytes);
    fd=open(p,O_WRONLY);C(fd>=0);C(pwrite(fd,"X",1,12)==1);C(!close(fd));
    f.present=0;C(bw_home_session_release(&f.owner,stamp()+20));
    C(!access(p,F_OK)&&!access(f.share,F_OK)&&!access(f.selected,F_OK));
    C(f.owner.private_inputs.descriptors_closed&&!f.owner.private_inputs.released);
    C(!f.owner.private_inputs.projection.removed&&!f.owner.private_inputs.selection.removed&&!f.owner.private_inputs.share.removed);
    C(bw_home_session_release(&f.owner,stamp()+20));

    /* Invalid early arguments still consume the caller's temporary bytes. */
    cases++;bytes=envelope(64);C(bw_home_session_publish_projection(NULL,NULL,NULL,NULL,&bytes,64));C(!bytes);
}
struct launch_fixture {
    struct fixture files;void **temporary;unsigned launches,discovered,assignments,events;
    int bundle_result,launch_result,cancel_after_launch;
};
static double launch_now(void *raw){(void)raw;return stamp();}
static int launch_pause(void *raw,unsigned delay){(void)raw;(void)delay;C(0);return -1;}
static int launch_current(void *raw){struct launch_fixture *f=raw;return f->cancel_after_launch&&f->launches?-1:0;}
static int launch_bundle(void *raw,const char *root){struct launch_fixture *f=raw;C(!strcmp(root,f->files.root));return f->bundle_result;}
static int launch_call(void *raw){struct launch_fixture *f=raw;C(!f->temporary||!*f->temporary);C(!*f->files.config.password&&!*f->files.config.user);C(++f->launches==1);return f->launch_result;}
static int32_t launch_discover(void *raw){struct launch_fixture *f=raw;f->discovered++;return 500;}
static int launch_uid(void *raw,int32_t pid,uid_t *out){struct launch_fixture *f=raw;C(pid==500);f->assignments++;*out=geteuid();return 0;}
static int launch_live(void *raw,int32_t pid){(void)raw;C(pid==500);return 0;}
static int launch_exited(void *raw){(void)raw;return 0;}
static void launch_event(void *raw,const char *phase,const char *message,int32_t pid){struct launch_fixture *f=raw;C(phase&&message&&pid>=0);f->events++;}
static void launch_tests(const char *base){
    for(unsigned scenario=0;scenario<7;scenario++){
        struct launch_fixture f={0};init(&f.files,base);char p[1400];projection_path(&f.files,p);
        int project=scenario!=1;if(project)projected(&f.files);
        void *temporary=project?envelope(65539):NULL;f.temporary=project?&temporary:NULL;
        f.launch_result=scenario==3?0:scenario==4?-1:1;f.bundle_result=scenario==2?-2:0;f.cancel_after_launch=scenario==5;
        struct bw_preload_launch owner={0};
        struct bw_preload_launch_request q={f.files.root,&f.files.selection,&f.files.config,
            {&f,launch_now,launch_pause,launch_current,launch_bundle,launch_call,launch_discover,launch_uid,launch_live,launch_exited,launch_event}};
        if(scenario==6)q.root=NULL;
        int result=project?bw_preload_launch_run_projection(&q,&owner,&temporary,65539):bw_preload_launch_run(&q,&owner);
        C(!temporary);C((result==0)==(scenario==0||scenario==1));
        if(scenario==2){C(!f.launches&&owner.bundle_uncertain&&owner.blocked);}
        else if(scenario==6)C(!f.launches&&!owner.handoff.initialized);
        else {C(f.launches==1&&owner.finished&&owner.handoff.released&&owner.handoff.descriptors_closed);}
        if(project&&f.launches)C(owner.handoff.projection.removed&&owner.handoff.projection.close_attempted);
        all_absent(&f.files,p);
    }
}
int main(int argc,char **argv) {
    C(argc==2);session_tests(argv[1]);projection_tests(argv[1]);launch_tests(argv[1]);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualHandoff\":true,\"actualHomeSession\":true,\"actualLaunchController\":true,\"sourceAuthorityProved\":false,\"nativeUidProved\":false}\n",cases,checks);return 0;
}
