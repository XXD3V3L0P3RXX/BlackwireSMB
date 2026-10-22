#include "lifecycle.h"
#include <limits.h>
#include <string.h>

enum event_kind { EVENT_START, EVENT_EXIT, EVENT_FINAL };
struct event {
    enum event_kind kind;
    char title[BW_LIFECYCLE_TITLE_BYTES+1];
    int32_t pid;
    uint32_t app_id;
    unsigned has_app;
};
static int space(unsigned char c) { return c==' ' || c=='\t'; }
static int digit(unsigned char c) { return c>='0' && c<='9'; }
static int title_valid(const char *s,size_t n) {
    if (n!=BW_LIFECYCLE_TITLE_BYTES) return 0;
    for (size_t i=0;i<4;i++) if (s[i]<'A' || s[i]>'Z') return 0;
    for (size_t i=4;i<n;i++) if (!digit((unsigned char)s[i])) return 0;
    return 1;
}
static int prefix(const char *s,size_t n,const char *literal) {
    size_t k=strlen(literal);
    return n>=k && !memcmp(s,literal,k);
}
static int hex(unsigned char c) {
    if (digit(c)) return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static int timestamp(const char *s,size_t n) {
    if (n!=10 || s[0]!='[' || s[3]!=':' || s[6]!=':' || s[9]!=']') return 0;
    const unsigned at[]={1,2,4,5,7,8};
    for (size_t i=0;i<sizeof at/sizeof at[0];i++)
        if (!digit((unsigned char)s[at[i]])) return 0;
    return (s[1]-'0')*10+s[2]-'0'<24 &&
           (s[4]-'0')*10+s[5]-'0'<60 && (s[7]-'0')*10+s[8]-'0'<60;
}
/* 1=GAME body, 0=unrelated line, -1=malformed GAME framing. */
static int game_body(const char **body,size_t *length) {
    const char *s=*body; size_t n=*length,at=0;
    while (at<n && space((unsigned char)s[at])) at++;
    if (!prefix(s+at,n-at,"[GAME]")) {
        size_t end=at;
        if (end==n || s[end]!='[') return 0;
        while (end<n && s[end]!=']') end++;
        if (end==n) return 0;
        size_t next=end+1;
        while (next<n && space((unsigned char)s[next])) next++;
        if (!prefix(s+next,n-next,"[GAME]")) return 0;
        if (!timestamp(s+at,end-at+1) || next==end+1) return -1;
        at=next;
    }
    at+=6;
    if (at==n || !space((unsigned char)s[at])) return -1;
    while (at<n && space((unsigned char)s[at])) at++;
    *body=s+at; *length=n-at;
    return 1;
}
static int parse_event(const char *s,size_t n,struct event *e) {
    static const char *const labels[]={"started: ",
        "process exited, awaiting same-app handoff: ",
        "finalizing stopped application: "};
    size_t at=0; unsigned kind;
    for (kind=0;kind<3;kind++) if (prefix(s,n,labels[kind])) break;
    if (kind==3) return 0;
    at=strlen(labels[kind]); e->kind=(enum event_kind)kind;
    if (n-at<10 || !title_valid(s+at,9) || s[at+9]!=' ') return -1;
    memcpy(e->title,s+at,9); e->title[9]=0; at+=10;
    if (!prefix(s+at,n-at,"pid=")) return -1;
    at+=4; size_t begin=at; uint32_t pid=0;
    while (at<n && digit((unsigned char)s[at])) {
        unsigned d=(unsigned)(s[at]-'0');
        if (pid>((uint32_t)INT32_MAX-d)/10) return -1;
        pid=pid*10+d; at++;
    }
    if (at==begin || !pid) return -1;
    e->pid=(int32_t)pid; e->has_app=0; e->app_id=0;
    if (at<n) {
        if (!prefix(s+at,n-at," app_id=0x")) return -1;
        at+=10;
        if (n-at!=8) return -1;
        for (;at<n;at++) {
            int d=hex((unsigned char)s[at]); if (d<0) return -1;
            e->app_id=(e->app_id<<4)|(uint32_t)d;
        }
        e->has_app=1;
    }
    if (kind!=EVENT_FINAL && !e->has_app) return -1;
    return 1;
}
static void track_start(struct bw_lifecycle_track *t,const struct event *e) {
    if (!t->seen || t->pid!=e->pid) {
        *t=(struct bw_lifecycle_track){e->pid,e->pid,e->app_id,1,0};
        return;
    }
    if (t->state==BW_LIFECYCLE_STOPPED || t->app_id!=e->app_id) t->ambiguous=1;
    /* Identical duplicate START while live is harmless; it cannot erase exit
     * evidence or make a detected PID reuse unambiguous. */
}
static void track_exit(struct bw_lifecycle_track *t,const struct event *e) {
    if (!t->seen || t->pid!=e->pid) return;
    if (e->has_app && e->app_id!=t->app_id) t->ambiguous=1;
    if (!t->ambiguous) t->state=BW_LIFECYCLE_STOPPED;
}
int bw_lifecycle_init(struct bw_lifecycle *p,const char *title,int32_t expected) {
    if (!p) return -1;
    memset(p,0,sizeof *p); p->invalid=1;
    if (!title || expected<0 || !title_valid(title,strlen(title))) return -1;
    memcpy(p->title_id,title,10); p->expected_pid=expected; p->invalid=0;
    return 0;
}
int bw_lifecycle_feed_line(struct bw_lifecycle *p,const void *line,size_t n) {
    if (!p) return -1;
    if (p->invalid || p->finished || !line || !n || n>BW_LIFECYCLE_MAX_LINE) goto invalid;
    const char *s=line;
    if (s[n-1]!='\n') goto invalid;
    n--; if (n && s[n-1]=='\r') n--;
    for (size_t i=0;i<n;i++) if (!s[i] || s[i]=='\n' || s[i]=='\r') goto invalid;
    int framing=game_body(&s,&n);
    if (framing<0) goto invalid;
    if (!framing) return 0;
    struct event e={0}; int parsed=parse_event(s,n,&e);
    if (parsed<0) goto invalid;
    if (!parsed) {
        /* An unrecognized lifecycle variant mentioning this title might hide
         * a handoff/exit, so do not infer liveness across it. */
        for (size_t i=0;i+9<=n;i++)
            if (!memcmp(s+i,p->title_id,9)) goto invalid;
        if (prefix(s,n,"started") || prefix(s,n,"process exited") ||
            prefix(s,n,"finalizing stopped")) goto invalid;
        return 0;
    }
    if (memcmp(e.title,p->title_id,10)) return 0;
    if (e.kind==EVENT_START) {
        track_start(&p->latest,&e);
        if (e.pid==p->expected_pid) track_start(&p->expected,&e);
    } else {
        track_exit(&p->latest,&e); track_exit(&p->expected,&e);
    }
    return 1;
invalid:
    p->invalid=1; return -1;
}
static int32_t result(const struct bw_lifecycle *p,const struct bw_lifecycle_track *t) {
    if (!p->finished || p->invalid || !t->seen || t->ambiguous)
        return BW_LIFECYCLE_UNKNOWN;
    return t->state;
}
int32_t bw_lifecycle_discover(const struct bw_lifecycle *p) {
    return p?result(p,&p->latest):BW_LIFECYCLE_UNKNOWN;
}
int32_t bw_lifecycle_expected(const struct bw_lifecycle *p) {
    return p?result(p,&p->expected):BW_LIFECYCLE_UNKNOWN;
}
int32_t bw_lifecycle_finish(struct bw_lifecycle *p,int complete) {
    if (!p) return BW_LIFECYCLE_UNKNOWN;
    if (complete!=1) p->invalid=1;
    p->finished=1;
    return p->expected_pid?bw_lifecycle_expected(p):bw_lifecycle_discover(p);
}
