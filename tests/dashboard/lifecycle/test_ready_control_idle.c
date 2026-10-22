/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Actual backend CONTROL/default native setup parser + actual idle cache.
 * Only the Sony process-query transport and retained startup proof are modeled.
 * No game, registration, preparation or network operation is called. */
#define BW_NATIVE_BACKEND_HOST_TEST 1
#define BW_NATIVE_PRELOAD 1
#define BW_NATIVE_PLAYBACK 1
#include "native_backend.c"
#include "game_stop_identity.h"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <sys/stat.h>

static struct bw_native_backend owner;
static struct play_observer ctx;
static char root[256],log_path[300],config_path[300];
static unsigned cases,checks,legacy_calls,legacy_lines,cache_lines,queries,settings_reads,held_checks,custom_calls;
static unsigned fd_owned[8192],live_fds,close_fault,source_changed,custom_refuse,game_active,shift_on_line;
static double clock_shift;
static FILE *legacy_stream;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"case%u line%d: %s\n",cases,__LINE__,#x);exit(1);}}while(0)
int rc_actual_setup_read(const char *,struct bw_share_config *,struct bw_setup_generation *);
int bw_native_setup_read(const char *p,struct bw_share_config *c,struct bw_setup_generation *g){settings_reads++;return rc_actual_setup_read(p,c,g);}
void bw_native_sensitive_clear(void *p,size_t n){volatile unsigned char *b=p;while(n--)*b++=0;}
int __real_open(const char *,int,...);int __real_dup(int);int __real_close(int);int __real_fclose(FILE *);
FILE *__real_fopen(const char *,const char *);char *__real_fgets(char *,int,FILE *);int __real_clock_gettime(clockid_t,struct timespec *);
static void take_fd(int fd){CHECK(fd>=0&&(unsigned)fd<8192&&!fd_owned[fd]);fd_owned[fd]=1;live_fds++;}
static void drop_fd(int fd){CHECK(fd>=0&&(unsigned)fd<8192&&fd_owned[fd]&&live_fds);fd_owned[fd]=0;live_fds--;}
int __wrap_open(const char *p,int flags,...){mode_t mode=0;if(flags&O_CREAT){va_list a;va_start(a,flags);mode=(mode_t)va_arg(a,int);va_end(a);}int fd=__real_open(p,flags,mode);if(fd>=0&&!strcmp(p,log_path))take_fd(fd);return fd;}
int __wrap_dup(int fd){int n=__real_dup(fd);if(n>=0&&(unsigned)fd<8192&&fd_owned[fd])take_fd(n);return n;}
int __wrap_close(int fd){int tracked=fd>=0&&(unsigned)fd<8192&&fd_owned[fd];if(tracked)drop_fd(fd);int rc=__real_close(fd);if(tracked&&close_fault){close_fault=0;errno=EIO;return -1;}return rc;}
FILE *__wrap_fopen(const char *p,const char *mode){
 if(!strcmp(p,"/data/shadowmount/debug.log")){CHECK(!legacy_stream);legacy_calls++;legacy_stream=__real_fopen(log_path,mode);return legacy_stream;}
 return __real_fopen(p,mode);
}
int __wrap_fclose(FILE *f){int fd=fileno(f);if(f==legacy_stream)legacy_stream=NULL;else if(fd>=0&&(unsigned)fd<8192&&fd_owned[fd])drop_fd(fd);return __real_fclose(f);}
char *__wrap_fgets(char *s,int n,FILE *f){char *r=__real_fgets(s,n,f);if(r){if(f==legacy_stream)legacy_lines++;else{cache_lines++;if(shift_on_line){clock_shift=25;shift_on_line=0;}}}return r;}
int __wrap_clock_gettime(clockid_t id,struct timespec *t){int r=__real_clock_gettime(id,t);if(!r&&id==CLOCK_MONOTONIC){long long ns=(long long)(clock_shift*1e9);t->tv_sec+=ns/1000000000;t->tv_nsec+=ns%1000000000;if(t->tv_nsec>=1000000000){t->tv_sec++;t->tv_nsec-=1000000000;}}return r;}
static void put32(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(i*8));}
static void put64(unsigned char *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(unsigned char)(n>>(i*8));}
static uint32_t firmware(void *p){(void)p;return BW_STOP_PROFILE_FW;}
static struct bw_src_process_result query(void *p,void *buffer,size_t size){
 (void)p;queries++;struct bw_src_process_result r={.used=3*BW_STOP_PROFILE_RECORD};
 if(buffer){CHECK(size>=r.used);memset(buffer,0,r.used);for(unsigned i=0;i<3;i++){
  unsigned char *b=(unsigned char *)buffer+i*BW_STOP_PROFILE_RECORD;put32(b,BW_STOP_PROFILE_RECORD);put32(b+72,i==2?5:i);put64(b+336,i==2?123:0);
  strcpy((char *)b+447,i==2?(game_active?"eboot.bin":"payload.elf"):i?"init":"kernel");
 }}return r;
}
int bw_startup_idle_ps5(void *p){static const struct bw_src_idle_ops ops={firmware,query,NULL};return bw_src_idle_evaluate(p,&ops);}
int bw_startup_idle_ps5_cached(const struct bw_startup_idle_request *q,struct bw_idle_log_cache *cache,const char operation[33]){
 static const struct bw_src_idle_ops ops={firmware,query,NULL};CHECK(q->deadline<=now()+2.01);return bw_src_idle_evaluate_cached(q,&ops,cache,operation);
}
int bw_native_launch_admission_current(struct bw_native_launch_admission *p,const char *base,const struct bw_provider_binding *id,const struct bw_reader_selection *s,struct bw_native_prepared *out){
 CHECK(p==ctx.context.startup&&!strcmp(base,root)&&id==&ctx.context.captured.identity&&s==&ctx.context.reader_selection&&!out);held_checks++;
 /* Same two na_guard -> Play current checkpoints as the retained proof.
  * Actual retained-file verification is an explicit modeled boundary here. */
 return play_current_adapter(&ctx)||source_changed||play_current_adapter(&ctx)?-1:0;
}
static int custom_idle(void *p){CHECK(p==&owner&&!pthread_mutex_trylock(&owner.lock));CHECK(!pthread_mutex_unlock(&owner.lock));custom_calls++;return custom_refuse?-1:0;}
static void observe(void *p,const struct bw_reader_event *e){CHECK(p==&owner&&e&&e->phase);}
static void write_log(const char *extra){FILE *f=__real_fopen(log_path,"w");CHECK(f);for(unsigned i=0;i<500;i++)CHECK(fputs("ordinary complete line\n",f)>=0);if(extra)CHECK(fputs(extra,f)>=0);CHECK(!__real_fclose(f));}
static void write_config(char value){FILE *f=__real_fopen(config_path,"w");CHECK(f);CHECK(fprintf(f,"server=127.0.0.1\nshare=fixture\nuser=Blackwire\npassword=%c\npath=\n",value)>0);CHECK(!__real_fclose(f));CHECK(!chmod(config_path,0600));}
static void reset(void){
 CHECK(!live_fds&&!legacy_stream);clock_shift=0;close_fault=source_changed=custom_refuse=game_active=shift_on_line=0;
 memset(&owner,0,sizeof owner);memset(&ctx,0,sizeof ctx);CHECK(!pthread_mutex_init(&owner.lock,NULL));
 owner.active=1;owner.worker_kind=WORK_PLAY;owner.generation=4;owner.play_epoch=8;owner.settings_revision=2;
 owner.ops.read_settings=read_settings;owner.ops.idle=bw_native_setup_idle;owner.opaque=&owner;
 strcpy(owner.external_nonce,"cccccccccccccccccccccccccccccccc");strcpy(owner.playing.selection.run,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
 backend_data_root=root;backend_game_log=log_path;write_config('a');write_log(NULL);
 struct bw_share_config c={0};CHECK(bw_native_setup_read(root,&c,&owner.playing.selection.connection)==BW_SETUP_VALID);bw_share_clear(&c);
 ctx.context=(struct play_context){.current=play_current,.guard=&owner,.owner=&owner,.selected=&owner.playing.selection,.captured=owner.playing.selection,.generation=4,.epoch=8,.revision=2,.startup=(void *)(uintptr_t)0x1234};
 memcpy(ctx.context.reservation,owner.external_nonce,33);ctx.observe=observe;ctx.observer=&owner;
 legacy_calls=legacy_lines=cache_lines=queries=settings_reads=held_checks=custom_calls=0;
}
static void begin(void){CHECK(!play_control_log_begin(&ctx,now()+20,play_data_adapter));}
static int call(void){return play_registration_control(&ctx,BW_NRL_REGISTER);}
static void end(void){CHECK(!play_control_log_close(&ctx));CHECK(!live_fds&&!legacy_stream);CHECK(!pthread_mutex_destroy(&owner.lock));cases++;}
static void retire_setup(void){
 ctx.context.startup=NULL;ctx.context.startup_closed=1;ctx.context.retirement_deadline=now()+30;ctx.context.retirement_cache_scope=1;
 memcpy(ctx.context.release.run,ctx.context.captured.run,33);ctx.context.release.runtime_released=ctx.context.release.io_released=ctx.context.release.private_released=1;
 owner.playing.retiring=1;
}
static int retire(void){return play_registration_control(&ctx,BW_NRL_RETIRE);}
static void cost(void){
 reset();begin();for(unsigned i=0;i<40;i++)CHECK(!call());
 CHECK(queries==160&&settings_reads==160&&held_checks==40&&cache_lines==500);
#ifdef EXPECT_BASELINE
 CHECK(legacy_calls==40&&legacy_lines==20000);
#else
 CHECK(!legacy_calls&&!legacy_lines);
#endif
 printf("{\"case\":\"default-native-40-controls\",\"legacyLogOpens\":%u,\"legacyParsedLines\":%u,\"cacheParsedLines\":%u,\"processQueryCalls\":%u,\"settingsReads\":%u,\"heldProofChecks\":%u}\n",legacy_calls,legacy_lines,cache_lines,queries,settings_reads,held_checks);end();
}
int main(void){
 strcpy(root,"/tmp/blackwire-ready-idle-XXXXXX");CHECK(mkdtemp(root));CHECK(snprintf(log_path,sizeof log_path,"%s/lifecycle.log",root)>0);CHECK(snprintf(config_path,sizeof config_path,"%s/share.conf",root)>0);
 cost();
#ifndef EXPECT_BASELINE
 reset();owner.ops.idle=custom_idle;begin();for(unsigned i=0;i<40;i++)CHECK(!call());CHECK(custom_calls==40&&queries==160&&held_checks==40&&settings_reads==160);end();
 reset();CHECK(!call()&&legacy_calls==1&&legacy_lines==500&&cache_lines==1000&&queries==4&&settings_reads==4);end();
 reset();owner.ops.idle=custom_idle;custom_refuse=1;begin();CHECK(call()<0&&custom_calls==1&&!queries&&!held_checks);end();
 reset();begin();CHECK(!call());write_log("[GAME] started: PPSA01234 pid=24\n");CHECK(call()<0&&held_checks==1&&!legacy_calls);end();
 reset();begin();CHECK(!call());write_log("[GAME] started: PPSA01234 pid=invalid\n");CHECK(call()<0&&held_checks==1&&!legacy_calls);end();
 reset();begin();CHECK(!call());game_active=1;CHECK(call()<0&&held_checks==1&&!legacy_calls);game_active=0;end();
 reset();begin();CHECK(!call());write_config('b');CHECK(call()<0&&owner.playing.changed&&held_checks==1&&!legacy_calls);end();
 reset();begin();CHECK(!call());owner.playing.close_requested=1;CHECK(call()<0&&held_checks==1&&!legacy_calls);end();
 reset();begin();CHECK(!call());owner.generation++;CHECK(call()<0&&held_checks==1&&!legacy_calls);end();
 reset();begin();CHECK(!call());source_changed=1;CHECK(call()<0&&held_checks==2&&!legacy_calls);end();
 reset();begin();CHECK(!call());clock_shift=21;CHECK(call()<0&&held_checks==1&&!legacy_calls);clock_shift=0;end();
 reset();begin();shift_on_line=1;CHECK(call()<0&&!held_checks&&!legacy_calls&&cache_lines==1);clock_shift=0;end();
 reset();begin();CHECK(!call());close_fault=1;CHECK(play_control_log_close(&ctx)<0&&owner.control_release_unknown&&!live_fds&&!ctx.context.control_log);CHECK(!pthread_mutex_destroy(&owner.lock));cases++;
 reset();retire_setup();for(unsigned i=0;i<40;i++)CHECK(!retire());CHECK(!legacy_calls&&queries==160&&settings_reads==40&&!held_checks&&cache_lines==500);end();
 reset();retire_setup();owner.ops.idle=custom_idle;for(unsigned i=0;i<4;i++)CHECK(!retire());CHECK(custom_calls==4&&queries==16&&settings_reads==4);end();
 reset();retire_setup();owner.external_nonce[0]='f';CHECK(retire()<0&&!ctx.context.control_log&&!queries&&!legacy_calls);end();
#endif
 CHECK(!unlink(config_path)&&!unlink(log_path)&&!rmdir(root));printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualBackendControl\":true,\"actualDefaultSetupParser\":true,\"actualSettingsFiles\":true,\"actualCacheAndProcessRecordParser\":true,\"consoleActions\":0,\"nativeTimingClaim\":false}\n",cases,checks);return 0;
}
