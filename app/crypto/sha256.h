/* KerneliaOS SHA-256 ve HMAC-SHA256 (FIPS 180-4, RFC 2104). Saf C, donanim bagimliligi yok. */
#ifndef KERNELIA_SHA256_H
#define KERNELIA_SHA256_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t h[8];
    uint8_t  buf[64];
    uint32_t buflen;
    uint64_t total;   /* islenen toplam bayt */
} sha256_ctx;

void sha256_init(sha256_ctx *c);
void sha256_update(sha256_ctx *c, const uint8_t *data, size_t n);
void sha256_final(sha256_ctx *c, uint8_t out[32]);
void sha256(const uint8_t *data, size_t n, uint8_t out[32]);
void hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *data, size_t dlen, uint8_t out[32]);

#endif
