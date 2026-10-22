/*
 * Blackwire standalone web service. Serves the
 * game list on the console at http://127.0.0.1:5918/.
 */

#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#ifndef BW_LIBRARY_HOST_TEST
#include <ps5/kernel.h>
#else
int kernel_dynlib_handle(int,const char *,uint32_t *);
void *kernel_dynlib_dlsym(int,uint32_t,const char *);
void *kernel_dynlib_resolve(int,uint32_t,const char *);
#endif
#include <stdint.h>
#include <stdio.h>
#include "launch_timing.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
#ifdef BW_NATIVE_BACKEND
#include "native_backend.h"
static struct bw_native_backend *native_backend;
#if defined(BW_NATIVE_DASHBOARD) || defined(BW_WEB_ONLY)
#include "resident.h"
#ifndef BW_WEB_ONLY
#include "native_publish.h"
#endif
static struct bd_resident *dashboard;
#endif
#endif

#if defined(BW_MEDIA_PUBLIC_TRIAL) && defined(BW_WEB_ONLY) && !defined(BW_NATIVE_DASHBOARD)
#define BW_MEDIA_WEB_LAUNCHER 1
#include "native_document.h"
#include "native_setup.h"
#include "startup_runtime_idle.h"
#include "media_public_trial.h"
#include <pthread.h>
#endif
#ifndef BW_MEDIA_APP_PARENT
#define BW_MEDIA_APP_PARENT "/user/app"
#endif
#define TITLE_ID "PSBW50001"
#define APP_DIR BW_MEDIA_APP_PARENT "/" TITLE_ID
#define SYS_DIR APP_DIR "/sce_sys"
#define PARAM_PATH SYS_DIR "/param.json"
#define ICON_PATH SYS_DIR "/icon0.png"
#define LOG_PATH "/data/blackwire/library.log"
#define CONF_PATH "/data/blackwire/share.conf"
#define PORT 5918
#ifndef BW_NATIVE_BACKEND
#define PC_PORT 5917
#endif

#define INCASSET(name, file)                                                   \
  __asm__(".section .rodata\n"                                                 \
          ".global " #name "\n"                                                \
          ".global " #name "_size\n"                                           \
          ".align 16\n" #name ":\n"                                            \
          ".incbin \"" file "\"\n.L" #name "_end:\n" #name "_size:\n"          \
          ".quad .L" #name "_end - " #name "\n"                                \
          ".previous\n");                                                      \
  extern const uint8_t name[];                                                 \
  extern const size_t name##_size

INCASSET(page_html, "../desk/index.html");
INCASSET(brand_logo_png, "icon0.png");
#ifdef BW_MEDIA_WEB_LAUNCHER
INCASSET(param_json, "param.json");
#endif

typedef struct notify_request {
  char unused[45];
  char message[3075];
} notify_request_t;


int sceKernelSendNotificationRequest(int device, notify_request_t *req, size_t size, int flags);
int sceNetInit(void);
#ifdef BW_WEB_ONLY
#include "reader_user.h"
int sceSystemServiceLaunchWebBrowser(const char *,void *);
#endif
long __crt_syscall(long sysno, ...);

static void log_line(const char *msg);

static void name_this_process(void) {
  long rc = __crt_syscall(464, (long)-1, "blackwire.elf");

  if (rc < 0) {
    log_line("process name was not set");
  } else {
    log_line("process name blackwire.elf");
  }
}

static FILE *logfp;

static void log_line(const char *msg) {
  if (!logfp) {
    return;
  }
  fprintf(logfp, "%s\n", msg);
  fflush(logfp);
}

/* Startup-only diagnostic checkpoints. Never called from hot logging. */
#ifdef BW_STARTUP_TRACE
#ifndef BW_STARTUP_TRACE_DIRECTORY
#define BW_STARTUP_TRACE_DIRECTORY "/data/blackwire"
#endif
static int startup_trace_fd = -1;
static unsigned startup_trace_rows;
static void startup_mark(const char *phase) {
  const int saved = errno;
  char row[160];
  if (startup_trace_rows >= 32u) return;
  int n = snprintf(row, sizeof row, "BW core-isolation-01 pid=%ld row=%u %s\n",
                   (long)getpid(), ++startup_trace_rows, phase);
  if (n > 0 && (size_t)n < sizeof row) {
    /* FD1 may be an ELF-loader socket. No pipe fallback, flags or handlers. */
    (void)send(STDOUT_FILENO, row, (size_t)n, MSG_DONTWAIT | MSG_NOSIGNAL);
    struct stat st;
    if (startup_trace_fd >= 0 && !fstat(startup_trace_fd, &st) && S_ISREG(st.st_mode) &&
        st.st_nlink == 1 && st.st_size >= 0 && (uint64_t)st.st_size + (size_t)n <= 16384u) {
      size_t at = 0;
      for (unsigned attempt = 0; attempt < 4u && at < (size_t)n; attempt++) {
        ssize_t wrote = write(startup_trace_fd, row + at, (size_t)n - at);
        if (wrote > 0) at += (size_t)wrote;
        else if (wrote < 0 && errno == EINTR) continue;
        else break;
      }
      if (at == (size_t)n) (void)fsync(startup_trace_fd);
    }
  }
  errno = saved;
}
static void startup_trace_open(void) {
  const int saved = errno;
  struct stat st;char path[160];
  if (!lstat(BW_STARTUP_TRACE_DIRECTORY, &st) && S_ISDIR(st.st_mode) &&
      snprintf(path, sizeof path, BW_STARTUP_TRACE_DIRECTORY "/startup-core-01-%ld.log", (long)getpid()) > 0) {
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_NOFOLLOW, 0600);
    if (fd >= 0) {
      if (!fstat(fd, &st) && S_ISREG(st.st_mode) && st.st_nlink == 1 && st.st_size == 0) {
        startup_trace_fd = fd;
        int parent = open(BW_STARTUP_TRACE_DIRECTORY, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
        if (parent >= 0) { (void)fsync(parent); (void)close(parent); }
      } else (void)close(fd);
    }
  }
  errno = saved;
}
static void startup_trace_finish(void) {
  const int saved = errno;
  if (startup_trace_fd >= 0) { (void)close(startup_trace_fd); startup_trace_fd = -1; }
  errno = saved;
}
#else
#define startup_mark(phase) ((void)0)
#define startup_trace_open() ((void)0)
#define startup_trace_finish() ((void)0)
#endif

static void notify(const char *msg) {
  notify_request_t req;
  const int saved_errno = errno;

  memset(&req, 0, sizeof req);
  snprintf(req.message, sizeof req.message, "%s", msg);
  const int result = sceKernelSendNotificationRequest(0, &req, sizeof req, 0);
  log_line(msg);
  struct timespec at = {0};
  const int timed = clock_gettime(CLOCK_MONOTONIC, &at) == 0;
  char observed[512];
  snprintf(observed, sizeof observed,
           "[Blackwire notification] rc=%d monotonic=%lld.%09ld text=%.200s",
           result, timed ? (long long)at.tv_sec : -1LL,
           timed ? (long)at.tv_nsec : 0L, msg);
  log_line(observed);
  errno = saved_errno;
}

static int write_full(int fd, const void *data, size_t len) {
  const char *p = data;
  size_t off = 0;

  while (off < len) {
    ssize_t n = write(fd, p + off, len - off);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) {
      return -1;
    }
    off += (size_t)n;
  }
  return 0;
}

#ifndef BW_NATIVE_BACKEND
static int load_server(char *dest, size_t size) {
  FILE *fp = fopen(CONF_PATH, "r");
  char line[256];

  snprintf(dest, size, "%s", "127.0.0.1");
  if (!fp) {
    return -1;
  }
  while (fgets(line, sizeof line, fp)) {
    char *nl;
    if (strncmp(line, "server=", 7) != 0) {
      continue;
    }
    nl = strchr(line, '\n');
    if (nl) {
      *nl = 0;
    }
    nl = strchr(line, '\r');
    if (nl) {
      *nl = 0;
    }
    if (line[7]) {
      snprintf(dest, size, "%s", line + 7);
    }
    break;
  }
  fclose(fp);
  return 0;
}
#endif

#ifdef BW_MEDIA_WEB_LAUNCHER


static int asset_matches(const char *path, const uint8_t *data, size_t size) {
  struct stat st;
  uint8_t buffer[4096];
  int fd = open(path, O_RDONLY | O_NOFOLLOW);
  size_t done = 0;
  if (fd < 0) return 0;
  if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_nlink != 1 || st.st_size < 0 || (uint64_t)st.st_size != size) {
    close(fd); return 0;
  }
  while (done < size) {
    size_t take = size - done < sizeof buffer ? size - done : sizeof buffer;
    ssize_t got = read(fd, buffer, take);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0 || memcmp(buffer, data + done, (size_t)got) != 0) {
      close(fd); return 0;
    }
    done += (size_t)got;
  }
  return close(fd) == 0;
}

struct media_directories { struct stat app, sys; int app_created, sys_created; };
struct media_stage { const char *temporary, *final; dev_t dev; ino_t ino; int created, closed; };

static int media_same_directory(const char *path, const struct stat *expected) {
  struct stat st;
  return lstat(path, &st) == 0 && S_ISDIR(st.st_mode) && st.st_dev == expected->st_dev && st.st_ino == expected->st_ino;
}

static int media_inventory(const char *path, int system, int stages) {
  DIR *dir = opendir(path); struct dirent *entry; int result = 0;
  if (!dir) return -1;
  errno = 0;
  while ((entry = readdir(dir)) != NULL) {
    const char *name = entry->d_name;
    if (!strcmp(name, ".") || !strcmp(name, "..")) continue;
    if ((!system && !strcmp(name, "sce_sys")) ||
        (system && (!strcmp(name, "param.json") || !strcmp(name, "icon0.png"))) ||
        (system && stages && (!strcmp(name, ".blackwire-param.new") || !strcmp(name, ".blackwire-icon.new")))) continue;
    result = -1; break;
  }
  if (errno) result = -1;
  if (closedir(dir)) result = -1;
  return result;
}

static int media_parameter_field(void *opaque, const char *key, size_t size, const void *value, size_t length) {
  unsigned *seen = opaque, bit = 0; char text[96];
  if (size == 7 && !memcmp(key, "titleId", 7)) {
    bit = 1;
    if (bw_document_string(value, length, text, sizeof text) || strcmp(text, TITLE_ID)) return -1;
  } else if (size == 11 && !memcmp(key, "deeplinkUri", 11)) {
    bit = 2;
    if (bw_document_string(value, length, text, sizeof text) || strcmp(text, "http://127.0.0.1:5918/")) return -1;
  } else if (size == 23 && !memcmp(key, "applicationCategoryType", 23)) {
    bit = 4;
    if (length != 5 || memcmp(value, "66048", 5)) return -1;
  } else if (size == 19 && !memcmp(key, "localizedParameters", 19)) {
    bit = 8;
  } else return -1;
  if (*seen & bit) return -1;
  *seen |= bit; return 0;
}

static int media_parameter_owned(void) {
  struct stat st; unsigned seen = 0; unsigned char bytes[4096]; size_t done = 0;
  int fd = open(PARAM_PATH, O_RDONLY | O_NOFOLLOW), result = -1;
  if (fd < 0) return -1;
  if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1 || st.st_size <= 0 || (uint64_t)st.st_size > sizeof bytes) goto finish;
  while (done < (size_t)st.st_size) {
    ssize_t got = read(fd, bytes + done, (size_t)st.st_size - done);
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) goto finish;
    done += (size_t)got;
  }
  if (!bw_document_fields(bytes, done, media_parameter_field, &seen) && (seen & 7u) == 7u) result = 0;
finish:
  if (close(fd)) result = -1;
  return result;
}

static int media_regular_or_absent(const char *path) {
  struct stat st;
  if (lstat(path, &st)) return errno == ENOENT ? 0 : -1;
  return S_ISREG(st.st_mode) && st.st_nlink == 1 ? 0 : -1;
}

static int media_namespace_current(const struct media_directories *dirs, int stages) {
  return media_same_directory(APP_DIR, &dirs->app) && media_same_directory(SYS_DIR, &dirs->sys) &&
         !media_inventory(APP_DIR, 0, 0) && !media_inventory(SYS_DIR, 1, stages) &&
         !media_regular_or_absent(PARAM_PATH) && !media_regular_or_absent(ICON_PATH) &&
         (dirs->app_created || !media_parameter_owned());
}

static int media_directories_open(struct media_directories *dirs) {
  struct stat parent;
  memset(dirs, 0, sizeof *dirs);
  if (lstat(BW_MEDIA_APP_PARENT, &parent) || !S_ISDIR(parent.st_mode)) return -1;
  if (!mkdir(APP_DIR, 0755)) dirs->app_created = 1;
  else if (errno != EEXIST) return -1;
  if (lstat(APP_DIR, &dirs->app) || !S_ISDIR(dirs->app.st_mode)) return -1;
  if (!dirs->app_created && (lstat(SYS_DIR, &dirs->sys) || !S_ISDIR(dirs->sys.st_mode) ||
      media_inventory(APP_DIR, 0, 0) || media_parameter_owned())) return -1;
  if (!mkdir(SYS_DIR, 0755)) dirs->sys_created = 1;
  else if (errno != EEXIST) return -1;
  if (lstat(SYS_DIR, &dirs->sys) || !S_ISDIR(dirs->sys.st_mode)) return -1;
  return media_namespace_current(dirs, 0) ? 0 : -1;
}

static int media_stage_asset(struct media_stage *stage, const void *bytes, size_t size) {
  if (asset_matches(stage->final, bytes, size)) return 0;
  int fd = open(stage->temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0644);
  struct stat st; int result = -1;
  if (fd < 0) return -1;
  stage->created = 1;
  if (!fstat(fd, &st)) {
    stage->dev = st.st_dev; stage->ino = st.st_ino;
    if (!write_full(fd, bytes, size) && !fsync(fd)) result = 0;
  }
  if (close(fd)) result = -1;
  else stage->closed = 1;
  return result;
}

static void media_stage_cleanup(struct media_stage *stage) {
  struct stat st;
  if (stage->created && stage->closed && !lstat(stage->temporary, &st) && S_ISREG(st.st_mode) &&
      st.st_dev == stage->dev && st.st_ino == stage->ino) (void)unlink(stage->temporary);
}

static void media_empty_created_directories(const struct media_directories *dirs) {
  /* Only newly created, still-identical empty directories can be removed. */
  if (dirs->sys_created && media_same_directory(SYS_DIR, &dirs->sys)) (void)rmdir(SYS_DIR);
  if (dirs->app_created && media_same_directory(APP_DIR, &dirs->app)) (void)rmdir(APP_DIR);
}

static int media_prepare_files(void) {
  struct media_directories dirs;
  struct media_stage stages[2] = {
    {SYS_DIR "/.blackwire-param.new", PARAM_PATH, 0, 0, 0, 0},
    {SYS_DIR "/.blackwire-icon.new", ICON_PATH, 0, 0, 0, 0}
  };
  int result = -1;
  if (media_directories_open(&dirs)) goto finish;
  if (media_stage_asset(&stages[0], param_json, param_json_size) ||
      media_stage_asset(&stages[1], brand_logo_png, brand_logo_png_size)) goto finish;
  for (unsigned i = 0; i < 2; i++) {
    if (!media_namespace_current(&dirs, 1)) goto finish;
    if (stages[i].created) {
      struct stat staged;
      if (!stages[i].closed || lstat(stages[i].temporary, &staged) || !S_ISREG(staged.st_mode) ||
          staged.st_dev != stages[i].dev || staged.st_ino != stages[i].ino ||
          rename(stages[i].temporary, stages[i].final)) goto finish;
    }
  }
  if (media_namespace_current(&dirs, 0) && asset_matches(PARAM_PATH, param_json, param_json_size) &&
      asset_matches(ICON_PATH, brand_logo_png, brand_logo_png_size)) result = 0;
finish:
  for (unsigned i = 0; i < 2; i++) media_stage_cleanup(&stages[i]);
  if (result) media_empty_created_directories(&dirs);
  return result;
}

/* The listener is live before this one bounded startup reservation worker is
 * scheduled. Only idle observations may repeat; registration never repeats. */
/* Private explicit-action prototype. No startup SDK load or scheduling. */
int sceKernelLoadStartModule(const char *,size_t,const void *,uint32_t,void *,int *);
struct media_public_resident {
  pthread_mutex_t lock;pthread_t thread;unsigned running,reserved,release_unknown;
  char nonce[33];double deadline;struct bw_media_public_record record;
};
static struct media_public_resident media_trial={.lock=PTHREAD_MUTEX_INITIALIZER};
static double media_now(void){struct timespec t;return clock_gettime(CLOCK_MONOTONIC,&t)?-1:(double)t.tv_sec+(double)t.tv_nsec/1e9;}
static const char *media_setup_status(void){
  pthread_mutex_lock(&media_trial.lock);
  const char *status=media_trial.release_unknown?"ownership_held":media_trial.running?"explicit_trial_running":bw_media_public_phase_name(media_trial.record.phase);
  pthread_mutex_unlock(&media_trial.lock);return status;
}
static int media_trial_current(void *opaque){
  struct media_public_resident *s=opaque;double at=media_now();
  if(!s->reserved||at<0||at>=s->deadline||bw_native_backend_external_current(native_backend,s->nonce)||bw_native_setup_idle(NULL))return -1;
  return 0;
}
static int media_trial_lease(void *opaque){
  struct media_public_resident *s=opaque;double at=media_now();
  return !s->reserved||at<0||at>=s->deadline||bw_native_backend_external_current(native_backend,s->nonce)?-1:0;
}
static int media_trial_idle(void *opaque){
  struct media_public_resident *s=opaque;
  struct bw_startup_idle_request idle={.deadline=s->deadline,.current=media_trial_lease,.opaque=s};
  return media_trial_current(s)||bw_startup_idle_ps5(&idle)||media_trial_lease(s)?-1:0;
}
static int media_trial_title_idle(void *opaque,const char title[10]){
  (void)opaque;return bw_native_backend_registration_pending(native_backend,title)==0?0:-1;
}
static int media_trial_assets(void *opaque,int stage){
  if(media_trial_idle(opaque))return -1;
  if(stage)return media_prepare_files();
  struct media_directories dirs={0};
  return lstat(APP_DIR,&dirs.app)||lstat(SYS_DIR,&dirs.sys)||!media_namespace_current(&dirs,0)||
    !asset_matches(PARAM_PATH,param_json,param_json_size)||!asset_matches(ICON_PATH,brand_logo_png,brand_logo_png_size)?-1:0;
}
static int media_trial_sdk(void *opaque,struct bw_media_public_sdk *api){
  if(media_trial_idle(opaque))return -1;memset(api,0,sizeof *api);uint32_t handle=0;
  if(kernel_dynlib_handle(-1,"libSceAppInstUtil.sprx",&handle)){
    if(media_trial_idle(opaque))return -1;
    int loaded=sceKernelLoadStartModule("/system/common/lib/libSceAppInstUtil.sprx",0,NULL,0,NULL,NULL);
    if(loaded>0)handle=(uint32_t)loaded;
    else if(kernel_dynlib_handle(-1,"libSceAppInstUtil.sprx",&handle))return -1;
  }
  api->initialize=(int (*)(void))kernel_dynlib_dlsym(-1,handle,"sceAppInstUtilInitialize");
  api->terminate=(int (*)(void))kernel_dynlib_dlsym(-1,handle,"sceAppInstUtilTerminate");
  /* Exact normal Payload Manager export; no internal function/hook is used. */
  api->title_dir=(int (*)(const char *,const char *,void *))kernel_dynlib_resolve(-1,handle,"Wudg3Xe3heE");
  api->install_all=(int (*)(void *))kernel_dynlib_dlsym(-1,handle,"sceAppInstUtilAppInstallAll");
  return media_trial_idle(opaque)||!api->initialize||!api->terminate||(!api->title_dir&&!api->install_all)?-1:0;
}
static double media_trial_now(void *opaque){(void)opaque;return media_now();}
static int media_trial_wait(void *opaque,unsigned milliseconds){
  if(media_trial_lease(opaque))return -1;struct timespec delay={(time_t)(milliseconds/1000u),(long)(milliseconds%1000u)*1000000L};
  return nanosleep(&delay,NULL)||media_trial_lease(opaque)?-1:0;
}
static void media_trial_report(void *opaque,const struct bw_media_public_record *record){
  struct media_public_resident *s=opaque;pthread_mutex_lock(&s->lock);s->record=*record;pthread_mutex_unlock(&s->lock);
}
static void *media_public_worker(void *opaque){
  struct media_public_resident *s=opaque;pthread_mutex_lock(&s->lock);struct bw_media_public_record record=s->record;pthread_mutex_unlock(&s->lock);
  struct bw_media_public_request request={.apps=BW_MEDIA_APP_PARENT,.database="/system_data/priv/mms/app.db",.opaque=s,
    .current=media_trial_idle,.title_idle=media_trial_title_idle,.assets=media_trial_assets,.sdk_open=media_trial_sdk,
    .now=media_trial_now,.wait=media_trial_wait,.report=media_trial_report};
  (void)bw_media_public_run(&request,&record);
  /* A platform return and failed row observation are not an active game/copy.
   * Release only this known inactive reservation; never clear backend unknown
   * controls, a different nonce, or any registration ownership ledger. */
  int released=bw_native_backend_external_release_idle(native_backend,s->nonce);
  pthread_mutex_lock(&s->lock);s->record=record;s->running=0;
  if(released)s->release_unknown=1;else{s->reserved=0;bw_native_sensitive_clear(s->nonce,sizeof s->nonce);}
  pthread_mutex_unlock(&s->lock);
  notify(released?"Blackwire Media trial reservation needs status; no repeat was submitted.":
    record.registered?"Blackwire Media registration was observed.":"Blackwire Media registration is unconfirmed. No repeat was submitted; game actions remain available.");
  return NULL;
}
static int media_public_reply(struct bw_native_response *out,int status,const char *bytes){
  out->bytes=strdup(bytes);if(!out->bytes)return -1;out->status=status;out->type="application/json";out->size=strlen(bytes);return 0;
}
static int media_no_field(void *opaque,const char *key,size_t n,const void *raw,size_t size){(void)opaque;(void)key;(void)n;(void)raw;(void)size;return -1;}
static int media_public_handle(const char *method,const char *url,const void *body,size_t size,const char *token,struct bw_native_response *out){
  int action=!strcmp(url,"/api/media/register-public-sdk"),status=!strcmp(url,"/api/media/public-sdk-status");if(!action&&!status)return 1;
  if(status){
    if(strcmp(method,"GET"))return media_public_reply(out,405,"{\"error\":\"Media status requires GET\"}");
    char bytes[640],platform[32];pthread_mutex_lock(&media_trial.lock);struct bw_media_public_record record=media_trial.record;
    unsigned running=media_trial.running,held=media_trial.reserved||media_trial.release_unknown;pthread_mutex_unlock(&media_trial.lock);
    if(record.returned)snprintf(platform,sizeof platform,"%d",record.platform_status);else snprintf(platform,sizeof platform,"null");
    snprintf(bytes,sizeof bytes,"{\"titleId\":\"PSBW50001\",\"phase\":\"%s\",\"mode\":\"%s\",\"running\":%s,\"registrationAttempted\":%s,\"platformReturned\":%s,\"platformStatus\":%s,\"registered\":%s,\"reservationHeld\":%s,\"reason\":\"%s\"}",
      bw_media_public_phase_name(record.phase),bw_media_public_mode_name(record.mode),running?"true":"false",record.attempted?"true":"false",record.returned?"true":"false",platform,record.registered?"true":"false",held?"true":"false",record.reason?record.reason:"not_requested");
    return media_public_reply(out,200,bytes);
  }
  if(strcmp(method,"POST"))return media_public_reply(out,405,"{\"error\":\"Media registration requires explicit authenticated POST\"}");
  if(!token||strlen(token)!=32||strspn(token,"0123456789abcdef")!=32)return media_public_reply(out,403,"{\"error\":\"Use the current resident action token\"}");
  if(!body||size>64||bw_document_fields(body,size,media_no_field,NULL))return media_public_reply(out,400,"{\"error\":\"Media trial body must be an empty JSON object\"}");
  pthread_mutex_lock(&media_trial.lock);
  if(media_trial.running||media_trial.reserved||media_trial.release_unknown){pthread_mutex_unlock(&media_trial.lock);return media_public_reply(out,409,"{\"error\":\"A Media observation already owns its reservation\"}");}
  char nonce[33];double began=media_now();
  if(began<0||bw_native_setup_nonce(nonce)){pthread_mutex_unlock(&media_trial.lock);return media_public_reply(out,503,"{\"error\":\"Media trial could not create an owner\"}");}
  int accepted=bw_native_backend_external_reserve(native_backend,nonce,token);
  if(accepted){pthread_mutex_unlock(&media_trial.lock);return media_public_reply(out,accepted>0?409:403,accepted>0?"{\"error\":\"Wait for current game or file work\"}":"{\"error\":\"Use the current resident action token\"}");}
  memcpy(media_trial.nonce,nonce,sizeof nonce);media_trial.reserved=1;media_trial.deadline=began+90.0;
  /* Nothing has been staged or submitted. Discard only this empty reservation
   * if preexisting unknown controls refuse it; keep those original controls. */
  if(media_trial_lease(&media_trial)){
    int released=bw_native_backend_external_release(native_backend,nonce);
    if(released)media_trial.release_unknown=1;else{media_trial.reserved=0;bw_native_sensitive_clear(media_trial.nonce,sizeof media_trial.nonce);}
    pthread_mutex_unlock(&media_trial.lock);return media_public_reply(out,409,"{\"error\":\"Existing control ownership is unconfirmed; Media was not started\"}");
  }
  media_trial.running=1;pthread_attr_t attr;int initialized=!pthread_attr_init(&attr),started=-1;
  if(initialized){started=pthread_attr_setstacksize(&attr,4u*1024u*1024u);if(!started)started=pthread_attr_setdetachstate(&attr,PTHREAD_CREATE_DETACHED);
    if(!started)started=pthread_create(&media_trial.thread,&attr,media_public_worker,&media_trial);pthread_attr_destroy(&attr);}
  if(started){media_trial.running=0;int released=bw_native_backend_external_release_idle(native_backend,nonce);
    if(released)media_trial.release_unknown=1;else{media_trial.reserved=0;bw_native_sensitive_clear(media_trial.nonce,sizeof media_trial.nonce);}}
  pthread_mutex_unlock(&media_trial.lock);
  return media_public_reply(out,started?503:202,started?"{\"error\":\"Media trial worker did not start; no API call was submitted\"}":"{\"ok\":true,\"message\":\"Explicit Media trial accepted. Public batch fallback may register every pending staged application. No automatic replay.\"}");
}

#endif

static void send_bytes(int fd, int code, const char *status, const char *type, const void *data, size_t len) {
  char hdr[256];
  int n = snprintf(hdr, sizeof hdr,
                   "HTTP/1.0 %d %s\r\nContent-Type: %s\r\nContent-Length: %llu\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\nConnection: close\r\n\r\n",
                   code, status, type, (unsigned long long)len);

  if (n < 0 || (size_t)n >= sizeof hdr) return;
  write_full(fd, hdr, (size_t)n);
  write_full(fd, data, len);
}

static void send_text(int fd, int code, const char *status, const char *type, const char *text) {
  send_bytes(fd, code, status, type, text, strlen(text));
}

#ifndef BW_NATIVE_BACKEND
static int connect_to(const char *host, int port) {
  struct sockaddr_in addr;
  struct timeval tv;
  int fd;

  memset(&addr, 0, sizeof addr);
  addr.sin_family = AF_INET;
  addr.sin_port = htons((unsigned short)port);
  addr.sin_addr.s_addr = inet_addr(host);
  if (addr.sin_addr.s_addr == INADDR_NONE) {
    return -1;
  }
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    return -1;
  }
  tv.tv_sec = 75;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  if (connect(fd, (struct sockaddr *)&addr, sizeof addr) != 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static void proxy(int client, const char *host, const char *method, const char *url, const char *body, size_t body_size) {
  char req[768];
  char buf[8192];
  int fd = connect_to(host, PC_PORT);
  int n;

  if (fd < 0) {
    send_text(client, 502, "Bad Gateway", "application/json", "{\"error\":\"PC is not answering\"}");
    return;
  }
  n = snprintf(req, sizeof req,
               "%s %s HTTP/1.0\r\nHost: %s\r\nContent-Type: application/json\r\nContent-Length: %llu\r\nConnection: close\r\n\r\n",
               method, url, host, (unsigned long long)body_size);
  if (n < 0 || (size_t)n >= sizeof req || write_full(fd, req, (size_t)n) != 0 ||
      (body_size && write_full(fd, body, body_size) != 0)) {
    close(fd);
    send_text(client, 502, "Bad Gateway", "application/json", "{\"error\":\"PC is not answering\"}");
    return;
  }
  for (;;) {
    ssize_t got = read(fd, buf, sizeof buf);
    if (got <= 0) {
      break;
    }
    if (write_full(client, buf, (size_t)got) != 0) {
      break;
    }
  }
  close(fd);
}
#endif

static int read_request(int fd, char *buf, size_t cap) {
  size_t n = 0;
  size_t wanted = 0;

  while (n + 1 < cap) {
    ssize_t got = read(fd, buf + n, cap - 1 - n);
    if (got <= 0) {
      break;
    }
    n += (size_t)got;
    buf[n] = 0;
    if (!wanted) {
      char *headers_end = strstr(buf, "\r\n\r\n");
      if (headers_end) {
        char *line = strstr(buf, "\r\n");
        size_t body_size = 0;
        int seen_length = 0;

        while (line && line < headers_end) {
          char *next;
          line += 2;
          next = strstr(line, "\r\n");
          if (!next || next > headers_end) return -1;
          if (strncasecmp(line, "Transfer-Encoding:", 18) == 0) return -1;
          if (strncasecmp(line, "Content-Length:", 15) == 0) {
            char *end;
            unsigned long length;
            if (seen_length++) return -1;
            errno = 0;
            length = strtoul(line + 15, &end, 10);
            while (end < next && (*end == ' ' || *end == '\t')) end++;
            if (errno || end != next || length > 4096) return -1;
            body_size = (size_t)length;
          }
          line = next;
        }
        wanted = (size_t)(headers_end + 4 - buf) + body_size;
        if (wanted >= cap) return -1;
      }
    }
    if (wanted && n >= wanted) return n == wanted ? (int)wanted : -1;
  }
  buf[n] = 0;
  return -1;
}
#ifdef BW_NATIVE_BACKEND
static int native_header(const char *request,const char *key,char *out,size_t capacity){
  const char *end=strstr(request,"\r\n\r\n"),*line=strstr(request,"\r\n");int found=0;
  if(!end||!line)return -1;
  while(line<end){
    line+=2;const char *next=strstr(line,"\r\n"),*colon=next?memchr(line,':',(size_t)(next-line)):NULL;if(!next||next>end)return -1;
    if(colon&&(size_t)(colon-line)==strlen(key)&&!strncasecmp(line,key,strlen(key))){
      if(found++)return -1;const char *value=colon+1,*last=next;while(value<last&&(*value==' '||*value=='\t'))value++;while(last>value&&(last[-1]==' '||last[-1]=='\t'))last--;
      size_t size=(size_t)(last-value);if(size>=capacity)return -1;memcpy(out,value,size);out[size]=0;
    }line=next;
  }return found;
}
#endif

#if defined(BW_NATIVE_BACKEND) && defined(BW_HOME_STARTUP)
static int native_reader_peer(int fd){
  struct sockaddr_storage peer;memset(&peer,0,sizeof peer);socklen_t used=sizeof peer;
  if(getpeername(fd,(struct sockaddr *)&peer,&used)||peer.ss_family!=AF_INET||used!=sizeof(struct sockaddr_in))return 0;
  const struct sockaddr_in *address=(const struct sockaddr_in *)&peer;
  return ntohl(address->sin_addr.s_addr)==INADDR_LOOPBACK;
}
#endif

#ifndef BW_RESIDENT_BUILD_ID
#define BW_RESIDENT_BUILD_ID "standalone-home-socket-repair-20261021-01"
#endif
#if defined(BW_NATIVE_PRELOAD) || defined(BW_WEB_ONLY)
#include "preload_bundle.h"
#endif
static int health_document(char *out,size_t capacity){
  char reader[65]="unavailable";
  const char *media="disabled";
#ifdef BW_MEDIA_WEB_LAUNCHER
  media=media_setup_status();
#endif
#if defined(BW_NATIVE_PRELOAD) || defined(BW_WEB_ONLY)
  for(unsigned i=0;i<32;i++)snprintf(reader+2*i,3,"%02x",bw_preload_embedded_image.sha256[i]);
#endif
  int n=snprintf(out,capacity,"{\"ok\":true,\"buildId\":\"%s\",\"readerSha256\":\"%s\",\"mediaSetupStatus\":\"%s\"}",BW_RESIDENT_BUILD_ID,reader,media);
  return n<0||(size_t)n>=capacity?-1:n;
}

static void serve(int fd, const char *host) {
  char req[8192];
  char method[8];
  char url[4096];
  char *body;
  int request_size;
  size_t body_size;
  struct timeval tv;

#ifdef SO_NOSIGPIPE
  int no_sigpipe=1;
  if(setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&no_sigpipe,sizeof no_sigpipe)){
    log_line("client socket could not disable SIGPIPE");return;
  }
#endif

  tv.tv_sec = 8;
  tv.tv_usec = 0;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
  request_size = read_request(fd, req, sizeof req);
  if (request_size < 0 || sscanf(req, "%7s %4095s", method, url) != 2) {
    send_text(fd, 400, "Bad Request", "text/plain", "bad request");
#ifdef BW_NATIVE_BACKEND
    bw_native_sensitive_clear(req,sizeof req);
#endif
    return;
  }
  body = strstr(req, "\r\n\r\n");
  if (body) {
    body += 4;
  }
  body_size = body ? (size_t)(request_size - (body - req)) : 0;
  if (strcmp(method, "GET") == 0 && (strcmp(url, "/") == 0 || strcmp(url, "/ps5") == 0)) {
    send_bytes(fd, 200, "OK", "text/html; charset=utf-8", page_html, page_html_size);
  } else if (strcmp(method, "GET") == 0 && strcmp(url, "/assets/blackwire-logo.png") == 0) {
    send_bytes(fd, 200, "OK", "image/png", brand_logo_png, brand_logo_png_size);
  } else if (strcmp(method, "GET") == 0 && strcmp(url, "/health") == 0) {
    char health[256];int length=health_document(health,sizeof health);
    if(length<0)send_text(fd,503,"Service Unavailable","application/json","{\"ok\":false}");
    else send_bytes(fd,200,"OK","application/json",health,(size_t)length);
#ifdef BW_NATIVE_BACKEND
  } else {
    (void)host;
    struct bw_native_response response={0};char token[33]={0};
    if(!strcmp(method,"POST")){
      char content_type[96]={0};int content=native_header(req,"Content-Type",content_type,sizeof content_type),auth=native_header(req,"X-Blackwire-Token",token,sizeof token);
      if(content<0||auth<0){send_text(fd,400,"Bad Request","application/json","{\"error\":\"Ambiguous native request headers\"}");goto native_done;}
      if(content!=1||strncasecmp(content_type,"application/json",16)||(content_type[16]&&content_type[16]!=';')){
        send_text(fd,415,"Unsupported Media Type","application/json","{\"error\":\"Use Blackwire's controls to send a JSON request\"}");goto native_done;
      }
    }
    int handled=1;
#ifdef BW_MEDIA_WEB_LAUNCHER
    handled=media_public_handle(method,url,body,body_size,token,&response);
#endif
#ifdef BW_HOME_STARTUP
    if(!strcmp(url,"/api/reader/bootstrap")){
      if(strcmp(method,"POST")){send_text(fd,405,"Method Not Allowed","application/json","{\"error\":\"Native reader POST required\"}");goto native_done;}
      char reader[32]={0},origin[256]={0},referer[256]={0},content[96]={0};
      int reader_seen=native_header(req,"X-Blackwire-Reader",reader,sizeof reader),origin_seen=native_header(req,"Origin",origin,sizeof origin),
        referer_seen=native_header(req,"Referer",referer,sizeof referer),type_seen=native_header(req,"Content-Type",content,sizeof content);
      int native_headers=reader_seen==1&&!strcmp(reader,"home/1")&&type_seen==1&&!strcmp(content,"application/json");
      int reader_loopback=native_reader_peer(fd),reader_origin=origin_seen!=0||referer_seen!=0;
      handled=bw_native_backend_home_claim(native_backend,body,body_size,reader_loopback,native_headers,reader_origin,&response);
      char startup_observation[224];
      snprintf(startup_observation,sizeof startup_observation,"[Blackwire HOME] response status=%u handler=%d loopback=%d headers=%d origin=%d bodyBytes=%zu",
        response.status,handled,reader_loopback,native_headers,reader_origin,body_size);
      log_line(startup_observation);
      /* home/1 readers require this exact media type. Keep the established
       * startup wire contract without changing or rebuilding their reader. */
      if(!handled)response.type="application/json";
      goto native_response;
    }
#endif
#if defined(BW_NATIVE_DASHBOARD) || defined(BW_WEB_ONLY)
    if(handled==1)handled=bd_resident_handle(dashboard,method,url,body,body_size,token,&response);
#endif
    if(handled==1)handled=bw_native_backend_handle_authenticated(native_backend,method,url,body,body_size,token,&response);
#ifdef BW_HOME_STARTUP
native_response:
#endif
    if(handled) {
      send_text(fd,503,"Service Unavailable","application/json","{\"error\":\"Native status unavailable\"}");
    } else {
      const char *status=response.status==200?"OK":response.status==202?"Accepted":response.status==400?"Bad Request":response.status==403?"Forbidden":response.status==404?"Not Found":
        response.status==405?"Method Not Allowed":response.status==409?"Conflict":"Service Unavailable";
      send_bytes(fd,response.status,status,response.type,response.bytes,response.size);
    }
native_done:
    bw_native_response_clear(&response);
    bw_native_sensitive_clear(token,sizeof token);bw_native_sensitive_clear(req,sizeof req);
#else
  } else if (strcmp(method, "GET") == 0 &&
             (strncmp(url, "/api/state", 10) == 0 || strncmp(url, "/cover", 6) == 0)) {
    proxy(fd, host, "GET", url, NULL, 0);
  } else if (strcmp(method, "POST") == 0 &&
             (strcmp(url, "/api/play") == 0 || strcmp(url, "/api/close") == 0 ||
              strcmp(url, "/api/send") == 0 || strcmp(url, "/api/settings") == 0 ||
              strcmp(url, "/api/setup") == 0)) {
    /* Preparation returns a desktop job immediately; bulk uploads never occupy
       this single-threaded console server while the job runs. */
    proxy(fd, host, "POST", url, body, body_size);
  } else {
    send_text(fd, 404, "Not Found", "text/plain", "not found");
#endif
  }
}

int main(void) {
  char host[64]={0};
  int srv = -1;
  int yes = 1;
  struct sockaddr_in addr;

  startup_mark("main.enter");
  mkdir("/data/blackwire", 0755);
  startup_mark("trace.open.before");
  startup_trace_open();
  startup_mark("trace.open.after");
  logfp = fopen(LOG_PATH, "a");
  BW_LT_LOGGER(log_line);
  log_line("library start");
  name_this_process();
  startup_mark("net.init.before");
  sceNetInit();
  startup_mark("net.init.after");
#ifndef BW_NATIVE_BACKEND
  load_server(host, sizeof host);
#endif

  startup_mark("listen.before");
  srv = socket(AF_INET, SOCK_STREAM, 0);
  if (srv >= 0) {
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(srv, 8) != 0) {
      log_line("page port unavailable; preserving the existing launcher");
      close(srv);
      srv = -1;
    } else {
      log_line("listening 5918");
    }
  } else {
    log_line("page socket failed");
  }
  startup_mark(srv >= 0 ? "listen.after.ok" : "listen.after.failed");
  /* Do not install another tile/backend or leave an idle duplicate daemon
     behind when an existing launcher already owns this same port. */
  if(srv<0){startup_trace_finish();if(logfp)fclose(logfp);return 1;}
#ifdef BW_NATIVE_BACKEND
  startup_mark("backend.create.before");
  native_backend=bw_native_backend_create(NULL,NULL);
  startup_mark(native_backend ? "backend.create.after.ok" : "backend.create.after.failed");
  if(!native_backend){startup_trace_finish();close(srv);if(logfp)fclose(logfp);return 1;}
#if defined(BW_NATIVE_DASHBOARD) || defined(BW_WEB_ONLY)
  /* The native product owns its separate cover-app publisher. The standalone
     product serves the web page and registers only an ordinary Media URL. */
#ifndef BW_WEB_ONLY
  if(bd_native_publish(native_backend,log_line))notify("Blackwire browser is available; native Dashboard installation needs attention.");
#endif
  startup_mark("resident.create.before");
  dashboard=bd_resident_create_with_feedback(native_backend,notify);
  startup_mark(dashboard ? "resident.create.after.ok" : "resident.create.after.failed");
  if(!dashboard)log_line("Blackwire library service could not start; native backend controls remain available");
#endif
  startup_mark("refresh.before");
  (void)bw_native_backend_refresh(native_backend);
  startup_mark("refresh.after");
#endif
  name_this_process();
  startup_mark("running.notify.before");
  notify("Blackwire is running");
  startup_mark("running.notify.after");
#if defined(BW_WEB_ONLY) && !defined(BW_TEST_PC_BROWSER_ONLY)
  /* One ordinary Sony browser request after the listener/backend are ready.
   * The Media shortcut opens this same page; no native Dashboard is installed. */
  uint32_t browser_user;struct bw_reader_user_observation browser_user_observation;
  startup_mark("browser.user.before");
  int browser_result=bw_reader_foreground_user(&browser_user,&browser_user_observation);
  startup_mark("browser.user.after");
  if(!browser_result){
    startup_mark("browser.open.before");
    browser_result=sceSystemServiceLaunchWebBrowser("http://127.0.0.1:5918/",NULL);
    startup_mark("browser.open.after");
  }
  char browser_message[128];
  snprintf(browser_message,sizeof browser_message,"Blackwire browser-open returned %d",browser_result);
  log_line(browser_message);
  if(browser_result)notify("Blackwire is ready at this PS5 address on port 5918. Browser opening needs attention.");
#endif
  startup_mark("accept.loop");
  startup_trace_finish();
  for (;;) {
    if (srv >= 0) {
      fd_set ready;
      struct timeval wait;

      FD_ZERO(&ready);
      FD_SET(srv, &ready);
      wait.tv_sec = 0;
      wait.tv_usec = 20000;
      if (select(srv + 1, &ready, NULL, NULL, &wait) > 0) {
        int fd = accept(srv, NULL, NULL);
        if (fd >= 0) {
          serve(fd, host);
          close(fd);
        }
      }
    }
  }
}
