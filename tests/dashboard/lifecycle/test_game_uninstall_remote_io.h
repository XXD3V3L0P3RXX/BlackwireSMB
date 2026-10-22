/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_GAME_UNINSTALL_REMOTE_TEST_IO_H
#define BW_GAME_UNINSTALL_REMOTE_TEST_IO_H
#include <time.h>
#include <unistd.h>
int gur_test_clock_gettime(clockid_t,struct timespec *);
int gur_test_nanosleep(const struct timespec *,struct timespec *);
int gur_test_close(int);
#define clock_gettime gur_test_clock_gettime
#define nanosleep gur_test_nanosleep
#define close gur_test_close
#endif
