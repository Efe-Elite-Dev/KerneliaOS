/* KerneliaOS TLS 1.3 kayit katmani: TLS_AES_128_GCM_SHA256 ile kayit koruma (RFC 8446 bolum 5.2).
   Kayit: 5 baytlik baslik (0x17, 0x0303, uzunluk) + sifreli(icerik || icerik_tipi) + 16 bayt etiket. */
#ifndef KERNELIA_TLS13_H
#define KERNELIA_TLS13_H
#include <stdint.h>
#include <stddef.h>

#define TLS_CT_HANDSHAKE 22
#define TLS_CT_APPDATA   23
#define TLS_MAX_PT       16384

typedef struct {
    uint8_t  key[16];
    uint8_t  iv[12];
    uint64_t seq;
} tls_keys;

/* Gonderim: pt'yi kayit olarak out'a yazar (out en az plen + 22 bayt). Donus: kayit uzunlugu, hata -1. */
int tls_seal(tls_keys *k, uint8_t type, const uint8_t *pt, size_t plen, uint8_t *out, size_t cap);
/* Alim: rec'i acar, icerigi pt'ye yazar. *ptype icerik tipi. Donus: icerik uzunlugu, hata -1. */
int tls_open(tls_keys *k, const uint8_t *rec, size_t rlen, uint8_t *pt, size_t cap, uint8_t *ptype);

#endif
