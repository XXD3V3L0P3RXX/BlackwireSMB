#ifndef BW_NATIVE_REGISTRATION_H
#define BW_NATIVE_REGISTRATION_H
#include "reader_selection.h"
/* Purely read-only: exact canonical staged mount, byte-identical param.json,
 * selected title/content ID in app.db. Unknown/missing/mismatched data fails.
 * No scanner configuration, filesystem staging or registration mutation. */
int bw_native_registration_verify(const char *,const char *,const char *,const struct bw_reader_selection *);
enum bw_registration_state {BW_REG_UNKNOWN=-1,BW_REG_MATCH=0,BW_REG_ABSENT=1,BW_REG_PENDING=2,BW_REG_CONFLICT=3};
/* Distinguish a truly absent title or this exact partially staged mount from
 * another installed source. Both checks remain read-only. */
enum bw_registration_state bw_native_registration_state(const char *,const char *,const char *,const struct bw_reader_selection *);
/* Independent read-only SQLite query. 1=one exact row, 0=no row, -1=unknown or
 * conflicting/duplicate content. The VFS rejects every write/create/delete and
 * shared-memory request; a DB requiring recovery/WAL mapping is refused. */
int bw_registration_db_match(const char *,const char *,const char *);
#endif
