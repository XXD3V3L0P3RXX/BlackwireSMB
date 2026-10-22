#include "source_directory.h"
#include <string.h>

static uint16_t get16(const uint8_t *p){return (uint16_t)p[0]|(uint16_t)p[1]<<8;}
static uint32_t get32(const uint8_t *p){uint32_t n=0;for(unsigned i=0;i<4;i++)n|=(uint32_t)p[i]<<(i*8);return n;}
static uint64_t get64(const uint8_t *p){uint64_t n=0;for(unsigned i=0;i<8;i++)n|=(uint64_t)p[i]<<(i*8);return n;}
uint64_t bw_source_unix_mtime(uint64_t ticks){
    const uint64_t epoch=UINT64_C(116444736000000000);
    return ticks<epoch?0:(ticks-epoch)/UINT64_C(10000000);
}
static int name_utf8(char out[BW_SOURCE_NAME_BYTES+1],const uint8_t *wire,size_t bytes){
    if(!bytes||(bytes&1)||bytes>510)return -1;
    size_t at=0;
    for(size_t i=0;i<bytes;i+=2){
        uint32_t c=get16(wire+i);
        if(c>=0xd800&&c<=0xdbff){
            if(i+3>=bytes)return -1;
            uint32_t low=get16(wire+i+2);if(low<0xdc00||low>0xdfff)return -1;
            c=0x10000+((c-0xd800)<<10)+(low-0xdc00);i+=2;
        }else if(c>=0xdc00&&c<=0xdfff)return -1;
        if(!c)return -1;
        uint8_t encoded[4];size_t count;
        if(c<0x80){encoded[0]=(uint8_t)c;count=1;}
        else if(c<0x800){encoded[0]=(uint8_t)(0xc0|(c>>6));encoded[1]=(uint8_t)(0x80|(c&63));count=2;}
        else if(c<0x10000){encoded[0]=(uint8_t)(0xe0|(c>>12));encoded[1]=(uint8_t)(0x80|((c>>6)&63));encoded[2]=(uint8_t)(0x80|(c&63));count=3;}
        else {encoded[0]=(uint8_t)(0xf0|(c>>18));encoded[1]=(uint8_t)(0x80|((c>>12)&63));encoded[2]=(uint8_t)(0x80|((c>>6)&63));encoded[3]=(uint8_t)(0x80|(c&63));count=4;}
        if(count>BW_SOURCE_NAME_BYTES-at)return -1;
        memcpy(out+at,encoded,count);at+=count;
    }
    out[at]=0;return 0;
}
int bw_source_directory_page(const void *data,size_t size,bw_source_visit_fn visit,void *context){
    if(!data||!visit||size<80||size>BW_SOURCE_DIRECTORY_PAGE)return -1;
    const uint8_t *bytes=data;size_t at=0;
    for(;;){
        size_t remaining=size-at;if(remaining<80)return -1;
        const uint8_t *row=bytes+at;uint32_t next=get32(row),name_bytes=get32(row+60);
        if(name_bytes>remaining-80||name_bytes>510)return -1;
        size_t used=80u+name_bytes;
        if(next&&((next&7)||next<used||next>remaining||remaining-next<80))return -1;
        if(!next&&remaining-used>7)return -1;
        char name[BW_SOURCE_NAME_BYTES+1];
        if(name_utf8(name,row+80,name_bytes))return -1;
        struct bw_source_info info={get64(row+40),get64(row+72),get64(row+24),get64(row+32),get32(row+56)};
        if(visit(context,name,&info))return -1;
        if(!next)return 0;
        at+=next;
    }
}
