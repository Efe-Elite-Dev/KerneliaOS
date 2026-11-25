/* HKDF (RFC 5869 test 1) ve TLS 1.3 kayit korumasi icin host testleri.
   Derleme: gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_tls app/test/test_tls.c && ./test_tls */
#include <stdio.h>
#include <string.h>
#include "../crypto/sha256.c"
#include "../crypto/aes.c"
#include "../crypto/gcm.c"
#include "../crypto/hkdf.c"
#include "../crypto/tls13.c"

static int oks = 0, fails = 0;
static void check(const char *n, int ok){ if(ok) oks++; else { fails++; printf("BASARISIZ: %s\n", n); } }
static void unhex(const char *hex, uint8_t *out, size_t n){ for(size_t i = 0; i < n; i++){ unsigned v; sscanf(hex + 2*i, "%2x", &v); out[i] = (uint8_t)v; } }

int main(void){
    /* RFC 5869 test 1 */
    uint8_t ikm[22], salt[13], info[10], prk[32], okm[42], want[42];
    memset(ikm, 0x0b, 22);
    unhex("000102030405060708090a0b0c", salt, 13);
    unhex("f0f1f2f3f4f5f6f7f8f9", info, 10);
    hkdf_extract(salt, 13, ikm, 22, prk);
    check("hkdf extract prk", memcmp(prk, (uint8_t[]){0x07,0x77,0x09,0x36,0x2c,0x2e,0x32,0xdf,0x0d,0xdc,0x3f,0x0d,0xc4,0x7b,0xba,0x63,0x90,0xb6,0xc7,0x3b,0xb5,0x0f,0x9c,0x31,0x22,0xec,0x84,0x4a,0xd7,0xc2,0xb3,0xe5}, 32) == 0);
    check("hkdf expand", hkdf_expand(prk, info, 10, okm, 42) == 0);
    unhex("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865", want, 42);
    check("hkdf RFC 5869 test 1", memcmp(okm, want, 42) == 0);

    /* TLS kayit: gidis-donus, sira no ve sahte kayit reddi */
    tls_keys snd = {{0}, {0}, 0}, rcv = {{0}, {0}, 0};
    for(int i = 0; i < 16; i++) snd.key[i] = rcv.key[i] = (uint8_t)(0x10 + i);
    for(int i = 0; i < 12; i++) snd.iv[i] = rcv.iv[i] = (uint8_t)(0xA0 + i);
    uint8_t msg[300], rec[400], got[400];
    for(int i = 0; i < 300; i++) msg[i] = (uint8_t)(i * 7);
    int rl = tls_seal(&snd, TLS_CT_APPDATA, msg, 300, rec, sizeof rec);
    check("seal uzunlugu", rl == 5 + 300 + 1 + 16);
    uint8_t ty = 0;
    int gl = tls_open(&rcv, rec, (size_t)rl, got, sizeof got, &ty);
    check("open uzunlugu", gl == 300);
    check("open icerik", gl == 300 && memcmp(got, msg, 300) == 0);
    check("open icerik tipi", ty == TLS_CT_APPDATA);

    /* ayni icerik ikinci kez farkli sifreli metin vermeli (sira no) */
    uint8_t rec2[400];
    int rl2 = tls_seal(&snd, TLS_CT_APPDATA, msg, 300, rec2, sizeof rec2);
    check("sira no degisir", rl2 == rl && memcmp(rec, rec2, (size_t)rl) != 0);
    check("ikinci kayit acilir", tls_open(&rcv, rec2, (size_t)rl2, got, sizeof got, &ty) == 300);

    /* bozulmus kayit reddedilir */
    tls_keys snd2 = snd, rcv2 = rcv;
    (void)snd2; (void)rcv2;
    uint8_t rec3[400];
    tls_keys a = {{0}, {0}, 0}, b = {{0}, {0}, 0};
    for(int i = 0; i < 16; i++) a.key[i] = b.key[i] = (uint8_t)i;
    for(int i = 0; i < 12; i++) a.iv[i] = b.iv[i] = (uint8_t)i;
    int l3 = tls_seal(&a, TLS_CT_HANDSHAKE, msg, 10, rec3, sizeof rec3);
    rec3[7] ^= 1;
    check("bozulmus kayit reddedilir", tls_open(&b, rec3, (size_t)l3, got, sizeof got, &ty) == -1);

    /* farkli sira no: acilmamali */
    tls_keys c = {{0}, {0}, 0};
    for(int i = 0; i < 16; i++) c.key[i] = (uint8_t)i;
    for(int i = 0; i < 12; i++) c.iv[i] = (uint8_t)i;
    int l4 = tls_seal(&a, TLS_CT_APPDATA, msg, 10, rec3, sizeof rec3);
    c.seq = 5;
    check("yanlis sira no reddedilir", tls_open(&c, rec3, (size_t)l4, got, sizeof got, &ty) == -1);

    /* bos icerik ve icerik tipi kontrolu */
    tls_keys d = {{0}, {0}, 0}, e = {{0}, {0}, 0};
    for(int i = 0; i < 16; i++) d.key[i] = e.key[i] = 0x33;
    for(int i = 0; i < 12; i++) d.iv[i] = e.iv[i] = 0x44;
    int l5 = tls_seal(&d, TLS_CT_HANDSHAKE, msg, 0, rec3, sizeof rec3);
    int g5 = tls_open(&e, rec3, (size_t)l5, got, sizeof got, &ty);
    check("bos icerik", g5 == 0 && ty == TLS_CT_HANDSHAKE);

    printf("tls/hkdf: %d ok, %d basarisiz\n", oks, fails);
    return fails ? 1 : 0;
}
