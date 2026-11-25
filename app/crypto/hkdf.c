/* KerneliaOS HKDF-SHA256. Bkz. hkdf.h */
#include "hkdf.h"
#include "sha256.h"

void hkdf_extract(const uint8_t *salt, size_t slen, const uint8_t *ikm, size_t ilen, uint8_t prk[32]){
    uint8_t zero[32] = {0};
    if(salt == NULL || slen == 0){ salt = zero; slen = 32; }
    hmac_sha256(salt, slen, ikm, ilen, prk);
}

int hkdf_expand(const uint8_t prk[32], const uint8_t *info, size_t ilen, uint8_t *okm, size_t L){
    if(L > 255 * 32) return -1;
    uint8_t t[32];
    uint8_t buf[32 + 256 + 1];   /* T(i-1) || info || i */
    size_t tlen = 0, done = 0;
    for(uint8_t i = 1; done < L; i++){
        size_t p = 0;
        for(size_t k = 0; k < tlen; k++) buf[p++] = t[k];
        for(size_t k = 0; k < ilen; k++) buf[p++] = info[k];
        buf[p++] = i;
        hmac_sha256(prk, 32, buf, p, t);
        tlen = 32;
        size_t take = L - done < 32 ? L - done : 32;
        for(size_t k = 0; k < take; k++) okm[done + k] = t[k];
        done += take;
    }
    return 0;
}
