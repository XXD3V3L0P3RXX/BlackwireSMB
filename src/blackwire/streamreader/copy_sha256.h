#ifndef BW_COPY_SHA256_H
#define BW_COPY_SHA256_H
/* Portable SHA-256 (FIPS 180-4). No allocation or platform crypto dependency. */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
struct cp_sha { uint32_t h[8]; uint64_t length; unsigned used; uint8_t block[64]; };
static inline uint32_t cp_rotr(uint32_t x,unsigned n) { return (x>>n)|(x<<(32-n)); }
static inline void cp_sha_block(struct cp_sha *s,const uint8_t *b) {
  static const uint32_t k[64]={
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  uint32_t w[64],a=s->h[0],c=s->h[2],d=s->h[3],e=s->h[4],f=s->h[5],g=s->h[6],h=s->h[7],bb=s->h[1];
  for (unsigned i=0;i<16;i++) w[i]=(uint32_t)b[4*i]<<24|(uint32_t)b[4*i+1]<<16|(uint32_t)b[4*i+2]<<8|b[4*i+3];
  for (unsigned i=16;i<64;i++) {
    uint32_t x=w[i-15],y=w[i-2];
    w[i]=w[i-16]+(cp_rotr(x,7)^cp_rotr(x,18)^(x>>3))+w[i-7]+(cp_rotr(y,17)^cp_rotr(y,19)^(y>>10));
  }
  for (unsigned i=0;i<64;i++) {
    uint32_t t=h+(cp_rotr(e,6)^cp_rotr(e,11)^cp_rotr(e,25))+((e&f)^(~e&g))+k[i]+w[i];
    uint32_t u=(cp_rotr(a,2)^cp_rotr(a,13)^cp_rotr(a,22))+((a&bb)^(a&c)^(bb&c));
    h=g;g=f;f=e;e=d+t;d=c;c=bb;bb=a;a=t+u;
  }
  s->h[0]+=a;s->h[1]+=bb;s->h[2]+=c;s->h[3]+=d;s->h[4]+=e;s->h[5]+=f;s->h[6]+=g;s->h[7]+=h;
}
static inline void cp_sha_init(struct cp_sha *s) {
  static const uint32_t init[8]={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
  memset(s,0,sizeof *s);memcpy(s->h,init,sizeof init);
}
static inline void cp_sha_update(struct cp_sha *s,const void *data,size_t n) {
  const uint8_t *b=data; s->length+=(uint64_t)n;
  while (n) { size_t take=64-s->used;if(take>n)take=n;memcpy(s->block+s->used,b,take);s->used+=(unsigned)take;b+=take;n-=take;
    if(s->used==64){cp_sha_block(s,s->block);s->used=0;}
  }
}
static inline void cp_sha_final(struct cp_sha *s,uint8_t out[32]) {
  uint64_t bits=s->length*8;uint8_t one=128,zero=0;cp_sha_update(s,&one,1);
  while(s->used!=56)cp_sha_update(s,&zero,1);
  uint8_t tail[8];for(unsigned i=0;i<8;i++)tail[7-i]=(uint8_t)(bits>>(8*i));cp_sha_update(s,tail,8);
  for(unsigned i=0;i<8;i++)for(unsigned j=0;j<4;j++)out[4*i+j]=(uint8_t)(s->h[i]>>(24-8*j));
  memset(s,0,sizeof *s);
}
static inline void cp_sha_bytes(const void *data,size_t n,uint8_t out[32]) {
  struct cp_sha s;cp_sha_init(&s);cp_sha_update(&s,data,n);cp_sha_final(&s,out);
}
#endif
