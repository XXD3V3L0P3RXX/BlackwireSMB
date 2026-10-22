/* PFSC range format, documented by pinned PSBrew/MkPFS and PS5-Game-Compressor
 * sources in docs/COMPRESSED-SOURCES.md. Independent bounded range layer.
 */
#include "pfsc_range.h"
#include <string.h>

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t le64(const uint8_t *p) {
    return (uint64_t)le32(p) | (uint64_t)le32(p + 4) << 32;
}
static int span(uint64_t offset, uint64_t length, uint64_t size) {
    return offset <= size && length <= size - offset;
}
static int raw(struct bw_pfsc *p, uint64_t offset, void *out, size_t size) {
    if (!span(offset, size, p->stored_size)) return BW_PFSC_RANGE;
    return p->pread(p->source, p->base + offset, out, size) ? BW_PFSC_IO : 0;
}

static int table_entry(struct bw_pfsc *p, uint64_t index, uint64_t *out) {
    uint64_t byte, page;
    size_t within;
    int result;
    if (index > p->block_count) return BW_PFSC_RANGE;
    byte = index * 8;
    page = byte / BW_PFSC_TABLE_PAGE * BW_PFSC_TABLE_PAGE;
    within = (size_t)(byte - page);
    if (!p->page_valid || p->table_page != page) {
        uint64_t left = p->table_bytes - page;
        p->page_valid = 0;
        p->page_size = left < BW_PFSC_TABLE_PAGE ? (size_t)left : BW_PFSC_TABLE_PAGE;
        result = raw(p, 0x400 + page, p->page, p->page_size);
        if (result) return result;
        p->table_page = page;
        p->page_valid = 1;
    }
    if (within > p->page_size || p->page_size - within < 8) return BW_PFSC_FORMAT;
    *out = le64(p->page + within);
    return 0;
}

int bw_pfsc_open(struct bw_pfsc *p, bw_pfsc_pread_fn read_source, void *source,
                 bw_pfsc_inflate_fn inflate, void *codec, uint64_t image_size,
                 uint64_t base, uint64_t stored, uint64_t logical) {
    uint8_t head[48];
    uint64_t padded, first, last;
    int result;
    if (!p) return BW_PFSC_ARGUMENT;
    memset(p, 0, sizeof *p);
    if (!read_source || !inflate || stored < sizeof head ||
        !span(base, stored, image_size)) return BW_PFSC_ARGUMENT;
    p->pread = read_source; p->source = source;
    p->inflate = inflate; p->codec = codec;
    p->image_size = image_size; p->base = base; p->stored_size = stored;
    result = raw(p, 0, head, sizeof head);
    if (result) return result;
    if (le32(head) != 0x43534650u || le32(head + 4) || le32(head + 8) != 6 ||
        le32(head + 12) != BW_PFSC_BLOCK || le64(head + 16) != BW_PFSC_BLOCK ||
        le64(head + 24) != 0x400) return BW_PFSC_FORMAT;
    p->data_start = le64(head + 32);
    padded = le64(head + 40);
    if (!padded || padded > BW_PFSC_MAX_LOGICAL || padded % BW_PFSC_BLOCK ||
        !logical || logical > padded || padded - logical >= BW_PFSC_BLOCK ||
        p->data_start < BW_PFSC_BLOCK || p->data_start > stored)
        return BW_PFSC_FORMAT;
    p->logical_size = logical; p->block_count = padded / BW_PFSC_BLOCK;
    p->table_bytes = (p->block_count + 1) * 8;
    if (!span(0x400, p->table_bytes, p->data_start)) return BW_PFSC_FORMAT;
    result = table_entry(p, 0, &first);
    if (result) return result;
    result = table_entry(p, p->block_count, &last);
    if (result) return result;
    if (first != p->data_start || last <= first || last > stored)
        return BW_PFSC_FORMAT;
    p->ready = 1;
    return 0;
}

static int decode(struct bw_pfsc *p, uint64_t index,uint64_t last_index) {
    uint64_t start, end;
    if (p->decoded_valid && p->decoded_index == index) return 0;
#if BW_PFSC_READAHEAD
    int sequential=p->decoded_valid&&index==p->decoded_index+1;
#endif
    p->decoded_valid = 0;
    int result = table_entry(p, index, &start);
    if (result) return result;
    result = table_entry(p, index + 1, &end);
    if (result) return result;
    if (start < p->data_start || end <= start || end > p->stored_size ||
        end - start > BW_PFSC_BLOCK) return BW_PFSC_FORMAT;
    size_t packed_size=(size_t)(end-start);
#if BW_PFSC_READAHEAD
    if(p->window_size&&start>=p->window_at&&start-p->window_at<=p->window_size&&
       packed_size<=p->window_size-(size_t)(start-p->window_at)){
        memcpy(p->packed,p->window+(size_t)(start-p->window_at),packed_size);result=0;
    }else if(sequential&&index<last_index&&packed_size<=sizeof p->window){
        p->window_size=0;
        uint64_t last=index+(sizeof p->window/BW_PFSC_BLOCK)-1,fetch_end;
        if(last>last_index)last=last_index;
        result=table_entry(p,last+1,&fetch_end);if(result)return result;
        if(fetch_end<end||fetch_end>p->stored_size||fetch_end-start>sizeof p->window)return BW_PFSC_FORMAT;
        size_t fetch=(size_t)(fetch_end-start);
        result=raw(p,start,p->window,fetch);
        if(!result){p->window_at=start;p->window_size=fetch;memcpy(p->packed,p->window,packed_size);}
    }else
#endif
    result = raw(p, start, p->packed, packed_size);
#if !BW_PFSC_READAHEAD
    (void)last_index;
#endif
    if (result) return result;
    if (end - start == BW_PFSC_BLOCK) {
        memcpy(p->decoded, p->packed, BW_PFSC_BLOCK);
    } else if (p->inflate(p->codec, p->packed, (size_t)(end - start),
                          p->decoded, BW_PFSC_BLOCK)) {
        return BW_PFSC_CODEC;
    }
    p->decoded_index = index;
    p->decoded_valid = 1;
    return 0;
}

int bw_pfsc_read(struct bw_pfsc *p, uint64_t offset, void *output, size_t size) {
    uint8_t *dest = output;
    if (!p || !p->ready || (!output && size)) return BW_PFSC_ARGUMENT;
    if (size > BW_PFSC_MAX_READ || !span(offset, size, p->logical_size))
        return BW_PFSC_RANGE;
    uint64_t last=size?(offset+size-1u)/BW_PFSC_BLOCK:0;
    while (size) {
        size_t within = (size_t)(offset % BW_PFSC_BLOCK);
        size_t take = BW_PFSC_BLOCK - within;
        int result;
        if (take > size) take = size;
        result = decode(p, offset / BW_PFSC_BLOCK,last);
        if (result) return result;
        memcpy(dest, p->decoded + within, take);
        dest += take; offset += take; size -= take;
    }
    return 0;
}
