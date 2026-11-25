/* KerneliaOS TLS 1.3 istemci el sikismasi (RFC 8446). Tek sifre: TLS_AES_128_GCM_SHA256.
   Anahtar degisimi: X25519. Sunucu dogrulamasi: x509 zinciri + hostname.
   Sunucu CertificateVerify imzasi: rsa_pss_rsae_sha256 (RSA) veya ecdsa_secp256r1_sha256 (P-256).
   Ag erisimi cagiranin io callback'leri ile; rastgelelik cagirandan (entropy, 64 bayt):
   kernelde bu deger guvenilir bir RNG'den gelmeli, henuz yok. */
#ifndef KERNELIA_TLS_HS_H
#define KERNELIA_TLS_HS_H
#include <stdint.h>
#include <stddef.h>
#include "tls13.h"
#include "sha256.h"
#include "x509.h"

#define TLS_HS_BUF 16384

typedef struct {
    int (*send)(void *ctx, const uint8_t *buf, size_t n);   /* tum n bayt; 0 basarili */
    int (*recv)(void *ctx, uint8_t *buf, size_t n);         /* tam n bayt; 0 basarili */
    void *ctx;
} tls_io;

typedef struct {
    tls_io io;
    tls_keys wr, rd;               /* gecerli yazma/okuma anahtarlari */
    sha256_ctx tr;                 /* el sikisma transkripti */
    uint8_t priv[32], pub[32], srv_pub[32];
    uint8_t hs_sec[32], c_fin[32], s_fin[32];
    x509_cert trust;               /* dogrulayici CA (ca_der'den) */
    int leaf_ec;                   /* sunucu sertifikasi P-256 mi */
    uint8_t leaf_ec_pub[65];
    uint8_t leaf_n[512]; size_t leaf_n_len;
    uint8_t leaf_e[16];  size_t leaf_e_len;
    int rd_encrypted;
    uint8_t hs[TLS_HS_BUF]; size_t hs_len, hs_pos;
    uint8_t rec[TLS_MAX_PT + 256];
    uint8_t pt[TLS_MAX_PT + 256];
} tls_hs;

/* El sikismayi yapar. host: SNI ve SAN kontrolu; ca_der: guvenilen CA (DER). Basarili 0. */
int tls_hs_connect(tls_hs *s, const tls_io *io, const char *host, size_t hlen,
                   const uint8_t *ca_der, size_t ca_len, const uint8_t entropy[64]);

/* Uygulama verisi gonderir (n <= TLS_MAX_PT). Basarili 0. */
int tls_hs_write(tls_hs *s, const uint8_t *data, size_t n);

/* Uygulama verisi okur. Donus: bayt sayisi (>0), kapanista 0, hata -1. */
int tls_hs_read(tls_hs *s, uint8_t *buf, size_t cap);

#endif
