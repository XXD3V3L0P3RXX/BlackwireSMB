#ifndef BW_NATIVE_DERIVED_SEND_H
#define BW_NATIVE_DERIVED_SEND_H
#include "native_send.h"
#include "derived_terminal.h"

struct bw_native_derived_prepared {
    struct bw_native_prepared base; /* game is always the ORIGINAL source game */
    uint32_t prepared_policy;
    char prepared_component[75];
    struct bw_derived_prepared_identity prepared;
    uint8_t provenance_sha256[32],terminal_sha256[32];
    size_t provenance_size,terminal_size;
    const char *error; /* static stage message; no paths/credentials/body bytes */
};
struct bw_native_derived_send_files {
    const char *data_root,*game_log;
    struct bw_derived_material *material;
    struct bw_source_provider *original;
    struct bw_packed_project *project;
    struct bw_derived_local *local;
    int (*current)(void *); void *opaque;
};
/* Zero initialize. Owns exact raw provenance bytes and an independent decode.
 * Historical structural inspection never grants source freshness or Play. */
struct bw_native_derived_snapshot {
    struct bw_native_derived_prepared prepared;
    struct bw_derived_terminal_record terminal;
    struct bw_derived_provenance provenance;
    uint8_t *provenance_bytes;size_t provenance_size;
};
struct bw_native_derived_verify {
    const char *data_root,*game_log;
    const uint8_t *expected_terminal_sha256;
    struct bw_source_provider *original;
    struct bw_packed_project *project;
    struct bw_derived_local *local;
    int (*current)(void *);void *opaque;
};
/* One joined accepted PREPARE worker borrows actual coherent material/LOCAL/
 * provider/parser/source/job/grant lifetimes exclusively. No source writes or
 * provider mutation. Outer guard may perform controller/filesystem checks, but
 * is called only outside LOCAL consumers. Borrowed source observers remain
 * pure/coalescing and must not reenter SDK/DATA or settings APIs. Every LOCAL
 * is read once, even if an existing destination already matches. Body chunks
 * write only an owned held temp FD and update hash/count; they are provisional.
 * Durable BWNDR001 is published last. Failure after publication is UNCONFIRMED.
 * Old native-prepared, v4/v5 formats, global controls and registrations stay
 * untouched. Success proves startup copy only, never native visibility/Play. */
int bw_native_derived_send_files(const struct bw_native_derived_send_files *,
                               struct bw_native_derived_prepared *);
int bw_native_derived_send_inspect(const char *,const struct bw_provider_binding *,
                                 struct bw_native_derived_snapshot *);
/* Structural inspection plus guarded destination LOCAL hashes only. Suitable
 * for an outer registration fence; it makes NO fresh PC/source claim. */
int bw_native_derived_send_local_verify(const char *,const struct bw_provider_binding *,
    const uint8_t[32],int (*)(void *),void *,struct bw_native_derived_snapshot *);
void bw_native_derived_snapshot_destroy(struct bw_native_derived_snapshot *);
/* Fresh provider/parser/LOCAL reconstructed under ACTUAL Play ownership, with
 * an explicit live destination grant. No material/prepare job is fabricated.
 * Rechecks exact historical commitments and all LOCAL destination hashes after
 * actual provider/parser freshness validation, using the preparation run for
 * BWGEN001. Expected terminal pin is mandatory. No source body is reconsumed. */
int bw_native_derived_send_verify(const struct bw_native_derived_verify *,
                                struct bw_native_derived_snapshot *);
#endif
