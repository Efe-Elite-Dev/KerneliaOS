/* KerneliaOS X.509 (RFC 5280) ayrıştırma, RSA zincir doğrulama ve hostname kontrolü.
   Yalnızca DER; RSA anahtarlı ve sha256WithRSAEncryption imzalı sertifikalar desteklenir.
   Geçerlilik tarihi (notBefore/notAfter) bu sürümde denetlenmez: kernelde saat kaynağı yok. */
#ifndef KERNELIA_X509_H
#define KERNELIA_X509_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    const uint8_t *tbs;      size_t tbs_len;      /* imzalanan TBSCertificate (tam DER) */
    const uint8_t *sig;      size_t sig_len;      /* imza degeri */
    const uint8_t *issuer;   size_t issuer_len;   /* Name (tam DER) */
    const uint8_t *subject;  size_t subject_len;  /* Name (tam DER) */
    const uint8_t *n;        size_t n_len;        /* RSA modulu */
    const uint8_t *e;        size_t e_len;        /* RSA ortak us */
    const uint8_t *san;      size_t san_len;      /* GeneralNames icerigi (SAN uzantisi yoksa 0) */
    int sha256_rsa;                               /* imza algoritmasi sha256WithRSA mi */
} x509_cert;

/* DER sertifikayi ayristirir. Basarili 0. */
int x509_parse(x509_cert *c, const uint8_t *der, size_t len);

/* child, issuer tarafindan imzalanmis mi? (issuer adi, RSA-SHA256 imzasi). Basarili 0. */
int x509_verify_issuer(const x509_cert *child, const x509_cert *issuer);

/* host, SAN dNSName girdilerinden biriyle eslesiyor mu? (*.ornek.com tek etiket esler).
   SAN yoksa eslesme yoktur (CN'ye geri donulmez). Basarili 0. */
int x509_check_hostname(const x509_cert *c, const char *host, size_t hlen);

#endif
