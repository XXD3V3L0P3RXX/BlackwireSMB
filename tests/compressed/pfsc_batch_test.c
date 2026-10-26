/* SPDX-License-Identifier: GPL-3.0-or-later
 * Real PFSC/zlib decoding with a counted read-only memory transport. */
#include "pfsc_range.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"PFSC batching line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
struct image {uint8_t *bytes;size_t size;uint64_t data_start,first,last;unsigned payload_calls;int fail_payload;};
static uint64_t le64(const uint8_t *p){uint64_t value=0;for(unsigned i=0;i<8;i++)value|=(uint64_t)p[i]<<(8*i);return value;}
static void put64(uint8_t *p,uint64_t value){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(value>>(8*i));}
static uint8_t *load(const char *path,size_t *size){
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long n=ftell(file);CHECK(n>0);*size=(size_t)n;
    CHECK(!fseek(file,0,SEEK_SET));uint8_t *bytes=malloc(*size);CHECK(bytes);CHECK(fread(bytes,1,*size,file)==*size);CHECK(!fclose(file));return bytes;
}
static int read_image(void *opaque,uint64_t at,void *out,size_t size){
    struct image *p=opaque;if(at>p->size||size>p->size-at)return -1;
    if(at>=p->data_start){p->payload_calls++;if(at<p->first)p->first=at;if(at+size>p->last)p->last=at+size;
        CHECK(size<=BW_PFSC_READAHEAD);if(p->fail_payload)return -1;}
    memcpy(out,p->bytes+at,size);return 0;
}
static struct bw_pfsc *open_image(struct image *p,size_t logical){
    struct bw_pfsc *decoder=calloc(1,sizeof *decoder);CHECK(decoder);
    CHECK(!bw_pfsc_open(decoder,read_image,p,bw_pfsc_inflate_miniz,NULL,p->size,0,p->size,logical));
    p->payload_calls=0;p->first=UINT64_MAX;p->last=0;return decoder;
}
static void compare(struct image *p,const uint8_t *original,size_t logical,uint64_t at,size_t size,unsigned calls){
    struct bw_pfsc *decoder=open_image(p,logical);uint8_t *out=malloc(size);CHECK(out);
    CHECK(!bw_pfsc_read(decoder,at,out,size));CHECK(!memcmp(out,original+at,size));
    if(p->payload_calls!=calls)fprintf(stderr,"Payload fetches: got %u expected %u (logical size %zu)\n",p->payload_calls,calls,size);
    CHECK(p->payload_calls==calls);
    uint64_t first=at/BW_PFSC_BLOCK,last=(at+size-1)/BW_PFSC_BLOCK;
    /* Transport can read complete compression blocks, but no block outside
     * the requested logical range. This catches accidental speculation. */
    CHECK(p->first==le64(p->bytes+0x400+first*8));CHECK(p->last==le64(p->bytes+0x400+(last+1)*8));
    free(out);free(decoder);
}
int main(int argc,char **argv){
    CHECK(argc==4);struct image image={0};size_t logical=0;image.bytes=load(argv[1],&image.size);uint8_t *original=load(argv[2],&logical);
    CHECK(logical==BW_PFSC_MAX_READ);image.data_start=le64(image.bytes+32);unsigned full_calls=(unsigned)strtoul(argv[3],NULL,10);CHECK(full_calls==1||full_calls==4);
    compare(&image,original,logical,0,logical,full_calls);
    compare(&image,original,logical,3*BW_PFSC_BLOCK,BW_PFSC_BLOCK,1);
    compare(&image,original,logical,17,2*BW_PFSC_BLOCK+123,1);
    compare(&image,original,logical,7*BW_PFSC_BLOCK+9,2*BW_PFSC_BLOCK,1);
    struct bw_pfsc *decoder=open_image(&image,logical);uint8_t *out=malloc(logical);CHECK(out);image.fail_payload=1;
    CHECK(bw_pfsc_read(decoder,0,out,logical)==BW_PFSC_IO);image.fail_payload=0;free(decoder);
    uint64_t second=le64(image.bytes+0x410);put64(image.bytes+0x410,le64(image.bytes+0x408));
    decoder=open_image(&image,logical);CHECK(bw_pfsc_read(decoder,0,out,logical)==BW_PFSC_FORMAT);free(decoder);put64(image.bytes+0x410,second);
    if(full_calls==1){
        uint64_t end=le64(image.bytes+0x408);CHECK(end-image.data_start<BW_PFSC_BLOCK);image.bytes[end-1]^=1;
        decoder=open_image(&image,logical);CHECK(bw_pfsc_read(decoder,0,out,logical)==BW_PFSC_CODEC);free(decoder);image.bytes[end-1]^=1;
    }
    printf("{\"passed\":true,\"checks\":%u,\"fullReadPayloadCalls\":%u,\"logicalBytes\":%zu,\"boundedFetches\":true}\n",checks,full_calls,logical);
    free(out);free(original);free(image.bytes);return 0;
}
