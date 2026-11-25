/* KerneliaOS X25519 (RFC 7748). Bkz. x25519.c */
#ifndef KERNELIA_X25519_H
#define KERNELIA_X25519_H
#include <stdint.h>

/* q = n * p (x-koordinat). n: 32 bayt olcekleyici (clamp uygulanir). */
int x25519(uint8_t q[32], const uint8_t n[32], const uint8_t p[32]);

/* pub = priv * baz nokta (u = 9) */
void x25519_public(uint8_t pub[32], const uint8_t priv[32]);

#endif
