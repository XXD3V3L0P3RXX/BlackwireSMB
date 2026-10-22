/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_NATIVE_REMOVAL_REQUEST_H
#define BW_NATIVE_REMOVAL_REQUEST_H
#include "native_request.h"
struct bw_native_removal_request {
    struct bw_native_send_request selected;
    char confirmed_title[10];
};
/* Four exact flat fields for Uninstall; Restore retains the existing three
 * selection fields. No operation, path, binding or credential from clients. */
int bw_native_removal_request_parse(const void *,size_t,unsigned,
    struct bw_native_removal_request *);
#endif
