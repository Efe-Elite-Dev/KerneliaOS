/* KerneliaOS RSA PKCS#1 v1.5 + SHA-256 imza dogrulama. Bkz. rsa.h */
#include "rsa.h"
#include "bignum.h"
#include "sha256.h"

#define RSA_MAX_BYTES 512

/* SHA-256 DigestInfo oneki (RFC 8017 9.2 not 1) */
static const uint8_t DIGEST_INFO_SHA256[19] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
    0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
};

int rsa_pkcs1_sha256_verify(const uint8_t *n, size_t nlen,
                            const uint8_t *e, size_t elen,
                            const uint8_t *sig, size_t slen,
                            const uint8_t *msg, size_t mlen){
    /* Bbastaki sifir baytlari at */
    while(nlen > 0 && *n == 0){ n++; nlen--; }
    /* EM >= tLen(51) + 11 bayt olmali; en fazla RSA_MAX_BYTES */
    if(nlen < 62 || nlen > RSA_MAX_BYTES) return -1;
    if(slen != nlen) return -1;
    /* Ortak us tek ve birden buyuk olmali */
    if(elen == 0 || (e[elen - 1] & 1) == 0) return -1;

    size_t len = (nlen + 3) / 4;
    uint32_t nm[BN_MAXLEN], sm[BN_MAXLEN], rm[BN_MAXLEN];
    if(bn_from_be(nm, len, n, nlen) != 0) return -1;
    if(bn_from_be(sm, len, sig, slen) != 0) return -1;
    /* s < n olmali */
    if(bn_cmp(sm, nm, len) >= 0) return -1;
    if(bn_modexp(rm, sm, e, elen, nm, len) != 0) return -1;

    uint8_t em[RSA_MAX_BYTES];
    bn_to_be(em, nlen, rm, len);

    /* EM = 00 || 01 || PS (FF, en az 8 bayt) || 00 || DigestInfo || H */
    size_t tlen = sizeof(DIGEST_INFO_SHA256) + 32;
    if(em[0] != 0x00 || em[1] != 0x01) return -1;
    size_t sep = nlen - tlen - 1;           /* 00 ayiraci */
    if(sep < 10) return -1;                 /* PS en az 8 bayt */
    if(em[sep] != 0x00) return -1;
    for(size_t i = 2; i < sep; i++) if(em[i] != 0xFF) return -1;
    for(size_t i = 0; i < sizeof(DIGEST_INFO_SHA256); i++){
        if(em[sep + 1 + i] != DIGEST_INFO_SHA256[i]) return -1;
    }

    uint8_t h[32];
    sha256(msg, mlen, h);
    for(size_t i = 0; i < 32; i++){
        if(em[nlen - 32 + i] != h[i]) return -1;
    }
    return 0;
}
