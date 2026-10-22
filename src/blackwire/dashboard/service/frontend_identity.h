/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BLACKWIRE_DASHBOARD_FRONTEND_IDENTITY_H
#define BLACKWIRE_DASHBOARD_FRONTEND_IDENTITY_H

#include "../model/dashboard_model.h"
#include "game_identity.h"

#define BD_FRONTEND_TITLE "PPSA99171"
#define BD_FRONTEND_SOURCE "/data/homebrew/PPSA99171"
#define BD_FRONTEND_REGISTERED "/user/app/PPSA99171"
#define BD_FRONTEND_BOOT_LIMIT UINT64_C(134217728)
#define BD_FRONTEND_PARAM_LIMIT UINT64_C(65536)
#define BD_FRONTEND_LINK_LIMIT UINT64_C(1023)

struct bd_frontend_file {
    uint64_t bytes;
    uint8_t sha256[32];
};
/* Supplied by the trusted daemon from its actual installed build, never hello
 * JSON. No default hashes exist. Source and registered param.json must be byte
 * identical. mount.lnk must name the fixed source, with at most one LF/CRLF.
 * Hashes describe this expected installed source snapshot; they do not attest
 * the already mapped process image or all of its runtime libraries/assets. */
struct bd_frontend_package {
    struct bd_frontend_file eboot,param,mount_link;
};

/* One owner per foreground generation. Serialize access using the daemon's
 * existing lease. Fields are retained trusted C state, not deserialized input.
 * No function discovers PIDs, closes an app or changes registration/files. */
struct bd_frontend_owner {
    struct bd_frontend_package expected;
    struct rc_game_identity original;
    struct bd_frontend_identity identity;
    char source[BD_SOURCE_BYTES],registered[BD_SOURCE_BYTES];
    unsigned initialized,admitted;
};

/* init returns 0 on a well-formed expected tuple, -1 otherwise (and clears the
 * owner). It does not claim that files currently exist. Caller retires a
 * completed owner before explicitly initializing for a later app generation.
 * Never reinitialize a retained owner while its launch intent is in progress. */
int bd_frontend_init(struct bd_frontend_owner *,const struct bd_frontend_package *);

/* Candidate PID/appId are untrusted selection hints only. Admission uses the
 * unchanged rc_game_identity role/birth/Sony checks and fixed title. A successful
 * owner is captured exactly once with the original <=60s admission time. Hello
 * retries with that same PID/appId call current; another candidate is refused.
 * QUALIFIED=1, verified exact-PID absence=0, any uncertainty/refusal=-1.
 * Bounded source hashing is bracketed by unchanged native identity captures.
 * Calls are read-only but are not atomic process/mount locks or hard timeouts. */
int bd_frontend_admit(struct bd_frontend_owner *,int32_t,uint32_t);
int bd_frontend_current(const struct bd_frontend_owner *);

/* Returns 0 and a COPY of the originally admitted identity, -1 if none. This
 * cached identity is suitable as bd_intent's expected owner, not a fresh
 * liveness assertion. current must qualify it before accepting an intent and
 * supply exact absence afterward. Failed calls never change original birth or
 * admitted_wall_us; get still returns that original snapshot after exit. */
int bd_frontend_get(const struct bd_frontend_owner *,struct bd_frontend_identity *);

/* Same read-only semantics, with the existing identity seam for host fault
 * testing. Trusted callbacks must obey game_identity.h; no replacement parser,
 * process-role shortcut or candidate-provided identity is accepted here. */
int bd_frontend_admit_with_ops(struct bd_frontend_owner *,int32_t,uint32_t,
                               const struct rc_game_identity_ops *);
int bd_frontend_current_with_ops(const struct bd_frontend_owner *,
                                 const struct rc_game_identity_ops *);

/* Preparation-only retained source witness for the already admitted frontend.
 * Acquisition checks the same real package bytes and process identity, keeping
 * four files/four directory descriptors until checked close. Frequent current
 * checks requalify exact public PID/app/title/birth and every held/named fine
 * identity without hashing the executable per data chunk. Control boundaries
 * rehash the retained files. Any uncertainty poisons this one Send scope.
 * Serialize calls and retain ownership until close; no launch/registration
 * idle permission is created by this object. close consumes it even on error. */
struct bd_frontend_preparation;
int bd_frontend_preparation_open(const struct bd_frontend_owner *,struct bd_frontend_preparation **);
int bd_frontend_preparation_open_with_ops(const struct bd_frontend_owner *,const struct rc_game_identity_ops *,struct bd_frontend_preparation **);
int bd_frontend_preparation_current(struct bd_frontend_preparation *);
int bd_frontend_preparation_control(struct bd_frontend_preparation *);
int bd_frontend_preparation_close(struct bd_frontend_preparation *);

#ifdef BD_FRONTEND_IDENTITY_HOST_TEST
/* Host-only real-filesystem fixture paths. This symbol is absent in native
 * builds; production always uses the fixed source and registration roots. */
int bd_frontend_init_at_for_test(struct bd_frontend_owner *,
                                 const struct bd_frontend_package *,const char *,const char *);
#endif
#endif
