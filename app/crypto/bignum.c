/* KerneliaOS buyuk sayi aritmetigi. Bkz. bignum.h.
   Modüler üs alma ve modüler çarpım Montgomery çarpımı (CIOS) ile yapılır. Saf C, libc yok. */
#include "bignum.h"

static int bn_cmp_raw(const uint32_t *a, const uint32_t *b, size_t len){
    for(size_t i = len; i-- > 0; ){
        if(a[i] != b[i]) return a[i] < b[i] ? -1 : 1;
    }
    return 0;
}

int bn_cmp(const uint32_t *a, const uint32_t *b, size_t len){
    return bn_cmp_raw(a, b, len);
}

/* r = a - b (mod 2^(32*len)). Sonuc a >= b ise gercek fark. r ile a ayni olabilir. */
static void bn_sub_raw(uint32_t *r, const uint32_t *a, const uint32_t *b, size_t len){
    uint64_t borrow = 0;
    for(size_t i = 0; i < len; i++){
        uint64_t d = (uint64_t)a[i] - b[i] - borrow;
        r[i] = (uint32_t)d;
        borrow = (d >> 63) & 1;   /* negatifse ust bit 1 olur */
    }
}

/* r = a + b (mod 2^(32*len)); tasma dondurulur */
static void bn_add_raw(uint32_t *r, const uint32_t *a, const uint32_t *b, size_t len){
    uint64_t carry = 0;
    for(size_t i = 0; i < len; i++){
        uint64_t s = (uint64_t)a[i] + b[i] + carry;
        r[i] = (uint32_t)s;
        carry = s >> 32;
    }
}

/* x = 2x mod m (x < m varsayilir) */
static void bn_dbl_mod(uint32_t *x, const uint32_t *m, size_t len){
    uint32_t carry = 0;
    for(size_t i = 0; i < len; i++){
        uint32_t nc = x[i] >> 31;
        x[i] = (x[i] << 1) | carry;
        carry = nc;
    }
    if(carry || bn_cmp_raw(x, m, len) >= 0) bn_sub_raw(x, x, m, len);
}

/* -m^{-1} mod 2^32 (m tek). Newton iterasyonu: her adimda bit sayisi ikiye katlanir. */
static uint32_t mont_n0(uint32_t m0){
    uint32_t inv = m0;
    for(int i = 0; i < 4; i++) inv *= 2u - m0 * inv;
    return (uint32_t)(0u - inv);
}

/* r = a * b * R^{-1} mod m, R = 2^(32*len). a, b < m. r ile a/b takma ad olabilir. */
static void mont_mul(uint32_t *r, const uint32_t *a, const uint32_t *b,
                     const uint32_t *m, size_t len, uint32_t n0){
    uint32_t t[BN_MAXLEN + 2];
    for(size_t i = 0; i < len + 2; i++) t[i] = 0;
    for(size_t i = 0; i < len; i++){
        uint64_t c = 0;
        for(size_t j = 0; j < len; j++){
            uint64_t s = (uint64_t)t[j] + (uint64_t)a[j] * b[i] + c;
            t[j] = (uint32_t)s;
            c = s >> 32;
        }
        uint64_t s = (uint64_t)t[len] + c;
        t[len] = (uint32_t)s;
        t[len + 1] = (uint32_t)(s >> 32);

        uint32_t u = t[0] * n0;
        c = ((uint64_t)t[0] + (uint64_t)u * m[0]) >> 32;
        for(size_t j = 1; j < len; j++){
            uint64_t s2 = (uint64_t)t[j] + (uint64_t)u * m[j] + c;
            t[j - 1] = (uint32_t)s2;
            c = s2 >> 32;
        }
        s = (uint64_t)t[len] + c;
        t[len - 1] = (uint32_t)s;
        t[len] = t[len + 1] + (uint32_t)(s >> 32);
    }
    /* t < 2m: gerekirse bir kez m cikar */
    if(t[len] != 0 || bn_cmp_raw(t, m, len) >= 0){
        bn_sub_raw(r, t, m, len);
    } else {
        for(size_t i = 0; i < len; i++) r[i] = t[i];
    }
}

int bn_from_be(uint32_t *out, size_t len, const uint8_t *in, size_t inlen){
    for(size_t i = 0; i < len; i++) out[i] = 0;
    for(size_t i = 0; i < inlen; i++){
        uint8_t b = in[inlen - 1 - i];      /* i. bayt, en dusuk bayttan say */
        size_t limb = i / 4;
        if(limb >= len){
            if(b != 0) return -1;
            continue;
        }
        out[limb] |= (uint32_t)b << (8 * (i % 4));
    }
    return 0;
}

void bn_to_be(uint8_t *out, size_t outlen, const uint32_t *a, size_t len){
    for(size_t i = 0; i < outlen; i++){
        size_t limb = i / 4;
        uint8_t b = limb < len ? (uint8_t)(a[limb] >> (8 * (i % 4))) : 0;
        out[outlen - 1 - i] = b;
    }
}

int bn_modexp(uint32_t *res, const uint32_t *base, const uint8_t *exp, size_t elen,
              const uint32_t *m, size_t len){
    if(len == 0 || len > BN_MAXLEN) return -1;
    if((m[0] & 1u) == 0) return -1;

    uint32_t one[BN_MAXLEN], r2[BN_MAXLEN], bm[BN_MAXLEN], acc[BN_MAXLEN];
    for(size_t i = 0; i < len; i++){ one[i] = 0; r2[i] = 0; }
    one[0] = 1;
    if(bn_cmp_raw(base, m, len) >= 0) return -1;
    /* m > 1 olmali */
    int m_is_one = (m[0] == 1);
    for(size_t i = 1; i < len && m_is_one; i++) if(m[i] != 0) m_is_one = 0;
    if(m_is_one) return -1;

    /* R^2 mod m: 1'den basla, 64*len kez ikiyle carp */
    r2[0] = 1;
    for(size_t i = 0; i < 64 * len; i++) bn_dbl_mod(r2, m, len);

    uint32_t n0 = mont_n0(m[0]);
    mont_mul(bm, base, r2, m, len, n0);      /* base Montgomery formunda */
    mont_mul(acc, r2, one, m, len, n0);      /* 1 Montgomery formunda (R mod m) */

    for(size_t i = 0; i < elen; i++){
        for(int bit = 7; bit >= 0; bit--){
            mont_mul(acc, acc, acc, m, len, n0);
            if((exp[i] >> bit) & 1) mont_mul(acc, acc, bm, m, len, n0);
        }
    }
    mont_mul(res, acc, one, m, len, n0);     /* Montgomery'den cik */
    return 0;
}

int bn_ctx_init(bn_ctx *c, const uint32_t *m, size_t len){
    if(len == 0 || len > BN_MAXLEN) return -1;
    if((m[0] & 1u) == 0) return -1;
    c->len = len;
    for(size_t i = 0; i < len; i++){ c->m[i] = m[i]; c->r2[i] = 0; }
    c->r2[0] = 1;
    for(size_t i = 0; i < 64 * len; i++) bn_dbl_mod(c->r2, c->m, len);
    c->n0 = mont_n0(m[0]);
    return 0;
}

void bn_mulmod(const bn_ctx *c, uint32_t *r, const uint32_t *a, const uint32_t *b){
    uint32_t t[BN_MAXLEN];
    mont_mul(t, a, b, c->m, c->len, c->n0);          /* a*b*R^-1 */
    mont_mul(r, t, c->r2, c->m, c->len, c->n0);      /* *R^2*R^-1 = a*b */
}

void bn_addmod(uint32_t *r, const uint32_t *a, const uint32_t *b, const uint32_t *m, size_t len){
    uint32_t t[BN_MAXLEN];
    uint64_t carry = 0;
    for(size_t i = 0; i < len; i++){
        uint64_t s = (uint64_t)a[i] + b[i] + carry;
        t[i] = (uint32_t)s;
        carry = s >> 32;
    }
    /* tasma varsa ya da t >= m ise bir kez m cikar */
    if(carry || bn_cmp_raw(t, m, len) >= 0) bn_sub_raw(t, t, m, len);
    for(size_t i = 0; i < len; i++) r[i] = t[i];
}

void bn_submod(uint32_t *r, const uint32_t *a, const uint32_t *b, const uint32_t *m, size_t len){
    uint32_t t[BN_MAXLEN];
    bn_sub_raw(t, a, b, len);
    if(bn_cmp_raw(a, b, len) < 0) bn_add_raw(t, t, m, len);   /* a < b: m ekle */
    for(size_t i = 0; i < len; i++) r[i] = t[i];
}
