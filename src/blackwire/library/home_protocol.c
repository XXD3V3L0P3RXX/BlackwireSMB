/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "home_protocol.h"
#include "native_document.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>
static const char digits[]="0123456789abcdef";
static int hex(const char *s,size_t n){unsigned nz=0;if(!s||!memchr(s,0,n+1)||strlen(s)!=n)return 0;for(size_t i=0;i<n;i++){if(!strchr("0123456789abcdef",s[i]))return 0;nz|=s[i]!='0';}return nz!=0;}
struct fields {struct bw_home_claim claim;char run[33];unsigned seen;int ack;};
static int field(void *opaque,const char *key,size_t n,const void *raw,size_t size){
    struct fields *f=opaque;unsigned bit=0;char text[65];if(n!=strlen(key))return -1;
    if(!strcmp(key,"protocol")){bit=1;if(bw_document_string(raw,size,text,sizeof text)||strcmp(text,BW_HOME_PROTOCOL))return -1;}
    else if(!strcmp(key,"nonce")){bit=2;if(bw_document_string(raw,size,f->claim.nonce,sizeof f->claim.nonce)||!hex(f->claim.nonce,32))return -1;}
    else if(!f->ack&&!strcmp(key,"pid")){
        bit=4;if(!size||size>10)return -1;const unsigned char *p=raw;uint32_t value=0;
        if(p[0]=='0')return -1;for(size_t i=0;i<size;i++){if(p[i]<'0'||p[i]>'9'||value>(INT32_MAX-(unsigned)(p[i]-'0'))/10)return -1;value=value*10+(unsigned)(p[i]-'0');}
        if(value<=1)return -1;f->claim.pid=(int32_t)value;
    }else if(!f->ack&&!strcmp(key,"deadlineUs")){
        bit=16;if(!size||size>19)return -1;const unsigned char *p=raw;uint64_t value=0;
        if(p[0]=='0')return -1;for(size_t i=0;i<size;i++){if(p[i]<'0'||p[i]>'9'||value>(INT64_MAX-(unsigned)(p[i]-'0'))/10)return -1;value=value*10+(unsigned)(p[i]-'0');}
        f->claim.deadline_us=value;
    }else if(!f->ack&&!strcmp(key,"mapSha256")){
        bit=8;if(bw_document_string(raw,size,text,sizeof text)||!hex(text,64))return -1;
        for(unsigned i=0;i<32;i++){unsigned a=(unsigned)(strchr(digits,text[i*2])-digits),b=(unsigned)(strchr(digits,text[i*2+1])-digits);f->claim.map_sha256[i]=(uint8_t)(a*16+b);}
    }else if(f->ack&&!strcmp(key,"run")){bit=4;if(bw_document_string(raw,size,f->run,sizeof f->run)||!hex(f->run,32))return -1;}
    else if(f->ack&&!strcmp(key,"accepted")){bit=8;if(size!=4||memcmp(raw,"true",4))return -1;}
    else return -1;
    if(f->seen&bit)return -1;f->seen|=bit;return 0;
}
int bw_home_claim_parse(struct bw_home_claim *out,const void *raw,size_t size){
    if(!out)return -1;memset(out,0,sizeof *out);struct fields f={0};
    if(!raw||!size||size>BW_HOME_REQUEST_LIMIT||bw_document_fields(raw,size,field,&f)||f.seen!=31)return -1;
    *out=f.claim;return 0;
}
int bw_home_claim_format(const struct bw_home_claim *c,char *out,size_t cap,size_t *used){
    if(used)*used=0;if(!out||!cap||!used||!c||c->pid<=1||!c->deadline_us||c->deadline_us>INT64_MAX||!hex(c->nonce,32))return -1;out[0]=0;
    char digest[65];unsigned nz=0;for(unsigned i=0;i<32;i++){digest[i*2]="0123456789abcdef"[c->map_sha256[i]>>4];digest[i*2+1]="0123456789abcdef"[c->map_sha256[i]&15];nz|=c->map_sha256[i];}digest[64]=0;if(!nz)return -1;
    int n=snprintf(out,cap,"{\"protocol\":\"%s\",\"pid\":%d,\"deadlineUs\":%llu,\"nonce\":\"%s\",\"mapSha256\":\"%s\"}",BW_HOME_PROTOCOL,c->pid,(unsigned long long)c->deadline_us,c->nonce,digest);
    if(n<0||(size_t)n>=cap||(size_t)n>BW_HOME_REQUEST_LIMIT){out[0]=0;return -1;}*used=(size_t)n;return 0;
}
int bw_home_ack_parse(const void *raw,size_t size,const char nonce[33],char run[33]){
    if(!run)return -1;memset(run,0,33);struct fields f={.ack=1};
    if(!raw||!size||size>BW_HOME_REQUEST_LIMIT||!nonce||!hex(nonce,32)||bw_document_fields(raw,size,field,&f)||f.seen!=15||memcmp(nonce,f.claim.nonce,33))return -1;
    memcpy(run,f.run,33);return 0;
}
int bw_home_ack_format(const char nonce[33],const char run[33],char *out,size_t cap,size_t *used){
    if(used)*used=0;if(!out||!cap||!used||!nonce||!run||!hex(nonce,32)||!hex(run,32))return -1;out[0]=0;
    int n=snprintf(out,cap,"{\"protocol\":\"%s\",\"accepted\":true,\"nonce\":\"%s\",\"run\":\"%s\"}",BW_HOME_PROTOCOL,nonce,run);
    if(n<0||(size_t)n>=cap){out[0]=0;return -1;}*used=(size_t)n;return 0;
}
