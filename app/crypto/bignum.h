/* KerneliaOS buyuk sayi aritmetigi. Sayilar 32 bit limb'lerden olusur, en dusuk limb index 0.
   Uzunluk (len) limb sayisidir; en fazla BN_MAXLEN limb (4096 bit). */
#ifndef KERNELIA_BIGNUM_H
#define KERNELIA_BIGNUM_H
#include <stdint.h>
#include <stddef.h>

#define BN_MAXLEN 128

/* Buyuk-endian bayt dizisini limb dizisine cevirir. Sigmiyorsa -1. */
int bn_from_be(uint32_t *out, size_t len, const uint8_t *in, size_t inlen);

/* Limb dizisini buyuk-endian bayt dizisine yazar (outlen bayt). */
void bn_to_be(uint8_t *out, size_t outlen, const uint32_t *a, size_t len);

/* Karsilastirma: a < b ise -1, esitse 0, buyukse 1. */
int bn_cmp(const uint32_t *a, const uint32_t *b, size_t len);

/* res = base^exp mod m. m tek sayi ve base < m olmali; exp buyuk-endian bayt.
   Hata (m cift, base >= m, uzunluk siniri) durumunda -1. */
int bn_modexp(uint32_t *res, const uint32_t *base, const uint8_t *exp, size_t elen,
              const uint32_t *m, size_t len);

#endif
