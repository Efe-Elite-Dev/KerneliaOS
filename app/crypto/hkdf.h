/* KerneliaOS HKDF-SHA256 (RFC 5869). TLS 1.3 anahtar takvimi bunun uzerine kurulur. */
#ifndef KERNELIA_HKDF_H
#define KERNELIA_HKDF_H
#include <stdint.h>
#include <stddef.h>

void hkdf_extract(const uint8_t *salt, size_t slen, const uint8_t *ikm, size_t ilen, uint8_t prk[32]);
/* L en fazla 255*32 bayt. Donus 0 basarili, -1 L cok buyuk. */
int  hkdf_expand(const uint8_t prk[32], const uint8_t *info, size_t ilen, uint8_t *okm, size_t L);

#endif
