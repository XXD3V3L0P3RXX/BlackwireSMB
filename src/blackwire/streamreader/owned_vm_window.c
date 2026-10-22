#include "owned_vm_window.h"
#include <string.h>

static int same_mapping(const struct bw_owned_vm_snapshot *a,const struct bw_owned_vm_snapshot *b) {
    return a->process==b->process && a->vmspace==b->vmspace && a->entry==b->entry &&
        a->start==b->start && a->end==b->end && a->protection==b->protection;
}
static int checked_clock(const struct bw_vm_window_ops *ops,uint64_t start,uint64_t *last) {
    uint64_t now=ops->now_us(ops->context);
    if (!now || now<*last || now<start) return -1;
    *last=now; return now-start>=BW_VM_WINDOW_MAX_US?1:0;
}
enum bw_vm_window_error bw_vm_window_check(struct bw_vm_window_state *state,
    const struct bw_vm_window_ops *ops,struct bw_vm_window_diagnostic *d) {
    if (!d) return BW_VM_WINDOW_INVALID;
    memset(d,0,sizeof *d);
    if (!state || !ops || !ops->owner || !ops->probe || !ops->now_us || !ops->pause_us ||
        (state->anchored!=0 && state->anchored!=1)) return BW_VM_WINDOW_INVALID;
    uint64_t start=ops->now_us(ops->context),last=start,first_good=0;
    struct bw_owned_vm_snapshot candidate;
    memset(&candidate,0,sizeof candidate);
    if (!start) return BW_VM_WINDOW_CLOCK;
    int confirm=!state->anchored,transient=0;
    for (unsigned i=0;i<BW_VM_WINDOW_PROBES;i++) {
        int clock=checked_clock(ops,start,&last);
        if (clock) return clock<0?BW_VM_WINDOW_CLOCK:BW_VM_WINDOW_LIMIT;
        if (ops->owner(ops->context)) return BW_VM_WINDOW_OWNER;
        memset(&d->last,0,sizeof d->last); d->probes++;
        d->last_probe=ops->probe(ops->context,&d->last);
        if (ops->owner(ops->context)) return BW_VM_WINDOW_OWNER;
        clock=checked_clock(ops,start,&last);
        if (clock) return clock<0?BW_VM_WINDOW_CLOCK:BW_VM_WINDOW_LIMIT;
        if (d->last_probe==BW_OWNED_VM_OK) {
            if (d->last.error!=BW_OWNED_VM_OK || d->last.pass!=2 ||
                !same_mapping(&d->last.snapshots[0],&d->last.snapshots[1])) return BW_VM_WINDOW_PROBE;
            const struct bw_owned_vm_snapshot *current=&d->last.snapshots[1];
            if (state->anchored && !same_mapping(&state->anchor,current)) return BW_VM_WINDOW_MAPPING;
            if (!confirm) return BW_VM_WINDOW_OK;
            if (!first_good) { first_good=last; candidate=*current; }
            else if (!same_mapping(&candidate,current)) return BW_VM_WINDOW_MAPPING;
            if (last-first_good>=BW_VM_WINDOW_STABLE_US) {
                if (!state->anchored) { state->anchor=*current; state->anchored=1; }
                d->recovered=transient;
                return BW_VM_WINDOW_OK;
            }
        } else if (d->last_probe==BW_OWNED_VM_NOT_MAPPED || d->last_probe==BW_OWNED_VM_CHANGED) {
            confirm=1; first_good=0; transient=1;
        } else return BW_VM_WINDOW_PROBE;
        if (i+1<BW_VM_WINDOW_PROBES) { ops->pause_us(ops->context,1000); d->waits++; }
    }
    return BW_VM_WINDOW_LIMIT;
}
const char *bw_vm_window_error_text(enum bw_vm_window_error error) {
    static const char *const names[]={"ok","invalid window","pending owner changed",
        "permission probe refused","original allocation mapping changed","invalid clock","bounded observation expired"};
    return (unsigned)error<sizeof names/sizeof names[0]?names[error]:"unknown observation error";
}
