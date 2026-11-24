/* SHA-256, HMAC-SHA256, AES ve AES-GCM icin host testleri (standart test vektorleri).
   Derleme: gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_crypto app/test/test_crypto.c && ./test_crypto */
#include <stdio.h>
#include <string.h>
#include "../crypto/sha256.c"
#include "../crypto/aes.c"
#include "../crypto/gcm.c"

static int fails = 0, oks = 0;

static void check(const char *name, int ok){
    if(ok){ oks++; }
    else { fails++; printf("BASARISIZ: %s\n", name); }
}

static int hex_eq(const uint8_t *b, size_t n, const char *hex){
    for(size_t i = 0; i < n; i++){
        unsigned v;
        if(sscanf(hex + 2*i, "%2x", &v) != 1) return 0;
        if(b[i] != (uint8_t)v) return 0;
    }
    return hex[2*n] == 0;
}

static void unhex(const char *hex, uint8_t *out, size_t n){
    for(size_t i = 0; i < n; i++){ unsigned v; sscanf(hex + 2*i, "%2x", &v); out[i] = (uint8_t)v; }
}

int main(void){
    uint8_t d[32];

    /* SHA-256 (FIPS 180-2 ornekleri) */
    sha256((const uint8_t *)"", 0, d);
    check("sha256 bos", hex_eq(d, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    sha256((const uint8_t *)"abc", 3, d);
    check("sha256 abc", hex_eq(d, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const char *lng = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256((const uint8_t *)lng, strlen(lng), d);
    check("sha256 448 bit", hex_eq(d, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    /* parcali guncelleme ayni sonucu vermeli */
    sha256_ctx c; sha256_init(&c);
    for(size_t i = 0; i < strlen(lng); i++) sha256_update(&c, (const uint8_t *)lng + i, 1);
    sha256_final(&c, d);
    check("sha256 parcali", hex_eq(d, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));

    /* HMAC-SHA256 RFC 4231 test 1 */
    uint8_t key[20]; memset(key, 0x0b, 20);
    hmac_sha256(key, 20, (const uint8_t *)"Hi There", 8, d);
    check("hmac rfc4231-1", hex_eq(d, 32, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"));
    /* RFC 4231 test 2: anahtar "Jefe" */
    hmac_sha256((const uint8_t *)"Jefe", 4, (const uint8_t *)"what do ya want for nothing?", 28, d);
    check("hmac rfc4231-2", hex_eq(d, 32, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"));

    /* AES FIPS-197 Ek C.1 (128 ve 256 bit) */
    aes_ctx a; uint8_t pt[16], ct[16], k16[16], k32[32];
    unhex("00112233445566778899aabbccddeeff", pt, 16);
    unhex("000102030405060708090a0b0c0d0e0f", k16, 16);
    check("aes128 init", aes_init(&a, k16, 16) == 0);
    aes_encrypt_block(&a, pt, ct);
    check("aes128 FIPS-197", hex_eq(ct, 16, "69c4e0d86a7b0430d8cdb78070b4c55a"));
    unhex("000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f", k32, 32);
    check("aes256 init", aes_init(&a, k32, 32) == 0);
    aes_encrypt_block(&a, pt, ct);
    check("aes256 FIPS-197", hex_eq(ct, 16, "8ea2b7ca516745bfeafc49904b496089"));
    check("aes gecersiz anahtar", aes_init(&a, k32, 24) == -1);

    /* AES-GCM test durumu 2 (GCM spec): anahtar 0, IV 0, pt 0^128 */
    uint8_t zk[16] = {0}, ziv[12] = {0}, zpt[16] = {0}, gct[16], gtag[16], back[16];
    check("gcm enc", gcm_encrypt(zk, 16, ziv, NULL, 0, zpt, 16, gct, gtag) == 0);
    check("gcm ct", hex_eq(gct, 16, "0388dace60b6a392f328c2b971b2fe78"));
    check("gcm tag", hex_eq(gtag, 16, "ab6e47d42cec13bdf53a67b21257bddf"));
    check("gcm dec", gcm_decrypt(zk, 16, ziv, NULL, 0, gct, 16, gtag, back) == 0 && memcmp(back, zpt, 16) == 0);
    gtag[0] ^= 1;
    check("gcm yanlis etiket reddedilir", gcm_decrypt(zk, 16, ziv, NULL, 0, gct, 16, gtag, back) == -1);
    gtag[0] ^= 1;

    /* AES-GCM: AAD'li ve kismi bloklu gidis-donus + AAD degisince red (bilinen cevap vektoru yok) */
    uint8_t k[16], iv[12], aad[20], p[60], c2[60], t2[16], b2[60];
    unhex("feffe9928665731c6d6a8f9467308308", k, 16);
    unhex("cafebabefacedbaddecaf888", iv, 12);
    unhex("feedfacedeadbeeffeedfacedeadbeefabaddad2", aad, 20);
    unhex("d9313225f88406e2a2a9e71d5a2f5a1f" "5e5b2e3e4bfa8a8e0c1f33a8af1f8e0a"
          "b8d7b2c3f1d7e7e1f7d2a6e4b8c9d0e1" "f2a3b4c5d6e7f8091a2b3c4d5e6f7081", p, 60);
    check("gcm enc2", gcm_encrypt(k, 16, iv, aad, 20, p, 60, c2, t2) == 0);
    check("gcm dec2", gcm_decrypt(k, 16, iv, aad, 20, c2, 60, t2, b2) == 0 && memcmp(b2, p, 60) == 0);
    aad[0] ^= 1;
    check("gcm aad degisince reddedilir", gcm_decrypt(k, 16, iv, aad, 20, c2, 60, t2, b2) == -1);

    printf("crypto: %d ok, %d basarisiz\n", oks, fails);
    return fails ? 1 : 0;
}
