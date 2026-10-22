#ifndef BW_PACKED_SHA1_H
#define BW_PACKED_SHA1_H
/* Original portable SHA-1 integrity helper; FIPS 180-4 arithmetic. This is not
 * a signature, publisher authentication, or source ownership mechanism. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct pp_sha1 {uint32_t h[5];uint64_t bytes;size_t used;uint8_t block[64];};
static inline uint32_t pp_rol(uint32_t x,unsigned n){return (x<<n)|(x>>(32u-n));}
static inline void pp_sha1_block(struct pp_sha1 *s,const uint8_t *b){
    uint32_t w[80];for(unsigned i=0;i<16;i++)w[i]=(uint32_t)b[4*i]<<24|(uint32_t)b[4*i+1]<<16|(uint32_t)b[4*i+2]<<8|b[4*i+3];
    for(unsigned i=16;i<80;i++)w[i]=pp_rol(w[i-3]^w[i-8]^w[i-14]^w[i-16],1);
    uint32_t a=s->h[0],c=s->h[2],d=s->h[3],e=s->h[4],bb=s->h[1];
    for(unsigned i=0;i<80;i++){
        uint32_t f=i<20?((bb&c)|(~bb&d)):i<40?(bb^c^d):i<60?((bb&c)|(bb&d)|(c&d)):(bb^c^d);
        uint32_t k=i<20?UINT32_C(0x5a827999):i<40?UINT32_C(0x6ed9eba1):i<60?UINT32_C(0x8f1bbcdc):UINT32_C(0xca62c1d6);
        uint32_t t=pp_rol(a,5)+f+e+k+w[i];e=d;d=c;c=pp_rol(bb,30);bb=a;a=t;
    }
    s->h[0]+=a;s->h[1]+=bb;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;
}
static inline void pp_sha1_init(struct pp_sha1 *s){memset(s,0,sizeof *s);s->h[0]=UINT32_C(0x67452301);s->h[1]=UINT32_C(0xefcdab89);s->h[2]=UINT32_C(0x98badcfe);s->h[3]=UINT32_C(0x10325476);s->h[4]=UINT32_C(0xc3d2e1f0);}
static inline void pp_sha1_update(struct pp_sha1 *s,const void *raw,size_t n){
    const uint8_t *p=raw;s->bytes+=(uint64_t)n;
    while(n){size_t z=64-s->used;if(z>n)z=n;memcpy(s->block+s->used,p,z);s->used+=z;p+=z;n-=z;if(s->used==64){pp_sha1_block(s,s->block);s->used=0;}}
}
static inline void pp_sha1_final(struct pp_sha1 *s,uint8_t out[20]){
    uint64_t bits=s->bytes*8;uint8_t one=128,zero=0;pp_sha1_update(s,&one,1);while(s->used!=56)pp_sha1_update(s,&zero,1);
    uint8_t tail[8];for(unsigned i=0;i<8;i++)tail[7-i]=(uint8_t)(bits>>(8*i));pp_sha1_update(s,tail,8);
    for(unsigned i=0;i<5;i++)for(unsigned j=0;j<4;j++)out[4*i+j]=(uint8_t)(s->h[i]>>(24-8*j));memset(s,0,sizeof *s);
}
static inline void pp_sha1_bytes(const void *raw,size_t n,uint8_t out[20]){struct pp_sha1 s;pp_sha1_init(&s);pp_sha1_update(&s,raw,n);pp_sha1_final(&s,out);}
#endif
