/* SPDX-License-Identifier: GPL-3.0-or-later
 * Trusted startup token copy with the actual backend and scan worker.
 * Settings/discovery effects are local models; no SMB, SDK or console calls.
 */
#define main inherited_removal_main
#include "test_game_removal_backend.c"
#undef main

static unsigned settings_reads, discovers;
static int missing_settings(void *opaque,struct bw_share_config *config,struct bw_setup_generation *generation){
    struct removal_fixture *f=opaque;settings_reads++;memset(config,0,sizeof *config);*generation=f->settings;return BW_SETUP_MISSING;
}
static int local_discover(void *opaque,struct bw_share_config *config,struct bw_native_library *library){
    struct removal_fixture *f=opaque;CHECK(config&&library);discovers++;atomic_store(&f->entered,1);
    while(atomic_load(&f->block))pause_worker();memset(library,0,sizeof *library);return 0;
}
static void zero_output(const char out[33]){for(unsigned i=0;i<33;i++)CHECK(!out[i]);}
static void rejected(struct bw_native_backend *p,int expected){
    char out[33];memset(out,0x5a,sizeof out);CHECK(bw_native_backend_startup_token(p,out)==expected);zero_output(out);
}
static void copied(struct bw_native_backend *p){
    char out[33];memset(out,0x5a,sizeof out);CHECK(!bw_native_backend_startup_token(p,out));CHECK(!strcmp(out,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
    /* The returned copy owns no backend memory or idle authority. */
    out[0]='f';CHECK(p->setup_token[0]=='a'&&!p->external_nonce[0]);
    CHECK(!pthread_mutex_trylock(&p->lock));CHECK(!pthread_mutex_unlock(&p->lock));
}
int main(void){
    struct removal_fixture f;char token[33];const char *owner="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
    atomic_store(&checked,0);cases=0;

    /* A fresh missing share.conf is admitted after the real initial settings
     * refresh, without a configured share or discovery/network request. */
    fixture_init(&f);f.p->ops.read_settings=missing_settings;memset(&f.settings,0,sizeof f.settings);
    f.p->settings_known=0;f.p->settings_status=BW_SETUP_UNSAFE;settings_reads=discovers=0;
    rejected(f.p,1);CHECK(!settings_reads&&!discovers);CHECK(!bw_native_backend_refresh(f.p));
    CHECK(f.p->settings_known&&f.p->settings_status==BW_SETUP_MISSING&&!f.p->active&&settings_reads==1&&!discovers);
    copied(f.p);CHECK(settings_reads==1);CHECK(!bw_native_backend_startup_token(f.p,token));
    CHECK(!bw_native_backend_external_reserve(f.p,owner,token));rejected(f.p,1);
    CHECK(!bw_native_backend_external_current(f.p,owner));CHECK(!bw_native_backend_external_release_idle(f.p,owner));copied(f.p);fixture_close(&f);

    /* A real active scan owns admission until its joined worker completes. */
    fixture_init(&f);f.p->ops.discover=local_discover;f.p->ops.inspect=NULL;atomic_store(&f.block,1);
    CHECK(!bw_native_backend_refresh(f.p));while(!atomic_load(&f.entered))pause_worker();
    rejected(f.p,1);CHECK(f.p->active&&!f.p->external_nonce[0]);atomic_store(&f.block,0);wait_worker(&f);copied(f.p);fixture_close(&f);

    fixture_init(&f);reserve(&f,owner);rejected(f.p,1);CHECK(!strcmp(f.p->external_nonce,owner));
    CHECK(!bw_native_backend_external_release_idle(f.p,owner));copied(f.p);fixture_close(&f);

    fixture_init(&f);f.p->control_release_unknown=1;rejected(f.p,1);CHECK(f.p->control_release_unknown);
    f.p->control_release_unknown=0;copied(f.p);fixture_close(&f);

    fixture_init(&f);f.p->settings_known=0;rejected(f.p,1);f.p->settings_known=1;f.p->settings_status=BW_SETUP_UNSAFE;
    rejected(f.p,1);f.p->settings_status=BW_SETUP_INVALID;copied(f.p);fixture_close(&f);

    fixture_init(&f);f.p->settings_enabled=0;rejected(f.p,-1);f.p->settings_enabled=1;
    memset(f.p->setup_token,'0',32);f.p->setup_token[32]=0;rejected(f.p,-1);fixture_close(&f);

    fixture_init(&f);f.p->setup_token[0]='x';rejected(f.p,-1);fixture_close(&f);

    cases++;rejected(NULL,-1);CHECK(bw_native_backend_startup_token(NULL,NULL)==-1);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualBackendAccessor\":true,\"actualMissingSettingsRefresh\":true,\"actualScanWorker\":true,\"noSmbConnectionRequired\":true,\"settingsAndDiscoveryModeled\":true,\"consoleActions\":0}\n",cases,atomic_load(&checked));return 0;
}
