/* KerneliaOS AES-GCM. Bkz. gcm.h. AES icin aes.c kullanilir. */
#include "gcm.h"
#include "aes.h"

/* GF(2^128) carpimi (SP 800-38D algoritma 1). Bloklar buyuk-endian bayt dizisi. */
static void gf128_mul(const uint8_t x[16], const uint8_t y[16], uint8_t z[16]){
    uint8_t v[16];
    uint8_t r[16] = {0};
    for(int i = 0; i < 16; i++) v[i] = y[i];
    for(int i = 0; i < 128; i++){
        if((x[i >> 3] >> (7 - (i & 7))) & 1){
            for(int j = 0; j < 16; j++) r[j] ^= v[j];
        }
        int lsb = v[15] & 1;
        for(int j = 15; j > 0; j--) v[j] = (uint8_t)((v[j] >> 1) | ((v[j-1] & 1) << 7));
        v[0] >>= 1;
        if(lsb) v[0] ^= 0xE1;
    }
    for(int j = 0; j < 16; j++) z[j] = r[j];
}

/* GHASH: A ve C'yi sifir dolgulu blok halinde isler, sonuna uzunluk blogunu ekler */
static void ghash(const uint8_t h[16], const uint8_t *aad, size_t alen,
                  const uint8_t *ct, size_t clen, uint8_t out[16]){
    uint8_t y[16] = {0};
    uint8_t blk[16], tmp[16];
    for(size_t off = 0; off < alen; off += 16){
        for(int j = 0; j < 16; j++) blk[j] = 0;
        size_t n = alen - off < 16 ? alen - off : 16;
        for(size_t j = 0; j < n; j++) blk[j] = aad[off + j];
        for(int j = 0; j < 16; j++) y[j] ^= blk[j];
        gf128_mul(y, h, tmp);
        for(int j = 0; j < 16; j++) y[j] = tmp[j];
    }
    for(size_t off = 0; off < clen; off += 16){
        for(int j = 0; j < 16; j++) blk[j] = 0;
        size_t n = clen - off < 16 ? clen - off : 16;
        for(size_t j = 0; j < n; j++) blk[j] = ct[off + j];
        for(int j = 0; j < 16; j++) y[j] ^= blk[j];
        gf128_mul(y, h, tmp);
        for(int j = 0; j < 16; j++) y[j] = tmp[j];
    }
    uint64_t abits = (uint64_t)alen * 8, cbits = (uint64_t)clen * 8;
    for(int j = 0; j < 8; j++){
        blk[j]     = (uint8_t)(abits >> (56 - 8*j));
        blk[8 + j] = (uint8_t)(cbits >> (56 - 8*j));
    }
    for(int j = 0; j < 16; j++) y[j] ^= blk[j];
    gf128_mul(y, h, tmp);
    for(int j = 0; j < 16; j++) out[j] = tmp[j];
}

/* 32 bit sayac: son 4 baytini artirir */
static void inc32(uint8_t cb[16]){
    for(int i = 15; i >= 12; i--){
        cb[i]++;
        if(cb[i] != 0) break;
    }
}

/* CTR: out[i] = in[i] ^ E(cb), cb her blokta artirilir */
static void ctr_xor(const aes_ctx *a, uint8_t cb[16], const uint8_t *in, size_t n, uint8_t *out){
    uint8_t ks[16];
    for(size_t off = 0; off < n; off += 16){
        aes_encrypt_block(a, cb, ks);
        inc32(cb);
        size_t m = n - off < 16 ? n - off : 16;
        for(size_t j = 0; j < m; j++) out[off + j] = in[off + j] ^ ks[j];
    }
}

static int setup(aes_ctx *a, uint8_t h[16], uint8_t j0[16],
                 const uint8_t *key, size_t klen, const uint8_t iv[12]){
    if(aes_init(a, key, klen) != 0) return -1;
    for(int i = 0; i < 16; i++) h[i] = 0;
    aes_encrypt_block(a, h, h);
    for(int i = 0; i < 12; i++) j0[i] = iv[i];
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;
    return 0;
}

int gcm_encrypt(const uint8_t *key, size_t klen, const uint8_t iv[12],
                const uint8_t *aad, size_t alen,
                const uint8_t *pt, size_t plen, uint8_t *ct, uint8_t tag[16]){
    aes_ctx a; uint8_t h[16], j0[16], cb[16], s[16], ek[16];
    if(setup(&a, h, j0, key, klen, iv) != 0) return -1;
    for(int i = 0; i < 16; i++) cb[i] = j0[i];
    inc32(cb);
    ctr_xor(&a, cb, pt, plen, ct);
    ghash(h, aad, alen, ct, plen, s);
    aes_encrypt_block(&a, j0, ek);
    for(int i = 0; i < 16; i++) tag[i] = s[i] ^ ek[i];
    return 0;
}

int gcm_decrypt(const uint8_t *key, size_t klen, const uint8_t iv[12],
                const uint8_t *aad, size_t alen,
                const uint8_t *ct, size_t clen, const uint8_t tag[16], uint8_t *pt){
    aes_ctx a; uint8_t h[16], j0[16], cb[16], s[16], ek[16];
    if(setup(&a, h, j0, key, klen, iv) != 0) return -1;
    ghash(h, aad, alen, ct, clen, s);
    aes_encrypt_block(&a, j0, ek);
    uint8_t diff = 0;
    for(int i = 0; i < 16; i++) diff |= (uint8_t)((s[i] ^ ek[i]) ^ tag[i]);
    if(diff != 0) return -1;
    for(int i = 0; i < 16; i++) cb[i] = j0[i];
    inc32(cb);
    ctr_xor(&a, cb, ct, clen, pt);
    return 0;
}
