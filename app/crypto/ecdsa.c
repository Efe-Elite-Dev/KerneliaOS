/* KerneliaOS ECDSA P-256 dogrulama. Bkz. ecdsa.h
   Alan ve skaler aritmetigi bignum.c uzerinden (Montgomery). Noktalar affine koordinatta;
   ters alma Fermat ile yapilir (yavas ama kucuk). Yalnizca acik veri uzerinde calisir:
   sabit zamanli degildir. */
#include "ecdsa.h"
#include "bignum.h"

#define EL 8

/* P-256 parametreleri (FIPS 186-4 / SEC 2), little-endian limb (index 0 en dusuk) */
static const uint32_t P256_P[EL] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
                                0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF};
static const uint32_t P256_N[EL] = {0xFC632551, 0xF3B9CAC2, 0xA7179E84, 0xBCE6FAAD,
                                0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF};
static const uint32_t P256_B[EL] = {0x27D2604B, 0x3BCE3C3E, 0xCC53B0F6, 0x651D06B0,
                                0x769886BC, 0xB3EBBD55, 0xAA3A93E7, 0x5AC635D8};
static const uint32_t P256_GX[EL] = {0xD898C296, 0xF4A13945, 0x2DEB33A0, 0x77037D81,
                                0x63A440F2, 0xF8BCE6E5, 0xE12C4247, 0x6B17D1F2};
static const uint32_t P256_GY[EL] = {0x37BF51F5, 0xCBB64068, 0x6B315ECE, 0x2BCE3357,
                                0x7C0F9E16, 0x8EE7EB4A, 0xFE1A7F9B, 0x4FE342E2};
static const uint32_t P256_THREE[EL] = {3, 0, 0, 0, 0, 0, 0, 0};

typedef struct { uint32_t x[EL]; uint32_t y[EL]; int inf; } ec_pt;

static bn_ctx P256_CP, P256_CN;                 /* alan (mod p) ve skaler (mod n) baglamlari */
static uint8_t P256_EXPP[32], P256_EXPN[32]; /* p-2 ve n-2: ters alma ussu */

static int p256_is_zero(const uint32_t *a){
    for(size_t i = 0; i < EL; i++) if(a[i]) return 0;
    return 1;
}

static int p256_eq(const uint32_t *a, const uint32_t *b){ return bn_cmp(a, b, EL) == 0; }

static void p256_cpy(uint32_t *r, const uint32_t *a){
    for(size_t i = 0; i < EL; i++) r[i] = a[i];
}

static void p256_fmul(uint32_t *r, const uint32_t *a, const uint32_t *b){ bn_mulmod(&P256_CP, r, a, b); }
static void p256_fadd(uint32_t *r, const uint32_t *a, const uint32_t *b){ bn_addmod(r, a, b, P256_P, EL); }
static void p256_fsub(uint32_t *r, const uint32_t *a, const uint32_t *b){ bn_submod(r, a, b, P256_P, EL); }
static void p256_finv(uint32_t *r, const uint32_t *a){ bn_modexp(r, a, P256_EXPP, 32, P256_P, EL); }

/* ussu m-2 (buyuk-endian, 32 bayt); m tek bir asal */
static void p256_mkexp(uint8_t out[32], const uint32_t *m){
    uint32_t t[EL];
    uint64_t borrow = 0;
    for(size_t i = 0; i < EL; i++){
        uint64_t sub = (i == 0) ? 2 : 0;
        uint64_t d = (uint64_t)m[i] - sub - borrow;
        t[i] = (uint32_t)d;
        borrow = (d >> 63) & 1;
    }
    bn_to_be(out, 32, t, EL);
}

/* r = 2P ((x,y) eğrisi, a = -3) */
static void p256_dbl(ec_pt *r, const ec_pt *in){
    ec_pt a = *in;
    if(a.inf || p256_is_zero(a.y)){ r->inf = 1; return; }
    uint32_t t[EL], num[EL], den[EL], lam[EL], x3[EL], y3[EL];
    p256_fmul(t, a.x, a.x);                 /* x^2 */
    p256_fadd(num, t, t);
    p256_fadd(num, num, t);                 /* 3x^2 */
    p256_fsub(num, num, P256_THREE);             /* 3x^2 - 3 */
    p256_fadd(den, a.y, a.y);               /* 2y */
    p256_finv(den, den);
    p256_fmul(lam, num, den);
    p256_fmul(t, lam, lam);
    p256_fsub(x3, t, a.x);
    p256_fsub(x3, x3, a.x);
    p256_fsub(t, a.x, x3);
    p256_fmul(y3, lam, t);
    p256_fsub(y3, y3, a.y);
    p256_cpy(r->x, x3);
    p256_cpy(r->y, y3);
    r->inf = 0;
}

/* r = a + b */
static void p256_add(ec_pt *r, const ec_pt *in1, const ec_pt *in2){
    ec_pt a = *in1, b = *in2;
    if(a.inf){ *r = b; return; }
    if(b.inf){ *r = a; return; }
    if(p256_eq(a.x, b.x)){
        if(p256_eq(a.y, b.y)){ p256_dbl(r, &a); return; }
        r->inf = 1;                      /* P + (-P) = O */
        return;
    }
    uint32_t t[EL], num[EL], den[EL], lam[EL], x3[EL], y3[EL];
    p256_fsub(num, b.y, a.y);
    p256_fsub(den, b.x, a.x);
    p256_finv(den, den);
    p256_fmul(lam, num, den);
    p256_fmul(t, lam, lam);
    p256_fsub(x3, t, a.x);
    p256_fsub(x3, x3, b.x);
    p256_fsub(t, a.x, x3);
    p256_fmul(y3, lam, t);
    p256_fsub(y3, y3, a.y);
    p256_cpy(r->x, x3);
    p256_cpy(r->y, y3);
    r->inf = 0;
}

/* r = k * p, k 32 bayt buyuk-endian (msb once) */
static void p256_mul(ec_pt *r, const uint8_t k[32], const ec_pt *p){
    ec_pt acc;
    for(size_t i = 0; i < EL; i++){ acc.x[i] = 0; acc.y[i] = 0; }
    acc.inf = 1;
    for(size_t i = 0; i < 32; i++){
        for(int bit = 7; bit >= 0; bit--){
            p256_dbl(&acc, &acc);
            if((k[i] >> bit) & 1) p256_add(&acc, &acc, p);
        }
    }
    *r = acc;
}

/* DER uzunlugu oku (kisa ve en fazla 2 baytlik uzun bicim) */
static int p256_der_len(const uint8_t **p, const uint8_t *end, size_t *len){
    if(*p >= end) return -1;
    uint8_t b = *(*p)++;
    if((b & 0x80) == 0){ *len = b; return 0; }
    size_t nb = b & 0x7F;
    if(nb == 0 || nb > 2 || (size_t)(end - *p) < nb) return -1;
    size_t l = 0;
    for(size_t i = 0; i < nb; i++) l = (l << 8) | *(*p)++;
    *len = l;
    return 0;
}

/* DER INTEGER oku: pozitif olmali, bastaki 0 baytlari atilir, en fazla 32 bayt anlamli */
static int p256_der_int(const uint8_t **p, const uint8_t *end, uint32_t r[EL]){
    if(*p >= end || **p != 0x02) return -1;
    (*p)++;
    size_t l;
    if(p256_der_len(p, end, &l) != 0 || l == 0 || (size_t)(end - *p) < l) return -1;
    const uint8_t *v = *p;
    *p += l;
    if(v[0] & 0x80) return -1;
    while(l > 1 && v[0] == 0){ v++; l--; }
    if(l > 32) return -1;
    return bn_from_be(r, EL, v, l);
}

static int p256_der_sig(const uint8_t *sig, size_t slen, uint32_t r[EL], uint32_t s[EL]){
    if(slen < 2 || sig[0] != 0x30) return -1;
    const uint8_t *p = sig + 1, *end = sig + slen;
    size_t l;
    if(p256_der_len(&p, end, &l) != 0 || (size_t)(end - p) != l) return -1;
    if(p256_der_int(&p, end, r) != 0 || p256_der_int(&p, end, s) != 0) return -1;
    return p == end ? 0 : -1;
}

int ecdsa_p256_verify(const uint8_t pub[65], const uint8_t *sig, size_t slen, const uint8_t hash[32]){
    if(pub[0] != 0x04) return -1;
    if(bn_ctx_init(&P256_CP, P256_P, EL) != 0 || bn_ctx_init(&P256_CN, P256_N, EL) != 0) return -1;
    p256_mkexp(P256_EXPP, P256_P);
    p256_mkexp(P256_EXPN, P256_N);

    uint32_t r[EL], s[EL], e[EL], qx[EL], qy[EL];
    if(p256_der_sig(sig, slen, r, s) != 0) return -1;
    if(p256_is_zero(r) || p256_is_zero(s) || bn_cmp(r, P256_N, EL) >= 0 || bn_cmp(s, P256_N, EL) >= 0) return -1;
    if(bn_from_be(qx, EL, pub + 1, 32) != 0 || bn_from_be(qy, EL, pub + 33, 32) != 0) return -1;
    if(bn_cmp(qx, P256_P, EL) >= 0 || bn_cmp(qy, P256_P, EL) >= 0) return -1;

    /* Q egri uzerinde mi: y^2 = x^3 - 3x + b */
    uint32_t lhs[EL], rhs[EL], t[EL];
    p256_fmul(lhs, qy, qy);
    p256_fmul(t, qx, qx);
    p256_fmul(rhs, t, qx);                  /* x^3 */
    p256_fmul(t, qx, P256_THREE);                /* 3x */
    p256_fsub(rhs, rhs, t);
    p256_fadd(rhs, rhs, P256_B);
    if(!p256_eq(lhs, rhs)) return -1;

    /* e = H mod n (256 bit ozet; n'den en fazla bir kez cikarmak yeter) */
    if(bn_from_be(e, EL, hash, 32) != 0) return -1;
    if(bn_cmp(e, P256_N, EL) >= 0) bn_submod(e, e, P256_N, P256_N, EL);

    /* w = s^-1 mod n; u1 = e*w; u2 = r*w */
    uint32_t w[EL], u1[EL], u2[EL];
    uint8_t u1b[32], u2b[32];
    if(bn_modexp(w, s, P256_EXPN, 32, P256_N, EL) != 0) return -1;
    bn_mulmod(&P256_CN, u1, e, w);
    bn_mulmod(&P256_CN, u2, r, w);
    bn_to_be(u1b, 32, u1, EL);
    bn_to_be(u2b, 32, u2, EL);

    /* R = u1*G + u2*Q */
    ec_pt G, Q, A, B, R;
    p256_cpy(G.x, P256_GX); p256_cpy(G.y, P256_GY); G.inf = 0;
    p256_cpy(Q.x, qx); p256_cpy(Q.y, qy); Q.inf = 0;
    p256_mul(&A, u1b, &G);
    p256_mul(&B, u2b, &Q);
    p256_add(&R, &A, &B);
    if(R.inf) return -1;

    /* x(R) mod n == r */
    uint32_t xr[EL];
    p256_cpy(xr, R.x);
    if(bn_cmp(xr, P256_N, EL) >= 0) bn_submod(xr, xr, P256_N, P256_N, EL);
    return p256_eq(xr, r) ? 0 : -1;
}
