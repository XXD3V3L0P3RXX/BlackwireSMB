#ifndef BW_SOURCE_DIRECTORY_H
#define BW_SOURCE_DIRECTORY_H
#include <stddef.h>
#include <stdint.h>
#define BW_SOURCE_DIRECTORY_PAGE 65536u
#define BW_SOURCE_NAME_BYTES 1024u
struct bw_source_info {
    uint64_t size,inode,mtime_ticks,ctime_ticks;
    uint32_t attributes;
};
typedef int (*bw_source_visit_fn)(void *,const char *,const struct bw_source_info *);
/* Strict bounded FILE_ID_FULL_DIR_INFORMATION decoding. The callback's name
 * and metadata are borrowed for the call only. No filesystem/network calls. */
int bw_source_directory_page(const void *,size_t,bw_source_visit_fn,void *);
uint64_t bw_source_unix_mtime(uint64_t ticks);
#endif
