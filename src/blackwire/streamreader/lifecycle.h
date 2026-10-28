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
/* Logical application-idle history is distinct from the selected-PID lifetime
 * API above. These pure helpers own no files, callbacks, allocations or native
 * identity. Every caller must preserve its existing complete-read, snapshot,
 * close, process/birth and authority brackets. A matching FINALIZING is only
 * the inherited logical history endpoint, not proof of completed SMP cleanup
 * or a live-process census. Empty/quiet or debug-disabled logs prove no more. */
#include <string.h>
enum { BW_LIFECYCLE_IDLE_MAX_TITLES=128, BW_LIFECYCLE_IDLE_MAX_LINE=1024 };
enum bw_lifecycle_idle_reason {
    BW_LIFECYCLE_IDLE_NONE, BW_LIFECYCLE_IDLE_INCOMPLETE,
    BW_LIFECYCLE_IDLE_RECORD, BW_LIFECYCLE_IDLE_CAPACITY
};
struct bw_lifecycle_idle_row {
    char title[10];int32_t pid;uint32_t app_id;
    unsigned active,pending,has_app;
};
struct bw_lifecycle_idle_fold {
    struct bw_lifecycle_idle_row rows[BW_LIFECYCLE_IDLE_MAX_TITLES];
    size_t count;enum bw_lifecycle_idle_reason reason;
    unsigned initialized,finished,invalid;
};
enum bw_lifecycle_idle_event_kind {
    BW_LIFECYCLE_IDLE_START, BW_LIFECYCLE_IDLE_AWAIT,
    BW_LIFECYCLE_IDLE_HANDOFF, BW_LIFECYCLE_IDLE_FINAL
};
struct bw_lifecycle_idle_event {
    char title[10];int32_t pid,next_pid;uint32_t app_id;unsigned has_app;
};
static inline void bw_lifecycle_idle_fold_init(struct bw_lifecycle_idle_fold *p){
    if(p){memset(p,0,sizeof *p);p->initialized=1;}
}
static inline int bw_lifecycle_idle_fail(struct bw_lifecycle_idle_fold *p,enum bw_lifecycle_idle_reason reason){
    if(p){if(p->reason==BW_LIFECYCLE_IDLE_NONE)p->reason=reason;p->invalid=1;}return -1;
}
static inline int bw_lifecycle_idle_title_valid(const char *p,size_t n){
    if(n!=9||(memcmp(p,"PPSA",4)&&memcmp(p,"CUSA",4)&&memcmp(p,"LAPY",4)&&memcmp(p,"FAKE",4)))return 0;
    for(size_t i=4;i<9;i++)if(p[i]<'0'||p[i]>'9')return 0;return 1;
}
static inline int bw_lifecycle_idle_take(const char **p,const char *end,const char *text){
    size_t n=strlen(text);if((size_t)(end-*p)<n||memcmp(*p,text,n))return -1;*p+=n;return 0;
}
static inline int bw_lifecycle_idle_pid(const char **p,const char *end,int32_t *out){
    const char *at=*p,*begin=at;uint32_t value=0;
    while(at<end&&*at>='0'&&*at<='9'){
        unsigned digit=(unsigned)(*at-'0');if(value>((uint32_t)INT32_MAX-digit)/10u)return -1;
        value=value*10u+digit;at++;
    }
    if(at==begin||!value)return -1;*p=at;*out=(int32_t)value;return 0;
}
static inline int bw_lifecycle_idle_app(const char **p,const char *end,uint32_t *out){
    if((size_t)(end-*p)<8u)return -1;uint32_t value=0;
    for(unsigned i=0;i<8u;i++){
        unsigned char c=(unsigned char)(*p)[i];unsigned digit;
        if(c>='0'&&c<='9')digit=(unsigned)(c-'0');
        else if(c>='a'&&c<='f')digit=(unsigned)(c-'a'+10);
        else if(c>='A'&&c<='F')digit=(unsigned)(c-'A'+10);else return -1;
        value=(value<<4)|digit;
    }
    *p+=8;*out=value;return 0;
}
static inline const char *bw_lifecycle_idle_tag(const char *line,size_t n,enum bw_lifecycle_idle_event_kind *kind){
    static const struct {const char *text;size_t length;} tags[]={
        {"[GAME] started",sizeof "[GAME] started"-1},
        {"[GAME] process exited, awaiting same-app handoff",sizeof "[GAME] process exited, awaiting same-app handoff"-1},
        {"[GAME] process handoff",sizeof "[GAME] process handoff"-1},
        {"[GAME] finalizing stopped application",sizeof "[GAME] finalizing stopped application"-1}
    };
    /* Retain tag priority, then the earliest occurrence within that tag, even
     * when a complete line contains more than one recognizable tag. Only '['
     * positions can match; memchr remains bounded by the caller's exact line. */
    unsigned best=(unsigned)(sizeof tags/sizeof tags[0]);const char *found=NULL,*cursor=line;size_t left=n;
    while(left){
        const char *at=memchr(cursor,'[',left);if(!at)break;
        size_t remaining=n-(size_t)(at-line);
        for(unsigned k=0;k<best;k++)if(remaining>=tags[k].length&&!memcmp(at,tags[k].text,tags[k].length)){
            best=k;found=at+tags[k].length;break;
        }
        if(!best)break;
        cursor=at+1;left=n-(size_t)(cursor-line);
    }
    if(found)*kind=(enum bw_lifecycle_idle_event_kind)best;return found;
}
static inline int bw_lifecycle_idle_decode(const char *p,const char *end,enum bw_lifecycle_idle_event_kind kind,struct bw_lifecycle_idle_event *event){
    if(bw_lifecycle_idle_take(&p,end,": ")||(size_t)(end-p)<10u||p[9]!=' '||!bw_lifecycle_idle_title_valid(p,9))return -1;
    memcpy(event->title,p,9);event->title[9]=0;p+=9;
    if(kind==BW_LIFECYCLE_IDLE_HANDOFF){
        if(bw_lifecycle_idle_take(&p,end," app_id=0x")||bw_lifecycle_idle_app(&p,end,&event->app_id)||
           bw_lifecycle_idle_take(&p,end," pid=")||bw_lifecycle_idle_pid(&p,end,&event->pid)||
           bw_lifecycle_idle_take(&p,end," -> pid=")||bw_lifecycle_idle_pid(&p,end,&event->next_pid))return -1;
        event->has_app=1;
    }else{
        if(bw_lifecycle_idle_take(&p,end," pid=")||bw_lifecycle_idle_pid(&p,end,&event->pid))return -1;
        /* Simple legacy START/final pairs remain supported. A handoff still
         * requires an app-bearing prior START or its matching awaiting row. */
        if(kind!=BW_LIFECYCLE_IDLE_FINAL&&p<end){
            if(bw_lifecycle_idle_take(&p,end," app_id=0x")||bw_lifecycle_idle_app(&p,end,&event->app_id))return -1;
            event->has_app=1;
        }
    }
    return p==end?0:-1;
}
/* Feed one complete LF/CRLF line, with its exact byte length (no NUL needed).
 * 1=recognized event,0=unrelated complete line,-1=sticky malformed history. */
static inline int bw_lifecycle_idle_fold_line(struct bw_lifecycle_idle_fold *p,const void *bytes,size_t n){
    if(!p||p->initialized!=1)return -1;
    if(p->invalid)return -1;
    if(p->finished)return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_RECORD);
    if(!bytes||!n||n>BW_LIFECYCLE_IDLE_MAX_LINE)return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_INCOMPLETE);
    const char *line=(const char *)bytes;
    if(line[n-1]!='\n')return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_INCOMPLETE);
    n--;if(n&&line[n-1]=='\r')n--;
    for(size_t i=0;i<n;i++)if(!line[i]||line[i]=='\n'||line[i]=='\r')return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_RECORD);
    enum bw_lifecycle_idle_event_kind kind=BW_LIFECYCLE_IDLE_START;
    const char *at=bw_lifecycle_idle_tag(line,n,&kind);if(!at)return 0;
    struct bw_lifecycle_idle_event event={0};
    if(bw_lifecycle_idle_decode(at,line+n,kind,&event))return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_RECORD);
    if(p->count>BW_LIFECYCLE_IDLE_MAX_TITLES)return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_CAPACITY);
    size_t i=0;for(;i<p->count;i++)if(!memcmp(p->rows[i].title,event.title,10))break;
    if(i==p->count){
        if(kind==BW_LIFECYCLE_IDLE_HANDOFF||kind==BW_LIFECYCLE_IDLE_AWAIT)return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_RECORD);
        if(p->count==BW_LIFECYCLE_IDLE_MAX_TITLES)return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_CAPACITY);
        memcpy(p->rows[i].title,event.title,10);p->count++;
    }
    struct bw_lifecycle_idle_row *row=p->rows+i;
    if(kind==BW_LIFECYCLE_IDLE_START){
        /* Explicit newer START preserves the established latest-row policy. */
        row->pid=event.pid;row->app_id=event.app_id;row->has_app=event.has_app;row->active=1;row->pending=0;
    }else if(kind==BW_LIFECYCLE_IDLE_AWAIT){
        if(!row->active||row->pid!=event.pid||(row->has_app&&row->app_id&&event.has_app&&row->app_id!=event.app_id))
            return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_RECORD);
        row->pending=1;
        if(event.has_app){row->app_id=event.app_id;row->has_app=1;}
    }else if(kind==BW_LIFECYCLE_IDLE_HANDOFF){
        if(!row->active||row->pid!=event.pid||!row->has_app||(row->app_id&&row->app_id!=event.app_id)||
           (event.pid==event.next_pid&&!row->pending))return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_RECORD);
        for(size_t j=0;j<p->count;j++)if(j!=i&&p->rows[j].active&&p->rows[j].pid==event.next_pid)
            return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_RECORD);
        row->pid=event.next_pid;row->app_id=event.app_id;row->has_app=1;row->pending=0;
    }else if(row->pid==event.pid){
        /* Noncurrent OLD finalization is harmless and cannot clear NEW. A
         * later matching NEW finalization can still close the logical row. */
        row->active=0;row->pending=0;
    }
    return 1;
}
/* 0=logical quiet (or only the caller's already-qualified exact current row),
 * 1=live/pending history, -1=unknown. TITLE precedes PID. Pending is never
 * exempt; no app-id chain transfers native birth/package/ownership authority.
 * A failed complete_read poisons this fold until explicit reinitialization. */
static inline int bw_lifecycle_idle_fold_finish(struct bw_lifecycle_idle_fold *p,int complete_read,const char *qualified_allow_title,int32_t qualified_allow_pid){
    if(!p||p->initialized!=1)return -1;
    if(complete_read!=1)bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_INCOMPLETE);
    p->finished=1;if(p->invalid)return -1;
    if(p->count>BW_LIFECYCLE_IDLE_MAX_TITLES)return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_CAPACITY);
    if(qualified_allow_title){
        size_t n=0;while(n<10u&&qualified_allow_title[n])n++;
        if(qualified_allow_pid<=1||!bw_lifecycle_idle_title_valid(qualified_allow_title,n))return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_RECORD);
    }else if(qualified_allow_pid)return bw_lifecycle_idle_fail(p,BW_LIFECYCLE_IDLE_RECORD);
    for(size_t i=0;i<p->count;i++)if(p->rows[i].active&&
        (p->rows[i].pending||!qualified_allow_title||p->rows[i].pid!=qualified_allow_pid||memcmp(p->rows[i].title,qualified_allow_title,10)))return 1;
    return 0;
}
#endif
