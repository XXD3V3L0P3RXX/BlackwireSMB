#ifndef BW_NATIVE_REGISTRATION_H
#define BW_NATIVE_REGISTRATION_H
#include "reader_selection.h"
/* Purely read-only: exact canonical staged mount, byte-identical param.json,
 * selected title/content ID in app.db. Unknown/missing/mismatched data fails.
 * No scanner configuration, filesystem staging or registration mutation. */
int bw_native_registration_verify(const char *,const char *,const char *,const struct bw_reader_selection *);
enum bw_registration_state {BW_REG_UNKNOWN=-1,BW_REG_MATCH=0,BW_REG_ABSENT=1,BW_REG_PENDING=2,BW_REG_CONFLICT=3};
enum bw_registration_db_state {BW_REG_DB_UNKNOWN=-1,BW_REG_DB_ABSENT=0,BW_REG_DB_MATCH=1,BW_REG_DB_CONFLICT=2};
enum bw_registration_db_reason {
    BW_REG_DB_NOT_OBSERVED=0,BW_REG_DB_INVALID_ARGUMENT,BW_REG_DB_PATH_UNAVAILABLE,BW_REG_DB_PATH_INVALID,
    BW_REG_DB_FILE_INVALID,BW_REG_DB_VFS_UNAVAILABLE,BW_REG_DB_OPEN_FAILED,BW_REG_DB_NOT_READONLY,
    BW_REG_DB_PREPARE_FAILED,BW_REG_DB_BIND_FAILED,BW_REG_DB_STEP_FAILED,BW_REG_DB_ROW_INVALID,
    BW_REG_DB_CONTENT_DIFFERS,BW_REG_DB_DUPLICATE_ROW,BW_REG_DB_FINALIZE_FAILED,BW_REG_DB_CLOSE_FAILED,
    BW_REG_DB_IDENTITY_CHANGED,BW_REG_DB_ROW_ABSENT,BW_REG_DB_ROW_MATCH
};
struct bw_registration_db_observation {
    enum bw_registration_db_state state;
    enum bw_registration_db_reason reason;
    int sqlite_result,error_snapshot;
    unsigned release_or_identity_unknown;
};
enum bw_registration_reason {
    BW_REG_NOT_OBSERVED=0,BW_REG_INVALID_ARGUMENT,BW_REG_ALLOCATION_FAILED,BW_REG_PATH_LIMIT,
    BW_REG_SOURCE_DIRECTORY_UNAVAILABLE,BW_REG_APPS_PARENT_UNAVAILABLE,BW_REG_APP_DIRECTORY_MISSING,
    BW_REG_APP_DIRECTORY_UNAVAILABLE,BW_REG_MOUNT_MISSING,BW_REG_MOUNT_UNREADABLE,BW_REG_MOUNT_DIFFERS,
    BW_REG_SOURCE_PARAM_UNREADABLE,BW_REG_SOURCE_PARAM_INVALID,BW_REG_APP_PARAM_UNREADABLE,
    BW_REG_PARAM_PAIR_DIFFERS,BW_REG_DATABASE_UNKNOWN,BW_REG_DATABASE_CONFLICT,BW_REG_DATABASE_ABSENT,
    BW_REG_DATABASE_ROW_WITHOUT_APP,BW_REG_APP_LOOKUP_FAILED,BW_REG_OBSERVATION_CHANGED,
    BW_REG_EXACT_MATCH,BW_REG_TITLE_ABSENT,BW_REG_EXACT_PARTIAL
};
/* Observation only. Fixed labels/numeric results, never DB contents, paths,
 * credentials or authority. The first failed verification reason is retained
 * separately from the final fallback-state reason. Legacy decisions remain. */
struct bw_native_registration_observation {
    enum bw_registration_state state;
    enum bw_registration_reason first_reason,reason;
    int first_raw_result,first_error_snapshot,raw_result,error_snapshot;
    struct bw_registration_db_observation database;
};
enum bw_registration_state bw_native_registration_observe(const char *,const char *,const char *,
    const struct bw_reader_selection *,struct bw_native_registration_observation *);
const char *bw_native_registration_reason_name(enum bw_registration_reason);
const char *bw_registration_state_name(enum bw_registration_state);
const char *bw_registration_db_reason_name(enum bw_registration_db_reason);
const char *bw_registration_db_state_name(enum bw_registration_db_state);
/* Distinguish a truly absent title or this exact partially staged mount from
 * another installed source. Both checks remain read-only. */
enum bw_registration_state bw_native_registration_state(const char *,const char *,const char *,const struct bw_reader_selection *);
/* Independent read-only SQLite query. 1=one exact row, 0=no row, -1=unknown or
 * conflicting/duplicate content. The VFS rejects every write/create/delete and
 * shared-memory request; a DB requiring recovery/WAL mapping is refused. */
int bw_registration_db_match(const char *,const char *,const char *);
/* Same -1/0/1 result and identical read-only query/release sequence as match;
 * the optional observation distinguishes known row conflict from query error. */
int bw_registration_db_observe(const char *,const char *,const char *,struct bw_registration_db_observation *);
#endif
