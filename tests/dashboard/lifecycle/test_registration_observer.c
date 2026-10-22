/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "registration_observer.c"
#include <assert.h>
static unsigned checks,cases;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"observer line%d %s\n",__LINE__,#x);exit(1);}}while(0)
struct fixture {uint64_t now;unsigned busy,idle,read_bad,nonce_bad,reserve_bad,release_bad,clock_bad;int observed;const char *body;unsigned reads,reserves,observes,releases,reports;char owner[33];};
static int clock_now(void *o,uint64_t *n){struct fixture *f=o;*n=f->now;return f->clock_bad?-1:0;}
static int busy(void *o){return (int)((struct fixture *)o)->busy;}
static int idle(void *o){return (int)((struct fixture *)o)->idle;}
static int read_state(void *o,struct bw_native_response *r){struct fixture *f=o;f->reads++;r->status=f->read_bad?409:200;r->bytes=strdup(f->body);r->size=strlen(f->body);return 0;}
static int nonce(void *o,char n[33]){struct fixture *f=o;strcpy(n,f->nonce_bad?"bad":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");return 0;}
static int reserve(void *o,const char n[33],const char t[33]){struct fixture *f=o;f->reserves++;CHECK(!strcmp(n,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb")&&!strcmp(t,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")&&!f->owner[0]);if(f->reserve_bad)return -1;strcpy(f->owner,n);return 0;}
static int observe(void *o,const char n[33],struct bw_native_cleanup_observation *s){struct fixture *f=o;f->observes++;CHECK(!strcmp(n,f->owner));s->pending=f->observed!=1;return f->observed;}
static int release(void *o,const char n[33]){struct fixture *f=o;f->releases++;CHECK(!strcmp(n,f->owner));if(f->release_bad)return -1;memset(f->owner,0,sizeof f->owner);return 0;}
static void report(void *o,const char *s){struct fixture *f=o;f->reports++;CHECK(strstr(s,"hardware unverified")&&strstr(s,"retained")&&!strstr(s,"aaaaaaaa")&&!strstr(s,"bbbbbbbb"));}
static const char *valid="{\"library\":{},\"setup\":{\"token\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}";
static struct bd_registration_observer *make(struct fixture *f){cases++;*f=(struct fixture){.now=100,.body=valid,.observed=1};struct bd_registration_observer_ops ops={f,clock_now,busy,idle,read_state,nonce,reserve,observe,release,report};struct bd_registration_observer *r=bd_registration_observer_create(NULL,&ops);CHECK(r);return r;}
/* Any accidental default/native invocation fails the focused host fixture. */
int bw_native_backend_busy(struct bw_native_backend *p){(void)p;abort();}
int bw_native_setup_idle(void *o){(void)o;abort();}
int bw_startup_idle_ps5(void *o){(void)o;abort();}
int bw_native_setup_nonce(char n[33]){(void)n;abort();}
int bw_native_backend_handle(struct bw_native_backend *p,const char *a,const char *b,const void *c,size_t d,struct bw_native_response *e){(void)p;(void)a;(void)b;(void)c;(void)d;(void)e;abort();}
int bw_native_backend_external_reserve(struct bw_native_backend *p,const char n[33],const char t[33]){(void)p;(void)n;(void)t;abort();}
int bw_native_backend_external_release(struct bw_native_backend *p,const char n[33]){(void)p;(void)n;abort();}
int bw_native_backend_registration_observe(struct bw_native_backend *p,const char n[33],struct bw_native_cleanup_observation *s){(void)p;(void)n;(void)s;abort();}
void bw_native_response_clear(struct bw_native_response *r){free(r->bytes);memset(r,0,sizeof *r);}
int main(void){
    struct fixture f;struct bd_registration_observer *r=make(&f);
    CHECK(bd_registration_observer_tick(r)==1&&f.reserves==1&&f.observes==1&&f.releases==1&&!f.owner[0]);
    CHECK(bd_registration_observer_tick(r)==1&&f.reads==1);f.now+=4999;CHECK(bd_registration_observer_tick(r)==1&&f.reads==1);f.now++;CHECK(bd_registration_observer_tick(r)==1&&f.observes==2);CHECK(!bd_registration_observer_destroy(r));
    const char *bad[]={"{}","{\"setup\":{}}","{\"setup\":{\"token\":\"bad\"}}","{\"setup\":{\"token\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\",\"token\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}","{\"setup\":{\"token\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"},\"setup\":{\"token\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}","{\"setup\":{\"token\":\"00000000000000000000000000000000\"}}","{\"setup\":{\"token\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}}garbage"};
    for(unsigned i=0;i<sizeof bad/sizeof bad[0];i++){r=make(&f);f.body=bad[i];CHECK(bd_registration_observer_tick(r)<0&&!f.reserves&&!f.observes&&!f.releases);CHECK(!bd_registration_observer_destroy(r));}
    for(unsigned i=0;i<6;i++){r=make(&f);if(i==0)f.busy=1;if(i==1)f.idle=1;if(i==2)f.read_bad=1;if(i==3)f.nonce_bad=1;if(i==4)f.reserve_bad=1;if(i==5)f.clock_bad=1;
        CHECK(bd_registration_observer_tick(r)!=0&&!f.observes&&!f.releases);CHECK(!bd_registration_observer_destroy(r));}
    r=make(&f);f.observed=-1;CHECK(bd_registration_observer_tick(r)<0&&f.observes==1&&f.releases==1&&!f.owner[0]);CHECK(!bd_registration_observer_destroy(r));
    r=make(&f);f.release_bad=1;CHECK(bd_registration_observer_tick(r)<0&&f.observes==1&&f.reports==1&&f.owner[0]);CHECK(bd_registration_observer_destroy(r)==1);
    f.now+=5000;CHECK(bd_registration_observer_tick(r)<0&&f.observes==1&&f.reserves==1&&f.reports==1&&f.releases==2);
    f.release_bad=0;f.now+=5000;CHECK(!bd_registration_observer_tick(r)&&f.observes==1&&f.reserves==1&&!f.owner[0]);CHECK(!bd_registration_observer_destroy(r));
    r=make(&f);CHECK(bd_registration_observer_tick(r)==1);f.now--;CHECK(bd_registration_observer_tick(r)<0&&f.reserves==1);CHECK(!bd_registration_observer_destroy(r));
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"mutationApiCalls\":0}\n",cases,checks);return 0;
}
