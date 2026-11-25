/* KerneliaOS TLS 1.3 istemci el sikismasi. Bkz. tls_hs.h */
#include "tls_hs.h"
#include "hkdf.h"
#include "x25519.h"
#include "rsa.h"
#include "ecdsa.h"

/* ---- yardimcilar ---- */
static size_t hs_slen(const char *s){ size_t n = 0; while(s[n]) n++; return n; }
static void hs_put16(uint8_t *p, uint32_t v){ p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static void hs_put24(uint8_t *p, uint32_t v){ p[0] = (uint8_t)(v >> 16); p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)v; }
static uint32_t hs_get16(const uint8_t *p){ return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t hs_get24(const uint8_t *p){ return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]; }
static void hs_copy(uint8_t *d, const uint8_t *s, size_t n){ for(size_t i = 0; i < n; i++) d[i] = s[i]; }
static int hs_ct_eq(const uint8_t *a, const uint8_t *b, size_t n){
    uint8_t d = 0;
    for(size_t i = 0; i < n; i++) d |= (uint8_t)(a[i] ^ b[i]);
    return d == 0;
}
static int hs_io_send(tls_hs *s, const uint8_t *b, size_t n){ return s->io.send(s->io.ctx, b, n); }
static int hs_io_recv(tls_hs *s, uint8_t *b, size_t n){ return s->io.recv(s->io.ctx, b, n); }

/* Transkript: mevcut ozet (ctx kopyasindan) */
static void hs_th(const tls_hs *s, uint8_t out[32]){
    sha256_ctx c = s->tr;
    sha256_final(&c, out);
}

/* ---- anahtar takvimi (RFC 8446 bolum 7.1) ---- */

/* HKDF-Expand-Label(secret, "tls13 " + label, context, L) */
static int hs_expand(const uint8_t sec[32], const char *label, const uint8_t *ctx, size_t clen,
                     uint8_t *out, size_t L){
    uint8_t info[54];
    size_t ll = hs_slen(label);
    if(ll > 12 || clen > 32) return -1;
    static const char pre[6] = {'t', 'l', 's', '1', '3', ' '};
    size_t p = 2;
    hs_put16(info, (uint32_t)L);
    info[p++] = (uint8_t)(6 + ll);
    for(size_t i = 0; i < 6; i++) info[p++] = (uint8_t)pre[i];
    for(size_t i = 0; i < ll; i++) info[p++] = (uint8_t)label[i];
    info[p++] = (uint8_t)clen;
    for(size_t i = 0; i < clen; i++) info[p++] = ctx[i];
    return hkdf_expand(sec, info, p, out, L);
}

/* Derive-Secret(sec, label, transkript) = HKDF-Expand-Label(sec, label, H, 32) */
static int hs_derive(const uint8_t sec[32], const char *label, const uint8_t th[32], uint8_t out[32]){
    return hs_expand(sec, label, th, 32, out, 32);
}

/* anahtar ve IV: key (16), iv (12); sira numarasi sifirlanir */
static int hs_keys(tls_keys *k, const uint8_t sec[32]){
    if(hs_expand(sec, "key", NULL, 0, k->key, 16) != 0) return -1;
    if(hs_expand(sec, "iv", NULL, 0, k->iv, 12) != 0) return -1;
    k->seq = 0;
    return 0;
}

/* finished anahtari: HKDF-Expand-Label(sec, "finished", "", 32) */
static int hs_finkey(const uint8_t sec[32], uint8_t fk[32]){
    return hs_expand(sec, "finished", NULL, 0, fk, 32);
}

/* ---- kayit katmani ---- */

/* Duz kayit gonder (ClientHello): 0x16 0x0301 uzunluk veri */
static int hs_send_plain(tls_hs *s, uint8_t type, const uint8_t *d, size_t n){
    uint8_t h[5] = {type, 0x03, 0x01, 0, 0};
    hs_put16(h + 3, (uint32_t)n);
    if(hs_io_send(s, h, 5) != 0) return -1;
    return hs_io_send(s, d, n);
}

/* Bir kayit okur. Duz kayitta (ServerHello) sadece 22; sifrelide icerik el sikisma (22) olmali.
   Handshake verisini hs tamponuna ekler. CCS (20) yoksayilir. */
static int hs_fetch(tls_hs *s){
    uint8_t *h = s->rec;
    if(hs_io_recv(s, h, 5) != 0) return -1;
    size_t rl = hs_get16(h + 3);
    if(rl == 0 || rl > TLS_MAX_PT + 256) return -1;
    if(hs_io_recv(s, h + 5, rl) != 0) return -1;
    if(h[0] == 20) return 0;                       /* change_cipher_spec: uyumluluk icin yoksay */

    const uint8_t *plain; size_t pl;
    if(!s->rd_encrypted){
        if(h[0] != 22) return -1;
        plain = h + 5; pl = rl;
    } else {
        if(h[0] != 23) return -1;
        uint8_t pt;
        int n = tls_open(&s->rd, h, 5 + rl, s->pt, sizeof s->pt, &pt);
        if(n < 0 || pt != 22) return -1;
        plain = s->pt; pl = (size_t)n;
    }
    /* tampon: tuketilmis kismi at, sonra ekle */
    if(s->hs_pos > 0){
        size_t avail = s->hs_len - s->hs_pos;
        hs_copy(s->hs, s->hs + s->hs_pos, avail);
        s->hs_len = avail;
        s->hs_pos = 0;
    }
    if(s->hs_len + pl > TLS_HS_BUF) return -1;
    hs_copy(s->hs + s->hs_len, plain, pl);
    s->hs_len += pl;
    return 0;
}

/* Sonraki tam el sikisma mesajini (baslik dahil) verir. Mesaj, sonraki hs_fetch'e kadar gecerlidir. */
static int hs_next(tls_hs *s, const uint8_t **msg, size_t *mlen){
    for(;;){
        size_t avail = s->hs_len - s->hs_pos;
        if(avail >= 4){
            size_t total = 4 + hs_get24(s->hs + s->hs_pos + 1);
            if(total > TLS_HS_BUF) return -1;
            if(avail >= total){
                *msg = s->hs + s->hs_pos;
                *mlen = total;
                s->hs_pos += total;
                return 0;
            }
        }
        if(hs_fetch(s) != 0) return -1;
    }
}

/* ---- ClientHello ---- */
static int hs_build_ch(tls_hs *s, const char *host, size_t hlen, const uint8_t entropy[64],
                       uint8_t *ch, size_t *chlen){
    uint8_t b[400];
    size_t p = 0;
    hs_put16(b + p, 0x0303); p += 2;                         /* legacy_version */
    hs_copy(b + p, entropy, 32); p += 32;                    /* random */
    b[p++] = 0;                                              /* legacy_session_id (bos) */
    hs_put16(b + p, 2); p += 2; b[p++] = 0x13; b[p++] = 0x01; /* TLS_AES_128_GCM_SHA256 */
    b[p++] = 1; b[p++] = 0;                                  /* sikistirma: yok */

    size_t ext_len_pos = p; p += 2;
    size_t ext_start = p;
    /* server_name: list_len(2)=3+hlen, name_type 0, name_len(2)=hlen */
    hs_put16(b + p, 0x0000); p += 2;
    hs_put16(b + p, 5 + hlen); p += 2;
    hs_put16(b + p, 3 + hlen); p += 2;
    b[p++] = 0;
    hs_put16(b + p, (uint32_t)hlen); p += 2;
    hs_copy(b + p, (const uint8_t *)host, hlen); p += hlen;
    /* supported_versions: TLS 1.3 */
    hs_put16(b + p, 0x002B); p += 2; hs_put16(b + p, 3); p += 2; b[p++] = 2; hs_put16(b + p, 0x0304); p += 2;
    /* supported_groups: x25519 */
    hs_put16(b + p, 0x000A); p += 2; hs_put16(b + p, 4); p += 2; hs_put16(b + p, 2); p += 2; hs_put16(b + p, 0x001D); p += 2;
    /* signature_algorithms: rsa_pss_rsae_sha256, ecdsa_secp256r1_sha256, rsa_pkcs1_sha256 */
    hs_put16(b + p, 0x000D); p += 2; hs_put16(b + p, 8); p += 2; hs_put16(b + p, 6); p += 2;
    hs_put16(b + p, 0x0804); p += 2; hs_put16(b + p, 0x0403); p += 2; hs_put16(b + p, 0x0401); p += 2;
    /* key_share: x25519 */
    hs_put16(b + p, 0x0033); p += 2; hs_put16(b + p, 38); p += 2; hs_put16(b + p, 36); p += 2;
    hs_put16(b + p, 0x001D); p += 2; hs_put16(b + p, 32); p += 2;
    hs_copy(b + p, s->pub, 32); p += 32;
    hs_put16(b + ext_len_pos, (uint32_t)(p - ext_start));

    ch[0] = 1;                                               /* ClientHello */
    hs_put24(ch + 1, (uint32_t)p);
    hs_copy(ch + 4, b, p);
    *chlen = 4 + p;
    return 0;
}

/* ---- ServerHello ---- */
static int hs_parse_sh(tls_hs *s, const uint8_t *m, size_t ml){
    const uint8_t *b = m + 4;
    size_t bl = ml - 4;
    if(bl < 2 + 32 + 1) return -1;
    size_t p = 2 + 32;
    size_t sidl = b[p]; p += 1 + sidl;
    if(p + 2 + 1 + 2 > bl) return -1;
    if(hs_get16(b + p) != 0x1301) return -1;                 /* TLS_AES_128_GCM_SHA256 */
    p += 2 + 1;                                              /* sifre, sikistirma */
    size_t el = hs_get16(b + p); p += 2;
    if(p + el != bl) return -1;
    int got_ver = 0, got_ks = 0;
    size_t q = p;
    while(q < bl){
        if(q + 4 > bl) return -1;
        uint32_t et = hs_get16(b + q), ed = hs_get16(b + q + 2);
        q += 4;
        if(q + ed > bl) return -1;
        if(et == 0x002B){                                    /* supported_versions */
            if(ed != 2 || hs_get16(b + q) != 0x0304) return -1;
            got_ver = 1;
        } else if(et == 0x0033){                             /* key_share */
            if(ed < 4 || hs_get16(b + q) != 0x001D) return -1;
            size_t kl = hs_get16(b + q + 2);
            if(kl != 32 || 4 + kl != ed) return -1;
            hs_copy(s->srv_pub, b + q + 4, 32);
            got_ks = 1;
        }
        q += ed;
    }
    return (got_ver && got_ks) ? 0 : -1;
}

/* ---- Certificate ---- */
static int hs_process_cert(tls_hs *s, const uint8_t *b, size_t bl, const char *host, size_t hlen){
    if(bl < 4) return -1;
    size_t p = 1 + b[0];                                     /* certificate_request_context */
    if(p + 3 > bl) return -1;
    size_t ll = hs_get24(b + p); p += 3;
    if(p + ll != bl) return -1;
    size_t end = p + ll;
    x509_cert certs[4];
    size_t n = 0;
    while(p < end){
        if(n >= 4 || p + 3 > end) return -1;
        size_t cl = hs_get24(b + p); p += 3;
        if(p + cl > end) return -1;
        if(x509_parse(&certs[n], b + p, cl) != 0) return -1;
        n++;
        p += cl;
        if(p + 2 > end) return -1;
        size_t el = hs_get16(b + p); p += 2;
        if(p + el > end) return -1;
        p += el;
    }
    if(n == 0) return -1;
    for(size_t i = 0; i + 1 < n; i++){
        if(x509_verify_issuer(&certs[i], &certs[i + 1]) != 0) return -1;
    }
    if(x509_verify_issuer(&certs[n - 1], &s->trust) != 0) return -1;   /* zincir guvenilen CA'ya baglanir */
    if(x509_check_hostname(&certs[0], host, hlen) != 0) return -1;

    /* yaprak anahtari (CertificateVerify icin sakla) */
    if(certs[0].ec){
        if(certs[0].ec_len != 65) return -1;
        s->leaf_ec = 1;
        hs_copy(s->leaf_ec_pub, certs[0].ec, 65);
    } else {
        if(!certs[0].n || !certs[0].e) return -1;
        if(certs[0].n_len > sizeof s->leaf_n || certs[0].e_len > sizeof s->leaf_e) return -1;
        s->leaf_ec = 0;
        s->leaf_n_len = certs[0].n_len;
        hs_copy(s->leaf_n, certs[0].n, certs[0].n_len);
        s->leaf_e_len = certs[0].e_len;
        hs_copy(s->leaf_e, certs[0].e, certs[0].e_len);
    }
    return 0;
}

/* ---- CertificateVerify ---- */
static int hs_verify_cv(tls_hs *s, const uint8_t *b, size_t bl, const uint8_t th[32]){
    if(bl < 4) return -1;
    uint32_t alg = hs_get16(b);
    size_t sl = hs_get16(b + 2);
    if(4 + sl != bl) return -1;
    const uint8_t *sig = b + 4;

    /* icerik: 64 x 0x20 || "TLS 1.3, server CertificateVerify" || 0x00 || transkript */
    uint8_t content[160];
    size_t p = 0;
    for(size_t i = 0; i < 64; i++) content[p++] = 0x20;
    static const char ctx[] = "TLS 1.3, server CertificateVerify";
    for(size_t i = 0; i + 1 < sizeof ctx; i++) content[p++] = (uint8_t)ctx[i];
    content[p++] = 0x00;
    hs_copy(content + p, th, 32); p += 32;

    if(alg == 0x0804 && !s->leaf_ec){                        /* rsa_pss_rsae_sha256 */
        return rsa_pss_sha256_verify(s->leaf_n, s->leaf_n_len, s->leaf_e, s->leaf_e_len,
                                     sig, sl, content, p) == 0 ? 0 : -1;
    }
    if(alg == 0x0403 && s->leaf_ec){                         /* ecdsa_secp256r1_sha256 */
        uint8_t h[32];
        sha256(content, p, h);
        return ecdsa_p256_verify(s->leaf_ec_pub, sig, sl, h) == 0 ? 0 : -1;
    }
    return -1;
}

/* ---- el sikisma akisi ---- */
int tls_hs_connect(tls_hs *s, const tls_io *io, const char *host, size_t hlen,
                   const uint8_t *ca_der, size_t ca_len, const uint8_t entropy[64]){
    if(hlen == 0 || hlen > 128) return -1;
    s->io = *io;
    s->hs_len = 0; s->hs_pos = 0;
    s->rd_encrypted = 0;
    s->leaf_ec = 0; s->leaf_n_len = 0; s->leaf_e_len = 0;
    sha256_init(&s->tr);
    if(x509_parse(&s->trust, ca_der, ca_len) != 0) return -1;

    /* ECDHE anahtari: entropy[32..63] ozel anahtar */
    hs_copy(s->priv, entropy + 32, 32);
    x25519_public(s->pub, s->priv);

    /* ClientHello */
    uint8_t ch[512]; size_t chl = 0;
    if(hs_build_ch(s, host, hlen, entropy, ch, &chl) != 0) return -1;
    sha256_update(&s->tr, ch, chl);
    if(hs_send_plain(s, 22, ch, chl) != 0) return -1;

    /* ServerHello (duz kayit) */
    const uint8_t *m; size_t ml;
    if(hs_next(s, &m, &ml) != 0 || m[0] != 2) return -1;
    if(hs_parse_sh(s, m, ml) != 0) return -1;
    sha256_update(&s->tr, m, ml);

    /* ECDH: paylasilan sir (dusuk dereceli nokta reddedilir) */
    uint8_t shared[32], zero[32] = {0};
    if(x25519(shared, s->priv, s->srv_pub) != 0) return -1;
    if(hs_ct_eq(shared, zero, 32)) return -1;

    /* El sikisma anahtarlari */
    uint8_t zeros[32] = {0}, early[32], derived[32], eh[32], th[32], c_sec[32], s_sec[32];
    uint8_t master_der[32], master[32], c_ap[32], s_ap[32], tsf[32];
    hkdf_extract(NULL, 0, zeros, 32, early);
    sha256((const uint8_t *)"", 0, eh);
    if(hs_derive(early, "derived", eh, derived) != 0) return -1;
    hkdf_extract(derived, 32, shared, 32, s->hs_sec);
    hs_th(s, th);
    if(hs_derive(s->hs_sec, "c hs traffic", th, c_sec) != 0) return -1;
    if(hs_derive(s->hs_sec, "s hs traffic", th, s_sec) != 0) return -1;
    if(hs_keys(&s->wr, c_sec) != 0 || hs_keys(&s->rd, s_sec) != 0) return -1;
    if(hs_finkey(c_sec, s->c_fin) != 0 || hs_finkey(s_sec, s->s_fin) != 0) return -1;
    s->rd_encrypted = 1;

    /* EncryptedExtensions, Certificate, CertificateVerify, Finished */
    int got_fin = 0;
    while(!got_fin){
        if(hs_next(s, &m, &ml) != 0) return -1;
        const uint8_t *body = m + 4;
        size_t bl = ml - 4;
        if(m[0] == 8){                                       /* EncryptedExtensions */
            /* icerigi bu surumde kullanilmiyor */
        } else if(m[0] == 11){                               /* Certificate */
            if(hs_process_cert(s, body, bl, host, hlen) != 0) return -1;
        } else if(m[0] == 15){                               /* CertificateVerify */
            uint8_t tc[32];
            hs_th(s, tc);                                    /* Certificate'a kadar olan transkript */
            if(hs_verify_cv(s, body, bl, tc) != 0) return -1;
        } else if(m[0] == 20){                               /* Finished */
            if(bl != 32) return -1;
            uint8_t tc[32], expect[32];
            hs_th(s, tc);                                    /* CertificateVerify'a kadar */
            hmac_sha256(s->s_fin, 32, tc, 32, expect);
            if(!hs_ct_eq(expect, body, 32)) return -1;
            got_fin = 1;
        } else {
            return -1;
        }
        sha256_update(&s->tr, m, ml);
    }

    /* Ana sir ve uygulama trafik anahtarlari (transkript: sunucu Finished dahil) */
    hs_th(s, tsf);
    sha256((const uint8_t *)"", 0, eh);
    if(hs_derive(s->hs_sec, "derived", eh, master_der) != 0) return -1;
    hkdf_extract(master_der, 32, zeros, 32, master);
    if(hs_derive(master, "c ap traffic", tsf, c_ap) != 0) return -1;
    if(hs_derive(master, "s ap traffic", tsf, s_ap) != 0) return -1;

    /* istemci Finished: HMAC(c_fin, transkript), el sikisma anahtariyla sifreli */
    uint8_t fin[36];
    fin[0] = 20; hs_put24(fin + 1, 32);
    hmac_sha256(s->c_fin, 32, tsf, 32, fin + 4);
    int rl = tls_seal(&s->wr, 22, fin, sizeof fin, s->rec, sizeof s->rec);
    if(rl < 0) return -1;
    if(hs_io_send(s, s->rec, (size_t)rl) != 0) return -1;

    /* uygulama anahtarlari; sira numaralari sifirlanir */
    if(hs_keys(&s->wr, c_ap) != 0 || hs_keys(&s->rd, s_ap) != 0) return -1;
    return 0;
}

int tls_hs_write(tls_hs *s, const uint8_t *data, size_t n){
    if(n > TLS_MAX_PT) return -1;
    int rl = tls_seal(&s->wr, 23, data, n, s->rec, sizeof s->rec);
    if(rl < 0) return -1;
    return hs_io_send(s, s->rec, (size_t)rl);
}

int tls_hs_read(tls_hs *s, uint8_t *buf, size_t cap){
    for(;;){
        if(hs_io_recv(s, s->rec, 5) != 0) return -1;
        size_t rl = hs_get16(s->rec + 3);
        if(rl == 0 || rl > TLS_MAX_PT + 256) return -1;
        if(hs_io_recv(s, s->rec + 5, rl) != 0) return -1;
        if(s->rec[0] == 20) continue;
        if(s->rec[0] != 23) return -1;
        uint8_t pt;
        int n = tls_open(&s->rd, s->rec, 5 + rl, s->pt, sizeof s->pt, &pt);
        if(n < 0) return -1;
        if(pt == 22) continue;                               /* yeni oturum bileti vb.: yoksay */
        if(pt == 21) return 0;                               /* uyari: kapanis */
        if(pt != 23) return -1;
        if((size_t)n > cap) return -1;
        hs_copy(buf, s->pt, (size_t)n);
        return n;
    }
}
