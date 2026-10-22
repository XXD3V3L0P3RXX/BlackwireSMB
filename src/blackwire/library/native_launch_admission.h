#ifndef BW_NATIVE_LAUNCH_ADMISSION_H
#define BW_NATIVE_LAUNCH_ADMISSION_H
#include "native_send.h"
#include "reader_selection.h"

/* Internal, single joined Play owner. This is deliberately not a persisted
 * capability or a reader file-format change. A Ready FOLDER receipt seals the
 * strict Send result; normal Play checks its bounded metadata and required
 * boot/module bytes once, then retains their named/held identities. It does
 * not walk the current PC tree or inspect every STREAM placeholder. */
struct bw_native_launch_admission;
int bw_native_launch_admission_open(struct bw_native_launch_admission **,
    const char *,const struct bw_provider_binding *,const struct bw_native_prepared *,
    const char *run,int (*current)(void *),void *,const char **error);
int bw_native_launch_admission_selection(struct bw_native_launch_admission *,struct bw_reader_selection *);
/* No body reads. Checks the original Play/settings guard, exact canonical
 * root/control names and retained required files including fine timestamps.
 * Any uncertainty poisons this one Play; a new Send/repair remains separate. */
int bw_native_launch_admission_current(struct bw_native_launch_admission *,
    const char *,const struct bw_provider_binding *,const struct bw_reader_selection *,
    struct bw_native_prepared *);
/* Bounded source/param/boot/module checks on the already connected share.
 * Durable generation is the Send seal, not a freshly reconstructed tree seal.
 * Unchanged early-reader per-request size/mtime + held/named fences remain. */
int bw_native_launch_admission_source(struct bw_native_launch_admission *,
    const struct bw_share_config *,struct bw_source_smb *,const struct bw_reader_selection *,
    uint8_t map_sha256[32],uint8_t generation_sha256[32],const char **error);
/* Read-only pinned full map digest from this held current receipt. This is
 * local identity evidence only; source() remains mandatory before handoff. */
int bw_native_launch_admission_map_sha256(struct bw_native_launch_admission *,uint8_t[32]);
/* Consumes the owner, including on a close error. Call only after all borrowed
 * registration/preflight/reader callbacks have returned. No cross-Play cache. */
int bw_native_launch_admission_close(struct bw_native_launch_admission **);
#endif
