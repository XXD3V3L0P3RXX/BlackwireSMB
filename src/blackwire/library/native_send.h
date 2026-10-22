#ifndef BW_NATIVE_SEND_H
#define BW_NATIVE_SEND_H
#include "source_binding.h"
#include "source_provider.h"
enum bw_prepared_state {BW_PREPARED_ABSENT,BW_PREPARED_VALID,BW_PREPARED_INVALID,BW_PREPARED_UNKNOWN};
enum bw_native_send_result {BW_SEND_OK=0,BW_SEND_FAILED=-1,BW_SEND_UNCONFIRMED=-2,BW_SEND_BUSY=1};
enum bw_native_retirement_state {BW_RETIRE_NONE,BW_RETIRE_COMPLETE,BW_RETIRE_REFUSED,BW_RETIRE_RECOVERY_REQUIRED};
struct bw_native_prepared {
    enum bw_prepared_state state;
    char game[75],run[33];
    uint64_t local_bytes,remote_bytes,allocated_bytes;
    uint32_t local_count,stream_count,entries;
    unsigned bytes_verified;
    uint8_t record_sha256[32];
    /* Explicit prepared-target refinement; game remains ORIGINAL identity.
     * Zero is the unchanged legacy target. Policy1 is a distinct derived root
     * backed by BWNDR001/BWDPR001, never a replacement provider binding. */
    uint32_t prepared_policy;
    char prepared_component[75];
    uint8_t provenance_sha256[32];
    uint64_t provenance_size;
    unsigned startup_upgrade_required;
    /* Diagnostic only; no persisted or live authority. */
    enum bw_native_retirement_state retirement;
    const char *retirement_error;
};
struct bw_native_send_files {
    const char *data_root,*game_log;
    struct bw_source_provider *provider;
    int (*current)(void *);
    void *opaque;
    /* Required ONLY for an owned classification retirement. Control-phase
     * zero means the exact title/root has independently been observed unmapped
     * and remains under the caller's lifecycle exclusion. The caller must also
     * exclude canonical explicit scanner roots/manual sources within the owned
     * data_root/.native-startup-rollback scope; dot spelling alone is not that
     * authority. Path-equal mount.lnk
     * or registration MATCH is not sufficient. Never called in SMB consumers;
     * this operation performs no mount/registration/API action itself. */
    int (*upgrade_unmapped)(void *,const char *root,const char *title);
    void *upgrade_opaque;
    /* Trusted per-invocation preparation authority. NULL preserves every
     * original full-idle check. A positive scope must independently qualify
     * the same admitted frontend and exclude all other games/work. No request
     * JSON, launch/preflight or registration path supplies these callbacks. */
    int (*idle)(void *);
    int (*publication_current)(void *);
};
/* One joined worker owns the supplied connection/bundle. Fixed per-target
 * state under data_root/native-prepared and games/<binding.game>. Existing
 * unowned directories are never adopted. No global stream/registration file,
 * source write, registration, process hook or game launch is performed. */
int bw_native_send_files(const struct bw_native_send_files *,struct bw_native_prepared *);
/* Read-only durable metadata/index inspection. This does not rehash every
 * startup body and never grants Play. bytes_verified is false after reload. */
int bw_native_send_inspect(const char *,const struct bw_provider_binding *,struct bw_native_prepared *);
/* Read-only launch preflight: rehash every LOCAL body against the checked
 * durable digest table, then recheck namespace/record identity. The caller's
 * guard is required and may cancel between files. Success proves local startup
 * bytes only; it does not verify the current PC source, register or launch. */
int bw_native_send_verify(const char *,const struct bw_provider_binding *,int (*)(void *),void *,struct bw_native_prepared *);
/* Launch check against the exact selected completion and a freshly produced,
 * sealed current-PC bundle. Requires all namespace/digest/map bindings as well
 * as fresh local startup hashes. No registration, writes or game operations. */
int bw_native_send_preflight(const char *,const struct bw_provider_binding *,const uint8_t[32],
    struct bw_source_provider *,int (*)(void *),void *,struct bw_native_prepared *);
#endif
