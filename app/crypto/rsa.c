/* KerneliaOS RSA imza dogrulama: PKCS#1 v1.5 ve PSS (SHA-256). Bkz. rsa.h */
#include "rsa.h"
#include "bignum.h"
#include "sha256.h"

#define RSA_MAX_BYTES 512
#define PSS_HLEN 32
#define PSS_SLEN 32

/* SHA-256 DigestInfo oneki (RFC 8017 9.2 not 1) */
static const uint8_t DIGEST_INFO_SHA256[19] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86, 0x48, 0x01, 0x65,
    0x03, 0x04, 0x02, 0x01, 0x05, 0x00, 0x04, 0x20
};

/* Bastaki sifir baytlarini atar, boyut ve us kontrolu yapar. */
static int rsa_check_params(const uint8_t **n, size_t *nlen,
                            const uint8_t *e, size_t elen, size_t slen){
    while(*nlen > 0 && **n == 0){ (*n)++; (*nlen)--; }
    if(*nlen < 62 || *nlen > RSA_MAX_BYTES) return -1;   /* PKCS#1: EM >= tLen + 11 */
    if(slen != *nlen) return -1;
    if(elen == 0 || (e[elen - 1] & 1) == 0) return -1;   /* us tek olmali */
    return 0;
}

/* em = s^e mod n, nlen bayt buyuk-endian. s < n olmali. */
static int rsa_public_op(const uint8_t *n, size_t nlen, const uint8_t *e, size_t elen,
                         const uint8_t *sig, size_t slen, uint8_t *em){
    size_t len = (nlen + 3) / 4;
    uint32_t nm[BN_MAXLEN], sm[BN_MAXLEN], rm[BN_MAXLEN];
    if(bn_from_be(nm, len, n, nlen) != 0) return -1;
    if(bn_from_be(sm, len, sig, slen) != 0) return -1;
    if(bn_cmp(sm, nm, len) >= 0) return -1;
    if(bn_modexp(rm, sm, e, elen, nm, len) != 0) return -1;
    bn_to_be(em, nlen, rm, len);
    return 0;
}

int rsa_pkcs1_sha256_verify(const uint8_t *n, size_t nlen,
                            const uint8_t *e, size_t elen,
                            const uint8_t *sig, size_t slen,
                            const uint8_t *msg, size_t mlen){
    if(rsa_check_params(&n, &nlen, e, elen, slen) != 0) return -1;
    uint8_t em[RSA_MAX_BYTES];
    if(rsa_public_op(n, nlen, e, elen, sig, slen, em) != 0) return -1;

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

/* MGF1 (RFC 8017 B.2.1), SHA-256 ile: mask = H(seed||0) || H(seed||1) || ... */
static void mgf1_sha256(const uint8_t *seed, size_t seedlen, uint8_t *mask, size_t masklen){
    uint8_t buf[RSA_MAX_BYTES + 4];
    uint8_t h[32];
    size_t off = 0;
    for(uint32_t ctr = 0; off < masklen; ctr++){
        for(size_t i = 0; i < seedlen; i++) buf[i] = seed[i];
        buf[seedlen]     = (uint8_t)(ctr >> 24);
        buf[seedlen + 1] = (uint8_t)(ctr >> 16);
        buf[seedlen + 2] = (uint8_t)(ctr >> 8);
        buf[seedlen + 3] = (uint8_t)ctr;
        sha256(buf, seedlen + 4, h);
        for(size_t i = 0; i < 32 && off < masklen; i++) mask[off++] = h[i];
    }
}

/* EMSA-PSS-VERIFY (RFC 8017 9.1.2), Hash = MGF = SHA-256, sLen = 32 */
int rsa_pss_sha256_verify(const uint8_t *n, size_t nlen,
                          const uint8_t *e, size_t elen,
                          const uint8_t *sig, size_t slen,
                          const uint8_t *msg, size_t mlen){
    if(rsa_check_params(&n, &nlen, e, elen, slen) != 0) return -1;

    /* modBits: modulun bit uzunlugu; emBits = modBits - 1 */
    size_t topbits = 0;
    for(uint8_t top = n[0]; top; top >>= 1) topbits++;
    size_t modBits = (nlen - 1) * 8 + topbits;
    size_t emBits = modBits - 1;
    size_t emLen = (emBits + 7) / 8;
    if(emLen < PSS_HLEN + PSS_SLEN + 2) return -1;

    uint8_t full[RSA_MAX_BYTES];
    if(rsa_public_op(n, nlen, e, elen, sig, slen, full) != 0) return -1;

    /* EM en sagdaki emLen bayttir; ustteki baytlar sifir olmali */
    size_t skip = nlen - emLen;
    for(size_t i = 0; i < skip; i++) if(full[i] != 0) return -1;
    const uint8_t *em = full + skip;
    if(em[emLen - 1] != 0xBC) return -1;

    size_t dbLen = emLen - PSS_HLEN - 1;
    const uint8_t *maskedDB = em;
    const uint8_t *H = em + dbLen;
    unsigned zbits = (unsigned)(8 * emLen - emBits);     /* 0..7 */
    if(zbits && (maskedDB[0] >> (8 - zbits)) != 0) return -1;

    uint8_t dbmask[RSA_MAX_BYTES], db[RSA_MAX_BYTES];
    mgf1_sha256(H, PSS_HLEN, dbmask, dbLen);
    for(size_t i = 0; i < dbLen; i++) db[i] = maskedDB[i] ^ dbmask[i];
    if(zbits) db[0] &= (uint8_t)(0xFF >> zbits);

    /* DB = PS (00...) || 01 || salt */
    size_t psLen = dbLen - PSS_SLEN - 1;
    for(size_t i = 0; i < psLen; i++) if(db[i] != 0) return -1;
    if(db[psLen] != 0x01) return -1;
    const uint8_t *salt = db + psLen + 1;

    /* M' = 8 sifir || H(mesaj) || salt; H' = H(M') == H */
    uint8_t mp[8 + 32 + PSS_SLEN];
    for(size_t i = 0; i < 8; i++) mp[i] = 0;
    sha256(msg, mlen, mp + 8);
    for(size_t i = 0; i < PSS_SLEN; i++) mp[40 + i] = salt[i];
    uint8_t hp[32];
    sha256(mp, sizeof mp, hp);
    for(size_t i = 0; i < 32; i++) if(hp[i] != H[i]) return -1;
    return 0;
}
