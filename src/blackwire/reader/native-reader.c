/* Experimental general file-map reader with separate verified native read adapters.
 * Blackwire bounded streaming reader. Preserved PC files are opened read-only.
 * Native AIO receives a private request copy pointing at bounded staging data.
 * All native completions must finish before that staging area can be reused.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <time.h>
#include <unistd.h>
#include <ps5/kernel.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include "cache.h"
#include "protocol.h"
#include "filemap.h"
#include "reader_source.h"
#include "reader_selection.h"
#include "reader_preflight.h"
#include "launch_timing.h"
#include "native_setup.h"
#include "reader_runtime.h"
#include "native_launch_admission.h"
#include "native_projection_handoff.h"
#include "native_registration.h"
#include "owned_vm_window.h"
#include "reader_user.h"
#include "path_protocol.h"
#include "read_plan.h"
#include "native_profile.h"
#include "lifecycle.h"
#include "stage_identity.h"
#ifdef BW_NATIVE_PRELOAD
#include "preload_bundle.h"
#include "preload_launch.h"
#include "startup_runtime_idle.h"
#endif

static int bw_reader_has_prepared_target(const struct bw_reader_selection *s) {
  return bw_reader_selection_is_derived(s) || bw_reader_selection_is_batch(s);
}

/* Private reader boundary only. The SDK result cannot certify a full copy.
 * Every positive SDK subcall fits one remote 4KiB virtual page and its caller
 * buffer slice. No retry, zero-fill, SDK mutation or admission change. Caller
 * owns a valid buffer of len bytes; integer checks do not validate C pointers. */
static int bw_reader_copy_range(intptr_t address,const void *buffer,size_t length) {
  if (!length) return 0;
  if (!buffer || length-1 > UINTPTR_MAX-(uintptr_t)address ||
      length-1 > UINTPTR_MAX-(uintptr_t)buffer) return -1;
  return 0;
}
static int bw_reader_proc_copyin(pid_t pid,const void *buffer,intptr_t address,size_t length) {
  if (bw_reader_copy_range(address,buffer,length)) return -1;
  size_t done=0;
  while (done<length) {
    uintptr_t current=(uintptr_t)address+done;
    size_t chunk=4096u-(size_t)(current&4095u);
    if (chunk>length-done) chunk=length-done;
    int result=kernel_proc_copyin(pid,(const unsigned char *)buffer+done,(intptr_t)current,chunk);
    if (result) return result;
    done+=chunk;
  }
  return 0;
}
static int bw_reader_proc_copyout(pid_t pid,intptr_t address,void *buffer,size_t length) {
  if (bw_reader_copy_range(address,buffer,length)) return -1;
  size_t done=0;
  while (done<length) {
    uintptr_t current=(uintptr_t)address+done;
    size_t chunk=4096u-(size_t)(current&4095u);
    if (chunk>length-done) chunk=length-done;
    int result=kernel_proc_copyout(pid,(intptr_t)current,(unsigned char *)buffer+done,chunk);
    if (result) return result;
    done+=chunk;
  }
  return 0;
}

#ifdef BW_OWNED_VM
#include "owned_vm.h"
extern unsigned long KERNEL_OFFSET_VMSPACE_P_ROOT;
_Static_assert(sizeof(unsigned long)==8,"SDK kernel pointer width");
static const char *owner_guard_site="none";
#define CACHE_OWNER_GUARD(site,expected,out) (owner_guard_site=(site),cache_owner_guard(expected,out))
#else
#define CACHE_OWNER_GUARD(site,expected,out) cache_owner_guard(expected,out)
#endif

#define STAGE_SIZE (256ULL * 1024 * 1024)
#define MAX_READ (64ULL * 1024 * 1024)
#define SLOT_SIZE 32768u
#define STAT_AT 512u
#define PATH_AT 1024u
#define ITEM_AT 4096u
#define STATE_AT 256u
#define CACHE_PATH "/data/blackwire/cache/stream-v1.bin"
#define STATUS_PATH "/data/blackwire/native-reader-status.json"
#define SCE_EIO ((int32_t)0x80020005u)
_Static_assert(ITEM_AT + 128*sizeof(struct bw_aio_item) <= SLOT_SIZE, "shadow capacity");
_Static_assert(ITEM_AT + BW_READ_MAX_IOV*sizeof(struct bw_read_iovec) <= SLOT_SIZE, "iovec capacity");
_Static_assert(STAT_AT + sizeof(struct stat) <= PATH_AT, "stat scratch");
_Static_assert(STAT_AT + BW_STAGE_CHALLENGE_BYTES <= PATH_AT, "challenge scratch");
_Static_assert(STAGE_SIZE == BW_STAGE_IDENTITY_CAPACITY, "fixed proof capacity");


static struct bw_reader_runtime reader_runtime=BW_READER_RUNTIME_INITIALIZER;
static pthread_mutex_t reader_gate=PTHREAD_MUTEX_INITIALIZER;
static int reader_launch_issued;
static struct bw_vm_window_state reader_vm_stable;
static unsigned reader_vm_reports;
static struct bw_files file_map;
static struct bw_reader_source reader_source;
static struct bw_derived_reader_source reader_derived_source;
static struct bw_derived_reader_claim reader_derived_claim;
static int reader_derived_admitted;
static struct bw_reader_container reader_container;
static struct bw_reader_selection reader_selection;
static struct bw_native_launch_admission *reader_startup;
static const char *reader_startup_failure;
static unsigned reader_io_released;
static struct bw_setup_generation reader_connection_generation;
#ifndef BW_NATIVE_READER_EMBEDDED
static const char *reader_selection_path="/data/blackwire/native-reader.conf";
#endif
static char source_name[BW_MANIFEST_MAX_PATH+1],source_binding_hex[65];
static uint64_t manifest_hash;
static uint32_t manifest_count;
static char game_name[256], title_id[16], run_id[40], game_root[768];
static uint64_t eboot_size, timeout_ticks, launch_us, first_batch_us;
static FILE *logfp;
#ifdef BW_NATIVE_PRELOAD
/* Process-private stable address; creator descriptors are never serialized,
 * copied into a receipt or recycled after an uncertain close. */
static struct bw_preload_launch reader_preloaded;
static int reader_early_active;
#endif
static struct smb2_context *smb;
static pid_t game_pid=-1;
static uintptr_t slot_addr, ampr_base;
struct read_route { const char *name; unsigned profile; uintptr_t offset,code,pointer,saved; uint64_t operations; };
static struct read_route routes[BW_PATH_ROUTE_COUNT]={
  {"sceKernelAioSubmitReadCommandsMultiple",BW_NATIVE_AIO_MULTIPLE,0x64aa8,0,0,0,0},
  {"sceKernelAioSubmitReadCommands",BW_NATIVE_AIO_SINGLE,0x64aa0,0,0,0,0},
  {"pread",BW_NATIVE_PREAD,0x64a80,0,0,0,0},
  {"preadv",BW_NATIVE_PREADV,0x64a88,0,0,0,0},
  {"read",BW_NATIVE_READ,0x64a90,0,0,0,0},
  {"readv",BW_NATIVE_READV,0x64a98,0,0,0,0},
  {"lseek",BW_NATIVE_LSEEK,0x64a68,0,0,0,0}
};
static uintptr_t native_poll, native_open, native_fstat, native_close, native_errno;
static uint64_t network_bytes, hit_bytes, staged_bytes, high_water;
static int stage_fd=-1, game_cache_fd=-1;
static struct bw_stage_proof stage_proof;
static unsigned cache_challenge_attempted,native_read_submitted;
static struct stat stage_stat;
static struct bw_cache memory_cache;
static unsigned aio_batches,armed_routes,binding_write_attempted;
static unsigned cache_diagnostics;
static unsigned cache_check_kind=UINT32_MAX;
static const char *cache_check_stage="prepare";
static uint64_t request_started_us;
static uint64_t progress_report_us, idle_report_us;
static unsigned idle_reports;
static uintptr_t request_errno_pointer;
static int profile_verbose;
static const char *phase="preflight";
static char last_message[256]="Preparing stream";

typedef struct { uint32_t size,user,opts; uint64_t crash; uint32_t check; } launch_ctx;
int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(uint32_t *);
int sceSystemServiceLaunchApp(const char *,char **,launch_ctx *);
int sceNetInit(void);
long __crt_syscall(long,...);

static uint64_t monotonic_us(void) {
  struct timespec now;
  if (clock_gettime(CLOCK_MONOTONIC,&now)) return 0;
  return (uint64_t)now.tv_sec*1000000 + (uint64_t)now.tv_nsec/1000;
}
static struct timespec diagnostic_wall_time(void) {
  struct timespec now={0}; int saved_errno=errno;
  if (clock_gettime(CLOCK_REALTIME,&now)) memset(&now,0,sizeof now);
  errno=saved_errno; return now;
}
static void note(const char *message) {
  fprintf(logfp,"%s\n",message); fflush(logfp);
}
/* Bounded failure leaves only: no extra game calls, retries, or policy changes.
 * Arguments below are scalar metadata/addresses, never file contents. */
static int cache_diagnostic(const char *check,const char *format,...) {
  int saved_errno=errno;
  if (cache_diagnostics++<16) {
    struct timespec wall=diagnostic_wall_time();
    char detail[640]; va_list args;
    va_start(args,format); vsnprintf(detail,sizeof detail,format,args); va_end(args);
    uint64_t now=monotonic_us();
#ifdef BW_OWNED_VM
    fprintf(logfp,"cache guard ownerSite=%s wallSec=%lld wallNs=%09ld operation=%u kind=%u stage=%s gamePid=%d elapsedUs=%llu check=%s %s\n",
      owner_guard_site,(long long)wall.tv_sec,wall.tv_nsec,aio_batches,cache_check_kind,cache_check_stage,(int)game_pid,
      (unsigned long long)(request_started_us && now>=request_started_us?now-request_started_us:0),check,detail);
#else
    fprintf(logfp,"cache guard wallSec=%lld wallNs=%09ld operation=%u kind=%u stage=%s gamePid=%d elapsedUs=%llu check=%s %s\n",
      (long long)wall.tv_sec,wall.tv_nsec,aio_batches,cache_check_kind,cache_check_stage,(int)game_pid,
      (unsigned long long)(request_started_us && now>=request_started_us?now-request_started_us:0),check,detail);
#endif
    fflush(logfp);
  }
  errno=saved_errno;
  return -1;
}
static void json_string(FILE *fp,const char *s) {
  fputc('"',fp);
  for (;*s;s++) {
    unsigned char c=(unsigned char)*s;
    if (c=='"' || c=='\\') { fputc('\\',fp); fputc(c,fp); }
    else if (c<32) fprintf(fp,"\\u%04x",c);
    else fputc(c,fp);
  }
  fputc('"',fp);
}
static void status_write(const char *next,const char *message) {
  char temp[160];
  phase=next;
  if (message) snprintf(last_message,sizeof last_message,"%s",message);
  const struct bw_reader_event event={phase,last_message,game_pid,network_bytes,hit_bytes,high_water};
  bw_reader_runtime_observe(&reader_runtime,&event);
  snprintf(temp,sizeof temp,"/data/blackwire/native-reader-status.%d.tmp",(int)getpid());
  FILE *fp=fopen(temp,"w");
  if (!fp) return;
  fputs("{\"runId\":",fp); json_string(fp,run_id);
  fputs(",\"titleId\":",fp); json_string(fp,title_id);
  fputs(",\"game\":",fp); json_string(fp,game_name);
  fputs(",\"phase\":",fp); json_string(fp,phase);
  fputs(",\"message\":",fp); json_string(fp,last_message);
#ifdef BW_NATIVE_PRELOAD
  if(reader_early_active) {
    /* The SMB/cache owner now lives in the game. This controller has no IPC
     * counter witness: zero must not be represented as observed delivery. */
    fprintf(fp,",\"gamePid\":%d,\"readerPid\":%d,\"controllerPid\":%d,\"readerMode\":\"preloaded\","
            "\"networkBytes\":null,\"cacheHitBytes\":null,\"dataCountersObserved\":false,"
            "\"memoryCacheBudget\":67108864,\"stageBytes\":0,\"stageHighWater\":0,\"stagedBytes\":0}",
            game_pid>0?(int)game_pid:0,game_pid>0?(int)game_pid:0,(int)getpid());
    if(fclose(fp)==0)rename(temp,STATUS_PATH);
    return;
  }
#endif
  fprintf(fp,",\"gamePid\":%d,\"readerPid\":%d,\"networkBytes\":%llu,\"cacheHitBytes\":%llu,"
          "\"cacheBytes\":%llu,\"stageBytes\":%llu,\"stageHighWater\":%llu,\"stagedBytes\":%llu}",
          game_pid>0?(int)game_pid:0,(int)getpid(),(unsigned long long)network_bytes,
          (unsigned long long)hit_bytes,(unsigned long long)(BW_CACHE_BLOCK*BW_CACHE_SLOTS),
          (unsigned long long)STAGE_SIZE,(unsigned long long)high_water,(unsigned long long)staged_bytes);
  if (fclose(fp)==0) rename(temp,STATUS_PATH);
}
static int failure(const char *message) {
  int saved_errno=errno; struct timespec wall=diagnostic_wall_time();
  uint64_t now=monotonic_us();
  fprintf(logfp,"terminal failure wallSec=%lld wallNs=%09ld operation=%u kind=%u stage=%s gamePid=%d elapsedUs=%llu\n",
    (long long)wall.tv_sec,wall.tv_nsec,aio_batches,cache_check_kind,cache_check_stage,(int)game_pid,
    (unsigned long long)(request_started_us && now>=request_started_us?now-request_started_us:0));
  note(message); status_write("error",message); errno=saved_errno; return -1;
}


static int load_config(void) {
  struct bw_reader_selection selected;
  if (bw_reader_runtime_selection(&reader_runtime,&selected,&reader_connection_generation))
    return failure("Invalid source-bound reader selection");
  reader_selection=selected;
  memcpy(source_name,selected.source,sizeof source_name);
  memcpy(source_binding_hex,selected.binding_hex,sizeof source_binding_hex);
  strcpy(game_name,bw_reader_has_prepared_target(&selected)?selected.prepared_game:selected.game); strcpy(title_id,selected.title); strcpy(run_id,selected.run);
  eboot_size=selected.eboot_size; manifest_count=selected.manifest_count; manifest_hash=selected.manifest_hash;
  int n=snprintf(game_root,sizeof game_root,"/data/blackwire/games/%s/",game_name);
  return n<0 || (size_t)n>=sizeof game_root?failure("Invalid source-bound game root"):0;
}


static int remote_call(pid_t pid, uint64_t sysno, uint64_t args[6], uint64_t *out) {
  register uint64_t r10 __asm__("r10") = 0;
  register uint64_t r8 __asm__("r8") = 0;
  register uint64_t r9 __asm__("r9") = 0;
  uint64_t value;
  unsigned char error;
  __asm__ __volatile__("syscall"
      : "=a"(value), "=@ccc"(error), "+r"(r10), "+r"(r8), "+r"(r9)
      : "a"(UINT64_C(0x500000027)), "D"((uint64_t)(uint32_t)pid),
        "S"(sysno), "d"((uint64_t)args) : "rcx", "r11", "memory");
  if (out) *out = value;
  return error ? -1 : 0;
}

static uint64_t tsc(void) {
  uint32_t lo, hi;
  __asm__ __volatile__("lfence; rdtsc" : "=a"(lo), "=d"(hi) : : "memory");
  return ((uint64_t)hi << 32) | lo;
}

static int calibrate_timeout(void) {
  struct timespec first, last;
  uint64_t a, b;
  int64_t ns;
  if (clock_gettime(CLOCK_MONOTONIC, &first) != 0) return -1;
  a = tsc();
  usleep(100000);
  b = tsc();
  if (clock_gettime(CLOCK_MONOTONIC, &last) != 0) return -1;
  ns = (last.tv_sec - first.tv_sec) * 1000000000LL + last.tv_nsec - first.tv_nsec;
  if (ns <= 0 || b <= a) return -1;
  timeout_ticks = (uint64_t)((double)(b - a) * 30000000000.0 / (double)ns);
  return timeout_ticks >= 30000000ULL && timeout_ticks <= 300000000000ULL ? 0 : -1;
}

static int read_exact(struct smb2fh *remote, uint8_t *buf, uint32_t size, uint64_t offset) {
  uint32_t done = 0;
  while (done < size) {
    int got = smb2_pread(smb, remote, buf + done, size - done, offset + done);
    if (got <= 0 || (uint32_t)got > size - done) return -1;
    done += (uint32_t)got;
  }
  return 0;
}

static int write_exact(int fd, const uint8_t *buf, uint32_t size, uint64_t offset) {
  uint32_t done = 0;
  while (done < size) {
    ssize_t got = pwrite(fd, buf + done, size - done, (off_t)(offset + done));
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0 || (uint64_t)got > size - done) return -1;
    done += (uint32_t)got;
  }
  return 0;
}

static uintptr_t pread_address(pid_t pid) {
  const char *names[] = { "libkernel_sys.sprx", "libkernel.sprx" };
  for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
    uint32_t handle;
    if (kernel_dynlib_handle(pid, names[i], &handle) == 0) {
      intptr_t addr = kernel_dynlib_dlsym(pid, handle, "sceKernelPread");
      if (addr) return (uintptr_t)addr;
    }
  }
  return 0;
}

/* Profile reads and export resolution always use this exact selected PID. */
static int profile_read(void *opaque,uint64_t address,void *bytes,size_t size) {
  (void)opaque; return bw_reader_proc_copyout(game_pid,address,bytes,size);
}
static int current_profile(struct bw_native_profile *profile,uint32_t *module) {
  uint32_t handle,ampr_handle;
  if (kernel_dynlib_handle(game_pid,"libSceAmpr.sprx",&ampr_handle) ||
      (uintptr_t)kernel_dynlib_mapbase_addr(game_pid,ampr_handle)!=ampr_base) return -1;
  if (kernel_dynlib_handle(game_pid,"libkernel.sprx",&handle)) return -1;
  uintptr_t base=(uintptr_t)kernel_dynlib_mapbase_addr(game_pid,handle);
  if (base<0x10000) return -1;
  *profile=(struct bw_native_profile){profile_read,NULL,ampr_base,base,"libkernel.sprx"};
  if (module) *module=handle;
  return 0;
}
static int validate_profile_layout(void) {
  struct bw_native_profile profile;
  return !current_profile(&profile,NULL) && bw_native_check_ampr(&profile)==BW_NATIVE_OK;
}
/* BW_NATIVE_ADMISSION_RETRY_BEGIN */
struct native_admission_identity {
  pid_t pid;
  uint32_t kernel;
  uintptr_t ampr,kernel_base,entry;
};
static int native_admission_cancelled(void) {
  pthread_mutex_lock(&reader_runtime.lock);
  int stopped=!reader_runtime.active || reader_runtime.cancelled;
  pthread_mutex_unlock(&reader_runtime.lock);
  return stopped;
}
static int native_admission_identity(const char *symbol,struct bw_native_profile *profile,
                                     struct native_admission_identity *out) {
  out->pid=game_pid; out->ampr=ampr_base;
  if (out->pid<=0 || native_admission_cancelled() || current_profile(profile,&out->kernel)) return -1;
  out->kernel_base=(uintptr_t)profile->libkernel_base;
  out->entry=(uintptr_t)kernel_dynlib_dlsym(out->pid,out->kernel,symbol);
  return !out->entry || game_pid!=out->pid || ampr_base!=out->ampr || native_admission_cancelled()?-1:0;
}
static int native_admission_same(const struct native_admission_identity *a,
                                 const struct native_admission_identity *b) {
  return a->pid==b->pid && a->ampr==b->ampr && a->kernel==b->kernel &&
    a->kernel_base==b->kernel_base && a->entry==b->entry;
}
static int validate_profile_target(unsigned role,uintptr_t target,const char *symbol) {
  uint64_t start=monotonic_us(),last=start;
  struct native_admission_identity anchor={0};
  if (!start || start>UINT64_MAX-UINT64_C(20000)) return 0;
  for (unsigned attempt=0;attempt<3;attempt++) {
    uint64_t now=monotonic_us();
    if (!now || now<last || now-start>=UINT64_C(20000) || native_admission_cancelled()) return 0;
    last=now;
    if (attempt && (game_pid!=anchor.pid || ampr_base!=anchor.ampr)) return 0;
    struct bw_native_profile profile,after_profile;
    struct native_admission_identity before,after;
    if (native_admission_identity(symbol,&profile,&before)) {
      fprintf(logfp,"native admission unavailable role=%u symbol=%s target=%llx gamePid=%d attempt=%u\n",
        role,symbol,(unsigned long long)target,(int)game_pid,attempt+1); fflush(logfp); return 0;
    }
    if (!attempt) anchor=before;
    else if (!native_admission_same(&anchor,&before)) return 0;
    int protection=target>=0x10000?kernel_get_vmem_protection(before.pid,target,1):-1;
    enum bw_native_error checked=bw_native_check_target(&profile,(enum bw_native_role)role,symbol,before.entry,target);
    if (profile_verbose || role==BW_NATIVE_AIO_POLL || protection<0 || !(protection&4) || checked!=BW_NATIVE_OK) {
      fprintf(logfp,"native admission role=%u symbol=%s entry=%llx target=%llx protection=%x result=%s attempt=%u\n",
        role,symbol,(unsigned long long)before.entry,(unsigned long long)target,protection,bw_native_error_text(checked),attempt+1); fflush(logfp);
    }
    if (game_pid!=anchor.pid || ampr_base!=anchor.ampr ||
        native_admission_identity(symbol,&after_profile,&after) || !native_admission_same(&anchor,&after)) return 0;
    now=monotonic_us();
    if (!now || now<last || now-start>=UINT64_C(20000) || native_admission_cancelled()) return 0;
    last=now;
    if (checked!=BW_NATIVE_OK || protection>=0) return protection>=0 && (protection&4) && checked==BW_NATIVE_OK;
    if (attempt==2) {
      fprintf(logfp,"native admission negative protection query exhausted role=%u attempts=3\n",role); fflush(logfp); return 0;
    }
    if (usleep(1000)) return 0;
  }
  return 0;
}
/* BW_NATIVE_ADMISSION_RETRY_END */

static uintptr_t native_helper(enum bw_native_role role) {
  struct bw_native_profile profile; uint32_t handle; uintptr_t target=0;
  if (current_profile(&profile,&handle)) return 0;
  const char *symbol=bw_native_symbol(role); uintptr_t offset=bw_native_slot_offset(role);
  uintptr_t entry=(uintptr_t)kernel_dynlib_dlsym(game_pid,handle,symbol);
  if (offset) {
    if (bw_reader_proc_copyout(game_pid,ampr_base+offset,&target,sizeof target)) return 0;
  } else target=entry;
  return validate_profile_target((unsigned)role,target,symbol)?target:0;
}
static int route_valid(unsigned kind,uintptr_t target) {
  if (kind>=BW_PATH_ROUTE_COUNT) return 0;
  return validate_profile_target(routes[kind].profile,target,routes[kind].name);
}
#ifdef BW_READER_READY
struct ready_modules {
  pid_t pid;
  uint32_t ampr,kernel;
  uintptr_t ampr_base,kernel_base,multiple;
};
static struct ready_modules ready_anchor;
static uintptr_t ready_native;
static int ready_pinned,ready_reported;
static unsigned ready_deferred_reports;
/* BW_READY_WINDOW_DIAGNOSTIC_BEGIN: observations never affect admission. */
struct ready_window_diagnostic {
  uint64_t address,offset;
  size_t length,captured;
  int present,result;
  unsigned char bytes[32];
};
static struct ready_window_diagnostic ready_window;
static int ready_read(void *opaque,uint64_t address,void *dest,size_t size) {
  const struct ready_modules *modules=opaque;
  int result=bw_reader_proc_copyout(modules->pid,address,dest,size);
  /* Existing fixed windows are in retained executable PT_LOAD[0,0x4f0e2).
   * Later installer-state reads cannot replace this code observation. */
  if (address>=modules->ampr_base && address-modules->ampr_base<UINT64_C(0x4f0e2) &&
      size && size<=UINT64_C(0x4f0e2)-(address-modules->ampr_base)) {
    memset(&ready_window,0,sizeof ready_window);
    ready_window.present=1; ready_window.address=address;
    ready_window.offset=address-modules->ampr_base; ready_window.length=size;
    ready_window.result=result;
    if (!result) {
      ready_window.captured=size<sizeof ready_window.bytes?size:sizeof ready_window.bytes;
      memcpy(ready_window.bytes,dest,ready_window.captured);
    }
  }
  return result;
}
static void ready_layout_diagnostic(const char *event) {
  fprintf(logfp,"reader readiness layout window event=%s gamePid=%d present=%d address=%llx offset=%llx length=%zu readResult=%d captured=%zu bytes=",
    event,(int)game_pid,ready_window.present,(unsigned long long)ready_window.address,
    (unsigned long long)ready_window.offset,ready_window.length,ready_window.result,ready_window.captured);
  for (size_t i=0;i<ready_window.captured;i++) fprintf(logfp,"%02x",(unsigned)ready_window.bytes[i]);
  fputc('\n',logfp); fflush(logfp);
}
/* BW_READY_WINDOW_DIAGNOSTIC_END */

static int ready_modules_read(struct ready_modules *modules) {
  memset(modules,0,sizeof *modules); modules->pid=game_pid;
  if (game_pid<=0 || kernel_dynlib_handle(game_pid,"libSceAmpr.sprx",&modules->ampr) ||
      kernel_dynlib_handle(game_pid,"libkernel.sprx",&modules->kernel)) return -1;
  modules->ampr_base=(uintptr_t)kernel_dynlib_mapbase_addr(game_pid,modules->ampr);
  modules->kernel_base=(uintptr_t)kernel_dynlib_mapbase_addr(game_pid,modules->kernel);
  modules->multiple=(uintptr_t)kernel_dynlib_dlsym(game_pid,modules->kernel,
    "sceKernelAioSubmitReadCommandsMultiple");
  return modules->ampr_base<0x10000 || modules->kernel_base<0x10000 || !modules->multiple?-1:0;
}
static int ready_modules_equal(const struct ready_modules *a,const struct ready_modules *b) {
  return a->pid==b->pid && a->ampr==b->ampr && a->kernel==b->kernel &&
    a->ampr_base==b->ampr_base && a->kernel_base==b->kernel_base && a->multiple==b->multiple;
}
/* A successful first observation pins this module identity and native target.
 * Observations never acquire Ampr's lock or authorize replacing a zero slot. */
static enum bw_native_error ready_check(uint64_t native,uint64_t hook,struct bw_native_aio_state *state) {
  struct ready_modules before,after;
  memset(state,0,sizeof *state);
  memset(&ready_window,0,sizeof ready_window);
  if (ready_modules_read(&before) || before.ampr_base!=ampr_base) return BW_NATIVE_MODULE;
  if (ready_pinned && !ready_modules_equal(&before,&ready_anchor)) return BW_NATIVE_CHANGED;
  if (ready_pinned && native && native!=ready_native) return BW_NATIVE_STATE_MISMATCH;
  struct bw_native_profile profile={ready_read,&before,before.ampr_base,before.kernel_base,"libkernel.sprx"};
  enum bw_native_error error=bw_native_check_aio_ready(&profile,before.multiple,
    native?native:ready_native,hook,state);
  if (ready_modules_read(&after) || !ready_modules_equal(&before,&after) ||
      (ready_pinned && !ready_modules_equal(&after,&ready_anchor))) return BW_NATIVE_CHANGED;
  if (error==BW_NATIVE_OK && !ready_pinned) {
    ready_anchor=before; ready_native=(uintptr_t)state->trampoline; ready_pinned=1;
  }
  return error;
}
/* Only incomplete/changed observations before any binding may be retried.
 * A partial binding is never silently abandoned or rebound after uncertainty. */
static int ready_require(uint64_t native,uint64_t hook,const char *event) {
  struct bw_native_aio_state state;
  enum bw_native_error error=ready_check(native,hook,&state);
  if (error==BW_NATIVE_OK) {
    if (!ready_reported) {
      fprintf(logfp,"reader readiness completed gamePid=%d ampr=%x kernel=%x base=%llx native=%llx atUs=%llu\n",
        (int)game_pid,ready_anchor.ampr,ready_anchor.kernel,(unsigned long long)ready_anchor.ampr_base,
        (unsigned long long)ready_native,(unsigned long long)(monotonic_us()-launch_us)); fflush(logfp);
      ready_reported=1;
    }
    return 0;
  }
  if (error==BW_NATIVE_LAYOUT) ready_layout_diagnostic(event);
  int deferred=error==BW_NATIVE_NOT_READY || error==BW_NATIVE_CHANGED;
  if (!deferred || binding_write_attempted || ready_deferred_reports++<8) {
    fprintf(logfp,"reader readiness refused event=%s error=%s lock=%u hooks=%u installed=%u trampoline=%llx slot=%llx armed=%u attempted=%d atUs=%llu\n",
      event,bw_native_error_text(error),state.lock,state.hooks_installed,state.multiple_installed,
      (unsigned long long)state.trampoline,(unsigned long long)state.original_slot,armed_routes,binding_write_attempted,
      (unsigned long long)(monotonic_us()-launch_us)); fflush(logfp);
  }
  return deferred && !binding_write_attempted?1:-1;
}
static int ready_bound(unsigned bound,const char *event) {
  int checked=ready_require(routes[0].saved,bound?routes[0].code:0,event);
  if (checked) return checked;
  for (unsigned i=0;i<BW_PATH_ROUTE_COUNT;i++) {
    uintptr_t value=0,expected=i<bound?routes[i].code:routes[i].saved;
    if (bw_reader_proc_copyout(game_pid,routes[i].pointer,&value,sizeof value) || value!=expected) {
      fprintf(logfp,"reader readiness route changed event=%s kind=%u bound=%u value=%llx expected=%llx\n",
        event,i,bound,(unsigned long long)value,(unsigned long long)expected); fflush(logfp);
      return binding_write_attempted?-1:1;
    }
  }
  return ready_require(routes[0].saved,bound?routes[0].code:0,event);
}
static int ready_monitor(const char *event) {
  uintptr_t primary=0;
  if (bw_reader_proc_copyout(game_pid,routes[0].pointer,&primary,sizeof primary)) return -1;
  if (primary!=routes[0].code && primary!=routes[0].saved) {
    fprintf(logfp,"reader readiness foreign primary event=%s value=%llx; refusing overwrite\n",
      event,(unsigned long long)primary); fflush(logfp); return -1;
  }
  return ready_require(routes[0].saved,primary==routes[0].code?routes[0].code:0,event)?-1:0;
}
#endif
static int aio_original_ready(void) {
  uint32_t handle;
  if (kernel_dynlib_handle(game_pid,"libSceAmpr.sprx",&handle)) return 0;
  ampr_base=(uintptr_t)kernel_dynlib_mapbase_addr(game_pid,handle);
  if (ampr_base<0x10000 || !validate_profile_layout()) return 0;
#ifdef BW_READER_READY
  int ready=ready_require(0,0,"startup-before");
  if (ready) return ready<0?-1:0;
#endif
  for (unsigned i=0;i<BW_PATH_ROUTE_COUNT;i++) {
    uintptr_t target=0;
    if (bw_reader_proc_copyout(game_pid,ampr_base+routes[i].offset,&target,sizeof target) || !route_valid(i,target)) return 0;
  }
#ifdef BW_READER_READY
  ready=ready_require(0,0,"startup-after");
  if (ready) return ready<0?-1:0;
#endif
  fprintf(logfp,"all seven preserved native I/O routes ready at +%llu us\n",(unsigned long long)(monotonic_us()-launch_us));
  fflush(logfp); return 1;
}
/* 1 means no pointer write was attempted and initialization may be sampled
 * again. From the first attempted write, failures are terminal for this run. */
static int install_aio_pointer(void) {
  if (armed_routes || binding_write_attempted) return -1;
#ifdef BW_READER_READY
  int ready=ready_require(0,0,"bind-entry");
  if (ready) return ready;
#endif
  profile_verbose=1;
  if (!validate_profile_layout()) { note("Native layout not ready at binding"); profile_verbose=0; return 1; }
  for (unsigned i=0;i<BW_PATH_ROUTE_COUNT;i++) {
    uintptr_t target=0;
    routes[i].pointer=ampr_base+routes[i].offset;
    int read_result=bw_reader_proc_copyout(game_pid,routes[i].pointer,&target,sizeof target);
    if (read_result || !route_valid(i,target)) {
      fprintf(logfp,"native pointer snapshot deferred kind=%u read=%d target=%llx\n",i,read_result,(unsigned long long)target); fflush(logfp);
      profile_verbose=0; return 1;
    }
    routes[i].saved=target;
  }
  if (native_helper(BW_NATIVE_FSTAT)!=native_fstat || native_helper(BW_NATIVE_OPEN)!=native_open ||
      native_helper(BW_NATIVE_CLOSE)!=native_close || native_helper(BW_NATIVE_ERRNO)!=native_errno) {
    note("Native file helpers changed before binding"); profile_verbose=0; return 1;
  }
  profile_verbose=0;
  /* Recheck the entire snapshot before the first mutation. */
  for (unsigned i=0;i<BW_PATH_ROUTE_COUNT;i++) {
    uintptr_t current=0;
    if (bw_reader_proc_copyout(game_pid,routes[i].pointer,&current,sizeof current) || current!=routes[i].saved) {
      fprintf(logfp,"native pointer refreshed before binding kind=%u\n",i); fflush(logfp); return 1;
    }
  }
  for (unsigned i=0;i<BW_PATH_ROUTE_COUNT;i++) {
    uintptr_t current=0,after=0;
#ifdef BW_READER_READY
    ready=ready_bound(i,"bind-before");
    if (ready) return ready;
#endif
    int read_result=bw_reader_proc_copyout(game_pid,routes[i].pointer,&current,sizeof current);
    if (read_result || current!=routes[i].saved) return binding_write_attempted?-1:1;
    int protection=kernel_get_vmem_protection(game_pid,routes[i].pointer,sizeof routes[i].code);
    /* A write followed by the saved value does not prove nothing happened:
     * initialization could have reset the pointer after a hook invocation.
     * From the first attempted write onward, every failure is terminal. */
    binding_write_attempted=1;
    int write_result=bw_reader_proc_copyin(game_pid,&routes[i].code,routes[i].pointer,sizeof routes[i].code);
    int verify_result=bw_reader_proc_copyout(game_pid,routes[i].pointer,&after,sizeof after);
    fprintf(logfp,"native pointer bind kind=%u protection=%x write=%d verify=%d after=%llx expected=%llx\n",i,
      protection,write_result,verify_result,(unsigned long long)after,(unsigned long long)routes[i].code); fflush(logfp);
    if (!verify_result && after==routes[i].code) armed_routes++;
    else return -1; /* Unknown or reset write outcome pins this attempt. */
    if (write_result) return -1;
#ifdef BW_READER_READY
    if (ready_bound(i+1,"bind-after")) return -1;
#endif
    fprintf(logfp,"read route armed kind=%u slot=%llx native=%llx code=%llx\n",i,
      (unsigned long long)routes[i].pointer,(unsigned long long)routes[i].saved,(unsigned long long)routes[i].code); fflush(logfp);
  }
  return 0;
}
static int monitor_aio_pointer(void) {
#ifdef BW_READER_READY
  if (ready_monitor("monitor-entry")) return -1;
#endif
  for (unsigned i=0;i<BW_PATH_ROUTE_COUNT;i++) {
    uintptr_t value=0,current=0;
    if (bw_reader_proc_copyout(game_pid,routes[i].pointer,&value,sizeof value)) return -1;
    if (value==routes[i].code) continue;
#ifdef BW_READER_READY
    if (value!=routes[i].saved || !validate_profile_layout() || !route_valid(i,value) ||
        ready_monitor("monitor-rearm") ||
        bw_reader_proc_copyout(game_pid,routes[i].pointer,&current,sizeof current) || current!=value ||
        bw_reader_proc_copyin(game_pid,&routes[i].code,routes[i].pointer,sizeof routes[i].code) ||
        bw_reader_proc_copyout(game_pid,routes[i].pointer,&current,sizeof current) || current!=routes[i].code ||
        ready_monitor("monitor-readback")) return -1;
#else
    if (value!=routes[i].saved || !validate_profile_layout() || !route_valid(i,value) ||
        bw_reader_proc_copyout(game_pid,routes[i].pointer,&current,sizeof current) || current!=value ||
        bw_reader_proc_copyin(game_pid,&routes[i].code,routes[i].pointer,sizeof routes[i].code) ||
        bw_reader_proc_copyout(game_pid,routes[i].pointer,&current,sizeof current) || current!=routes[i].code) return -1;
#endif
    fprintf(logfp,"read route initialization refreshed; rearmed kind=%u\n",i); fflush(logfp);
  }
#ifdef BW_READER_READY
  if (ready_bound(BW_PATH_ROUTE_COUNT,"monitor-complete")) return -1;
#endif
  return 0;
}

static int32_t game_state(int32_t expected) {
  if (expected>0) {
    int state=bw_reader_runtime_poll(&reader_runtime,expected);
    return state==1?expected:state==0?BW_LIFECYCLE_STOPPED:BW_LIFECYCLE_UNKNOWN;
  }
  struct bw_lifecycle parser; struct stat before,after,named;
  char line[BW_LIFECYCLE_MAX_LINE+1];
  if (bw_lifecycle_init(&parser,title_id,expected)) return BW_LIFECYCLE_UNKNOWN;
  int fd=open("/data/shadowmount/debug.log",O_RDONLY|O_NOFOLLOW);
  if (fd<0) return BW_LIFECYCLE_UNKNOWN;
  if (fstat(fd,&before) || !S_ISREG(before.st_mode) || before.st_nlink!=1) { close(fd); return BW_LIFECYCLE_UNKNOWN; }
  FILE *fp=fdopen(fd,"r"); if (!fp) { close(fd); return BW_LIFECYCLE_UNKNOWN; }
  while (fgets(line,sizeof line,fp)) if (bw_lifecycle_feed_line(&parser,line,strlen(line))<0) break;
  int complete=feof(fp) && !ferror(fp) && !fstat(fd,&after) &&
    !lstat("/data/shadowmount/debug.log",&named) && S_ISREG(named.st_mode) && named.st_nlink==1 &&
    before.st_dev==after.st_dev && before.st_ino==after.st_ino && after.st_size>=before.st_size &&
    named.st_dev==after.st_dev && named.st_ino==after.st_ino;
  fclose(fp); int32_t found=bw_lifecycle_finish(&parser,complete);
  if (found>0 && reader_launch_issued && bw_reader_runtime_bind(&reader_runtime,parser.latest.app_id,found)) return BW_LIFECYCLE_UNKNOWN;
  return found;
}
static pid_t find_game(void) { return (pid_t)game_state(0); }
static int reader_launch_current(void *opaque) {
  BW_LT_COUNT(BW_LT_READER_GUARDS,1);
  (void)opaque;
  if (bw_reader_runtime_current(&reader_runtime)) return -1;
  struct bw_share_config config={0}; struct bw_setup_generation generation={0};
  int status=bw_native_setup_read("/data/blackwire",&config,&generation);
  bw_share_clear(&config);
  return status!=BW_SETUP_VALID || !bw_native_setup_generation_equal(&reader_connection_generation,&generation)?-1:0;
}
#ifdef BW_NATIVE_PRELOAD
static double reader_preload_now(void *opaque) {
  (void)opaque;struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;
  return (double)t.tv_sec+(double)t.tv_nsec/1e9;
}
static int reader_preload_pause(void *opaque,unsigned milliseconds) {
  (void)opaque;return milliseconds>1000?-1:usleep(milliseconds*1000u);
}
/* This ownership predicate remains available after cancellation/settings
 * change. It never authorizes game work, only independent exit observation. */
static int reader_preload_owner_current(void *opaque) {
  (void)opaque;pthread_mutex_lock(&reader_runtime.lock);
  int bad=!reader_runtime.active||strcmp(reader_runtime.request.selection.run,run_id)||
    strcmp(reader_runtime.request.selection.title,title_id);
  pthread_mutex_unlock(&reader_runtime.lock);return bad?-1:0;
}
static int reader_preload_idle(void) {
  struct bw_startup_idle_request request={.deadline=reader_preload_now(NULL)+1.0,
    .current=reader_preload_owner_current,.game_log="/data/shadowmount/debug.log"};
  return bw_startup_idle_ps5(&request);
}
static int reader_preload_bundle(void *opaque,const char *root) {
  (void)opaque;if(reader_launch_current(NULL)||reader_preload_idle())return -1;
  struct bw_preload_bundle_request q={root,reader_launch_current,NULL,reader_preload_now(NULL)+20.0};
  if(bw_reader_selection_is_projection(&reader_selection)){
    const struct bw_preload_companions *companions=NULL;
    if(!reader_startup||bw_native_launch_admission_companions(reader_startup,&companions))return -1;
    return bw_preload_bundle_verify_projection_checked(&q,companions,NULL);
  }
  const struct bw_preload_source_ampr *source_ampr=NULL;
  if(!reader_startup||bw_native_launch_admission_source_ampr(reader_startup,&source_ampr))return -1;
  return bw_preload_bundle_verify_source_checked(&q,source_ampr,NULL);
}
static int reader_preload_launch(void *opaque) {
  (void)opaque;launch_ctx ctx={.size=sizeof ctx};char *argv[]={NULL};
  struct bw_reader_user_observation user;
  if(BW_LT_CALL(BW_LT_FINAL_AUTH,(bw_reader_foreground_user(&ctx.user,&user)||bw_reader_runtime_before_launch(&reader_runtime)||
     reader_launch_current(NULL)||bw_reader_runtime_launch_begin(&reader_runtime))))return 0;
  launch_us=monotonic_us();status_write("launching","Starting the selected game with its bundled early reader.");
  int result=BW_LT_CALL(BW_LT_SONY,sceSystemServiceLaunchApp(title_id,argv,&ctx));
  bw_reader_runtime_launch_result(&reader_runtime,result);reader_launch_issued=result>=0;
  BW_LT_REPORT(BW_LT_SONY_RETURN);
  fprintf(logfp,"preload launch title=%s result=%08x scope=%s\n",title_id,(unsigned)result,bw_preload_embedded_image.scope_sha256);
  fflush(logfp);return result>=0?1:0;
}
static int32_t reader_preload_discover(void *opaque) {
  (void)opaque;int32_t observed=find_game();if(observed>1)game_pid=observed;
  return observed>1?observed:observed==BW_LIFECYCLE_STOPPED?0:-1;
}
static int reader_preload_app_current(const struct bw_session_ops *o,const struct bw_native_session *s) {
  uint32_t app=0;char title[16]={0};
  return o->running_app(o->opaque,&app)||app!=s->app_id||o->title(o->opaque,app,title)||
    !memchr(title,0,sizeof title)||strcmp(title,s->title)?-1:0;
}
static int reader_preload_consumer(void *opaque,int32_t pid,uid_t *out) {
  (void)opaque;if(!out||pid<=1)return -1;*out=(uid_t)-1;
  pthread_mutex_lock(&reader_runtime.lock);int result=-1;
  struct bw_native_session *s=&reader_runtime.session;const struct bw_session_ops *o=&reader_runtime.ops;
  if(!reader_runtime.active||reader_runtime.cancelled||!reader_runtime.launch_requested||reader_runtime.launch_accepted!=1||
     !s->bound||s->process.pid!=pid||strcmp(s->run,run_id)||strcmp(s->title,title_id)||
     bw_native_session_poll(s,o)!=1||reader_preload_app_current(o,s))goto done;
  int uid=kernel_get_ucred_uid(pid);
  if(uid<0||bw_native_session_poll(s,o)!=1||reader_preload_app_current(o,s))goto done;
  int again=kernel_get_ucred_uid(pid);
  if(again<0||again!=uid||bw_native_session_poll(s,o)!=1||reader_preload_app_current(o,s))goto done;
  *out=(uid_t)uid;result=0;
done:pthread_mutex_unlock(&reader_runtime.lock);return result;
}
static int reader_preload_live(void *opaque,int32_t pid) {
  (void)opaque;return bw_reader_runtime_poll(&reader_runtime,pid);
}
static int reader_preload_exited(void *opaque) {
  (void)opaque;if(reader_preload_owner_current(NULL))return -1;
  pthread_mutex_lock(&reader_runtime.lock);int known=0;struct bw_stop_identity ignored;
  if(!reader_runtime.launch_requested||reader_runtime.launch_accepted==0)known=1;
  else if(reader_runtime.launch_accepted==1&&reader_runtime.session.bound) {
    /* Fresh queries; the session's latched STOPPED phase alone is insufficient
     * during a later release, and PID reuse is explicitly not absence. */
    int32_t pid=reader_runtime.session.process.pid;
    known=reader_runtime.ops.process(reader_runtime.ops.opaque,pid,&ignored)==0&&
      reader_runtime.ops.process(reader_runtime.ops.opaque,pid,&ignored)==0;
  }
  pthread_mutex_unlock(&reader_runtime.lock);
  return !known||reader_preload_idle()||reader_preload_owner_current(NULL)?-1:0;
}
static void reader_preload_event(void *opaque,const char *next,const char *message,int32_t pid) {
  (void)opaque;if(pid>1)game_pid=pid;
  note(message);status_write(next,message);
}
static int reader_preload_run(void) {
  char root[1024];struct bw_share_config config={0};struct bw_setup_generation observed={0};
  struct bw_provider_binding source_identity={0};
  int n=snprintf(root,sizeof root,"/data/blackwire/games/%s",game_name);
  if(n<0||(size_t)n>=sizeof root||(reader_selection.source_kind!=BW_PROVIDER_FOLDER&&reader_selection.source_kind!=BW_PROVIDER_CONTAINER)||
     bw_native_setup_read("/data/blackwire",&config,&observed)!=BW_SETUP_VALID||
     !bw_native_setup_generation_equal(&reader_connection_generation,&observed)||
     bw_provider_binding_select(&source_identity,&config,reader_selection.source,reader_selection.title,
       (enum bw_provider_kind)reader_selection.source_kind,reader_selection.game)||reader_launch_current(NULL)) {
    bw_share_clear(&config);return failure("The selected early-reader connection or canonical preparation changed.");
  }
  struct bw_preload_launch_request request={.root=root,.selection=&reader_selection,.connection=&config,
    .ops={NULL,reader_preload_now,reader_preload_pause,reader_launch_current,reader_preload_bundle,
      reader_preload_launch,reader_preload_discover,reader_preload_consumer,reader_preload_live,
      reader_preload_exited,reader_preload_event}};
  int result;
  if(bw_reader_selection_is_projection(&reader_selection)){
    void *bytes=NULL;size_t size=0;
    if(bw_native_projection_handoff_bytes(reader_startup,&reader_selection,&bytes,&size)){bw_share_clear(&config);return failure("The held projected reader inputs changed before launch.");}
    result=bw_preload_launch_run_projection(&request,&reader_preloaded,&bytes,size);
  }else result=bw_preload_launch_run(&request,&reader_preloaded);
  bw_share_clear(&config);
  return result;
}
#endif
static int reader_data_current(void *opaque) {
  return opaque==&reader_runtime?bw_reader_runtime_data_current(&reader_runtime):-1;
}
static int reader_derived_current(void *opaque,const struct bw_derived_reader_claim *claim) {
  if (opaque!=&reader_runtime || !reader_derived_admitted || !claim ||
      memcmp(&claim->original,&reader_derived_claim.original,sizeof claim->original) ||
      memcmp(claim->prepared_game,reader_derived_claim.prepared_game,sizeof claim->prepared_game) ||
      memcmp(claim->run,reader_derived_claim.run,sizeof claim->run) ||
      claim->prepared_policy!=reader_derived_claim.prepared_policy ||
      claim->manifest_count!=reader_derived_claim.manifest_count ||
      claim->manifest_hash!=reader_derived_claim.manifest_hash || claim->eboot_size!=reader_derived_claim.eboot_size ||
      memcmp(claim->provenance_sha256,reader_derived_claim.provenance_sha256,32) ||
      memcmp(claim->receipt_sha256,reader_derived_claim.receipt_sha256,32) ||
      memcmp(claim->map_sha256,reader_derived_claim.map_sha256,32) ||
      memcmp(claim->commitment_sha256,reader_derived_claim.commitment_sha256,32)) return -1;
  return reader_data_current(opaque);
}
static int connect_share(void) {
  struct bw_share_config config={0},public_connection={0}; struct bw_provider_binding identity;
  struct bw_reader_data_admission admission={0};
  char root[1024]; int result=-1; struct bw_setup_generation observed={0};
  /* Projected physical LOCAL aliases are not remote source paths. They only
   * run through the qualified early DATA module, never the legacy late reader. */
  if(bw_reader_selection_is_projection(&reader_selection)){
#ifdef BW_NATIVE_PRELOAD
    if(!reader_startup||!reader_early_active){reader_startup_failure="The verified early reader is required for this game. Choose Send files to repair it.";goto done;}
#else
    goto done;
#endif
  }
  if (bw_native_setup_read("/data/blackwire",&config,&observed)!=BW_SETUP_VALID ||
      !bw_native_setup_generation_equal(&reader_connection_generation,&observed) ||
      reader_selection.source_policy!=BW_PROVIDER_POLICY_V1 ||
      bw_provider_binding_select(&identity,&config,source_name,title_id,(enum bw_provider_kind)reader_selection.source_kind,reader_selection.game) ||
      strcmp(identity.binding.game,reader_selection.game)) goto done;
  char hash[65];
  for (unsigned i=0;i<32;i++) snprintf(hash+2*i,3,"%02x",identity.binding.sha256[i]);
  if (strcmp(hash,source_binding_hex)) goto done;
  smb=smb2_init_context(); if (!smb) goto done;
  smb2_set_timeout(smb,10); smb2_set_user(smb,config.user); smb2_set_password(smb,config.password);
  smb2_set_sign(smb,1);
  smb2_set_security_mode(smb,SMB2_NEGOTIATE_SIGNING_ENABLED|SMB2_NEGOTIATE_SIGNING_REQUIRED);
  if (BW_LT_CALL(BW_LT_SMB_CONNECT,smb2_connect_share(smb,config.server,config.share,config.user))<0) goto done;
  int n=snprintf(root,sizeof root,"/data/blackwire/games/%s",game_name);
  int fast=0;
#ifdef BW_NATIVE_PRELOAD
  fast=reader_startup&&reader_early_active&&!bw_reader_has_prepared_target(&reader_selection)&&(identity.kind==BW_PROVIDER_FOLDER||identity.kind==BW_PROVIDER_CONTAINER);
#endif
  if(reader_startup&&!fast)goto done;
  if(n<0||(size_t)n>=sizeof root)goto done;
  if(fast){
    /* The early module uses exact /app0 paths from its own checked index and
     * map. The late reader's all-file inode/path map is unused in this path. */
    if(bw_native_launch_admission_current(reader_startup,"/data/blackwire",&identity,&reader_selection,NULL)){
      reader_startup_failure="The verified startup files or receipt changed. Choose Send to revalidate them.";goto done;
    }
  }else if (bw_files_load(&file_map,root,"blackwire.files")) {
    note(bw_files_error(&file_map)); goto done;
  }
  if (!fast&&(bw_files_count(&file_map)!=manifest_count || bw_files_checksum(&file_map)!=manifest_hash ||
      (identity.kind==BW_PROVIDER_FOLDER && !bw_reader_has_prepared_target(&reader_selection) &&
       bw_reader_source_smb_open(&reader_source,&file_map,&config,&identity.binding,smb)))) goto done;
  strcpy(public_connection.server,config.server); strcpy(public_connection.share,config.share); bw_share_clear(&config);
  struct bw_source_smb source={.smb=smb,.deadline=(double)monotonic_us()/1000000.0+
    (bw_reader_has_prepared_target(&reader_selection)?1800.0:300.0)};
  struct bw_native_prepared prepared; const char *error=NULL;
  struct bw_reader_preflight request={.data_root="/data/blackwire",.connection=&public_connection,.selected=&reader_selection,
    .source=&source,.current=reader_launch_current,.error=&error,.data_admission=&admission,
    .data_current=reader_data_current,.data_opaque=&reader_runtime,
    .data_epoch=bw_reader_runtime_data_epoch(&reader_runtime),.game_log="/data/shadowmount/debug.log",.startup=reader_startup};
  int checked=bw_reader_preflight_run(&request,&prepared);
  BW_LT_COUNT(BW_LT_SOURCE_REQUESTS,source.requests);BW_LT_COUNT(BW_LT_SOURCE_ENTRIES,source.entries);BW_LT_COUNT(BW_LT_SOURCE_CONTENT_BYTES,source.content_bytes);
  /* The fresh preparation owners have been destroyed before reader binding. */
  smb=source.smb;
  if (checked || !smb) {
    note(error?error:"Current PC and console startup verification failed");
    if(fast)reader_startup_failure=error?error:"The selected PC startup evidence is unavailable. Choose Send to revalidate it.";
    goto done;
  }
  if (bw_reader_has_prepared_target(&reader_selection)) {
    if (!admission.valid || !admission.provenance_bytes || !admission.provenance_size ||
        reader_data_current(&reader_runtime)) goto done;
    reader_derived_claim=admission.derived_claim; reader_derived_admitted=1;
    struct bw_derived_reader_request selected={.selection=&reader_selection,
      .provenance=admission.provenance_bytes,.provenance_size=admission.provenance_size,
      .admitted_receipt_sha256=admission.terminal_sha256,.current=reader_derived_current,.opaque=&reader_runtime};
    if (bw_derived_reader_source_smb_open(&reader_derived_source,&file_map,&public_connection,&selected,smb)) {
      note(reader_derived_source.error?reader_derived_source.error:"Derived STREAM source binding failed"); goto done;
    }
  } else if (!fast&&identity.kind==BW_PROVIDER_CONTAINER) {
    size_t map_size=0; const void *map=bw_files_manifest_bytes(&file_map,&map_size);
    if (bw_reader_container_open(&reader_container,smb,&admission,map,map_size,reader_launch_current,NULL)) {
      note("Fresh selected PC image or manifest generation changed"); goto done;
    }
  }
  fprintf(logfp,"source-bound reader records=%u streamFiles=%u metadataBytes=%zu handlesMax=%u preparedPolicy=%u startupReceiptReuse=%u\n",
    fast?manifest_count:bw_files_count(&file_map),fast?prepared.stream_count:file_map.stream_count,file_map.allocation_bytes,BW_FILES_HANDLE_LIMIT,reader_selection.prepared_policy,fast);
  fflush(logfp); result=0;
done:
  bw_reader_data_admission_destroy(&admission); bw_share_clear(&public_connection); bw_share_clear(&config);
  return result;
}

static int check_eboot(void) {
#ifdef BW_NATIVE_PRELOAD
  if(reader_startup&&reader_early_active&&reader_selection.source_kind==BW_PROVIDER_CONTAINER){
    /* The retained admission hashed the local boot file; container source()
     * independently checked its current startup digest. Do not open the late
     * reader's unused file-map/container owner for a second boot sample. */
    uint8_t map[32];return bw_native_launch_admission_map_sha256(reader_startup,map);
  }
#endif
  char path[2048]; uint8_t local[32],remote_bytes[32]; struct stat st; int result=-1;
  if (reader_selection.source_kind==BW_PROVIDER_CONTAINER) {
    snprintf(path,sizeof path,"%seboot.bin",game_root); int fd=open(path,O_RDONLY|O_NOFOLLOW);
    if (fd>=0 && !fstat(fd,&st) && (uint64_t)st.st_size==eboot_size && (st.st_mode&0111) &&
        pread(fd,local,16,0)==16 && pread(fd,local+16,16,eboot_size-16)==16 &&
        !bw_reader_container_boot(&reader_container,eboot_size,remote_bytes) &&
        !memcmp(local,remote_bytes,sizeof local)) result=0;
    if (fd>=0) close(fd);
    return result;
  }
  snprintf(path,sizeof path,"%s/eboot.bin",source_name);
  struct smb2fh *remote=smb2_open(smb,path,O_RDONLY);
  snprintf(path,sizeof path,"%seboot.bin",game_root); int fd=open(path,O_RDONLY|O_NOFOLLOW);
  if (remote && fd>=0 && !fstat(fd,&st) && (uint64_t)st.st_size==eboot_size && (st.st_mode&0111) &&
      pread(fd,local,16,0)==16 && pread(fd,local+16,16,eboot_size-16)==16 &&
      !read_exact(remote,remote_bytes,16,0) && !read_exact(remote,remote_bytes+16,16,eboot_size-16) &&
      !memcmp(local,remote_bytes,sizeof local)) result=0;
  if (fd>=0) close(fd);
  if (remote) smb2_close(smb,remote);
  return result;
}
static struct bw_stage_identity stage_identity(const struct stat *st) {
  return (struct bw_stage_identity){(uint64_t)st->st_dev,(uint64_t)st->st_ino,
    (uint64_t)st->st_size,(uint64_t)st->st_nlink,S_ISREG(st->st_mode)};
}
/* The held descriptor remains authoritative; a renamed, linked or replaced
 * name is an error even if a game-side descriptor can still read old bytes. */
static int payload_stage_snapshot(struct bw_stage_identity *held,struct bw_stage_identity *named) {
  char canonical[1024]; struct stat h,n;
  struct bw_stage_identity anchor=stage_identity(&stage_stat);
  if (stage_fd<0) return cache_diagnostic("payload-held-fd","fd=%d",stage_fd);
  if (!realpath(CACHE_PATH,canonical)) return cache_diagnostic("payload-realpath","errno=%d",errno);
  if (strcmp(canonical,CACHE_PATH)) return cache_diagnostic("payload-canonical","exactName=0");
  if (fstat(stage_fd,&h)) return cache_diagnostic("payload-held-stat","fd=%d errno=%d",stage_fd,errno);
  if (lstat(CACHE_PATH,&n)) return cache_diagnostic("payload-named-stat","errno=%d",errno);
  *held=stage_identity(&h); *named=stage_identity(&n);
  if (!bw_stage_payload_matches(&anchor,held,named))
    return cache_diagnostic("payload-tuple",
      "anchor(reg=%d links=%llu dev=%llu ino=%llu size=%llu) held(reg=%d links=%llu dev=%llu ino=%llu size=%llu) named(reg=%d links=%llu dev=%llu ino=%llu size=%llu)",
      anchor.regular,(unsigned long long)anchor.links,(unsigned long long)anchor.device,(unsigned long long)anchor.inode,(unsigned long long)anchor.size,
      held->regular,(unsigned long long)held->links,(unsigned long long)held->device,(unsigned long long)held->inode,(unsigned long long)held->size,
      named->regular,(unsigned long long)named->links,(unsigned long long)named->device,(unsigned long long)named->inode,(unsigned long long)named->size);
  return 0;
}
static int payload_stage_owned(void) {
  struct bw_stage_identity held,named;
  return payload_stage_snapshot(&held,&named);
}
static int prepare_stage(void) {
  struct stat directory,existing; struct statvfs space;
  if (mkdir("/data/blackwire/cache",0755) && errno!=EEXIST) return -1;
  if (lstat("/data/blackwire/cache",&directory) || !S_ISDIR(directory.st_mode)) return -1;
  if (chmod("/data/blackwire/cache",0755)) return -1;
  int is_new=0;
  if (lstat(CACHE_PATH,&existing)) {
    if (errno!=ENOENT) return -1;
    stage_fd=open(CACHE_PATH,O_CREAT|O_EXCL|O_RDWR|O_NOFOLLOW,0644); is_new=1;
  } else {
    if (!S_ISREG(existing.st_mode) || existing.st_nlink!=1 || (uint64_t)existing.st_size!=STAGE_SIZE) return -1;
    stage_fd=open(CACHE_PATH,O_RDWR|O_NOFOLLOW);
  }
  if (stage_fd<0 || flock(stage_fd,LOCK_EX|LOCK_NB)) return -1;
  if (fchmod(stage_fd,0644)) return -1;
  if (statvfs("/data/blackwire/cache",&space) ||
      (uint64_t)space.f_bavail*space.f_frsize<STAGE_SIZE+128ULL*1024*1024) return -1;
  if (is_new && ftruncate(stage_fd,(off_t)STAGE_SIZE)) return -1;
  if (fstat(stage_fd,&stage_stat) || !S_ISREG(stage_stat.st_mode) || stage_stat.st_nlink!=1 ||
      (uint64_t)stage_stat.st_size!=STAGE_SIZE || payload_stage_owned()) return -1;
  return bw_cache_init(&memory_cache);
}
static int prepare_aio_pages(void) {
  unsigned char shell[1024]; uint64_t args[6]={0},code=0,slot=0;
  size_t size=bw_path_hook_end-bw_path_hook_start;
  uintptr_t pread_addr=pread_address(game_pid);
  if (!pread_addr || size>sizeof shell) return -1;
  /* Preserve the settling interval established by the working reader. */
  usleep(150000);
#ifdef BW_READER_READY
  if (ready_require(0,0,"prepare-mmap")) return failure("Native readiness changed before private-page preparation");
#endif
  args[0]=(pread_addr&~0xfffffULL)-0x200000ULL;
  args[1]=BW_PATH_ROUTE_COUNT*4096; args[2]=3; args[3]=0x1002; args[4]=(uint64_t)-1;
  int allocation=remote_call(game_pid,477,args,&code);
  fprintf(logfp,"code mmap transport=%d result=%llx\n",allocation,(unsigned long long)code); fflush(logfp);
  if (allocation || code<0x10000 || code>UINT64_C(0x800000000000)-BW_PATH_ROUTE_COUNT*4096 || (code&4095))
    return failure("Game code allocation failed; no reader was installed");
  args[0]=code+0x100000; args[1]=SLOT_SIZE;
#ifdef BW_READER_READY
  if (ready_require(0,0,"prepare-request-mmap")) return failure("Native readiness changed before private request allocation");
#endif
  allocation=remote_call(game_pid,477,args,&slot);
  fprintf(logfp,"request mmap transport=%d result=%llx\n",allocation,(unsigned long long)slot); fflush(logfp);
  if (allocation || slot<0x10000 || slot>UINT64_C(0x800000000000)-SLOT_SIZE || (slot&4095) ||
      (slot<code+BW_PATH_ROUTE_COUNT*4096 && code<slot+SLOT_SIZE))
    return failure("Game request allocation failed; no reader was installed");
  native_fstat=native_helper(BW_NATIVE_FSTAT); native_open=native_helper(BW_NATIVE_OPEN);
  native_close=native_helper(BW_NATIVE_CLOSE); native_errno=native_helper(BW_NATIVE_ERRNO);
  uintptr_t error_fn=native_errno;
  if (!native_fstat || !native_open || !native_close || !error_fn) return failure("Native file operations or POSIX errno helper are unavailable");
  struct bw_path_request initial={.timeout_ticks=timeout_ticks};
#ifdef BW_READER_READY
  if (ready_require(0,0,"prepare-request")) return failure("Native readiness changed before private request preparation");
#endif
  if (bw_reader_proc_copyin(game_pid,&initial,slot,sizeof initial)) return -1;
  for (unsigned i=0;i<BW_PATH_ROUTE_COUNT;i++) {
    uint64_t fail=i<2?(uint64_t)(int64_t)SCE_EIO:UINT64_MAX;
    uint64_t error=i<2?0:error_fn; uint32_t kind=i;
    memcpy(shell,bw_path_hook_start,size);
    memcpy(shell+(bw_path_slot-bw_path_hook_start),&slot,sizeof slot);
    memcpy(shell+(bw_path_kind-bw_path_hook_start),&kind,sizeof kind);
    memcpy(shell+(bw_path_failure_return-bw_path_hook_start),&fail,sizeof fail);
    memcpy(shell+(bw_path_errno_fn-bw_path_hook_start),&error,sizeof error);
#ifdef BW_READER_READY
    if (ready_require(0,0,"prepare-code")) return failure("Native readiness changed before private code preparation");
#endif
    routes[i].code=code+4096*i;
    if (bw_reader_proc_copyin(game_pid,shell,routes[i].code,size)) return -1;
  }
  args[0]=code; args[1]=BW_PATH_ROUTE_COUNT*4096; args[2]=5;
  uint64_t protection_result=0;
#ifdef BW_READER_READY
  if (ready_require(0,0,"prepare-protection")) return failure("Native readiness changed before private code protection");
#endif
  int protection=remote_call(game_pid,74,args,&protection_result);
  fprintf(logfp,"code protection transport=%d result=%llx\n",protection,(unsigned long long)protection_result); fflush(logfp);
  if (protection || protection_result) return failure("Game reader protection failed; no reader was installed");
#ifdef BW_READER_READY
  if (ready_require(0,0,"prepare-complete")) return failure("Native readiness changed during private-page preparation");
#endif
  slot_addr=slot;
  fprintf(logfp,"bounded private pages code=%llx slot=%llx size=%u routes=%u errno=%llx\n",
    (unsigned long long)code,(unsigned long long)slot,SLOT_SIZE,BW_PATH_ROUTE_COUNT,(unsigned long long)error_fn);
  fflush(logfp); return 0;
}
static int game_call(uintptr_t function,const uint64_t args[6],uint64_t *result) {
  struct { uint64_t function,args[6],result; } call={.function=function};
  memcpy(call.args,args,sizeof call.args);
  uint32_t state=BW_CALL; uint64_t deadline=monotonic_us()+20000000;
  int copied=bw_reader_proc_copyin(game_pid,&call,slot_addr+offsetof(struct bw_path_request,function),sizeof call);
  if (copied) return cache_diagnostic("rpc-arguments-copyin","function=%llx copy=%d",(unsigned long long)function,copied);
  copied=bw_reader_proc_copyin(game_pid,&state,slot_addr,sizeof state);
  if (copied) return cache_diagnostic("rpc-call-copyin","function=%llx copy=%d",(unsigned long long)function,copied);
  for (;;) {
    uint32_t poison=0;
    copied=bw_reader_proc_copyout(game_pid,slot_addr,&state,sizeof state);
    if (copied) return cache_diagnostic("rpc-state-copyout","function=%llx copy=%d",(unsigned long long)function,copied);
    copied=bw_reader_proc_copyout(game_pid,slot_addr+offsetof(struct bw_path_request,poison),&poison,sizeof poison);
    if (copied) return cache_diagnostic("rpc-poison-copyout","function=%llx state=%u copy=%d",(unsigned long long)function,state,copied);
    if (poison) return cache_diagnostic("rpc-poison","function=%llx state=%u poison=%u",(unsigned long long)function,state,poison);
    if (state==BW_CALLED) break;
    if (monotonic_us()>=deadline) return cache_diagnostic("rpc-timeout","function=%llx state=%u poison=%u",(unsigned long long)function,state,poison);
    usleep(100);
  }
  copied=bw_reader_proc_copyout(game_pid,slot_addr+offsetof(struct bw_path_request,result),result,sizeof *result);
  if (copied) return cache_diagnostic("rpc-result-copyout","function=%llx state=%u copy=%d",(unsigned long long)function,state,copied);
  state=BW_PENDING;
  copied=bw_reader_proc_copyin(game_pid,&state,slot_addr,sizeof state);
  return copied?cache_diagnostic("rpc-pending-copyin","function=%llx result=%llx copy=%d",(unsigned long long)function,(unsigned long long)*result,copied):0;
}
static int game_stat(int fd,struct stat *st) {
  uint64_t args[6]={(uint64_t)(uint32_t)fd,slot_addr+STAT_AT,0,0,0,0},result;
  memset(st,0,sizeof *st);
  int copied=bw_reader_proc_copyin(game_pid,st,slot_addr+STAT_AT,sizeof *st);
  if (copied) return cache_diagnostic("game-stat-zero-copyin","fd=%d copy=%d",fd,copied);
  if (game_call(native_fstat,args,&result)) return -1;
  if ((int32_t)result) {
    uint64_t error_pointer=0,none[6]={0}; int native_error=0;
    if (game_call(native_errno,none,&error_pointer)) return -1;
    if (error_pointer<0x10000) return cache_diagnostic("game-stat-errno-pointer","fd=%d result=%llx pointer=%llx",fd,(unsigned long long)result,(unsigned long long)error_pointer);
    copied=bw_reader_proc_copyout(game_pid,error_pointer,&native_error,sizeof native_error);
    if (copied) return cache_diagnostic("game-stat-errno-copyout","fd=%d result=%llx copy=%d",fd,(unsigned long long)result,copied);
    fprintf(logfp,"native fstat failed fd=%d result=%llx errno=%d\n",fd,(unsigned long long)result,native_error); fflush(logfp);
    return (int32_t)result==-1 && native_error==EBADF?1:-1;
  }
  copied=bw_reader_proc_copyout(game_pid,slot_addr+STAT_AT,st,sizeof *st);
  if (copied) return cache_diagnostic("game-stat-copyout","fd=%d result=%llx copy=%d",fd,(unsigned long long)result,copied);
  return 0;
}
static int latch_game_files(void) {
  uint32_t id; struct stat st; uint64_t result=0,close_result=0;
  if (bw_files_first_stream(&file_map,&id)) return failure("File map has no remote data objects");
  char path[1024]; int n=snprintf(path,sizeof path,"/app0/%s",bw_files_path(&file_map,id));
  if (n<0 || (size_t)n>=sizeof path || bw_reader_proc_copyin(game_pid,path,slot_addr+PATH_AT,(size_t)n+1)) return -1;
  uint64_t args[6]={slot_addr+PATH_AT,O_RDONLY,0,0,0,0};
  if (game_call(native_open,args,&result) || (int32_t)result<0) return failure("Cannot open known app0 data object for identity verification");
  int fd=(int32_t)result;
  int bad=game_stat(fd,&st) || !S_ISREG(st.st_mode) || bw_files_latch_device(&file_map,(uint64_t)st.st_dev,(uint64_t)st.st_ino,(uint64_t)st.st_size,id);
  uint64_t close_args[6]={(uint64_t)(uint32_t)fd,0,0,0,0,0};
  if (!native_close || game_call(native_close,close_args,&close_result) || (int32_t)close_result) bad=1;
  if (bad) return failure("Game app0 identity did not match the prepared file map");
  fprintf(logfp,"verified app0 fileId=%u inode=%llu dev=%llu\n",id,(unsigned long long)st.st_ino,(unsigned long long)st.st_dev);
  fflush(logfp); return 0;
}
/* Only the still-pending intercepted thread may execute the proof RPC. The
 * native pread target is a validated saved syscall stub, never the hooked
 * export or cached slot, so proving the cache cannot reenter this shared CAS. */
#ifdef BW_OWNED_VM
static int owned_vm_process(void *opaque,int32_t pid,uint64_t *address,int *error_number) {
  (void)opaque; int previous=errno; errno=0;
  intptr_t process=kernel_get_proc(pid); *error_number=errno; errno=previous;
  *address=(uint64_t)process; return process?0:-1;
}
static int owned_vm_read(void *opaque,uint64_t address,void *bytes,size_t size,int *error_number) {
  (void)opaque; int previous=errno; errno=0;
  int copied=kernel_copyout((intptr_t)address,bytes,size); *error_number=errno; errno=previous;
  return copied;
}
#include "owned_vm_window.h"
struct owned_window_context {
  const struct bw_owned_vm_ops *metadata;
  int32_t pid;
  uint64_t root,base,size,address,length;
  int captured,owner_error;
  struct bw_path_request original;
};
static int owned_window_owner(void *opaque) {
  struct owned_window_context *c=opaque; struct bw_path_request current;
  if (game_state(c->pid)!=c->pid) { c->owner_error=1; return -1; }
  if (bw_reader_proc_copyout(c->pid,c->base,&current,sizeof current)) { c->owner_error=2; return -1; }
  if (current.state!=BW_PATH_PENDING || current.poison || current.kind>=BW_PATH_ROUTE_COUNT) { c->owner_error=3; return -1; }
  if (!c->captured) { c->original=current; c->captured=1; }
  else if (current.kind!=c->original.kind ||
      memcmp(current.original_args,c->original.original_args,sizeof current.original_args)) {
    c->owner_error=4; return -1;
  }
  return 0;
}
static enum bw_owned_vm_error owned_window_probe(void *opaque,struct bw_owned_vm_diagnostic *out) {
  struct owned_window_context *c=opaque;
  return bw_owned_vm_check(c->metadata,c->pid,c->root,c->base,c->size,c->address,c->length,out);
}
static uint64_t owned_window_now(void *opaque) { (void)opaque; return monotonic_us(); }
static void owned_window_pause(void *opaque,unsigned delay) { (void)opaque; usleep(delay); }
static enum bw_owned_vm_error owned_vm_stable(const struct bw_owned_vm_ops *metadata,
    int32_t pid,uint64_t root,uint64_t base,uint64_t size,uint64_t address,uint64_t length,
    struct bw_owned_vm_diagnostic *out) {
  struct owned_window_context context={.metadata=metadata,.pid=pid,.root=root,.base=base,
    .size=size,.address=address,.length=length};
  const struct bw_vm_window_ops ops={&context,owned_window_owner,owned_window_probe,
    owned_window_now,owned_window_pause};
  struct bw_vm_window_diagnostic diagnostic;
  enum bw_vm_window_error error=bw_vm_window_check(&reader_vm_stable,&ops,&diagnostic);
  *out=diagnostic.last;
  if (error || diagnostic.recovered || diagnostic.probes>1) {
    if (reader_vm_reports<16) {
      reader_vm_reports++;
      fprintf(logfp,"owned mapping window result=%s probes=%u waits=%u recovered=%d ownerError=%d lastProbe=%s anchor=%llx start=%llx end=%llx prot=%u\n",
        bw_vm_window_error_text(error),diagnostic.probes,diagnostic.waits,diagnostic.recovered,
        context.owner_error,bw_owned_vm_error_text(diagnostic.last_probe),(unsigned long long)reader_vm_stable.anchor.entry,
        (unsigned long long)reader_vm_stable.anchor.start,(unsigned long long)reader_vm_stable.anchor.end,reader_vm_stable.anchor.protection);
      fflush(logfp);
    }
  }
  if (!error) return BW_OWNED_VM_OK;
  out->error=diagnostic.last_probe==BW_OWNED_VM_OK?BW_OWNED_VM_CHANGED:diagnostic.last_probe;
  return out->error;
}
static int owned_scratch_permission(void) {
  struct bw_owned_vm_diagnostic d;
  const struct bw_owned_vm_ops ops={NULL,owned_vm_process,owned_vm_read};
  enum bw_owned_vm_error checked;
  if (KERNEL_OFFSET_PROC_P_VMSPACE!=0x200) {
    memset(&d,0,sizeof d); checked=BW_OWNED_VM_CONFIG;
  } else checked=owned_vm_stable(&ops,game_pid,KERNEL_OFFSET_VMSPACE_P_ROOT,
    slot_addr,SLOT_SIZE,slot_addr+STAT_AT,BW_STAGE_CHALLENGE_BYTES,&d);
  if (checked==BW_OWNED_VM_OK) return 0;
  const struct bw_owned_vm_snapshot *a=&d.snapshots[0],*b=&d.snapshots[1];
  return cache_diagnostic("owner-scratch-permission",
    "query=%s pass=%u queryStage=%s errno=%d read=%llx bytes=%zu rootOffset=%lx first(proc=%llx vm=%llx root=%llx entry=%llx start=%llx end=%llx prot=%u nodes=%u) second(proc=%llx vm=%llx root=%llx entry=%llx start=%llx end=%llx prot=%u nodes=%u)",
    bw_owned_vm_error_text(checked),d.pass,bw_owned_vm_stage_text(d.stage),d.callback_errno,
    (unsigned long long)d.read_address,d.read_size,KERNEL_OFFSET_VMSPACE_P_ROOT,
    (unsigned long long)a->process,(unsigned long long)a->vmspace,(unsigned long long)a->root,
    (unsigned long long)a->entry,(unsigned long long)a->start,(unsigned long long)a->end,a->protection,a->nodes,
    (unsigned long long)b->process,(unsigned long long)b->vmspace,(unsigned long long)b->root,
    (unsigned long long)b->entry,(unsigned long long)b->start,(unsigned long long)b->end,b->protection,b->nodes);
}
#endif
static int cache_owner_guard(const struct bw_path_request *expected,struct bw_path_request *out) {
  struct bw_path_request current;
  int32_t observed=game_state(game_pid);
  if (observed!=game_pid) return cache_diagnostic("owner-lifecycle","observed=%d expected=%d unknown=%d stopped=%d",
    observed,(int)game_pid,BW_LIFECYCLE_UNKNOWN,BW_LIFECYCLE_STOPPED);
  if (slot_addr<0x10000 || slot_addr>UINT64_C(0x800000000000)-SLOT_SIZE)
    return cache_diagnostic("owner-slot-range","slot=%llx bytes=%u",(unsigned long long)slot_addr,SLOT_SIZE);
#ifdef BW_OWNED_VM
  if (owned_scratch_permission()) return -1;
#else
  int protection=kernel_get_vmem_protection(game_pid,slot_addr+STAT_AT,BW_STAGE_CHALLENGE_BYTES);
  if (protection<0 || (protection&3)!=3) return cache_diagnostic("owner-scratch-protection","address=%llx bytes=%u protection=%d",
    (unsigned long long)(slot_addr+STAT_AT),BW_STAGE_CHALLENGE_BYTES,protection);
#endif
  int copied=bw_reader_proc_copyout(game_pid,slot_addr,&current,sizeof current);
  if (copied) return cache_diagnostic("owner-header-copyout","copy=%d bytes=%zu",copied,sizeof current);
  if (current.state!=BW_PATH_PENDING || current.poison || current.kind>=BW_PATH_ROUTE_COUNT)
    return cache_diagnostic("owner-header","state=%u poison=%u kind=%u expectedState=%u",current.state,current.poison,current.kind,BW_PATH_PENDING);
  if (expected && current.kind!=expected->kind)
    return cache_diagnostic("owner-kind","actual=%u expected=%u",current.kind,expected->kind);
  if (expected) for (unsigned i=0;i<6;i++) if (current.original_args[i]!=expected->original_args[i])
    return cache_diagnostic("owner-argument","index=%u actual=%llx expected=%llx",i,
      (unsigned long long)current.original_args[i],(unsigned long long)expected->original_args[i]);
  if (out) *out=current;
  return 0;
}
static int cache_snapshot(struct bw_stage_snapshot *out) {
  struct stat game;
  int32_t observed=game_state(game_pid);
  if (observed!=game_pid) return cache_diagnostic("snapshot-lifecycle-before","observed=%d expected=%d unknown=%d stopped=%d",
    observed,(int)game_pid,BW_LIFECYCLE_UNKNOWN,BW_LIFECYCLE_STOPPED);
  if (payload_stage_snapshot(&out->held,&out->named)) return -1;
  int stat_result=game_stat(game_cache_fd,&game);
  if (stat_result) return cache_diagnostic("snapshot-game-stat","fd=%d statResult=%d",game_cache_fd,stat_result);
  if (payload_stage_snapshot(&out->held,&out->named)) return -1;
  observed=game_state(game_pid);
  if (observed!=game_pid) return cache_diagnostic("snapshot-lifecycle-after","observed=%d expected=%d unknown=%d stopped=%d",
    observed,(int)game_pid,BW_LIFECYCLE_UNKNOWN,BW_LIFECYCLE_STOPPED);
  out->game=stage_identity(&game); return 0;
}
static int prove_game_cache(void) {
  struct bw_stage_snapshot before,after; struct bw_path_request owner;
  unsigned char challenge[BW_STAGE_CHALLENGE_BYTES],observed[BW_STAGE_CHALLENGE_BYTES]={0};
  struct bw_stage_identity anchor=stage_identity(&stage_stat);
  uint64_t returned=0,current=0;
  if (cache_challenge_attempted || native_read_submitted || CACHE_OWNER_GUARD("proof-preflight",NULL,&owner) ||
      cache_snapshot(&before) || !bw_stage_game_shape(&anchor,&before.game) ||
      bw_stage_challenge(run_id,challenge) || !route_valid(BW_PATH_PREAD,routes[BW_PATH_PREAD].saved) ||
      bw_reader_proc_copyout(game_pid,routes[BW_PATH_PREAD].pointer,&current,sizeof current) ||
      current!=routes[BW_PATH_PREAD].code) return failure("Cache backing proof preflight failed; staging retained");
  /* One attempt only. No ordinary read has been submitted, so offset zero is
   * unowned scratch. This writes exactly64 bytes into our existing fixed file. */
  cache_challenge_attempted=1;
  if (payload_stage_owned() || CACHE_OWNER_GUARD("proof-prepare",&owner,NULL) ||
      write_exact(stage_fd,challenge,sizeof challenge,0) || payload_stage_owned() ||
      bw_reader_proc_copyin(game_pid,observed,slot_addr+STAT_AT,sizeof observed))
    return failure("Cache backing challenge could not be prepared; staging retained");
  uint64_t args[6]={(uint64_t)(uint32_t)game_cache_fd,slot_addr+STAT_AT,sizeof challenge,0,0,0};
#ifdef BW_OWNED_VM
  if (CACHE_OWNER_GUARD("proof-before-pread",&owner,NULL))
    return failure("Cache backing proof ownership failed before native pread; staging retained");
  uint64_t native_started=monotonic_us();
  int called=game_call(routes[BW_PATH_PREAD].saved,args,&returned);
  struct timespec wall=diagnostic_wall_time();
  fprintf(logfp,"cache proof native wallSec=%lld wallNs=%09ld function=%llx call=%d returned=%llx expected=%zu elapsedUs=%llu\n",
    (long long)wall.tv_sec,wall.tv_nsec,(unsigned long long)routes[BW_PATH_PREAD].saved,called,
    (unsigned long long)returned,sizeof observed,(unsigned long long)(monotonic_us()-native_started)); fflush(logfp);
  if (called || returned!=sizeof observed)
    return failure("Cache backing proof native pread did not return exactly64 bytes; staging retained");
  if (CACHE_OWNER_GUARD("proof-after-pread",&owner,NULL) ||
      bw_reader_proc_copyout(game_pid,slot_addr+STAT_AT,observed,sizeof observed) ||
      cache_snapshot(&after) || CACHE_OWNER_GUARD("proof-after-stat",&owner,NULL) ||
      bw_stage_proof_latch(&stage_proof,&anchor,&before,&after,run_id,observed,(int64_t)returned))
    return failure("Game cache did not return the current backing challenge; staging retained");
#else
  if (CACHE_OWNER_GUARD("proof-before-pread",&owner,NULL) ||
      game_call(routes[BW_PATH_PREAD].saved,args,&returned) || returned!=sizeof observed ||
      CACHE_OWNER_GUARD("proof-after-pread",&owner,NULL) ||
      bw_reader_proc_copyout(game_pid,slot_addr+STAT_AT,observed,sizeof observed) ||
      cache_snapshot(&after) || CACHE_OWNER_GUARD("proof-after-stat",&owner,NULL) ||
      bw_stage_proof_latch(&stage_proof,&anchor,&before,&after,run_id,observed,(int64_t)returned))
    return failure("Game cache did not return the current backing challenge; staging retained");
#endif
  fprintf(logfp,"cache backing proof fd=%d bytes=%u payloadLinks=1 gameLinks=%llu inode=%llu gameDev=%llu\n",
    game_cache_fd,BW_STAGE_CHALLENGE_BYTES,(unsigned long long)after.game.links,
    (unsigned long long)after.game.inode,(unsigned long long)stage_proof.game_device); fflush(logfp);
  return 0;
}
static int ensure_game_cache(const char *stage) {
  cache_check_stage=stage;
  if (CACHE_OWNER_GUARD("ensure-owner",NULL,NULL) || payload_stage_owned())
    return failure("Payload cache identity or pending game ownership changed; staging retained");
  if (game_cache_fd>=0) {
    struct bw_stage_snapshot current; struct bw_stage_identity anchor=stage_identity(&stage_stat);
    if (cache_snapshot(&current))
      return failure("Game cache descriptor was closed or reused; staging retained");
    if (!bw_stage_proof_current(&stage_proof,&anchor,&current)) {
      cache_diagnostic("game-proof-tuple","verified=%d expectedDev=%llu game(reg=%d links=%llu dev=%llu ino=%llu size=%llu) expectedInode=%llu expectedSize=%llu",
        stage_proof.verified,(unsigned long long)stage_proof.game_device,current.game.regular,
        (unsigned long long)current.game.links,(unsigned long long)current.game.device,(unsigned long long)current.game.inode,
        (unsigned long long)current.game.size,(unsigned long long)anchor.inode,(unsigned long long)anchor.size);
      return failure("Game cache descriptor was closed or reused; staging retained");
    }
    return 0;
  }
  if (bw_reader_proc_copyin(game_pid,CACHE_PATH,slot_addr+PATH_AT,sizeof CACHE_PATH)) return -1;
  uint64_t args[6]={slot_addr+PATH_AT,O_RDONLY,0,0,0,0},result;
  if (game_call(native_open,args,&result)) return failure("Game-thread cache open call failed");
  if ((int32_t)result<0) {
    char error[128]; snprintf(error,sizeof error,"Game-thread cache open failed: %08x",(unsigned)(int32_t)result);
    return failure(error);
  }
  game_cache_fd=(int32_t)result;
  struct stat st;
  int stat_result=game_stat(game_cache_fd,&st);
  fprintf(logfp,"cache identity fd=%d openResult=%llx statResult=%d mode=%x links=%llu inode=%llu dev=%llu size=%lld expectedInode=%llu expectedDev=%llu expectedSize=%llu statBytes=%zu\n",
      game_cache_fd,(unsigned long long)result,stat_result,(unsigned)st.st_mode,(unsigned long long)st.st_nlink,
      (unsigned long long)st.st_ino,(unsigned long long)st.st_dev,(long long)st.st_size,
      (unsigned long long)stage_stat.st_ino,(unsigned long long)stage_stat.st_dev,(unsigned long long)STAGE_SIZE,sizeof st); fflush(logfp);
  struct bw_stage_identity anchor=stage_identity(&stage_stat),game=stage_identity(&st);
  if (stat_result || !bw_stage_game_shape(&anchor,&game))
    return failure("Opened cache shape did not match the fixed stage; staging retained");
  if (prove_game_cache()) return -1;
  if (bw_reader_proc_copyout(game_pid,ampr_base+0x64ab0,&native_poll,sizeof native_poll) || !native_poll)
    if (bw_reader_proc_copyout(game_pid,ampr_base+0x18397b38,&native_poll,sizeof native_poll)) return -1;
  if (!validate_profile_target(BW_NATIVE_AIO_POLL,native_poll,"sceKernelAioPollRequest")) return failure("Native poll function could not be validated");
  fprintf(logfp,"bounded game cache fd=%d inode=%llu size=%llu nativePoll=%llx\n",game_cache_fd,
          (unsigned long long)st.st_ino,(unsigned long long)STAGE_SIZE,(unsigned long long)native_poll);
  fflush(logfp); return latch_game_files();
}
static int file_for_fd(int fd) {
  struct stat st; uint32_t file_id=0;
  int stat_result=game_stat(fd,&st);
  if (stat_result) return stat_result>0?-3:-1;
  if (!S_ISREG(st.st_mode)) return -3;
  enum bw_files_match match=bw_files_lookup(&file_map,(uint64_t)st.st_dev,(uint64_t)st.st_ino,(uint64_t)st.st_size,&file_id);
  return match==BW_FILES_MATCH?(int)file_id:match==BW_FILES_LOCAL?-3:-1;
}
static int load_block(void *opaque,uint32_t id,uint64_t block,uint8_t *dest,size_t capacity,size_t *out_size) {
  (void)opaque;
  if (reader_selection.source_kind==BW_PROVIDER_CONTAINER) {
    uint64_t before=reader_container.image?reader_container.image->read_bytes:0;
    int result=bw_reader_container_load(&reader_container,&memory_cache,id,block,dest,capacity,out_size);
    if (reader_container.image && reader_container.image->read_bytes>=before) network_bytes+=reader_container.image->read_bytes-before;
    return result;
  }

  struct bw_manifest_record record;
  if (bw_files_get(&file_map,id,&record) || record.flags!=BW_MANIFEST_STREAM || capacity!=BW_CACHE_BLOCK || block>UINT64_MAX/BW_CACHE_BLOCK) return -1;
  uint64_t at=block*BW_CACHE_BLOCK;
  if (at>=record.size) return -1;
  uint32_t size=record.size-at<capacity?(uint32_t)(record.size-at):(uint32_t)capacity;
  struct smb2fh *remote=bw_reader_has_prepared_target(&reader_selection)?bw_derived_reader_source_get(&reader_derived_source,id):bw_reader_source_get(&reader_source,id);
  if (!remote || read_exact(remote,dest,size,at)) return -1;
  network_bytes+=size; *out_size=size; return 0;
}
static int reader_data_get(uint32_t id,uint64_t block,const uint8_t **data,size_t *size) {
  if (reader_selection.source_kind==BW_PROVIDER_FOLDER) {
    int derived=bw_reader_has_prepared_target(&reader_selection);
    if (derived && reader_derived_current(&reader_runtime,&reader_derived_claim)) {
      if (data) *data=NULL; if (size) *size=0; return -1;
    }
    int result=bw_cache_get(&memory_cache,id,block,load_block,NULL,data,size);
    if (derived && (result || reader_derived_current(&reader_runtime,&reader_derived_claim))) {
      if (data) *data=NULL; if (size) *size=0; return -1;
    }
    return result;
  }
  if (reader_selection.source_kind!=BW_PROVIDER_CONTAINER) { if (data) *data=NULL; if (size) *size=0; return -1; }
  uint64_t before=reader_container.image?reader_container.image->read_bytes:0;
  int result=bw_reader_container_get(&reader_container,&memory_cache,id,block,data,size);
  if (reader_container.image && reader_container.image->read_bytes>=before) network_bytes+=reader_container.image->read_bytes-before;
  return result;
}
static int stage_range(uint32_t pak,uint64_t at,uint64_t size,uint64_t destination) {
  while (size) {
    const uint8_t *data; size_t available; uint64_t hits=memory_cache.hits;
    uint64_t block=at/BW_CACHE_BLOCK; size_t within=(size_t)(at%BW_CACHE_BLOCK);
    if (reader_data_get(pak,block,&data,&available) || within>=available) return -1;
    size_t take=available-within; if (take>size) take=(size_t)size;
    if (memory_cache.hits!=hits) hit_bytes+=take;
    if (destination>STAGE_SIZE || take>STAGE_SIZE-destination ||
        write_exact(stage_fd,data+within,(uint32_t)take,destination)) return -1;
    staged_bytes+=take; destination+=take; at+=take; size-=take;
  }
  return 0;
}
static int resolve_read_file(void *opaque,int32_t fd,struct bw_read_file *file) {
  (void)opaque; int found=file_for_fd(fd);
  if (found==-3) return 0;
  struct bw_manifest_record record;
  if (found<0 || bw_files_get(&file_map,(uint32_t)found,&record)) return -1;
  file->file_id=(uint32_t)found; file->size=record.size; return 1;
}
static int verify_prefix(uint64_t destination,uint64_t offset,uint64_t length) {
  uint8_t expected[16],actual[16]; size_t size=length<16?(size_t)length:16;
  if (!size) return 0;
  return pread(stage_fd,expected,size,(off_t)offset)!=(ssize_t)size ||
    bw_reader_proc_copyout(game_pid,destination,actual,size) || memcmp(expected,actual,size) ? -1:0;
}
static int independent_aio_buffer(const struct bw_read_plan *plan,uint32_t index) {
  const struct bw_read_aio *selected=&plan->aio[index];
  if (selected->length>UINT64_MAX-selected->buffer) return 0;
  for (uint32_t i=0;i<plan->count;i++) {
    const struct bw_read_aio *other=&plan->aio[i];
    if (i==index || !other->length) continue;
    if (other->length>UINT64_MAX-other->buffer) return 0;
    if (selected->buffer<other->buffer+other->length && other->buffer<selected->buffer+selected->length) return 0;
  }
  return 1;
}
/* The exact validated helper runs once on this intercepted thread, not on a
 * payload/RPC worker. Later observations only copy its cell. Nothing writes
 * errno or changes the saved native return. Sequential bookkeeping separately
 * restores entry/native errno through this validated cell. Diagnostic RPC failure is terminal:
 * an incomplete call cannot safely be ignored before another slot mutation. */
static int request_errno_observe(const char *at) {
  if (!request_errno_pointer) return 0;
  int native_error=0;
  int copied=bw_reader_proc_copyout(game_pid,request_errno_pointer,&native_error,sizeof native_error);
  if (copied) return cache_diagnostic("observe-errno-copyout","at=%s pointer=%llx copy=%d",at,(unsigned long long)request_errno_pointer,copied);
  fprintf(logfp,"request errno operation=%u kind=%u at=%s pointer=%llx value=%d\n",
    aio_batches,cache_check_kind,at,(unsigned long long)request_errno_pointer,native_error); fflush(logfp);
  return 0;
}
static int request_errno_begin(const struct bw_path_request *req) {
  int sequential=req->kind==BW_PATH_READ || req->kind==BW_PATH_READV;
  if ((aio_batches>16 && !sequential) || req->kind<BW_PATH_PREAD) return 0;
  if (CACHE_OWNER_GUARD("errno-entry",req,NULL)) return -1;
  uint64_t args[6]={0},pointer=0;
  if (game_call(native_errno,args,&pointer)) return -1;
  if (pointer<0x10000 || pointer>UINT64_C(0x800000000000)-sizeof(int))
    return cache_diagnostic("observe-errno-range","pointer=%llx",(unsigned long long)pointer);
  int protection=kernel_get_vmem_protection(game_pid,pointer,sizeof(int));
  if (protection<0 || (sequential?(protection&3)!=3:!(protection&1)))
    return cache_diagnostic("observe-errno-protection","pointer=%llx protection=%d",(unsigned long long)pointer,protection);
  request_errno_pointer=(uintptr_t)pointer;
  return request_errno_observe("entry");
}
static int request_native_return(unsigned kind,const uint64_t args[6],uint64_t returned,int forwarded) {
  if (aio_batches<=16) {
    fprintf(logfp,"request native operation=%u kind=%u forwarded=%d return=%016llx signedReturn=%lld args=%llx,%llx,%llx,%llx,%llx,%llx\n",
      aio_batches,kind,forwarded,(unsigned long long)returned,(long long)(int64_t)returned,
      (unsigned long long)args[0],(unsigned long long)args[1],(unsigned long long)args[2],
      (unsigned long long)args[3],(unsigned long long)args[4],(unsigned long long)args[5]); fflush(logfp);
  }
  return request_errno_observe("native-return");
}
static int sequential_errno_read(int *value);
static int sequential_errno_write(int value);
static int forward_native_read(unsigned kind,const uint64_t args[6],uint64_t *returned) {
  cache_check_stage="forward-native";
  native_read_submitted=1;
  int native_error=0,sequential=kind==BW_PATH_READ || kind==BW_PATH_READV;
  if (game_call(routes[kind].saved,args,returned)) return -1;
  if (sequential && sequential_errno_read(&native_error)) return -1;
  if (request_native_return(kind,args,*returned,1) || ensure_game_cache("forward-postnative")) return -1;
  if (sequential && sequential_errno_write(native_error)) return -1;
  return request_errno_observe("forward-postguard");
}
/* Sequential routes share the same pending RPC owner as positional reads.
 * Call the validated saved lseek target, never the hooked Ampr slot/export.
 * This serializes only these intercepted routes; it cannot lock direct
 * syscalls or an unrelated dup/close user of the open-file description. */
struct sequential_identity { int32_t fd; struct bw_read_file file; };
/* request_errno_begin validated this intercepted thread's writable errno cell.
 * Preserve the entry value before the native data call and its resulting value
 * after our fstat/lseek bookkeeping. No value is invented on native success. */
static int sequential_errno_read(int *value) {
  return !request_errno_pointer || bw_reader_proc_copyout(game_pid,request_errno_pointer,value,sizeof *value)?
    failure("Sequential errno capture failed; staging retained"):0;
}
static int sequential_errno_write(int value) {
  return !request_errno_pointer || bw_reader_proc_copyin(game_pid,&value,request_errno_pointer,sizeof value)?
    failure("Sequential errno restore failed; staging retained"):0;
}
static int forward_requested_read(unsigned kind,const uint64_t args[6],int entry_errno,uint64_t *returned) {
  if ((kind==BW_PATH_READ || kind==BW_PATH_READV) && sequential_errno_write(entry_errno)) return -1;
  return (forward_native_read)(kind,args,returned);
}
static int sequential_resolve(void *opaque,int32_t fd,struct bw_read_file *file) {
  const struct sequential_identity *expected=opaque;
  if (fd!=expected->fd || resolve_read_file(NULL,fd,file)!=1 ||
      file->file_id!=expected->file.file_id || file->size!=expected->file.size) return -1;
  return 1;
}
static int serialized_seek(const struct bw_path_request *req,const uint64_t args[6],uint64_t *returned) {
  uintptr_t current=0;
  if (CACHE_OWNER_GUARD("seek-before",req,NULL) ||
      !route_valid(BW_PATH_LSEEK,routes[BW_PATH_LSEEK].saved) ||
      bw_reader_proc_copyout(game_pid,routes[BW_PATH_LSEEK].pointer,&current,sizeof current) ||
      current!=routes[BW_PATH_LSEEK].code)
    return failure("Native seek ownership changed; staging retained");
  if (game_call(routes[BW_PATH_LSEEK].saved,args,returned))
    return failure("Native seek did not return; staging retained");
  return CACHE_OWNER_GUARD("seek-after",req,NULL);
}
static int sequential_position(const struct bw_path_request *req,int32_t fd,uint64_t *position) {
  uint64_t args[6]={(uint64_t)(uint32_t)fd,0,SEEK_CUR,0,0,0};
  if (serialized_seek(req,args,position)) return -1;
  return (int64_t)*position<0?failure("Streamed sequential file position is unavailable"):0;
}
static int sequential_advance(const struct bw_path_request *req,const struct sequential_identity *identity,
    uint64_t position,uint64_t returned) {
  struct bw_read_file current; uint64_t observed=0;
  if (!returned || returned>INT64_MAX || position>(uint64_t)INT64_MAX-returned)
    return failure("Streamed sequential file position overflow");
  if (sequential_resolve((void *)identity,identity->fd,&current)!=1 ||
      sequential_position(req,identity->fd,&observed) || observed!=position)
    return failure("Streamed sequential descriptor or position changed; staging retained");
  uint64_t target=position+returned;
  uint64_t args[6]={(uint64_t)(uint32_t)identity->fd,target,SEEK_SET,0,0,0};
  if (serialized_seek(req,args,&observed)) return -1;
  return observed!=target?failure("Streamed sequential file position was not advanced exactly"):0;
}
static int deliver_batch(const struct bw_path_request *req,uint64_t *returned) {
  struct bw_read_plan plan; struct bw_read_aio original[BW_READ_MAX_ITEMS];
  struct bw_read_iovec iov[BW_READ_MAX_IOV]; int32_t ids[BW_READ_MAX_ITEMS];
  uint64_t args[6],result=0,before=network_bytes,position=0; enum bw_read_error planned;
  struct sequential_identity identity={0}; int entry_errno=0,native_error=0;
  if (req->kind>=BW_PATH_ROUTE_COUNT) return failure("Unknown native read adapter");
  memcpy(args,req->original_args,sizeof args);
  unsigned kind=req->kind,execute_kind=kind; int sequential=kind==BW_PATH_READ || kind==BW_PATH_READV;
  int32_t count=(int32_t)args[kind<2?1:2];
  cache_check_kind=kind; cache_check_stage="entry"; request_started_us=monotonic_us(); request_errno_pointer=0;
  if (aio_batches<=16) {
    int saved_errno=errno; struct timespec wall=diagnostic_wall_time();
    fprintf(logfp,"request entry wallSec=%lld wallNs=%09ld operation=%u kind=%u state=%u poison=%u originalArgs=%llx,%llx,%llx,%llx,%llx,%llx\n",
      (long long)wall.tv_sec,wall.tv_nsec,aio_batches,kind,req->state,req->poison,
      (unsigned long long)args[0],(unsigned long long)args[1],(unsigned long long)args[2],
      (unsigned long long)args[3],(unsigned long long)args[4],(unsigned long long)args[5]); fflush(logfp);
    errno=saved_errno;
  }
  if (request_errno_begin(req)) return failure("POSIX errno observation failed; staging retained");
  if (sequential && sequential_errno_read(&entry_errno)) return -1;
  struct read_route *route=&routes[kind]; route->operations++;
  if (kind==BW_PATH_LSEEK) {
    if (serialized_seek(req,args,&result) || request_native_return(kind,args,result,1)) return -1;
    *returned=result; return 0;
  }
  /* Challenge before every ordinary native read, including forwarded calls.
   * Helper RPCs and the proof read itself are not normal submissions. */
  if (ensure_game_cache(game_cache_fd<0?"before-first":"before-read")) return -1;
  if (request_errno_observe("after-preflight")) return -1;
  cache_check_stage="planning";
  /* Native APIs reject invalid counts themselves; no remote object is read. */
  if ((kind<2 && (count<=0 || count>(int32_t)BW_READ_MAX_ITEMS)) ||
      ((kind==BW_PATH_PREADV || kind==BW_PATH_READV) && (count<=0 || count>IOV_MAX)))
    return forward_requested_read(kind,args,entry_errno,returned);
  if ((kind==BW_PATH_PREADV || kind==BW_PATH_READV) && count>(int32_t)BW_READ_MAX_IOV) {
    struct bw_read_file file; int stream=resolve_read_file(NULL,(int32_t)args[0],&file);
    if (stream<0) return failure("Vector file identity lookup failed");
    if (stream) return failure("Streamed vector count exceeds the staging policy");
    return forward_requested_read(kind,args,entry_errno,returned);
  }
  if (sequential) {
    /* Identify before copying vectors or asking a non-file for its offset.
     * LOCAL/non-file/invalid-FD calls retain their original native route. */
    int stream=resolve_read_file(NULL,(int32_t)args[0],&identity.file);
    if (stream<0) return failure("Sequential file identity lookup failed");
    if (!stream) return forward_requested_read(kind,args,entry_errno,returned);
    if ((kind==BW_PATH_READ && args[2] && args[1]<0x10000) ||
        (kind==BW_PATH_READV && args[1]<0x10000))
      return forward_requested_read(kind,args,entry_errno,returned); /* Native-invalid scalar arguments. */
    identity.fd=(int32_t)args[0];
    if (sequential_position(req,identity.fd,&position)) return -1;
    execute_kind=kind==BW_PATH_READ?BW_PATH_PREAD:BW_PATH_PREADV;
  }
  if (kind<2) {
    if (args[0]<0x10000 || args[3]<0x10000 ||
        bw_reader_proc_copyout(game_pid,args[0],original,(size_t)count*sizeof original[0])) return -1;
    planned=bw_read_plan_aio(&plan,(enum bw_read_kind)kind,original,(uint32_t)count,game_cache_fd,resolve_read_file,NULL);
  } else if (execute_kind==BW_PATH_PREAD) {
    planned=bw_read_plan_pread(&plan,(int32_t)args[0],args[1],args[2],
      (int64_t)(sequential?position:args[3]),game_cache_fd,
      sequential?sequential_resolve:resolve_read_file,sequential?&identity:NULL);
  } else {
    if (args[1]<0x10000 || bw_reader_proc_copyout(game_pid,args[1],iov,(size_t)count*sizeof iov[0])) return -1;
    planned=bw_read_plan_preadv(&plan,(int32_t)args[0],iov,(uint32_t)count,
      (int64_t)(sequential?position:args[3]),game_cache_fd,
      sequential?sequential_resolve:resolve_read_file,sequential?&identity:NULL);
  }
  if (planned!=BW_READ_OK) return failure(bw_read_error_string(planned));
  if (plan.forward_unchanged && !sequential) return forward_requested_read(kind,args,entry_errno,returned);
  /* A captured STREAM zero/EOF request performs a zero-byte native call.
   * Neither a seek nor file growth can turn it into a placeholder read. */
  if (sequential && plan.forward_unchanged) {
    plan.native_length=0;
    if (execute_kind==BW_PATH_PREADV)
      for (uint32_t i=0;i<(uint32_t)count;i++) plan.iov[i].length=0;
  }
  if (aio_batches<=16) {
    fprintf(logfp,"request plan operation=%u kind=%u count=%u pieces=%u stageBytes=%llu nativeIds=%u\n",
      aio_batches,kind,plan.count,plan.piece_count,(unsigned long long)plan.stage_bytes,plan.native_id_count); fflush(logfp);
  }
  if (plan.stage_bytes>high_water) high_water=plan.stage_bytes;
  cache_check_stage="staging";
  for (uint32_t i=0;i<plan.piece_count;i++) {
    const struct bw_read_piece *piece=&plan.pieces[i];
    if (stage_range(piece->file_id,piece->source_offset,piece->length,piece->stage_offset)) return -1;
  }
  /* Check both namespaces after writing, before a native consumer can read. */
  if (ensure_game_cache("poststage")) return -1;
  if (request_errno_observe("pre-native")) return -1;
  if (kind<2) {
    if (bw_reader_proc_copyin(game_pid,plan.aio,slot_addr+ITEM_AT,(size_t)count*sizeof plan.aio[0])) return -1;
    args[0]=slot_addr+ITEM_AT;
  } else {
    args[0]=(uint64_t)(uint32_t)plan.native_fd; args[3]=(uint64_t)plan.native_offset;
    if (execute_kind==BW_PATH_PREAD) args[2]=plan.native_length;
    else {
      if (bw_reader_proc_copyin(game_pid,plan.iov,slot_addr+ITEM_AT,(size_t)count*sizeof plan.iov[0])) return -1;
      args[1]=slot_addr+ITEM_AT;
    }
  }
  cache_check_stage="native";
  native_read_submitted=1;
  if (sequential && sequential_errno_write(entry_errno)) return -1;
  if (game_call(routes[execute_kind].saved,args,&result)) return failure("Native call did not return; staging retained");
  if (sequential && sequential_errno_read(&native_error)) return -1;
  if (request_native_return(kind,args,result,0)) return -1;
  cache_check_stage="completion";
  if (kind<2) {
    if ((int32_t)result) return failure("Native AIO submission failed; staging retained");
    if (bw_reader_proc_copyout(game_pid,req->original_args[3],ids,(size_t)plan.native_id_count*sizeof ids[0])) return -1;
    for (uint32_t i=0;i<plan.native_id_count;i++)
      for (uint32_t j=0;j<i;j++) if (ids[i]==ids[j]) return failure("Native AIO IDs were not independent; staging retained");
    uint64_t deadline=monotonic_us()+15000000;
    for (uint32_t i=0;i<plan.native_id_count;i++) {
      if (ids[i]<0) return failure("Native AIO returned an invalid ID; staging retained");
      for (;;) {
        int state=0; uint64_t poll_args[6]={(uint64_t)(uint32_t)ids[i],slot_addr+STATE_AT,0,0,0,0},poll_result=0;
        if (bw_reader_proc_copyin(game_pid,&state,slot_addr+STATE_AT,sizeof state) ||
            game_call(native_poll,poll_args,&poll_result) || (int32_t)poll_result ||
            bw_reader_proc_copyout(game_pid,slot_addr+STATE_AT,&state,sizeof state)) return -1;
        int base=state&~0x10000;
        if (state<0) return failure("Native read returned an invalid completion state");
        if (base==3 || base==4) break;
        if (monotonic_us()>=deadline) return failure("Native read timeout; staging remains pinned");
        usleep(100);
      }
    }
    for (uint32_t i=0;i<plan.piece_count;i++) {
      const struct bw_read_piece *piece=&plan.pieces[i];
      const struct bw_read_aio *item=&plan.aio[piece->index]; struct bw_aio_result completion;
      if (bw_reader_proc_copyout(game_pid,item->result,&completion,sizeof completion) ||
          (completion.state&~0x10000u)!=3 || completion.returned!=(int64_t)piece->length)
        return failure("Native read did not deliver the complete staged range");
      if (route->operations<=8 && independent_aio_buffer(&plan,piece->index) &&
          verify_prefix(item->buffer,piece->stage_offset,piece->length))
        return failure("Native destination differs from staged PC bytes");
    }
  } else {
    if ((int64_t)result>=0 && result>plan.native_length) return failure("Native synchronous read returned an invalid length");
    if ((int64_t)result>0 && route->operations<=8) {
      uint64_t destination=req->original_args[1],length=result,offset=0;
      if (execute_kind==BW_PATH_PREADV) {
        uint64_t remaining=result,consumed=0;
        /* The last written vector remains verifiable even if callers use
         * overlapping destinations; earlier vectors may be overwritten. */
        for (uint32_t i=0;i<(uint32_t)count && remaining;i++) if (plan.iov[i].length) {
          uint64_t take=remaining<plan.iov[i].length?remaining:plan.iov[i].length;
          destination=plan.iov[i].buffer; length=take; offset=consumed;
          consumed+=take; remaining-=take;
        }
      }
      if (verify_prefix(destination,offset,length)) return failure("Native synchronous destination differs from staged PC bytes");
    }
  }
  /* All staged AIO completions above are terminal; synchronous reads returned.
   * Check identity before acknowledging and permitting the next stage write. */
  if (ensure_game_cache("postnative")) return -1;
  if (request_errno_observe("postguard")) return -1;
  /* Only the completed positive native result consumes the original FD.
   * Failure after delivery is terminal/provisional, never a zero fallback. */
  if (sequential && (int64_t)result>0 && sequential_advance(req,&identity,position,result)) return -1;
  if (sequential && (int64_t)result<=0) {
    struct bw_read_file observed;
    if (sequential_resolve(&identity,identity.fd,&observed)!=1)
      return failure("Streamed sequential descriptor changed after native completion");
  }
  if (sequential && sequential_errno_write(native_error)) return -1;
  *returned=result;
  uint64_t progress_now=monotonic_us();
  int progress_due=progress_now && (!progress_report_us || progress_now<progress_report_us || progress_now-progress_report_us>=UINT64_C(1000000));
  if (route->operations<=8 || aio_batches%100==0) {
    fprintf(logfp,"delivered operation=%u kind=%u routeCount=%llu nativeCompleted=1 network=%llu networkTotal=%llu hitBytes=%llu stagingHighWater=%llu\n",
      aio_batches,kind,(unsigned long long)route->operations,(unsigned long long)(network_bytes-before),
      (unsigned long long)network_bytes,(unsigned long long)hit_bytes,(unsigned long long)high_water); fflush(logfp);
  }
  if (route->operations<=8 || aio_batches%100==0 || progress_due) {
    status_write("running",NULL); progress_report_us=progress_now;
  }
  return 0;
}
static int serve(void) {
  for (;;) {
    int32_t observed=game_state(game_pid);
    if (observed==BW_LIFECYCLE_STOPPED) return 0;
    if (observed==BW_LIFECYCLE_UNKNOWN) { usleep(1000); continue; }
    if (observed!=game_pid) return failure("Selected game lifecycle became inconsistent; staging retained");
    struct bw_path_request req,check;
    if (monitor_aio_pointer()) return failure("Native read pointer ownership changed");
    if (bw_reader_proc_copyout(game_pid,slot_addr,&req,sizeof req)) return -1;
    if (req.poison) return failure("Game read timed out; staging retained");
    if (req.state!=BW_PATH_PENDING) {
      uint64_t idle_now=monotonic_us();
      if (!idle_report_us || idle_now<idle_report_us) idle_report_us=idle_now;
      if (idle_now && idle_now-idle_report_us>=UINT64_C(5000000)) {
        idle_report_us=idle_now;
        if (idle_reports<64) {
          fprintf(logfp,"reader wait observed operation=%u state=%u poison=%u kind=%u elapsedUs=%llu networkTotal=%llu hitBytes=%llu\n",
            aio_batches,req.state,req.poison,req.kind,(unsigned long long)(idle_now>=launch_us?idle_now-launch_us:0),
            (unsigned long long)network_bytes,(unsigned long long)hit_bytes); fflush(logfp); idle_reports++;
        }
        status_write("running",NULL); progress_report_us=idle_now;
      }
      usleep(1000); continue;
    }
    if (!first_batch_us) first_batch_us=monotonic_us();
    aio_batches++; uint64_t returned=0;
    int error=deliver_batch(&req,&returned);
    cache_check_stage="acknowledgement";
    int copied=bw_reader_proc_copyout(game_pid,slot_addr,&check,sizeof check);
    if (copied) return cache_diagnostic("ack-header-copyout","copy=%d priorError=%d",copied,error);
    if (check.poison || check.state!=BW_PATH_PENDING || check.kind!=req.kind)
      return cache_diagnostic("ack-header","state=%u poison=%u kind=%u expectedKind=%u priorError=%d",
        check.state,check.poison,check.kind,req.kind,error);
    for (unsigned i=0;i<6;i++) if (check.original_args[i]!=req.original_args[i])
      return cache_diagnostic("ack-argument","index=%u actual=%llx expected=%llx priorError=%d",i,
        (unsigned long long)check.original_args[i],(unsigned long long)req.original_args[i],error);
    if (error) {
      uint32_t poison=1;
      bw_reader_proc_copyin(game_pid,&poison,slot_addr+offsetof(struct bw_path_request,poison),sizeof poison);
      if (strcmp(phase,"error")) failure("Bounded streaming stopped; staging will not be overwritten");
      return -1;
    }
    uint32_t state=BW_PATH_COMPLETE;
    if (bw_reader_proc_copyin(game_pid,&returned,slot_addr+offsetof(struct bw_path_request,final_return),sizeof returned) ||
        bw_reader_proc_copyin(game_pid,&state,slot_addr,sizeof state)) return -1;
  }
  return 0;
}
int bw_stream_run(void) {
  int result=1;
  /* Keep the normal app process name when this runs in its worker. */
#ifndef BW_NATIVE_READER_EMBEDDED
  __crt_syscall(464,(long)-1,"bw-stream");
#endif
  logfp=fopen("/data/blackwire/native-reader.log","a"); if (!logfp) { reader_io_released=1;return 1; }
  note("Blackwire general file-map native-path candidate starting");
#ifdef BW_READER_READY
  note("reader readiness candidate: retained installer completion and module identity required");
#endif
#ifdef BW_OWNED_VM
  note("owned scratch candidate: double bounded covering-entry RW snapshots; no next-list traversal");
#endif
  if (load_config()) goto done;
#ifdef BW_NATIVE_PRELOAD
  reader_early_active=reader_selection.source_kind==BW_PROVIDER_FOLDER||reader_selection.source_kind==BW_PROVIDER_CONTAINER;
#endif
  status_write("preflight","Checking PC files and fixed cache");
  if (bw_native_setup_idle(NULL) || find_game()>0) { failure("Selected game is already running"); goto done; }
  if (sceNetInit()<0 || calibrate_timeout() || connect_share() || BW_LT_CALL(BW_LT_BOOT_SAMPLES,check_eboot())) {
    failure(reader_startup_failure?reader_startup_failure:"PC share or staged game validation failed"); goto done;
  }
#ifdef BW_NATIVE_PRELOAD
  if(reader_early_active) {
    /* The same current PC/local/terminal preflight ran above. Do not enter the
     * late poll/hook or daemon 256 MiB stage path after any early-path result. */
    result=reader_preload_run();
    if(!result)status_write("stopped","Game exited; exact private reader inputs were released.");
    goto done;
  }
#endif
  if (prepare_stage()) { failure("Fixed cache unavailable, busy, or insufficient free space"); goto done; }
  launch_ctx ctx={.size=sizeof ctx}; char *argv[]={NULL}; launch_us=monotonic_us();
  status_write("launching","Starting selected game");
  struct bw_reader_user_observation user_service;
  int launch_result=bw_reader_foreground_user(&ctx.user,&user_service);
  fprintf(logfp,"foreground user before=%08x initializeCalled=%u initialize=%08x afterCalled=%u after=%08x\n",
    (unsigned)user_service.foreground_before,user_service.initialize_called,(unsigned)user_service.initialize_result,
    user_service.foreground_after_called,(unsigned)user_service.foreground_after); fflush(logfp);
  if (launch_result) { failure(launch_result==BW_READER_USER_INIT_FAILED?"User service initialization failed":"Foreground user could not be selected"); goto done; }
  if (bw_reader_runtime_before_launch(&reader_runtime) || reader_launch_current(NULL) || bw_reader_runtime_launch_begin(&reader_runtime)) {
    failure("Launch cancelled, connection changed, or exact title registration unavailable"); goto done;
  }
  launch_result=sceSystemServiceLaunchApp(title_id,argv,&ctx);
  bw_reader_runtime_launch_result(&reader_runtime,launch_result); reader_launch_issued=launch_result>=0;
  fprintf(logfp,"launch title=%s result=%08x\n",title_id,(unsigned)launch_result); fflush(logfp);
  if (launch_result<0) {
    char message[160]; snprintf(message,sizeof message,"Game launch failed (0x%08x); check title registration",(unsigned)launch_result);
    failure(message); goto done;
  }
  for (int i=0;i<10000 && game_pid<0;i++) { game_pid=find_game(); if (game_pid<0) usleep(1000); }
  if (game_pid<0) { failure("Game process did not appear"); goto done; }
  for (int i=0;i<5000;i++) {
    int32_t observed=game_state(game_pid);
    if (observed==BW_LIFECYCLE_STOPPED) { failure("Game exited during startup"); goto done; }
#ifdef BW_READER_READY
    if (observed==game_pid) {
      int ready=aio_original_ready();
      if (ready<0) { failure("Retained native readiness profile could not be verified"); goto done; }
      if (ready) break;
    }
#else
    if (observed==game_pid && aio_original_ready()) break;
#endif
    if (i==4999) { failure("Native AIO did not become ready"); goto done; }
    usleep(1000);
  }
  if (prepare_aio_pages()) {
    if (strcmp(phase,"error")) failure("Unable to prepare validated native reader");
    goto done;
  }
  int installed=1;
  for (int i=0;i<100 && installed==1;i++) {
    int32_t observed=game_state(game_pid);
    if (observed==BW_LIFECYCLE_STOPPED) { failure("Game exited before reader binding"); goto done; }
    if (observed!=game_pid) { usleep(25000); continue; }
    installed=install_aio_pointer();
    if (installed==1) usleep(25000);
  }
  if (installed) {
    uint32_t poison=1;
    bw_reader_proc_copyin(game_pid,&poison,slot_addr+offsetof(struct bw_path_request,poison),sizeof poison);
    failure(binding_write_attempted?"Native reader binding could not be verified; this attempt was stopped":"Native reader binding did not become stable; no route was installed");
    goto done;
  }
  status_write("running","PC streaming reader is ready");
  if (serve()) { if (strcmp(phase,"error")) failure("Reader stopped unexpectedly"); goto done; }
  result=0; status_write("stopped","Game exited");
done:
  /* An unresolved native read can still own staging bytes. Retain the file
   * lock and payload until that game exits; another title cannot reuse them.
   */
  /* The reusable app worker keeps all reader/cache state until the exact
   * process generation exits. A log-only exit never releases ownership. */
  while (bw_reader_runtime_end(&reader_runtime)) {
    if (game_pid<=0) game_pid=find_game();
    usleep(100000);
  }
  int io_closed=!bw_reader_container_close(&reader_container); /* owned DATA teardown before map/SMB */
  bw_files_destroy(&file_map);
  if (bw_derived_reader_source_destroy(&reader_derived_source)) {
    io_closed=0;
    note("Derived source teardown reported an error"); result=1;
  }
  reader_derived_admitted=0; memset(&reader_derived_claim,0,sizeof reader_derived_claim);
  bw_cache_destroy(&memory_cache);
  if (stage_fd>=0) { if(close(stage_fd))io_closed=0;stage_fd=-1; }
  if (smb) { smb2_destroy_context(smb);smb=NULL; }
  if(fclose(logfp))io_closed=0;logfp=NULL;reader_io_released=io_closed;if(!io_closed)result=1;return result;
}

static int reader_reset(void) {
#ifdef BW_NATIVE_PRELOAD
  if(reader_preloaded.busy||reader_preloaded.blocked)return -1;
  reader_early_active=0;
#endif
  bw_reader_container_close(&reader_container); /* consume prior DATA owner before reset */
  bw_files_destroy(&file_map);
  (void)bw_derived_reader_source_destroy(&reader_derived_source);
  if (reader_derived_source.state || reader_derived_source.busy) return -1;
  reader_derived_admitted=0; memset(&reader_derived_claim,0,sizeof reader_derived_claim);
  memset(&file_map,0,sizeof file_map); memset(&reader_source,0,sizeof reader_source);
  memset(&reader_selection,0,sizeof reader_selection); memset(&reader_connection_generation,0,sizeof reader_connection_generation);
  reader_startup=NULL;reader_startup_failure=NULL;reader_io_released=0;
  memset(source_name,0,sizeof source_name); memset(source_binding_hex,0,sizeof source_binding_hex);
  manifest_hash=manifest_count=0; memset(game_name,0,sizeof game_name); memset(title_id,0,sizeof title_id);
  memset(run_id,0,sizeof run_id); memset(game_root,0,sizeof game_root);
  eboot_size=timeout_ticks=launch_us=first_batch_us=0; logfp=NULL; smb=NULL; game_pid=-1; slot_addr=ampr_base=0;
  for (unsigned i=0;i<BW_PATH_ROUTE_COUNT;i++) routes[i].code=routes[i].pointer=routes[i].saved=routes[i].operations=0;
  native_poll=native_open=native_fstat=native_close=native_errno=0;
  network_bytes=hit_bytes=staged_bytes=high_water=0; stage_fd=game_cache_fd=-1;
  memset(&stage_proof,0,sizeof stage_proof); cache_challenge_attempted=native_read_submitted=0;
  memset(&stage_stat,0,sizeof stage_stat); memset(&memory_cache,0,sizeof memory_cache);
  aio_batches=armed_routes=binding_write_attempted=cache_diagnostics=0; cache_check_kind=UINT32_MAX; cache_check_stage="prepare";
  request_started_us=request_errno_pointer=0; progress_report_us=idle_report_us=0; idle_reports=0; profile_verbose=0; phase="preflight"; strcpy(last_message,"Preparing stream");
  owner_guard_site="none"; memset(&ready_anchor,0,sizeof ready_anchor); ready_native=0; ready_pinned=ready_reported=0; ready_deferred_reports=0;
  reader_launch_issued=0; memset(&reader_vm_stable,0,sizeof reader_vm_stable); reader_vm_reports=0;
  return 0;
}
int bw_native_reader_run(const struct bw_native_reader_request *request) {
  if(request&&request->released)memset(request->released,0,sizeof *request->released);
  if (pthread_mutex_trylock(&reader_gate)) return -1;
  struct bw_session_ops ops; bw_native_session_ps5_ops(&ops);
  if (bw_reader_runtime_begin(&reader_runtime,request,&ops)) { pthread_mutex_unlock(&reader_gate); return -1; }
  if (reader_reset()) { (void)bw_reader_runtime_end(&reader_runtime); pthread_mutex_unlock(&reader_gate); return -1; }
  reader_startup=request->startup;
  int result=bw_stream_run();
  /* The early log-open failure returns before bw_stream_run's cleanup. */
  struct bw_reader_selection selected; struct bw_setup_generation generation;
  if (!bw_reader_runtime_selection(&reader_runtime,&selected,&generation)) {
    while (bw_reader_runtime_end(&reader_runtime)) usleep(100000);
  }
  if(request->released){
    struct bw_native_reader_release released={0};
    memcpy(released.run,request->selection.run,sizeof released.run);
    /* Only this successful runtime's end path can reach here: accepted or
     * unknown launches remain inside exact-exit observation until discharged.
     * A blocked handoff/uncertain close is deliberately not a release proof. */
    released.runtime_released=1;released.io_released=reader_io_released;
#ifdef BW_NATIVE_PRELOAD
    released.private_released=!reader_preloaded.busy&&!reader_preloaded.blocked&&
      (!reader_preloaded.handoff.initialized||(reader_preloaded.finished&&reader_preloaded.handoff.released&&reader_preloaded.handoff.descriptors_closed));
#else
    released.private_released=1;
#endif
    *request->released=released;
  }
  reader_startup=NULL;pthread_mutex_unlock(&reader_gate); return result;
}
int bw_native_reader_close(const char *run) { return bw_reader_runtime_close(&reader_runtime,run); }
#ifndef BW_NATIVE_READER_EMBEDDED
static int reader_standalone_current(void *opaque) {
  const struct bw_reader_selection *expected=opaque; struct bw_reader_selection now;
  if (bw_reader_selection_load(&now,reader_selection_path)) return -1;
  return strcmp(now.source,expected->source) || strcmp(now.game,expected->game) || strcmp(now.title,expected->title) ||
    strcmp(now.run,expected->run) || strcmp(now.binding_hex,expected->binding_hex) || strcmp(now.prepared_record_hex,expected->prepared_record_hex) ||
    now.eboot_size!=expected->eboot_size || now.manifest_count!=expected->manifest_count || now.manifest_hash!=expected->manifest_hash ||
    now.source_kind!=expected->source_kind || now.source_policy!=expected->source_policy?-1:0;
}
static int reader_standalone_registered(void *opaque,const struct bw_reader_selection *selection) {
  return reader_standalone_current(opaque) || bw_native_registration_verify("/data/blackwire","/user/app","/system_data/priv/mms/app.db",selection)?-1:0;
}
int main(void) {
  struct bw_native_reader_request request={.current=reader_standalone_current,.before_launch=reader_standalone_registered};
  struct bw_share_config config={0};
  int status=bw_native_setup_read("/data/blackwire",&config,&request.connection); bw_share_clear(&config);
  if (status!=BW_SETUP_VALID || bw_reader_selection_load(&request.selection,reader_selection_path)) return 1;
  request.opaque=&request.selection; return bw_native_reader_run(&request);
}
#endif

