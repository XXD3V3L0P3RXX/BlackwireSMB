/* The actual startup preflight coordinator; platform/clock effects modeled. */
#include "../../../src/blackwire/library/media_startup.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static unsigned cases,checks;
static void check(int good,int line){checks++;if(!good){fprintf(stderr,"startup coordinator case %u line %d\n",cases,line);abort();}}
#define CHECK(x) check(!!(x),__LINE__)
struct fixture {double at,step;unsigned attempts,waits,starts,busy;int terminal,wait_fail,start_fail,accept_clock;};
static double clock_now(void *opaque){return ((struct fixture *)opaque)->at;}
static int attempt(void *opaque){
    struct fixture *f=opaque;f->attempts++;
    if(f->terminal)return f->terminal;
    if(f->busy){f->busy--;return 1;}
    if(f->accept_clock)f->at=f->accept_clock<0?9:100;
    return 0;
}
static int wait_for_idle(void *opaque,unsigned milliseconds){
    struct fixture *f=opaque;CHECK(milliseconds==BW_MEDIA_STARTUP_POLL_MS);f->waits++;
    f->at+=f->step;return f->wait_fail?-1:0;
}
static int launch(void *opaque){struct fixture *f=opaque;f->starts++;return f->start_fail?-1:0;}
static int acquire(struct fixture *f,double deadline,const char **reason){
    struct bw_media_startup_ops ops={.attempt=attempt,.now=clock_now,.wait=wait_for_idle,.opaque=f};
    return bw_media_startup_acquire(&ops,deadline,reason);
}
static struct fixture fresh(void){cases++;return (struct fixture){.at=10,.step=.25};}
int main(void){
    const char *reason=NULL;struct fixture f=fresh();
    CHECK(!acquire(&f,100,&reason));CHECK(f.attempts==1&&!f.waits);CHECK(!strcmp(reason,"startup_idle_confirmed"));
    f=fresh();f.busy=3;CHECK(!acquire(&f,100,&reason));CHECK(f.attempts==4&&f.waits==3);CHECK(f.at==10.75);
    f=fresh();f.busy=999;CHECK(acquire(&f,11,&reason)==-1);CHECK(f.attempts==4&&f.waits==4);CHECK(!strcmp(reason,"startup_idle_wait_expired"));
    f=fresh();f.busy=999;f.step=0;CHECK(acquire(&f,100,&reason)==-1);CHECK(f.attempts==361&&f.waits==361);
    f=fresh();f.terminal=-1;CHECK(acquire(&f,100,&reason)==-1);CHECK(f.attempts==1&&!f.waits);CHECK(!strcmp(reason,"startup_preflight_refused"));
    f=fresh();f.terminal=-2;CHECK(acquire(&f,100,&reason)==-2);CHECK(f.attempts==1&&!f.waits);CHECK(!strcmp(reason,"startup_reservation_release_unconfirmed"));
    f=fresh();f.busy=1;f.wait_fail=1;CHECK(acquire(&f,100,&reason)==-1);CHECK(f.attempts==1&&f.waits==1);CHECK(!strcmp(reason,"startup_idle_wait_failed"));
    f=fresh();f.busy=1;f.step=-1;CHECK(acquire(&f,100,&reason)==-1);CHECK(f.attempts==1&&f.waits==1);CHECK(!strcmp(reason,"startup_clock_changed"));
    f=fresh();f.accept_clock=-1;CHECK(acquire(&f,100,&reason)==-1);CHECK(f.attempts==1&&!f.waits);CHECK(!strcmp(reason,"startup_clock_changed"));
    f=fresh();f.accept_clock=1;CHECK(acquire(&f,100,&reason)==-1);CHECK(f.attempts==1&&!f.waits);CHECK(!strcmp(reason,"startup_idle_wait_expired"));
    f=fresh();CHECK(acquire(&f,10,&reason)==-1);CHECK(!f.attempts&&!f.waits);
    f=fresh();f.at=NAN;CHECK(acquire(&f,100,&reason)==-1);CHECK(!f.attempts&&!f.waits);
    f=fresh();CHECK(acquire(&f,INFINITY,&reason)==-1);CHECK(!f.attempts&&!f.waits);
    f=fresh();unsigned requested=0;CHECK(!bw_media_startup_once(&requested,launch,&f));CHECK(requested&&f.starts==1);
    CHECK(bw_media_startup_once(&requested,launch,&f)==1);CHECK(f.starts==1);
    f=fresh();requested=0;f.start_fail=1;CHECK(bw_media_startup_once(&requested,launch,&f)==-1);CHECK(requested&&f.starts==1);
    CHECK(bw_media_startup_once(&requested,launch,&f)==1);CHECK(f.starts==1);
    f=fresh();CHECK(bw_media_startup_once(NULL,launch,&f)==-1);CHECK(bw_media_startup_once(&requested,NULL,&f)==-1);CHECK(!f.starts);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualCoordinator\":true,\"platformEffectsModeled\":true,\"consoleActions\":0}\n",cases,checks);
    return 0;
}
