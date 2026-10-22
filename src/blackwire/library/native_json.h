#ifndef BW_NATIVE_JSON_H
#define BW_NATIVE_JSON_H
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct bw_json {char *bytes;size_t used,capacity,maximum;int bad;};
static inline int bw_json_utf8(const char *text){
    const unsigned char *p=(const unsigned char *)text;
    while(*p){uint32_t c=*p++;if(c<128)continue;unsigned n;uint32_t minimum;
        if(c>=0xc2&&c<=0xdf){n=1;minimum=0x80;c&=31;}else if(c>=0xe0&&c<=0xef){n=2;minimum=0x800;c&=15;}
        else if(c>=0xf0&&c<=0xf4){n=3;minimum=0x10000;c&=7;}else return 0;
        while(n--){if((*p&0xc0)!=0x80)return 0;c=(c<<6)|(*p++&63);}if(c<minimum||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return 0;
    }return 1;
}
static inline void bw_json_append(struct bw_json *p,const char *bytes,size_t size){
    if(p->bad)return;if(!p->maximum||p->used>=p->maximum||size>p->maximum-p->used-1){p->bad=1;return;}
    size_t need=p->used+size+1;if(need>p->capacity){size_t next=p->capacity?p->capacity:4096;
        if(next>p->maximum)next=p->maximum;while(next<need)next=next>p->maximum/2?p->maximum:next*2;
        char *memory=realloc(p->bytes,next);if(!memory){p->bad=1;return;}p->bytes=memory;p->capacity=next;}
    memcpy(p->bytes+p->used,bytes,size);p->used+=size;p->bytes[p->used]=0;
}
static inline void bw_json_text(struct bw_json *p,const char *text){bw_json_append(p,text,strlen(text));}
static inline void bw_json_format(struct bw_json *p,const char *format,...){
    char text[1024];va_list ap;va_start(ap,format);int n=vsnprintf(text,sizeof text,format,ap);va_end(ap);
    if(n<0||(size_t)n>=sizeof text){p->bad=1;return;}bw_json_append(p,text,(size_t)n);
}
static inline void bw_json_string(struct bw_json *p,const char *text){
    if(!bw_json_utf8(text)){p->bad=1;return;}bw_json_text(p,"\"");
    for(const unsigned char *s=(const unsigned char *)text;*s;s++){
        if(*s=='"'||*s=='\\')bw_json_format(p,"\\%c",*s);else if(*s<32)bw_json_format(p,"\\u%04x",*s);else bw_json_append(p,(const char *)s,1);
    }bw_json_text(p,"\"");
}
#endif
