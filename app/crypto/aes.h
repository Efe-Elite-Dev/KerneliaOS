/* KerneliaOS AES (FIPS 197), yalnizca sifreleme: 128 ve 256 bit anahtar. GCM icin yeterli.
   S-kutusu GF(2^8) uzerinden hesaplanir (elle tablo yazimi hatasi riski yok). */
#ifndef KERNELIA_AES_H
#define KERNELIA_AES_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint8_t rk[240];   /* en fazla 15 anahtar blogu (AES-256) */
    int     rounds;    /* 10 (AES-128) ya da 14 (AES-256) */
} aes_ctx;

/* klen: 16 ya da 32. Donus 0 basarili, -1 gecersiz anahtar uzunlugu. */
int  aes_init(aes_ctx *c, const uint8_t *key, size_t klen);
void aes_encrypt_block(const aes_ctx *c, const uint8_t in[16], uint8_t out[16]);

#endif
