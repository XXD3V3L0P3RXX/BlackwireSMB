#include "settings-startup-under-test.h"
#include <stdio.h>
static int mode,reads,queued,fail_spawn;
int bw_native_setup_generation_equal(const struct bw_setup_generation *a,const struct bw_setup_generation *b){return !memcmp(a,b,sizeof *a);}
static int fixture_read(void *owner,struct bw_share_config *c,struct bw_setup_generation *g){
    assert(owner);reads++;memset(c,0,sizeof *c);memset(g,0,sizeof *g);
    if(mode==BW_SETUP_VALID){strcpy(c->server,"192.0.2.10");strcpy(c->share,"FixtureLibrary");strcpy(c->user,"project-account");strcpy(c->password,"fixture-password");g->present=1;}
    return mode;
}
static int worker_start(pthread_t *t,void *(*entry)(void *),void *opaque){
    (void)t;struct bw_native_backend *p=opaque;assert(entry==scan&&p->active);queued++;return fail_spawn?-1:0;
}
static void init(struct bw_native_backend *p,int status){memset(p,0,sizeof *p);assert(!pthread_mutex_init(&p->lock,NULL));p->settings_enabled=1;p->ops.read_settings=fixture_read;p->opaque=p;mode=status;reads=queued=fail_spawn=0;}
static void done(struct bw_native_backend *p){assert(!pthread_mutex_destroy(&p->lock));}
int main(void){
    struct bw_native_backend p;unsigned cases=0;
    init(&p,BW_SETUP_VALID);assert(!refresh_unlocked(&p));
    assert(reads==1&&p.settings_known&&p.settings_status==BW_SETUP_VALID&&"Saved settings must be known before slow scan starts");
    assert(queued==1&&p.active&&!strcmp(p.server,"192.0.2.10")&&!strcmp(p.share,"FixtureLibrary")&&p.password_saved);done(&p);cases++;
    init(&p,BW_SETUP_MISSING);assert(!refresh_unlocked(&p));assert(reads==1&&p.settings_known&&p.settings_status==BW_SETUP_MISSING&&!p.active&&!queued&&p.phase==BACKEND_NEEDS_SETUP);done(&p);cases++;
    init(&p,BW_SETUP_INVALID);assert(!refresh_unlocked(&p));assert(p.settings_known&&p.settings_status==BW_SETUP_INVALID&&!p.active&&!queued&&p.phase==BACKEND_NEEDS_SETUP);done(&p);cases++;
    init(&p,BW_SETUP_UNSAFE);assert(!refresh_unlocked(&p));assert(p.settings_known&&p.settings_status==BW_SETUP_UNSAFE&&!p.active&&!queued&&p.phase==BACKEND_FAILED);done(&p);cases++;
    init(&p,BW_SETUP_VALID);p.active=1;assert(refresh_unlocked(&p)==1&&!reads&&!queued);done(&p);cases++;
    init(&p,BW_SETUP_VALID);p.settings_enabled=0;assert(!refresh_unlocked(&p));assert(!reads&&!p.settings_known&&queued==1&&p.active);done(&p);cases++;
    init(&p,BW_SETUP_VALID);fail_spawn=1;assert(refresh_unlocked(&p)==-1);assert(p.settings_known&&!p.active&&p.phase==BACKEND_FAILED&&!strcmp(p.share,"FixtureLibrary"));done(&p);cases++;
    printf("{\"passed\":true,\"cases\":%u,\"actualStartupFunctions\":true,\"scanWorkerHeld\":true,\"credentialsPrinted\":false,\"consoleContacted\":false}\n",cases);return 0;
}
