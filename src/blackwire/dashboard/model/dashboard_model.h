#ifndef BLACKWIRE_DASHBOARD_MODEL_H
#define BLACKWIRE_DASHBOARD_MODEL_H

#include <stddef.h>
#include <stdint.h>

/* Pure bounded model and handoff protocol. No filesystem, process or network
 * calls. Trusted daemon adapters supply observed identities and capabilities;
 * neither a browser nor the foreground renderer supplies those observations.
 * Caller serializes mutations under its daemon lease; this core has no locks. */
#define BD_CATALOG_MAX 512u
#define BD_ID_BYTES 96u
#define BD_TITLE_BYTES 256u
#define BD_SOURCE_BYTES 1024u
#define BD_REASON_BYTES 256u
#define BD_COVER_BYTES 128u
#define BD_EXIT_WAIT_MS UINT64_C(15000)

#ifdef __cplusplus
extern "C" {
#endif

enum bd_source_kind { BD_PC_STREAM=1, BD_LOCAL_FOLDER=2, BD_INSTALLED_PACKAGE=3, BD_LOCAL_IMAGE=4 };
enum bd_item_flags { BD_READY=1u, BD_CAN_PLAY=2u, BD_CAN_SEND=4u,
    BD_CAN_UNINSTALL=8u,BD_REMOVED=16u,BD_CAN_RESTORE=32u,BD_CAN_REMOVE_STARTUP=64u };
enum bd_action { BD_PLAY=1, BD_SEND=2, BD_UNINSTALL=3, BD_RESTORE=4, BD_REMOVE_STARTUP=5 };

struct bd_item {
    char id[BD_ID_BYTES],title[BD_TITLE_BYTES],title_id[10];
    char source[BD_SOURCE_BYTES],registered_source[BD_SOURCE_BYTES];
    char cover_ref[BD_COVER_BYTES];
    char play_reason[BD_REASON_BYTES],send_reason[BD_REASON_BYTES];
    char uninstall_reason[BD_REASON_BYTES],restore_reason[BD_REASON_BYTES];
    enum bd_source_kind kind;
    unsigned flags;
    uint64_t catalog_generation,library_generation;
    uint32_t settings_revision;
    /* Adapter digest of the exact source + registration observation, not an
     * assurance that every asset was hashed or that gameplay was verified. */
    uint8_t source_fingerprint[32];
    /* Measured by strict preparation, not current logical folder allocation.
     * Unknown is distinct from zero. Presentation only; no launch authority. */
    unsigned storage_known;
    uint64_t startup_bytes,pc_bytes,console_allocated_bytes;
};

/* Caller-owned fixed storage keeps snapshot memory bounded. Adding an entry
 * never merges by title ID; IDs include its kind and source fingerprint. */
struct bd_catalog {
    struct bd_item *items;
    size_t count,capacity;
    uint64_t generation;
};

struct bd_pc_input {
    const char *title,*title_id,*source,*play_reason,*send_reason;
    uint8_t source_fingerprint[32];
    uint64_t library_generation;
    uint32_t settings_revision;
    unsigned startup_ready,can_play,can_send;
    unsigned can_uninstall,removed,can_restore,can_remove_startup;
    const char *uninstall_reason,*restore_reason;
    unsigned storage_known;
    uint64_t startup_bytes,pc_bytes,console_allocated_bytes;
};

enum bd_local_facts {
    BD_LOCAL_REGISTERED=1u,
    BD_LOCAL_SOURCE_PRESENT=2u,
    BD_LOCAL_BOOT_METADATA=4u,
    BD_LOCAL_REGISTRATION_MATCH=8u,
    /* No known transfer; stable metadata observation. Not all-asset or full
     * copy completeness certification. */
    BD_LOCAL_COPY_IDLE=16u
};
#define BD_LOCAL_REQUIRED 31u
enum bd_local_exclusions {
    BD_LOCAL_BLACKWIRE_OWNED=1u,
    BD_LOCAL_BLACKWIRE_MARKER=2u,
    BD_LOCAL_TRANSFER_ACTIVE=4u,
    BD_LOCAL_AMBIGUOUS=8u
};
struct bd_local_input {
    const char *title,*title_id,*source,*registered_source;
    uint8_t source_fingerprint[32];
    enum bd_source_kind kind;
    unsigned facts,exclusions;
};

int bd_catalog_init(struct bd_catalog *,struct bd_item *,size_t,uint64_t);
int bd_catalog_add_pc(struct bd_catalog *,const struct bd_pc_input *);
int bd_catalog_add_local(struct bd_catalog *,const struct bd_local_input *);
const struct bd_item *bd_catalog_find(const struct bd_catalog *,const char *);
/* PC actions are preparation-gated: ready permits Play, unready permits Send.
 * Current capabilities still apply; catalog and action admission share them. */
int bd_item_can_play(const struct bd_item *);
int bd_item_can_send(const struct bd_item *);
int bd_item_can_uninstall(const struct bd_item *);
int bd_item_can_restore(const struct bd_item *);
int bd_item_can_remove_startup(const struct bd_item *);

struct bd_frontend_identity {
    int32_t pid;
    uint32_t app_id;
    uint64_t birth_sec;
    uint32_t birth_usec;
    char title_id[10];
    /* Expected installed frontend package snapshot; not a claim that a file
     * hash attests the already mapped process image. */
    uint8_t package_fingerprint[32];
};
enum bd_intent_phase {
    BD_INTENT_EMPTY=0,BD_INTENT_WAIT_EXIT,BD_INTENT_DISPATCHING,
    BD_INTENT_ACCEPTED,BD_INTENT_REFUSED,BD_INTENT_UNKNOWN,BD_INTENT_CANCELLED
};
struct bd_intent_request {
    const char *item_id;
    uint64_t catalog_generation;
    enum bd_source_kind kind;
    enum bd_action action;
};
struct bd_intent {
    enum bd_intent_phase phase;
    enum bd_action action;
    char nonce[33];
    struct bd_item selected;
    struct bd_frontend_identity frontend;
    uint64_t started_ms,last_ms,deadline_ms;
    unsigned dispatch_calls;
    char message[BD_REASON_BYTES];
};

/* All callbacks belong to the resident daemon. frontend_state returns 1 only
 * for the exact admitted native frontend, 0 for verified absence, -1 for an
 * uncertain or replaced generation. idle uses the existing full idle checks
 * without exempting the dashboard. current reopens/rechecks exact source and
 * registration plus revisions. Dispatch reuses native BW start/send or one
 * normal Sony local-title launch; 1=accepted, 0=definite refusal, -1=unknown.
 * An unknown outcome is consumed and is NEVER automatically dispatched again. */
struct bd_intent_ops {
    void *opaque;
    int (*now_ms)(void *,uint64_t *);
    int (*frontend_state)(void *,const struct bd_frontend_identity *);
    int (*idle)(void *);
    int (*current)(void *,const struct bd_item *);
    int (*dispatch)(void *,const struct bd_item *,enum bd_action,const char *);
};
int bd_intent_begin(struct bd_intent *,const struct bd_catalog *,const struct bd_intent_request *,
                    const struct bd_frontend_identity *,const char *,const struct bd_intent_ops *);
int bd_intent_tick(struct bd_intent *,const struct bd_intent_ops *);
int bd_intent_cancel(struct bd_intent *);

#ifdef __cplusplus
}
#endif
#endif
