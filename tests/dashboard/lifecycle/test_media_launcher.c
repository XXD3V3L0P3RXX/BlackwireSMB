/* Actual main.c Media publisher, synthetic normal Sony APIs and local files. */
#define BW_LIBRARY_HOST_TEST 1
#define BW_NATIVE_BACKEND 1
#define BW_WEB_ONLY 1
#define main fixture_resident_main
#include "../../../src/blackwire/library/main.c"
#undef main

static unsigned cases, checks, registrations, initializations, writes;
static int missing_installer, missing_symbol, init_result, install_result;
static int fault_write, fault_close, fault_icon_rename;
static char last_notification[3075];

static void check(int value, int line) {
  checks++;
  if (!value) { fprintf(stderr, "Media fixture failed at line %d case %u errno %d\n", line, cases, errno); abort(); }
}
#define CHECK(value) check(!!(value), __LINE__)

ssize_t __real_write(int, const void *, size_t);
int __real_close(int);
int __real_rename(const char *, const char *);
ssize_t __wrap_write(int fd, const void *bytes, size_t size) {
  writes++;
  if (fault_write) { fault_write = 0; errno = EIO; return -1; }
  return __real_write(fd, bytes, size);
}
int __wrap_close(int fd) {
  int result = __real_close(fd);
  if (fault_close) { fault_close = 0; errno = EIO; return -1; }
  return result;
}
int __wrap_rename(const char *from, const char *to) {
  if (fault_icon_rename && !strcmp(to, ICON_PATH)) { fault_icon_rename = 0; errno = EIO; return -1; }
  return __real_rename(from, to);
}

static int api_init(void) { initializations++; return init_result; }
static int api_simple(void) { return 0; }
static int api_user(void *arg) { CHECK(!arg); return 0; }
static int api_install(const char *title, const char *parent, void *arg) {
  registrations++;
  CHECK(!strcmp(title, "PSBW50001"));
  CHECK(!strcmp(parent, BW_MEDIA_APP_PARENT "/"));
  CHECK(!arg);
  CHECK(asset_matches(PARAM_PATH, param_json, param_json_size));
  CHECK(asset_matches(ICON_PATH, brand_logo_png, brand_logo_png_size));
  CHECK(!media_inventory(APP_DIR, 0, 0));
  CHECK(!media_inventory(SYS_DIR, 1, 0));
  return install_result;
}

int kernel_dynlib_handle(int pid, const char *name, uint32_t *handle) {
  CHECK(pid == -1);
  if (!strcmp(name, APPINST_MODULE)) { if (missing_installer) return -1; *handle = 1; }
  else if (!strcmp(name, "libSceUserService.sprx")) *handle = 2;
  else if (!strcmp(name, "libSceNetCtl.sprx")) *handle = 3;
  else return -1;
  return 0;
}
void *kernel_dynlib_dlsym(int pid, uint32_t handle, const char *name) {
  CHECK(pid == -1);
  if (handle == 1 && !strcmp(name, "sceAppInstUtilInitialize")) return (void *)api_init;
  if (handle == 2 && !strcmp(name, "sceUserServiceInitialize")) return (void *)api_user;
  if (handle == 3 && !strcmp(name, "sceNetCtlInit")) return (void *)api_simple;
  return NULL;
}
void *kernel_dynlib_resolve(int pid, uint32_t handle, const char *nid) {
  CHECK(pid == -1 && handle == 1 && !strcmp(nid, NID_INSTALL_TITLE_DIR));
  return missing_symbol ? NULL : (void *)api_install;
}
int sceKernelLoadStartModule(const char *path, size_t argc, const void *argv, uint32_t flags, void *opt, int *res) {
  (void)path; (void)argc; (void)argv; (void)flags; (void)opt; (void)res;
  return -1;
}
int sceKernelSendNotificationRequest(int device, notify_request_t *request, size_t size, int flags) {
  CHECK(!device && size == sizeof *request && !flags);
  snprintf(last_notification, sizeof last_notification, "%s", request->message);
  return 0;
}

static int exists(const char *path) { struct stat st; return !lstat(path, &st); }
static void raw_file(const char *path, const void *bytes, size_t size) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
  CHECK(fd >= 0);
  CHECK(__real_write(fd, bytes, size) == (ssize_t)size);
  CHECK(!__real_close(fd));
}
static void reset(void) {
  struct stat st;
  fault_write = fault_close = fault_icon_rename = 0;
  if (!lstat(APP_DIR, &st) && S_ISLNK(st.st_mode)) CHECK(!unlink(APP_DIR));
  else {
    if (!lstat(SYS_DIR, &st) && S_ISLNK(st.st_mode)) CHECK(!unlink(SYS_DIR));
    else {
      (void)unlink(PARAM_PATH); (void)unlink(ICON_PATH);
      (void)unlink(SYS_DIR "/.blackwire-param.new"); (void)unlink(SYS_DIR "/.blackwire-icon.new");
      (void)rmdir(SYS_DIR);
    }
    (void)unlink(APP_DIR "/eboot.bin"); (void)rmdir(APP_DIR);
  }
  CHECK(!exists(APP_DIR));
  missing_installer = missing_symbol = init_result = install_result = 0;
  registrations = initializations = writes = 0; last_notification[0] = 0;
  cases++;
}
static void owned_files(void) {
  CHECK(!mkdir(APP_DIR, 0755)); CHECK(!mkdir(SYS_DIR, 0755));
  raw_file(PARAM_PATH, param_json, param_json_size);
  raw_file(ICON_PATH, brand_logo_png, brand_logo_png_size);
}

int main(void) {
  CHECK(param_json_size < 4096 && brand_logo_png_size > 0);
  CHECK(memmem(param_json, param_json_size, "Blackwire", 9) != NULL);
  CHECK(memmem(param_json, param_json_size, "http://127.0.0.1:5918/", 22) != NULL);
  CHECK(memmem(param_json, param_json_size, "66048", 5) != NULL);

  reset(); /* Fresh ordinary Media shortcut, no executable payload. */
  install_media_web_launcher();
  CHECK(registrations == 1 && initializations == 1);
  CHECK(asset_matches(ICON_PATH, brand_logo_png, brand_logo_png_size));
  CHECK(!exists(APP_DIR "/eboot.bin"));
  CHECK(strstr(last_notification, "Media shortcut registered") != NULL);

  reset(); /* Existing files do not establish that the database entry exists. */
  owned_files();
  install_media_web_launcher();
  CHECK(registrations == 1 && initializations == 1 && writes == 0);

  reset(); /* Normal already-installed result still comes from one API call. */
  owned_files(); install_result = (int)ALREADY_THERE;
  install_media_web_launcher();
  CHECK(registrations == 1 && writes == 0);
  CHECK(strstr(last_notification, "Media shortcut registered") != NULL);

  reset(); /* Update only the icon in an exact known web-shortcut namespace. */
  owned_files(); raw_file(ICON_PATH, "old icon", 8);
  install_media_web_launcher();
  CHECK(registrations == 1 && asset_matches(ICON_PATH, brand_logo_png, brand_logo_png_size));

  reset(); /* A native/unknown occupant is never overwritten or registered. */
  owned_files(); raw_file(APP_DIR "/eboot.bin", "keep", 4);
  install_media_web_launcher();
  CHECK(!registrations && !writes && asset_matches(APP_DIR "/eboot.bin", (const uint8_t *)"keep", 4));

  reset(); /* A different title or URI is not adopted as Blackwire metadata. */
  owned_files(); unsigned char other[4096]; memcpy(other, param_json, param_json_size);
  unsigned char *id = memmem(other, param_json_size, "PSBW50001", 9); CHECK(id != NULL); id[8] = '2';
  raw_file(PARAM_PATH, other, param_json_size);
  install_media_web_launcher();
  CHECK(!registrations && !writes && asset_matches(PARAM_PATH, other, param_json_size));

  reset(); /* An old PC-service URL is not published or followed. */
  owned_files(); memcpy(other, param_json, param_json_size);
  unsigned char *port = memmem(other, param_json_size, "5918", 4); CHECK(port != NULL); port[3] = '7';
  raw_file(PARAM_PATH, other, param_json_size);
  install_media_web_launcher();
  CHECK(!registrations && !writes && asset_matches(PARAM_PATH, other, param_json_size));

  reset(); /* An alias at the exact application root cannot redirect writes. */
  raw_file(BW_MEDIA_APP_PARENT "/outside", "keep", 4);
  CHECK(!symlink(BW_MEDIA_APP_PARENT "/outside", APP_DIR));
  install_media_web_launcher();
  CHECK(!registrations && !writes && asset_matches(BW_MEDIA_APP_PARENT "/outside", (const uint8_t *)"keep", 4));

  reset(); /* A redirected sce_sys directory is also preserved. */
  CHECK(!mkdir(APP_DIR, 0755)); CHECK(!mkdir(BW_MEDIA_APP_PARENT "/outside-dir", 0755));
  raw_file(BW_MEDIA_APP_PARENT "/outside-dir/param.json", param_json, param_json_size);
  raw_file(BW_MEDIA_APP_PARENT "/outside-dir/icon0.png", "keep", 4);
  CHECK(!symlink(BW_MEDIA_APP_PARENT "/outside-dir", SYS_DIR));
  install_media_web_launcher();
  CHECK(!registrations && !writes && asset_matches(BW_MEDIA_APP_PARENT "/outside-dir/icon0.png", (const uint8_t *)"keep", 4));

  reset(); /* Unknown preexisting temporary data is preserved. */
  owned_files(); raw_file(SYS_DIR "/.blackwire-icon.new", "keep", 4);
  install_media_web_launcher();
  CHECK(!registrations && !writes && asset_matches(SYS_DIR "/.blackwire-icon.new", (const uint8_t *)"keep", 4));

  reset(); /* No asset publication if the normal installer is unavailable. */
  missing_installer = 1; install_media_web_launcher();
  CHECK(!registrations && !initializations && !exists(APP_DIR));

  reset(); missing_symbol = 1; install_media_web_launcher();
  CHECK(!registrations && !initializations && !exists(APP_DIR));

  reset(); init_result = -5; install_media_web_launcher();
  CHECK(!registrations && initializations == 1 && !exists(APP_DIR));

  reset(); /* Known-closed write failure cleans only this call's empty tree. */
  fault_write = 1; install_media_web_launcher();
  CHECK(!registrations && !exists(APP_DIR));

  reset(); /* Unknown close outcome is preserved, with no registration/retry. */
  fault_close = 1; install_media_web_launcher();
  CHECK(!registrations && exists(SYS_DIR "/.blackwire-param.new"));
  CHECK(!exists(PARAM_PATH));

  reset(); /* Interrupted second rename can be repaired from owned metadata. */
  fault_icon_rename = 1; install_media_web_launcher();
  CHECK(!registrations && asset_matches(PARAM_PATH, param_json, param_json_size) && !exists(ICON_PATH));
  CHECK(!exists(SYS_DIR "/.blackwire-icon.new"));
  install_media_web_launcher();
  CHECK(registrations == 1 && asset_matches(ICON_PATH, brand_logo_png, brand_logo_png_size));

  reset(); /* API rejection is reported, not described as registered. */
  install_result = -7; install_media_web_launcher();
  CHECK(registrations == 1 && strstr(last_notification, "registration failed") != NULL);
  CHECK(asset_matches(PARAM_PATH, param_json, param_json_size));

  unsigned completed = cases;
  reset(); cases = completed;
  printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"normalMediaApiModeled\":true,\"actualFilesystemAndMainSource\":true,\"consoleContacted\":false}\n", cases, checks);
  return 0;
}
