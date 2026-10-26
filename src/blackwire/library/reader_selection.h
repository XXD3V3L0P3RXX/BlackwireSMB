#ifndef BW_READER_SELECTION_H
#define BW_READER_SELECTION_H
#include "source_identity.h"
#define BW_READER_SELECTION_BYTES 4096u
struct bw_reader_selection {
    char source[BW_MANIFEST_MAX_PATH+1],game[75],title[10],run[33],binding_hex[65],prepared_record_hex[65];
    uint64_t eboot_size,manifest_hash;
    uint32_t manifest_count,source_kind,source_policy;
    uint32_t prepared_policy; /* 0 legacy, 1 derived v6, 2 batch v7, 3 projection v8 */
    char prepared_game[75],provenance_hex[65];
};
/* Version 4 is strictly the unchanged folder wire format and parses to an
 * explicit FOLDER/V1 identity. Version 5 is strictly CONTAINER/V1 and requires
 * its explicit source_kind/source_policy fields. Mixed or unknown identities
 * refuse. Parsing grants no launch permission;
 * the connection must recompute the full source binding before any remote I/O.
 * The launch preflight must match the selected durable record and current PC
 * snapshot and rehash local startup bodies before starting the game. */
int bw_reader_selection_parse(struct bw_reader_selection *,const void *,size_t);
/* The one canonical serializer used by runtime admission and persistence.
 * Explicit FOLDER/V1 inputs emit the original version-4 bytes; CONTAINER/V1
 * inputs emit version 5. In-memory zero values never imply a folder. Failure
 * clears size and any supplied output; no partial selection is published. */
int bw_reader_selection_serialize(const struct bw_reader_selection *,void *,size_t,size_t *);
/* Exact canonical absolute name, regular single-link 0600 file, bounded
 * read/readback and named/held identity checks. Never creates or modifies it. */
int bw_reader_selection_load(struct bw_reader_selection *,const char *);

/* Version6 retains game/source_binding as ORIGINAL FOLDER/V1 identity.
 * prepared_policy1/prepared_game are separate; prepared_record_hex is SHA256
 * of COMPLETE new terminal bytes and provenance_hex of COMPLETE BWDPR001.
 * All v6 lines/order/decimal/hex/newline bytes are canonical. None is a live
 * admission or freshness token. Zero appended fields preserve v4/v5. */
int bw_reader_selection_is_derived(const struct bw_reader_selection *);
/* Version7 uses prepared_policy2 and independently pinned BWDPR002/BWNDR002.
 * Original FOLDER/V1 identity remains unchanged; a successful parse grants no
 * source, material, startup-copy or reader admission. V6 is never reinterpreted.
 * The V1-specific helper above deliberately remains false for this policy. */
int bw_reader_selection_is_batch(const struct bw_reader_selection *);

/* Version8 preserves the ORIGINAL FOLDER/CONTAINER V1 identity and uses an
 * explicitly sealed companion projection. prepared_game must equal game;
 * provenance_hex is the full companion-receipt SHA, not a freshness token.
 * Parsing never qualifies that receipt or permits a generated LOCAL path to
 * be treated as a remote file. Admission and reader DATA setup must separately
 * verify the held physical tables, receipt and original source generation.
 * Older wire versions and prepared policies retain their original meanings. */
int bw_reader_selection_is_projection(const struct bw_reader_selection *);

#endif
