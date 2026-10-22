/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BLACKWIRE_DASHBOARD_LOCAL_HTTP_H
#define BLACKWIRE_DASHBOARD_LOCAL_HTTP_H

#include <stddef.h>
#include <stdint.h>

enum bd_http_route { BD_HTTP_SMP_GAMES, BD_HTTP_BFP_JOB_STATUS };

struct bd_http_options {
    /* Zero chooses the route's normal port, or a ten-second deadline. Port
     * overrides still connect only to literal IPv4 loopback. */
    uint16_t port;
    uint32_t timeout_ms; /* 1..30000 when nonzero. */
    int (*current)(void *); /* Optional pure cancellation guard; zero=current. */
    void *opaque;
};

struct bd_http_reply {
    unsigned char *bytes; /* Owned exact body, with an extra convenience NUL. */
    size_t size;          /* Does not include that NUL; body may contain NUL. */
    unsigned status;
    const char *error;    /* Static reason on refusal, NULL on complete reply. */
    int sys_errno;       /* Captured failing operation errno, if applicable. */
};

/* Fixed read-only observations only: SMP POST /api/v1/games with
 * {"include_size":false}, <=1MiB; BFpilot GET /api/fs/job/status, <=16KiB.
 * Exactly one connection/request, no retry/redirect/alternate address. Requires
 * one valid Content-Length, no Transfer-Encoding, and connection EOF after the
 * exact body. Nonredirect HTTP error status/body is returned for caller policy.
 * Return 0 means framing/transport completion, not service success/readiness.
 * On -1 there is no partial body; status may retain a parsed HTTP status.
 * Destroy a previous reply before reusing it. No launch or mutation is exposed.
 */
int bd_local_http(enum bd_http_route, const struct bd_http_options *,
                  struct bd_http_reply *);
void bd_local_http_reply_destroy(struct bd_http_reply *);

#endif
