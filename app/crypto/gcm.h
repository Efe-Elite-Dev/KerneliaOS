/* KerneliaOS AES-GCM (NIST SP 800-38D), 96 bit IV. Etiket 16 bayt. */
#ifndef KERNELIA_GCM_H
#define KERNELIA_GCM_H
#include <stdint.h>
#include <stddef.h>

/* Sifreler: ct, pt ile ayni uzunlukta. tag 16 bayt. Donus 0 basarili. */
int gcm_encrypt(const uint8_t *key, size_t klen, const uint8_t iv[12],
                const uint8_t *aad, size_t alen,
                const uint8_t *pt, size_t plen, uint8_t *ct, uint8_t tag[16]);

/* Etiketi dogrular; eslesmezse pt doldurulmaz ve -1 doner. Basarili 0. */
int gcm_decrypt(const uint8_t *key, size_t klen, const uint8_t iv[12],
                const uint8_t *aad, size_t alen,
                const uint8_t *ct, size_t clen, const uint8_t tag[16], uint8_t *pt);

#endif
