#ifndef BW_STARTUP_RUNTIME_CONTROL_INTERNAL_H
#define BW_STARTUP_RUNTIME_CONTROL_INTERNAL_H
#include "startup_runtime_control.h"
#include <stddef.h>
enum bw_src_operation {BW_SRC_VERSION,BW_SRC_INFO,BW_SRC_UNMOUNT,BW_SRC_SETTINGS,BW_SRC_MANUAL};
#define BW_SRC_DOCUMENT_LIMIT (512u*1024u)
#define BW_SRC_PATH_LIMIT 256u
#define BW_SRC_PATH_BYTES 1024u
struct bw_src_reply {unsigned mounted,source_available,capabilities;char **paths;size_t path_count;};
struct bw_src_connection {uint16_t port;int (*current)(void *);void *opaque;};
void bw_src_reply_clear(struct bw_src_reply *);
int bw_src_reply_parse(enum bw_src_operation,const char *,const char *,const void *,size_t,struct bw_src_reply *);
int bw_src_http(const struct bw_src_connection *,enum bw_src_operation,const char *,const char *,struct bw_src_reply *);
/* Read-only local filesystem checks, outside every DATA/SMB consumer. */
int bw_src_path_excluded(const char *scope,const char *path,int (*current)(void *),void *);
int bw_src_paths_excluded(const struct bw_src_reply *,int settings,int (*current)(void *),void *);
#endif
