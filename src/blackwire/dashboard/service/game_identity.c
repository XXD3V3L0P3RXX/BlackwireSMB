/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "game_identity.h"
#include <limits.h>
#include <string.h>

#define RC_PROCESS_CAPACITY 16384u
#define RC_TITLE_CAPACITY 128u

/* Exact natural-layout declaration in ps5-payload-manager/src/process_mgr.c.
 * Unknown fields are neither interpreted nor used as identity evidence. */
struct rc_sony_app_info {
    uint32_t app_id;
    uint64_t unknown1;
    char title_id[14];
    char unknown2[0x3c];
};
_Static_assert(offsetof(struct rc_sony_app_info, title_id) == 16, "App-info title field");
_Static_assert(sizeof(struct rc_sony_app_info) == 96, "Source app-info buffer layout");

static int title_valid(const char *title, size_t capacity) {
    if (capacity < 10 || !memchr(title, 0, capacity) ||
        memcmp(title, "PPSA", 4) || title[9] != 0) return 0;
    for (unsigned i = 4; i < 9; ++i)
        if (title[i] < '0' || title[i] > '9') return 0;
    return 1;
}
static int selection_valid(const struct rc_game_selection *selection, int32_t self) {
    return self > 1 && selection->pid > 1 && selection->pid != self &&
        selection->app_id > 0 && selection->app_id <= INT32_MAX &&
        title_valid(selection->title, sizeof selection->title);
}
static int ops_valid(const struct rc_game_identity_ops *ops, int initial) {
    return ops && ops->firmware && ops->self_pid && ops->process && ops->app_info &&
        ops->running_app && ops->app_title && (!initial || ops->wall_us);
}
static int process_capture(const struct rc_game_identity_ops *ops, int32_t pid,
                           unsigned char fill, struct bw_stop_identity *identity) {
    unsigned char raw[RC_PROCESS_CAPACITY];
    size_t used = 0;
    memset(raw, fill, sizeof raw);
    int result = ops->process(ops->opaque, pid, raw, sizeof raw, &used);
    if (result == RC_GAME_ABSENT) return used == 0 ? RC_GAME_ABSENT : RC_GAME_UNKNOWN;
    if (result != RC_GAME_QUALIFIED || used > sizeof raw ||
        bw_stop_identity_parse(raw, used, BW_STOP_PROFILE_FW, pid, identity)) return RC_GAME_UNKNOWN;
    return RC_GAME_QUALIFIED;
}
static int sony_capture(const struct rc_game_identity_ops *ops,
                        const struct rc_game_selection *selection, unsigned char fill) {
    uint32_t app_id = 0, running = 0;
    char kernel_title[14], lnc_title[RC_TITLE_CAPACITY];
    memset(kernel_title, fill, sizeof kernel_title);
    memset(lnc_title, fill, sizeof lnc_title);
    if (ops->app_info(ops->opaque, selection->pid, &app_id, kernel_title) ||
        app_id != selection->app_id || !title_valid(kernel_title, sizeof kernel_title) ||
        memcmp(kernel_title, selection->title, sizeof selection->title) ||
        ops->running_app(ops->opaque, &running) || running != selection->app_id ||
        ops->app_title(ops->opaque, running, lnc_title, sizeof lnc_title) ||
        !title_valid(lnc_title, sizeof lnc_title) ||
        memcmp(lnc_title, selection->title, sizeof selection->title)) return -1;
    return 0;
}
static int capture(const struct rc_game_identity_ops *ops,
                   const struct rc_game_selection *selection,
                   const struct rc_game_identity *expected,
                   struct rc_game_identity *out) {
    int initial = expected == NULL;
    if (!ops_valid(ops, initial)) return RC_GAME_UNKNOWN;
    uint32_t firmware = ops->firmware(ops->opaque);
    int32_t self = ops->self_pid(ops->opaque);
    if (firmware != BW_STOP_PROFILE_FW || !selection_valid(selection, self)) return RC_GAME_UNKNOWN;
    struct bw_stop_identity first, last;
    int first_result = process_capture(ops, selection->pid, 0xa5, &first);
    if (first_result == RC_GAME_UNKNOWN) return RC_GAME_UNKNOWN;
    if (first_result == RC_GAME_ABSENT) {
        int second_result = process_capture(ops, selection->pid, 0x5a, &last);
        return second_result == RC_GAME_ABSENT && ops->firmware(ops->opaque) == firmware &&
            ops->self_pid(ops->opaque) == self ? RC_GAME_ABSENT : RC_GAME_UNKNOWN;
    }
    if (bw_stop_identity_match(&first, &first) != 1 ||
        (expected && (expected->firmware != firmware ||
                      bw_stop_identity_match(&expected->process, &first) != 1))) return RC_GAME_UNKNOWN;
    int64_t wall_before = 0;
    if (initial) {
        wall_before = ops->wall_us(ops->opaque);
        if (bw_stop_identity_admit(&first, wall_before)) return RC_GAME_UNKNOWN;
    }
    if (sony_capture(ops, selection, 0xa5) || sony_capture(ops, selection, 0x5a) ||
        process_capture(ops, selection->pid, 0x5a, &last) != RC_GAME_QUALIFIED ||
        bw_stop_identity_match(&first, &last) != 1 ||
        ops->firmware(ops->opaque) != firmware || ops->self_pid(ops->opaque) != self) return RC_GAME_UNKNOWN;
    int64_t admitted_wall = expected ? expected->admitted_wall_us : 0;
    if (initial) {
        admitted_wall = ops->wall_us(ops->opaque);
        if (admitted_wall < wall_before || bw_stop_identity_admit(&last, admitted_wall)) return RC_GAME_UNKNOWN;
    }
    struct rc_game_identity qualified = {0};
    if (expected) qualified = *expected;
    else {
        qualified.process = last;
        qualified.firmware = firmware;
        qualified.app_id = selection->app_id;
        memcpy(qualified.title, selection->title, sizeof qualified.title);
        qualified.admitted_wall_us = admitted_wall;
    }
    *out = qualified;
    return RC_GAME_QUALIFIED;
}
int rc_game_identity_admit_with_ops(const struct rc_game_identity_ops *ops,
                                  const struct rc_game_selection *selection,
                                  struct rc_game_identity *out) {
    if (!out) return RC_GAME_UNKNOWN;
    struct rc_game_selection selected = {0};
    struct rc_game_identity_ops held = {0};
    if (selection) selected = *selection;
    if (ops) held = *ops;
    memset(out, 0, sizeof *out);
    return selection && ops ? capture(&held, &selected, NULL, out) : RC_GAME_UNKNOWN;
}
int rc_game_identity_current_with_ops(const struct rc_game_identity_ops *ops,
                                    const struct rc_game_identity *expected,
                                    struct rc_game_identity *out) {
    if (!out) return RC_GAME_UNKNOWN;
    struct rc_game_identity prior = {0};
    struct rc_game_identity_ops held = {0};
    if (expected) prior = *expected;
    if (ops) held = *ops;
    memset(out, 0, sizeof *out);
    if (!expected || !ops || prior.firmware != BW_STOP_PROFILE_FW ||
        bw_stop_identity_admit(&prior.process, prior.admitted_wall_us)) return RC_GAME_UNKNOWN;
    struct rc_game_selection selected = {prior.process.pid, prior.app_id, {0}};
    memcpy(selected.title, prior.title, sizeof selected.title);
    return capture(&held, &selected, &prior, out);
}

#ifndef RC_GAME_IDENTITY_HOST_TEST
#include "native_process_result.h"
#include <ps5/kernel.h>
#include <sys/syscall.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

int sceKernelGetAppInfo(pid_t, struct rc_sony_app_info *);
int sceLncUtilGetAppIdOfRunningBigApp(void);
int sceLncUtilGetAppTitleId(uint32_t, char *);

static uint32_t native_firmware(void *opaque) { (void)opaque; return kernel_get_fw_version(); }
static int32_t native_self(void *opaque) { (void)opaque; return (int32_t)getpid(); }
static int native_process(void *opaque, int32_t pid, void *raw, size_t capacity, size_t *used) {
    (void)opaque;
    if (!raw || !used || capacity != RC_PROCESS_CAPACITY || pid <= 1) return RC_GAME_UNKNOWN;
    size_t size = capacity;
    int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, pid};
    _Static_assert(SYS___sysctl == 202, "Existing public sysctl ABI");
    register uint64_t r10 __asm__("r10") = (uintptr_t)&size;
    register uint64_t r8 __asm__("r8") = 0, r9 __asm__("r9") = 0;
    uint64_t value;
    unsigned char carry;
    __asm__ __volatile__("syscall" : "=a"(value), "=@ccc"(carry), "+r"(r10), "+r"(r8), "+r"(r9)
        : "a"((uint64_t)SYS___sysctl), "D"((uintptr_t)mib), "S"((uint64_t)4), "d"((uintptr_t)raw)
        : "rcx", "r11", "memory");
    int result = bw_native_process_result(value, carry, size, capacity);
    *used = result == RC_GAME_QUALIFIED ? size : 0;
    return result;
}
static int native_app_info(void *opaque, int32_t pid, uint32_t *app_id, char title[14]) {
    (void)opaque;
    struct rc_sony_app_info info;
    memset(&info, 0xa5, sizeof info);
    if (sceKernelGetAppInfo((pid_t)pid, &info)) return -1;
    *app_id = info.app_id;
    memcpy(title, info.title_id, sizeof info.title_id);
    return 0;
}
static int native_running(void *opaque, uint32_t *app_id) {
    (void)opaque;
    int result = sceLncUtilGetAppIdOfRunningBigApp();
    if (result <= 0) return -1;
    *app_id = (uint32_t)result;
    return 0;
}
static int native_title(void *opaque, uint32_t app_id, char *title, size_t capacity) {
    (void)opaque;
    if (capacity != RC_TITLE_CAPACITY) return -1;
    return sceLncUtilGetAppTitleId(app_id, title);
}
static int64_t native_wall(void *opaque) {
    (void)opaque;
    struct timespec time;
    if (clock_gettime(CLOCK_REALTIME, &time) || time.tv_sec <= 0 ||
        time.tv_sec > INT64_MAX / 1000000 || time.tv_nsec < 0 || time.tv_nsec >= 1000000000L) return 0;
    int64_t seconds = (int64_t)time.tv_sec * 1000000;
    if (time.tv_nsec / 1000 > INT64_MAX - seconds) return 0;
    return seconds + time.tv_nsec / 1000;
}
static const struct rc_game_identity_ops native_ops = {
    NULL, native_firmware, native_self, native_process, native_app_info,
    native_running, native_title, native_wall
};
#endif

int rc_game_identity_admit(const struct rc_game_selection *selection, struct rc_game_identity *out) {
#ifndef RC_GAME_IDENTITY_HOST_TEST
    return rc_game_identity_admit_with_ops(&native_ops, selection, out);
#else
    (void)selection;
    if (out) memset(out, 0, sizeof *out);
    return RC_GAME_UNKNOWN;
#endif
}
int rc_game_identity_current(const struct rc_game_identity *expected, struct rc_game_identity *out) {
#ifndef RC_GAME_IDENTITY_HOST_TEST
    return rc_game_identity_current_with_ops(&native_ops, expected, out);
#else
    (void)expected;
    if (out) memset(out, 0, sizeof *out);
    return RC_GAME_UNKNOWN;
#endif
}
