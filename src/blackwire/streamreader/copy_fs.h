/* Anchored traversal and idle-log parser adapted from prepare_v2.c. The
 * existing sparse helper is unchanged. All *at syscall errors are normalized. */
#ifndef BW_COPY_FS_H
#define BW_COPY_FS_H
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef BW_COPY_HOST_TEST
#include <sys/syscall.h>
#endif
#include "manifest.h"
#ifndef BW_GAMES_ROOT
#define BW_GAMES_ROOT "/data/blackwire/games"
#endif
struct destination_v2 { dev_t dev; ino_t ino; int missing; };
struct prepare_v2 {
  char run_id[33], title[10], game[241], root[2048], hash_text[17];
  uint64_t eboot_size, manifest_hash;
  uint32_t manifest_count;
};
static const char *prepare_v2_error="Preparation copy failed";
static inline int v2_fail(const char *message) { prepare_v2_error=message; return -1; }
/* Diagnostic observations only. The syscall result/error policy is unchanged. */
static struct { int attempted,result,error,carry;long raw; } cp_link_observation;
static struct { int attempted,result,error,carry;long raw; } cp_final_open_observation;
#ifndef BW_COPY_HOST_TEST
static long cp_at_raw;
static int cp_at_carry;
/* Isolated raw syscall boundary: do not carry C caller-saved values
 * (notably a sixth-argument mode/output pointer in R9) across this asm. */
static __attribute__((noinline)) long cp_at(long number,long a,long b,long c,long d,long e) {
  register long r10 __asm__("r10")=d;
  register long r8 __asm__("r8")=e;
  register long rdx __asm__("rdx")=c;
  long value=number; unsigned char carry;
  __asm__ volatile("syscall" : "+a"(value), "=@ccc"(carry), "+D"(a), "+S"(b), "+d"(rdx), "+r"(r10), "+r"(r8)
    : : "rcx","r11","r9","memory");
  cp_at_raw=value;cp_at_carry=carry;
  if (carry || value<0 || value>INT_MAX) { errno=carry && value>0 && value<=INT_MAX?(int)value:EIO;return -1; }
  return value;
}

static inline int v2_openat(int fd,const char *p,int flags,mode_t mode) { return (int)cp_at(SYS_openat,fd,(long)p,flags,mode,0); }
static inline int v2_mkdirat(int fd,const char *p,mode_t mode) { return (int)cp_at(SYS_mkdirat,fd,(long)p,mode,0,0); }
static inline int cp_fstatat(int fd,const char *p,struct stat *s,int flags) { return (int)cp_at(SYS_fstatat,fd,(long)p,(long)s,flags,0); }
static inline int cp_linkat(int a,const char *p,int b,const char *q) {
  int result=(int)cp_at(SYS_linkat,a,(long)p,b,(long)q,0),saved=errno;
  cp_link_observation.attempted=1;cp_link_observation.result=result;cp_link_observation.error=result?saved:0;
  cp_link_observation.raw=cp_at_raw;cp_link_observation.carry=cp_at_carry;errno=saved;return result;
}
static inline int cp_unlinkat(int fd,const char *p) { return (int)cp_at(SYS_unlinkat,fd,(long)p,0,0,0); }
static inline int cp_renameat(int a,const char *p,int b,const char *q) { return (int)cp_at(SYS_renameat,a,(long)p,b,(long)q,0); }
#else
#define v2_openat openat
#define v2_mkdirat mkdirat
#define cp_fstatat fstatat
static inline int cp_linkat(int a,const char *p,int b,const char *q) {
  int result=linkat(a,p,b,q,0),saved=errno;
  cp_link_observation.attempted=1;cp_link_observation.result=result;cp_link_observation.error=result?saved:0;
  cp_link_observation.raw=result;cp_link_observation.carry=-1;errno=saved;return result;
}
#define cp_unlinkat(fd,p) unlinkat(fd,p,0)
#define cp_renameat renameat
#endif
static inline int cp_final_openat(int dirfd,const char *leaf,mode_t mode) {
  int result=v2_openat(dirfd,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,mode),saved=errno;
  cp_final_open_observation.attempted=1;cp_final_open_observation.result=result;cp_final_open_observation.error=result<0?saved:0;
#ifndef BW_COPY_HOST_TEST
  cp_final_open_observation.raw=cp_at_raw;cp_final_open_observation.carry=cp_at_carry;
#else
  cp_final_open_observation.raw=result;cp_final_open_observation.carry=-1;
#endif
  errno=saved;return result;
}
static inline int v2_identity(const struct stat *a,const struct stat *b) {
  return a->st_ino==b->st_ino && a->st_dev==b->st_dev;
}

static inline int v2_copy(char *out,size_t capacity,const char *value) {
  size_t size=strlen(value);
  if (size>=capacity) return -1;
  memcpy(out,value,size+1); return 0;
}
static inline int v2_utf8(const unsigned char *text,size_t size) {
  for (size_t i=0;i<size;) {
    uint32_t value,minimum; unsigned extra; unsigned char c=text[i++];
    if (c<0x80) continue;
    if (c>=0xc2 && c<=0xdf) { value=c&31; extra=1; minimum=0x80; }
    else if (c>=0xe0 && c<=0xef) { value=c&15; extra=2; minimum=0x800; }
    else if (c>=0xf0 && c<=0xf4) { value=c&7; extra=3; minimum=0x10000; }
    else return 0;
    if (extra>size-i) return 0;
    for (unsigned j=0;j<extra;j++) {
      c=text[i++]; if ((c&0xc0)!=0x80) return 0;
      value=(value<<6)|(c&63);
    }
    if (value<minimum || value>0x10ffff || (value>=0xd800 && value<=0xdfff)) return 0;
  }
  return 1;
}
static inline int v2_game_component(const char *text) {
  size_t size=strlen(text);
  if (!size || size>240 || !strcmp(text,".") || !strcmp(text,"..")) return 0;
  for (size_t i=0;i<size;i++) {
    unsigned char c=(unsigned char)text[i];
    if (c<32 || c==127 || strchr("/\\:=|",c)) return 0;
  }
  return v2_utf8((const unsigned char *)text,size);
}
static inline int v2_hex(const char *text,size_t digits,uint64_t *out) {
  uint64_t value=0;
  if (strlen(text)!=digits) return -1;
  for (size_t i=0;i<digits;i++) {
    unsigned char c=(unsigned char)text[i]; unsigned nibble;
    if (c>='0' && c<='9') nibble=(unsigned)(c-'0');
    else if (c>='a' && c<='f') nibble=(unsigned)(c-'a'+10);
    else return -1;
    value=(value<<4)|nibble;
  }
  if (out) *out=value;
  return 0;
}
static inline int v2_read_all(int fd,void *bytes,size_t length) {
  size_t at=0;
  while (at<length) {
    ssize_t got=read(fd,(char *)bytes+at,length-at);
    if (got<0 && errno==EINTR) continue;
    if (got<=0) return -1;
    at+=(size_t)got;
  }
  return 0;
}

/* Unknown/unreadable lifecycle state is an error. An old PID exit must not
 * clear a newer same-title process. No process or game memory is inspected. */
enum v2_idle_reason {
  V2_IDLE_UNOBSERVED, V2_IDLE_STOPPED, V2_IDLE_LOG_OPEN, V2_IDLE_LOG_READ,
  V2_IDLE_LOG_INCOMPLETE, V2_IDLE_LOG_RECORD, V2_IDLE_LOG_CAPACITY,
  V2_IDLE_LOG_CLOSE, V2_IDLE_UNMATCHED_START
};
struct v2_idle_observation {
  enum v2_idle_reason reason;
  int error;
  unsigned error_valid,close_failed;
};
static inline int v2_games_stopped_observe(const char *path,struct v2_idle_observation *out) {
  struct { char title[16]; int alive,pid; } games[128];
  struct v2_idle_observation observed={V2_IDLE_UNOBSERVED,0,0,0};
  size_t count=0; char line[1024];
  if (out) *out=observed;
  errno=0; FILE *fp=fopen(path,"r"); int io_error=errno;
  if (!fp) {
    observed.reason=V2_IDLE_LOG_OPEN;observed.error=io_error;observed.error_valid=io_error!=0;
    if (out) *out=observed;
    return v2_fail("Cannot verify that games are stopped");
  }
  memset(games,0,sizeof games);
  for (;;) {
    errno=0; char *got=fgets(line,sizeof line,fp);io_error=errno;
    if (ferror(fp)) {
      observed.reason=V2_IDLE_LOG_READ;observed.error=io_error;observed.error_valid=io_error!=0;break;
    }
    if (!got) break;
    if (!strchr(line,'\n')) { observed.reason=V2_IDLE_LOG_INCOMPLETE;break; }
    char *at=NULL; int alive=0,pid=0;
    if ((at=strstr(line,"[GAME] started: PPSA"))) { at+=strlen("[GAME] started: "); alive=1; }
    else if ((at=strstr(line,"[GAME] process exited, awaiting same-app handoff: PPSA")))
      at+=strlen("[GAME] process exited, awaiting same-app handoff: ");
    else if ((at=strstr(line,"[GAME] finalizing stopped application: PPSA")))
      at+=strlen("[GAME] finalizing stopped application: ");
    else continue;
    char title[16]={0};
    if (sscanf(at,"%15s pid=%d",title,&pid)!=2 || strlen(title)!=9 || strncmp(title,"PPSA",4) || pid<=0) {
      observed.reason=V2_IDLE_LOG_RECORD;break;
    }
    for (size_t i=4;i<9;i++) if (title[i]<'0' || title[i]>'9') observed.reason=V2_IDLE_LOG_RECORD;
    if (observed.reason!=V2_IDLE_UNOBSERVED) break;
    size_t i=0; for (;i<count;i++) if (!strcmp(games[i].title,title)) break;
    if (i==count) {
      if (count==sizeof games/sizeof games[0]) { observed.reason=V2_IDLE_LOG_CAPACITY;break; }
      memcpy(games[i].title,title,sizeof title);count++;
    }
    if (alive) { games[i].pid=pid;games[i].alive=1; }
    else if (games[i].pid==pid) games[i].alive=0;
  }
  if (observed.reason==V2_IDLE_UNOBSERVED) {
    observed.reason=V2_IDLE_STOPPED;
    for (size_t i=0;i<count;i++) if (games[i].alive) { observed.reason=V2_IDLE_UNMATCHED_START;break; }
  }
  errno=0; int close_result=fclose(fp);io_error=errno;
  if (close_result) {
    observed.close_failed=1;
    if (observed.reason==V2_IDLE_STOPPED) {
      observed.reason=V2_IDLE_LOG_CLOSE;observed.error=io_error;observed.error_valid=io_error!=0;
    }
  }
  if (out) *out=observed;
  if (observed.reason==V2_IDLE_STOPPED) return 0;
  if (observed.reason==V2_IDLE_UNMATCHED_START) return v2_fail("Close running games before preparing files");
  return v2_fail("Game state log is incomplete or unsupported");
}
static inline int v2_games_stopped(const char *path) { return v2_games_stopped_observe(path,NULL); }
static inline int v2_root_current(const struct prepare_v2 *cfg,int rootfd) {
  char resolved[PATH_MAX]; struct stat path_st,fd_st;
  return realpath(cfg->root,resolved) && !strcmp(resolved,cfg->root) &&
    !lstat(cfg->root,&path_st) && S_ISDIR(path_st.st_mode) &&
    !fstat(rootfd,&fd_st) && S_ISDIR(fd_st.st_mode) && v2_identity(&path_st,&fd_st);
}
static inline int v2_open_root(const struct prepare_v2 *cfg) {
  int fd=open(cfg->root,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
  if (fd<0) return v2_fail("The staged game directory is missing or linked");
  if (!v2_root_current(cfg,fd)) { close(fd); return v2_fail("The game directory is not its exact canonical path"); }
  return fd;
}
static inline int v2_directory_current(const struct prepare_v2 *cfg,int rootfd,int fd,const char *relative) {
  char path[4096],resolved[4096]; struct stat path_st,fd_st;
  int n=snprintf(path,sizeof path,"%s%s%s",cfg->root,*relative?"/":"",relative);
  return n>=0 && (size_t)n<sizeof path && v2_root_current(cfg,rootfd) &&
    realpath(path,resolved) && !strcmp(path,resolved) && !lstat(path,&path_st) &&
    S_ISDIR(path_st.st_mode) && !fstat(fd,&fd_st) && S_ISDIR(fd_st.st_mode) && v2_identity(&path_st,&fd_st);
}
/* Return an owned parent fd, or -2 when a parent is missing during the read-only
 * pass. Existing components are always traversed using O_NOFOLLOW. */
static inline int v2_parent(const struct prepare_v2 *cfg,int rootfd,const char *relative,int create,
                     char leaf[256],char parent_relative[1024]) {
  char walk[1024]; size_t used=0;
  if (v2_copy(walk,sizeof walk,relative)) return v2_fail("A destination path exceeds its manifest limit");
  int fd=dup(rootfd); if (fd<0) return v2_fail("Cannot anchor a destination directory");
  parent_relative[0]=0;
  for (char *part=walk;;) {
    char *slash=strchr(part,'/');
    if (!slash) {
      if (v2_copy(leaf,256,part) || !v2_directory_current(cfg,rootfd,fd,parent_relative)) {
        close(fd); return v2_fail("A destination directory changed or is not canonical");
      }
      return fd;
    }
    *slash=0;
    if (!v2_directory_current(cfg,rootfd,fd,parent_relative)) {
      close(fd); return v2_fail("A destination directory changed during traversal");
    }
    int next=v2_openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
    if (next<0 && errno==ENOENT) {
      if (!create) { close(fd); return -2; }
      if (v2_mkdirat(fd,part,0755)) { close(fd); return v2_fail("A missing destination directory appeared or could not be created"); }
      next=v2_openat(fd,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
      if (next>=0 && fchmod(next,0755)) { close(next); close(fd); return v2_fail("Cannot set access on a new destination directory"); }
    }
    if (next<0) { close(fd); return v2_fail("A destination directory is linked or is not a directory"); }
    size_t length=strlen(part);
    if (used+length+(used?1:0)>=1024) { close(next); close(fd); return v2_fail("A destination directory path exceeds its limit"); }
    if (used) parent_relative[used++]='/';
    memcpy(parent_relative+used,part,length+1); used+=length;
    if (!v2_directory_current(cfg,rootfd,next,parent_relative)) {
      close(next); close(fd); return v2_fail("A destination directory has an unsafe identity");
    }
    close(fd); fd=next; part=slash+1;
  }
}
static inline int v2_check_opened(const struct prepare_v2 *cfg,int rootfd,int parentfd,const char *parent_relative,
                           const char *leaf,const struct bw_manifest_record *record,int fd,
                           const struct destination_v2 *expected,int empty,struct stat *observed) {
  struct stat path_st,fd_st;
  if (!v2_directory_current(cfg,rootfd,parentfd,parent_relative) ||
      cp_fstatat(parentfd,leaf,&path_st,AT_SYMLINK_NOFOLLOW) || !S_ISREG(path_st.st_mode) || path_st.st_nlink!=1 ||
      fstat(fd,&fd_st) || !S_ISREG(fd_st.st_mode) || fd_st.st_nlink!=1 || !v2_identity(&path_st,&fd_st) ||
      fd_st.st_size<0 || (uint64_t)fd_st.st_size!=record->size || (empty && fd_st.st_blocks!=0) ||
      (expected && (expected->missing || expected->ino!=fd_st.st_ino || expected->dev!=fd_st.st_dev)))
    return v2_fail("A destination has an unsafe identity, size, or allocation");
  if (observed) *observed=fd_st;
  return 0;
}

#endif
