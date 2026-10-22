/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef BW_MEDIA_PUBLIC_TRIAL_H
#define BW_MEDIA_PUBLIC_TRIAL_H
#include <stddef.h>
#include <stdint.h>

#define BW_MEDIA_PUBLIC_TITLE "PSBW50001"
#define BW_MEDIA_PUBLIC_APP_LIMIT 256u
#define BW_MEDIA_PUBLIC_DB_LIMIT 4096u
struct bw_registration_titles {size_t count;char title[BW_MEDIA_PUBLIC_DB_LIMIT][10];};
/* Fresh bounded read using the existing no-write/no-shared-memory VFS.
 * Sorted unique nine-character title IDs only. Unknown, duplicate, over-limit,
 * schema/recovery/WAL-mapping or close failures refuse the whole observation. */
int bw_registration_db_titles(const char *,struct bw_registration_titles *);

enum bw_media_public_mode {BW_MEDIA_PUBLIC_NONE,BW_MEDIA_PUBLIC_TITLE_DIR,BW_MEDIA_PUBLIC_BATCH};
enum bw_media_public_phase {BW_MEDIA_PUBLIC_IDLE,BW_MEDIA_PUBLIC_CHECKING,
    BW_MEDIA_PUBLIC_OBSERVING,BW_MEDIA_PUBLIC_VERIFIED,BW_MEDIA_PUBLIC_REFUSED,BW_MEDIA_PUBLIC_UNCONFIRMED};
struct bw_media_public_record {
    enum bw_media_public_phase phase;enum bw_media_public_mode mode;
    unsigned attempted,returned,registered;
    int platform_status,initialize_status,terminate_status;
    const char *reason; /* fixed source strings only; never a borrowed path */
};
struct bw_media_public_sdk {
    int (*initialize)(void);
    int (*terminate)(void);
    int (*title_dir)(const char *,const char *,void *);
    int (*install_all)(void *);
};
struct bw_media_public_request {
    const char *apps,*database; /* trusted resident constants; host fixture paths */
    void *opaque;
    int (*current)(void *); /* same reservation/settings and independent full idle */
    int (*title_idle)(void *,const char[10]); /* no known BW pending registration */
    int (*assets)(void *,int stage); /* existing exact owned asset transaction */
    int (*sdk_open)(void *,struct bw_media_public_sdk *); /* explicit action only */
    double (*now)(void *);
    int (*wait)(void *,unsigned milliseconds);
    void (*report)(void *,const struct bw_media_public_record *); /* optional cached status only */
};
/* Synchronous joined worker body. No backend/HTTP/global ownership is changed.
 * Calls one normal API only: TitleDir when exported, otherwise AppInstallAll.
 * A per-title error never falls through to the batch API. An attempted record
 * can only be observed on later explicit calls; it can never submit again.
 * After the API returns, fresh own-row observation is bounded to <=30 seconds.
 * The public platform RPC itself has no cancellation/timeout contract here. */
int bw_media_public_run(const struct bw_media_public_request *,struct bw_media_public_record *);
const char *bw_media_public_phase_name(enum bw_media_public_phase);
const char *bw_media_public_mode_name(enum bw_media_public_mode);
#endif
