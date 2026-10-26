/* Offline actual libsmb2 CMAC tests. RFC 4493 section 4 is the fixed oracle:
 * https://www.rfc-editor.org/rfc/rfc4493#section-4
 * Synthetic keys/messages only; no networking, credentials or console state. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "aes.h"
void smb3_aes_cmac_128(uint8_t *,uint8_t *,uint64_t,uint8_t *);
void __real_AES128_init_ctx(struct AES128_ctx *,const uint8_t *);
void __real_AES128_ECB_encrypt(uint8_t *,const uint8_t *,uint8_t *);
static uint64_t schedules,legacy_encrypts;
static unsigned checks,cases;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"CMAC schedule test line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void __wrap_AES128_init_ctx(struct AES128_ctx *p,const uint8_t *key){schedules++;__real_AES128_init_ctx(p,key);}
void __wrap_AES128_ECB_encrypt(uint8_t *in,const uint8_t *key,uint8_t *out){legacy_encrypts++;__real_AES128_ECB_encrypt(in,key,out);}
static void hex(const char *text,uint8_t *out,size_t size){
    C(strlen(text)==size*2);for(size_t i=0;i<size;i++){unsigned value;C(sscanf(text+2*i,"%2x",&value)==1);out[i]=(uint8_t)value;}
}
static void shift(const uint8_t in[16],uint8_t out[16]){
    unsigned carry=0;for(int i=15;i>=0;i--){unsigned next=in[i]>>7;out[i]=(uint8_t)((in[i]<<1)|carry);carry=next;}if(carry)out[15]^=0x87;
}
/* Reference keeps the former per-block ECB entry point (and therefore a full
 * key expansion per call), separate from the production CMAC implementation. */
static void legacy_reference(const uint8_t key[16],const uint8_t *message,size_t size,uint8_t mac[16]){
    uint8_t zero[16]={0},l[16],k1[16],k2[16],state[16]={0},last[16]={0};
    AES128_ECB_encrypt(zero,key,l);shift(l,k1);shift(k1,k2);
    size_t blocks=size?(size-1)/16+1:1;
    for(size_t i=0;i<blocks-1;i++){
        for(unsigned j=0;j<16;j++)state[j]^=message[i*16+j];
        AES128_ECB_encrypt(state,key,state);
    }
    if(size&&size%16==0){memcpy(last,message+(blocks-1)*16,16);for(unsigned j=0;j<16;j++)last[j]^=k1[j];}
    else{size_t left=size%16;if(left)memcpy(last,message+(blocks-1)*16,left);last[left]=0x80;for(unsigned j=0;j<16;j++)last[j]^=k2[j];}
    for(unsigned j=0;j<16;j++)state[j]^=last[j];AES128_ECB_encrypt(state,key,mac);
}
static void compare(uint8_t key[16],uint8_t *message,size_t size){
    uint8_t reference[16],actual[16],saved_key[16];memcpy(saved_key,key,16);
    uint64_t old=legacy_encrypts,initialized=schedules;legacy_reference(key,message,size,reference);
    uint64_t expected_blocks=size?(size-1)/16+1:1;
    C(legacy_encrypts-old==expected_blocks+1&&schedules==initialized);
    initialized=schedules;old=legacy_encrypts;smb3_aes_cmac_128(key,message,size,actual);
    C(schedules-initialized==1&&legacy_encrypts==old&&!memcmp(reference,actual,16)&&!memcmp(key,saved_key,16));cases++;
}
int main(void){
    uint8_t key[16],message[64],expected[16],actual[16];
    hex("2b7e151628aed2a6abf7158809cf4f3c",key,16);
    hex("6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e5130c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710",message,64);
    const size_t sizes[]={0,16,40,64};
    const char *macs[]={"bb1d6929e95937287fa37d129b756746","070a16b46b4d4144f79bdd9dd04a287c","dfa66747de9ae63030ca32611497c827","51f0bebf7e3b9d92fc49741779363cfe"};
    for(unsigned i=0;i<4;i++){hex(macs[i],expected,16);smb3_aes_cmac_128(key,message,sizes[i],actual);C(!memcmp(actual,expected,16));compare(key,message,sizes[i]);}
    hex(macs[0],expected,16);smb3_aes_cmac_128(key,NULL,0,actual);C(!memcmp(actual,expected,16));
    /* Compatibility ECB plus independently keyed immutable contexts. Alternated
     * calls expose a static last-key cache or accidental schedule mutation. */
    struct AES128_ctx a,b,as,bs;uint8_t other_key[16],left[16],right[16],decoded[16];
    for(unsigned i=0;i<16;i++)other_key[i]=(uint8_t)(key[i]^0xa5);
    AES128_init_ctx(&a,key);AES128_init_ctx(&b,other_key);as=a;bs=b;
    unsigned hardware=a.use_aesni;
#if defined(BLACKWIRE_AES_TEST_FORCE_SOFTWARE) && BLACKWIRE_AES_TEST_FORCE_SOFTWARE
    C(!a.use_aesni&&!b.use_aesni);
#endif
    for(unsigned i=0;i<64;i++){
        uint8_t *input=message+(i%4)*16;
        AES128_ECB_encrypt_ctx(&a,input,left);AES128_ECB_encrypt(input,key,right);C(!memcmp(left,right,16));
        AES128_ECB_decrypt(left,key,decoded);C(!memcmp(decoded,input,16));
        AES128_ECB_encrypt_ctx(&b,input,left);AES128_ECB_encrypt(input,other_key,right);C(!memcmp(left,right,16));
        memcpy(left,input,16);AES128_ECB_encrypt_ctx(&a,left,left);AES128_ECB_encrypt(input,key,right);C(!memcmp(left,right,16));
    }
    C(!memcmp(&a,&as,sizeof a)&&!memcmp(&b,&bs,sizeof b));
    uint8_t unaligned_context[sizeof(struct AES128_ctx)+1],unaligned_input[17],unaligned_output[17];
    struct AES128_ctx *unaligned=(struct AES128_ctx *)(void *)(unaligned_context+1);
    memcpy(unaligned_input+1,message,16);AES128_init_ctx(unaligned,key);
    AES128_ECB_encrypt_ctx(unaligned,unaligned_input+1,unaligned_output+1);
    AES128_ECB_encrypt(message,key,right);C(!memcmp(unaligned_output+1,right,16));
    /* Preserve the old wrapper's unusual but defined key/output alias order:
     * its input copy happens before it expands the key at that same address. */
    memcpy(left,key,16);AES128_ECB_encrypt(message,left,left);AES128_init_ctx(&a,message);AES128_ECB_encrypt_ctx(&a,message,right);C(!memcmp(left,right,16));
    const size_t lengths[]={1,2,15,17,31,32,33,63,65,255,256,257,4095,4096,4097,65535,65536,65537,1048575,1048576,1048577};
    uint8_t *body=malloc(1048578);C(body);uint32_t random=0x6d4c3b2a;
    for(unsigned round=0;round<4;round++){
        for(size_t i=0;i<1048578;i++){random^=random<<13;random^=random>>17;random^=random<<5;body[i]=(uint8_t)random;}
        for(unsigned i=0;i<16;i++)key[i]=body[i*7+round];
        for(unsigned i=0;i<sizeof lengths/sizeof *lengths;i++)compare(key,body+(round&1),lengths[i]);
        for(unsigned i=0;i<32;i++)compare(key,body,(i*193u+round*17u)%8193u);
    }
    uint64_t initialized=schedules,old=legacy_encrypts;legacy_reference(key,body,1048576,expected);
    uint64_t old_expansions=legacy_encrypts-old;old=legacy_encrypts;smb3_aes_cmac_128(key,body,1048576,actual);
    C(old_expansions==65537&&schedules-initialized==1&&legacy_encrypts==old&&!memcmp(expected,actual,16));
    free(body);printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"rfc4493Vectors\":4,\"oneMiB\":{\"previousKeyExpansions\":65537,\"currentKeyExpansions\":1},\"hardwareEncryptionUsed\":%s,\"separateContextsImmutable\":true,\"compatibilityWrappers\":true,\"signingPolicyUnchanged\":true,\"consoleContacted\":false}\n",cases,checks,hardware?"true":"false");return 0;
}
