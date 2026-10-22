#ifndef BW_GAME_UNINSTALL_TEST_IO_H
#define BW_GAME_UNINSTALL_TEST_IO_H
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
int gu_test_open(const char *,int,...);
int gu_test_openat(int,const char *,int,...);
int gu_test_close(int);
ssize_t gu_test_write(int,const void *,size_t);
int gu_test_unlinkat(int,const char *,int);
int gu_test_dup(int);
int gu_test_closedir(DIR *);
#define open gu_test_open
#define openat gu_test_openat
#define close gu_test_close
#define write gu_test_write
#define unlinkat gu_test_unlinkat
#define dup gu_test_dup
#define closedir gu_test_closedir
#endif
