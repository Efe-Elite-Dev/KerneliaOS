/* KerneliaOS X.509 ayrıştırma. Bkz. x509.h */
#include "x509.h"
#include "rsa.h"
#include "ecdsa.h"
#include "sha256.h"

/* DER okuyucu: bir imlec (pointer + kalan uzunluk) */
typedef struct { const uint8_t *p; size_t n; } cur;

static int cur_peek(const cur *c){
    return c->n > 0 ? (int)c->p[0] : -1;
}

/* Bir TLV oku. full: TLV'nin tam baslangici, flen: tam uzunlugu (baslik dahil). */
static int cur_next(cur *c, uint8_t *tag, cur *val, const uint8_t **full, size_t *flen){
    if(c->n < 2) return -1;
    const uint8_t *q = c->p;
    size_t rem = c->n;
    size_t l = q[1], hl = 2;
    if(l & 0x80){
        size_t nb = l & 0x7F;
        if(nb == 0 || nb > 4 || rem < 2 + nb) return -1;
        l = 0;
        for(size_t i = 0; i < nb; i++) l = (l << 8) | q[2 + i];
        hl = 2 + nb;
    }
    if(rem < hl || rem - hl < l) return -1;
    *tag = q[0];
    val->p = q + hl;
    val->n = l;
    *full = q;
    *flen = hl + l;
    c->p = q + hl + l;
    c->n = rem - hl - l;
    return 0;
}

static int cur_expect(cur *c, uint8_t tag, cur *val){
    uint8_t t; const uint8_t *full; size_t flen;
    if(cur_next(c, &t, val, &full, &flen) != 0) return -1;
    return t == tag ? 0 : -1;
}

/* Tam TLV'yi (baslik dahil) ve icerigini dondurur */
static int cur_expect_full(cur *c, uint8_t tag, cur *val, const uint8_t **full, size_t *flen){
    uint8_t t;
    if(cur_next(c, &t, val, full, flen) != 0) return -1;
    return t == tag ? 0 : -1;
}

/* Sabit OID icerikleri (yalnizca deger baytlari) */
static const uint8_t OID_SHA256_RSA[9]   = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B};
static const uint8_t OID_SHA256_ECDSA[8] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02};
static const uint8_t OID_RSA_ENC[9]      = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
static const uint8_t OID_EC_PUB[7]       = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
static const uint8_t OID_P256[8]         = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
static const uint8_t OID_SAN[3]          = {0x55, 0x1D, 0x11};   /* 2.5.29.17 */

static int bytes_eq(const uint8_t *a, size_t al, const uint8_t *b, size_t bl){
    if(al != bl) return 0;
    for(size_t i = 0; i < al; i++) if(a[i] != b[i]) return 0;
    return 1;
}

int x509_parse(x509_cert *c, const uint8_t *der, size_t len){
    for(size_t i = 0; i < sizeof(x509_cert); i++) ((uint8_t *)c)[i] = 0;

    cur top = {der, len}, cert, tbs, sa, bits, spki, key, rsa, ext, exts, oid, ov;
    uint8_t t; const uint8_t *full; size_t flen;

    /* Certificate ::= SEQUENCE { tbs, sigAlg, sigValue } */
    if(cur_expect_full(&top, 0x30, &cert, &full, &flen) != 0) return -1;
    if(cur_expect_full(&cert, 0x30, &tbs, &full, &flen) != 0) return -1;
    c->tbs = full; c->tbs_len = flen;

    if(cur_expect(&cert, 0x30, &sa) != 0) return -1;
    if(cur_expect(&sa, 0x06, &oid) != 0) return -1;
    c->sha256_rsa   = bytes_eq(oid.p, oid.n, OID_SHA256_RSA, sizeof OID_SHA256_RSA);
    c->sha256_ecdsa = bytes_eq(oid.p, oid.n, OID_SHA256_ECDSA, sizeof OID_SHA256_ECDSA);
    if(!c->sha256_rsa && !c->sha256_ecdsa) return -1;

    if(cur_expect(&cert, 0x03, &bits) != 0 || bits.n < 1 || bits.p[0] != 0) return -1;
    c->sig = bits.p + 1; c->sig_len = bits.n - 1;

    /* TBSCertificate: [0] version (ops.), serial, sigAlg, issuer, validity, subject, spki, [3] ext */
    if(cur_peek(&tbs) == 0xA0){
        if(cur_next(&tbs, &t, &ext, &full, &flen) != 0) return -1;  /* version atlanir */
    }
    if(cur_expect(&tbs, 0x02, &ext) != 0) return -1;               /* serialNumber */
    if(cur_expect(&tbs, 0x30, &ext) != 0) return -1;               /* signature alg */
    if(cur_expect_full(&tbs, 0x30, &ext, &full, &flen) != 0) return -1;
    c->issuer = full; c->issuer_len = flen;
    if(cur_expect(&tbs, 0x30, &ext) != 0) return -1;               /* validity (denetlenmez) */
    if(cur_expect_full(&tbs, 0x30, &ext, &full, &flen) != 0) return -1;
    c->subject = full; c->subject_len = flen;

    /* SubjectPublicKeyInfo { algId, BIT STRING } */
    cur alg, curve;
    if(cur_expect(&tbs, 0x30, &spki) != 0) return -1;
    if(cur_expect(&spki, 0x30, &alg) != 0) return -1;
    if(cur_expect(&alg, 0x06, &oid) != 0) return -1;
    if(cur_expect(&spki, 0x03, &key) != 0 || key.n < 1 || key.p[0] != 0) return -1;
    if(bytes_eq(oid.p, oid.n, OID_RSA_ENC, sizeof OID_RSA_ENC)){
        rsa.p = key.p + 1; rsa.n = key.n - 1;                      /* RSAPublicKey */
        if(cur_expect(&rsa, 0x30, &ext) != 0) return -1;
        if(cur_expect(&ext, 0x02, &oid) != 0) return -1;           /* modulus */
        c->n = oid.p; c->n_len = oid.n;
        if(cur_expect(&ext, 0x02, &oid) != 0) return -1;           /* publicExponent */
        c->e = oid.p; c->e_len = oid.n;
    } else if(bytes_eq(oid.p, oid.n, OID_EC_PUB, sizeof OID_EC_PUB)){
        if(cur_expect(&alg, 0x06, &curve) != 0) return -1;         /* namedCurve */
        if(!bytes_eq(curve.p, curve.n, OID_P256, sizeof OID_P256)) return -1;
        c->ec = key.p + 1; c->ec_len = key.n - 1;                  /* 04 || X || Y */
    } else {
        return -1;
    }

    /* [3] EXPLICIT Extensions: SAN'i ara */
    if(cur_peek(&tbs) == 0xA3){
        if(cur_expect(&tbs, 0xA3, &ext) != 0) return -1;
        if(cur_expect(&ext, 0x30, &exts) != 0) return -1;
        while(cur_peek(&exts) == 0x30){
            if(cur_expect(&exts, 0x30, &ext) != 0) return -1;
            if(cur_expect(&ext, 0x06, &oid) != 0) return -1;
            if(cur_peek(&ext) == 0x01){                             /* critical */
                if(cur_expect(&ext, 0x01, &ov) != 0) return -1;
            }
            if(cur_expect(&ext, 0x04, &ov) != 0) return -1;         /* extnValue */
            if(bytes_eq(oid.p, oid.n, OID_SAN, sizeof OID_SAN)){
                cur gn;
                if(cur_expect(&ov, 0x30, &gn) != 0) return -1;
                c->san = gn.p; c->san_len = gn.n;
            }
        }
    }
    return 0;
}

int x509_verify_issuer(const x509_cert *child, const x509_cert *issuer){
    if(!bytes_eq(child->issuer, child->issuer_len, issuer->subject, issuer->subject_len)) return -1;
    if(child->sha256_rsa){
        if(!issuer->n || !issuer->e) return -1;
        return rsa_pkcs1_sha256_verify(issuer->n, issuer->n_len, issuer->e, issuer->e_len,
                                       child->sig, child->sig_len, child->tbs, child->tbs_len);
    }
    if(child->sha256_ecdsa){
        uint8_t h[32];
        if(!issuer->ec || issuer->ec_len != 65) return -1;
        sha256(child->tbs, child->tbs_len, h);
        return ecdsa_p256_verify(issuer->ec, child->sig, child->sig_len, h);
    }
    return -1;
}

static uint8_t lower(uint8_t c){ return (c >= 'A' && c <= 'Z') ? (uint8_t)(c + 32) : c; }

static int eq_ci(const uint8_t *a, size_t al, const uint8_t *b, size_t bl){
    if(al != bl) return 0;
    for(size_t i = 0; i < al; i++) if(lower(a[i]) != lower(b[i])) return 0;
    return 1;
}

/* pat: SAN dNSName (ornek: "*.wild.test" veya "example.test"); host: baglanilan ad */
static int name_match(const uint8_t *pat, size_t pl, const char *host, size_t hl){
    const uint8_t *h = (const uint8_t *)host;
    if(pl >= 2 && pat[0] == '*' && pat[1] == '.'){
        size_t dot = 0;
        while(dot < hl && h[dot] != '.') dot++;
        if(dot == 0 || dot >= hl) return 0;                       /* ilk etiket bos ya da tek etiket */
        /* kalan kisim (noktayla birlikte) joker sonrasiyla birebir eslesmeli */
        return eq_ci(pat + 1, pl - 1, h + dot, hl - dot);
    }
    return eq_ci(pat, pl, h, hl);
}

int x509_check_hostname(const x509_cert *c, const char *host, size_t hlen){
    if(!c->san || hlen == 0) return -1;
    cur g = {c->san, c->san_len};
    while(cur_peek(&g) >= 0){
        cur v; uint8_t t; const uint8_t *full; size_t flen;
        if(cur_next(&g, &t, &v, &full, &flen) != 0) return -1;
        if(t == 0x82 && name_match(v.p, v.n, host, hlen)) return 0;  /* dNSName [2] */
    }
    return -1;
}
