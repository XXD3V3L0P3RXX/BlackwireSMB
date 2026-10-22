#ifndef BW_PREPARATION_RECEIPT_H
#define BW_PREPARATION_RECEIPT_H
/* Versioned bytes shared with desk/preparation_receipt.py. No filesystem or
 * console calls. Callers separately anchor and hold the named marker inode. */
#include "copy_sha256.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#define BW_GENERATION_SIZE 128u
#define BW_GENERATION_PREFIX "blackwire.generation."
static inline int bw_generation_run(const char *run) {
    if (!run || strlen(run)!=32) return -1;
    for (unsigned i=0;i<32;i++) if (!((run[i]>='0'&&run[i]<='9')||(run[i]>='a'&&run[i]<='f'))) return -1;
    return 0;
}
static inline int bw_generation_name(char out[64],const char *run) {
    if (bw_generation_run(run)) return -1;
    int n=snprintf(out,64,BW_GENERATION_PREFIX "%s.bin",run);
    return n<0||n>=64?-1:0;
}
static inline int bw_generation_bytes(uint8_t out[BW_GENERATION_SIZE],const char *run,
                                       const char *title,const char *game,const uint8_t manifest[32]) {
    if (bw_generation_run(run)||!title||strlen(title)!=9||strncmp(title,"PPSA",4)||!game||!*game||strlen(game)>240||!manifest) return -1;
    for (unsigned i=4;i<9;i++) if (title[i]<'0'||title[i]>'9') return -1;
    memset(out,0,BW_GENERATION_SIZE);memcpy(out,"BWGEN001",8);out[8]=1;out[12]=128;
    memcpy(out+16,run,32);memcpy(out+48,title,9);cp_sha_bytes(game,strlen(game),out+64);memcpy(out+96,manifest,32);
    return 0;
}
static inline int bw_generation_matches(const void *bytes,size_t size,const char *run,
                                         const char *title,const char *game,const uint8_t manifest[32]) {
    uint8_t expected[BW_GENERATION_SIZE];
    return size!=BW_GENERATION_SIZE||bw_generation_bytes(expected,run,title,game,manifest)||memcmp(bytes,expected,sizeof expected)?-1:0;
}
#endif
