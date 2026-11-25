/* KerneliaOS TLS 1.3 kayit koruma. Bkz. tls13.h */
#include "tls13.h"
#include "gcm.h"

/* nonce = iv XOR (sira no, 12 baytin son 8'i, big-endian) */
static void nonce_for(const tls_keys *k, uint8_t n[12]){
    for(int i = 0; i < 12; i++) n[i] = k->iv[i];
    for(int i = 0; i < 8; i++) n[4 + i] ^= (uint8_t)(k->seq >> (56 - 8 * i));
}

int tls_seal(tls_keys *k, uint8_t type, const uint8_t *pt, size_t plen, uint8_t *out, size_t cap){
    if(plen > TLS_MAX_PT) return -1;
    size_t inner = plen + 1;               /* icerik || icerik tipi */
    size_t clen = inner + 16;              /* + etiket */
    if(cap < 5 + clen) return -1;
    uint8_t aad[5] = {0x17, 0x03, 0x03, (uint8_t)(clen >> 8), (uint8_t)clen};
    uint8_t inbuf[TLS_MAX_PT + 1];
    for(size_t i = 0; i < plen; i++) inbuf[i] = pt[i];
    inbuf[plen] = type;
    uint8_t nonce[12], tag[16];
    nonce_for(k, nonce);
    for(int i = 0; i < 5; i++) out[i] = aad[i];
    if(gcm_encrypt(k->key, 16, nonce, aad, 5, inbuf, inner, out + 5, tag) != 0) return -1;
    for(int i = 0; i < 16; i++) out[5 + inner + i] = tag[i];
    k->seq++;
    return (int)(5 + clen);
}

int tls_open(tls_keys *k, const uint8_t *rec, size_t rlen, uint8_t *pt, size_t cap, uint8_t *ptype){
    if(rlen < 5 + 16 + 1) return -1;
    if(rec[0] != 0x17 || rec[1] != 0x03 || rec[2] != 0x03) return -1;
    size_t clen = ((size_t)rec[3] << 8) | rec[4];
    if(clen != rlen - 5 || clen < 17) return -1;
    size_t inner = clen - 16;
    if(inner > TLS_MAX_PT + 1) return -1;
    uint8_t nonce[12];
    nonce_for(k, nonce);
    uint8_t tmp[TLS_MAX_PT + 1];
    if(gcm_decrypt(k->key, 16, nonce, rec, 5, rec + 5, inner, rec + 5 + inner, tmp) != 0) return -1;
    /* sondan: icerik tipi, once sifir dolgu olabilir (RFC 8446 5.4) */
    size_t n = inner;
    while(n > 0 && tmp[n - 1] == 0) n--;
    if(n == 0) return -1;
    uint8_t t = tmp[n - 1];
    size_t plen = n - 1;
    if(plen > cap) return -1;
    for(size_t i = 0; i < plen; i++) pt[i] = tmp[i];
    *ptype = t;
    k->seq++;
    return (int)plen;
}
