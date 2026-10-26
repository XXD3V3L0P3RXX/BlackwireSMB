/* From https://github.com/bitdust/tiny-AES128-C
 * Licenced as Public Domain
 */

#ifndef _AES_H_
#define _AES_H_

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
 
#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

// #define the macros below to 1/0 to enable/disable the mode of operation.
//
// CBC enables AES128 encryption in CBC-mode of operation and handles 0-padding.
// ECB enables the basic ECB 16-byte block algorithm. Both can be enabled simultaneously.

// The #ifndef-guard allows it to be configured before #include'ing or at compile time.
#ifndef CBC
  #define CBC 0
#endif

#ifndef ECB
  #define ECB 1
#endif



#if defined(ECB) && ECB

/* Blackwire: one caller-owned AES-128 key schedule, reusable for a bounded
 * operation such as one CMAC message. Encryption does not modify the schedule;
 * no process-global or last-key cache is used. Keep the context disjoint from
 * input/output blocks and clear it when the operation no longer needs its key.
 * Block input/output must be disjoint or identical. Hardware use is selected
 * from local CPU capabilities during init; unavailable CPUs keep pure C. */
struct AES128_ctx { uint8_t round_key[176]; uint8_t use_aesni; };
void AES128_init_ctx(struct AES128_ctx *context, const uint8_t *key);
void AES128_ECB_encrypt_ctx(const struct AES128_ctx *context,
                            const uint8_t *input, uint8_t *output);

void AES128_ECB_encrypt(uint8_t* input, const uint8_t* key, uint8_t *output);
void AES128_ECB_decrypt(uint8_t* input, const uint8_t* key, uint8_t *output);

#endif // #if defined(ECB) && ECB


#if defined(CBC) && CBC

void AES128_CBC_encrypt_buffer(uint8_t* output, uint8_t* input, uint32_t length, const uint8_t* key, uint8_t* iv);
void AES128_CBC_decrypt_buffer(uint8_t* output, uint8_t* input, uint32_t length, const uint8_t* key, uint8_t* iv);

#endif // #if defined(CBC) && CBC



#endif //_AES_H_
