/* Buyuk sayi ve RSA-PKCS#1 v1.5 (SHA-256) dogrulama testleri.
   Vektorler OpenSSL ile uretildi (rsa_vectors.h).
   Derleme: gcc -std=c99 -Wall -Wextra -Werror -Wno-unused-function -o test_rsa app/test/test_rsa.c && ./test_rsa */
#include <stdio.h>
#include <string.h>
#include "../crypto/bignum.c"
#include "../crypto/sha256.c"
#include "../crypto/rsa.c"
#include "rsa_vectors.h"

static int fails = 0, oks = 0;

static void check(const char *name, int ok){
    if(ok){ oks++; }
    else { fails++; printf("BASARISIZ: %s\n", name); }
}

int main(void){
    /* Bignum: klasik 4^13 mod 497 = 445 */
    uint32_t m1[1] = {497}, b1[1] = {4}, r1[1];
    uint8_t e13[1] = {13};
    check("modexp 4^13 mod 497", bn_modexp(r1, b1, e13, 1, m1, 1) == 0 && r1[0] == 445);
    /* Cift modul reddedilir */
    uint32_t m2[1] = {496};
    check("modexp cift modul reddedilir", bn_modexp(r1, b1, e13, 1, m2, 1) == -1);

    /* Dogru imzalar (OpenSSL) */
    check("imza 1 gecerli", rsa_pkcs1_sha256_verify(RSA_N, sizeof RSA_N, RSA_E, sizeof RSA_E,
                                                   SIG1, sizeof SIG1, MSG1, sizeof MSG1) == 0);
    check("imza 2 gecerli", rsa_pkcs1_sha256_verify(RSA_N, sizeof RSA_N, RSA_E, sizeof RSA_E,
                                                   SIG2, sizeof SIG2, MSG2, sizeof MSG2) == 0);

    /* Yanlis imza: baska mesajin imzasi */
    check("yanlis mesaj reddedilir", rsa_pkcs1_sha256_verify(RSA_N, sizeof RSA_N, RSA_E, sizeof RSA_E,
                                                            SIG1, sizeof SIG1, MSG2, sizeof MSG2) == -1);

    /* Mesajin tek bitini boz */
    uint8_t msg[sizeof MSG1];
    memcpy(msg, MSG1, sizeof MSG1);
    msg[3] ^= 0x01;
    check("bozuk mesaj reddedilir", rsa_pkcs1_sha256_verify(RSA_N, sizeof RSA_N, RSA_E, sizeof RSA_E,
                                                           SIG1, sizeof SIG1, msg, sizeof msg) == -1);

    /* Imzanin tek bitini boz (orta bayt) */
    uint8_t sig[sizeof SIG1];
    memcpy(sig, SIG1, sizeof SIG1);
    sig[sizeof sig / 2] ^= 0x10;
    check("bozuk imza reddedilir", rsa_pkcs1_sha256_verify(RSA_N, sizeof RSA_N, RSA_E, sizeof RSA_E,
                                                          sig, sizeof sig, MSG1, sizeof MSG1) == -1);

    /* s >= n: tum 0xFF */
    uint8_t big[sizeof SIG1];
    memset(big, 0xFF, sizeof big);
    check("s >= n reddedilir", rsa_pkcs1_sha256_verify(RSA_N, sizeof RSA_N, RSA_E, sizeof RSA_E,
                                                      big, sizeof big, MSG1, sizeof MSG1) == -1);

    /* Imza uzunlugu modulle uyusmuyor */
    check("kisa imza reddedilir", rsa_pkcs1_sha256_verify(RSA_N, sizeof RSA_N, RSA_E, sizeof RSA_E,
                                                         SIG1, sizeof SIG1 - 1, MSG1, sizeof MSG1) == -1);

    /* Yanlis us: 3 yerine 0x010001 yerine 0x03 */
    uint8_t e3[1] = {3};
    check("yanlis us reddedilir", rsa_pkcs1_sha256_verify(RSA_N, sizeof RSA_N, e3, 1,
                                                         SIG1, sizeof SIG1, MSG1, sizeof MSG1) == -1);

    printf("rsa: %d ok, %d basarisiz\n", oks, fails);
    return fails ? 1 : 0;
}
