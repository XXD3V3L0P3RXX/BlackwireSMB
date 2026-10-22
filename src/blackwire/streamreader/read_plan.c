#include "read_plan.h"
#include <string.h>

static enum bw_read_error bad(struct bw_read_plan *out,enum bw_read_error error) {
    if (out) memset(out,0,sizeof *out);
    return error;
}
static int address_range(uint64_t address,uint64_t length) {
    return !length || (address && length<=UINT64_MAX-address);
}
static uint64_t aligned(uint64_t bytes) {
    return (bytes+BW_READ_STAGE_ALIGNMENT-1)&~(BW_READ_STAGE_ALIGNMENT-1);
}
static int file_info(bw_read_resolve resolve,void *context,int32_t fd,struct bw_read_file *file) {
    memset(file,0,sizeof *file);
    int result=resolve(context,fd,file);
    return result==0?0:(result==1 && file->size<=INT64_MAX?1:-1);
}
static int overlap(uint64_t a,uint64_t b) { return a<b+16 && b<a+16; }
enum bw_read_error bw_read_plan_aio(struct bw_read_plan *out,enum bw_read_kind kind,
                                    const struct bw_read_aio *input,uint32_t count,
                                    int32_t stage_fd,bw_read_resolve resolve,void *context) {
    struct bw_read_plan plan;
    if (!out || !input || !resolve || !count || count>BW_READ_MAX_ITEMS ||
        (kind!=BW_READ_AIO_MULTIPLE && kind!=BW_READ_AIO_SINGLE)) return bad(out,BW_READ_INVALID);
    memset(&plan,0,sizeof plan); plan.kind=kind; plan.count=count;
    plan.native_id_count=kind==BW_READ_AIO_SINGLE?1:count;
    memcpy(plan.aio,input,(size_t)count*sizeof *input);
    uint64_t cursor=0;
    for (uint32_t i=0;i<count;i++) {
        struct bw_read_file file;
        int stream=file_info(resolve,context,input[i].fd,&file);
        if (stream<0) return bad(out,BW_READ_IDENTITY);
        if (!stream) continue;
        if (input[i].offset<0) return bad(out,BW_READ_INVALID);
        if (input[i].length>BW_READ_MAX_BYTES) return bad(out,BW_READ_LIMIT);
        uint64_t at=(uint64_t)input[i].offset;
        uint64_t length=at>=file.size?0:file.size-at;
        if (length>input[i].length) length=input[i].length;
        if (!length) continue;
        if (stage_fd<0) return bad(out,BW_READ_INVALID);
        if (!address_range(input[i].buffer,length)) return bad(out,BW_READ_OVERFLOW);
        if (cursor>BW_READ_STAGE_BYTES || length>BW_READ_STAGE_BYTES-cursor) return bad(out,BW_READ_LIMIT);
        plan.pieces[plan.piece_count++]=(struct bw_read_piece){file.file_id,i,at,length,cursor};
        plan.aio[i].fd=stage_fd; plan.aio[i].offset=(int64_t)cursor; plan.aio[i].length=length;
        cursor=aligned(cursor+length);
    }
    if (plan.piece_count) {
        for (uint32_t i=0;i<count;i++) {
            if (!address_range(input[i].result,16)) return bad(out,BW_READ_INVALID);
            for (uint32_t j=0;j<i;j++) if (overlap(input[i].result,input[j].result)) return bad(out,BW_READ_RESULT_ALIAS);
        }
    }
    plan.stage_bytes=cursor; plan.forward_unchanged=!plan.piece_count;
    *out=plan; return BW_READ_OK;
}
enum bw_read_error bw_read_plan_pread(struct bw_read_plan *out,int32_t fd,uint64_t buffer,
                                      uint64_t length,int64_t offset,int32_t stage_fd,
                                      bw_read_resolve resolve,void *context) {
    struct bw_read_plan plan; struct bw_read_file file;
    if (!out || !resolve) return bad(out,BW_READ_INVALID);
    memset(&plan,0,sizeof plan); plan.kind=BW_READ_PREAD; plan.count=1; plan.forward_unchanged=1;
    plan.native_fd=fd; plan.native_offset=offset; plan.native_buffer=buffer; plan.native_length=length;
    int stream=file_info(resolve,context,fd,&file);
    if (stream<0) return bad(out,BW_READ_IDENTITY);
    if (stream) {
        if (offset<0) return bad(out,BW_READ_INVALID);
        if (length>BW_READ_MAX_BYTES) return bad(out,BW_READ_LIMIT);
        uint64_t at=(uint64_t)offset,actual=at>=file.size?0:file.size-at;
        if (actual>length) actual=length;
        if (actual) {
            if (stage_fd<0) return bad(out,BW_READ_INVALID);
            if (!address_range(buffer,actual)) return bad(out,BW_READ_OVERFLOW);
            plan.pieces[0]=(struct bw_read_piece){file.file_id,0,at,actual,0};
            plan.piece_count=1; plan.forward_unchanged=0;
            plan.native_fd=stage_fd; plan.native_offset=0; plan.native_length=actual;
            plan.stage_bytes=aligned(actual);
        }
    }
    *out=plan; return BW_READ_OK;
}
enum bw_read_error bw_read_plan_preadv(struct bw_read_plan *out,int32_t fd,
                                       const struct bw_read_iovec *input,uint32_t count,
                                       int64_t offset,int32_t stage_fd,
                                       bw_read_resolve resolve,void *context) {
    struct bw_read_plan plan; struct bw_read_file file;
    if (!out || !resolve || !input || !count || count>BW_READ_MAX_IOV) return bad(out,BW_READ_INVALID);
    memset(&plan,0,sizeof plan); plan.kind=BW_READ_PREADV; plan.count=count; plan.forward_unchanged=1;
    plan.native_fd=fd; plan.native_offset=offset;
    memcpy(plan.iov,input,(size_t)count*sizeof *input);
    int stream=file_info(resolve,context,fd,&file);
    if (stream<0) return bad(out,BW_READ_IDENTITY);
    if (!stream) { *out=plan; return BW_READ_OK; }
    if (offset<0) return bad(out,BW_READ_INVALID);
    uint64_t total=0;
    for (uint32_t i=0;i<count;i++) {
        if (input[i].length>UINT64_MAX-total) return bad(out,BW_READ_OVERFLOW);
        total+=input[i].length;
        if (!address_range(input[i].buffer,input[i].length)) return bad(out,BW_READ_OVERFLOW);
    }
    if (total>BW_READ_MAX_BYTES) return bad(out,BW_READ_LIMIT);
    plan.native_length=total;
    uint64_t at=(uint64_t)offset,actual=at>=file.size?0:file.size-at;
    if (actual>total) actual=total;
    if (!actual) { *out=plan; return BW_READ_OK; }
    if (stage_fd<0) return bad(out,BW_READ_INVALID);
    uint64_t remaining=actual;
    for (uint32_t i=0;i<count;i++) {
        uint64_t length=input[i].length<remaining?input[i].length:remaining;
        plan.iov[i].length=length;
        remaining-=length;
    }
    plan.pieces[0]=(struct bw_read_piece){file.file_id,0,at,actual,0}; plan.piece_count=1;
    plan.native_fd=stage_fd; plan.native_offset=0; plan.native_length=actual;
    plan.stage_bytes=aligned(actual); plan.forward_unchanged=0;
    *out=plan; return BW_READ_OK;
}
const char *bw_read_error_string(enum bw_read_error error) {
    static const char *const messages[]={"ok","invalid read arguments","bounded read or staging limit exceeded",
        "read address or length overflow","file identity lookup failed","overlapping native AIO result slots are unsupported"};
    return (unsigned)error<sizeof messages/sizeof messages[0]?messages[error]:"unknown read-plan error";
}
