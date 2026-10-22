#ifndef BW_REMOVED_TEST_IO_H
#define BW_REMOVED_TEST_IO_H
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
int rm_test_open(const char *,int,...);
int rm_test_openat(int,const char *,int,...);
int rm_test_close(int);
ssize_t rm_test_write(int,const void *,size_t);
int rm_test_unlinkat(int,const char *,int);
#define open rm_test_open
#define openat rm_test_openat
#define close rm_test_close
#define write rm_test_write
#define unlinkat rm_test_unlinkat
#endif
