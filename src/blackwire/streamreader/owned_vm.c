#include "owned_vm.h"
#include <string.h>

#define USER_MIN UINT64_C(0x10000)
#define USER_END UINT64_C(0x800000000000)
#define KERNEL_MIN UINT64_C(0xffff800000000000)

static uint64_t le64(const unsigned char *p) {
    uint64_t v=0; for (unsigned i=0;i<8;i++) v|=(uint64_t)p[i]<<(8*i); return v;
}
/* SDK kernel_is_heap_addr model, additionally requiring x86-64 canonical
 * bit47. vm_map_entry/process/vmspace objects must be aligned heap pointers;
 * kernel text (upper word ffff) is deliberately not admitted as a heap node. */
static int heap_byte(uint64_t p) {
    unsigned hi=(unsigned)((p>>32)&0xffff);
    return p>=KERNEL_MIN && (p>>48)==0xffff && hi && hi!=0xffff;
}
static int heap_object(uint64_t p) { return !(p&7) && heap_byte(p); }
static enum bw_owned_vm_error refuse(struct bw_owned_vm_diagnostic *d,enum bw_owned_vm_error e) {
    d->error=e; return e;
}
static enum bw_owned_vm_error read_at(const struct bw_owned_vm_ops *ops,
    struct bw_owned_vm_diagnostic *d,uint64_t object,uint64_t offset,void *out,size_t size,
    enum bw_owned_vm_stage stage) {
    d->stage=stage; d->callback_errno=0; d->read_address=0; d->read_size=size;
    if (!heap_object(object) || !size || offset>UINT64_MAX-object ||
        size-1>UINT64_MAX-(object+offset) || !heap_byte(object+offset) ||
        !heap_byte(object+offset+size-1)) return refuse(d,BW_OWNED_VM_POINTER);
    d->read_address=object+offset;
    if (ops->read(ops->context,d->read_address,out,size,&d->callback_errno))
        return refuse(d,BW_OWNED_VM_READ);
    return BW_OWNED_VM_OK;
}
static enum bw_owned_vm_error snapshot(const struct bw_owned_vm_ops *ops,int32_t pid,
    uint64_t root_offset,uint64_t address,uint64_t end,struct bw_owned_vm_diagnostic *d) {
    struct bw_owned_vm_snapshot *s=&d->snapshots[d->pass-1];
    unsigned char bytes[16]; enum bw_owned_vm_error e;
    d->stage=BW_OWNED_VM_PROC; d->callback_errno=0; d->read_address=0; d->read_size=0;
    if (ops->process(ops->context,pid,&s->process,&d->callback_errno))
        return refuse(d,BW_OWNED_VM_PROCESS);
    if (!heap_object(s->process)) return refuse(d,BW_OWNED_VM_POINTER);
    e=read_at(ops,d,s->process,0x200,bytes,8,BW_OWNED_VM_VMSPACE); if (e) return e;
    s->vmspace=le64(bytes);
    e=read_at(ops,d,s->vmspace,root_offset,bytes,8,BW_OWNED_VM_ROOT); if (e) return e;
    s->root=le64(bytes); s->entry=s->root;
    for (unsigned i=0;i<BW_OWNED_VM_MAX_NODES;i++) {
        if (!s->entry) return refuse(d,BW_OWNED_VM_NOT_MAPPED);
        s->nodes=i+1;
        e=read_at(ops,d,s->entry,0x20,bytes,sizeof bytes,BW_OWNED_VM_ENTRY_BOUNDS); if (e) return e;
        s->start=le64(bytes); s->end=le64(bytes+8);
        if (s->start>=s->end || s->end>USER_END) return refuse(d,BW_OWNED_VM_BOUNDS);
        if (address<s->start || address>=s->end) {
            e=read_at(ops,d,s->entry,address<s->start?0x10:0x18,bytes,8,BW_OWNED_VM_CHILD);
            if (e) return e;
            s->entry=le64(bytes); continue;
        }
        if (end>s->end) return refuse(d,BW_OWNED_VM_BOUNDS);
        e=read_at(ops,d,s->entry,0x64,&s->protection,1,BW_OWNED_VM_PROTECTION); if (e) return e;
        if ((s->protection&3)!=3) return refuse(d,BW_OWNED_VM_RIGHTS);
        return BW_OWNED_VM_OK;
    }
    d->stage=BW_OWNED_VM_CHILD; return refuse(d,BW_OWNED_VM_LIMIT);
}
enum bw_owned_vm_error bw_owned_vm_check(const struct bw_owned_vm_ops *ops,
    int32_t pid,uint64_t root_offset,uint64_t owned_base,uint64_t owned_size,
    uint64_t address,uint64_t length,struct bw_owned_vm_diagnostic *d) {
    enum bw_owned_vm_error e;
    if (!d) return BW_OWNED_VM_INVALID;
    memset(d,0,sizeof *d);
    if (!ops || !ops->process || !ops->read || pid<=0 || owned_base<USER_MIN ||
        owned_base>=USER_END || (owned_base&0x3fff) || !owned_size || (owned_size&0x3fff) ||
        owned_size>BW_OWNED_VM_MAX_BYTES || owned_size>USER_END-owned_base ||
        address<owned_base || address>=owned_base+owned_size || !length ||
        length>owned_base+owned_size-address) return refuse(d,BW_OWNED_VM_INVALID);
    if (root_offset!=0x1c0 && root_offset!=0x1c8 && root_offset!=0x1d0)
        return refuse(d,BW_OWNED_VM_CONFIG);
    for (d->pass=1;d->pass<=2;d->pass++) {
        e=snapshot(ops,pid,root_offset,address,address+length,d); if (e) return e;
    }
    d->pass=2; d->stage=BW_OWNED_VM_COMPARE;
    const struct bw_owned_vm_snapshot *a=&d->snapshots[0],*b=&d->snapshots[1];
    if (a->process!=b->process || a->vmspace!=b->vmspace || a->entry!=b->entry ||
        a->start!=b->start || a->end!=b->end || a->protection!=b->protection)
        return refuse(d,BW_OWNED_VM_CHANGED);
    return refuse(d,BW_OWNED_VM_OK);
}
const char *bw_owned_vm_error_text(enum bw_owned_vm_error e) {
    static const char *const names[]={"ok","invalid owned user range","unsupported SDK root offset",
        "process lookup failed","kernel metadata read failed","invalid kernel heap pointer",
        "range is unmapped","single entry does not cover range","entry is not readable and writable",
        "covering mapping changed","bounded tree depth exceeded"};
    return (unsigned)e<sizeof names/sizeof names[0]?names[e]:"unknown owned mapping error";
}
const char *bw_owned_vm_stage_text(enum bw_owned_vm_stage s) {
    static const char *const names[]={"validate","process","vmspace","tree-root","entry-bounds",
        "selected-child","entry-protection","compare"};
    return (unsigned)s<sizeof names/sizeof names[0]?names[s]:"unknown";
}
