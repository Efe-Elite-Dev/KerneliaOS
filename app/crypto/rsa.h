/* KerneliaOS RSA imza dogrulama: RSASSA-PKCS1-v1_5 ile SHA-256 (RFC 8017 sekil 8.2.2).
   Yalnizca dogrulama; anahtar ve imza buyuk-endian bayt dizisi olarak verilir. */
#ifndef KERNELIA_RSA_H
#define KERNELIA_RSA_H
#include <stdint.h>
#include <stddef.h>

/* n: modul (nlen bayt), e: ortak us (elen bayt), sig: imza (slen bayt = nlen),
   msg: dogrulanan mesaj. Gecerliyse 0, degilse -1. */
int rsa_pkcs1_sha256_verify(const uint8_t *n, size_t nlen,
                            const uint8_t *e, size_t elen,
                            const uint8_t *sig, size_t slen,
                            const uint8_t *msg, size_t mlen);

#endif
