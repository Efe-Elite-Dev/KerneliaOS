/* KerneliaOS X25519 (RFC 7748): Curve25519 uzerinde Diffie-Hellman anahtar degisimi.
   Alan aritmetigi: 16 limb (her biri 16 bit tabani), TweetNaCl tarzinda sabit zamanli Montgomery merdiveni.
   Saf C, donanim bagimliligi yok. */
#include <stdint.h>
#include <stddef.h>

typedef long long gf[16];

static const gf GF_121665 = {0xDB41, 1};

static void car25519(gf o){
    for(int i = 0; i < 16; i++){
        o[i] += (1LL << 16);
        long long c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15);
        o[i] -= c << 16;
    }
}

/* b == 1 ise p ve q degisir; sabit zamanli */
static void sel25519(gf p, gf q, int b){
    long long c = ~((long long)b - 1);
    for(int i = 0; i < 16; i++){
        long long t = c & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

static void pack25519(uint8_t *o, const gf n){
    gf m, t;
    for(int i = 0; i < 16; i++) t[i] = n[i];
    car25519(t); car25519(t); car25519(t);
    for(int j = 0; j < 2; j++){
        m[0] = t[0] - 0xffed;
        for(int i = 1; i < 15; i++){
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        sel25519(t, m, 1 - b);
    }
    for(int i = 0; i < 16; i++){
        o[2 * i]     = (uint8_t)(t[i] & 0xff);
        o[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

static void unpack25519(gf o, const uint8_t *n){
    for(int i = 0; i < 16; i++) o[i] = n[2 * i] + ((long long)n[2 * i + 1] << 8);
    o[15] &= 0x7fff;
}

static void A(gf o, const gf a, const gf b){ for(int i = 0; i < 16; i++) o[i] = a[i] + b[i]; }
static void Z(gf o, const gf a, const gf b){ for(int i = 0; i < 16; i++) o[i] = a[i] - b[i]; }

static void M(gf o, const gf a, const gf b){
    long long t[31];
    for(int i = 0; i < 31; i++) t[i] = 0;
    for(int i = 0; i < 16; i++)
        for(int j = 0; j < 16; j++) t[i + j] += a[i] * b[j];
    for(int i = 0; i < 15; i++) t[i] += 38 * t[i + 16];
    for(int i = 0; i < 16; i++) o[i] = t[i];
    car25519(o);
    car25519(o);
}

static void S(gf o, const gf a){ M(o, a, a); }

/* o = i^(p-2) (ters) */
static void inv25519(gf o, const gf i){
    gf c;
    for(int a = 0; a < 16; a++) c[a] = i[a];
    for(int a = 253; a >= 0; a--){
        S(c, c);
        if(a != 2 && a != 4) M(c, c, i);
    }
    for(int a = 0; a < 16; a++) o[a] = c[a];
}

/* q = n * p (x-koordinati). n: 32 bayt olcekleyici (clamp burada uygulanir). */
static void scalarmult(uint8_t *q, const uint8_t *n, const uint8_t *p){
    uint8_t z[32];
    gf x, a, b, c, d, e, f;
    for(int i = 0; i < 32; i++) z[i] = n[i];
    z[31] = (uint8_t)((n[31] & 127) | 64);
    z[0] &= 248;
    unpack25519(x, p);
    for(int i = 0; i < 16; i++){ b[i] = x[i]; d[i] = a[i] = c[i] = 0; }
    a[0] = d[0] = 1;
    for(int i = 254; i >= 0; i--){
        int r = (z[i >> 3] >> (i & 7)) & 1;
        sel25519(a, b, r);
        sel25519(c, d, r);
        A(e, a, c); Z(a, a, c); A(c, b, d); Z(b, b, d);
        S(d, e); S(f, a); M(a, c, a); M(c, b, e);
        A(e, a, c); Z(a, a, c); S(b, a); Z(c, d, f);
        M(a, c, GF_121665); A(a, a, d); M(c, c, a); M(a, d, f);
        M(d, b, x); S(b, e);
        sel25519(a, b, r);
        sel25519(c, d, r);
    }
    inv25519(c, c);
    M(a, a, c);
    pack25519(q, a);
}

/* Donus: 0. q (32 bayt) = paylasilan sir (ya da genel anahtar, p = baz nokta). */
int x25519(uint8_t q[32], const uint8_t n[32], const uint8_t p[32]){
    scalarmult(q, n, p);
    return 0;
}

/* Genel anahtar: baz nokta u=9 ile carpim */
void x25519_public(uint8_t pub[32], const uint8_t priv[32]){
    static const uint8_t base[32] = {9};
    scalarmult(pub, priv, base);
}
