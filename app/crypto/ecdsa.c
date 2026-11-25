/* KerneliaOS ECDSA P-256 dogrulama. Bkz. ecdsa.h
   Alan ve skaler aritmetigi bignum.c uzerinden (Montgomery). Noktalar affine koordinatta;
   ters alma Fermat ile yapilir (yavas ama kucuk). Yalnizca acik veri uzerinde calisir:
   sabit zamanli degildir. */
#include "ecdsa.h"
#include "bignum.h"

#define EL 8

/* P-256 parametreleri (FIPS 186-4 / SEC 2), little-endian limb (index 0 en dusuk) */
static const uint32_t FP[EL] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
                                0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF};
static const uint32_t FN[EL] = {0xFC632551, 0xF3B9CAC2, 0xA7179E84, 0xBCE6FAAD,
                                0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF};
static const uint32_t FB[EL] = {0x27D2604B, 0x3BCE3C3E, 0xCC53B0F6, 0x651D06B0,
                                0x769886BC, 0xB3EBBD55, 0xAA3A93E7, 0x5AC635D8};
static const uint32_t GX[EL] = {0xD898C296, 0xF4A13945, 0x2DEB33A0, 0x77037D81,
                                0x63A440F2, 0xF8BCE6E5, 0xE12C4247, 0x6B17D1F2};
static const uint32_t GY[EL] = {0x37BF51F5, 0xCBB64068, 0x6B315ECE, 0x2BCE3357,
                                0x7C0F9E16, 0x8EE7EB4A, 0xFE1A7F9B, 0x4FE342E2};
static const uint32_t THREE[EL] = {3, 0, 0, 0, 0, 0, 0, 0};

typedef struct { uint32_t x[EL]; uint32_t y[EL]; int inf; } ec_pt;

static bn_ctx CP, CN;                 /* alan (mod p) ve skaler (mod n) baglamlari */
static uint8_t EXP_P[32], EXP_N[32]; /* p-2 ve n-2: ters alma ussu */

static int is_zero(const uint32_t *a){
    for(size_t i = 0; i < EL; i++) if(a[i]) return 0;
    return 1;
}

static int eq(const uint32_t *a, const uint32_t *b){ return bn_cmp(a, b, EL) == 0; }

static void cpy(uint32_t *r, const uint32_t *a){
    for(size_t i = 0; i < EL; i++) r[i] = a[i];
}

static void fp_mul(uint32_t *r, const uint32_t *a, const uint32_t *b){ bn_mulmod(&CP, r, a, b); }
static void fp_add(uint32_t *r, const uint32_t *a, const uint32_t *b){ bn_addmod(r, a, b, FP, EL); }
static void fp_sub(uint32_t *r, const uint32_t *a, const uint32_t *b){ bn_submod(r, a, b, FP, EL); }
static void fp_inv(uint32_t *r, const uint32_t *a){ bn_modexp(r, a, EXP_P, 32, FP, EL); }

/* ussu m-2 (buyuk-endian, 32 bayt); m tek bir asal */
static void make_exp(uint8_t out[32], const uint32_t *m){
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
static void pt_dbl(ec_pt *r, const ec_pt *in){
    ec_pt a = *in;
    if(a.inf || is_zero(a.y)){ r->inf = 1; return; }
    uint32_t t[EL], num[EL], den[EL], lam[EL], x3[EL], y3[EL];
    fp_mul(t, a.x, a.x);                 /* x^2 */
    fp_add(num, t, t);
    fp_add(num, num, t);                 /* 3x^2 */
    fp_sub(num, num, THREE);             /* 3x^2 - 3 */
    fp_add(den, a.y, a.y);               /* 2y */
    fp_inv(den, den);
    fp_mul(lam, num, den);
    fp_mul(t, lam, lam);
    fp_sub(x3, t, a.x);
    fp_sub(x3, x3, a.x);
    fp_sub(t, a.x, x3);
    fp_mul(y3, lam, t);
    fp_sub(y3, y3, a.y);
    cpy(r->x, x3);
    cpy(r->y, y3);
    r->inf = 0;
}

/* r = a + b */
static void pt_add(ec_pt *r, const ec_pt *in1, const ec_pt *in2){
    ec_pt a = *in1, b = *in2;
    if(a.inf){ *r = b; return; }
    if(b.inf){ *r = a; return; }
    if(eq(a.x, b.x)){
        if(eq(a.y, b.y)){ pt_dbl(r, &a); return; }
        r->inf = 1;                      /* P + (-P) = O */
        return;
    }
    uint32_t t[EL], num[EL], den[EL], lam[EL], x3[EL], y3[EL];
    fp_sub(num, b.y, a.y);
    fp_sub(den, b.x, a.x);
    fp_inv(den, den);
    fp_mul(lam, num, den);
    fp_mul(t, lam, lam);
    fp_sub(x3, t, a.x);
    fp_sub(x3, x3, b.x);
    fp_sub(t, a.x, x3);
    fp_mul(y3, lam, t);
    fp_sub(y3, y3, a.y);
    cpy(r->x, x3);
    cpy(r->y, y3);
    r->inf = 0;
}

/* r = k * p, k 32 bayt buyuk-endian (msb once) */
static void pt_mul(ec_pt *r, const uint8_t k[32], const ec_pt *p){
    ec_pt acc;
    for(size_t i = 0; i < EL; i++){ acc.x[i] = 0; acc.y[i] = 0; }
    acc.inf = 1;
    for(size_t i = 0; i < 32; i++){
        for(int bit = 7; bit >= 0; bit--){
            pt_dbl(&acc, &acc);
            if((k[i] >> bit) & 1) pt_add(&acc, &acc, p);
        }
    }
    *r = acc;
}

/* DER uzunlugu oku (kisa ve en fazla 2 baytlik uzun bicim) */
static int der_length(const uint8_t **p, const uint8_t *end, size_t *len){
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
static int der_int(const uint8_t **p, const uint8_t *end, uint32_t r[EL]){
    if(*p >= end || **p != 0x02) return -1;
    (*p)++;
    size_t l;
    if(der_length(p, end, &l) != 0 || l == 0 || (size_t)(end - *p) < l) return -1;
    const uint8_t *v = *p;
    *p += l;
    if(v[0] & 0x80) return -1;
    while(l > 1 && v[0] == 0){ v++; l--; }
    if(l > 32) return -1;
    return bn_from_be(r, EL, v, l);
}

static int der_sig(const uint8_t *sig, size_t slen, uint32_t r[EL], uint32_t s[EL]){
    if(slen < 2 || sig[0] != 0x30) return -1;
    const uint8_t *p = sig + 1, *end = sig + slen;
    size_t l;
    if(der_length(&p, end, &l) != 0 || (size_t)(end - p) != l) return -1;
    if(der_int(&p, end, r) != 0 || der_int(&p, end, s) != 0) return -1;
    return p == end ? 0 : -1;
}

int ecdsa_p256_verify(const uint8_t pub[65], const uint8_t *sig, size_t slen, const uint8_t hash[32]){
    if(pub[0] != 0x04) return -1;
    if(bn_ctx_init(&CP, FP, EL) != 0 || bn_ctx_init(&CN, FN, EL) != 0) return -1;
    make_exp(EXP_P, FP);
    make_exp(EXP_N, FN);

    uint32_t r[EL], s[EL], e[EL], qx[EL], qy[EL];
    if(der_sig(sig, slen, r, s) != 0) return -1;
    if(is_zero(r) || is_zero(s) || bn_cmp(r, FN, EL) >= 0 || bn_cmp(s, FN, EL) >= 0) return -1;
    if(bn_from_be(qx, EL, pub + 1, 32) != 0 || bn_from_be(qy, EL, pub + 33, 32) != 0) return -1;
    if(bn_cmp(qx, FP, EL) >= 0 || bn_cmp(qy, FP, EL) >= 0) return -1;

    /* Q egri uzerinde mi: y^2 = x^3 - 3x + b */
    uint32_t lhs[EL], rhs[EL], t[EL];
    fp_mul(lhs, qy, qy);
    fp_mul(t, qx, qx);
    fp_mul(rhs, t, qx);                  /* x^3 */
    fp_mul(t, qx, THREE);                /* 3x */
    fp_sub(rhs, rhs, t);
    fp_add(rhs, rhs, FB);
    if(!eq(lhs, rhs)) return -1;

    /* e = H mod n (256 bit ozet; n'den en fazla bir kez cikarmak yeter) */
    if(bn_from_be(e, EL, hash, 32) != 0) return -1;
    if(bn_cmp(e, FN, EL) >= 0) bn_submod(e, e, FN, FN, EL);

    /* w = s^-1 mod n; u1 = e*w; u2 = r*w */
    uint32_t w[EL], u1[EL], u2[EL];
    uint8_t u1b[32], u2b[32];
    if(bn_modexp(w, s, EXP_N, 32, FN, EL) != 0) return -1;
    bn_mulmod(&CN, u1, e, w);
    bn_mulmod(&CN, u2, r, w);
    bn_to_be(u1b, 32, u1, EL);
    bn_to_be(u2b, 32, u2, EL);

    /* R = u1*G + u2*Q */
    ec_pt G, Q, A, B, R;
    cpy(G.x, GX); cpy(G.y, GY); G.inf = 0;
    cpy(Q.x, qx); cpy(Q.y, qy); Q.inf = 0;
    pt_mul(&A, u1b, &G);
    pt_mul(&B, u2b, &Q);
    pt_add(&R, &A, &B);
    if(R.inf) return -1;

    /* x(R) mod n == r */
    uint32_t xr[EL];
    cpy(xr, R.x);
    if(bn_cmp(xr, FN, EL) >= 0) bn_submod(xr, xr, FN, FN, EL);
    return eq(xr, r) ? 0 : -1;
}
