#include "native_profile.h"
#include <string.h>

#define USER_MIN UINT64_C(0x10000)
#define USER_END UINT64_C(0x800000000000)

/* Provenance: docs/NATIVE-READ-PROVENANCE.md. These are bytes from the
 * preserved ELF, not positions inferred from a newer source hook enum. */
static const struct window {
    uint64_t offset;
    size_t length;
    unsigned char bytes[17];
} windows[] = {
    {0x0fb00,17,{0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x83,0xe4,0xe0}},
    {0x1f7f2,7,{0x48,0x8b,0x0d,0x6f,0x52,0x04,0x00}}, /* lseek */
    {0x1fb96,7,{0x4c,0x8b,0x05,0xe3,0x4e,0x04,0x00}}, /* pread */
    {0x1fd8d,7,{0x48,0x8b,0x1d,0xf4,0x4c,0x04,0x00}}, /* preadv */
    {0x20114,7,{0x48,0x8b,0x0d,0x75,0x49,0x04,0x00}}, /* read */
    {0x20421,7,{0x48,0x8b,0x0d,0x70,0x46,0x04,0x00}}, /* readv */
    {0x2124c,7,{0x4c,0x8b,0x05,0x4d,0x38,0x04,0x00}}, /* AIO single */
    {0x0fb7c,7,{0x4c,0x8b,0x05,0x25,0x4f,0x05,0x00}}, /* AIO multiple */
    {0x12fd4,7,{0x48,0x8b,0x0d,0xd5,0x1a,0x05,0x00}}, /* AIO poll */
    {0x1d5ab,7,{0x48,0x8b,0x0d,0xa6,0x74,0x04,0x00}}, /* POSIX close, NOT sceKernelClose */
    {0x1d6ee,7,{0x48,0x8b,0x0d,0x6b,0x73,0x04,0x00}}, /* fstat */
    {0x1bf4b,7,{0x4c,0x8b,0x35,0xfe,0x88,0x04,0x00}}, /* POSIX open -> 64850 */
};

/* Retained ELF SHA256 039b09cd...555b01. Full refresh prefix includes
 * all 84 table iterations and the mandatory-mask completion store. */
static const struct runtime_window {
    uint64_t offset; size_t length; unsigned char bytes[378];
} runtime_windows[] = {
    {0x42d3f,11,{
        0xb8,0x01,0x00,0x00,0x00,0x87,0x05,0xbe,0xd2,0xcc,0x18,
    }},
    {0x42d88,10,{
        0x8b,0x05,0x7e,0xd2,0xcc,0x18,0x85,0xc0,0x74,0x4b,
    }},
    {0x43144,17,{
        0xe8,0xf7,0x7e,0x00,0x00,0x31,0xc0,0xc7,0x05,0xb3,0xce,0xcc,0x18,0x00,0x00,0x00,
        0x00,
    }},
    {0x4b040,378,{
        0x55,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x8d,0x05,0x77,0x9d,0x01,
        0x00,0x41,0xb9,0x01,0x00,0x00,0x00,0x4c,0x8d,0x15,0xa2,0x0f,0x01,0x00,0x4c,0x8d,
        0x1d,0xeb,0x97,0x01,0x00,0xbb,0x00,0x00,0x00,0x00,0x41,0xbf,0x00,0x00,0x00,0x00,
        0x45,0x31,0xc0,0x45,0x31,0xf6,0x31,0xc9,0x31,0xff,0x31,0xf6,0x90,0x90,0x90,0x90,
        0x89,0xca,0x49,0x89,0xf5,0x49,0x89,0xfc,0xc4,0xc2,0xf1,0xf7,0xf9,0x48,0x8d,0x68,
        0xb0,0x80,0xe2,0x3f,0x89,0xd6,0x40,0xf6,0xde,0x84,0xd2,0x49,0x8d,0x14,0x1a,0xc4,
        0xc2,0xcb,0xf7,0xf1,0x49,0x0f,0x44,0xf0,0xf6,0xc1,0x40,0x48,0x0f,0x45,0xf7,0x49,
        0x0f,0x45,0xf8,0x80,0x78,0x98,0x00,0x49,0x0f,0x44,0xf0,0x49,0x0f,0x44,0xf8,0x49,
        0x09,0xf6,0x49,0x09,0xff,0x4c,0x09,0xee,0x4c,0x09,0xe7,0x80,0x38,0x00,0x48,0x0f,
        0x44,0xea,0x49,0x0f,0x44,0xf5,0x49,0x0f,0x44,0xfc,0x48,0xff,0xc1,0x48,0x83,0xe8,
        0x80,0x48,0x8b,0x55,0x00,0x4a,0x89,0x14,0x1b,0x48,0x83,0xc3,0x08,0x48,0x83,0xf9,
        0x54,0x75,0x8d,0x31,0xc0,0x80,0x3d,0x64,0x9a,0x01,0x00,0x00,0xb9,0x00,0x00,0x00,
        0x00,0xba,0x00,0x00,0x00,0x00,0x74,0x07,0x48,0x8b,0x15,0x01,0x9a,0x01,0x00,0x48,
        0x89,0x15,0x0a,0x4f,0xcc,0x18,0xba,0x00,0x00,0x00,0x00,0x80,0x3d,0xb6,0x9a,0x01,
        0x00,0x00,0x74,0x07,0x48,0x8b,0x15,0x5d,0x9a,0x01,0x00,0x48,0x89,0x15,0xf6,0x4e,
        0xcc,0x18,0xba,0x00,0x00,0x00,0x00,0x80,0x3d,0x12,0x9b,0x01,0x00,0x00,0x74,0x07,
        0x48,0x8b,0x15,0xb9,0x9a,0x01,0x00,0x48,0x89,0x15,0xe2,0x4e,0xcc,0x18,0xba,0x00,
        0x00,0x00,0x00,0x80,0x3d,0x6e,0x9b,0x01,0x00,0x00,0x74,0x07,0x48,0x8b,0x15,0x15,
        0x9b,0x01,0x00,0x48,0x89,0x15,0xce,0x4e,0xcc,0x18,0xba,0x00,0x00,0x00,0x00,0x80,
        0x3d,0xca,0x9b,0x01,0x00,0x00,0x74,0x07,0x48,0x8b,0x15,0x71,0x9b,0x01,0x00,0x48,
        0x89,0x15,0xba,0x4e,0xcc,0x18,0x80,0x3d,0x1b,0x4f,0xcc,0x18,0x00,0x74,0x07,0x48,
        0x8b,0x0d,0xc2,0x4e,0xcc,0x18,0x4c,0x31,0xff,0x4d,0x09,0xf7,0x48,0x89,0x0d,0x5d,
        0x4e,0xcc,0x18,0x0f,0x95,0xc1,0x4c,0x31,0xf6,0x48,0x09,0xfe,0x0f,0x94,0xc2,0x20,
        0xca,0x0f,0xb6,0xca,0x89,0x0d,0x52,0x4e,0xcc,0x18,
    }},
    {0xe43f,10,{
        0xc4,0xe3,0x79,0x20,0x05,0xff,0x8e,0x05,0x00,0x02,
    }},
};

/* Retained RW PT_LOAD: vaddr=0x60000, memsz=0x18cc0fe0. The high
 * state words are mapped BSS, not file offsets and not an out-of-span guess. */
#define AMPR_RW_FIRST UINT64_C(0x60000)
#define AMPR_RW_END UINT64_C(0x18d20fe0)
#define AIO_LOCK UINT64_C(0x18d10008)
#define AIO_COMPLETE UINT64_C(0x18d1000c)
#define AIO_TRAMPOLINE UINT64_C(0x672f8)
#define AIO_INSTALLED UINT64_C(0x67348)
#define AIO_ORIGINAL UINT64_C(0x64aa8)
_Static_assert(AMPR_RW_END<=BW_NATIVE_AMPR_SPAN,"Retained RW mapping fits profile span");
_Static_assert(AIO_LOCK>=AMPR_RW_FIRST && AIO_LOCK+4<=AMPR_RW_END,"Lock mapped as uint32");
_Static_assert(AIO_COMPLETE>=AMPR_RW_FIRST && AIO_COMPLETE+4<=AMPR_RW_END,"Completion mapped as uint32");
_Static_assert(AIO_TRAMPOLINE>=AMPR_RW_FIRST && AIO_TRAMPOLINE+8<=AMPR_RW_END,"Trampoline mapped as pointer");
_Static_assert(AIO_INSTALLED>=AMPR_RW_FIRST && AIO_INSTALLED+1<=AMPR_RW_END,"Hook flag mapped as byte");
_Static_assert(AIO_ORIGINAL>=AMPR_RW_FIRST && AIO_ORIGINAL+8<=AMPR_RW_END,"Original mapped as pointer");

static const unsigned char aio_prefix[] = {
    0x55,0x48,0x89,0xe5,0x49,0x89,0xc8,0x89,0xd1,0x89,0xf2,0x48,0x89,0xfe
};
static const unsigned char read_prefix[] = {
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x54,0x53,0x41,0x89,0xff
};
static const unsigned char preadv_prefix[] = {
    0x48,0xc7,0xc0,0x21,0x01,0x00,0x00,0x49,0x89,0xca
};
static const unsigned char poll_prefix[] = {
    0x55,0x48,0x89,0xe5,0x53,0x48,0x83,0xec,0x18,0x48,0xbb
};
static const unsigned char pread_stub[] = {
    0x48,0xc7,0xc0,0xdb,0x01,0x00,0x00,0x49,0x89,0xca,0x0f,0x05,0x72,0x01,0xc3,0x48,
    0x8d,0x0d,0x9e,0xe8,0xff,0xff,0xff,0xe1,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc
};
static const unsigned char lseek_stub[] = {
    0x48,0xc7,0xc0,0xde,0x01,0x00,0x00,0x49,0x89,0xca,0x0f,0x05,0x72,0x01,0xc3,0x48,
    0x8d,0x0d,0x3e,0xe8,0xff,0xff,0xff,0xe1,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc
};
static const unsigned char abs_jump[] = {0xff,0x25,0,0,0,0};
static const unsigned char close_prefix[] = {
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x53,0x50,0x89,0xfb,
    0x64,0x4c,0x8b,0x3c,0x25,0x10,0x00,0x00,0x00
};
static const unsigned char fstat_prefix[] = {
    0x48,0xc7,0xc0,0xbd,0x00,0x00,0x00,0x49,0x89,0xca
};
static const unsigned char open_prefix[] = {
    0x55,0x48,0x89,0xe5,0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x53,0x48,0x81,0xec,0xd8,0,0,0
};
/* Village-03 expanded capture, entire function through RET. Every branch is
 * internal; no CALL or indirect branch. Globals are range/thread data only.
 * The result is TLS+0x188 or libkernel+0x75258; no errno access occurs here. */
static const unsigned char errno_body[] = {
    0x55,0x48,0x89,0xe5,0x48,0x8b,0x0d,0x35,0x95,0x06,0x00,0x48,0x85,0xc9,0x74,0x05,
    0x48,0x89,0xea,0xeb,0x02,0x31,0xd2,0x48,0x83,0x3d,0xb9,0xf1,0x09,0x00,0x00,0x48,
    0x8d,0x05,0x32,0x24,0x07,0x00,0x74,0x3e,0x48,0x8d,0x71,0xff,0x48,0x39,0xd6,0x73,
    0x0c,0x48,0x03,0x0d,0x10,0x95,0x06,0x00,0x48,0x39,0xca,0x76,0x29,0x64,0x48,0x8b,
    0x0c,0x25,0x10,0x00,0x00,0x00,0x48,0x39,0x0d,0x8b,0xf1,0x09,0x00,0x48,0x8d,0x15,
    0x04,0x24,0x07,0x00,0x48,0x8d,0x81,0x88,0x01,0x00,0x00,0x48,0x0f,0x44,0xc2,0x48,
    0x85,0xc9,0x48,0x0f,0x44,0xc2,0x5d,0xc3
};

struct role_profile {
    const char *symbol;
    uint64_t slot, export_offset, raw_offset;
    const unsigned char *prefix;
    size_t prefix_length, branch_offset, return_offset;
};
static const struct role_profile roles[BW_NATIVE_ROLE_COUNT] = {
    {"sceKernelAioSubmitReadCommands",         0x64aa0,0x168d0,0,aio_prefix,sizeof aio_prefix,14,14},
    {"sceKernelAioSubmitReadCommandsMultiple", 0x64aa8,0x16910,0,aio_prefix,sizeof aio_prefix,14,14},
    {"sceKernelAioPollRequest",                0x64ab0,0x167b0,0,poll_prefix,sizeof poll_prefix,19,16},
    {"pread",  0x64a80,0x12910,0x1820,pread_stub,sizeof pread_stub,0,0},
    {"preadv", 0x64a88,0x011c0,0,preadv_prefix,sizeof preadv_prefix,10,10},
    {"read",   0x64a90,0x0e370,0,read_prefix,sizeof read_prefix,14,14},
    {"readv",  0x64a98,0x0e3d0,0,read_prefix,sizeof read_prefix,14,14},
    {"lseek",  0x64a68,0x128f0,0x1880,lseek_stub,sizeof lseek_stub,0,0},
    {"close",  0x64a58,0x0dc40,0,close_prefix,sizeof close_prefix,21,21},
    {"fstat",  0x64a60,0x00e60,0,fstat_prefix,sizeof fstat_prefix,10,10},
    {"open",   0x64850,0x0e020,0,open_prefix,sizeof open_prefix,20,20},
    {"__error",0,0x02e00,0x02e00,errno_body,sizeof errno_body,0,0},
};

static int valid_role(enum bw_native_role role) {
    return role >= BW_NATIVE_AIO_SINGLE && role < BW_NATIVE_ROLE_COUNT;
}
static int user_range(uint64_t address, uint64_t n) {
    return n && address >= USER_MIN && address < USER_END && n <= USER_END-address;
}
static int ampr_range(uint64_t base, uint64_t address, size_t n) {
    return address >= base && address-base < BW_NATIVE_AMPR_SPAN &&
        n <= BW_NATIVE_AMPR_SPAN-(address-base);
}
static uint64_t little64(const unsigned char *p) {
    uint64_t value = 0;
    for (unsigned i=0;i<8;i++) value |= (uint64_t)p[i] << (8*i);
    return value;
}

const char *bw_native_symbol(enum bw_native_role role) {
    return valid_role(role) ? roles[role].symbol : NULL;
}
uint64_t bw_native_slot_offset(enum bw_native_role role) {
    return valid_role(role) ? roles[role].slot : 0;
}
const char *bw_native_error_text(enum bw_native_error error) {
    switch (error) {
    case BW_NATIVE_OK: return "Inspected native profile matches";
    case BW_NATIVE_INVALID: return "Invalid native profile arguments";
    case BW_NATIVE_MODULE: return "Native module is not the inspected libkernel.sprx";
    case BW_NATIVE_READ_FAILED: return "Native profile memory read failed";
    case BW_NATIVE_LAYOUT: return "Ampr instruction profile differs";
    case BW_NATIVE_SYMBOL: return "Resolved native symbol differs from requested role";
    case BW_NATIVE_EXPORT: return "Native export is not at the inspected relative offset";
    case BW_NATIVE_RANGE: return "Native target is outside its inspected address range";
    case BW_NATIVE_PREFIX: return "Native target instruction fingerprint differs";
    case BW_NATIVE_JUMP: return "Native trampoline has a different branch or continuation";
    case BW_NATIVE_NOT_READY: return "Ampr hook installation is incomplete, locked or rolled back";
    case BW_NATIVE_CHANGED: return "Ampr hook state changed during validation";
    case BW_NATIVE_STATE_MISMATCH: return "Ampr completed hook state has an unexpected tuple";
    default: return "Unknown native profile error";
    }
}

enum bw_native_error bw_native_check_ampr(const struct bw_native_profile *p) {
    unsigned char bytes[17];
    if (!p || !p->read || !user_range(p->ampr_base,BW_NATIVE_AMPR_SPAN))
        return BW_NATIVE_INVALID;
    for (size_t i=0;i<sizeof windows/sizeof windows[0];i++) {
        if (p->read(p->context,p->ampr_base+windows[i].offset,bytes,windows[i].length))
            return BW_NATIVE_READ_FAILED;
        if (memcmp(bytes,windows[i].bytes,windows[i].length)) return BW_NATIVE_LAYOUT;
    }
    return BW_NATIVE_OK;
}

enum bw_native_error bw_native_check_target(const struct bw_native_profile *p,
    enum bw_native_role role, const char *symbol, uint64_t entry, uint64_t target) {
    unsigned char bytes[sizeof errno_body]; /* Complete104-byte helper or <=35-byte bridge. */
    enum bw_native_error error;
    const struct role_profile *r;
    size_t n;
    if (!p || !p->read || !valid_role(role) || !symbol ||
        !user_range(p->libkernel_base,UINT64_C(0x100000))) return BW_NATIVE_INVALID;
    if (!p->libkernel_module || strcmp(p->libkernel_module,"libkernel.sprx"))
        return BW_NATIVE_MODULE;
    r = &roles[role];
    if (strcmp(symbol,r->symbol)) return BW_NATIVE_SYMBOL;
    if (entry != p->libkernel_base+r->export_offset) return BW_NATIVE_EXPORT;
    error = bw_native_check_ampr(p);
    if (error) return error;
    if (r->raw_offset) {
        if (target != p->libkernel_base+r->raw_offset) return BW_NATIVE_RANGE;
        n = r->prefix_length;
    } else {
        n = r->branch_offset+14;
        if (!ampr_range(p->ampr_base,target,n)) return BW_NATIVE_RANGE;
    }
    if (p->read(p->context,target,bytes,n)) return BW_NATIVE_READ_FAILED;
    if (memcmp(bytes,r->prefix,r->prefix_length)) return BW_NATIVE_PREFIX;
    if (r->raw_offset) return BW_NATIVE_OK;
    /* The poll bridge expanded a RIP-relative instruction to movabs rbx.
     * Its embedded pointer is base-relative libkernel data, not arbitrary. */
    if (role == BW_NATIVE_AIO_POLL && little64(bytes+11) != p->libkernel_base+0x751d0)
        return BW_NATIVE_PREFIX;
    if (memcmp(bytes+r->branch_offset,abs_jump,sizeof abs_jump) ||
        little64(bytes+r->branch_offset+6) != entry+r->return_offset)
        return BW_NATIVE_JUMP;
    return BW_NATIVE_OK;
}


static int retained_data_read(const struct bw_native_profile *p,uint64_t offset,
                              void *dest,size_t length) {
    if (offset<AMPR_RW_FIRST || offset>=AMPR_RW_END || length>AMPR_RW_END-offset)
        return -1;
    return p->read(p->context,p->ampr_base+offset,dest,length);
}
static uint32_t little32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static enum bw_native_error aio_state_read(const struct bw_native_profile *p,
                                           struct bw_native_aio_state *state) {
    unsigned char words[8],pointer[8],flag;
    memset(state,0,sizeof *state);
    /* Adjacent uint32 lock/completion words are read together; no host struct
     * padding or assumed SDK stat layout is copied from the selected process. */
    if (retained_data_read(p,AIO_LOCK,words,sizeof words) ||
        retained_data_read(p,AIO_TRAMPOLINE,pointer,sizeof pointer)) return BW_NATIVE_READ_FAILED;
    state->lock=little32(words); state->hooks_installed=little32(words+4);
    state->trampoline=little64(pointer);
    if (retained_data_read(p,AIO_INSTALLED,&flag,sizeof flag) ||
        retained_data_read(p,AIO_ORIGINAL,pointer,sizeof pointer)) return BW_NATIVE_READ_FAILED;
    state->multiple_installed=flag; state->original_slot=little64(pointer);
    return BW_NATIVE_OK;
}
static int aio_state_equal(const struct bw_native_aio_state *a,const struct bw_native_aio_state *b) {
    return a->lock==b->lock && a->hooks_installed==b->hooks_installed &&
        a->trampoline==b->trampoline && a->original_slot==b->original_slot &&
        a->multiple_installed==b->multiple_installed;
}
enum bw_native_error bw_native_check_aio_ready(const struct bw_native_profile *p,
    uint64_t entry,uint64_t expected_native,uint64_t expected_hook,struct bw_native_aio_state *out) {
    struct bw_native_aio_state before,after;
    unsigned char bytes[378]; enum bw_native_error error,decision;
    if (out) memset(out,0,sizeof *out);
    if (!p || !p->read || !out || !user_range(p->libkernel_base,UINT64_C(0x100000)) ||
        (expected_native && !user_range(expected_native,1)) ||
        (expected_hook && (!expected_native || !user_range(expected_hook,1) || expected_hook==expected_native)))
        return BW_NATIVE_INVALID;
    if (!p->libkernel_module || strcmp(p->libkernel_module,"libkernel.sprx")) return BW_NATIVE_MODULE;
    if (entry!=p->libkernel_base+roles[BW_NATIVE_AIO_MULTIPLE].export_offset) return BW_NATIVE_EXPORT;
    error=bw_native_check_ampr(p);
    if (error) return error;
    for (size_t i=0;i<sizeof runtime_windows/sizeof runtime_windows[0];i++) {
        const struct runtime_window *w=&runtime_windows[i];
        /* Every admitted code window lies wholly inside retained executable
         * PT_LOAD [0,0x4f0e2), as independently checked against the ELF. */
        if (w->offset>=UINT64_C(0x4f0e2) || w->length>UINT64_C(0x4f0e2)-w->offset)
            return BW_NATIVE_RANGE;
        if (p->read(p->context,p->ampr_base+w->offset,bytes,w->length)) return BW_NATIVE_READ_FAILED;
        if (memcmp(bytes,w->bytes,w->length)) return BW_NATIVE_LAYOUT;
    }
    error=aio_state_read(p,&before);
    if (error) return error;
    *out=before;
    if (before.lock>1 || before.hooks_installed>1 || before.multiple_installed>1)
        decision=BW_NATIVE_STATE_MISMATCH;
    else if (before.lock || !before.hooks_installed || !before.multiple_installed)
        decision=BW_NATIVE_NOT_READY;
    else if (!before.trampoline || (expected_native && before.trampoline!=expected_native) ||
             before.original_slot!=(expected_hook?expected_hook:before.trampoline))
        decision=BW_NATIVE_STATE_MISMATCH;
    else
        decision=bw_native_check_target(p,BW_NATIVE_AIO_MULTIPLE,
            roles[BW_NATIVE_AIO_MULTIPLE].symbol,entry,before.trampoline);
    error=aio_state_read(p,&after);
    if (error) return error;
    *out=after;
    if (!aio_state_equal(&before,&after)) return BW_NATIVE_CHANGED;
    return decision;
}
