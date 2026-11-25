/* KerneliaOS rastgelelik kaynagi: donanim RDRAND komutu (Intel/AMD).
   RDRAND yoksa rng_available() 0 doner ve cagiran (or. HTTPS) islemi reddetmelidir;
   zayif bir yedek kaynak kullanilmaz. */
#ifndef KERNELIA_RNG_H
#define KERNELIA_RNG_H
#include <stdint.h>
#include <stddef.h>

/* CPUID.1:ECX bit 30 (RDRAND). Yoksa 0. */
int rng_available(void);

/* out'u n bayt rastgele veriyle doldurur. Basarili 0; RDRAND basarisiz olursa -1. */
int rng_fill(uint8_t *out, size_t n);

#endif
