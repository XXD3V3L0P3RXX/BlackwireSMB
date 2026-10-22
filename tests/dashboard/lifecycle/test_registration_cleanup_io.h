/* SPDX-License-Identifier: GPL-3.0-or-later
 * Host fault seam. Only the actual cleanup TU is compiled with this header. */
#ifndef BW_NRC_TEST_IO_H
#define BW_NRC_TEST_IO_H
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
int nrc_test_open(const char *,int,...);
int nrc_test_openat(int,const char *,int,...);
int nrc_test_close(int);
int nrc_test_fstatat(int,const char *,struct stat *,int);
ssize_t nrc_test_write(int,const void *,size_t);
ssize_t nrc_test_pread(int,void *,size_t,off_t);
int nrc_test_fsync(int);
#define open nrc_test_open
#define openat nrc_test_openat
#define close nrc_test_close
#define fstatat nrc_test_fstatat
#define write nrc_test_write
#define pread nrc_test_pread
#define fsync nrc_test_fsync
#endif
