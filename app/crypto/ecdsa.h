/* KerneliaOS ECDSA P-256 (SHA-256) imza dogrulama. Yalnizca dogrulama.
   Acik anahtar SEC 1 uncompressed bicimde: 04 || X || Y (65 bayt). */
#ifndef KERNELIA_ECDSA_H
#define KERNELIA_ECDSA_H
#include <stdint.h>
#include <stddef.h>

/* sig: DER SEQUENCE { r INTEGER, s INTEGER }. hash: mesajin SHA-256 ozeti.
   Gecerliyse 0, degilse -1. */
int ecdsa_p256_verify(const uint8_t pub[65], const uint8_t *sig, size_t slen, const uint8_t hash[32]);

#endif
