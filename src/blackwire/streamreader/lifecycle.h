#ifndef BW_LIFECYCLE_H
#define BW_LIFECYCLE_H
#include <stddef.h>
#include <stdint.h>

enum { BW_LIFECYCLE_UNKNOWN=-2, BW_LIFECYCLE_STOPPED=-1 };
enum { BW_LIFECYCLE_TITLE_BYTES=9, BW_LIFECYCLE_MAX_LINE=16384 };

struct bw_lifecycle_track {
    int32_t pid, state;
    uint32_t app_id;
    unsigned seen, ambiguous;
};
struct bw_lifecycle {
    char title_id[BW_LIFECYCLE_TITLE_BYTES+1];
    int32_t expected_pid;
    struct bw_lifecycle_track latest, expected;
    unsigned invalid, finished;
};

/* Pure snapshot parser: no file access, allocations, process queries or writes.
 * expected_pid=0 selects discovery; >0 independently tracks that exact PID.
 * Reinitialize for every snapshot, then feed ALL lines in original file order.
 * A line includes its LF (optional preceding CR), without a terminating NUL.
 * A split/truncated line is an error, rather than a continuation to be joined.
 * Accepted records are ShadowMount's timestamped [GAME] started, process exited,
 * and finalizing stopped application messages. Bare [GAME] lines also work.
 * app_id is validated but NEVER used to associate different process IDs.
 */
int bw_lifecycle_init(struct bw_lifecycle *parser,const char *title_id,
                      int32_t expected_pid);
/* 1: selected-title event; 0: ignored complete line; -1: malformed snapshot. */
int bw_lifecycle_feed_line(struct bw_lifecycle *parser,const void *line,size_t length);
/* complete_read is 1 ONLY after successful EOF with no ferror, failed read,
 * missing file, detected rotation/truncation, or unfinished last line. Passing
 * 0, malformed input, or no selected-PID START evidence yields UNKNOWN.
 * Until finish, getters also return UNKNOWN. A later failed finish poisons an
 * earlier successful result; only reinitializing can recover a failed parser.
 */
int32_t bw_lifecycle_finish(struct bw_lifecycle *parser,int complete_read);
int32_t bw_lifecycle_discover(const struct bw_lifecycle *parser);
int32_t bw_lifecycle_expected(const struct bw_lifecycle *parser);

/* STOPPED means a positive exit/finalizing record followed an observed START
 * for that PID. Another title/PID's exit cannot establish this. Missing START
 * after log rotation is UNKNOWN. A detected restart/reuse of the same PID is
 * ambiguous (the finalization record has no generation), so remains UNKNOWN.
 * Like the source log, this is observational, not an OS liveness guarantee.
 * Keep native buffers/locks pinned on UNKNOWN; it is never proof of exit.
 */
#endif
