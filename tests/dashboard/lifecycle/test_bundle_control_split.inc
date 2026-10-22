/* Included after the retained actual backend/settings/cache fixture. */
#include "under-test.c"
extern const uint8_t fixture_previous[];
extern const size_t fixture_previous_size;
static unsigned full_calls,lease_calls,fail_full_at,inject_at;
static int inject_kind;
static double full_cost;
static char game_path[2048],state_path[2048],prepared_game[75];
static uint8_t prepared_sha[32];
static void local_write(const char *p,const void *b,size_t n,mode_t mode){
 int fd=__real_open(p,O_WRONLY|O_CREAT|O_EXCL,mode);CHECK(fd>=0&&!fchmod(fd,mode));CHECK(write(fd,b,n)==(ssize_t)n);CHECK(!__real_close(fd));
}
static void join(char out[2048],const char *a,const char *b){int n=snprintf(out,2048,"%s/%s",a,b);CHECK(n>0&&n<2048);}
static void local_copy(const char *a,const char *b){
 int fd=__real_open(a,O_RDONLY);struct stat s;CHECK(fd>=0&&!fstat(fd,&s)&&s.st_size>0);uint8_t *p=malloc((size_t)s.st_size);CHECK(p&&pread(fd,p,(size_t)s.st_size,0)==s.st_size&&!__real_close(fd));local_write(b,p,(size_t)s.st_size,0755);free(p);
}
static int full_control(void *opaque){
 CHECK(opaque==&ctx);full_calls++;clock_shift+=full_cost;
 if(fail_full_at&&full_calls==fail_full_at)return -1;
 return play_registration_bundle_current(&ctx);
}
static int fast_lease(void *opaque){
 CHECK(opaque==&ctx);lease_calls++;
 if(inject_at&&lease_calls==inject_at){
  if(inject_kind==1)write_config('b');
  else if(inject_kind==2)source_changed=1;
  else if(inject_kind==3)write_log("[GAME] started: PPSA01234 pid=24\n");
  else if(inject_kind==4)owner.playing.close_requested=1;
  else if(inject_kind==5)clock_shift=21;
 }
 return play_data_adapter(&ctx);
}
static struct bw_preload_bundle_upgrade_request setup_bundle(unsigned existing){
 reset();full_calls=lease_calls=fail_full_at=inject_at=0;inject_kind=0;full_cost=0;
 char games[2048],states[2048],name[2048],folder[2048];join(games,root,"games");join(states,root,"native-prepared");
 CHECK(!mkdir(games,0700)||errno==EEXIST);CHECK(!mkdir(states,0700)||errno==EEXIST);
 strcpy(prepared_game,"PPSA99999_");for(unsigned i=0;i<32;i++)snprintf(prepared_game+10+2*i,3,"%02x",(i+cases+1)%256);
 join(game_path,games,prepared_game);join(state_path,states,prepared_game);CHECK(!mkdir(game_path,0700)&&!mkdir(state_path,0700));
 struct stat a,b;CHECK(!lstat(game_path,&a)&&!lstat(state_path,&b));uint8_t record[384]={0};memcpy(record,"BWNAT001",8);record[8]=1;record[12]=128;record[13]=1;
 memset(record+16,'a',32);for(unsigned i=0;i<32;i++)record[48+i]=(uint8_t)((i+cases+1)%256);
 pu_put(record+304,a.st_dev);pu_put(record+312,a.st_ino);pu_put(record+320,b.st_dev);pu_put(record+328,b.st_ino);cp_sha_bytes(record,352,record+352);cp_sha_bytes(record,sizeof record,prepared_sha);
 join(name,state_path,"prepared.bin");local_write(name,record,sizeof record,0600);
 if(existing){join(folder,game_path,"fakelib2");CHECK(!mkdir(folder,0755));
  for(unsigned i=0;i<5;i++){join(name,folder,pb_globals[i].name);
   if(i==0)local_write(name,existing==1?bw_preload_embedded_image.bytes:fixture_previous,existing==1?bw_preload_embedded_image.size:fixture_previous_size,0755);
   else{char source[2048];join(source,BW_PRELOAD_GLOBAL_ROOT,pb_globals[i].name);local_copy(source,name);}
  }
 }
 return (struct bw_preload_bundle_upgrade_request){.bundle={game_path,fast_lease,&ctx,now()+20},.data_root=root,.game=prepared_game,
  .prepared_run="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",.launch_run="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",.prepared_record_sha256=prepared_sha};
}
static int controlled(struct bw_preload_bundle_upgrade_request *q,struct bw_preload_bundle_result *d){
 const struct bw_preload_bundle_control control={full_control,&ctx};return bw_preload_bundle_upgrade_controlled(q,&control,d);
}
static void started(struct bw_preload_bundle_upgrade_request *q){CHECK(!play_control_log_begin(&ctx,q->bundle.deadline,play_data_adapter));}
static void finished(void){clock_shift=0;end();}
int main(int argc,char **argv){
 CHECK(argc==2&&strlen(argv[1])<sizeof root);strcpy(root,argv[1]);CHECK(snprintf(log_path,sizeof log_path,"%s/lifecycle.log",root)>0);CHECK(snprintf(config_path,sizeof config_path,"%s/share.conf",root)>0);
 struct bw_preload_bundle_result d;struct bw_preload_bundle_upgrade_request q;
 /* Actual legacy full controls repeatedly consume the unchanged 20s budget.
  * The deterministic 10ms/full CONTROL cost models elapsed work, not hardware. */
 q=setup_bundle(0);q.bundle.current=full_control;started(&q);full_cost=.010;
 int result=bw_preload_bundle_upgrade_checked(&q,&d);
 fprintf(stderr,"legacy result=%d full=%u settings=%u queries=%u clock=%.3f stage=%s reason=%s detail=%s\n",result,full_calls,settings_reads,queries,clock_shift,d.stage?d.stage:"",d.reason?d.reason:"",ctx.context.registration_failure?ctx.context.registration_failure:"");
 CHECK(result&&now()>=q.bundle.deadline&&clock_shift>=19.0&&full_calls>1500&&ctx.context.registration_failure&&!strcmp(ctx.context.registration_failure,"registration_game_log_idle_refused"));
 printf("{\"case\":\"legacy-cold-deadline\",\"result\":%d,\"fullControls\":%u,\"settingsReads\":%u,\"processQueries\":%u,\"modeledSeconds\":%.3f,\"mutation\":%u,\"releaseUnknown\":%u}\n",result,full_calls,settings_reads,queries,clock_shift,d.mutation_started,d.release_unknown);CHECK(!d.release_unknown);finished();
 for(unsigned existing=0;existing<=2;existing++){
  q=setup_bundle(existing);started(&q);full_cost=.010;result=controlled(&q,&d);
  if(result)fprintf(stderr,"stage=%s reason=%s detail=%s\n",d.stage?d.stage:"",d.reason?d.reason:"",ctx.context.registration_failure?ctx.context.registration_failure:"");
  CHECK(!result&&!d.release_unknown&&full_calls>0&&full_calls<40&&lease_calls>full_calls&&settings_reads==4*full_calls);
  printf("{\"case\":\"split-%s\",\"result\":%d,\"fullControls\":%u,\"leaseChecks\":%u,\"settingsReads\":%u,\"processQueries\":%u,\"modeledSeconds\":%.3f,\"mutation\":%u}\n",existing==0?"cold":existing==1?"exact":"predecessor",result,full_calls,lease_calls,settings_reads,queries,clock_shift,d.mutation_started);
  CHECK(bw_preload_bundle_upgrade_workspace(q.data_root,q.game)==0);finished();
 }
 /* External settings, retained source, actual lifecycle-log, cancellation and
  * immutable-deadline drift all remain refusals. No synthetic game is launched. */
 for(unsigned kind=1;kind<=5;kind++){
  q=setup_bundle(0);started(&q);inject_kind=(int)kind;inject_at=100;result=controlled(&q,&d);CHECK(result&&!d.release_unknown);
  if(kind==1)CHECK(owner.playing.changed);if(kind==4)CHECK(lease_calls==100);if(kind==5)CHECK(clock_shift==21);
  printf("{\"case\":\"split-drift-%u\",\"result\":%d,\"fullControls\":%u,\"leaseChecks\":%u,\"mutation\":%u}\n",kind,result,full_calls,lease_calls,d.mutation_started);finished();
 }
 /* A failed full checkpoint must not be bypassed by later cheap lease checks. */
 q=setup_bundle(0);started(&q);fail_full_at=3;CHECK(controlled(&q,&d)!=0&&full_calls==3&&!d.release_unknown);finished();
 /* A workspace is still an unconditional refusal, even for the exact reader. */
 q=setup_bundle(1);char workspace[2048];join(workspace,state_path,PU_WORKSPACE);CHECK(!mkdir(workspace,0700));started(&q);
 CHECK(controlled(&q,&d)!=0&&!d.mutation_started&&!d.release_unknown&&access(workspace,F_OK)==0);finished();
 q=setup_bundle(0);CHECK(bw_preload_bundle_upgrade_controlled(&q,NULL,&d)==-1&&!d.mutation_started&&!full_calls&&!lease_calls);CHECK(!pthread_mutex_destroy(&owner.lock));cases++;
 printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualBundleBackendSettingsCache\":true,\"modeledSonyTransportAndHeldStartupProof\":true,\"deadlineSeconds\":20,\"nativeTimingClaim\":false,\"consoleActions\":0}\n",cases,checks);return 0;
}
