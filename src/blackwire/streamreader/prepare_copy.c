/* Ordinary-filesystem preparation only. No game launch, process-memory access,
 * debugger, native installer, hooks, or driver. PC plans are ephemeral and
 * sources are opened read only. Game-file publication never replaces a name. */
#include "copy_fs.h"
#include "copy_sha256.h"
#include "../library/launch_timing.h"
#include "startup_mode.h"
#include "preparation_receipt.h"
#include <dirent.h>
#include <sys/file.h>
#include <time.h>
#include <strings.h>
#ifdef BW_COPY_HASH_DECLS
#include BW_COPY_HASH_DECLS
#endif
#if !defined(BW_COPY_HOST_TEST) && !defined(BW_COPY_EMBEDDED)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#endif
#ifndef BW_COPY_DATA
#define BW_COPY_DATA "/data/blackwire"
#endif
#define CP_MAX_MANIFEST (40u*1024u*1024u)
#define CP_MAX_INDEX (48u*1024u*1024u)
#define CP_BUFFER (256u*1024u)
static int (*cp_io_guard)(void *);
static void *cp_io_opaque;
static int cp_io_current(void){return cp_io_guard?cp_io_guard(cp_io_opaque):0;}
/* Optional explicitly owned native Send scope only. Standalone, inspection,
 * Play and other embedded callers keep the original full-idle predicate. */
static int (*cp_idle_guard)(void *);
static void *cp_idle_opaque;
static int cp_games_stopped(const char *path){return cp_idle_guard?cp_idle_guard(cp_idle_opaque):v2_games_stopped(path);}
#ifndef BW_COPY_GAME_LOG
#define BW_COPY_GAME_LOG "/data/shadowmount/debug.log"
#endif
struct cp_plan {
  struct prepare_v2 cfg;
  struct bw_manifest manifest;
  uint8_t *map,*digests,*index;
  size_t map_size,digest_size,index_size;
  char active[4097],selection[2048],phase[6],server[128];
  uint8_t index_sha[32];
  uint32_t local_count,verified_count;
  uint64_t copied_bytes;
  double deadline;
};
struct cp_remote_observation {
  uint64_t type,size,inode,mtime,mtime_nsec,ctime,ctime_nsec;
};
static struct {
  const char *operation;
  int error,hash_flags,remote_flags,cleanup_result,final_cleanup_result,final_created;
  int64_t result;
  uint32_t file_id,desired_mode,observed_mode;
  uint64_t offset,requested,expected_size,observed_size,observed_links,observed_inode,observed_device;
  uint8_t expected_sha[32],source_sha[32],readback_sha[32],final_sha[32];
  struct cp_remote_observation before,opened,named;
} cp_diagnostic;
static int cp_failure(const char *operation,int64_t result,int error) {
  if(!cp_diagnostic.operation){cp_diagnostic.operation=operation;cp_diagnostic.result=result;cp_diagnostic.error=error;}
  errno=error;return -1;
}
static void cp_diagnostic_reset(void) {memset(&cp_diagnostic,0,sizeof cp_diagnostic);memset(&cp_link_observation,0,sizeof cp_link_observation);memset(&cp_final_open_observation,0,sizeof cp_final_open_observation);}
static void cp_diagnostic_hash(FILE *fp,const uint8_t hash[32],int valid) {
  fputc('"',fp);if(valid)for(unsigned i=0;i<32;i++)fprintf(fp,"%02x",hash[i]);fputc('"',fp);
}
static void cp_diagnostic_remote(FILE *fp,const struct cp_remote_observation *r) {
  fprintf(fp,"{\"type\":%"PRIu64",\"size\":%"PRIu64",\"inode\":%"PRIu64",\"mtime\":%"PRIu64",\"mtimeNsec\":%"PRIu64",\"ctime\":%"PRIu64",\"ctimeNsec\":%"PRIu64"}",
    r->type,r->size,r->inode,r->mtime,r->mtime_nsec,r->ctime,r->ctime_nsec);
}
/* Only fixed operation names, numeric metadata and content digests are emitted.
 * Never print an SMB library error, source URL, user name, or share password. */
static void cp_diagnostic_json(FILE *fp) {
  if(!cp_diagnostic.operation)return;
  fprintf(fp,",\"failure\":{\"operation\":\"%s\",\"result\":%"PRId64",\"errno\":%d,\"fileId\":%u,\"offset\":%"PRIu64",\"requested\":%"PRIu64",\"expectedSize\":%"PRIu64",\"desiredMode\":%u,\"observedMode\":%u,\"observedSize\":%"PRIu64",\"observedLinks\":%"PRIu64",\"observedInode\":%"PRIu64",\"observedDevice\":%"PRIu64",\"temporaryCleanup\":%d,\"hashFlags\":%d,\"expectedSHA256\":",
    cp_diagnostic.operation,cp_diagnostic.result,cp_diagnostic.error,cp_diagnostic.file_id,cp_diagnostic.offset,cp_diagnostic.requested,cp_diagnostic.expected_size,
    cp_diagnostic.desired_mode,cp_diagnostic.observed_mode,cp_diagnostic.observed_size,cp_diagnostic.observed_links,cp_diagnostic.observed_inode,cp_diagnostic.observed_device,cp_diagnostic.cleanup_result,cp_diagnostic.hash_flags);
  cp_diagnostic_hash(fp,cp_diagnostic.expected_sha,cp_diagnostic.hash_flags&1);fputs(",\"sourceSHA256\":",fp);cp_diagnostic_hash(fp,cp_diagnostic.source_sha,cp_diagnostic.hash_flags&2);
  fputs(",\"readbackSHA256\":",fp);cp_diagnostic_hash(fp,cp_diagnostic.readback_sha,cp_diagnostic.hash_flags&4);
  fputs(",\"finalSHA256\":",fp);cp_diagnostic_hash(fp,cp_diagnostic.final_sha,cp_diagnostic.hash_flags&8);
  fprintf(fp,",\"remoteFlags\":%d,\"sourceBefore\":",cp_diagnostic.remote_flags);cp_diagnostic_remote(fp,&cp_diagnostic.before);
  fputs(",\"sourceOpened\":",fp);cp_diagnostic_remote(fp,&cp_diagnostic.opened);fputs(",\"sourceNamed\":",fp);cp_diagnostic_remote(fp,&cp_diagnostic.named);
  fprintf(fp,",\"linkat\":{\"attempted\":%s,\"result\":%d,\"errno\":%d,\"rawResult\":%ld,\"rawCarry\":%d}",
    cp_link_observation.attempted?"true":"false",cp_link_observation.result,cp_link_observation.error,cp_link_observation.raw,cp_link_observation.carry);
  fprintf(fp,",\"finalCreated\":%s,\"finalCleanup\":%d,\"finalOpen\":{\"attempted\":%s,\"result\":%d,\"errno\":%d,\"rawResult\":%ld,\"rawCarry\":%d}}",
    cp_diagnostic.final_created?"true":"false",cp_diagnostic.final_cleanup_result,cp_final_open_observation.attempted?"true":"false",
    cp_final_open_observation.result,cp_final_open_observation.error,cp_final_open_observation.raw,cp_final_open_observation.carry);
}
static inline uint32_t cp_u32(const uint8_t *p) { return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static inline uint64_t cp_u64(const uint8_t *p) { return cp_u32(p)|(uint64_t)cp_u32(p+4)<<32; }
static inline double cp_now(void) { struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (double)t.tv_sec+(double)t.tv_nsec/1e9; }
static inline int cp_alive(const struct cp_plan *p) { double now=cp_now();return now>=0 && now<p->deadline; }
static int cp_hexbytes(const char *text,uint8_t *out,size_t count) {
  if(strlen(text)!=count*2)return -1;
  for(size_t i=0;i<count;i++){uint64_t x;char pair[3]={text[i*2],text[i*2+1],0};if(v2_hex(pair,2,&x))return -1;out[i]=(uint8_t)x;}return 0;
}
static int cp_decimal(const char *text,uint64_t maximum,uint64_t *out) {
  uint64_t value=0;if(!*text)return -1;
  for(;*text;text++){
    if(*text<'0'||*text>'9')return -1;unsigned digit=(unsigned)(*text-'0');
    if(value>maximum/10||(value==maximum/10&&digit>maximum%10))return -1;value=value*10+digit;
  }
  *out=value;return 0;
}
static int cp_parse(struct cp_plan *p,const uint8_t *data,size_t size) {
  if(!size||size>4096||memchr(data,0,size))return v2_fail("Invalid bounded preparation selection");
  char bytes[4097];memcpy(bytes,data,size);bytes[size]=0;
  unsigned seen=0;uint64_t number=0;
  for(char *line=bytes;*line;){char *next=strchr(line,'\n');if(next)*next++=0;
    char *value=strchr(line,'=');unsigned bit=0;int bad=0;if(!value||strchr(line,'\r'))return -1;*value++=0;
    if(!strcmp(line,"version")){bit=1;bad=strcmp(value,"2");}
    else if(!strcmp(line,"run_id")){bit=2;bad=v2_copy(p->cfg.run_id,sizeof p->cfg.run_id,value)||v2_hex(value,32,NULL);}
    else if(!strcmp(line,"title_id")){bit=4;bad=v2_copy(p->cfg.title,sizeof p->cfg.title,value);}
    else if(!strcmp(line,"game")){bit=8;bad=v2_copy(p->cfg.game,sizeof p->cfg.game,value)||!v2_game_component(value);}
    else if(!strcmp(line,"eboot_size")){bit=16;bad=cp_decimal(value,UINT64_C(2147483648),&p->cfg.eboot_size)||!p->cfg.eboot_size;}
    else if(!strcmp(line,"manifest_count")){bit=32;bad=cp_decimal(value,BW_MANIFEST_MAX_FILES,&number)||!number;p->cfg.manifest_count=(uint32_t)number;}
    else if(!strcmp(line,"manifest_hash")){bit=64;bad=v2_hex(value,16,&p->cfg.manifest_hash)||v2_copy(p->cfg.hash_text,sizeof p->cfg.hash_text,value);}
    else if(!strcmp(line,"phase")){bit=128;bad=v2_copy(p->phase,sizeof p->phase,value)||(strcmp(value,"copy")&&strcmp(value,"index"));}
    else if(!strcmp(line,"manifest_bytes")){bit=256;bad=cp_decimal(value,CP_MAX_MANIFEST,&number)||number<40;p->map_size=(size_t)number;}
    else if(!strcmp(line,"digests_bytes")){bit=512;bad=cp_decimal(value,56u+32u*BW_MANIFEST_MAX_FILES,&number);p->digest_size=(size_t)number;}
    else if(!strcmp(line,"index_bytes")){bit=1024;bad=cp_decimal(value,CP_MAX_INDEX,&number);p->index_size=(size_t)number;}
    else if(!strcmp(line,"index_sha256")){bit=2048;bad=cp_hexbytes(value,p->index_sha,32);}
    else return v2_fail("Unknown preparation selection field");
    if(bad||(seen&bit))return v2_fail("Invalid or duplicated preparation selection field");seen|=bit;
    if(!next)break;line=next;
  }
  if(seen!=4095||strlen(p->cfg.title)!=9||strncmp(p->cfg.title,"PPSA",4))return -1;
  for(unsigned i=4;i<9;i++)if(p->cfg.title[i]<'0'||p->cfg.title[i]>'9')return -1;
  if(p->digest_size!=56u+32u*p->cfg.manifest_count||(!strcmp(p->phase,"index")?(p->index_size<48):p->index_size!=0))return -1;
  int n=snprintf(p->cfg.root,sizeof p->cfg.root,"%s/%s",BW_GAMES_ROOT,p->cfg.game);
  if(n<0||(size_t)n>=sizeof p->cfg.root)return -1;
  n=snprintf(p->selection,sizeof p->selection,"version=2\nrun_id=%s\ntitle_id=%s\ngame=%s\neboot_size=%"PRIu64"\nmanifest_count=%u\nmanifest_hash=%s\n",
    p->cfg.run_id,p->cfg.title,p->cfg.game,p->cfg.eboot_size,p->cfg.manifest_count,p->cfg.hash_text);
  if(n<0||(size_t)n>=sizeof p->selection)return -1;
  memcpy(p->active,data,size);p->active[size]=0;return 0;
}
static int cp_validate_map(struct cp_plan *p) {
  uint8_t hash[32];cp_sha_bytes(p->map,p->map_size,hash);
  if(bw_manifest_open(&p->manifest,p->map,p->map_size)||p->manifest.count!=p->cfg.manifest_count||p->manifest.checksum!=p->cfg.manifest_hash||
     memcmp(p->digests,"BWDIG001",8)||cp_u32(p->digests+8)!=1||cp_u32(p->digests+12)!=p->manifest.count||
     cp_u64(p->digests+16)!=p->manifest.checksum||memcmp(p->digests+24,hash,32))return v2_fail("Manifest or digest table does not match the selected generation");
  int boot=0;p->local_count=0;
  for(uint32_t i=0;i<p->manifest.count;i++){struct bw_manifest_record r;if(bw_manifest_get(&p->manifest,i,&r))return -1;
    if(!strcmp(r.path,"blackwire.files")||!strcmp(r.path,"ampr_emu.index"))return -1;
    if(!strcmp(r.path,"eboot.bin")){if(r.flags!=BW_MANIFEST_LOCAL||r.size!=p->cfg.eboot_size)return -1;boot=1;}
    if(r.flags==BW_MANIFEST_LOCAL)p->local_count++;
    else {uint8_t zero[32]={0};if(memcmp(p->digests+56u+32u*i,zero,32))return -1;}
  }return boot?0:v2_fail("The preparation manifest has no matching startup executable");
}
static int cp_write_all(int fd,const void *data,size_t size) {
#ifndef BW_COPY_WRITE
#define BW_COPY_WRITE write
#endif
  size_t at=0;while(at<size){if(cp_io_current())return -1;size_t want=size-at<65536?size-at:65536;ssize_t n=BW_COPY_WRITE(fd,(const uint8_t *)data+at,want);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>want)return -1;at+=(size_t)n;}return 0;
}
static int cp_regular(int parent,const char *leaf,int fd,uint64_t size,struct stat *out) {
  struct stat a,b;if(cp_fstatat(parent,leaf,&a,AT_SYMLINK_NOFOLLOW)||fstat(fd,&b)||!S_ISREG(a.st_mode)||!S_ISREG(b.st_mode)||
    a.st_nlink!=1||b.st_nlink!=1||!v2_identity(&a,&b)||b.st_size<0||(uint64_t)b.st_size!=size)return -1;
  if(out)*out=b;return 0;
}
static int cp_file_hash(int fd,uint64_t size,uint8_t hash[32]) {
  BW_LT_COUNT(BW_LT_LOCAL_HASH_CALLS,1);
#ifndef BW_COPY_HASH_READ
#define BW_COPY_HASH_READ pread
#endif
  uint8_t buffer[CP_BUFFER];struct cp_sha sha;cp_sha_init(&sha);uint64_t at=0;
  while(at<size){if(cp_io_current())return -1;size_t want=size-at<sizeof buffer?(size_t)(size-at):sizeof buffer;BW_LT_COUNT(BW_LT_LOCAL_READ_CALLS,1);ssize_t n=BW_COPY_HASH_READ(fd,buffer,want,(off_t)at);
    if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>want)return -1;BW_LT_COUNT(BW_LT_LOCAL_HASH_BYTES,(uint64_t)n);cp_sha_update(&sha,buffer,(size_t)n);at+=(uint64_t)n;}
  cp_sha_final(&sha,hash);return 0;
}
static int cp_existing(const struct prepare_v2 *cfg,int root,int parent,const char *parent_rel,const char *leaf,uint64_t size,const uint8_t hash[32]) {
  struct stat before,after;uint8_t actual[32];
  int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0)return errno==ENOENT?1:-1;
  int bad=!v2_directory_current(cfg,root,parent,parent_rel)||cp_regular(parent,leaf,fd,size,&before)||(!*parent_rel&&!strcmp(leaf,"eboot.bin")&&!(before.st_mode&0111))||cp_file_hash(fd,size,actual)||
    cp_regular(parent,leaf,fd,size,&after)||!v2_identity(&before,&after)||before.st_mtime!=after.st_mtime||before.st_ctime!=after.st_ctime||
    memcmp(hash,actual,32)||!v2_directory_current(cfg,root,parent,parent_rel);
  close(fd);return bad?v2_fail("An existing game file differs or changed; its bytes were preserved"):0;
}
static int cp_cleanup_temp(const struct prepare_v2 *,int,int,const char *,const char *,int,const struct stat *);
/* Target FS does not support hard links. Exclusively create a new final inode,
 * copy only verified bytes, and verify it completely before releasing the temp.
 * This prevents replacement but is NOT atomic complete-file publication: a
 * power loss can leave a partial new name that requires explicit recovery. */
static int cp_publish(const struct prepare_v2 *cfg,int root,int parent,const char *parent_rel,const char *temp,const char *leaf,int fd,uint64_t size,
                      mode_t mode,const uint8_t expected[32],double deadline) {
  struct stat before,created,held;int rc,final=-1,known_final=0,bad=1;
  memset(&cp_link_observation,0,sizeof cp_link_observation);memset(&cp_final_open_observation,0,sizeof cp_final_open_observation);
  cp_diagnostic.final_created=0;cp_diagnostic.final_cleanup_result=0;cp_diagnostic.desired_mode=(uint32_t)mode;cp_diagnostic.expected_size=size;
  memcpy(cp_diagnostic.expected_sha,expected,32);cp_diagnostic.hash_flags|=1;
  errno=0;if(!v2_directory_current(cfg,root,parent,parent_rel)){cp_failure("publication.parent",0,errno);goto done;}
  errno=0;rc=cp_regular(parent,temp,fd,size,&before);if(rc){cp_failure("publication.temporary",rc,errno);goto done;}
  if((before.st_mode&07777)!=mode){cp_failure("publication.temporary-mode",0,0);goto done;}
  errno=0;rc=fsync(fd);if(rc){cp_failure("publication.file-fsync",rc,errno);goto done;}
  errno=0;rc=cp_games_stopped(BW_COPY_GAME_LOG);if(rc){cp_failure("publication.idle",rc,errno);goto done;}
  double started=cp_now();if(started<0||started>=deadline){cp_failure("publication.deadline",0,0);goto done;}
  errno=0;final=cp_final_openat(parent,leaf,mode);if(final<0){cp_failure("publication.exclusive-open",final,errno);goto done;}
  cp_diagnostic.final_created=1;
  errno=0;rc=fstat(final,&created);known_final=!rc;if(rc){cp_failure("publication.created-stat",rc,errno);goto done;}
  errno=0;if(cp_regular(parent,leaf,final,0,NULL)||v2_identity(&before,&created)){cp_failure("publication.created-identity",-1,errno);goto done;}
  errno=0;rc=fchmod(final,mode);if(rc){cp_failure("publication.mode-set",rc,errno);goto done;}
  uint8_t buffer[CP_BUFFER],hash[32];struct cp_sha copied_sha;cp_sha_init(&copied_sha);uint64_t at=0;
  while(at<size){
    if(cp_io_current()){cp_failure("publication.cancelled",-1,0);goto done;}
    double now=cp_now();if(now<0||now>=deadline){cp_failure("publication.deadline",0,0);goto done;}
    errno=0;if(!v2_directory_current(cfg,root,parent,parent_rel)||cp_regular(parent,temp,fd,size,&held)||!v2_identity(&before,&held)||
      held.st_mtime!=before.st_mtime||held.st_ctime!=before.st_ctime||(held.st_mode&07777)!=mode){cp_failure("publication.copy-temporary",-1,errno);goto done;}
    errno=0;if(cp_regular(parent,leaf,final,at,&held)||!v2_identity(&created,&held)||(held.st_mode&07777)!=mode){cp_failure("publication.copy-final",-1,errno);goto done;}
    size_t want=size-at<sizeof buffer?(size_t)(size-at):sizeof buffer;ssize_t got;
    cp_diagnostic.offset=at;cp_diagnostic.requested=want;
    do{errno=0;got=pread(fd,buffer,want,(off_t)at);}while(got<0&&errno==EINTR);
    if(got<=0||(size_t)got>want){cp_failure("publication.temporary-read",got,errno);goto done;}
    /* Only verified TEMP -> exclusively created FINAL publication uses this
     * existing full buffer. Source consumers and generic writes stay <=64 KiB.
     * Every actual attempt, including short/EINTR continuations, reacquires
     * current authority before rechecking the named parent and held inodes. */
    size_t written=0;
    while(written<(size_t)got){
      if(cp_io_current()){cp_failure("publication.cancelled",-1,0);goto done;}
      errno=0;if(!v2_directory_current(cfg,root,parent,parent_rel)||cp_regular(parent,temp,fd,size,&held)||!v2_identity(&before,&held)||
        held.st_mtime!=before.st_mtime||held.st_ctime!=before.st_ctime||(held.st_mode&07777)!=mode){cp_failure("publication.copy-temporary",-1,errno);goto done;}
      errno=0;if(cp_regular(parent,leaf,final,at+(uint64_t)written,&held)||!v2_identity(&created,&held)||(held.st_mode&07777)!=mode){cp_failure("publication.copy-final",-1,errno);goto done;}
      double write_now=cp_now();if(write_now<0||write_now>=deadline){cp_failure("publication.deadline",0,0);goto done;}
      size_t remaining=(size_t)got-written;cp_diagnostic.offset=at+(uint64_t)written;cp_diagnostic.requested=remaining;
      errno=0;ssize_t n=BW_COPY_WRITE(final,buffer+written,remaining);
      if(n<0&&errno==EINTR)continue;
      if(n<=0||(size_t)n>remaining){cp_failure("publication.final-write",n,errno);goto done;}
      written+=(size_t)n;
    }
    cp_sha_update(&copied_sha,buffer,(size_t)got);at+=(uint64_t)got;
  }
  cp_sha_final(&copied_sha,hash);memcpy(cp_diagnostic.readback_sha,hash,32);cp_diagnostic.hash_flags|=4;cp_diagnostic.offset=at;
  if(memcmp(hash,expected,32)){cp_failure("publication.temporarySHA",0,0);goto done;}
  errno=0;rc=fsync(final);if(rc){cp_failure("publication.final-fsync",rc,errno);goto done;}
  errno=0;rc=cp_regular(parent,leaf,final,size,&held);if(rc||!v2_identity(&created,&held)){cp_failure("publication.final-identity",rc,errno);goto done;}
  cp_diagnostic.observed_mode=(uint32_t)held.st_mode;cp_diagnostic.observed_size=(uint64_t)held.st_size;
  cp_diagnostic.observed_links=(uint64_t)held.st_nlink;cp_diagnostic.observed_inode=(uint64_t)held.st_ino;cp_diagnostic.observed_device=(uint64_t)held.st_dev;
  if((held.st_mode&07777)!=mode){cp_failure("publication.final-mode",0,0);goto done;}
  errno=0;rc=cp_file_hash(final,size,hash);if(rc){cp_failure("publication.final-readback",rc,errno);goto done;}
  memcpy(cp_diagnostic.final_sha,hash,32);cp_diagnostic.hash_flags|=8;
  if(memcmp(hash,expected,32)){cp_failure("publication.finalSHA",0,0);goto done;}
  errno=0;if(!v2_directory_current(cfg,root,parent,parent_rel)||cp_regular(parent,leaf,final,size,&held)||!v2_identity(&created,&held)||(held.st_mode&07777)!=mode||
     cp_regular(parent,temp,fd,size,&held)||!v2_identity(&before,&held)||held.st_mtime!=before.st_mtime||held.st_ctime!=before.st_ctime||(held.st_mode&07777)!=mode){cp_failure("publication.verified-identity",-1,errno);goto done;}
  errno=0;rc=cp_games_stopped(BW_COPY_GAME_LOG);if(rc){cp_failure("publication.final-idle",rc,errno);goto done;}
  errno=0;rc=fsync(parent);if(rc){cp_failure("publication.directory-fsync",rc,errno);goto done;}
  errno=0;rc=cp_cleanup_temp(cfg,root,parent,parent_rel,temp,fd,&before);
  if(rc!=1){cp_failure("publication.temporary-unlink",rc,errno);goto done;}
  bad=0;
done:
  if(bad&&known_final)cp_diagnostic.final_cleanup_result=cp_cleanup_temp(cfg,root,parent,parent_rel,leaf,final,&created);
  if(final>=0){errno=0;rc=close(final);if(rc){cp_failure("publication.final-close",rc,errno);bad=1;}}
  return bad?v2_fail("Exclusive final-file copy or verification failed; existing files were preserved"):0;
}
/* Failure cleanup is limited to the still-held inode created by this call.
 * Multiple names or a changed identity are ambiguous; retain that case.
 * Never remove a pre-existing collision, a replacement, or a glob of old runs. */
static int cp_cleanup_temp(const struct prepare_v2 *cfg,int root,int parent,const char *parent_rel,const char *temp,int fd,const struct stat *created) {
  struct stat named,held;
  if(!v2_directory_current(cfg,root,parent,parent_rel)||cp_fstatat(parent,temp,&named,AT_SYMLINK_NOFOLLOW)||fstat(fd,&held)||
     !S_ISREG(named.st_mode)||!S_ISREG(held.st_mode)||named.st_nlink!=1||held.st_nlink!=1||
     !v2_identity(&named,&held)||!v2_identity(created,&held)||!v2_directory_current(cfg,root,parent,parent_rel))return 0;
  if(cp_unlinkat(parent,temp))return -1;
  return fsync(parent)?-1:1;
}
static int cp_blob(const struct prepare_v2 *cfg,int root,const char *leaf,const void *bytes,size_t size,const char *run,double deadline) {
  cp_diagnostic_reset();cp_diagnostic.file_id=UINT32_MAX;
  uint8_t hash[32];cp_sha_bytes(bytes,size,hash);
  int existing=cp_existing(cfg,root,root,"",leaf,size,hash);if(existing<=0)return existing;
  char temp[128];int n=snprintf(temp,sizeof temp,"%s.%s.copy.tmp",leaf,run);if(n<0||(size_t)n>=sizeof temp)return -1;
  int fd=v2_openat(root,temp,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0644);if(fd<0)return v2_fail("A preparation temporary file already exists; inspect it before retrying");
  uint8_t actual[32];struct stat created;int known=!fstat(fd,&created);
  int bad=!known||fchmod(fd,0644)||cp_write_all(fd,bytes,size)||fsync(fd)||cp_file_hash(fd,size,actual)||memcmp(actual,hash,32)||cp_publish(cfg,root,root,"",temp,leaf,fd,size,0644,hash,deadline);
  if(bad&&known)cp_diagnostic.cleanup_result=cp_cleanup_temp(cfg,root,root,"",temp,fd,&created);close(fd);return bad?-1:0;
}

static int cp_refresh(const struct cp_plan *p);
static int cp_permission_note(const struct cp_plan *p,uint32_t file_id,const struct stat *st,mode_t next,const char *phase) {
  struct prepare_v2 data;memset(&data,0,sizeof data);if(v2_copy(data.root,sizeof data.root,BW_COPY_DATA))return -1;
  int root=v2_open_root(&data);if(root<0)return -1;
  const char *leaf="prepare-copy-modes.log";
  int fd=v2_openat(root,leaf,O_WRONLY|O_APPEND|O_CREAT|O_NOFOLLOW|O_NONBLOCK,0600);int result=-1;struct stat log;
  if(fd<0)goto done;
  if(flock(fd,LOCK_EX|LOCK_NB)||fstat(fd,&log)||!S_ISREG(log.st_mode)||log.st_nlink!=1||
      (log.st_mode&0777)!=0600||log.st_size<0||log.st_size>1024*1024||
      !v2_root_current(&data,root)||cp_regular(root,leaf,fd,(uint64_t)log.st_size,NULL))goto done;
  char line[384];int n=snprintf(line,sizeof line,"run=%s title=%s fileId=%u inode=%"PRIu64" device=%"PRIu64" before=%04o after=%04o phase=%s\n",
    p->cfg.run_id,p->cfg.title,file_id,(uint64_t)st->st_ino,(uint64_t)st->st_dev,(unsigned)(st->st_mode&07777),(unsigned)next,phase);
  if(n<0||(size_t)n>=sizeof line||n>1024*1024-log.st_size||cp_write_all(fd,line,(size_t)n)||fsync(fd)||
      !v2_root_current(&data,root)||cp_regular(root,leaf,fd,(uint64_t)log.st_size+(size_t)n,NULL))goto done;
  result=0;
done:
  if(fd>=0)close(fd);close(root);return result;
}
/* Called only in copy phase after the complete conflict preflight. Adds only
 * missing execute bits to a matching LOCAL loader file. Original bytes and
 * mtime are retained; the old/new mode and inode are fsynced before mutation.
 */
static int cp_repair_execute(struct cp_plan *p,int root,int parent,const char *parent_rel,const char *leaf,
                             const struct bw_manifest_record *record,const uint8_t expected[32],uint32_t file_id) {
  if(!(bw_startup_mode(record->path)&0111))return 0;
  if(record->flags!=BW_MANIFEST_LOCAL)return v2_fail("Startup permission repair only accepts a LOCAL file");
  if(strcmp(p->phase,"copy"))return v2_fail("Startup permission repair requires the active copy phase");
  int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0),result=-1;struct stat before,after;uint8_t actual[32];
  if(fd<0)return v2_fail("A startup module is unavailable for permission verification");
  if(!v2_directory_current(&p->cfg,root,parent,parent_rel)||cp_regular(parent,leaf,fd,record->size,&before))goto done;
  if((before.st_mode&0111)==0111){result=0;goto done;}
  if((before.st_mode&07000)||cp_file_hash(fd,record->size,actual)||memcmp(actual,expected,32)||
     cp_regular(parent,leaf,fd,record->size,&after)||!v2_identity(&before,&after)||
     before.st_mtime!=after.st_mtime||before.st_ctime!=after.st_ctime||before.st_mode!=after.st_mode||
     !cp_alive(p)||cp_refresh(p)||cp_games_stopped(BW_COPY_GAME_LOG))goto done;
  mode_t mode=(before.st_mode&0777)|0111;
  if(cp_permission_note(p,file_id,&before,mode,"intent")||cp_refresh(p)||cp_games_stopped(BW_COPY_GAME_LOG)||
     !v2_directory_current(&p->cfg,root,parent,parent_rel)||cp_regular(parent,leaf,fd,record->size,&after)||
     !v2_identity(&before,&after)||before.st_mtime!=after.st_mtime||before.st_ctime!=after.st_ctime||before.st_mode!=after.st_mode)goto done;
  if(fchmod(fd,mode)||fsync(fd)||cp_regular(parent,leaf,fd,record->size,&after)||!v2_identity(&before,&after)||
     (after.st_mode&07777)!=mode||after.st_mtime!=before.st_mtime||cp_file_hash(fd,record->size,actual)||memcmp(actual,expected,32)||
     !v2_directory_current(&p->cfg,root,parent,parent_rel)||cp_permission_note(p,file_id,&before,mode,"applied"))goto done;
  result=0;
done:
  close(fd);return result?v2_fail("A startup module's execute permission could not be verified or repaired; its bytes were preserved"):0;
}
/* Only owned control selections permit replacement. A bounded byte-for-byte
 * prior file is retained once per run, never a game file or share credential. */
static int cp_control(struct cp_plan *p,int datafd,const char *leaf,const char *bytes) {
  char temp[128],backup[128];struct stat before,after;int old=-1,newfd=-1,result=-1;uint8_t prior[4096];size_t prior_size=0;
  struct prepare_v2 anchor;memset(&anchor,0,sizeof anchor);if(v2_copy(anchor.root,sizeof anchor.root,BW_COPY_DATA))return -1;
  if(strlen(bytes)>4096||!v2_root_current(&anchor,datafd))return v2_fail("Owned control directory changed identity");
  snprintf(temp,sizeof temp,"%s.%s.new",leaf,p->cfg.run_id);snprintf(backup,sizeof backup,"%s.previous.%s",leaf,p->cfg.run_id);
  old=v2_openat(datafd,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
  if(old>=0){if(fstat(old,&before)||!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_size<0||before.st_size>4096||
      cp_regular(datafd,leaf,old,(uint64_t)before.st_size,NULL)||v2_read_all(old,prior,(size_t)before.st_size))goto done;
    prior_size=(size_t)before.st_size;int saved=v2_openat(datafd,backup,O_CREAT|O_EXCL|O_NOFOLLOW|O_WRONLY,0600);if(saved<0)goto done;
    int bad=fchmod(saved,0600)||cp_write_all(saved,prior,prior_size)||fsync(saved);if(close(saved))bad=1;if(bad)goto done;
  }else if(errno!=ENOENT)goto done;
  newfd=v2_openat(datafd,temp,O_CREAT|O_EXCL|O_NOFOLLOW|O_WRONLY,0644);if(newfd<0||fchmod(newfd,0644)||cp_write_all(newfd,bytes,strlen(bytes))||fsync(newfd)||cp_refresh(p)||cp_games_stopped(BW_COPY_GAME_LOG))goto done;
  if(old>=0){if(cp_regular(datafd,leaf,old,prior_size,&after)||!v2_identity(&before,&after)||before.st_mtime!=after.st_mtime||before.st_ctime!=after.st_ctime)goto done;}
  else {if(!cp_fstatat(datafd,leaf,&after,AT_SYMLINK_NOFOLLOW)||errno!=ENOENT)goto done;}
  if(!v2_root_current(&anchor,datafd)||cp_renameat(datafd,temp,datafd,leaf)||cp_regular(datafd,leaf,newfd,strlen(bytes),NULL)||!v2_root_current(&anchor,datafd)||fsync(datafd))goto done;
  result=0;
done:if(old>=0)close(old);if(newfd>=0)close(newfd);memset(prior,0,sizeof prior);return result?v2_fail("Owned control selection changed or could not be published; prior selection retained when present"):0;
}

static int cp_measure_same(const struct stat *a,const struct stat *b) {
  return v2_identity(a,b)&&a->st_mode==b->st_mode&&a->st_nlink==b->st_nlink&&a->st_size==b->st_size&&a->st_blocks==b->st_blocks&&
    a->st_mtim.tv_sec==b->st_mtim.tv_sec&&a->st_mtim.tv_nsec==b->st_mtim.tv_nsec&&
    a->st_ctim.tv_sec==b->st_ctim.tv_sec&&a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
/* Count actual allocation, including retained prefixes and unrelated extras
 * inside this selected directory. Never open links, devices or other mounts.
 * Each directory walk gets its own cursor; dup would share a consumed offset. */
static int cp_measure_tree(const struct cp_plan *p,int directory,dev_t device,unsigned depth,uint64_t *total,uint32_t *entries) {
  const uint64_t maximum=UINT64_C(9007199254740991);struct stat before,after;
  if(depth>48||!cp_alive(p)||fstat(directory,&before)||!S_ISDIR(before.st_mode)||before.st_dev!=device||before.st_blocks<0||
    ++*entries>262144||(uint64_t)before.st_blocks>(maximum-*total)/512)return -1;
  *total+=(uint64_t)before.st_blocks*512;
  int cursor=v2_openat(directory,".",O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);if(cursor<0)return -1;
  DIR *dir=fdopendir(cursor);if(!dir){close(cursor);return -1;}int bad=0;struct dirent *entry;
  for(;;){errno=0;entry=readdir(dir);if(!entry){if(errno)bad=1;break;}
    if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;
    struct stat named,held;int fd=-1;
    if(!cp_alive(p)||cp_fstatat(directory,entry->d_name,&named,AT_SYMLINK_NOFOLLOW)||named.st_dev!=device){bad=1;break;}
    if(S_ISDIR(named.st_mode)){
      fd=v2_openat(directory,entry->d_name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
      if(fd<0||fstat(fd,&held)||!cp_measure_same(&named,&held)||cp_measure_tree(p,fd,device,depth+1,total,entries))bad=1;
    }else if(S_ISREG(named.st_mode)&&named.st_nlink==1&&named.st_size>=0&&named.st_blocks>=0){
      fd=v2_openat(directory,entry->d_name,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
      if(fd<0||fstat(fd,&held)||!cp_measure_same(&named,&held)||++*entries>262144||(uint64_t)held.st_blocks>(maximum-*total)/512)bad=1;
      else *total+=(uint64_t)held.st_blocks*512;
    }else bad=1;
    if(!bad&&(cp_fstatat(directory,entry->d_name,&after,AT_SYMLINK_NOFOLLOW)||!cp_measure_same(&named,&after)))bad=1;
    if(fd>=0&&close(fd))bad=1;if(bad)break;
  }
  if(closedir(dir))bad=1;
  if(fstat(directory,&after)||!cp_measure_same(&before,&after))bad=1;
  return bad?-1:0;
}
static int cp_receipt(struct cp_plan *p,int datafd,int root) {
  uint8_t manifest[32],marker[BW_GENERATION_SIZE],generation[32];char leaf[64],receipt[64],json[1400],game_hex[481],manifest_hex[65],generation_hex[65];
  uint64_t local_bytes=0,remote_bytes=0;
  for(uint32_t i=0;i<p->manifest.count;i++){
    struct bw_manifest_record record;if(bw_manifest_get(&p->manifest,i,&record))return -1;
    uint64_t *sum=record.flags==BW_MANIFEST_LOCAL?&local_bytes:&remote_bytes;
    if(record.size>UINT64_C(9007199254740991)-*sum)return -1;*sum+=record.size;
  }
  if(local_bytes>UINT64_C(9007199254740991)-remote_bytes)return -1;
  cp_sha_bytes(p->map,p->map_size,manifest);
  if(bw_generation_name(leaf,p->cfg.run_id)||bw_generation_bytes(marker,p->cfg.run_id,p->cfg.title,p->cfg.game,manifest)||
    !cp_alive(p)||cp_refresh(p)||cp_games_stopped(BW_COPY_GAME_LOG)||
    cp_blob(&p->cfg,root,leaf,marker,sizeof marker,p->cfg.run_id,p->deadline))return -1;
  cp_sha_bytes(marker,sizeof marker,generation);
  for(size_t i=0;i<strlen(p->cfg.game);i++)snprintf(game_hex+i*2,3,"%02x",(unsigned char)p->cfg.game[i]);
  for(unsigned i=0;i<32;i++){snprintf(manifest_hex+i*2,3,"%02x",manifest[i]);snprintf(generation_hex+i*2,3,"%02x",generation[i]);}
  struct stat root_stat;uint64_t total=0;uint32_t entries=0;
  if(!v2_root_current(&p->cfg,root)||fstat(root,&root_stat)||cp_measure_tree(p,root,root_stat.st_dev,0,&total,&entries)||
    !v2_root_current(&p->cfg,root)||cp_refresh(p)||cp_games_stopped(BW_COPY_GAME_LOG))return v2_fail("Selected game allocation could not be measured; no storage receipt was published");
  int n=snprintf(json,sizeof json,"{\"version\":2,\"preparationRunId\":\"%s\",\"titleId\":\"%s\",\"gameHex\":\"%s\",\"manifestSha256\":\"%s\",\"generationSha256\":\"%s\",\"allocatedBytes\":%"PRIu64",\"entries\":%u,\"localBytes\":%"PRIu64",\"remoteBytes\":%"PRIu64",\"allocationVerified\":true}\n",
    p->cfg.run_id,p->cfg.title,game_hex,manifest_hex,generation_hex,total,entries,local_bytes,remote_bytes);
  if(n<0||(size_t)n>=sizeof json)return -1;
  snprintf(receipt,sizeof receipt,"preparation-%s.json",p->cfg.title);
  return cp_control(p,datafd,receipt,json);
}

/* Shared by target transport and host pipe tests. Strict bounded framing;
 * the caller owns fd and must set a receive timeout before using a socket. */
#ifndef BW_COPY_EMBEDDED
static int cp_http_receive(const struct cp_plan *p,int fd,size_t maximum,uint8_t **out,size_t *out_size) {
  uint8_t header[8193];size_t used=0,body=0;int result=-1;*out=NULL;*out_size=0;
  while(used<8192){ssize_t got=read(fd,header+used,1);if(got<0&&errno==EINTR)continue;if(got!=1)goto done;used++;
    if(used>=4&&!memcmp(header+used-4,"\r\n\r\n",4))break;}
  if(memchr(header,0,used))goto done;
  header[used]=0;if(used<4||memcmp(header+used-4,"\r\n\r\n",4)||
    (strncmp((char *)header,"HTTP/1.0 200 ",13)&&strncmp((char *)header,"HTTP/1.1 200 ",13)))goto done;
  unsigned lengths=0;char *line=strstr((char *)header,"\r\n");if(!line)goto done;line+=2;
  while(*line&&strcmp(line,"\r\n")){char *end=strstr(line,"\r\n"),*colon=strchr(line,':');if(!end||!colon||colon>end)goto done;*end=0;*colon++=0;while(*colon==' ')colon++;
    if(!strcasecmp(line,"Content-Length")){uint64_t size;if(++lengths!=1||cp_decimal(colon,maximum,&size))goto done;body=(size_t)size;}
    if(!strcasecmp(line,"Transfer-Encoding")||!strcasecmp(line,"Content-Encoding"))goto done;line=end+2;
  }
  if(lengths!=1||!body)goto done;*out=malloc(body);if(!*out)goto done;
  for(size_t at=0;at<body;){if(!cp_alive(p))goto done;ssize_t got=read(fd,*out+at,body-at);if(got<0&&errno==EINTR)continue;if(got<=0)goto done;at+=(size_t)got;}
  {uint8_t extra;ssize_t got;do{got=read(fd,&extra,1);}while(got<0&&errno==EINTR);if(got!=0)goto done;}
  *out_size=body;result=0;
done:
  if(result){free(*out);*out=NULL;return v2_fail("The bounded PC response was truncated or invalid");}return 0;
}
#endif

#if !defined(BW_COPY_HOST_TEST) && !defined(BW_COPY_EMBEDDED)
/* Deliberately fixed peer port and generated paths. No redirects, compression,
 * chunking, cookies, URL credentials, or caller-supplied download URLs. */
static int cp_fetch(const struct cp_plan *p,const char *path,size_t maximum,uint8_t **out,size_t *out_size) {
  struct sockaddr_in address;int fd=-1,result=-1;*out=NULL;*out_size=0;
  if(!cp_alive(p)||strncmp(path,"/api/preparation/",17))return v2_fail("Preparation plan expired");
  memset(&address,0,sizeof address);address.sin_family=AF_INET;address.sin_port=htons(5917);
  if(inet_pton(AF_INET,p->server,&address.sin_addr)!=1)return -1;
  fd=socket(AF_INET,SOCK_STREAM,0);if(fd<0)return -1;struct timeval timeout={45,0};
  if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof timeout)||setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof timeout)||connect(fd,(struct sockaddr *)&address,sizeof address))goto done;
  char request[256];int n=snprintf(request,sizeof request,"GET %s HTTP/1.0\r\nHost: %s:5917\r\nConnection: close\r\n\r\n",path,p->server);
  if(n<0||(size_t)n>=sizeof request||cp_write_all(fd,request,(size_t)n))goto done;
  result=cp_http_receive(p,fd,maximum,out,out_size);
done:
  if(fd>=0)close(fd);if(result){free(*out);*out=NULL;return v2_fail("The pinned PC preparation resource is unavailable or invalid");}return 0;
}
static int cp_refresh_path(const struct cp_plan *p,const char *path) {
  uint8_t *bytes=NULL;size_t size=0;if(cp_fetch(p,path,4096,&bytes,&size))return -1;
  int bad=size!=strlen(p->active)||memcmp(bytes,p->active,size);free(bytes);
  return bad?v2_fail("The active preparation selection or source generation changed"):0;
}
static int cp_refresh(const struct cp_plan *p) {return cp_refresh_path(p,"/api/preparation/lease");}
static int cp_generation(const struct cp_plan *p) {return cp_refresh_path(p,"/api/preparation/active");}
static int cp_resource(const struct cp_plan *p,const char *kind,size_t size,uint8_t **out) {
  char path[128];snprintf(path,sizeof path,"/api/preparation/%s/%s",p->cfg.run_id,kind);size_t got;
  if(cp_fetch(p,path,size,out,&got))return -1;if(got!=size){free(*out);*out=NULL;return v2_fail("Preparation resource length mismatch");}return 0;
}
static int cp_share_field(char *out,size_t capacity,const char *key,int required) {
  int fd=open(BW_COPY_DATA "/share.conf",O_RDONLY|O_NOFOLLOW|O_NONBLOCK);struct stat st;
  if(fd<0)return -1;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<1||st.st_size>4096){close(fd);return -1;}
  char data[4097];if(v2_read_all(fd,data,(size_t)st.st_size)){close(fd);return -1;}close(fd);data[st.st_size]=0;if(memchr(data,0,(size_t)st.st_size))return -1;
  unsigned seen=0;out[0]=0;
  for(char *line=data;*line;){char *next=strchr(line,'\n');if(next)*next++=0;line[strcspn(line,"\r")]=0;
    size_t n=strlen(key);if(!strncmp(line,key,n)&&line[n]=='='){if(seen++||v2_copy(out,capacity,line+n+1))return -1;}
    if(!next)break;line=next;}
  memset(data,0,sizeof data);return required&&!seen?-1:0;
}
static int cp_smb_same(const struct smb2_stat_64 *a,const struct smb2_stat_64 *b) {
  return a->smb2_type==SMB2_TYPE_FILE&&b->smb2_type==SMB2_TYPE_FILE&&a->smb2_size==b->smb2_size&&a->smb2_ino==b->smb2_ino&&
    a->smb2_mtime==b->smb2_mtime&&a->smb2_mtime_nsec==b->smb2_mtime_nsec&&a->smb2_ctime==b->smb2_ctime&&a->smb2_ctime_nsec==b->smb2_ctime_nsec;
}
static void cp_remote_observe(struct cp_remote_observation *out,const struct smb2_stat_64 *st) {
  out->type=st->smb2_type;out->size=st->smb2_size;out->inode=st->smb2_ino;
  out->mtime=st->smb2_mtime;out->mtime_nsec=st->smb2_mtime_nsec;out->ctime=st->smb2_ctime;out->ctime_nsec=st->smb2_ctime_nsec;
}
static int cp_smb_current(struct smb2_context *smb,struct smb2fh *fh,const char *path,const struct smb2_stat_64 *before) {
  struct smb2_stat_64 opened,named;errno=0;int rc=smb2_fstat(smb,fh,&opened);
  if(rc){cp_failure("sourceGeneration.opened-stat",rc,errno);return 0;}cp_remote_observe(&cp_diagnostic.opened,&opened);cp_diagnostic.remote_flags|=2;
  errno=0;rc=smb2_stat(smb,path,&named);if(rc){cp_failure("sourceGeneration.named-stat",rc,errno);return 0;}
  cp_remote_observe(&cp_diagnostic.named,&named);cp_diagnostic.remote_flags|=4;
  if(!cp_smb_same(before,&opened)){cp_failure("sourceGeneration.opened-changed",0,0);return 0;}
  if(!cp_smb_same(before,&named)){cp_failure("sourceGeneration.named-changed",0,0);return 0;}
  return 1;
}
static int cp_source_file(struct cp_plan *p,struct smb2_context *smb,int root,uint32_t i) {
  struct bw_manifest_record r;char leaf[256],parent_rel[1024],source[1536],temp[128];
  cp_diagnostic_reset();cp_diagnostic.file_id=i;errno=0;int rc=bw_manifest_get(&p->manifest,i,&r);
  if(rc||r.flags!=BW_MANIFEST_LOCAL)return cp_failure("selection.record",rc,errno);
  cp_diagnostic.expected_size=r.size;cp_diagnostic.desired_mode=bw_startup_mode(r.path);
  errno=0;rc=cp_refresh(p);if(rc)return cp_failure("selection.lease",rc,errno);
  errno=0;rc=cp_games_stopped(BW_COPY_GAME_LOG);if(rc)return cp_failure("source.idle",rc,errno);
  errno=0;int parent=v2_parent(&p->cfg,root,r.path,1,leaf,parent_rel);if(parent<0)return cp_failure("destination.parent",parent,errno);
  const uint8_t *expected=p->digests+56u+32u*i;
  memcpy(cp_diagnostic.expected_sha,expected,32);cp_diagnostic.hash_flags=1;
  errno=0;int exists=cp_existing(&p->cfg,root,parent,parent_rel,leaf,r.size,expected);if(exists<=0){
    if(exists)cp_failure("destination.existing",exists,errno);
    else if(cp_repair_execute(p,root,parent,parent_rel,leaf,&r,expected,i))exists=cp_failure("destination.execute-mode",-1,errno);
    close(parent);return exists;
  }
  int n=snprintf(source,sizeof source,"%s/%s",p->cfg.game,r.path);if(n<0||(size_t)n>=sizeof source){close(parent);return cp_failure("source.path",n,0);}
  struct smb2_stat_64 before;struct smb2fh *fh=NULL;int fd=-1,bad=1,known_temp=0;struct stat created;
  errno=0;rc=smb2_stat(smb,source,&before);if(rc){cp_failure("source.named-stat",rc,errno);goto done;}
  cp_remote_observe(&cp_diagnostic.before,&before);cp_diagnostic.remote_flags=1;
  if(before.smb2_type!=SMB2_TYPE_FILE||before.smb2_size!=r.size||before.smb2_mtime!=r.mtime_sec){cp_failure("source.named-metadata",0,0);goto done;}
  errno=0;fh=smb2_open(smb,source,O_RDONLY);if(!fh){cp_failure("source.readonly-open",0,errno);goto done;}
  if(!cp_smb_current(smb,fh,source,&before))goto done;
  n=snprintf(temp,sizeof temp,".blackwire-copy-%s-%u.tmp",p->cfg.run_id,i);if(n<0||(size_t)n>=sizeof temp){cp_failure("destination.temporary-name",n,0);goto done;}
  errno=0;if(!v2_directory_current(&p->cfg,root,parent,parent_rel)){cp_failure("destination.parent-current",0,errno);goto done;}
  mode_t mode=(mode_t)bw_startup_mode(r.path);
  errno=0;fd=v2_openat(parent,temp,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,mode);if(fd<0){cp_failure("destination.exclusive-open",fd,errno);goto done;}
  errno=0;rc=fstat(fd,&created);known_temp=!rc;if(!known_temp){cp_failure("destination.created-stat",rc,errno);goto done;}
  errno=0;rc=fchmod(fd,mode);if(rc){cp_failure("destination.mode-set",rc,errno);goto done;}
  uint8_t buffer[CP_BUFFER],hash[32];struct cp_sha sha;cp_sha_init(&sha);uint64_t at=0;
  while(at<r.size){uint32_t want=r.size-at<sizeof buffer?(uint32_t)(r.size-at):sizeof buffer;
    cp_diagnostic.offset=at;cp_diagnostic.requested=want;
    if(!cp_alive(p)){cp_failure("source.deadline",0,0);goto done;}
    errno=0;if(!v2_directory_current(&p->cfg,root,parent,parent_rel)){cp_failure("destination.copy-parent",0,errno);goto done;}
    errno=0;int got=smb2_pread(smb,fh,buffer,want,at);if(got<=0||(uint32_t)got>want){cp_failure("source.read",got,errno);goto done;}
    errno=0;rc=cp_write_all(fd,buffer,(size_t)got);if(rc){cp_failure("destination.write",rc,errno);goto done;}
    cp_sha_update(&sha,buffer,(size_t)got);at+=(uint64_t)got;p->copied_bytes+=(uint64_t)got;
  }
  cp_sha_final(&sha,hash);memcpy(cp_diagnostic.source_sha,hash,32);cp_diagnostic.hash_flags|=2;cp_diagnostic.offset=at;
  if(memcmp(hash,expected,32)){cp_failure("sourceSHA.mismatch",0,0);goto done;}
  errno=0;rc=fsync(fd);if(rc){cp_failure("readback.file-fsync",rc,errno);goto done;}
  struct stat copied;errno=0;rc=fstat(fd,&copied);if(rc){cp_failure("readback.file-stat",rc,errno);goto done;}
  cp_diagnostic.observed_mode=(uint32_t)copied.st_mode;cp_diagnostic.observed_size=(uint64_t)copied.st_size;
  cp_diagnostic.observed_links=(uint64_t)copied.st_nlink;cp_diagnostic.observed_inode=(uint64_t)copied.st_ino;cp_diagnostic.observed_device=(uint64_t)copied.st_dev;
  if((copied.st_mode&0777)!=mode){cp_failure("readback.mode",0,0);goto done;}
  errno=0;rc=cp_file_hash(fd,r.size,hash);if(rc){cp_failure("readback.read",rc,errno);goto done;}
  memcpy(cp_diagnostic.readback_sha,hash,32);cp_diagnostic.hash_flags|=4;
  if(memcmp(hash,expected,32)){cp_failure("readbackSHA.mismatch",0,0);goto done;}
  if(!cp_smb_current(smb,fh,source,&before))goto done;
  errno=0;rc=cp_refresh(p);if(rc){cp_failure("sourceGeneration.lease",rc,errno);goto done;}
  if(cp_publish(&p->cfg,root,parent,parent_rel,temp,leaf,fd,r.size,mode,expected,p->deadline))goto done;
  bad=0;
done:
  if(bad&&known_temp)cp_diagnostic.cleanup_result=cp_cleanup_temp(&p->cfg,root,parent,parent_rel,temp,fd,&created);
  if(fd>=0)close(fd);if(fh){errno=0;rc=smb2_close(smb,fh);if(rc){cp_failure("source.close",rc,errno);bad=1;}}close(parent);
  return bad?v2_fail("Startup copy failed source, digest, identity, or no-replace checks; existing files were preserved"):0;
}
#endif
#ifndef BW_COPY_HOST_TEST
static void cp_json_string(FILE *fp,const char *s) {fputc('"',fp);for(;*s;s++){unsigned char c=(unsigned char)*s;if(c=='"'||c=='\\')fputc('\\',fp);if(c<32)fprintf(fp,"\\u%04x",c);else fputc(c,fp);}fputc('"',fp);}
static int cp_status(const struct cp_plan *p,int datafd,const char *phase,const char *message) {
  struct prepare_v2 anchor;memset(&anchor,0,sizeof anchor);if(v2_copy(anchor.root,sizeof anchor.root,BW_COPY_DATA)||!v2_root_current(&anchor,datafd))return -1;
  char temp[96];snprintf(temp,sizeof temp,"prepare-copy-status.%d.tmp",(int)getpid());
  int fd=v2_openat(datafd,temp,O_CREAT|O_EXCL|O_NOFOLLOW|O_WRONLY,0644);if(fd<0)return -1;FILE *fp=fdopen(fd,"w");if(!fp){close(fd);return -1;}
  fputs("{\"runId\":",fp);cp_json_string(fp,p->cfg.run_id);fputs(",\"titleId\":",fp);cp_json_string(fp,p->cfg.title);fputs(",\"game\":",fp);cp_json_string(fp,p->cfg.game);
  fputs(",\"manifestHash\":",fp);cp_json_string(fp,p->cfg.hash_text);fputs(",\"action\":",fp);cp_json_string(fp,p->phase);fputs(",\"phase\":",fp);cp_json_string(fp,phase);fputs(",\"message\":",fp);cp_json_string(fp,message);
  fprintf(fp,",\"localCount\":%u,\"verifiedCount\":%u,\"copiedBytes\":%"PRIu64,p->local_count,p->verified_count,p->copied_bytes);
  if(!strcmp(phase,"error"))cp_diagnostic_json(fp);fputc('}',fp);
  int bad=ferror(fp)||fflush(fp)||fsync(fd);if(fclose(fp))bad=1;
  return bad||!v2_root_current(&anchor,datafd)?-1:cp_renameat(datafd,temp,datafd,"prepare-copy-status.json");
}
/* Validate PC-built index rows against the immutable manifest and exact
 * console metadata through held parent descriptors before publishing it. */
#endif
/* The normal Ready-Play admission uses the same complete in-memory format,
 * path/size and hash-slot validation without reopening every prepared file.
 * Strict Send/repair callers continue through cp_index_rows below. */
static int cp_index_rows_mode(struct cp_plan *p,int root,int inspect_files) {
  const uint8_t *b=p->index;size_t size=p->index_size;uint8_t hash[32];cp_sha_bytes(b,size,hash);
  if(size<48||memcmp(hash,p->index_sha,32)||memcmp(b,"AMPRIDX3",8)||cp_u32(b+8)!=3||cp_u32(b+12)!=24||cp_u64(b+16)!=p->manifest.count)return -1;
  uint64_t pool=cp_u64(b+24),slots_at=cp_u64(b+32);uint32_t slot_size=cp_u32(b+40),slots=cp_u32(b+44);
  uint64_t paths_at=48u+24u*p->manifest.count;
  if(pool>32u*1024u*1024u||paths_at+pool>size||slot_size!=16||slots<2||slots>524288||(slots&(slots-1))||
     slots_at!=((paths_at+pool+15)&~UINT64_C(15))||slots_at+(uint64_t)slots*16!=size)return -1;
  uint8_t *seen=calloc(p->manifest.count,1);if(!seen)return -1;int bad=0;uint64_t cursor=0;
  for(uint32_t i=0;i<p->manifest.count&&!bad;i++){
    const uint8_t *entry=b+48u+24u*i;uint32_t offset=cp_u32(entry),length=cp_u32(entry+4);struct bw_manifest_record r;
    if(bw_manifest_get(&p->manifest,i,&r)||offset!=cursor||offset>pool||length>=pool-offset||b[paths_at+offset+length]||length!=6u+r.path_length||
       memcmp(b+paths_at+offset,"/app0/",6)||memcmp(b+paths_at+offset+6,r.path,r.path_length)||cp_u64(entry+8)!=r.size){bad=1;break;}
    if(inspect_files){
      char leaf[256],parent_rel[1024];int parent=v2_parent(&p->cfg,root,r.path,0,leaf,parent_rel);if(parent<0){bad=1;break;}
      int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);struct stat st;
      if(fd<0||!v2_directory_current(&p->cfg,root,parent,parent_rel)||cp_regular(parent,leaf,fd,r.size,&st)||(uint64_t)st.st_mtime!=cp_u64(entry+16)||
         (strcmp(p->phase,"copy")&&r.flags==BW_MANIFEST_LOCAL&&(bw_startup_mode(r.path)&0111)&&(st.st_mode&0111)!=0111))bad=1;
      if(fd>=0)close(fd);close(parent);
    }
    cursor+=(uint64_t)length+1;
  }
  if(cursor!=pool)bad=1;
  for(uint64_t i=paths_at+pool;i<slots_at;i++)if(b[i])bad=1;
  /* Prove each hash slot references exactly one matching row, including the
   * collision marker and open-addressing reachability used by AMPRIDX3. */
  for(uint32_t j=0;j<slots&&!bad;j++){
    const uint8_t *s=b+slots_at+16u*j;uint64_t value=cp_u64(s);uint32_t record=cp_u32(s+8),flags=cp_u32(s+12);
    if(!record){if(value||flags)bad=1;continue;}if(record>p->manifest.count||seen[record-1]++||flags>1){bad=1;break;}
    const uint8_t *entry=b+48u+24u*(record-1),*path=b+paths_at+cp_u32(entry);uint32_t length=cp_u32(entry+4);uint64_t actual=UINT64_C(1469598103934665603);
    for(uint32_t k=0;k<length;k++){uint8_t c=path[k];if(c>='A'&&c<='Z')c+=32;actual=(actual^c)*UINT64_C(1099511628211);}if(!actual)actual=1;
    if(value!=actual){bad=1;break;}uint32_t at=(uint32_t)value&(slots-1),collisions=0;
    while(at!=j){const uint8_t *prior=b+slots_at+16u*at;if(!cp_u32(prior+8)){bad=1;break;}if(cp_u64(prior)==value)collisions++;at=(at+1)&(slots-1);}
    if(collisions&&!flags)bad=1;
  }
  for(uint32_t i=0;i<p->manifest.count;i++)if(seen[i]!=1)bad=1;free(seen);
  return bad?v2_fail("Prepared index does not match the selected console files"):0;
}
static int cp_index_rows(struct cp_plan *p,int root) {return cp_index_rows_mode(p,root,1);}
static int cp_preflight(struct cp_plan *p,int root) {
  /* No game-file or directory writes occur in this pass. Conflicting late
   * LOCAL records, STREAM placeholders, manifest and existing index all stop
   * the operation before copying the first missing startup file. */
  uint8_t manifest_sha[32];cp_sha_bytes(p->map,p->map_size,manifest_sha);
  int meta=cp_existing(&p->cfg,root,root,"","blackwire.files",p->map_size,manifest_sha);if(meta<0)return -1;
  for(uint32_t i=0;i<p->manifest.count;i++){
    struct bw_manifest_record r;char leaf[256],parent_rel[1024];if(bw_manifest_get(&p->manifest,i,&r))return -1;
    int parent=v2_parent(&p->cfg,root,r.path,0,leaf,parent_rel);if(parent==-2)continue;if(parent<0)return -1;
    int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(fd<0){int missing=errno==ENOENT;close(parent);if(missing)continue;return v2_fail("An existing destination is linked or unavailable");}
    struct stat st;int bad=cp_regular(parent,leaf,fd,r.size,&st)||(!strcmp(r.path,"eboot.bin")&&!(st.st_mode&0111));close(fd);
    if(!bad&&r.flags==BW_MANIFEST_LOCAL)bad=cp_existing(&p->cfg,root,parent,parent_rel,leaf,r.size,p->digests+56u+32u*i)!=0;
    close(parent);if(bad)return v2_fail("Existing startup or streamed file differs; no files were copied");
  }
  int fd=v2_openat(root,"ampr_emu.index",O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0)return errno==ENOENT?0:v2_fail("An existing index is linked or unavailable");
  struct stat st;int bad=fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<48||st.st_size>CP_MAX_INDEX;
  if(bad){close(fd);return v2_fail("The existing index is not a bounded regular file");}
  uint8_t *prior=p->index,prior_sha[32];size_t prior_size=p->index_size;memcpy(prior_sha,p->index_sha,32);
  p->index_size=(size_t)st.st_size;p->index=malloc(p->index_size);
  bad=!p->index||cp_regular(root,"ampr_emu.index",fd,p->index_size,NULL)||v2_read_all(fd,p->index,p->index_size)||cp_regular(root,"ampr_emu.index",fd,p->index_size,NULL);
  if(!bad){cp_sha_bytes(p->index,p->index_size,p->index_sha);bad=cp_index_rows(p,root);}
  close(fd);free(p->index);p->index=prior;p->index_size=prior_size;memcpy(p->index_sha,prior_sha,32);
  return bad?v2_fail("The existing index differs from this source and console metadata; no files were copied"):0;
}
#if !defined(BW_COPY_HOST_TEST) && !defined(BW_COPY_EMBEDDED)
int sceNetInit(void);long __crt_syscall(long,...);
int main(void) {
  struct cp_plan p;memset(&p,0,sizeof p);p.deadline=cp_now()+1800;int result=1,datafd=-1,root=-1,games=-1;struct smb2_context *smb=NULL;
  __crt_syscall(464,(long)-1,"bw-preparecopy");
  struct prepare_v2 data_cfg;memset(&data_cfg,0,sizeof data_cfg);snprintf(data_cfg.root,sizeof data_cfg.root,"%s",BW_COPY_DATA);
  datafd=v2_open_root(&data_cfg);if(datafd<0)return 1;
  if(cp_share_field(p.server,sizeof p.server,"server",1)||sceNetInit()<0)goto done;
  uint8_t *active=NULL;size_t active_size=0;if(cp_fetch(&p,"/api/preparation/active",4096,&active,&active_size))goto done;
  int parsed=cp_parse(&p,active,active_size);free(active);if(parsed)goto done;
  if(cp_status(&p,datafd,"preparing","Validating selected source and anchored console files")||cp_games_stopped(BW_COPY_GAME_LOG)||
    cp_resource(&p,"manifest",p.map_size,&p.map)||cp_resource(&p,"digests",p.digest_size,&p.digests)||cp_validate_map(&p))goto done;
  /* Root creation is anchored below the held Blackwire directory. Existing
   * ancestors must already be canonical; only this selected leaf is created. */
  games=v2_openat(datafd,"games",O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);if(games<0)goto done;
  struct prepare_v2 games_cfg;memset(&games_cfg,0,sizeof games_cfg);snprintf(games_cfg.root,sizeof games_cfg.root,"%s",BW_GAMES_ROOT);
  if(!v2_root_current(&data_cfg,datafd)||!v2_root_current(&games_cfg,games))goto done;
  root=v2_openat(games,p.cfg.game,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
  if(root<0&&errno==ENOENT&&!strcmp(p.phase,"copy")){if(cp_refresh(&p)||cp_games_stopped(BW_COPY_GAME_LOG)||v2_mkdirat(games,p.cfg.game,0755))goto done;root=v2_openat(games,p.cfg.game,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);if(root>=0&&fchmod(root,0755))goto done;}
  if(root<0||!v2_root_current(&p.cfg,root)||!v2_root_current(&games_cfg,games)||cp_preflight(&p,root))goto done;
  if(!strcmp(p.phase,"copy")){
    char share[128],user[128],password[128];if(cp_share_field(share,sizeof share,"share",1)||cp_share_field(user,sizeof user,"user",1)||cp_share_field(password,sizeof password,"password",0))goto done;
    smb=smb2_init_context();if(!smb){memset(password,0,sizeof password);goto done;}smb2_set_timeout(smb,10);smb2_set_user(smb,user);smb2_set_password(smb,password);smb2_set_sign(smb,1);smb2_set_security_mode(smb,SMB2_NEGOTIATE_SIGNING_ENABLED|SMB2_NEGOTIATE_SIGNING_REQUIRED);
    int connected=smb2_connect_share(smb,p.server,share,user);memset(password,0,sizeof password);if(connected<0)goto done;
    double progress_at=cp_now();
    for(uint32_t i=0;i<p.manifest.count;i++){
      struct bw_manifest_record r;if(bw_manifest_get(&p.manifest,i,&r))goto done;
      if(r.flags==BW_MANIFEST_LOCAL){if(cp_source_file(&p,smb,root,i))goto done;p.verified_count++;}
      double now=cp_now();if(now<0)goto done;if(now-progress_at>=1){if(cp_status(&p,datafd,"preparing","Verifying existing and missing LOCAL files; sources are read only"))goto done;progress_at=now;}
    }
    if(cp_generation(&p)||!v2_root_current(&data_cfg,datafd)||cp_blob(&p.cfg,root,"blackwire.files",p.map,p.map_size,p.cfg.run_id,p.deadline)||
      cp_control(&p,datafd,"stream.conf",p.selection))goto done;
    char verify[2100];snprintf(verify,sizeof verify,"%smode=verify\n",p.selection);
    if(cp_control(&p,datafd,"register-v2.conf",verify)||cp_generation(&p)||cp_status(&p,datafd,"copied","Every LOCAL file and manifest verified; sparse preparation is still required"))goto done;
  }else{
    if(cp_resource(&p,"index",p.index_size,&p.index)||cp_index_rows(&p,root)||cp_generation(&p)||
       cp_blob(&p.cfg,root,"ampr_emu.index",p.index,p.index_size,p.cfg.run_id,p.deadline)||cp_generation(&p)||cp_receipt(&p,datafd,root)||
       cp_status(&p,datafd,"indexed","Selected index and console metadata verified; registration verification is still required"))goto done;
  }
  result=0;
done:
  if(result&&*p.cfg.run_id)cp_status(&p,datafd,"error",prepare_v2_error);
  if(smb)smb2_destroy_context(smb);if(root>=0)close(root);if(games>=0)close(games);free(p.map);free(p.digests);free(p.index);close(datafd);return result;
}
#endif
